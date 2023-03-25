/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Settings.h"

#include "utilities/FileUtils.h"

using namespace tvlink;
using namespace tvlink::utilities;

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromAddon(const std::string& userPath, const std::string& clientPath)
{
  m_userPath = userPath;
  m_clientPath = clientPath;

  // TVLINK Server
  m_tvlinkIP = kodi::addon::GetSettingString("tvlinkIP", "127.0.0.1");
  m_tvlinkPort = kodi::addon::GetSettingString("tvlinkPort", "2020");
  m_tvlinkUser = kodi::addon::GetSettingString("tvlinkUser");
  m_tvlinkToken = kodi::addon::GetSettingString("tvlinkToken");
  m_connectTimeout = kodi::addon::GetSettingInt("connectTimeout", 10);
  m_curlBuff = kodi::addon::GetSettingBoolean("curlBuff", false);
  m_useFFmpeg = kodi::addon::GetSettingBoolean("useFFmpeg", false);

  // M3U
  if (m_useFFmpeg)
    m_tvlinkList = "/ffmpeglist";
  else
    m_tvlinkList = "/playlist";

  if (!m_tvlinkToken.empty())
  {
    if (!m_tvlinkUser.empty())
      m_m3uUrl = "http://" + m_tvlinkIP + ":" + m_tvlinkPort + "/" + m_tvlinkToken + m_tvlinkList + "/" + m_tvlinkUser;
    else
      m_m3uUrl = "http://" + m_tvlinkIP + ":" + m_tvlinkPort + "/" + m_tvlinkToken + m_tvlinkList;
  }
  else
  {
    if (!m_tvlinkUser.empty())
      m_m3uUrl = "http://" + m_tvlinkIP + ":" + m_tvlinkPort + m_tvlinkList + "/" + m_tvlinkUser;
    else
      m_m3uUrl = "http://" + m_tvlinkIP + ":" + m_tvlinkPort + m_tvlinkList;
  }

  m_cacheM3U = kodi::addon::GetSettingBoolean("m3uCache", false);
  m_startChannelNumber = kodi::addon::GetSettingInt("startNum", 1);
  m_numberChannelsByM3uOrderOnly = kodi::addon::GetSettingBoolean("numberByOrder", false);
  m_m3uRefreshMode = kodi::addon::GetSettingEnum<RefreshMode>("m3uRefreshMode", RefreshMode::REPEATED_REFRESH);
  m_m3uRefreshIntervalMins = kodi::addon::GetSettingInt("m3uRefreshIntervalMins", 180);
  m_m3uRefreshHour = kodi::addon::GetSettingInt("m3uRefreshHour", 4);

  // EPG
  m_epgUrl = "http://" + m_tvlinkIP + ":" + m_tvlinkPort + "/xmltv";
  m_cacheEPG = kodi::addon::GetSettingBoolean("epgCache", true);
  m_epgTimeShiftHours = kodi::addon::GetSettingFloat("epgTimeShift", 0.0f);
  m_tsOverride = kodi::addon::GetSettingBoolean("epgTSOverride", false);
}

void Settings::ReloadAddonSettings()
{
  ReadFromAddon(m_userPath, m_clientPath);
}

ADDON_STATUS Settings::SetValue(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  // reset cache and restart addon

  std::string strFile = FileUtils::GetUserDataAddonFilePath(M3U_CACHE_FILENAME);
  if (FileUtils::FileExists(strFile))
    FileUtils::DeleteFile(strFile);

  strFile = FileUtils::GetUserDataAddonFilePath(XMLTV_CACHE_FILENAME);
  if (FileUtils::FileExists(strFile))
    FileUtils::DeleteFile(strFile);

  // TVLINK
  if (settingName == "tvlinkIP")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_tvlinkIP, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "tvlinkPort")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_tvlinkPort, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "tvlinkUser")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_tvlinkUser, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "m3uCache")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_cacheM3U, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "startNum")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_startChannelNumber, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "numberByOrder")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_numberChannelsByM3uOrderOnly, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "m3uRefreshMode")
    return SetEnumSetting<RefreshMode, ADDON_STATUS>(settingName, settingValue, m_m3uRefreshMode, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "m3uRefreshIntervalMins")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_m3uRefreshIntervalMins, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "m3uRefreshHour")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_m3uRefreshHour, ADDON_STATUS_OK, ADDON_STATUS_OK);
  // EPG
  else if (settingName == "epgCache")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_cacheEPG, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "epgTimeShift")
    return SetSetting<float, ADDON_STATUS>(settingName, settingValue, m_epgTimeShiftHours, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "epgTSOverride")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_tsOverride, ADDON_STATUS_OK, ADDON_STATUS_OK);

  return ADDON_STATUS_OK;
}
