#pragma once

/*
 *      Copyright (C) 2011-2013 Team XBMC
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

#include "windowing/WinEventsSDL.h"

class CWinEventsOSX : public CWinEventsSDL
{
public:
  CWinEventsOSX();
  ~CWinEventsOSX();

  void MessagePush(XBMC_Event *newEvent);
  bool MessagePump();
  virtual size_t  GetQueueSize();

  void *GetEventTap(){return mEventTap;}
  bool TapVolumeKeys(){return mTapVolumeKeys;}
  bool TapPowerKey(){return mTapPowerKey;}
  void SetEnabled(bool enable){mEnabled = enable;}
  bool IsEnabled(){return mEnabled;}

private:
  
  void *mRunLoopSource;
  void *mEventTap;
  bool mEnabled;
  bool mTapVolumeKeys;
  bool mTapPowerKey;
  
  void enableTap();
  void disableTap();

};
