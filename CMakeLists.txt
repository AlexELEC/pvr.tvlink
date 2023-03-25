cmake_minimum_required(VERSION 3.5)
project(pvr.tvlink)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(Pugixml REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${PUGIXML_INCLUDE_DIRS}
                    ${ZLIB_INCLUDE_DIRS})

set(DEPLIBS ${PUGIXML_LIBRARIES}
            ${ZLIB_LIBRARIES})

message(STATUS "PUGIXML_LIBRARIES: ${PUGIXML_LIBRARIES}")
message(STATUS "ZLIB_LIBRARIES: ${ZLIB_LIBRARIES}")

set(IPTV_SOURCES src/PVRLinkData.cpp
                 src/tvlink/CatchupController.cpp
                 src/tvlink/Channels.cpp
                 src/tvlink/ChannelGroups.cpp
                 src/tvlink/Epg.cpp
                 src/tvlink/PlaylistLoader.cpp
                 src/tvlink/Settings.cpp
                 src/tvlink/StreamManager.cpp
                 src/tvlink/data/Channel.cpp
                 src/tvlink/data/ChannelEpg.cpp
                 src/tvlink/data/ChannelGroup.cpp
                 src/tvlink/data/EpgEntry.cpp
                 src/tvlink/data/EpgGenre.cpp
                 src/tvlink/utilities/FileUtils.cpp
                 src/tvlink/utilities/Logger.cpp
                 src/tvlink/utilities/StreamUtils.cpp
                 src/tvlink/utilities/WebUtils.cpp)

set(IPTV_HEADERS src/PVRLinkData.h
                 src/tvlink/CatchupController.h
                 src/tvlink/Channels.h
                 src/tvlink/ChannelGroups.h
                 src/tvlink/Epg.h
                 src/tvlink/PlaylistLoader.h
                 src/tvlink/Settings.h
                 src/tvlink/StreamManager.h
                 src/tvlink/data/Channel.h
                 src/tvlink/data/ChannelEpg.h
                 src/tvlink/data/ChannelGroup.h
                 src/tvlink/data/EpgEntry.h
                 src/tvlink/data/EpgGenre.h
                 src/tvlink/data/StreamEntry.h
                 src/tvlink/utilities/FileUtils.h
                 src/tvlink/utilities/Logger.h
                 src/tvlink/utilities/StreamUtils.h
                 src/tvlink/utilities/TimeUtils.h
                 src/tvlink/utilities/WebUtils.h
                 src/tvlink/utilities/XMLUtils.h)

addon_version(pvr.tvlink IPTV)
add_definitions(-DIPTV_VERSION=${IPTV_VERSION})

build_addon(pvr.tvlink IPTV DEPLIBS)

include(CPack)
