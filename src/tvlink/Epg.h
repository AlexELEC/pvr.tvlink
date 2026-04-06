/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "Channels.h"
#include "Settings.h"
#include "data/ChannelEpg.h"
#include "data/EpgGenre.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <kodi/addon-instance/PVR.h>

namespace tvlink
{
  static const int SECONDS_IN_DAY = 86400;
  static const std::string GENRES_MAP_FILENAME = "genres.xml";
  static const std::string GENRE_DIR = "/genres";
  static const std::string GENRE_ADDON_DATA_BASE_DIR = ADDON_DATA_BASE_DIR + GENRE_DIR;
  static const int DEFAULT_EPG_MAX_DAYS = 3;
  static const std::string PARSED_EPG_CACHE_FILENAME = "xmltv.parsed.cache";

  enum class XmltvFileFormat
  {
    NORMAL,
    TAR_ARCHIVE,
    INVALID
  };

  class Epg
  {
  public:
    Epg(kodi::addon::CInstancePVRClient* client, tvlink::Channels& channels);

    bool Init(int epgMaxPastDays, int epgMaxFutureDays);

    PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results);
    void SetEPGMaxPastDays(int epgMaxPastDays);
    void SetEPGMaxFutureDays(int epgMaxFutureDays);
    void Clear();
    void ReloadEPG();

    data::EpgEntry* GetLiveEPGEntry(const data::Channel& myChannel) const;
    data::EpgEntry* GetEPGEntry(const data::Channel& myChannel, time_t lookupTime) const;
    int GetEPGTimezoneShiftSecs(const data::Channel& myChannel) const;

  private:
    static const XmltvFileFormat GetXMLTVFileFormat(const char* buffer);
    static void MoveOldGenresXMLFileToNewLocation();

    bool LoadEPG(time_t iStart, time_t iEnd);
    bool TryLoadParsedCache(time_t start, time_t end);
    bool SaveParsedCache(time_t start, time_t end) const;
    bool ShouldUseParsedCache() const;
    std::uint64_t GetChannelsSignature() const;
    bool GetXMLTVFileWithRetries(std::string& data);
    char* FillBufferFromXMLTVData(std::string& data, std::string& decompressedData);
    bool LoadChannelEpgs(const pugi::xml_node& rootElement);
    void LoadEpgEntries(const pugi::xml_node& rootElement, int start, int end);
    bool LoadGenres();

    data::ChannelEpg* FindEpgForChannel(const std::string& id) const;
    data::ChannelEpg* FindEpgForChannel(int uniqueChannelId) const;
    data::ChannelEpg* FindEpgForChannel(const data::Channel& channel) const;
    void BuildChannelEpgIndex();
    void ApplyChannelsLogosFromEPG();

    std::string m_xmltvLocation;
    int m_epgTimeShift;
    bool m_tsOverride;
    time_t m_lastStart;
    time_t m_lastEnd;
    int m_epgMaxPastDays;
    int m_epgMaxFutureDays;
    long m_epgMaxPastDaysSeconds;
    long m_epgMaxFutureDaysSeconds;

    tvlink::Channels& m_channels;
    std::vector<data::ChannelEpg> m_channelEpgs;
    std::unordered_map<int, data::ChannelEpg*> m_channelEpgIndex;
    std::vector<tvlink::data::EpgGenre> m_genreMappings;

    kodi::addon::CInstancePVRClient* m_client;
  };
} //namespace tvlink
