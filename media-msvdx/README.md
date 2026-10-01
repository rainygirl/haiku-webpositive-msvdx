# msvdx_h264: hardware H.264 for the Haiku Media Kit on the GMA500

A Media Kit decoder add-on that decodes H.264 on the video decoder of the
Intel GMA500 (Poulsbo, SCH US15W) -- the Sony VAIO P's chipset. Anything that
plays video through the Media Kit uses it: WebPositive's HTML5 video (once
HaikuWebKit registers its media engine, see `../webkit/haikuwebkit-1.9.19-media.patch`),
MediaPlayer, and so on.

## How it is chosen

The ffmpeg reader describes an H.264 track as `B_MISC_FORMAT_FAMILY`, `'ffmp'`,
`AV_CODEC_ID_H264`; this add-on registers the same description. The Media Kit
looks for a decoder directory by directory, user and non-packaged directories
before the system's, so the add-on must sit in a non-packaged directory to win
over the ffmpeg plugin: `make install` puts it in
`~/config/non-packaged/add-ons/x86/media/plugins/`, and the package's
post-install script copies it to `/boot/system/non-packaged/add-ons/...`.

The Media Kit does not try another decoder when the one it picked fails, so
this one falls back by itself: with no firmware, no GMA500, the hardware held
by another application, or a stream it does not handle (interlaced, not 4:2:0,
larger than 1920x1088), it loads the system ffmpeg plugin and hands it the
track, the chunk that failed included. `MSVDX_MEDIA=0` forces that.

## Measured on the VAIO P (Atom Z520)

YouTube's 360p progressive MP4 (itag 18, 640x360 H.264 Main + AAC), decoded to
B_RGB32 through `BMediaTrack::ReadFrames()`:

| decoder | CPU per frame |
|---|---|
| ffmpeg plugin (software) | 17.1 ms |
| msvdx_h264 | 5.8 ms |

## Layout

`hw/`, `chromium/`, `shim/` and `rtv_msvdx.*` are the decode engine shared with
R Television (`rtelevision/platforms/haiku/msvdx`) and R Chromium
(`chromium114_port/files/media/gpu/haiku/msvdx`): Chromium's H.264 parser and
decoder (BSD) driving psb_video's H.264 command builder (MIT) and a userland
MSVDX driver. Keep the copies in step. `MsvdxDecoder.cpp` is the Media Kit side.

Intel's `msvdx_fw.bin` comes from the `msvdx_firmware` package.
