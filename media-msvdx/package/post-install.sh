#!/bin/sh
# msvdx_media_x86: put the decoder where the Media Kit looks before the
# system's own plugins. A packaged add-on would land in the same directory as
# the ffmpeg plugin, and within one directory the order is not defined.
dir="$(finddir B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY)/x86/media/plugins"
mkdir -p "$dir"
cp -f "$(finddir B_SYSTEM_DATA_DIRECTORY)/msvdx_media/msvdx_h264" "$dir/msvdx_h264"
