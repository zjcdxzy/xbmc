/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include "cores/AudioEngine/Sinks/AESinkDARWINIOS.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "cores/AudioEngine/Utils/AERingBuffer.h"
#include "osx/DarwinUtils.h"
#include "utils/log.h"
#include "threads/Condition.h"
#include "windowing/WindowingFactory.h"

#include <sstream>
#include <AudioToolbox/AudioToolbox.h>

#define CA_MAX_CHANNELS 8
static enum AEChannel CAChannelMap[CA_MAX_CHANNELS + 1] = {
  AE_CH_FL , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_FC , AE_CH_LFE , AE_CH_SL , AE_CH_SR ,
  AE_CH_NULL
};

/***************************************************************************************/
/***************************************************************************************/
class CAAudioUnitSink
{
  public:
    CAAudioUnitSink();
   ~CAAudioUnitSink();

    bool         open(AudioStreamBasicDescription outputFormat);
    bool         close();
    bool         play(bool mute);
    bool         mute(bool mute);
    bool         pause();
    void         drain();
    double       getDelay();
    double       cacheSize();
    unsigned int write(uint8_t *data, unsigned int byte_count);
    unsigned int chunkSize() { return m_bufferDuration * m_sampleRate; }

  private:
    bool         setupAudio();
    bool         checkAudioRoute();
    bool         checkSessionProperties();
    bool         activateAudioSession();
    void         deactivateAudioSession();
 
    // callbacks
    static void sessionPropertyCallback(void *inClientData,
                  AudioSessionPropertyID inID, UInt32 inDataSize, const void *inData);

    static void sessionInterruptionCallback(void *inClientData, UInt32 inInterruption);

    static OSStatus renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                  const AudioTimeStamp *inTimeStamp, UInt32 inOutputBusNumber, UInt32 inNumberFrames,
                  AudioBufferList *ioData);

    bool                m_setup;
    bool                m_initialized;
    bool                m_activated;
    AudioUnit           m_audioUnit;
    AudioStreamBasicDescription m_outputFormat;
    AERingBuffer       *m_buffer;

    bool                m_mute;
    Float32             m_outputVolume;
    Float32             m_outputLatency;
    Float32             m_bufferDuration;

    unsigned int        m_sampleRate;
    unsigned int        m_frameSize;
    unsigned int        m_frames;

    bool                m_playing;
    bool                m_playing_saved;
    volatile bool       m_started;
};

CAAudioUnitSink::CAAudioUnitSink()
: m_initialized(false)
, m_activated(false)
, m_buffer(NULL)
, m_playing(false)
, m_playing_saved(false)
, m_started(false)
{
}

CAAudioUnitSink::~CAAudioUnitSink()
{
  close();
}

bool CAAudioUnitSink::open(AudioStreamBasicDescription outputFormat)
{
  m_mute          = false;
  m_setup         = false;
  m_outputFormat  = outputFormat;
  m_outputLatency = 0.0;
  m_bufferDuration= 0.0;
  m_outputVolume  = 1.0;
  m_sampleRate    = (unsigned int)outputFormat.mSampleRate;
  m_frameSize     = outputFormat.mChannelsPerFrame * outputFormat.mBitsPerChannel / 8;

  /* TODO: Reduce the size of this buffer, pre-calculate the size based on how large
           the buffers are that CA calls us with in the renderCallback - perhaps call
           the checkSessionProperties() before running this? */
  m_buffer = new AERingBuffer(0.25 * m_frameSize * m_sampleRate);

  return setupAudio();
}

bool CAAudioUnitSink::close()
{
  deactivateAudioSession();
  
  delete m_buffer;
  m_buffer = NULL;

  m_started = false;
  return true;
}

bool CAAudioUnitSink::play(bool mute)
{    
  if (!m_playing)
  {
    if (activateAudioSession())
    {
      CAAudioUnitSink::mute(mute);
      m_playing = !AudioOutputUnitStart(m_audioUnit);
    }
  }

  return m_playing;
}

bool CAAudioUnitSink::mute(bool mute)
{
  m_mute = mute;

  return true;
}

bool CAAudioUnitSink::pause()
{	
  if (m_playing)
    m_playing = AudioOutputUnitStop(m_audioUnit);

  return m_playing;
}

double CAAudioUnitSink::getDelay()
{
  double delay = (double)m_buffer->GetReadSize() / m_frameSize;
  delay /= m_sampleRate;
  delay += m_bufferDuration + m_outputLatency;

  return delay;
}

