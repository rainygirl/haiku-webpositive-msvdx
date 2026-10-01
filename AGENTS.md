# WebPositive HTML5 video on the GMA500 -- Development Notes

What each piece does and why, what was measured, and the traps. Install
instructions are in [`README.md`](README.md).

Scope: Haiku x86_gcc2 hybrid, HaikuWebKit 1.9.19, and the Intel GMA500
(Poulsbo) of the Sony VAIO P for the hardware decoding. Other x86 machines get
working HTML5 video from the WebKit fixes alone, decoded in software.

## HTML5 video never played: HaikuWebKit does not register its media engine (2026-10-01)

WebPositive showed YouTube's "your browser can't play this video" on the VAIO
P, and a test page found why: `canPlayType()` answered "" for every type,
`video/mp4` included, and no media thread was ever created. The Media Kit was
not the problem -- a plain x86 program calling `get_next_file_format()` lists
`video/mp4` and `video/webm`, and `BMediaFile(BUrl)` opens and decodes YouTube's
own progressive stream.

`Source/WebCore/platform/graphics/MediaPlayer.cpp` defines
`PlatformMediaEngineClassName` as `MediaPlayerPrivate` under `PLATFORM(HAIKU)`
and never uses it: `buildMediaEnginesVector()` registers the Cocoa, GStreamer,
Media Foundation and HolePunch engines and nothing for Haiku. So
`installedMediaEngines()` is empty, `MediaPlayer::supportsType()` finds no
engine, and no HTML5 audio or video plays at all. Checked in the
`HaikuWebKit-1.9.19` and `HaikuWebKit-1.9.26` tags; 1.10.0 most likely has the
same gap. `webkit/haikuwebkit-1.9.19-media.patch` adds the registration
(`PlatformMediaEngineClassName::registerMediaEngine(addMediaEngine)`). It first
shipped in `haikuwebkit_x86` 1.9.19-4; -5 added the clock fix below and -6 the
`haikuwebkit_x86_media` provides (see "Packaging").

Why that is enough for YouTube: without Media Source Extensions (HaikuWebKit
builds with `ENABLE_MEDIA_SOURCE` off) YouTube falls back to a progressive MP4,
itag 18, 640x360 H.264 Main + AAC. Measured by deleting `MediaSource` from the
page in R Chromium: the `<video>` gets a `googlevideo.com/videoplayback?...
itag=18&mime=video/mp4` URL and loads it. The Media Kit's `http_streamer` reads
that over HTTPS sequentially (no range requests) and the ffmpeg reader demuxes
it.

### Hardware H.264 for the Media Kit: media-msvdx/

`media-msvdx/` is a Media Kit decoder add-on that decodes H.264 on the GMA500's
video decoder, with the engine R Chromium and R Television use. It registers
the same format description the ffmpeg reader gives H.264 tracks
(`B_MISC_FORMAT_FAMILY`, `'ffmp'`, `AV_CODEC_ID_H264`); the Media Kit picks the
decoder from the first add-on directory that has one, non-packaged before
system, so the add-on must live in a non-packaged directory -- inside one
directory the order is whatever the directory returns. The `msvdx_media_x86`
package therefore carries it under `data/` and a post-install script copies it
to `/boot/system/non-packaged/add-ons/x86/media/plugins`.

The Media Kit has no fallback: if the decoder it picked fails, the track does
not play. The add-on falls back by itself -- no firmware, no GMA500, hardware
held by another team, or a stream it does not handle -- by loading the system
ffmpeg plugin and handing it the track, the failed chunk included.

Measured on 640x360 H.264 through `BMediaTrack::ReadFrames()` into B_RGB32:
17.1 ms of CPU per frame with the ffmpeg plugin, 5.8 ms with the add-on.

### The player's clock came from a decoder that lies (fixed in haikuwebkit_x86 1.9.19-5)

With the engine registered, video played in slow motion and the picture froze
for seconds at a time. `MediaPlayerPrivateHaiku` decodes video from inside the
`BSoundPlayer` callback and took its clock from `m_audioTrack->CurrentTime()`;
a video frame was read whenever the video track's `CurrentTime()` was behind
it. On MP4 the ffmpeg plugin's AAC decoder advances `CurrentTime()` by about a
twentieth of real time, and the video track's times are not reliable either,
so the page's clock crawled and frames came out in bursts.

The patch counts instead of asking: `m_currentTime = m_timeBase +
audioFramesPlayed / audioRate`, a video frame is read while `m_timeBase +
videoFramesShown / videoRate` is behind that, and a seek resets the counters
under `m_mediaLock` with `m_timeBase` set to where the tracks actually landed.
Rates come from the decoded formats in `IdentifyTracks`. Measured on a local
30 s Big Buck Bunny MP4: 709 pictures in 30 s, real time, picture in step with
the sound. YouTube's watch page plays its ads and then the video itself with
the add-on decoding (1245 pictures to pts 30.4 s in the trace).

### Building it, and two traps

`webkit/build-haikuwebkit-x86.sh` runs haiku-rwebpositive-arm64's x86 build
(which carries the leak fixes in `haikuwebkit-1.9.19-x86.patch`) with this
repository's media patch applied on top and this repository's `.PackageInfo`.
The two patches together reproduce the tree the published package was built
from byte for byte (checked with `diff -r` against that tree).

The cross toolchain taken from the Chromium 114 build container keeps a
flattened copy of Haiku's headers in `i586-pc-haiku/include`, which the compiler
searches before any `-idirafter`. Its `posix/pthread.h` then shadows
`gnu/pthread.h`, and WTF fails on `pthread_getattr_np`. Move everything but
`include/c++` aside before configuring.

The default colima VM here has a 40 GB disk that another container keeps 96%
full; the WebKit tree filled it to 100% within minutes. Build in a separate
colima profile (`colima start wk86 --disk 80 ...`), and note that `colima
start` switches the default docker context to the new profile -- switch it
back (`docker context use colima`) or every other session's `docker exec`
loses its containers.

## Packaging: a revision cannot be required (2026-10-01)

`webpositive_hwvideo` is an empty package whose requirements pull in the rest.
It first required `haikuwebkit_x86 >= 1.9.19-5`, and a test package requiring
`>= 1.9.19-6` installed without complaint on a machine that had only -5. Every
haikuwebkit_x86 1.9.19 package, the official -2 included, lists
`haikuwebkit_x86 = 1.9.19` in its provides: a version with no revision, and the
resolver counts a provide without a revision as meeting any revision. So
`pkgman install webpositive_hwvideo` on a stock system would have kept the
official WebKit, without the media engine, and nothing would have played.

haikuwebkit_x86 1.9.19-6 is -5's files with one more provide,
`haikuwebkit_x86_media = 1.0.0`, and `webpositive_hwvideo` requires that. A
versioned requirement on a package's own name means nothing when the package
provides its name unrevisioned; give the change a name of its own.

The packages are served from their own repository,
`https://pkgman.rainygirl.com/x86_gcc2-webpositive`, so that adding the main
`x86_gcc2` repository does not replace anyone's WebKit. It carries
`haikuwebkit_x86`, `msvdx_media_x86`, `msvdx_firmware` and
`webpositive_hwvideo`; the recipes for the last three are in pkgman-repo.

`msvdx_media_x86`'s post-install script copies the add-on out of
`data/msvdx_media/` into the system's non-packaged add-ons directory (see
above); its pre-uninstall script removes it. Package `-0` or `-2` on the VAIO,
never the default level 9 (hours of CPU on the Atom).
