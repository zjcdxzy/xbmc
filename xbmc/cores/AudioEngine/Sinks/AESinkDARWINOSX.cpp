/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <CoreServices/CoreServices.h>
#include "cores/AudioEngine/Sinks/AESinkDARWINOSX.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "cores/AudioEngine/Engines/CoreAudio/CoreAudioAEHAL.h"
#include "cores/AudioEngine/Engines/CoreAudio/CoreAudioHardware.h"
#include "osx/DarwinUtils.h"
#include "utils/log.h"

#include <sstream>

#define CA_MAX_CHANNELS 8
static enum AEChannel CAChannelMap[CA_MAX_CHANNELS + 1] = {
  AE_CH_FL , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_FC , AE_CH_LFE , AE_CH_SL , AE_CH_SR ,
  AE_CH_NULL
};

static bool HasSampleRate(const AESampleRateList &list, const unsigned int samplerate)
{
  for (size_t i = 0; i < list.size(); ++i)
  {
    if (list[i] == samplerate)
      return true;
  }
  return false;
}

static bool HasDataFormat(const AEDataFormatList &list, const enum AEDataFormat format)
{
  for (size_t i = 0; i < list.size(); ++i)
  {
    if (list[i] == format)
      return true;
  }
  return false;
}

static AudioStreamBasicDescription* GetStreamDescriptions(AudioStreamID streamID)
{
  // Retrieve all the stream formats supported by this output stream

  AudioObjectPropertyAddress propertyAddress; 
  propertyAddress.mScope    = kAudioObjectPropertyScopeGlobal; 
  propertyAddress.mElement  = kAudioObjectPropertyElementMaster;
  propertyAddress.mSelector = kAudioStreamPropertyPhysicalFormats; 

  UInt32 listSize = 0;
  OSStatus ret = AudioObjectGetPropertyDataSize(streamID, &propertyAddress, 0, NULL, &listSize); 
  if (ret != noErr)
  {
    CLog::Log(LOGDEBUG, "CCoreAudioHardware::FormatsList: "
      "Unable to get list size. Error = %s", GetError(ret).c_str());
    return NULL;
  }

  // Space for a terminating ID:
  listSize += sizeof(AudioStreamBasicDescription);
  AudioStreamBasicDescription *list = (AudioStreamBasicDescription*)malloc(listSize);
  if (list == NULL)
  {
    CLog::Log(LOGERROR, "CCoreAudioHardware::FormatsList: Out of memory?");
    return NULL;
  }

  ret = AudioObjectGetPropertyData(streamID, &propertyAddress, 0, NULL, &listSize, list); 
  if (ret != noErr)
  {
    CLog::Log(LOGDEBUG, "CCoreAudioHardware::FormatsList: "
      "Unable to get list. Error = %s", GetError(ret).c_str());
    free(list);
    return NULL;
  }

  // Add a terminating ID:
  list[listSize/sizeof(AudioStreamBasicDescription)].mFormatID = 0;

  return list;
}

static AudioStreamID* GetDeviceStreams(AudioDeviceID deviceId)
{
  // Get a list of all the streams on this device
  AudioObjectPropertyAddress  propertyAddress;
  propertyAddress.mScope    = kAudioDevicePropertyScopeOutput;
  propertyAddress.mElement  = 0;
  propertyAddress.mSelector = kAudioDevicePropertyStreams;

  UInt32 listSize;
  OSStatus ret = AudioObjectGetPropertyDataSize(deviceId, &propertyAddress, 0, NULL, &listSize); 
  if (ret != noErr)
    return NULL;

  // Space for a terminating ID:
  listSize += sizeof(AudioStreamID);
  AudioStreamID *list = (AudioStreamID*)malloc(listSize);
  if (list == NULL)
  {
    CLog::Log(LOGERROR, "CCoreAudioHardware::StreamsList: Out of memory?");
    return NULL;
  }

  ret = AudioObjectGetPropertyData(deviceId, &propertyAddress, 0, NULL, &listSize, list);
  if (ret != noErr)
  {
    CLog::Log(LOGERROR, "CCoreAudioHardware::StreamsList: "
      "Unable to get list. Error = %s", GetError(ret).c_str());
    return NULL;
  }

  // Add a terminating ID:
  list[listSize/sizeof(AudioStreamID)] = kAudioHardwareBadStreamError;

  return list;
}

