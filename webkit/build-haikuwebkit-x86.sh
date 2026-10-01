#!/bin/bash
#
# Build haikuwebkit_x86 1.9.19-6: HaikuWebKit 1.9.19 for the x86 secondary
# architecture of an x86_gcc2 hybrid, with HTML5 video working.
#
# The build itself is haiku-rwebpositive-arm64's build-haikuwebkit-x86.sh,
# which carries the leak fixes (haikuwebkit-1.9.19-x86.patch). This wrapper
# runs it with this repository's haikuwebkit-1.9.19-media.patch applied on
# top and this repository's .PackageInfo, so the package says what it carries.
#
# Usage: RWP=/path/to/haiku-rwebpositive-arm64 build-haikuwebkit-x86.sh [step]
# (steps as in the wrapped script; the default is all of them)
#
set -eu
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RWP="${RWP:-$HERE/../../haiku-rwebpositive-arm64}"
[ -f "$RWP/build-haikuwebkit-x86.sh" ] ||
	{ echo "set RWP to a haiku-rwebpositive-arm64 checkout" >&2; exit 1; }

# The wrapped script finds its patch, .PackageInfo and toolchain file next to
# itself, so give it a directory where those are ours.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
for f in "$RWP"/*; do ln -s "$f" "$WORK/"; done
# The script itself is copied too: it locates its files from its own path.
rm "$WORK/haikuwebkit-1.9.19-x86.patch" "$WORK/haikuwebkit-x86.PackageInfo" \
	"$WORK/build-haikuwebkit-x86.sh"
cat "$RWP/haikuwebkit-1.9.19-x86.patch" "$HERE/haikuwebkit-1.9.19-media.patch" \
	> "$WORK/haikuwebkit-1.9.19-x86.patch"
cp "$HERE/haikuwebkit-x86.PackageInfo" "$WORK/haikuwebkit-x86.PackageInfo"
cp "$RWP/build-haikuwebkit-x86.sh" "$WORK/build-haikuwebkit-x86.sh"

REVISION="${REVISION:-6}" bash "$WORK/build-haikuwebkit-x86.sh" "${1:-all}"