double CAAudioUnitSink::cacheSize()
{
  return (double)m_buffer->GetMaxSize() / (double)(m_frameSize * m_sampleRate);
}

CCriticalSection mutex;
XbmcThreads::ConditionVariable condVar;

unsigned int CAAudioUnitSink::write(uint8_t *data, unsigned int frames)
{
  if (m_buffer->GetWriteSize() < frames * m_frameSize)
  { // no space to write - wait for a bit
    CSingleLock lock(mutex);
    if (!m_started)
      condVar.wait(lock);
    else
      condVar.wait(lock, 900 * frames / m_sampleRate);
  }

  unsigned int write_frames = std::min(frames, m_buffer->GetWriteSize() / m_frameSize);
  if (write_frames)
    m_buffer->Write(data, write_frames * m_frameSize);
  
  return write_frames;
}

void CAAudioUnitSink::drain()
{
  CCriticalSection mutex;
  unsigned int bytes = m_buffer->GetReadSize();
  while (bytes)
  {
    CSingleLock lock(mutex);
    condVar.wait(mutex, 900 * bytes / (m_sampleRate * m_frameSize));
    bytes = m_buffer->GetReadSize();
  }
}

bool CAAudioUnitSink::setupAudio()
{
  if (m_setup && m_audioUnit)
    return true;

  // Audio Session Setup
  UInt32 sessionCategory = kAudioSessionCategory_MediaPlayback;
  if (AudioSessionSetProperty(kAudioSessionProperty_AudioCategory,
    sizeof(sessionCategory), &sessionCategory) != noErr)
    return false;

  AudioSessionAddPropertyListener(kAudioSessionProperty_AudioRouteChange,
    sessionPropertyCallback, this);

  AudioSessionAddPropertyListener(kAudioSessionProperty_CurrentHardwareOutputVolume,
    sessionPropertyCallback, this);

#if !TARGET_IPHONE_SIMULATOR
  // set the buffer size, this affects the number of samples
  // that get rendered every time the audio callback is fired.
  Float32 preferredBufferSize = 0.0232;
  AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration,
    sizeof(preferredBufferSize), &preferredBufferSize);
#endif

  if (AudioSessionSetActive(true) != noErr)
    return false;

  // Audio Unit Setup
  // Describe a default output unit.
  AudioComponentDescription description = {};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_RemoteIO;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;

  // Get component
  AudioComponent component;
  component = AudioComponentFindNext(NULL, &description);
  if (AudioComponentInstanceNew(component, &m_audioUnit) != noErr)
    return false;
  
	// Set the output stream format
  UInt32 ioDataSize = sizeof(AudioStreamBasicDescription);
  if (AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
    kAudioUnitScope_Input, 0, &m_outputFormat, ioDataSize) != noErr)
    return false;

  // Attach a render callback on the unit
  AURenderCallbackStruct callbackStruct = {};
  callbackStruct.inputProc = renderCallback;
  callbackStruct.inputProcRefCon = this;
  if (AudioUnitSetProperty(m_audioUnit,
    kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
    0, &callbackStruct, sizeof(callbackStruct)) != noErr)
    return false;

	if (AudioUnitInitialize(m_audioUnit) != noErr)
    return false;

  checkSessionProperties();

  m_setup = true;

  return m_setup;
}

bool CAAudioUnitSink::checkAudioRoute()
{
  // why do we need to know the audio route ?
  CFStringRef route;
  UInt32 propertySize = sizeof(CFStringRef);
  if (AudioSessionGetProperty(kAudioSessionProperty_AudioRoute, &propertySize, &route) != noErr)
    return false;

  return true;
}

bool CAAudioUnitSink::checkSessionProperties()
{
  checkAudioRoute();

  UInt32 ioDataSize;
  ioDataSize = sizeof(m_outputVolume);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareOutputVolume,
    &ioDataSize, &m_outputVolume) == noErr)

  ioDataSize = sizeof(m_outputLatency);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareOutputLatency,
    &ioDataSize, &m_outputLatency) == noErr)

  ioDataSize = sizeof(m_bufferDuration);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareIOBufferDuration,
    &ioDataSize, &m_bufferDuration) == noErr)
  CLog::Log(LOGDEBUG, "%s: volume = %f, latency = %f, buffer = %f", __FUNCTION__, m_outputVolume, m_outputLatency, m_bufferDuration);
  return true;
}

