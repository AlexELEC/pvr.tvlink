/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PVRLinkData.h"

#include "tvlink/Settings.h"
#include "tvlink/utilities/Logger.h"
#include "tvlink/utilities/TimeUtils.h"
#include "tvlink/utilities/WebUtils.h"

#include "kodi/General.h"

#include <ctime>
#include <chrono>

using namespace tvlink;
using namespace tvlink::data;
using namespace tvlink::utilities;

PVRLinkData::PVRLinkData()
{
  m_channels.Clear();
  m_channelGroups.Clear();
  m_epg.Clear();
}

ADDON_STATUS PVRLinkData::Create()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  /* Configure the logger */
  Logger::GetInstance().SetImplementation([](LogLevel level, const char* message)
  {
    /* Convert the log level */
    ADDON_LOG addonLevel;

    switch (level)
    {
      case LogLevel::LEVEL_FATAL:
        addonLevel = ADDON_LOG_FATAL;
        break;
      case LogLevel::LEVEL_ERROR:
        addonLevel = ADDON_LOG_ERROR;
        break;
      case LogLevel::LEVEL_WARNING:
        addonLevel = ADDON_LOG_WARNING;
        break;
      case LogLevel::LEVEL_INFO:
        addonLevel = ADDON_LOG_INFO;
        break;
      default:
        addonLevel = ADDON_LOG_DEBUG;
    }

    kodi::Log(addonLevel, "%s", message);
  });

  Logger::GetInstance().SetPrefix("pvr.tvlink");

  Logger::Log(LogLevel::LEVEL_INFO, "%s - Creating the PVR TVLINK add-on", __FUNCTION__);

  Settings::GetInstance().ReadFromAddon(kodi::addon::GetUserPath(), kodi::addon::GetAddonPath());

  m_channels.Init();
  m_channelGroups.Init();
  m_playlistLoader.Init();
  if (!m_playlistLoader.LoadPlayList())
  {
    m_channels.ChannelsLoadFailed();
    m_channelGroups.ChannelGroupsLoadFailed();
  }
  m_epg.Init(EpgMaxPastDays(), EpgMaxFutureDays());

  kodi::Log(ADDON_LOG_INFO, "%s Starting separate client update thread...", __FUNCTION__);

  m_running = true;
  m_thread = std::thread([&] { Process(); });
  iConnect_timeout = Settings::GetInstance().GetConnectTimeout(); // CURL connection timeout

  // ADDON_READ_TRUNCATED     - function returns before entire buffer has been filled
  // ADDON_READ_CHUNKED       - read in the minimum defined chunk size, this disables internal cache then
  // ADDON_READ_NO_CACHE      - open without caching
  // ADDON_READ_MULTI_STREAM  - will seek between multiple streams in the file frequently
  // ADDON_READ_AUDIO_VIDEO   - file is audio and/or video (and e.g. may grow)
  // ADDON_READ_REOPEN        - indicate that caller want to reopen a file if its already open

  if (Settings::GetInstance().GetCurlBuffering())
  {
    strCurl_buff = "buffering mode";
    iCurl_flags = ADDON_READ_CACHED | ADDON_READ_AUDIO_VIDEO;
  }
  else
  {
    strCurl_buff = "not buffering";
    iCurl_flags = ADDON_READ_TRUNCATED | ADDON_READ_CHUNKED | ADDON_READ_NO_CACHE | ADDON_READ_AUDIO_VIDEO;
  }

  return ADDON_STATUS_OK;
}

PVR_ERROR PVRLinkData::GetCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
  capabilities.SetSupportsEPG(true);
  capabilities.SetSupportsTV(true);
  capabilities.SetSupportsRadio(true);
  capabilities.SetSupportsChannelGroups(true);
  capabilities.SetSupportsRecordings(false);
  capabilities.SetSupportsRecordingsRename(false);
  capabilities.SetSupportsRecordingsLifetimeChange(false);
  capabilities.SetSupportsDescrambleInfo(false);
  capabilities.SetHandlesInputStream(true);
  capabilities.SetHandlesDemuxing(false);

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::GetBackendName(std::string& name)
{
  name = "TVLINK Server";
  return PVR_ERROR_NO_ERROR;
}
PVR_ERROR PVRLinkData::GetBackendVersion(std::string& version)
{
  version = STR(IPTV_VERSION);
  return PVR_ERROR_NO_ERROR;
}
PVR_ERROR PVRLinkData::GetConnectionString(std::string& connection)
{
  connection = "connected";
  return PVR_ERROR_NO_ERROR;
}

