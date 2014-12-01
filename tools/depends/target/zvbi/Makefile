include ../../Makefile.include
DEPS= ../../Makefile.include Makefile

# lib name, version
PACKAGENAME=zvbi
LIBNAME=lib$(PACKAGENAME)
VERSION=0.2.35
SOURCE=$(PACKAGENAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.bz2

# configuration settings
CONFIGURE=./configure --prefix=$(PREFIX) --without-x --disable-dvb --disable-bktr \
	--disable-nls --disable-proxy \
	--without-doxygen

LIBDYLIB=$(PLATFORM)/src/.libs/$(LIBNAME).a

CLEAN_FILES=$(ARCHIVE) $(PLATFORM)

all: $(LIBDYLIB) .installed-$(PLATFORM)

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(PLATFORM): $(TARBALLS_LOCATION)/$(ARCHIVE) $(DEPS)
	rm -rf $(PLATFORM)/*; mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	cd $(PLATFORM); patch -p1 < ../01-zvbi-ssize_max.patch
	cd $(PLATFORM); patch -p1 < ../02-zvbi-ioctl.patch
ifeq ($(OS),osx)
	cd $(PLATFORM); patch -p1 < ../03-zvbi-fix-clang-support.patch
endif
	cd $(PLATFORM); autoreconf -vif
	cd $(PLATFORM); $(CONFIGURE)

$(LIBDYLIB): $(PLATFORM)
	$(MAKE) -C $(PLATFORM)

.installed-$(PLATFORM): $(LIBDYLIB)
ifeq ($(OS),android)
	rm -f $(PLATFORM)/src/.libs/libzvbi.la $(PLATFORM)/src/.libs/libzvbi.so $(PLATFORM)/src/.libs/libzvbi.so.0
	mv -f $(PLATFORM)/src/.libs/libzvbi.so.0.0.0 $(PLATFORM)/src/.libs/libzvbi.so
	$(RPL) -e "libzvbi.so.0" "libzvbi.so\x00\x00" $(PLATFORM)/src/.libs/libzvbi.so
	-$(READELF) --dynamic $(PLATFORM)/src/.libs/libzvbi.so | grep ibrary
endif
	$(MAKE) -C $(PLATFORM) install
	touch $@
clean:
	$(MAKE) -C $(PLATFORM) clean
	rm -r .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)
