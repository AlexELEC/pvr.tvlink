/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "Channels.h"
#include "data/ChannelGroup.h"

#include <string>
#include <vector>

#include <kodi/addon-instance/pvr/ChannelGroups.h>

namespace tvlink
{
  class ChannelGroups
  {
  public:
    ChannelGroups(const tvlink::Channels& channels);

    int GetChannelGroupsAmount() const;
    PVR_ERROR GetChannelGroups(kodi::addon::PVRChannelGroupsResultSet& results, bool radio) const;
    PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results);

    int AddChannelGroup(tvlink::data::ChannelGroup& channelGroup);
    tvlink::data::ChannelGroup* GetChannelGroup(int uniqueId);
    tvlink::data::ChannelGroup* FindChannelGroup(const std::string& name);
    const std::vector<data::ChannelGroup>& GetChannelGroupsList() const { return m_channelGroups; }
    bool Init();
    void Clear();
    void ChannelGroupsLoadFailed() { m_channelGroupsLoadFailed = true; };

  private:
    const tvlink::Channels& m_channels;
    std::vector<tvlink::data::ChannelGroup> m_channelGroups;

    bool m_channelGroupsLoadFailed = false;
  };
} //namespace tvlink
