include ../../Makefile.include
DEPS= ../../Makefile.include 

# lib name, version
LIBNAME=LibRaw
VERSION=0.15.2
SOURCE=$(LIBNAME)-$(VERSION)
ARCHIVE=$(SOURCE).tar.gz

# configuration settings
CONFIGURE=./configure --prefix=$(PREFIX)


LIBDYLIB=$(PLATFORM)/lib/.libs/libraw.so.9

all: .installed-$(PLATFORM)

$(TARBALLS_LOCATION)/$(ARCHIVE):
	cd $(TARBALLS_LOCATION); $(RETRIEVE_TOOL) $(RETRIEVE_TOOL_FLAGS) $(BASE_URL)/$(ARCHIVE)

$(PLATFORM): $(TARBALLS_LOCATION)/$(ARCHIVE) $(DEPS)
	rm -rf $(PLATFORM); mkdir -p $(PLATFORM)
	cd $(PLATFORM); $(ARCHIVE_TOOL) $(ARCHIVE_TOOL_FLAGS) $(TARBALLS_LOCATION)/$(ARCHIVE)
	cd $(PLATFORM); patch -p0 < ../01-fixversion.patch
	cd $(PLATFORM); $(AUTORECONF) -vif
	cd $(PLATFORM); $(CONFIGURE)

$(LIBDYLIB): $(PLATFORM)
	$(MAKE) -C $(PLATFORM)

.installed-$(PLATFORM): $(LIBDYLIB)
	$(MAKE) -C $(PLATFORM) install
ifeq ($(OS),android)
	rm -f $(PREFIX)/lib/libraw.la $(PREFIX)/lib/libraw.so $(PREFIX)/lib/libraw.so.9
	mv -f $(PREFIX)/lib/libraw.so.9 $(PREFIX)/lib/libraw.so
	$(RPL) -e "libraw.so.9" "libraw.so\x00\x00" $(PREFIX)/lib/libraw.so
	-$(READELF) --dynamic $(PREFIX)/lib/libraw.so | grep ibrary
endif

	touch $@

clean:
	rm -rf $(PLATFORM) .installed-$(PLATFORM)

distclean::
	rm -rf $(PLATFORM) .installed-$(PLATFORM)