void PVRLinkData::Process()
{
  unsigned int refreshTimer = 0;
  time_t lastRefreshTimeSeconds = std::time(nullptr);
  int lastRefreshHour = Settings::GetInstance().GetM3URefreshHour(); //ignore if we start during same hour

  while (m_running)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(PROCESS_LOOP_WAIT_SECS * 1000));

    time_t currentRefreshTimeSeconds = std::time(nullptr);
    std::tm timeInfo = SafeLocaltime(currentRefreshTimeSeconds);
    refreshTimer += static_cast<unsigned int>(currentRefreshTimeSeconds - lastRefreshTimeSeconds);
    lastRefreshTimeSeconds = currentRefreshTimeSeconds;

    if (Settings::GetInstance().GetM3URefreshMode() == RefreshMode::REPEATED_REFRESH &&
        refreshTimer >= (Settings::GetInstance().GetM3URefreshIntervalMins() * 60))
      m_reloadChannelsGroupsAndEPG = true;

    if (Settings::GetInstance().GetM3URefreshMode() == RefreshMode::ONCE_PER_DAY &&
        lastRefreshHour != timeInfo.tm_hour && timeInfo.tm_hour == Settings::GetInstance().GetM3URefreshHour())
      m_reloadChannelsGroupsAndEPG = true;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running && m_reloadChannelsGroupsAndEPG)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

      Settings::GetInstance().ReloadAddonSettings();
      m_playlistLoader.ReloadPlayList();
      m_epg.ReloadEPG();

      m_reloadChannelsGroupsAndEPG = false;
      refreshTimer = 0;
    }
    lastRefreshHour = timeInfo.tm_hour;
  }
}

PVRLinkData::~PVRLinkData()
{
  Logger::Log(LEVEL_DEBUG, "%s Stopping update thread...", __FUNCTION__);
  m_running = false;
  if (m_thread.joinable())
    m_thread.join();

  std::lock_guard<std::mutex> lock(m_mutex);
  m_channels.Clear();
  m_channelGroups.Clear();
  m_epg.Clear();
}

PVR_ERROR PVRLinkData::GetChannelsAmount(int& amount)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  amount = m_channels.GetChannelsAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_channels.GetChannels(results, radio);
}

bool PVRLinkData::GetChannel(const kodi::addon::PVRChannel& channel, Channel& myChannel)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_channels.GetChannel(channel, myChannel);
}

bool PVRLinkData::GetChannel(unsigned int uniqueChannelId, tvlink::data::Channel& myChannel)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_channels.GetChannel(uniqueChannelId, myChannel);
}

PVR_ERROR PVRLinkData::GetChannelGroupsAmount(int& amount)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  amount = m_channelGroups.GetChannelGroupsAmount();
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_channelGroups.GetChannelGroups(results, radio);
}

PVR_ERROR PVRLinkData::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_channelGroups.GetChannelGroupMembers(group, results);
}

PVR_ERROR PVRLinkData::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_epg.GetEPGForChannel(channelUid, start, end, results);
}

