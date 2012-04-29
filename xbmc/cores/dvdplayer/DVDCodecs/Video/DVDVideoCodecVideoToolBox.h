#pragma once
/*
 *      Copyright (C) 2010 Team XBMC
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

#if defined(HAVE_VIDEOTOOLBOXDECODER)

#include <queue>

#include "DVDVideoCodec.h"
#include <CoreVideo/CoreVideo.h>
//#include <CoreMedia/CoreMedia.h>
//#include <CoreMedia/CMFormatDescription.h>
#include <CoreVideo/CoreVideo.h>                                                                                                                                                                                                            
#include <CoreVideo/CVHostTime.h> 

  typedef CFTypeRef CMFormatDescriptionRef;


// tracks a frame in and output queue in display order
typedef struct vtframe_queue {
  double              dts;
  double              pts;
  int                 width;
  int                 height;
  double              sort_time;
  FourCharCode        pixel_buffer_format;
  CVPixelBufferRef    pixel_buffer_ref;
  struct vtframe_queue  *nextframe;
} vtframe_queue;

class DllAvUtil;
class DllAvFormat;
class DllLibVTBDecoder;
class CDVDVideoCodecVideoToolBox : public CDVDVideoCodec
{
public:
  CDVDVideoCodecVideoToolBox();
  virtual ~CDVDVideoCodecVideoToolBox();

  // Required overrides
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose(void);
  virtual int  Decode(BYTE *pData, int iSize, double dts, double pts);
  virtual void Reset(void);
  virtual bool GetPicture(DVDVideoPicture *pDvdVideoPicture);
  virtual bool ClearPicture(DVDVideoPicture* pDvdVideoPicture);
  virtual void SetDropState(bool bDrop);
  virtual const char* GetName(void) { return (const char*)m_pFormatName; }

protected:
  void DisplayQueuePop(void);
  void CreateVTSession(int width, int height, CMFormatDescriptionRef fmt_desc);
  void DestroyVTSession(void);
  static void VTDecoderCallback(
    void *refcon, CFDictionaryRef frameInfo,
    OSStatus status, UInt32 infoFlags, CVBufferRef imageBuffer);

  void              *m_vt_session;    // opaque videotoolbox session
  CMFormatDescriptionRef m_fmt_desc;

  const char        *m_pFormatName;
  bool              m_DropPictures;
  DVDVideoPicture   m_videobuffer;

  double            m_sort_time_offset;
  pthread_mutex_t   m_queue_mutex;    // mutex protecting queue manipulation
  vtframe_queue       *m_display_queue; // display-order queue - next display frame is always at the queue head
  int32_t           m_queue_depth;    // we will try to keep the queue depth at m_max_ref_frames
  int32_t           m_max_ref_frames;

  bool              m_convert_bytestream;
  bool              m_convert_3byteTo4byteNALSize;

  DllAvUtil         *m_dllAvUtil;
  DllAvFormat       *m_dllAvFormat;
  DllLibVTBDecoder  *m_dll;
};

#endif
