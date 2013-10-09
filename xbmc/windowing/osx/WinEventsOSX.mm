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
#include "guilib/Key.h"
#include "ApplicationMessenger.h"
#include "utils/log.h"
#include "input/MouseStat.h"
#include "GUIUserMessages.h"
#include "osx/CocoaInterface.h"
#undef BOOL

#include <ApplicationServices/ApplicationServices.h>

bool ProcessOSXShortcuts(XBMC_Event& event)
{
  bool cmd = false;
  
  cmd   = !!(event.key.keysym.mod & (XBMCKMOD_LMETA | XBMCKMOD_RMETA));
  
  if (cmd && event.key.type == XBMC_KEYDOWN)
  {
    switch(event.key.keysym.sym)
    {
      case XBMCK_q:  // CMD-q to quit
        if (!g_application.m_bStop)
          CApplicationMessenger::Get().Quit();
        return true;
        
      case XBMCK_f: // CMD-f to toggle fullscreen
        g_application.OnAction(CAction(ACTION_TOGGLE_FULLSCREEN));
        return true;
        
      case XBMCK_s: // CMD-s to take a screenshot
        g_application.OnAction(CAction(ACTION_TAKE_SCREENSHOT));
        return true;
        
      case XBMCK_h: // CMD-h to hide (but we minimize for now)
      case XBMCK_m: // CMD-m to minimize
        CApplicationMessenger::Get().Minimize();
        return true;
        
      case XBMCK_v: // CMD-v to paste clipboard text
        if (g_Windowing.IsTextInputEnabled())
        {
          const char *szStr = Cocoa_Paste();
          if (szStr)
          {
            CGUIMessage msg(GUI_MSG_INPUT_TEXT, 0, 0);
            msg.SetLabel(szStr);
            g_windowManager.SendMessage(msg, g_windowManager.GetFocusedWindow());
          }
        }
        return true;
        
      default:
        return false;
    }
  }
  
  return false;
}

UniChar OsxKey2XbmcKey(UniChar character)
{
  switch(character)
  {
    case 0x1c:
      return XBMCK_LEFT;
    case 0x1d:
      return XBMCK_RIGHT;
    case 0x1e:
      return XBMCK_UP;
    case 0x1f:
      return XBMCK_DOWN;
    default:
      return character;
  }
}

XBMCMod OsxMod2XbmcMod(CGEventFlags appleModifier)
{
  unsigned int xbmcModifier = XBMCKMOD_NONE;
  // shift left
  if (appleModifier & kCGEventFlagMaskAlphaShift)
  xbmcModifier |= XBMCKMOD_LSHIFT;
  // shift right
  if (appleModifier & kCGEventFlagMaskShift)
  xbmcModifier |= XBMCKMOD_RSHIFT;
  // left ctrl
  if (appleModifier & kCGEventFlagMaskControl)
  xbmcModifier |= XBMCKMOD_LCTRL;
  // left alt/option
  if (appleModifier & kCGEventFlagMaskAlternate)
  xbmcModifier |= XBMCKMOD_LALT;
  // left command
  if (appleModifier & kCGEventFlagMaskCommand)
  xbmcModifier |= XBMCKMOD_LMETA;

  return (XBMCMod)xbmcModifier;
}