static std::string GetDeviceName(AudioDeviceID deviceId)
{
  if (!deviceId)
    return NULL;

  AudioObjectPropertyAddress  propertyAddress;
  propertyAddress.mScope    = kAudioDevicePropertyScopeOutput;
  propertyAddress.mElement  = 0;
  propertyAddress.mSelector = kAudioDevicePropertyDeviceName;

  UInt32 propertySize;
  OSStatus ret = AudioObjectGetPropertyDataSize(deviceId, &propertyAddress, 0, NULL, &propertySize);
  if (ret != noErr)
    return NULL;

  std::string name = "";
  char *buff = new char[propertySize + 1];
  buff[propertySize] = 0x00;
  ret = AudioObjectGetPropertyData(deviceId, &propertyAddress, 0, NULL, &propertySize, buff);
  if (ret != noErr)
  {
    CLog::Log(LOGERROR, "Unable to get device name - id: 0x%04x. Error = %s",
      (uint)deviceId, GetError(ret).c_str());
  }
  else
  {
    name = buff;
    // trim out any trailing spaces.
    name.erase(name.find_last_not_of(" ") + 1);
  }
  delete[] buff;

  return name;
}

static AudioDeviceID GetDefaultOutputDevice()
{
  AudioDeviceID deviceId = 0;
  static AudioDeviceID lastDeviceId = 0;
  
  AudioObjectPropertyAddress  propertyAddress;
  propertyAddress.mScope    = kAudioObjectPropertyScopeGlobal;
  propertyAddress.mElement  = kAudioObjectPropertyElementMaster;
  propertyAddress.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
  
  UInt32 size = sizeof(AudioDeviceID);
  OSStatus ret = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &size, &deviceId);
  
  // outputDevice is set to 0 if there is no audio device available
  // or if the default device is set to an encoded format
  if (ret != noErr || !deviceId)
  {
    CLog::Log(LOGERROR, "CCoreAudioHardware::GetDefaultOutputDevice:"
              " Unable to identify default output device. Error = %s", GetError(ret).c_str());
    // if there was no error and no deviceId was returned
    // return the last known default device
    if (ret == noErr && !deviceId)
      return lastDeviceId;
    else
      return 0;
  }
  
  lastDeviceId = deviceId;
  
  return deviceId;
}

static void GetOutputDeviceName(std::string& name)
{
  name = "Default";
  AudioDeviceID deviceId = CCoreAudioHardware::GetDefaultOutputDevice();
  
  if (deviceId)
  {
    AudioObjectPropertyAddress  propertyAddress;
    propertyAddress.mScope    = kAudioObjectPropertyScopeGlobal;
    propertyAddress.mElement  = kAudioObjectPropertyElementMaster;
    propertyAddress.mSelector = kAudioObjectPropertyName;
    
    CFStringRef theDeviceName = NULL;
    UInt32 propertySize = sizeof(CFStringRef);
    OSStatus ret = AudioObjectGetPropertyData(deviceId, &propertyAddress, 0, NULL, &propertySize, &theDeviceName);
    if (ret != noErr)
      return;
    
    DarwinCFStringRefToUTF8String(theDeviceName, name);
    
    CFRelease(theDeviceName);
  }
}

