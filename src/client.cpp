/*
 *      Copyright (C) 2013-2015 Anton Fedchin
 *      http://github.com/afedchin/xbmc-addon-iptvsimple/
 *
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "client.h"
#include "kodi/xbmc_pvr_dll.h"
#include "PVRIptvData.h"
#include "p8-platform/util/util.h"

using namespace ADDON;

#ifdef TARGET_WINDOWS
#define snprintf _snprintf
#ifdef CreateDirectory
#undef CreateDirectory
#endif
#ifdef DeleteFile
#undef DeleteFile
#endif
#endif

bool           m_bCreated       = false;
ADDON_STATUS   m_CurStatus      = ADDON_STATUS_UNKNOWN;
PVRIptvData   *m_data           = NULL;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_strUserPath   = "";
std::string g_strClientPath = "";

CHelper_libXBMC_addon *XBMC = NULL;
CHelper_libXBMC_pvr   *PVR  = NULL;

std::string g_strTvgPath    = "";
std::string g_strTVLServer  = "127.0.0.1";
std::string g_strLogoPath   = "";
std::string g_userAgent     = "";
int         g_iTVLPort      = 2020;
int         g_logoPathType  = 0;
int         g_iEPGTimeShift = 0;
int         g_iStartNumber  = 1;
bool        g_bTSOverride   = true;
bool        g_bCacheM3U     = false;
bool        g_bCacheSTRM    = false;
bool        g_bCacheEPG     = false;
int         g_iEPGLogos     = 1;
int         g_iPathType     = 0;

unsigned int g_iCacheFlag   = 0x08; /*READ_NO_CACHE*/

extern std::string PathCombine(const std::string &strPath, const std::string &strFileName)
{
  std::string strResult = strPath;
  if (strResult.at(strResult.size() - 1) == '\\' ||
      strResult.at(strResult.size() - 1) == '/')
  {
    strResult.append(strFileName);
  }
  else
  {
    strResult.append("/");
    strResult.append(strFileName);
  }

  return strResult;
}

extern std::string GetClientFilePath(const std::string &strFileName)
{
  return PathCombine(g_strClientPath, strFileName);
}

extern std::string GetUserFilePath(const std::string &strFileName)
{
  return PathCombine(g_strUserPath, strFileName);
}

extern "C" {

void ADDON_ReadSettings(void)
{
  char buffer[1024];
  if (XBMC->GetSetting("tvlServer", &buffer))
  {
    g_strTVLServer = buffer;
  }
  if (!XBMC->GetSetting("tvlPort", &g_iTVLPort))
  {
    g_iTVLPort = 2020;
  }
  if (!XBMC->GetSetting("m3uCache", &g_bCacheM3U))
  {
    g_bCacheM3U = false;
  }
  if (!XBMC->GetSetting("streamCache", &g_bCacheSTRM))
  {
    g_bCacheSTRM = false;
  }
  if (g_bCacheSTRM)
  {
    g_iCacheFlag = 0x04; /*READ_CACHED*/
  }
  else
  {
    g_iCacheFlag = 0x08; /*READ_NO_CACHE*/
  }

  if (!XBMC->GetSetting("epgPathType", &g_iPathType))
  {
    g_iPathType = 0;
  }

  if (!g_iPathType)
  {
    g_strTvgPath = std::string("http://") + g_strTVLServer + ":" + std::to_string(g_iTVLPort) + "/xmltv";
  }
  else
  {
    if (XBMC->GetSetting("epgUrl", &buffer))
    {
      g_strTvgPath = buffer;
    }
  }

  if (!XBMC->GetSetting("epgCache", &g_bCacheEPG))
  {
    g_bCacheEPG = true;
  }

  float fShift;
  if (XBMC->GetSetting("epgTimeShift", &fShift))
  {
    g_iEPGTimeShift = (int)(fShift * 3600.0); // hours to seconds
  }
  if (!XBMC->GetSetting("epgTSOverride", &g_bTSOverride))
  {
    g_bTSOverride = true;
  }
}

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
  {
    return ADDON_STATUS_UNKNOWN;
  }

  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_DEBUG, "%s - Creating the PVR IPTV Simple add-on", __FUNCTION__);

  m_CurStatus     = ADDON_STATUS_UNKNOWN;
  g_strUserPath   = pvrprops->strUserPath;
  g_strClientPath = pvrprops->strClientPath;

  if (!XBMC->DirectoryExists(g_strUserPath.c_str()))
  {
    XBMC->CreateDirectory(g_strUserPath.c_str());
  }

  ADDON_ReadSettings();

  m_data = new PVRIptvData;
  m_CurStatus = ADDON_STATUS_OK;
  m_bCreated = true;

  return m_CurStatus;
}

ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

