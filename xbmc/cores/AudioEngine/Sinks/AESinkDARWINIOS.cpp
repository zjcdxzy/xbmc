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

#include <sstream>
#include <AudioToolbox/AudioToolbox.h>

#define CA_MAX_CHANNELS 8
static enum AEChannel CAChannelMap[CA_MAX_CHANNELS + 1] = {
  AE_CH_FL , AE_CH_FR , AE_CH_BL , AE_CH_BR , AE_CH_FC , AE_CH_LFE , AE_CH_SL , AE_CH_SR ,
  AE_CH_NULL
};

/***************************************************************************************/
/***************************************************************************************/
#if DO_440HZ_TONE_TEST
static void SineWaveGeneratorInitWithFrequency(SineWaveGenerator *ctx, double frequency, double samplerate)
{
  // Given:
  //   frequency in cycles per second
  //   2*PI radians per sine wave cycle
  //   sample rate in samples per second
  //
  // Then:
  //   cycles     radians     seconds     radians
  //   ------  *  -------  *  -------  =  -------
  //   second      cycle      sample      sample
  ctx->currentPhase = 0.0;
  ctx->phaseIncrement = frequency * 2*M_PI / samplerate;
}

static int16_t SineWaveGeneratorNextSample(SineWaveGenerator *ctx)
{
  int16_t sample = INT16_MAX * sinf(ctx->currentPhase);
  
  ctx->currentPhase += ctx->phaseIncrement;
  // Keep the value between 0 and 2*M_PI
  while (ctx->currentPhase > 2*M_PI)
    ctx->currentPhase -= 2*M_PI;
  
  return sample / 4;
}
#endif

/***************************************************************************************/
/***************************************************************************************/
class CAAudioUnitSink
{
  public:
    CAAudioUnitSink();
   ~CAAudioUnitSink();

    bool        open(AudioStreamBasicDescription outputFormat);
    bool        close();
    bool        play(bool mute);
    bool        mute(bool mute);
    bool        pause();
    void        drain();
    bool        draining();
    double      getDelay();
    int         getReadSize();
    int         getWriteSize();
    int         write(uint8_t *data, unsigned int byte_count);

  private:
    bool        setupAudio();
    bool        checkAudioRoute();
    bool        checkSessionProperties();
    bool        activateAudioSession();
    void        deactivateAudioSession();
 
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
    AERingBuffer       *m_pcm_buffer;

    bool                m_mute;
    Float32             m_outputVolume;
    Float32             m_outputLatency;
    Float32             m_bufferDuration;
    double              m_samplingRate;
    int                 m_numBytesPerSample;
    bool                m_playing;
    bool                m_playing_saved;
    bool                m_draining;
};

CAAudioUnitSink::CAAudioUnitSink()
: m_initialized(false)
, m_activated(false)
, m_pcm_buffer(NULL)
, m_playing(false)
, m_playing_saved(false)
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
  m_draining      = false;
  m_outputFormat  = outputFormat;
  m_outputLatency = 0.0;
  m_bufferDuration= 0.0;
  m_outputVolume  = 1.0;
  m_samplingRate  = outputFormat.mSampleRate;
  m_numBytesPerSample = outputFormat.mChannelsPerFrame * outputFormat.mBitsPerChannel / 8;

  // 1/4 second pull buffer
  m_pcm_buffer = new AERingBuffer(0.25 * m_numBytesPerSample * m_samplingRate);

  return setupAudio();
}

bool CAAudioUnitSink::close()
{
  deactivateAudioSession();
  
  if (m_pcm_buffer)
    SAFE_DELETE(m_pcm_buffer);

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

void CAAudioUnitSink::drain()
{	
  m_draining = true;
}

bool CAAudioUnitSink::draining()
{	
  return m_draining;
}

double CAAudioUnitSink::getDelay()
{
  double delay = (double)m_pcm_buffer->GetReadSize();
  delay /= m_samplingRate   * m_numBytesPerSample;
  delay += m_bufferDuration + m_outputLatency;

  return delay;
}

int CAAudioUnitSink::getReadSize()
{
  return m_pcm_buffer->GetReadSize();
}

int CAAudioUnitSink::getWriteSize()
{
  return m_pcm_buffer->GetWriteSize();
}

int CAAudioUnitSink::write(uint8_t *data, unsigned int byte_count)
{
  unsigned int bytes_free  = m_pcm_buffer->GetWriteSize();
  unsigned int write_count = (unsigned int)byte_count <= bytes_free ? byte_count:bytes_free;

  return m_pcm_buffer->Write(data, write_count);
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

#if 0
  UInt32 ioDataSize;

  ioDataSize = sizeof(m_outputVolume);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareOutputVolume,
    &ioDataSize, &m_outputVolume) == noErr)
  NSLog(@"m_outputVolume(%f)", m_outputVolume);

  ioDataSize = sizeof(m_outputLatency);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareOutputLatency,
    &ioDataSize, &m_outputLatency) == noErr)
  NSLog(@"m_outputLatency(%f)", m_outputLatency);

  ioDataSize = sizeof(m_bufferDuration);
  if (AudioSessionGetProperty(kAudioSessionProperty_CurrentHardwareIOBufferDuration,
    &ioDataSize, &m_bufferDuration) == noErr)
  NSLog(@"m_bufferDuration(%f)", m_bufferDuration);
