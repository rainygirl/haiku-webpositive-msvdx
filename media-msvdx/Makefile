# msvdx_h264: Media Kit decoder add-on, H.264 on the GMA500 (x86 secondary
# architecture of an x86_gcc2 hybrid). Build on the machine:
#
#   make            -> build/msvdx_h264
#   make install    -> ~/config/non-packaged/add-ons/x86/media/plugins/
#
# Needs haiku_x86_devel (private media headers) and ffmpeg6_x86_devel
# (libswscale), both on the VAIO.
CC  = gcc-x86
CXX = g++-x86
BUILD ?= build
HDR = /boot/system/develop/headers
FFMPEG_INC ?= $(firstword $(wildcard $(HDR)/x86/ffmpeg6 $(HDR)/x86/ffmpeg $(HDR)/x86))

HW_C  := hw/msvdx.c hw/msvdx_decode.c hw/psb/psb_H264.c hw/psb/psb_cmdbuf.c \
         hw/psb/psb_cmdbuf_host.c
CR_CC := chromium/media/video/h264_parser.cc chromium/media/video/h264_poc.cc \
         chromium/media/video/h264_bit_reader.cc \
         chromium/media/video/h264_level_limits.cc \
         chromium/media/gpu/h264_dpb.cc chromium/media/gpu/codec_picture.cc \
         chromium/media/gpu/h264_decoder.cc shim/rtv_shim.cc rtv_msvdx.cc

OBJ := $(patsubst %.c,$(BUILD)/%.o,$(HW_C)) \
       $(patsubst %.cc,$(BUILD)/%.o,$(CR_CC)) $(BUILD)/MsvdxDecoder.o

HW_FLAGS := -std=gnu99 -O2 -fPIC -w -Ihw -Ihw/hwdefs -Ihw/psb \
            -I$(HDR)/private/drivers
CR_FLAGS := -std=gnu++17 -O2 -fPIC -fno-exceptions -Ishim -Ichromium -I. -Ihw
MK_FLAGS := -std=gnu++17 -O2 -fPIC -I. -I$(HDR)/private/media -I$(FFMPEG_INC)

$(BUILD)/msvdx_h264: $(OBJ)
	$(CXX) -shared -o $@ $(OBJ) -lmedia -lbe -lswscale -lavutil

$(BUILD)/hw/%.o: hw/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HW_FLAGS) -c $< -o $@

$(BUILD)/MsvdxDecoder.o: MsvdxDecoder.cpp rtv_msvdx.h
	@mkdir -p $(dir $@)
	$(CXX) $(MK_FLAGS) -c $< -o $@

$(BUILD)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CR_FLAGS) -c $< -o $@

PLUGINS := $(HOME)/config/non-packaged/add-ons/x86/media/plugins

install: $(BUILD)/msvdx_h264
	mkdir -p "$(PLUGINS)"
	cp $(BUILD)/msvdx_h264 "$(PLUGINS)/msvdx_h264"

clean:
	rm -rf $(BUILD)

.PHONY: install clean