void ADDON_Destroy()
{
  delete m_data;
  m_bCreated = false;
  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  // reset cache and restart addon

  std::string strFile = GetUserFilePath(M3U_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  strFile = GetUserFilePath(TVG_FILE_NAME);
  if (XBMC->FileExists(strFile.c_str(), false))
  {
    XBMC->DeleteFile(strFile.c_str());
  }

  return ADDON_STATUS_NEED_RESTART;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
}

void OnSystemWake()
{
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES* pCapabilities)
{
  pCapabilities->bSupportsEPG             = true;
  pCapabilities->bSupportsTV              = true;
  pCapabilities->bSupportsRadio           = true;
  pCapabilities->bSupportsChannelGroups   = true;
  pCapabilities->bSupportsRecordings      = false;
  pCapabilities->bSupportsRecordingsRename = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;
  pCapabilities->bHandlesInputStream = true;
  pCapabilities->bHandlesDemuxing = false;

  return PVR_ERROR_NO_ERROR;
}

const char *GetBackendName(void)
{
  static const char *strBackendName = "TVLINK PVR Add-on";
  return strBackendName;
}

const char *GetBackendVersion(void)
{
  static std::string strBackendVersion = STR(IPTV_VERSION);
  return strBackendVersion.c_str();
}

const char *GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

const char *GetBackendHostname(void)
{
  return "";
}

PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  *iTotal = 0;
  *iUsed  = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  if (m_data)
    return m_data->GetEPGForChannel(handle, channel, iStart, iEnd);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelsAmount(void)
{
  if (m_data)
    return m_data->GetChannelsAmount();

  return -1;
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannels(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

int GetChannelGroupsAmount(void)
{
  if (m_data)
    return m_data->GetChannelGroupsAmount();

  return -1;
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (m_data)
    return m_data->GetChannelGroups(handle, bRadio);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (m_data)
    return m_data->GetChannelGroupMembers(handle, group);

  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  snprintf(signalStatus.strAdapterName, sizeof(signalStatus.strAdapterName), "TVLINK Adapter 1");
  snprintf(signalStatus.strAdapterStatus, sizeof(signalStatus.strAdapterStatus), "OK");

  return PVR_ERROR_NO_ERROR;
}

// live stream functions

bool OpenLiveStream(const PVR_CHANNEL &channel)
{
  if (m_data)
    return m_data->OpenLiveStream(channel, g_iCacheFlag);

  return false;
}

void CloseLiveStream(void)
{
  m_data->CloseLiveStream();
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return m_data->ReadLiveStream(pBuffer, iBufferSize, g_iCacheFlag);
}

bool CanPauseStream(void){
  return true;
}

/** UNUSED API FUNCTIONS */
PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingsAmount(bool deleted) { return -1; }
PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelScan(void) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
bool OpenRecordedStream(const PVR_RECORDING &recording) { return false; }
void CloseRecordedStream(void) {}
int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize) { return 0; }
long long SeekRecordedStream(long long iPosition, int iWhence /* = SEEK_SET */) { return 0; }
long long LengthRecordedStream(void) { return 0; }
void DemuxReset(void) {}
void DemuxFlush(void) {}
long long SeekLiveStream(long long iPosition, int iWhence /* = SEEK_SET */) { return -1; }
long long LengthLiveStream(void) { return -1; }
PVR_ERROR DeleteRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR RenameRecording(const PVR_RECORDING &recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording) { return -1; }
PVR_ERROR GetRecordingEdl(const PVR_RECORDING&, PVR_EDL_ENTRY[], int*) { return PVR_ERROR_NOT_IMPLEMENTED; };
PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
int GetTimersAmount(void) { return -1; }
PVR_ERROR GetTimers(ADDON_HANDLE handle) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR AddTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR UpdateTimer(const PVR_TIMER &timer) { return PVR_ERROR_NOT_IMPLEMENTED; }
void DemuxAbort(void) {}
DemuxPacket* DemuxRead(void) { return NULL; }
bool IsTimeshifting(void) { return false; }
bool IsRealTimeStream(void) { return true; }
void PauseStream(bool bPaused) {}
bool CanSeekStream(void) { return false; }
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetStreamReadChunkSize(int* chunksize) { return PVR_ERROR_NOT_IMPLEMENTED; }

} // extern "C"