#endif

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

	for (size_t i = 0; i < ioData->mNumberBuffers; ++i)
	{
    // if muted or m_draining, flush read out the ringbuffer.
    if (sink->m_mute || sink->m_draining)
    {
      sink->m_pcm_buffer->Read(NULL, sink->m_pcm_buffer->GetReadSize());
      if (sink->m_draining)
        sink->m_draining = false;
    }

	  int readBytes = sink->m_pcm_buffer->GetReadSize();
	  if (readBytes > 0)
	  {
      int freeBytes = ioData->mBuffers[i].mDataByteSize;
      if (readBytes < freeBytes)
      {
        // we have less bytes to write than space in the buffer.
        // write what we have and zero fill the reset.
        sink->m_pcm_buffer->Read((unsigned char*)ioData->mBuffers[i].mData, readBytes);
        memset((char*)ioData->mBuffers[i].mData + readBytes, 0x00, freeBytes - readBytes) ;
      }
      else
      {
        // we have more bytes to write than space in the buffer.
        // write the full buffer size avaliable, the rest goes into the next buffer
        sink->m_pcm_buffer->Read((unsigned char*)ioData->mBuffers[i].mData, freeBytes);
      }
	  }
	  else
    {
      // nothing to write or mute, zero fill the buffer.
      memset(ioData->mBuffers[i].mData, 0x00, ioData->mBuffers[i].mDataByteSize);
    }
	}

  return noErr;
}

/***************************************************************************************/
/***************************************************************************************/
static void EnumerateDevices(AEDeviceInfoList &list)
{
  CAEDeviceInfo device;

  device.m_deviceName = "Default";
  device.m_displayName = device.m_deviceName;
  device.m_displayNameExtra = "";
  device.m_deviceType = AE_DEVTYPE_PCM;

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
{
}

CAESinkDARWINIOS::~CAESinkDARWINIOS()
{
}

bool CAESinkDARWINIOS::Initialize(AEAudioFormat &format, std::string &device)
{
  for (size_t i = 0; i < m_devices.size(); i++)
  {
    if (device.find(m_devices[i].m_deviceName) != std::string::npos)
    {
      m_info = m_devices[i];
      break;
    }
  }

  m_format = format;
  m_format.m_dataFormat = AE_FMT_S16LE;
  m_format.m_channelLayout = m_info.m_channels;
  m_format.m_frameSize = m_format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(m_format.m_dataFormat) >> 3);

  AudioStreamBasicDescription audioFormat = {};
  audioFormat.mFormatID = kAudioFormatLinearPCM;
  switch(m_format.m_sampleRate)
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
#if DO_440HZ_TONE_TEST
  SineWaveGeneratorInitWithFrequency(&m_SineWaveGenerator, 440.0, audioFormat.mSampleRate);
#endif

  m_audioSink = new CAAudioUnitSink;
  m_audioSink->open(audioFormat);

  m_sink_frameSize = m_format.m_channelLayout.Count() * CAEUtil::DataFormatToBits(m_format.m_dataFormat) >> 3;
  m_sinkbuffer_sec_per_byte = 1.0 / (double)(m_sink_frameSize * m_format.m_sampleRate);
  m_sinkbuffer_sec = (double)m_sinkbuffer_sec_per_byte * m_audioSink->getWriteSize();

  m_format.m_frames = m_audioSink->getWriteSize() / m_sink_frameSize;
  m_format.m_frameSamples = m_format.m_frames * m_format.m_channelLayout.Count();
  format = m_format;

  m_volume_changed = false;
  m_audioSink->play(false);

  return true;
}

void CAESinkDARWINIOS::Deinitialize()
{
  m_audioSink->close();
  SAFE_DELETE(m_audioSink);
}

bool CAESinkDARWINIOS::IsCompatible(const AEAudioFormat &format, const std::string &device)
{
  return ((m_format.m_sampleRate    == format.m_sampleRate) &&
          (m_format.m_dataFormat    == format.m_dataFormat) &&
          (m_format.m_channelLayout == format.m_channelLayout));
}

double CAESinkDARWINIOS::GetDelay()
{
  // this includes any latency due to AudioTrack buffer,
  // AudioMixer (if any) and audio hardware driver.

  double sinkbuffer_seconds_to_empty = m_sinkbuffer_sec_per_byte * (double)m_audioSink->getReadSize();
  return sinkbuffer_seconds_to_empty;
}

double CAESinkDARWINIOS::GetCacheTotal()
{
  // total amount that the audio sink can buffer in units of seconds

  return m_sinkbuffer_sec;
}

unsigned int CAESinkDARWINIOS::AddPackets(uint8_t *data, unsigned int frames, bool hasAudio, bool blocking)
{
  // write as many frames of audio as we can fit into our internal buffer.

  if (m_audioSink->draining())
    return frames;
  
  unsigned int write_frames = m_audioSink->getWriteSize() / m_sink_frameSize;
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
    m_audioSink->write(data, write_frames * m_sink_frameSize);

  // AddPackets runs under a non-idled AE thread we must block or sleep.
  // Trying to calc the optimal sleep is tricky so just a minimal sleep.
  if (blocking)
  {
    float sleep_msec = 0.8f * 1000 * m_sinkbuffer_sec_per_byte * write_frames * m_sink_frameSize;
    Sleep((int)sleep_msec);
  }

  return hasAudio ? write_frames:frames;
}

void CAESinkDARWINIOS::Drain()
{
  CLog::Log(LOGDEBUG, "CAESinkDARWINIOS::Drain");
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
