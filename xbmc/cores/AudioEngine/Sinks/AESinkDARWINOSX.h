#pragma once
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

#include "cores/AudioEngine/Interfaces/AESink.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"
#include "osx/CoreAudio.h"

class AERingBuffer;

class CAESinkDARWINOSX : public IAESink
{
public:
  virtual const char *GetName() { return "DARWINOSX"; }

  CAESinkDARWINOSX();
  virtual ~CAESinkDARWINOSX();

  virtual bool Initialize(AEAudioFormat &format, std::string &device);
  virtual void Deinitialize();
  virtual bool IsCompatible(const AEAudioFormat &format, const std::string &device);

  virtual double       GetDelay        ();
  virtual double       GetCacheTotal   ();
  virtual unsigned int AddPackets      (uint8_t *data, unsigned int frames, bool hasAudio, bool blocking = false);
  virtual void         Drain           ();
  static void          EnumerateDevicesEx(AEDeviceInfoList &list, bool force = false);

private:
  static OSStatus renderCallback(AudioDeviceID inDevice, const AudioTimeStamp* inNow, const AudioBufferList* inInputData, const AudioTimeStamp* inInputTime, AudioBufferList* outOutputData, const AudioTimeStamp* inOutputTime, void* inClientData);

  CAEDeviceInfo      m_info;
  AEAudioFormat      m_format;

  volatile bool      m_draining;

  CCoreAudioDevice   m_device;
  CCoreAudioStream   m_outputStream;
  unsigned int       m_latentFrames;

  AERingBuffer      *m_buffer;
};