static int GetTotalOutputChannels(AudioDeviceID deviceId)
{
  int channels = 0;

  if (!deviceId)
    return channels;

  AudioObjectPropertyAddress  propertyAddress;
  propertyAddress.mScope    = kAudioDevicePropertyScopeOutput;
  propertyAddress.mElement  = 0;
  propertyAddress.mSelector = kAudioDevicePropertyStreamConfiguration;

  UInt32 size = 0;
  OSStatus ret = AudioObjectGetPropertyDataSize(deviceId, &propertyAddress, 0, NULL, &size);
  if (ret != noErr)
    return channels;

  AudioBufferList *pList = (AudioBufferList*)malloc(size);
  ret = AudioObjectGetPropertyData(deviceId, &propertyAddress, 0, NULL, &size, pList);
  if (ret == noErr)
  {
    for (UInt32 buffer = 0; buffer < pList->mNumberBuffers; ++buffer)
      channels += pList->mBuffers[buffer].mNumberChannels;
  }
  else
  {
    CLog::Log(LOGERROR, "Unable to get device output channels - id: 0x%04x. Error = %s",
      (uint)deviceId, GetError(ret).c_str());
  }

  free(pList);

  return channels;
}

static int GetOutputDevicesIDs(std::list<AudioDeviceID> *pList)
{
  int found = 0;
  if (!pList)
    return found;

  // Obtain a list of all available audio devices
  AudioObjectPropertyAddress propertyAddress; 
  propertyAddress.mScope    = kAudioObjectPropertyScopeGlobal; 
  propertyAddress.mElement  = kAudioObjectPropertyElementMaster;
  propertyAddress.mSelector = kAudioHardwarePropertyDevices; 

  UInt32 size = 0;
  OSStatus ret = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &size); 
  if (ret != noErr)
  {
    CLog::Log(LOGERROR, "GetOutputDevicesIDs:"
      " Unable to retrieve the size of the list of available devices. Error = %s", GetError(ret).c_str());
    return found;
  }

  size_t deviceCount = size / sizeof(AudioDeviceID);
  AudioDeviceID* pDevices = new AudioDeviceID[deviceCount];
  ret = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propertyAddress, 0, NULL, &size, pDevices);
  if (ret != noErr)
  {
    CLog::Log(LOGERROR, "GetOutputDevicesIDs:"
      " Unable to retrieve the list of available devices. Error = %s", GetError(ret).c_str());
  }
  else
  {
    for (size_t dev = 0; dev < deviceCount; dev++)
    {
      // skip devices with no output channels
      if (GetTotalOutputChannels(pDevices[dev]) == 0)
        continue;
      found++;
      pList->push_back(pDevices[dev]);
    }
  }
  delete[] pDevices;

  return found;
}

typedef std::vector< std::pair<AudioDeviceID, CAEDeviceInfo> > CADeviceList;
static CADeviceList s_devices;