bool CAAudioUnitSink::activateAudioSession()
{
  if (!m_activated)
  {
    if (!m_initialized)
    {
      OSStatus osstat = AudioSessionInitialize(NULL, kCFRunLoopDefaultMode, sessionInterruptionCallback, this);
      if (osstat == kAudioSessionNoError || osstat == kAudioSessionAlreadyInitialized)
        m_initialized = true;
      else
        return false;
    }
    if (checkAudioRoute() && setupAudio())
      m_activated = true;
  }

  return m_activated;
}

void CAAudioUnitSink::deactivateAudioSession()
{
  if (m_activated)
  {
    pause();
    AudioUnitUninitialize(m_audioUnit);
    AudioComponentInstanceDispose(m_audioUnit), m_audioUnit = NULL;
    AudioSessionSetActive(false);
    AudioSessionRemovePropertyListenerWithUserData(kAudioSessionProperty_AudioRouteChange,
      sessionPropertyCallback, this);
    AudioSessionRemovePropertyListenerWithUserData(kAudioSessionProperty_CurrentHardwareOutputVolume,
      sessionPropertyCallback, this);

    m_setup = false;
    m_activated = false;
  }
}

void CAAudioUnitSink::sessionPropertyCallback(void *inClientData,
  AudioSessionPropertyID inID, UInt32 inDataSize, const void *inData)
{
  CAAudioUnitSink *sink = (CAAudioUnitSink*)inClientData;

  if (inID == kAudioSessionProperty_AudioRouteChange)
  {
    if (sink->checkAudioRoute())
      sink->checkSessionProperties();
  }
  else if (inID == kAudioSessionProperty_CurrentHardwareOutputVolume)
  {
    if (inData && inDataSize == 4)
      sink->m_outputVolume = *(float*)inData;
  }
}

void CAAudioUnitSink::sessionInterruptionCallback(void *inClientData, UInt32 inInterruption)
{    
  CAAudioUnitSink *sink = (CAAudioUnitSink*)inClientData;

  if (inInterruption == kAudioSessionBeginInterruption)
  {
    CLog::Log(LOGDEBUG, "Bgn interuption");
    sink->m_playing_saved = sink->m_playing;
    sink->pause();
  }
  else if (inInterruption == kAudioSessionEndInterruption)
  {
    CLog::Log(LOGDEBUG, "End interuption");
    if (sink->m_playing_saved)
    {
      sink->m_playing_saved = false;
      sink->play(sink->m_mute);
    }
  }
}

OSStatus CAAudioUnitSink::renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
  const AudioTimeStamp *inTimeStamp, UInt32 inOutputBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
  CAAudioUnitSink *sink = (CAAudioUnitSink*)inRefCon;

  sink->m_started = true;

	if (ioData->mNumberBuffers > 0)
	{
    /* buffers appear to come from CA already zero'd, so just copy what is wanted */
    unsigned int wanted = ioData->mBuffers[0].mDataByteSize;
    unsigned int bytes = std::min(sink->m_buffer->GetReadSize(), wanted);
    sink->m_buffer->Read((unsigned char*)ioData->mBuffers[0].mData, bytes);
    if (bytes != wanted)
      CLog::Log(LOGERROR, "%s: %sFLOW (%i vs %i) bytes", __FUNCTION__, bytes > wanted ? "OVER" : "UNDER", bytes, wanted);
  }
  // tell the sink we're good for more data
  condVar.notifyAll();

  return noErr;
}

/***************************************************************************************/
/***************************************************************************************/
static void EnumerateDevices(AEDeviceInfoList &list)
{
  CAEDeviceInfo device;

  device.m_deviceName = "default";
  device.m_displayName = "Default";
  device.m_displayNameExtra = "";
#if defined(TARGET_DARWIN_IOS_ATV2)
  device.m_deviceType = AE_DEVTYPE_IEC958;
#else
  // TODO screen changing on ios needs to call
  // devices changed once this is available in activae
  if (g_Windowing.GetCurrentScreen() > 0)
    device.m_deviceType = AE_DEVTYPE_IEC958; //allow passthrough for tvout
  else
    device.m_deviceType = AE_DEVTYPE_PCM;
#endif

  // add channel info
  CAEChannelInfo channel_info;
  for (UInt32 chan = 0; chan < 2; ++chan)
  {
    if (!device.m_channels.HasChannel(CAChannelMap[chan]))
      device.m_channels += CAChannelMap[chan];
    channel_info += CAChannelMap[chan];
  }

  device.m_sampleRates.push_back(44100);
  device.m_sampleRates.push_back(48000);

  device.m_dataFormats.push_back(AE_FMT_S16LE);
  //device.m_dataFormats.push_back(AE_FMT_S24LE3);
  //device.m_dataFormats.push_back(AE_FMT_S32LE);
  //device.m_dataFormats.push_back(AE_FMT_FLOAT);

  CLog::Log(LOGDEBUG, "EnumerateDevices:Device(%s)" , device.m_deviceName.c_str());

  list.push_back(device);
}

