/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "data/Channel.h"

#include <string>
#include <vector>

#include <kodi/addon-instance/pvr/Channels.h>

namespace tvlink
{
  class ChannelGroups;

  namespace data
  {
    class ChannelGroup;
  }

  class Channels
  {
  public:
    Channels() = default;

    bool Init();

    int GetChannelsAmount() const;
    PVR_ERROR GetChannels(kodi::addon::PVRChannelsResultSet& results, bool radio) const;
    bool GetChannel(const kodi::addon::PVRChannel& channel, tvlink::data::Channel& myChannel) const;
    bool GetChannel(int uniqueId, tvlink::data::Channel& myChannel) const;

    void AddChannel(tvlink::data::Channel& channel, std::vector<int>& groupIdList, tvlink::ChannelGroups& channelGroups);
    tvlink::data::Channel* GetChannel(int uniqueId);
    const tvlink::data::Channel* FindChannel(const std::string& id, const std::string& displayName) const;
    const std::vector<data::Channel>& GetChannelsList() const { return m_channels; }
    void Clear();

    int GetCurrentChannelNumber() const { return m_currentChannelNumber; }
    void ChannelsLoadFailed() { m_channelsLoadFailed = true; };

  private:
    int GenerateChannelId(const char* channelName, const char* streamUrl);

    int m_currentChannelNumber;
    bool m_channelsLoadFailed = false;

    std::vector<tvlink::data::Channel> m_channels;
  };
} //namespace tvlink