static void EnumerateDevices(CADeviceList &list)
{
  CAEDeviceInfo device;
  std::list<AudioDeviceID> deviceIDList;

  std::string defaultDeviceName;
  GetOutputDeviceName(defaultDeviceName);
  
  GetOutputDevicesIDs(&deviceIDList);
  while (!deviceIDList.empty())
  {
    AudioDeviceID deviceID = deviceIDList.front();

    device.m_channels.Reset();
    device.m_dataFormats.clear();
    device.m_sampleRates.clear();

    device.m_deviceType = AE_DEVTYPE_PCM;
    device.m_deviceName = GetDeviceName(deviceID);
    device.m_displayName = device.m_deviceName;
    device.m_displayNameExtra = "";

    if (device.m_deviceName.find("HDMI") != std::string::npos)
      device.m_deviceType = AE_DEVTYPE_HDMI;

    CLog::Log(LOGDEBUG, "EnumerateDevices:Device(%s)" , device.m_deviceName.c_str());
    AudioStreamID *streams = GetDeviceStreams(deviceID);
    if (streams)
    {
      for (int j = 0; streams[j] != kAudioHardwareBadStreamError; j++)
      {
        AudioStreamBasicDescription *descs = GetStreamDescriptions(streams[j]);
        if (descs)
        {
          for (int i = 0; descs[i].mFormatID != 0; i++)
          {
            // std::string formatString;
            // CLog::Log(LOGDEBUG, "EnumerateDevices:Format(%s)" ,
            //   StreamDescriptionToString(descs[i], formatString));

            // add stream format info
            switch (descs[i].mFormatID)
            {
              case kAudioFormatAC3:
              case kAudioFormat60958AC3:
                if (!HasDataFormat(device.m_dataFormats, AE_FMT_AC3))
                  device.m_dataFormats.push_back(AE_FMT_AC3);
                if (!HasDataFormat(device.m_dataFormats, AE_FMT_DTS))
                  device.m_dataFormats.push_back(AE_FMT_DTS);
                // if we are not hdmi, this is an S/PDIF device
                if (device.m_deviceType != AE_DEVTYPE_HDMI)
                  device.m_deviceType = AE_DEVTYPE_IEC958;
                break;
              default:
                AEDataFormat format = AE_FMT_INVALID;
                switch(descs[i].mBitsPerChannel)
                {
                  case 16:
                    if (descs[i].mFormatFlags & kAudioFormatFlagIsBigEndian)
                      format = AE_FMT_S16BE;
                    else
                      format = AE_FMT_S16LE;
                    break;
                  case 24:
                    if (descs[i].mFormatFlags & kAudioFormatFlagIsBigEndian)
                      format = AE_FMT_S24BE3;
                    else
                      format = AE_FMT_S24LE3;
                    break;
                  case 32:
                    if (descs[i].mFormatFlags & kAudioFormatFlagIsFloat)
                      format = AE_FMT_FLOAT;
                    else
                    {
                      if (descs[i].mFormatFlags & kAudioFormatFlagIsBigEndian)
                        format = AE_FMT_S32BE;
                      else
                        format = AE_FMT_S32LE;
                    }
                    break;
                }
                if (format != AE_FMT_INVALID && !HasDataFormat(device.m_dataFormats, format))
                  device.m_dataFormats.push_back(format);
                break;
            }
            // special check for AE_FMT_LPCM
            if (descs[i].mChannelsPerFrame == 8)
            {
              if (!HasDataFormat(device.m_dataFormats, AE_FMT_LPCM))
                device.m_dataFormats.push_back(AE_FMT_LPCM);
            }

            // add channel info
            CAEChannelInfo channel_info;
            for (UInt32 chan = 0; chan < CA_MAX_CHANNELS && chan < descs[i].mChannelsPerFrame; ++chan)
            {
              if (!device.m_channels.HasChannel(CAChannelMap[chan]))
                device.m_channels += CAChannelMap[chan];
              channel_info += CAChannelMap[chan];
            }

            // add sample rate info
            if (!HasSampleRate(device.m_sampleRates, descs[i].mSampleRate))
              device.m_sampleRates.push_back(descs[i].mSampleRate);
          }
          free(descs);
        }
      }
      free(streams);
    }

    list.push_back(std::make_pair(deviceID, device));

    // add the default device with m_deviceName = default
    if(defaultDeviceName == device.m_deviceName)
    {
      device.m_deviceName = std::string("default");
      device.m_displayName = std::string("default");
      device.m_displayNameExtra = std::string("");
      list.push_back(std::make_pair(deviceID, device));
    }

    deviceIDList.pop_front();
  }
}


////////////////////////////////////////////////////////////////////////////////////////////
CAESinkDARWINOSX::CAESinkDARWINOSX()
: m_latentFrames(0)
{
  SInt32 major, minor;
  Gestalt(gestaltSystemVersionMajor, &major);
  Gestalt(gestaltSystemVersionMinor, &minor);

  // By default, kAudioHardwarePropertyRunLoop points at the process's main thread on SnowLeopard,
  // If your process lacks such a run loop, you can set kAudioHardwarePropertyRunLoop to NULL which
  // tells the HAL to run it's own thread for notifications (which was the default prior to SnowLeopard).
  // So tell the HAL to use its own thread for similar behavior under all supported versions of OSX.
  if (major == 10 && minor >= 6)
  {
    CFRunLoopRef theRunLoop = NULL;
    AudioObjectPropertyAddress theAddress = {
      kAudioHardwarePropertyRunLoop,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMaster
    };
    OSStatus theError = AudioObjectSetPropertyData(kAudioObjectSystemObject,
                                                   &theAddress, 0, NULL, sizeof(CFRunLoopRef), &theRunLoop);
    if (theError != noErr)
    {
      CLog::Log(LOGERROR, "CCoreAudioAE::constructor: kAudioHardwarePropertyRunLoop error.");
    }
  }
}

