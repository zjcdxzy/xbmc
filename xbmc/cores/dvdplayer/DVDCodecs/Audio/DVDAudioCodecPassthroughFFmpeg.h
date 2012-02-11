#pragma once

/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
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

#include <list>
#include "system.h"
#include "DllAvFormat.h"
#include "DllAvCodec.h"
#include "DllAvUtil.h"

#include "../DVDFactoryCodec.h"
#include "DVDAudioCodec.h"

class IDVDAudioEncoder;

class CDVDAudioCodecPassthroughFFmpeg : public CDVDAudioCodec
{
public:
  CDVDAudioCodecPassthroughFFmpeg();
  virtual ~CDVDAudioCodecPassthroughFFmpeg();

  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int Decode(BYTE* pData, int iSize);
  virtual int GetData(BYTE** dst);
  virtual void Reset();
  virtual int GetChannels();
  virtual enum PCMChannels *GetChannelMap() { static enum PCMChannels map[2] = {PCM_FRONT_LEFT, PCM_FRONT_RIGHT}; return map; }
  virtual int GetSampleRate();
  virtual int GetBitsPerSample();
  virtual bool NeedPassthrough() { return true; }
  virtual const char* GetName()  { return "PassthroughFFmpeg"; }
  virtual int GetBufferSize();
  virtual IAudioRenderer::EEncoded GetRenderEncoding() { return m_Encoding; }

private:
  DllAvFormat m_dllAvFormat;
  DllAvUtil   m_dllAvUtil;
  DllAvCodec  m_dllAvCodec;

  enum StreamType
  {
    STREAM_TYPE_NULL,
    STREAM_TYPE_AC3,
    STREAM_TYPE_DTS,
    STREAM_TYPE_DTSHD,
    STREAM_TYPE_DTSHD_CORE,
    STREAM_TYPE_EAC3,
    STREAM_TYPE_MLP,
    STREAM_TYPE_TRUEHD
  };
  
  typedef struct
  {
    int      size;
    uint8_t *data;
  } DataPacket;

  typedef struct
  {
    AVFormatContext       *m_pFormat;
    AVStream              *m_pStream;
    std::list<DataPacket*> m_OutputBuffer;
    unsigned int           m_OutputSize;
    bool                   m_WroteHeader;
    unsigned char          m_BCBuffer[AVCODEC_MAX_AUDIO_FRAME_SIZE];
    unsigned int           m_Consumed;
    unsigned int           m_BufferSize;
    uint8_t               *m_Buffer;
  } Muxer;

  Muxer      m_SPDIF, m_ADTS;
  bool       SetupMuxer(CDVDStreamInfo &hints, CStdString muxerName, Muxer &muxer);
  static int MuxerReadPacket(void *opaque, uint8_t *buf, int buf_size);
  void       WriteFrame(Muxer &muxer, uint8_t *pData, int iSize);
  int        GetMuxerData(Muxer &muxer, uint8_t** dst);
  void       ResetMuxer(Muxer &muxer);
  void       DisposeMuxer(Muxer &muxer);

  bool m_bSupportsAC3Out;
  bool m_bSupportsAACOut;
  bool m_bSupportsMP1Out;
  bool m_bSupportsMP2Out;
  bool m_bSupportsMP3Out;
  bool m_bSupportsEAC3Out;
  bool m_bSupportsTHDOut;
  bool m_bSupportsMLPOut;
  int  m_iSupportsDTSLvl;
  bool m_bDTSCoreOut;

  CDVDAudioCodec   *m_Codec;
  IDVDAudioEncoder *m_Encoder;
  bool              m_InitEncoder;
  unsigned int      m_EncPacketSize;
  BYTE             *m_DecodeBuffer;
  unsigned int      m_DecodeSize;
  bool SupportsFormat(CDVDStreamInfo &hints);
  bool SetupEncoder  (CDVDStreamInfo &hints);

  uint8_t      m_FrameBuffer[AVCODEC_MAX_AUDIO_FRAME_SIZE];
  unsigned int m_Size;
  unsigned int m_InFrameSize;
  unsigned int m_OutFrameSize;
  bool         m_LostSync;
  bool         m_coreOnly;
  StreamType   m_StreamType;
  int          m_SampleRate;
  int          m_DataRate;
  int          m_Channels;
  int          m_SubStreams;
  AVCRC        m_crcMLP[1024];  /* MLP crc table */
  IAudioRenderer::EEncoded m_Encoding;

  unsigned int (CDVDAudioCodecPassthroughFFmpeg::*m_pSyncFrame)(BYTE* pData, unsigned int iSize);
  unsigned int SyncAC3(BYTE* pData, unsigned int iSize);
  unsigned int SyncDTS(BYTE* pData, unsigned int iSize);
  unsigned int SyncAAC(BYTE* pData, unsigned int iSize);
  unsigned int SyncMLP(BYTE* pData, unsigned int iSize);
};

