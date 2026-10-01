# WebPositive HTML5 video, hardware-decoded on the GMA500

For Korean, see [`README.ko.md`](README.ko.md).

Makes HTML5 video, YouTube included, play in WebPositive on 32-bit Haiku
(x86_gcc2 hybrid), and decodes the H.264 in hardware on the Intel GMA500
(Poulsbo) video decoder of the Sony VAIO P.

- **HaikuWebKit 1.9.19 fixes** (`webkit/`): HaikuWebKit never registered its
  own media engine, so no `<video>` or `<audio>` played and YouTube said the
  browser could not play the video. The player's clock also trusted decoder
  timestamps that run at a twentieth of real time on MP4. Both are fixed.
- **`msvdx_h264`, a Media Kit decoder add-on** (`media-msvdx/`): decodes H.264
  on the GMA500, falling back to the ffmpeg plugin for anything it cannot
  handle or on any other machine.

On the VAIO P (Atom Z520), YouTube's 360p stream costs 5.8 ms of CPU per
frame instead of 17.1 ms with ffmpeg. Without MSE, YouTube serves WebPositive
its progressive 360p MP4, so 360p is the resolution you get.

## Install with pkgman

```sh
pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2-webpositive
pkgman refresh
pkgman install webpositive_hwvideo
```

Then restart WebPositive. `webpositive_hwvideo` pulls in:

| Package | What it is |
|---|---|
| `haikuwebkit_x86` 1.9.19-6 | HaikuWebKit with the media engine and clock fixes (and the leak fixes from haiku-rwebpositive-arm64), replacing the official 1.9.19 |
| `msvdx_media_x86` | the decoder add-on |
| `msvdx_firmware` | Intel's microcode for the GMA500 video decoder |

To go back: `pkgman uninstall webpositive_hwvideo msvdx_media_x86`, and
reinstall the official WebKit with `pkgman install haikuwebkit_x86` after
removing this repository (`pkgman drop-repo`).

`MSVDX_MEDIA=0` in WebPositive's environment forces software decoding;
`MSVDX_MEDIA_DEBUG=1` traces the decoder to stderr.

## Requirements

- Haiku R1 beta6-era x86_gcc2 hybrid with HaikuWebKit 1.9.19 (the version
  WebPositive there is built against).
- Hardware decoding: Intel GMA500 / SCH US15W. Tested on the Sony VAIO P
  (VGN-P70H). Other machines play the same video in software.

## Building

- WebKit: `webkit/build-haikuwebkit-x86.sh`, a cross build on Linux; it uses
  [haiku-rwebpositive-arm64](https://github.com/rainygirl/haiku-rwebpositive-arm64)'s
  x86 build with this repository's patch on top. Set `RWP` to its checkout.
- Add-on: `cd media-msvdx && make` on the Haiku machine (needs
  `haiku_x86_devel` and `ffmpeg6_x86_devel`).

Development notes, root causes and measurements: [`AGENTS.md`](AGENTS.md).

## Licence

MIT for the new code; the WebKit patch, the Chromium and psb_video sources and
Intel's firmware keep their own licences. See [`LICENSE`](LICENSE).