CAESinkDARWINOSX::~CAESinkDARWINOSX()
{
}

bool CAESinkDARWINOSX::Initialize(AEAudioFormat &format, std::string &device)
{
  AudioDeviceID deviceID = 0;
  for (size_t i = 0; i < s_devices.size(); i++)
  {
    if (device.find(s_devices[i].second.m_deviceName) != std::string::npos)
    {
      m_info = s_devices[i].second;
      deviceID = s_devices[i].first;
      break;
    }
  }

  m_format = format;

  // default to 44100,
  // then check if we can support the requested rate.
  unsigned int sampleRate = 44100;
  for (size_t i = 0; i < m_info.m_sampleRates.size(); i++)
  {
    if (m_format.m_sampleRate == m_info.m_sampleRates[i])
    {
      sampleRate = m_format.m_sampleRate;
      break;
    }
  }

  // default to AE_FMT_FLOAT,
  // then check if we can support the requested format.
  AEDataFormat dataFormat = AE_FMT_FLOAT;
  for (size_t i = 0; i < m_info.m_dataFormats.size(); i++)
  {
    if (m_format.m_dataFormat == m_info.m_dataFormats[i])
    {
      dataFormat = m_format.m_dataFormat;
      break;
    }
  }

  m_format.m_sampleRate = sampleRate;
  m_format.m_dataFormat = dataFormat;
  m_format.m_channelLayout = m_info.m_channels;
  m_format.m_frameSize = m_format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(m_format.m_dataFormat) >> 3);

  /* OSX specific shit here... */
  AudioStreamBasicDescription audioFormat = {};
  audioFormat.mFormatID = kAudioFormatLinearPCM;
  audioFormat.mSampleRate = m_format.m_sampleRate;
  audioFormat.mFramesPerPacket = 1;
  audioFormat.mChannelsPerFrame= m_format.m_channelLayout.Count();
  audioFormat.mBitsPerChannel  = CAEUtil::DataFormatToBits(m_format.m_dataFormat);
  audioFormat.mBytesPerFrame   = audioFormat.mChannelsPerFrame * (audioFormat.mBitsPerChannel >> 3);
  audioFormat.mBytesPerPacket  = audioFormat.mBytesPerFrame;
  audioFormat.mFormatFlags    |= kLinearPCMFormatFlagIsPacked;
  if (dataFormat == AE_FMT_FLOAT)
    audioFormat.mFormatFlags  |= kLinearPCMFormatFlagIsFloat;
  else
    audioFormat.mFormatFlags  |= kLinearPCMFormatFlagIsSignedInteger;
#if DO_440HZ_TONE_TEST
  SineWaveGeneratorInitWithFrequency(&m_SineWaveGenerator, 440.0, audioFormat.mSampleRate);