// place holder for future native osx event handler
CGEventRef InputEventHandler(CGEventTapProxy proxy, CGEventType type,
                  CGEventRef event, void *refcon)
{
  bool passEvent = false;
  CWinEventsOSX *winEvents = (CWinEventsOSX *)refcon;
  
  if (type == kCGEventTapDisabledByTimeout)
  {
    if (winEvents->GetEventTap())
      CGEventTapEnable((CFMachPortRef)winEvents->GetEventTap(), true);
    return NULL;
  }
  
  // if we are not focused - pass the event along...
  if (!g_application.m_AppFocused)
    return event;
  
  // The incoming mouse position.
  CGPoint location = CGEventGetLocation(event);
  UniChar unicodeString[10];
  UniCharCount actualStringLength;
  CGKeyCode keycode;

  XBMC_Event newEvent;
  switch (type)
  {
    // handle mouse events and transform them into the xbmc event world
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
      
    // handle keyboard events and transform them into the xbmc event world
    case kCGEventKeyUp:
      keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
      CGEventKeyboardGetUnicodeString(event, sizeof(unicodeString) / sizeof(*unicodeString), &actualStringLength, unicodeString);
      unicodeString[0] = OsxKey2XbmcKey(unicodeString[0]);

      newEvent.type = XBMC_KEYUP;
      newEvent.key.keysym.scancode = keycode;
      newEvent.key.keysym.sym = (XBMCKey) unicodeString[0];
      newEvent.key.keysym.unicode = unicodeString[0];
      if (actualStringLength > 1)
        newEvent.key.keysym.unicode |= (unicodeString[1] << 8);
      newEvent.key.state = XBMC_RELEASED;
      newEvent.key.type = XBMC_KEYUP;
      newEvent.key.which = 0;
      newEvent.key.keysym.mod = OsxMod2XbmcMod(CGEventGetFlags(event));

      // always allow task switching - so pass all events with command pressed up
      if (CGEventGetFlags(event) & kCGEventFlagMaskCommand)
        passEvent = true;

      winEvents->MessagePush(&newEvent);
      break;
    case kCGEventKeyDown:     
      keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
      CGEventKeyboardGetUnicodeString(event, sizeof(unicodeString) / sizeof(*unicodeString), &actualStringLength, unicodeString);
      unicodeString[0] = OsxKey2XbmcKey(unicodeString[0]);

      newEvent.type = XBMC_KEYDOWN;
      newEvent.key.keysym.scancode = keycode;
      newEvent.key.keysym.sym = (XBMCKey) unicodeString[0];
      newEvent.key.keysym.unicode = unicodeString[0];
      if (actualStringLength > 1)
        newEvent.key.keysym.unicode |= (unicodeString[1] << 8);
      newEvent.key.state = XBMC_PRESSED ;
      newEvent.key.type = XBMC_KEYDOWN;
      newEvent.key.which = 0;
      newEvent.key.keysym.mod = OsxMod2XbmcMod(CGEventGetFlags(event));
      
      if (!ProcessOSXShortcuts(newEvent))
      {
        // always allow task switching - so pass all events with command pressed up
        if (CGEventGetFlags(event) & kCGEventFlagMaskCommand)
          passEvent = true;
      
        winEvents->MessagePush(&newEvent);
      }
      
      break;
    default:
      return event;
  }
  // We must return the event for it to be useful.
  if (passEvent)
    return event;
  else
    return NULL;
}

CWinEventsOSX::CWinEventsOSX()
{
  CGEventMask        eventMask;
  
  // Create an event tap. We are interested in mouse and keyboard events.
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
  
  eventMask |= CGEventMaskBit(kCGEventKeyDown) |
               CGEventMaskBit(kCGEventKeyUp);
  
  mEventTap = CGEventTapCreate(
                              kCGSessionEventTap, kCGTailAppendEventTap,
                              kCGEventTapOptionDefault, eventMask, InputEventHandler, this);
  if (!mEventTap) 
  {
    CLog::Log(LOGERROR, "failed to create event tap\n");
  }
  
  // Create a run loop source.
  mRunLoopSource = CFMachPortCreateRunLoopSource(
                                                kCFAllocatorDefault, (CFMachPortRef)mEventTap, 0); 
  // Add to the current run loop.
  CFRunLoopAddSource(CFRunLoopGetCurrent(), (CFRunLoopSourceRef)mRunLoopSource,
                     kCFRunLoopCommonModes);
  CFRelease(mRunLoopSource);
  
  // Enable the event tap.
  CGEventTapEnable((CFMachPortRef)mEventTap, true);
  CFRelease((CFMachPortRef)mEventTap);
}

CWinEventsOSX::~CWinEventsOSX()
{
  mEventTap = NULL;
  CFRunLoopRemoveSource(CFRunLoopGetCurrent(), (CFRunLoopSourceRef)mRunLoopSource, kCFRunLoopCommonModes);
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

