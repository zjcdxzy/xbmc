AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = -DTARGET_POSIX -DUSE_LZO_PACKING
AM_CFLAGS += @EXTRA_DEFINES@
AM_CXXFLAGS = $(AM_CFLAGS)

AM_CPPFLAGS =\
  -I. \
  -I./decoder \
  -I@KODI_SRC_DIR@/lib \
  -I@KODI_SRC_DIR@/lib/libsquish \
  -I@KODI_SRC_DIR@/xbmc \
  -I@KODI_SRC_DIR@/xbmc/linux \
  @INCLUDES@

#AM_LDFLAGS = -Wl,--whole-archive -L@KODI_SRC_DIR@/lib/libsquish -lsquish-native \
#             -L/opt/xbmc-deps/x86_64-linux-gnu-native/lib -lgif -lpng -ljpeg -llzo2 -Wl,--no-whole-archive
#AM_LDFLAGS = -static -Wl,--whole-archive -ljpeg -L@KODI_SRC_DIR@/lib/libsquish -lsquish-native -lpng -llzo2 -lz -lm -L/opt/xbmc-deps/x86_64-linux-gnu-native/lib -lgif -Wl,--no-whole-archive

#AM_LDFLAGS = -static -ljpeg -L@KODI_SRC_DIR@/lib/libsquish -lsquish-native @LIBS@ -lz -lm -L/opt/xbmc-deps/x86_64-linux-gnu-native/lib -lgif 
#AM_LDFLAGS = -static -Wl,--whole-archive -L@KODI_SRC_DIR@/lib/libsquish -lsquish-native -L/opt/xbmc-deps/x86_64-linux-gnu-native/lib -lgif @LIBS@ -Wl,--no-whole-archive
AM_LDFLAGS = -static -Wl,--whole-archive -L@KODI_SRC_DIR@/lib/libsquish -lsquish-native @LIBS@ -Wl,--no-whole-archive


bin_PROGRAMS = TexturePacker
TexturePacker_SOURCES =  md5.cpp \
  XBTFWriter.cpp \
  TexturePacker.cpp \
  DecoderManager.cpp \
  decoder/PNGDecoder.cpp \
  decoder/JPGDecoder.cpp \
  decoder/GifHelper.cpp \
  decoder/GIFDecoder.cpp \
  XBTF.cpp

XBTF.cpp:
	@cp @KODI_SRC_DIR@/xbmc/guilib/XBTF.cpp .
	@cp @KODI_SRC_DIR@/xbmc/guilib/XBTF.h .
