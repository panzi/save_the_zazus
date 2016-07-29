CC=gcc
BINEXT=
TARGET=$(shell uname|tr '[A-Z]' '[a-z]')$(shell getconf LONG_BIT)
BUILDDIR=build
BUILDDIR_BIN=$(BUILDDIR)/$(TARGET)
BUILDDIR_SRC=$(BUILDDIR)/src
INCLUDE=-I$(BUILDDIR_SRC)
COMMON_CFLAGS=-Wall -Werror -Wextra -std=gnu11 $(INCLUDE)
ifeq ($(DEBUG),ON)
	COMMON_CFLAGS+=-g -DDEBUG
else
	COMMON_CFLAGS+=-O2
endif
POSIX_CFLAGS=$(COMMON_CFLAGS) -pedantic -fdiagnostics-color
CFLAGS=$(COMMON_CFLAGS)
ARCH_FLAGS=
EXT_DEP=
ARCHIVE_LIBS=-larchive
STATIC_ARCHIVE_LIBS=-larchive -lxml2 -lbz2 -lz -liconv -llzma -lws2_32

OBJ=$(BUILDDIR_BIN)/save_the_zazus.o \
    $(BUILDDIR_BIN)/package_img_atlas0_png.o \
    $(BUILDDIR_BIN)/package_img_atlas1_png.o \
    $(BUILDDIR_BIN)/package_img_atlas2_png.o

ifeq ($(TARGET),win32)
	CC=i686-w64-mingw32-gcc
	ARCH_FLAGS=-m32 -DLIBARCHIVE_STATIC
	BINEXT=.exe
	LIB=-static $(STATIC_ARCHIVE_LIBS)
else
ifeq ($(TARGET),win64)
	CC=x86_64-w64-mingw32-gcc
	ARCH_FLAGS=-m64 -DLIBARCHIVE_STATIC
	BINEXT=.exe
	LIB=-static $(STATIC_ARCHIVE_LIBS)
else
ifeq ($(TARGET),linux32)
	CFLAGS=$(POSIX_CFLAGS)
	ARCH_FLAGS=-m32
	LIB=-larchive
else
ifeq ($(TARGET),linux64)
	CFLAGS=$(POSIX_CFLAGS)
	ARCH_FLAGS=-m64
	LIB=$(ARCHIVE_LIBS)
else
ifeq ($(TARGET),darwin32)
	CC=clang
	CFLAGS=$(POSIX_CFLAGS)
	ARCH_FLAGS=-m32
	EXT_DEP=macpkg
	LIB=$(ARCHIVE_LIBS)
else
ifeq ($(TARGET),darwin64)
	CC=clang
	CFLAGS=$(POSIX_CFLAGS)
	ARCH_FLAGS=-m64
	EXT_DEP=macpkg
	LIB=$(ARCHIVE_LIBS)
endif
endif
endif
endif
endif
endif

.PHONY: all clean save_the_zazus setup pkg

# keep intermediary files (e.g. package_img_atlas0_png.c) to
# do less redundant work (when cross compiling):
.SECONDARY:

all: save_the_zazus

setup:
	mkdir -p $(BUILDDIR_BIN) $(BUILDDIR_SRC)
#	cd libarchive; build/autogen.sh

save_the_zazus: $(BUILDDIR_BIN)/save_the_zazus$(BINEXT)

$(BUILDDIR_BIN)/save_the_zazus$(BINEXT): $(OBJ) # libarchive/.libs/libarchive.a
	$(CC) $(ARCH_FLAGS) $(OBJ) $(LIB) -o $@

$(BUILDDIR_SRC)/package_img_atlas0_png.c: package/img/atlas0.png
	xxd -i $< > $@

$(BUILDDIR_SRC)/package_img_atlas1_png.c: package/img/atlas1.png
	xxd -i $< > $@

$(BUILDDIR_SRC)/package_img_atlas2_png.c: package/img/atlas2.png
	xxd -i $< > $@

$(BUILDDIR_BIN)/%_png.o: $(BUILDDIR_SRC)/%_png.c
	$(CC) $(ARCH_FLAGS) $(CFLAGS) -c $< -o $@

$(BUILDDIR_BIN)/%.o: src/%.c
	$(CC) $(ARCH_FLAGS) $(CFLAGS) -c $< -o $@

#libarchive/.libs/libarchive.a:
#	cd libarchive; ./configure --enable-static --without-xml2 \
#		--without-expat --without-openssl --without-nettle \
#		--without-lzo2 --without-lzma --without-lz4 \
#		--without-lzmadec --without-bz2lib

clean:
	rm -f \
		$(OBJ) \
		$(BUILDDIR_BIN)/save_the_zazus$(BINEXT) \
		$(BUILDDIR_SRC)/package_img_atlas0_png.c \
		$(BUILDDIR_SRC)/package_img_atlas1_png.c \
		$(BUILDDIR_SRC)/package_img_atlas2_png.c