/***************************************************************************************/
/***************************************************************************************/
AEDeviceInfoList CAESinkDARWINIOS::m_devices;

CAESinkDARWINIOS::CAESinkDARWINIOS()
:   m_audioSink(NULL)
{
}

CAESinkDARWINIOS::~CAESinkDARWINIOS()
{
}

bool CAESinkDARWINIOS::Initialize(AEAudioFormat &format, std::string &device)
{
  bool found = false;
  for (size_t i = 0; i < m_devices.size(); i++)
  {
    if (device.find(m_devices[i].m_deviceName) != std::string::npos)
    {
      m_info = m_devices[i];
      found = true;
      break;
    }
  }
  
  if (!found)
    return false;

  format.m_dataFormat = AE_FMT_S16LE;
  format.m_channelLayout = m_info.m_channels;
  format.m_frameSize = format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(format.m_dataFormat) >> 3);

  AudioStreamBasicDescription audioFormat = {};
  audioFormat.mFormatID = kAudioFormatLinearPCM;
  switch(format.m_sampleRate)
  {
    case 11025:
    case 22050:
    case 44100:
    case 88200:
    case 176400:
      audioFormat.mSampleRate = 44100;
      break;
    default:
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 32000:
    case 48000:
    case 96000:
    case 192000:
    case 384000:
      audioFormat.mSampleRate = 48000;
      break;
  }
  audioFormat.mFramesPerPacket = 1;
  audioFormat.mChannelsPerFrame= 2;
  audioFormat.mBitsPerChannel  = 16;
  audioFormat.mBytesPerFrame   = 4;
  audioFormat.mBytesPerPacket  = 4;
  audioFormat.mFormatFlags    |= kLinearPCMFormatFlagIsPacked;
  audioFormat.mFormatFlags    |= kLinearPCMFormatFlagIsSignedInteger;

  m_audioSink = new CAAudioUnitSink;
  m_audioSink->open(audioFormat);

  format.m_frames = m_audioSink->chunkSize();
  format.m_frameSamples = m_format.m_frames * m_format.m_channelLayout.Count();
  m_format = format;

  m_volume_changed = false;
  m_audioSink->play(false);

  return true;
}

void CAESinkDARWINIOS::Deinitialize()
{
  if (m_audioSink)
    m_audioSink->close();

  delete m_audioSink;
  m_audioSink = NULL;
}

bool CAESinkDARWINIOS::IsCompatible(const AEAudioFormat &format, const std::string &device)
{
  return ((m_format.m_sampleRate    == format.m_sampleRate) &&
          (m_format.m_dataFormat    == format.m_dataFormat) &&
          (m_format.m_channelLayout == format.m_channelLayout));
}

double CAESinkDARWINIOS::GetDelay()
{
  if (m_audioSink)
    return m_audioSink->getDelay();
  return 0.0;
}

double CAESinkDARWINIOS::GetCacheTotal()
{
  if (m_audioSink)
    return m_audioSink->cacheSize();
  return 0.0;
}

unsigned int CAESinkDARWINIOS::AddPackets(uint8_t *data, unsigned int frames, bool hasAudio, bool blocking)
{
  if (m_audioSink)
    return m_audioSink->write(data, frames);
  return 0;
}

void CAESinkDARWINIOS::Drain()
{
  if (m_audioSink)
    m_audioSink->drain();
}

bool CAESinkDARWINIOS::HasVolume()
{
  return false;
}

void  CAESinkDARWINIOS::SetVolume(float scale)
{
  // CoreAudio uses fixed steps, reverse scale back to percent
  float gain = CAEUtil::ScaleToGain(scale);
  m_volume = CAEUtil::GainToPercent(gain);
  m_volume_changed = true;
}

void CAESinkDARWINIOS::EnumerateDevicesEx(AEDeviceInfoList &list, bool force)
{
  EnumerateDevices(m_devices);
  list = m_devices;
}