PVR_ERROR PVRLinkData::GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties)
{
  Logger::Log(LEVEL_DEBUG, "%s - Tag startTime: %ld \tendTime: %ld", __FUNCTION__, tag.GetStartTime(), tag.GetEndTime());

  if (GetChannel(static_cast<int>(tag.GetUniqueChannelId()), m_currentChannel))
  {
    Logger::Log(LEVEL_DEBUG, "%s - GetPlayEpgAsLive is %s", __FUNCTION__, Settings::GetInstance().CatchupPlayEpgAsLive() ? "enabled" : "disabled");

    std::map<std::string, std::string> catchupProperties;
    m_catchupController.ProcessEPGTagForTimeshiftedPlayback(tag, m_currentChannel, catchupProperties);
    std::string orgUrl = m_currentChannel.GetStreamURL();

    // shift catchup url
    m_currentChannel.GenerateShiftCatchupSource(orgUrl);
    const std::string catchupShiftUrl = m_catchupController.GetCatchupUrl(m_currentChannel);
      
    StreamUtils::SetAllStreamProperties(properties, m_currentChannel, catchupShiftUrl, false, catchupProperties);

    Logger::Log(LEVEL_INFO, "%s - EPG Catchup URL: %s", __FUNCTION__, WebUtils::RedactUrl(catchupShiftUrl).c_str());
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR PVRLinkData::IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable)
{
  if (!Settings::GetInstance().IsCatchupEnabled())
    return PVR_ERROR_NOT_IMPLEMENTED;

  const time_t now = std::time(nullptr);
  Channel channel;

  // Get the channel and set the current tag on it if found
  bIsPlayable = GetChannel(static_cast<int>(tag.GetUniqueChannelId()), channel) &&
                Settings::GetInstance().IsCatchupEnabled() && channel.IsCatchupSupported();

  if (channel.IgnoreCatchupDays())
  {
    // If we ignore catchup days then any tag can be played but only if it has a catchup ID
    bool hasCatchupId = false;
    EpgEntry* epgEntry = m_catchupController.GetEPGEntry(channel, tag.GetStartTime());
    if (epgEntry)
      hasCatchupId = !epgEntry->GetCatchupId().empty();

    bIsPlayable = bIsPlayable && hasCatchupId;
  }
  else
  {
    bIsPlayable = bIsPlayable &&
                  tag.GetStartTime() < now &&
                  tag.GetStartTime() >= (now - static_cast<time_t>(channel.GetCatchupDaysInSeconds())) &&
                  (!Settings::GetInstance().CatchupOnlyOnFinishedProgrammes() || tag.GetEndTime() < now);
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::SetEPGMaxPastDays(int epgMaxPastDays)
{
  m_epg.SetEPGMaxPastDays(epgMaxPastDays);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::SetEPGMaxFutureDays(int epgMaxFutureDays)
{
  m_epg.SetEPGMaxFutureDays(epgMaxFutureDays);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRLinkData::GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
  signalStatus.SetAdapterName("TVLINK Adapter 1");
  signalStatus.SetAdapterStatus("OK");

  return PVR_ERROR_NO_ERROR;
}

ADDON_STATUS PVRLinkData::SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // When a number of settings change set this on the first one so it can be picked up
  // in the process call for a reload of channels, groups and EPG.
  if (!m_reloadChannelsGroupsAndEPG)
    m_reloadChannelsGroupsAndEPG = true;

  return Settings::GetInstance().SetValue(settingName, settingValue);
}

// live stream functions

bool PVRLinkData::OpenLiveStream(const kodi::addon::PVRChannel& channel)
{
  if (GetChannel(channel, m_currentChannel))
  {
    ch_url = m_currentChannel.GetStreamURL();
    ch_name = m_currentChannel.GetChannelName();
    Logger::Log(LogLevel::LEVEL_INFO, "%s - [%s] %s Live URL: %s", __FUNCTION__, ch_name.c_str(), strCurl_buff.c_str(), WebUtils::RedactUrl(ch_url).c_str());

    m_streamHandle.CURLCreate(ch_url.c_str());
    m_streamHandle.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "connection-timeout", std::to_string(iConnect_timeout).c_str());
    m_streamHandle.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "data-timeout", "0");
    m_streamHandle.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "lowspeed-time", "0");
    m_streamHandle.CURLAddOption(ADDON_CURL_OPTION_PROTOCOL, "speed-limit", "0");

    m_streamHandle.CURLOpen(iCurl_flags);

    return m_streamHandle.IsOpen();
  }
  return false;
}

void PVRLinkData::CloseLiveStream(void)
{
  if (m_streamHandle.IsOpen())
  {
    m_streamHandle.Close();
    Logger::Log(LogLevel::LEVEL_INFO, "%s - [%s] Live URL: %s", __FUNCTION__, ch_name.c_str(), WebUtils::RedactUrl(ch_url).c_str());
  }

}

int PVRLinkData::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return m_streamHandle.Read(pBuffer, iBufferSize);
}

bool PVRLinkData::CanPauseStream()
{
  return m_streamHandle.IsOpen();
}

ADDONCREATOR(PVRLinkData)