#endif

  m_device.Open(deviceID);

  /* see if we have an appropriate stream */
  AudioStreamBasicDescription outputFormat = {0};
  AudioStreamID outputStream = 0;

  // Fetch a list of the streams defined by the output device
  AudioStreamIdList streams;
  UInt32  streamIndex = 0;
  m_device.GetStreams(&streams);

  while (!streams.empty())
  {
    // Get the next stream
    CCoreAudioStream stream;
    stream.Open(streams.front());
    streams.pop_front(); // We copied it, now we are done with it

    CLog::Log(LOGDEBUG, "%s: Found %s stream - id: 0x%04X, Terminal Type: 0x%04X",
              __FUNCTION__, stream.GetDirection() ? "Input" : "Output",
              (int)stream.GetId(),
              (unsigned int)stream.GetTerminalType());

    // Probe physical formats
    StreamFormatList physicalFormats;
    stream.GetAvailablePhysicalFormats(&physicalFormats);
    while (!physicalFormats.empty())
    {
      AudioStreamRangedDescription& desc = physicalFormats.front();
      std::string formatString;
      CLog::Log(LOGDEBUG, "%s: Considering Physical Format: %s", __FUNCTION__, StreamDescriptionToString(desc.mFormat, formatString));
      if (desc.mFormat.mSampleRate == sampleRate)
      {
        outputFormat = desc.mFormat; // Select this format
        // TODO: Is this technically correct? Will each stream have it's own IOProc buffer?
        outputStream = stream.GetId();
        break;
      }
      physicalFormats.pop_front();
    }

    // TODO: How do we determine if this is the right stream (not just the right format) to use?
    if (outputFormat.mFormatID)
      break; // We found a suitable format. No need to continue.
    streamIndex++;
  }

  unsigned int avgBytesPerSec = outputFormat.mChannelsPerFrame * (outputFormat.mBitsPerChannel>>3) * outputFormat.mSampleRate; // mBytesPerFrame is 0 for a cac3 stream
  std::string formatString;
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Selected stream[%u] - id: 0x%04X, Physical Format: %s (%u Bytes/sec.)", streamIndex, outputStream, StreamDescriptionToString(outputFormat, formatString), avgBytesPerSec);

  // Configure the output stream object
  m_outputStream.Open(outputStream); // This is the one we will keep
  AudioStreamBasicDescription virtualFormat;
  m_outputStream.GetVirtualFormat(&virtualFormat);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Previous Virtual Format: %s (%u Bytes/sec.)", StreamDescriptionToString(virtualFormat, formatString), avgBytesPerSec);
  AudioStreamBasicDescription previousPhysicalFormat;
  m_outputStream.GetPhysicalFormat(&previousPhysicalFormat);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: Previous Physical Format: %s (%u Bytes/sec.)", StreamDescriptionToString(previousPhysicalFormat, formatString), avgBytesPerSec);
  m_outputStream.SetPhysicalFormat(&outputFormat); // Set the active format (the old one will be reverted when we close)
  m_outputStream.GetVirtualFormat(&virtualFormat);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: New Virtual Format: %s (%u Bytes/sec.)", StreamDescriptionToString(virtualFormat, formatString), avgBytesPerSec);
  CLog::Log(LOGDEBUG, "CoreAudioRenderer::InitializeEncoded: New Physical Format: %s (%u Bytes/sec.)", StreamDescriptionToString(outputFormat, formatString), avgBytesPerSec);

  m_latentFrames = m_device.GetNumLatencyFrames();
  m_latentFrames += m_outputStream.GetNumLatencyFrames();

  m_buffer = new AERingBuffer(0.25 * avgBytesPerSec);
  CLog::Log(LOGDEBUG, "Buffer size: %u", (unsigned int)m_device.GetBufferSize());

  // Register for data request callbacks from the driver
  m_device.AddIOProc(renderCallback, this);

  /* TODO: Use the virtual format to determine our data format */
  m_format.m_dataFormat = AE_FMT_FLOAT;
  m_format.m_frameSize = m_format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(m_format.m_dataFormat) >> 3);
  m_format.m_frames = m_device.GetBufferSize();
  m_format.m_frameSamples = m_format.m_frames * m_format.m_channelLayout.Count();

  format = m_format;

  /* TODO: This doesn't appear to start the stream.  Changing the volume on my laptop gets the stream started... */
  m_device.Start();

  return true;
}

void CAESinkDARWINOSX::Deinitialize()
{
  m_device.Stop();
  m_device.RemoveIOProc();
  m_device.Close();
  if (m_buffer)
  {
    delete m_buffer;
    m_buffer = NULL;
  }
}

bool CAESinkDARWINOSX::IsCompatible(const AEAudioFormat &format, const std::string &device)
{
  return ((m_format.m_sampleRate    == format.m_sampleRate) &&
          (m_format.m_dataFormat    == format.m_dataFormat) &&
          (m_format.m_channelLayout == format.m_channelLayout));
}

