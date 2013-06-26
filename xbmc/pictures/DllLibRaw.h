#pragma once

/*
 *      Copyright (C) 2005-2013 Team XBMC
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

#include <libraw/libraw.h>
#include "DynamicDll.h"

class DllLibRawInterface
{
public:
    virtual ~DllLibRawInterface() {}
    virtual libraw_data_t* libraw_init(unsigned int flags)=0;
};

class DllLibRaw : public DllDynamic, DllLibRawInterface
{
  DECLARE_DLL_WRAPPER(DllLibRaw, DLL_PATH_LIBRAW)
  DEFINE_METHOD1(libraw_data_t*, libraw_init, (unsigned int p1));
  DEFINE_METHOD2(int, libraw_open_file, (libraw_data_t* p1, const char* p2));
  DEFINE_METHOD3(int, libraw_open_buffer, (libraw_data_t* p1, void * p2, size_t p3));
  DEFINE_METHOD1(void, libraw_close,(libraw_data_t* p1));
  DEFINE_METHOD1(int, libraw_raw2image, (libraw_data_t* p1));
  DEFINE_METHOD1(int, libraw_unpack, (libraw_data_t* p1));
  DEFINE_METHOD1(int, libraw_dcraw_process, (libraw_data_t* p1));
  DEFINE_METHOD1(int, libraw_unpack_thumb, (libraw_data_t* p1));
  DEFINE_METHOD2(libraw_processed_image_t*, libraw_dcraw_make_mem_image, (libraw_data_t* p1,int * p2));
  DEFINE_METHOD1(void, libraw_dcraw_clear_mem, (libraw_processed_image_t* p1));

  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD(libraw_init)
    RESOLVE_METHOD(libraw_open_file)
    RESOLVE_METHOD(libraw_open_buffer)
    RESOLVE_METHOD(libraw_close)
    RESOLVE_METHOD(libraw_raw2image)
    RESOLVE_METHOD(libraw_unpack)
    RESOLVE_METHOD(libraw_dcraw_process)
    RESOLVE_METHOD(libraw_unpack_thumb)
    RESOLVE_METHOD(libraw_dcraw_make_mem_image)
    RESOLVE_METHOD(libraw_dcraw_clear_mem)
  END_METHOD_RESOLVE()
};

