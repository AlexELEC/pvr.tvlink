/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "tvlink/CatchupController.h"
#include "tvlink/Channels.h"
#include "tvlink/ChannelGroups.h"
#include "tvlink/Epg.h"
#include "tvlink/PlaylistLoader.h"
#include "tvlink/data/Channel.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <kodi/addon-instance/PVR.h>
#include <kodi/Filesystem.h>

class ATTR_DLL_LOCAL PVRLinkData
  : public kodi::addon::CAddonBase,
    public kodi::addon::CInstancePVRClient
{
public:
  PVRLinkData();
  ~PVRLinkData() override;

  // kodi::addon::CAddonBase functions
  //@{
  ADDON_STATUS Create() override;
  ADDON_STATUS SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue) override;
  //@}

  // kodi::addon::CInstancePVRClient functions
  //@{
  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override;
  PVR_ERROR GetBackendName(std::string& name) override;
  PVR_ERROR GetBackendVersion(std::string& version) override;
  PVR_ERROR GetConnectionString(std::string& connection) override;

  PVR_ERROR OnSystemSleep() override { return PVR_ERROR_NO_ERROR; }
  PVR_ERROR OnSystemWake() override { return PVR_ERROR_NO_ERROR; }
  PVR_ERROR OnPowerSavingActivated() override { return PVR_ERROR_NO_ERROR; }
  PVR_ERROR OnPowerSavingDeactivated() override { return PVR_ERROR_NO_ERROR; }

  PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results) override;
  PVR_ERROR GetEPGTagStreamProperties(const kodi::addon::PVREPGTag& tag, std::vector<kodi::addon::PVRStreamProperty>& properties) override;
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable) override;
  PVR_ERROR SetEPGMaxPastDays(int epgMaxPastDays) override;
  PVR_ERROR SetEPGMaxFutureDays(int epgMaxFutureDays) override;

  PVR_ERROR GetChannelsAmount(int& amount) override;
  PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override;

  PVR_ERROR GetChannelGroupsAmount(int& amount) override;
  PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override;
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results) override;

  PVR_ERROR GetSignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus) override;
  //@}

  // Internal functions
  //@{
  bool GetChannel(const kodi::addon::PVRChannel& channel, tvlink::data::Channel& myChannel);

  // For catchup
  bool GetChannel(unsigned int uniqueChannelId, tvlink::data::Channel& myChannel);
  tvlink::CatchupController& GetCatchupController() { return m_catchupController; }
  //@}

  // TVLINK
  bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override;
  void CloseLiveStream() override;
  int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize) override;
  bool CanPauseStream() override;

protected:
  void Process();

private:
  static const int PROCESS_LOOP_WAIT_SECS = 2;
  unsigned int iCurl_flags;
  int iConnect_timeout;

  tvlink::data::Channel m_currentChannel;
  tvlink::Channels m_channels;
  tvlink::ChannelGroups m_channelGroups{m_channels};
  tvlink::PlaylistLoader m_playlistLoader{this, m_channels, m_channelGroups};
  tvlink::Epg m_epg{this, m_channels};
  tvlink::CatchupController m_catchupController{m_epg, &m_mutex};

  std::string strCurl_buff;
  std::atomic<bool> m_running{false};
  std::thread m_thread;
  std::mutex m_mutex;
  std::atomic_bool m_reloadChannelsGroupsAndEPG{false};
  kodi::vfs::CFile m_streamHandle;
  std::string ch_url;
  std::string ch_name;
};