double CAESinkDARWINOSX::GetDelay()
{
  if (m_buffer)
  {
    // Calculate the duration of the data in the cache
    double delay = (double)m_buffer->GetReadSize() / (double)m_format.m_frameSize;
    // TODO: Obtain hardware/os latency for better accuracy
    delay += (double)m_latentFrames;
    delay /= (double)m_format.m_sampleRate;
    return delay;
  }
  return 0.0;
}

double CAESinkDARWINOSX::GetCacheTotal()
{
  return (double)m_buffer->GetMaxSize() / (double)(m_format.m_frameSize * m_format.m_sampleRate);
}

unsigned int CAESinkDARWINOSX::AddPackets(uint8_t *data, unsigned int frames, bool hasAudio, bool blocking)
{
  if (m_buffer)
  {
    /* TODO: Check for draining?? */

    unsigned int write_frames = m_buffer->GetWriteSize() / m_format.m_frameSize;
    if (write_frames > frames)
      write_frames = frames;

#if DO_440HZ_TONE_TEST
    int16_t *samples = (int16_t*)data;
    for (unsigned int j = 0; j < (write_frames * m_sink_frameSize)/2; j++)
    {
      int16_t sample = SineWaveGeneratorNextSample(&m_SineWaveGenerator);
      samples[2 * j] = sample;
      samples[2 * j + 1] = sample;
    }
#endif

    if (hasAudio && write_frames)
      m_buffer->Write(data, write_frames * m_format.m_frameSize);

    return hasAudio ? write_frames:frames;
  }
  return frames;
}

void CAESinkDARWINOSX::Drain()
{
  CLog::Log(LOGDEBUG, "CAESinkDARWINOSX::Drain");
  /* TODO: What to do here?? */
}

void CAESinkDARWINOSX::EnumerateDevicesEx(AEDeviceInfoList &list, bool force)
{
  EnumerateDevices(s_devices);
  list.clear();
  for (CADeviceList::const_iterator i = s_devices.begin(); i != s_devices.end(); ++i)
    list.push_back(i->second);
}

OSStatus CAESinkDARWINOSX::renderCallback(AudioDeviceID inDevice, const AudioTimeStamp* inNow, const AudioBufferList* inInputData, const AudioTimeStamp* inInputTime, AudioBufferList* outOutputData, const AudioTimeStamp* inOutputTime, void* inClientData)
{
  CAESinkDARWINOSX *sink = (CAESinkDARWINOSX*)inClientData;

  for (size_t i = 0; i < outOutputData->mNumberBuffers; ++i)
  {
    /* TODO: Should we do something special when draining?
    if (sink->m_draining)
    {
      sink->m_buffer->Read(NULL, sink->m_buffer->GetReadSize());
      sink->m_draining = false;
    } */

    int readBytes = sink->m_buffer->GetReadSize();
    if (readBytes > 0)
    {
      int freeBytes = outOutputData->mBuffers[i].mDataByteSize;
//      CLog::Log(LOGDEBUG, "renderCallback buffer %i, size %i", i, freeBytes);
      if (readBytes < freeBytes)
      {
        // we have less bytes to write than space in the buffer.
        // write what we have and zero fill the reset.
        CLog::Log(LOGDEBUG, "Zero-filling %i bytes", freeBytes - readBytes);
        sink->m_buffer->Read((unsigned char*)outOutputData->mBuffers[i].mData, readBytes);
        memset((char*)outOutputData->mBuffers[i].mData + readBytes, 0x00, freeBytes - readBytes);
      }
      else
      {
        // we have more bytes to write than space in the buffer.
        // write the full buffer size avaliable, the rest goes into the next buffer
        sink->m_buffer->Read((unsigned char*)outOutputData->mBuffers[i].mData, freeBytes);
      }
    }
    else
    {
      // nothing to write or mute, zero fill the buffer.
      CLog::Log(LOGDEBUG, "Zero-filling %i bytes", outOutputData->mBuffers[i].mDataByteSize);
      memset(outOutputData->mBuffers[i].mData, 0x00, outOutputData->mBuffers[i].mDataByteSize);
    }
  }

  return noErr;
}
