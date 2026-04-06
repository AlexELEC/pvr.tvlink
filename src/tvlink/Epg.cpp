/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Epg.h"

#include "Settings.h"
#include "utilities/FileUtils.h"
#include "utilities/Logger.h"
#include "utilities/XMLUtils.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <thread>

#include <kodi/tools/StringUtils.h>
#include <pugixml.hpp>

using namespace kodi::tools;
using namespace tvlink;
using namespace tvlink::data;
using namespace tvlink::utilities;
using namespace pugi;

namespace
{
constexpr const char PARSED_EPG_CACHE_MAGIC[] = "TVLEPG3";
constexpr std::int64_t PARSED_EPG_CACHE_VERSION = 3;
constexpr time_t PARSED_EPG_CACHE_FUTURE_TOLERANCE_SECONDS = 6 * 60 * 60;

template<typename T>
bool WriteBinary(std::ostream& stream, const T& value)
{
  stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
  return stream.good();
}

template<typename T>
bool ReadBinary(std::istream& stream, T& value)
{
  stream.read(reinterpret_cast<char*>(&value), sizeof(T));
  return stream.good();
}

bool WriteString(std::ostream& stream, const std::string& value)
{
  std::uint32_t size = static_cast<std::uint32_t>(value.size());
  if (!WriteBinary(stream, size))
    return false;

  if (size > 0)
    stream.write(value.data(), size);

  return stream.good();
}

bool ReadString(std::istream& stream, std::string& value)
{
  std::uint32_t size = 0;
  if (!ReadBinary(stream, size))
    return false;

  value.resize(size);
  if (size > 0)
    stream.read(&value[0], size);

  return stream.good();
}

void HashCombine(std::uint64_t& hash, const std::string& value)
{
  for (unsigned char c : value)
  {
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
}

void HashCombine(std::uint64_t& hash, int value)
{
  HashCombine(hash, std::to_string(value));
}

} // unnamed namespace

Epg::Epg(kodi::addon::CInstancePVRClient* client, Channels& channels)
  : m_lastStart(0), m_lastEnd(0), m_channels(channels), m_client(client)
{
}

bool Epg::Init(int epgMaxPastDays, int epgMaxFutureDays)
{
  m_xmltvLocation = Settings::GetInstance().GetEpgLocation();
  m_epgTimeShift = Settings::GetInstance().GetEpgTimeshiftSecs();
  m_tsOverride = Settings::GetInstance().GetTsOverride();

  SetEPGMaxPastDays(epgMaxPastDays);
  SetEPGMaxFutureDays(epgMaxFutureDays);

  // For catchup (which always enabled) we need a local store of the EPG data. Kodi may
  // not load the data on each startup so we need to make sure it's loaded whether or not
  // kodi considers it necessary.
  time_t now = std::time(nullptr);
  time_t start = now - m_epgMaxPastDaysSeconds;
  time_t end   = now + m_epgMaxFutureDaysSeconds;
  
  if (!TryLoadParsedCache(start, end))
  {
    if (LoadEPG(start, end))
    {
      // Prevent duplicate EPG loading on first GetEPGForChannel()
      m_lastStart = start;
      m_lastEnd   = end;
    }
  }

  return true;
}

void Epg::Clear()
{
  m_channelEpgs.clear();
  m_channelEpgIndex.clear();
  m_genreMappings.clear();
}

void Epg::SetEPGMaxPastDays(int epgMaxPastDays)
{
  m_epgMaxPastDays = epgMaxPastDays;

  if (m_epgMaxPastDays > EPG_TIMEFRAME_UNLIMITED)
    m_epgMaxPastDaysSeconds = m_epgMaxPastDays * 24 * 60 * 60;
  else
    m_epgMaxPastDaysSeconds = DEFAULT_EPG_MAX_DAYS * 24 * 60 * 60;
}

void Epg::SetEPGMaxFutureDays(int epgMaxFutureDays)
{
  m_epgMaxFutureDays = epgMaxFutureDays;

  if (m_epgMaxFutureDays > EPG_TIMEFRAME_UNLIMITED)
    m_epgMaxFutureDaysSeconds = m_epgMaxFutureDays * 24 * 60 * 60;
  else
    m_epgMaxFutureDaysSeconds = DEFAULT_EPG_MAX_DAYS * 24 * 60 * 60;
}

bool Epg::ShouldUseParsedCache() const
{
  if (m_xmltvLocation.empty())
    return false;

  std::string flag;
  if (FileUtils::GetFileContents(m_xmltvLocation + ".status", flag))
  {
    StringUtils::Trim(flag);
    if (flag == "Updated")
      return true;

    Logger::Log(LEVEL_INFO, "%s - Parsed EPG cache skipped: xmltv.status says XMLTV changed", __FUNCTION__);
    return false;
  }

  Logger::Log(LEVEL_INFO, "%s - xmltv.status unavailable, parsed EPG cache will be used if present", __FUNCTION__);
  return true;
}

std::uint64_t Epg::GetChannelsSignature() const
{
  std::uint64_t hash = 1469598103934665603ULL;

  for (const auto& channel : m_channels.GetChannelsList())
  {
    HashCombine(hash, channel.GetUniqueId());
    HashCombine(hash, channel.GetTvgId());
    HashCombine(hash, channel.GetTvgName());
    HashCombine(hash, channel.GetChannelName());
    HashCombine(hash, channel.GetTvgShift());
  }

  return hash;
}

bool Epg::TryLoadParsedCache(time_t start, time_t end)
{
  if (!ShouldUseParsedCache())
    return false;

  const std::string cachePath = FileUtils::GetUserDataAddonFilePath(PARSED_EPG_CACHE_FILENAME);
  std::ifstream input(cachePath, std::ios::binary);
  if (!input.is_open())
    return false;

  char magic[sizeof(PARSED_EPG_CACHE_MAGIC)] = {};
  input.read(magic, sizeof(magic));
  if (!input.good() || std::string(magic, sizeof(magic)) != std::string(PARSED_EPG_CACHE_MAGIC, sizeof(PARSED_EPG_CACHE_MAGIC)))
    return false;

  std::int64_t version = 0;
  std::int64_t cachedStart = 0;
  std::int64_t cachedEnd = 0;
  std::int32_t epgTimeShift = 0;
  std::uint8_t tsOverride = 0;
  std::uint64_t channelsSignature = 0;
  std::uint32_t channelCount = 0;

  if (!ReadBinary(input, version) ||
      !ReadBinary(input, cachedStart) ||
      !ReadBinary(input, cachedEnd) ||
      !ReadBinary(input, epgTimeShift) ||
      !ReadBinary(input, tsOverride) ||
      !ReadBinary(input, channelsSignature) ||
      !ReadBinary(input, channelCount))
  {
    return false;
  }

  if (version != PARSED_EPG_CACHE_VERSION)
    return false;

  if (epgTimeShift != m_epgTimeShift || static_cast<bool>(tsOverride) != m_tsOverride)
    return false;

  if (channelsSignature != GetChannelsSignature())
    return false;

  if (cachedStart > start || (cachedEnd + PARSED_EPG_CACHE_FUTURE_TOLERANCE_SECONDS) < end)
    return false;

  std::vector<data::ChannelEpg> restoredChannels;
  restoredChannels.reserve(channelCount);

  for (std::uint32_t i = 0; i < channelCount; ++i)
  {
    data::ChannelEpg channelEpg;
    std::string id;
    std::string iconPath;
    std::uint32_t displayNameCount = 0;
    std::uint32_t entryCount = 0;

    if (!ReadString(input, id) ||
        !ReadString(input, iconPath) ||
        !ReadBinary(input, displayNameCount))
    {
      return false;
    }

    channelEpg.SetId(id);
    channelEpg.SetIconPath(iconPath);

    for (std::uint32_t j = 0; j < displayNameCount; ++j)
    {
      std::string displayName;
      if (!ReadString(input, displayName))
        return false;
      channelEpg.AddDisplayName(displayName);
    }

    if (!ReadBinary(input, entryCount))
      return false;

    for (std::uint32_t j = 0; j < entryCount; ++j)
    {
      data::EpgEntry entry;
      std::int32_t intValue = 0;
      std::int64_t timeValue = 0;
      std::uint8_t boolValue = 0;
      std::string stringValue;

      if (!ReadBinary(input, intValue)) return false; entry.SetBroadcastId(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetChannelId(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetGenreType(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetGenreSubType(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetYear(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetStarRating(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetEpisodeNumber(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetEpisodePartNumber(intValue);
      if (!ReadBinary(input, intValue)) return false; entry.SetSeasonNumber(intValue);
      if (!ReadBinary(input, timeValue)) return false; entry.SetStartTime(static_cast<time_t>(timeValue));
      if (!ReadBinary(input, timeValue)) return false; entry.SetEndTime(static_cast<time_t>(timeValue));

      if (!ReadString(input, stringValue)) return false; entry.SetFirstAired(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetTitle(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetEpisodeName(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetPlotOutline(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetPlot(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetIconPath(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetGenreString(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetCast(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetDirector(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetWriter(stringValue);
      if (!ReadString(input, stringValue)) return false; entry.SetCatchupId(stringValue);
      if (!ReadBinary(input, boolValue)) return false; entry.SetNew(boolValue != 0);
      if (!ReadBinary(input, boolValue)) return false; entry.SetPremiere(boolValue != 0);

      channelEpg.AddEpgEntry(entry);
    }

    restoredChannels.emplace_back(std::move(channelEpg));
  }

  if (!input.good() && !input.eof())
    return false;

  Clear();
  m_channelEpgs = std::move(restoredChannels);
  BuildChannelEpgIndex();
  LoadGenres();

  if (Settings::GetInstance().GetEpgLogosMode() != EpgLogosMode::IGNORE_XMLTV)
    ApplyChannelsLogosFromEPG();

  m_lastStart = static_cast<time_t>(cachedStart);
  m_lastEnd = static_cast<time_t>(cachedEnd);

  Logger::Log(LEVEL_INFO, "%s - Restored parsed EPG cache for %u channels", __FUNCTION__, channelCount);
  return true;
}

bool Epg::SaveParsedCache(time_t start, time_t end) const
{
  const time_t requiredWindow = static_cast<time_t>(m_epgMaxPastDaysSeconds + m_epgMaxFutureDaysSeconds);
  if ((end - start) < requiredWindow)
  {
    Logger::Log(LEVEL_DEBUG, "%s - Parsed EPG cache not saved: window [%lld, %lld] is smaller than startup cache window",
                __FUNCTION__, static_cast<long long>(start), static_cast<long long>(end));
    return false;
  }

  const std::string cachePath = FileUtils::GetUserDataAddonFilePath(PARSED_EPG_CACHE_FILENAME);
  std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
  if (!output.is_open())
    return false;

  output.write(PARSED_EPG_CACHE_MAGIC, sizeof(PARSED_EPG_CACHE_MAGIC));

  const std::int64_t version = PARSED_EPG_CACHE_VERSION;
  const std::int64_t cacheStart = static_cast<std::int64_t>(start);
  const std::int64_t cacheEnd = static_cast<std::int64_t>(end);
  const std::int32_t epgTimeShift = m_epgTimeShift;
  const std::uint8_t tsOverride = m_tsOverride ? 1 : 0;
  const std::uint64_t channelsSignature = GetChannelsSignature();
  const std::uint32_t channelCount = static_cast<std::uint32_t>(m_channelEpgs.size());

  if (!WriteBinary(output, version) ||
      !WriteBinary(output, cacheStart) ||
      !WriteBinary(output, cacheEnd) ||
      !WriteBinary(output, epgTimeShift) ||
      !WriteBinary(output, tsOverride) ||
      !WriteBinary(output, channelsSignature) ||
      !WriteBinary(output, channelCount))
  {
    return false;
  }

  for (const auto& channelEpg : m_channelEpgs)
  {
    const auto& displayNames = channelEpg.GetDisplayNames();
    const auto& entries = const_cast<data::ChannelEpg&>(channelEpg).GetEpgEntries();
    const std::uint32_t displayNameCount = static_cast<std::uint32_t>(displayNames.size());
    const std::uint32_t entryCount = static_cast<std::uint32_t>(entries.size());

    if (!WriteString(output, channelEpg.GetId()) ||
        !WriteString(output, channelEpg.GetIconPath()) ||
        !WriteBinary(output, displayNameCount))
    {
      return false;
    }

    for (const auto& displayNamePair : displayNames)
    {
      if (!WriteString(output, displayNamePair.m_displayName))
        return false;
    }

    if (!WriteBinary(output, entryCount))
      return false;

    for (const auto& entryPair : entries)
    {
      const auto& entry = entryPair.second;
      const std::int32_t broadcastId = entry.GetBroadcastId();
      const std::int32_t channelId = entry.GetChannelId();
      const std::int32_t genreType = entry.GetGenreType();
      const std::int32_t genreSubType = entry.GetGenreSubType();
      const std::int32_t year = entry.GetYear();
      const std::int32_t starRating = entry.GetStarRating();
      const std::int32_t episodeNumber = entry.GetEpisodeNumber();
      const std::int32_t episodePartNumber = entry.GetEpisodePartNumber();
      const std::int32_t seasonNumber = entry.GetSeasonNumber();
      const std::int64_t startTime = static_cast<std::int64_t>(entry.GetStartTime());
      const std::int64_t endTime = static_cast<std::int64_t>(entry.GetEndTime());
      const std::uint8_t isNew = entry.IsNew() ? 1 : 0;
      const std::uint8_t isPremiere = entry.IsPremiere() ? 1 : 0;

      if (!WriteBinary(output, broadcastId) ||
          !WriteBinary(output, channelId) ||
          !WriteBinary(output, genreType) ||
          !WriteBinary(output, genreSubType) ||
          !WriteBinary(output, year) ||
          !WriteBinary(output, starRating) ||
          !WriteBinary(output, episodeNumber) ||
          !WriteBinary(output, episodePartNumber) ||
          !WriteBinary(output, seasonNumber) ||
          !WriteBinary(output, startTime) ||
          !WriteBinary(output, endTime) ||
          !WriteString(output, entry.GetFirstAired()) ||
          !WriteString(output, entry.GetTitle()) ||
          !WriteString(output, entry.GetEpisodeName()) ||
          !WriteString(output, entry.GetPlotOutline()) ||
          !WriteString(output, entry.GetPlot()) ||
          !WriteString(output, entry.GetIconPath()) ||
          !WriteString(output, entry.GetGenreString()) ||
          !WriteString(output, entry.GetCast()) ||
          !WriteString(output, entry.GetDirector()) ||
          !WriteString(output, entry.GetWriter()) ||
          !WriteString(output, entry.GetCatchupId()) ||
          !WriteBinary(output, isNew) ||
          !WriteBinary(output, isPremiere))
      {
        return false;
      }
    }
  }

  return output.good();
}

bool Epg::LoadEPG(time_t start, time_t end)
{
  auto started = std::chrono::high_resolution_clock::now();
  Logger::Log(LEVEL_DEBUG, "%s - EPG Load Start", __FUNCTION__);

  if (m_xmltvLocation.empty())
  {
    Logger::Log(LEVEL_INFO, "%s - EPG file path is not configured. EPG not loaded.", __FUNCTION__);
    return false;
  }

  std::string data;

  if (GetXMLTVFileWithRetries(data))
  {
    std::string decompressedData;
    char* buffer = FillBufferFromXMLTVData(data, decompressedData);

    if (!buffer)
      return false;

    xml_document xmlDoc;
    xml_parse_result result = xmlDoc.load_string(buffer);

    if (!result)
    {
      std::string errorString;
      int offset = GetParseErrorString(buffer, result.offset, errorString);
      Logger::Log(LEVEL_ERROR, "%s - Unable parse EPG XML: %s, offset: %d: \n[ %s \n]", __FUNCTION__, result.description(), offset, errorString.c_str());
      return false;
    }

    const auto& rootElement = xmlDoc.child("tv");
    if (!rootElement)
    {
      Logger::Log(LEVEL_ERROR, "%s - Invalid EPG XML: no <tv> tag found", __FUNCTION__);
      return false;
    }

    if (!LoadChannelEpgs(rootElement))
      return false;

    LoadEpgEntries(rootElement, start, end);

    xmlDoc.reset();
  }
  else
  {
    return false;
  }

  LoadGenres();

  if (Settings::GetInstance().GetEpgLogosMode() != EpgLogosMode::IGNORE_XMLTV)
    ApplyChannelsLogosFromEPG();

  int milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::high_resolution_clock::now() - started).count();

  SaveParsedCache(start, end);

  Logger::Log(LEVEL_INFO, "%s - EPG Loaded - %d (ms)", __FUNCTION__, milliseconds);

  return true;
}

bool Epg::GetXMLTVFileWithRetries(std::string& data)
{
  int bytesRead = 0;

  bytesRead = FileUtils::GetFileContents(m_xmltvLocation, data);
  if (bytesRead > 0)
  {
    Logger::Log(LEVEL_INFO, "%s - EPG downloaded", __FUNCTION__);
    return true;
  }

  Logger::Log(LEVEL_ERROR, "%s - Unable to load EPG file '%s': file is missing or empty.", __FUNCTION__, m_xmltvLocation.c_str());
  return false;
}

char* Epg::FillBufferFromXMLTVData(std::string& data, std::string& decompressedData)
{
  char* buffer = nullptr;

  // gzip packed
  if (data.size() >= 3 &&
      data[0] == '\x1F' && data[1] == '\x8B' && data[2] == '\x08')
  {
    if (!FileUtils::GzipInflate(data, decompressedData))
    {
      Logger::Log(LEVEL_ERROR, "%s - Invalid EPG file '%s': unable to decompress file.", __FUNCTION__, m_xmltvLocation.c_str());
      return nullptr;
    }
    buffer = &(decompressedData[0]);
  }
  else
  {
    buffer = &(data[0]);
  }

  XmltvFileFormat fileFormat = GetXMLTVFileFormat(buffer);

  if (fileFormat == XmltvFileFormat::INVALID)
  {
    Logger::Log(LEVEL_ERROR, "%s - Invalid EPG file '%s': unable to parse file.", __FUNCTION__, m_xmltvLocation.c_str());
    return nullptr;
  }

  if (fileFormat == XmltvFileFormat::TAR_ARCHIVE)
    buffer += 0x200; // RECORDSIZE = 512

  return buffer;
}

namespace {

char GetLastValidCharInBuffer(const char* buffer)
{
  size_t charIndex = std::strlen(buffer) - 1;
  char lastValidChar = buffer[charIndex];

  while (charIndex != 0 &&
         (buffer[charIndex] == ' ' ||
          buffer[charIndex] == '\t'||
          buffer[charIndex] == '\n' ||
          buffer[charIndex] == '\r' ||
          buffer[charIndex] == '\f' ||
          buffer[charIndex] == '\v'))
  {
    lastValidChar = buffer[--charIndex];
  }

  return lastValidChar;
}

} // unnamed namespace

const XmltvFileFormat Epg::GetXMLTVFileFormat(const char* buffer)
{
  if (!buffer)
    return XmltvFileFormat::INVALID;

  if ((buffer[0] == '\x3C' && GetLastValidCharInBuffer(buffer) == '\x3E') || // Start with < and ends with >
      (buffer[0] == '\x3C' && buffer[1] == '\x3F' &&  buffer[2] == '\x78' && // xml should starts with '<?xml'
       buffer[3] == '\x6D' && buffer[4] == '\x6C'))
  {
    return XmltvFileFormat::NORMAL;
  }

  // check for BOM
  if (buffer[0] != '\xEF' || buffer[1] != '\xBB' || buffer[2] != '\xBF')
  {
    // check for tar archive
    if (strcmp(buffer + 0x101, "ustar") || strcmp(buffer + 0x101, "GNUtar"))
      return XmltvFileFormat::TAR_ARCHIVE;
    else
      return XmltvFileFormat::INVALID;
  }

  return XmltvFileFormat::NORMAL;
}

bool Epg::LoadChannelEpgs(const xml_node& rootElement)
{
  if (!rootElement)
    return false;

  m_channelEpgs.clear();

  for (const auto& channelNode : rootElement.children("channel"))
  {
    ChannelEpg channelEpg;

    if (channelEpg.UpdateFrom(channelNode, m_channels))
    {
      ChannelEpg* existingChannelEpg = FindEpgForChannel(channelEpg.GetId());
      if (existingChannelEpg)
      {
        if (existingChannelEpg->CombineNamesAndIconPathFrom(channelEpg))
          Logger::Log(LEVEL_DEBUG, "%s - Combined channel EPG with id '%s' now has display names: '%s'", __FUNCTION__, channelEpg.GetId().c_str(), channelEpg.GetJoinedDisplayNames().c_str());

        continue;
      }

      Logger::Log(LEVEL_DEBUG, "%s - Loaded channel EPG with id '%s' with display names: '%s'", __FUNCTION__, channelEpg.GetId().c_str(), channelEpg.GetJoinedDisplayNames().c_str());

      m_channelEpgs.emplace_back(channelEpg);
    }
  }

  if (m_channelEpgs.size() == 0)
  {
    Logger::Log(LEVEL_ERROR, "%s - EPG channels not found.", __FUNCTION__);
    return false;
  }
  else
  {
    Logger::Log(LEVEL_INFO, "%s - Loaded '%d' EPG channels.", __FUNCTION__, m_channelEpgs.size());
  }

  BuildChannelEpgIndex();

  return true;
}

void Epg::LoadEpgEntries(const xml_node& rootElement, int start, int end)
{
  int minShiftTime = m_epgTimeShift;
  int maxShiftTime = m_epgTimeShift;
  if (!m_tsOverride)
  {
    minShiftTime = SECONDS_IN_DAY;
    maxShiftTime = -SECONDS_IN_DAY;

    for (const auto& channel : m_channels.GetChannelsList())
    {
      if (channel.GetTvgShift() + m_epgTimeShift < minShiftTime)
        minShiftTime = channel.GetTvgShift() + m_epgTimeShift;
      if (channel.GetTvgShift() + m_epgTimeShift > maxShiftTime)
        maxShiftTime = channel.GetTvgShift() + m_epgTimeShift;
    }
  }

  ChannelEpg* channelEpg = nullptr;
  int count = 0;

  for (const auto& channelNode : rootElement.children("programme"))
  {
    std::string id;
    if (!GetAttributeValue(channelNode, "channel", id))
      continue;

    if (!channelEpg || !StringUtils::EqualsNoCase(channelEpg->GetId(), id))
    {
      if (!(channelEpg = FindEpgForChannel(id)))
        continue;
    }

    EpgEntry entry;
    if (entry.UpdateFrom(channelNode, id, start, end, minShiftTime, maxShiftTime))
    {
      count++;

      channelEpg->AddEpgEntry(entry);
    }
  }

  Logger::Log(LEVEL_INFO, "%s - Loaded '%d' EPG entries.", __FUNCTION__, count);
}


void Epg::ReloadEPG()
{
  m_xmltvLocation = Settings::GetInstance().GetEpgLocation();
  m_epgTimeShift = Settings::GetInstance().GetEpgTimeshiftSecs();
  m_tsOverride = Settings::GetInstance().GetTsOverride();
  m_lastStart = 0;
  m_lastEnd = 0;

  Clear();

  if (LoadEPG(m_lastStart, m_lastEnd))
  {
    for (const auto& myChannel : m_channels.GetChannelsList())
      m_client->TriggerEpgUpdate(myChannel.GetUniqueId());
  }
}

PVR_ERROR Epg::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  Channel myChannel;
  if (!m_channels.GetChannel(channelUid, myChannel))
    return PVR_ERROR_NO_ERROR;

  if (m_channelEpgs.empty() || start < m_lastStart || end > m_lastEnd)
  {
    // reload EPG only if requested interval extends outside the cached window
    LoadEPG(start, end);
    // doesn't matter whether EPG loaded or not, we shouldn't try to load it for same interval again
    m_lastStart = static_cast<int>(start);
    m_lastEnd = static_cast<int>(end);
  }

  ChannelEpg* channelEpg = FindEpgForChannel(channelUid);
  if (!channelEpg || channelEpg->GetEpgEntries().empty())
    return PVR_ERROR_NO_ERROR;

  const int shift = GetEPGTimezoneShiftSecs(myChannel);
  auto& epgEntries = channelEpg->GetEpgEntries();

  auto it = epgEntries.lower_bound(start - shift);
  if (it != epgEntries.begin())
  {
    auto prev = std::prev(it);
    if ((prev->second.GetEndTime() + shift) >= start)
      it = prev;
  }

  for (; it != epgEntries.end(); ++it)
  {
    auto& epgEntry = it->second;
    if ((epgEntry.GetEndTime() + shift) < start)
      continue;
    if ((epgEntry.GetStartTime() + shift) > end)
      break;

    kodi::addon::PVREPGTag tag;
    epgEntry.UpdateTo(tag, channelUid, shift, m_genreMappings);
    results.Add(tag);
  }

  return PVR_ERROR_NO_ERROR;
}

ChannelEpg* Epg::FindEpgForChannel(const std::string& id) const
{
  for (auto& myChannelEpg : m_channelEpgs)
  {
    if (StringUtils::EqualsNoCase(myChannelEpg.GetId(), id))
      return const_cast<ChannelEpg*>(&myChannelEpg);
  }

  return nullptr;
}

void Epg::BuildChannelEpgIndex()
{
  m_channelEpgIndex.clear();

  for (const auto& channel : m_channels.GetChannelsList())
  {
    if (ChannelEpg* channelEpg = FindEpgForChannel(channel))
      m_channelEpgIndex[channel.GetUniqueId()] = channelEpg;
  }
}

ChannelEpg* Epg::FindEpgForChannel(int uniqueChannelId) const
{
  auto it = m_channelEpgIndex.find(uniqueChannelId);
  if (it != m_channelEpgIndex.end())
    return it->second;

  return nullptr;
}

ChannelEpg* Epg::FindEpgForChannel(const Channel& channel) const
{
  if (ChannelEpg* channelEpg = FindEpgForChannel(channel.GetUniqueId()))
    return channelEpg;

  for (auto& myChannelEpg : m_channelEpgs)
  {
    if (StringUtils::EqualsNoCase(myChannelEpg.GetId(), channel.GetTvgId()))
      return const_cast<ChannelEpg*>(&myChannelEpg);
  }

  for (auto& myChannelEpg : m_channelEpgs)
  {
    for (const DisplayNamePair& displayNamePair : myChannelEpg.GetDisplayNames())
    {
      if (StringUtils::EqualsNoCase(displayNamePair.m_displayNameWithUnderscores, channel.GetTvgName()) ||
          StringUtils::EqualsNoCase(displayNamePair.m_displayName, channel.GetTvgName()))
        return const_cast<ChannelEpg*>(&myChannelEpg);
    }
  }

  for (auto& myChannelEpg : m_channelEpgs)
  {
    for (const DisplayNamePair& displayNamePair : myChannelEpg.GetDisplayNames())
    {
      if (StringUtils::EqualsNoCase(displayNamePair.m_displayName, channel.GetChannelName()))
        return const_cast<ChannelEpg*>(&myChannelEpg);
    }
  }

  return nullptr;
}

void Epg::ApplyChannelsLogosFromEPG()
{
  bool updated = false;

  for (const auto& channel : m_channels.GetChannelsList())
  {
    const ChannelEpg* channelEpg = FindEpgForChannel(channel);
    if (!channelEpg || channelEpg->GetIconPath().empty())
      continue;

    // 1 - prefer icon from playlist
    if (!channel.GetIconPath().empty() && Settings::GetInstance().GetEpgLogosMode() == EpgLogosMode::PREFER_M3U)
      continue;

    // 2 - prefer icon from epg
    if (!channelEpg->GetIconPath().empty() && Settings::GetInstance().GetEpgLogosMode() == EpgLogosMode::PREFER_XMLTV)
    {
      m_channels.GetChannel(channel.GetUniqueId())->SetIconPath(channelEpg->GetIconPath());
      updated = true;
    }
  }

  if (updated)
    m_client->TriggerChannelUpdate();
}

bool Epg::LoadGenres()
{
  return false;
}

void Epg::MoveOldGenresXMLFileToNewLocation()
{
  //If we don't have a genres.xml file yet copy it if it exists in any of the other old locations.
  //If not copy a placeholder file that allows the settings dialog to function.
  if (FileUtils::FileExists(ADDON_DATA_BASE_DIR + "/" + GENRES_MAP_FILENAME))
    FileUtils::CopyFile(ADDON_DATA_BASE_DIR + "/" + GENRES_MAP_FILENAME, DEFAULT_GENRE_TEXT_MAP_FILE);
  else if (FileUtils::FileExists(FileUtils::GetSystemAddonPath() + "/" + GENRES_MAP_FILENAME))
    FileUtils::CopyFile(FileUtils::GetSystemAddonPath() + "/" + GENRES_MAP_FILENAME, DEFAULT_GENRE_TEXT_MAP_FILE);
  else
    FileUtils::CopyFile(FileUtils::GetResourceDataPath() + "/" + GENRES_MAP_FILENAME, DEFAULT_GENRE_TEXT_MAP_FILE);

  FileUtils::DeleteFile(ADDON_DATA_BASE_DIR + "/" + GENRES_MAP_FILENAME.c_str());
  FileUtils::DeleteFile(FileUtils::GetSystemAddonPath() + "/" + GENRES_MAP_FILENAME.c_str());
}

EpgEntry* Epg::GetLiveEPGEntry(const Channel& myChannel) const
{
  return GetEPGEntry(myChannel, time(nullptr));
}

EpgEntry* Epg::GetEPGEntry(const Channel& myChannel, time_t lookupTime) const
{
  ChannelEpg* channelEpg = FindEpgForChannel(myChannel);
  if (!channelEpg || channelEpg->GetEpgEntries().size() == 0)
    return nullptr;

  int shift = GetEPGTimezoneShiftSecs(myChannel);

  for (auto& epgEntryPair : channelEpg->GetEpgEntries())
  {
    auto& epgEntry = epgEntryPair.second;
    time_t startTime = epgEntry.GetStartTime() + shift;
    time_t endTime = epgEntry.GetEndTime() + shift;
    if (startTime <= lookupTime && endTime > lookupTime)
      return &epgEntry;
    else if (startTime > lookupTime)
      break;
  }

  return nullptr;
}

int Epg::GetEPGTimezoneShiftSecs(const Channel& myChannel) const
{
  return m_tsOverride ? m_epgTimeShift : myChannel.GetTvgShift() + m_epgTimeShift;
}
