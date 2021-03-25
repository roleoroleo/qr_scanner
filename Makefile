OBJECTS = qr_scanner.o
FFMPEG = ffmpeg-4.0.4
FFMPEG_DIR = ./$(FFMPEG)
INC_FF = -I$(FFMPEG_DIR)
LIB_FF = $(FFMPEG_DIR)/libavcodec/libavcodec.a $(FFMPEG_DIR)/libavutil/libavutil.a -lpthread -lm
QUIRC = quirc
QUIRC_DIR = ./$(QUIRC)
INC_QUIRC = -I$(QUIRC_DIR)/lib
LIB_QUIRC = $(QUIRC_DIR)/libquirc.a

all: qr_scanner

qr_scanner.o: qr_scanner.c $(HEADERS)
	@$(build_ffmpeg)
	@$(build_quirc)
	$(CC) -c $< $(INC_FF) $(INC_QUIRC) -fPIC -O2 -o $@

qr_scanner: $(OBJECTS)
	$(CC) $(OBJECTS) $(LIB_FF) $(LIB_QUIRC) -fPIC -O2 -o $@
	$(STRIP) $@

.PHONY: clean

clean:
	rm -f qr_scanner
	rm -f $(OBJECTS)

distclean: clean
	rm -rf SDK

define build_ffmpeg
    # get archive
    if [ ! -f SDK/ffmpeg.tar.bz2 ]; then \
        mkdir -p SDK; \
        wget -O ./SDK/ffmpeg.tar.bz2.tmp "http://ffmpeg.org/releases/$(FFMPEG).tar.bz2"; \
        mv ./SDK/ffmpeg.tar.bz2.tmp ./SDK/ffmpeg.tar.bz2; \
    fi

    # untar
    if [ ! -f $(FFMPEG)/README.md ]; then \
         tar jxvf ./SDK/ffmpeg.tar.bz2; \
    fi

   # build
    if [ ! -f $(FFMPEG)/libavcodec/libavcodec.a ] || [ ! -f $(FFMPEG)/libavutil/libavutil.a ]; then \
        cd $(FFMPEG); \
        if [ -z $(CROSSPREFIX) ]; then \
            ./configure --target-os=linux --disable-x86asm --disable-ffplay --disable-ffprobe --disable-doc  --disable-decoders --enable-decoder=h264 --disable-encoders --disable-demuxers --enable-demuxer=h264 --disable-muxers --disable-protocols --disable-parsers --enable-parser=h264 --disable-filters --disable-bsfs --disable-indevs --disable-outdevs; \
        else \
            ./configure --enable-cross-compile --cross-prefix=$(CROSSPREFIX) --arch=armel --target-os=linux --prefix=$(CROSSPATH) --disable-ffplay --disable-ffprobe --disable-doc  --disable-decoders --enable-decoder=h264 --disable-encoders --disable-demuxers --enable-demuxer=h264 --disable-muxers --disable-protocols --disable-parsers --enable-parser=h264 --disable-filters --disable-bsfs --disable-indevs --disable-outdevs; \
        fi; \
        make; \
        cd ..;\
    fi
endef

define build_quirc
    # get repo
    if [ ! -d quirc ]; then \
        git clone https://github.com/dlbeer/quirc; \
    fi

   # build
    if [ ! -f $(QUIRC_DIR)/libquirc.a ]; then \
        cd $(QUIRC_DIR); \
        make libquirc.a; \
        cd ..;\
    fi
endef
