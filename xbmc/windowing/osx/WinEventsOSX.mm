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

#define BOOL XBMC_BOOL
#include "windowing/osx/WinEventsOSX.h"
#include "input/XBMC_vkeys.h"
#include "Application.h"
#include "windowing/WindowingFactory.h"
#include "threads/CriticalSection.h"
#include "guilib/GUIWindowManager.h"
#include "utils/log.h"
#include "input/MouseStat.h"
#undef BOOL

#include <ApplicationServices/ApplicationServices.h>

// place holder for future native osx event handler
CGEventRef MouseEventHandler(CGEventTapProxy proxy, CGEventType type,
                  CGEventRef event, void *refcon)
{
  CWinEventsOSX *winEvents = (CWinEventsOSX *)refcon;
  
  // The incoming mouse position.
  CGPoint location = CGEventGetLocation(event);
  XBMC_Event newEvent;
  switch (type)
  {
    case kCGEventLeftMouseUp:
      newEvent.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.button = XBMC_BUTTON_LEFT;
      newEvent.button.state = XBMC_RELEASED;
      newEvent.button.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);
      break;
    case kCGEventLeftMouseDown:
      newEvent.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.button = XBMC_BUTTON_LEFT;
      newEvent.button.state = XBMC_PRESSED;
      newEvent.button.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);      
      break;
    case kCGEventRightMouseUp:
      newEvent.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.button = XBMC_BUTTON_RIGHT;
      newEvent.button.state = XBMC_RELEASED;
      newEvent.button.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);
      break;
    case kCGEventRightMouseDown:
      newEvent.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.button = XBMC_BUTTON_RIGHT;
      newEvent.button.state = XBMC_PRESSED;
      newEvent.button.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);      
      break;
    case kCGEventOtherMouseUp:
      newEvent.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.button = XBMC_BUTTON_MIDDLE;
      newEvent.button.state = XBMC_RELEASED;
      newEvent.button.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);
      break;
    case kCGEventOtherMouseDown:
      newEvent.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.button = XBMC_BUTTON_MIDDLE;
      newEvent.button.state = XBMC_PRESSED;
      newEvent.button.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.which = 0;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      winEvents->MessagePush(&newEvent);      
      break;
    case kCGEventMouseMoved:
    case kCGEventLeftMouseDragged:
    case kCGEventRightMouseDragged:
    case kCGEventOtherMouseDragged:
      newEvent.type = XBMC_MOUSEMOTION;
      newEvent.motion.type = XBMC_MOUSEMOTION;
      newEvent.motion.xrel = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
      newEvent.motion.yrel = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
      newEvent.motion.state = 0;
      newEvent.motion.which = 0;
      newEvent.motion.x = location.x;
      newEvent.motion.y = location.y;
      winEvents->MessagePush(&newEvent);
      break;
    case kCGEventScrollWheel:
      newEvent.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.state = XBMC_PRESSED;
      newEvent.button.x = location.x;
      newEvent.button.y = location.y;
      newEvent.button.which = 0;
      newEvent.button.type = XBMC_MOUSEBUTTONDOWN;
      newEvent.button.button = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1) > 0 ? XBMC_BUTTON_WHEELUP : XBMC_BUTTON_WHEELDOWN;
      winEvents->MessagePush(&newEvent);
      newEvent.type = XBMC_MOUSEBUTTONUP;
      newEvent.button.state = XBMC_RELEASED;
      winEvents->MessagePush(&newEvent);
      break;
    default:
      break;
  }
  // We must return the event for it to be useful.
  return event;
}

CWinEventsOSX::CWinEventsOSX()
{
  CFMachPortRef      eventTap;
  CGEventMask        eventMask;
  CFRunLoopSourceRef runLoopSource;
  
  // Create an event tap. We are interested in mouse events.
  eventMask = CGEventMaskBit(kCGEventLeftMouseDown) |
              CGEventMaskBit(kCGEventMouseMoved) |
              CGEventMaskBit(kCGEventLeftMouseUp) |
              CGEventMaskBit(kCGEventRightMouseUp) |
              CGEventMaskBit(kCGEventRightMouseDown) |
              CGEventMaskBit(kCGEventOtherMouseDown) |
              CGEventMaskBit(kCGEventOtherMouseUp) |
              CGEventMaskBit(kCGEventOtherMouseDragged) |
              CGEventMaskBit(kCGEventRightMouseDragged) |
              CGEventMaskBit(kCGEventLeftMouseDragged) |
              CGEventMaskBit(kCGEventScrollWheel);
  eventTap = CGEventTapCreate(
                              kCGSessionEventTap, kCGHeadInsertEventTap,
                              0, eventMask, MouseEventHandler, this);
  if (!eventTap) 
  {
    CLog::Log(LOGERROR, "failed to create event tap\n");
  }
  
  // Create a run loop source.
  runLoopSource = CFMachPortCreateRunLoopSource(
                                                kCFAllocatorDefault, eventTap, 0);
  
  // Add to the current run loop.
  CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
                     kCFRunLoopCommonModes);
  
  // Enable the event tap.
  CGEventTapEnable(eventTap, true);
  
  // Set it all running.
  //CFRunLoopRun();  
}

CWinEventsOSX::~CWinEventsOSX()
{
}

static CCriticalSection g_inputCond;

static std::list<XBMC_Event> events;

void CWinEventsOSX::MessagePush(XBMC_Event *newEvent)
{
  CSingleLock lock(g_inputCond);
  
  events.push_back(*newEvent);
}

bool CWinEventsOSX::MessagePump()
{
  bool ret = false;
  ret = CWinEventsSDL::MessagePump();
  
  // Do not always loop, only pump the initial queued count events. else if ui keep pushing
  // events the loop won't finish then it will block xbmc main message loop.
  for (size_t pumpEventCount = GetQueueSize(); pumpEventCount > 0; --pumpEventCount)
  {
    // Pop up only one event per time since in App::OnEvent it may init modal dialog which init
    // deeper message loop and call the deeper MessagePump from there.
    XBMC_Event pumpEvent;
    {
      CSingleLock lock(g_inputCond);
      if (events.size() == 0)
        return ret;
      pumpEvent = events.front();
      events.pop_front();
    }  
    
    ret |= g_application.OnEvent(pumpEvent);
  }
  return ret;
}

size_t CWinEventsOSX::GetQueueSize()
{
  CSingleLock lock(g_inputCond);
  return events.size();
}

