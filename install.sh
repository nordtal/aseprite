#!/usr/bin/env bash
# Installs Nordtal Designer on macOS from source, or updates it:
#
#   curl -fsSL https://raw.githubusercontent.com/nordtal/aseprite/main/install.sh | bash
#
# It installs what the build needs that is missing (the Command Line
# Tools, CMake, Ninja and Skia), clones this repository or pulls it,
# builds the app and puts it into /Applications, or ~/Applications if
# that can't be written to. A failed step names itself and leaves an
# installed app as it was.
#
# NORDTAL_DESIGNER_HOME is where the source, the tools and the build
# go (~/.nordtal-designer), NORDTAL_DESIGNER_BRANCH the branch it
# builds (main) and NORDTAL_DESIGNER_JOBS how many compilers run at
# once (as many as Ninja chooses).
set -Eeuo pipefail

repo=https://github.com/nordtal/aseprite.git
home=${NORDTAL_DESIGNER_HOME:-$HOME/.nordtal-designer}
branch=${NORDTAL_DESIGNER_BRANCH:-main}
src=$home/source
tools=$home/tools
build=$home/build

CMAKE_VERSION=3.31.8
CMAKE_SHA256=d1449f969c54d5c00886d5b643340d493dfb3c81cb39ee29b35453395c11ebf7
NINJA_VERSION=1.12.1
NINJA_SHA256=89a287444b5b3e98f88a945afa50ce937b8ffd1dcc59c555ad9b1baf855298c9

current=
step() {
  current=$1
  echo "==> $1"
}
trap '[ -n "$current" ] && echo "Nordtal Designer: the step \"$current\" failed. The installed app is unchanged." >&2' ERR

[ "$(uname -s)" = Darwin ] || {
  echo "This script installs Nordtal Designer on macOS. On Windows, run install.ps1." >&2
  exit 1
}
case $(uname -m) in
  arm64) arch=arm64 ;;
  x86_64) arch=x64 ;;
  *) echo "Nordtal Designer builds for arm64 and x86_64 only." >&2; exit 1 ;;
esac
mkdir -p "$tools"

# fetch <url> <sha256> <file>
fetch() {
  curl -fsSL -o "$3.part" "$1"
  echo "$2  $3.part" | shasum -a 256 -c - > /dev/null
  mv "$3.part" "$3"
}

step "Command Line Tools"
if ! xcode-select -p > /dev/null 2>&1; then
  xcode-select --install || true
  echo "Install the Command Line Tools in the window macOS shows, then run the same line again." >&2
  current=
  exit 1
fi

step "CMake"
cmake=$tools/cmake-$CMAKE_VERSION/CMake.app/Contents/bin/cmake
if [ ! -x "$cmake" ]; then
  fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-macos-universal.tar.gz" \
        $CMAKE_SHA256 "$tools/cmake.tar.gz"
  rm -rf "$tools/cmake-$CMAKE_VERSION"
  mkdir "$tools/cmake-$CMAKE_VERSION"
  tar -xzf "$tools/cmake.tar.gz" -C "$tools/cmake-$CMAKE_VERSION" --strip-components 1
  rm "$tools/cmake.tar.gz"
fi

step "Ninja"
ninja=$tools/ninja-$NINJA_VERSION/ninja
if [ ! -x "$ninja" ]; then
  fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-mac.zip" \
        $NINJA_SHA256 "$tools/ninja.zip"
  rm -rf "$tools/ninja-$NINJA_VERSION"
  unzip -q "$tools/ninja.zip" -d "$tools/ninja-$NINJA_VERSION"
  rm "$tools/ninja.zip"
fi

step "Source"
if [ -d "$src/.git" ]; then
  git -C "$src" fetch --quiet origin "$branch"
  git -C "$src" checkout --quiet -B "$branch" FETCH_HEAD
  git -C "$src" submodule sync --quiet --recursive
  git -C "$src" submodule update --quiet --init --recursive
else
  rm -rf "$src"
  git clone --quiet --recursive --branch "$branch" "$repo" "$src"
fi

step "Skia"
skia_tag=$(xargs < "$src/laf/misc/skia-tag.txt")
skia=$tools/skia-$skia_tag-$arch
if [ ! -d "$skia/out/Release-$arch" ]; then
  curl -fsSL -o "$tools/skia.zip" "$(bash "$src/laf/misc/skia-url.sh" release macos $arch)"
  rm -rf "$skia"
  unzip -q "$tools/skia.zip" -d "$skia"
  rm "$tools/skia.zip"
fi

step "Build"
"$cmake" -S "$src" -B "$build" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$ninja" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES=$(uname -m) \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DENABLE_CCACHE=off \
  -DLAF_BACKEND=skia \
  -DSKIA_DIR="$skia" \
  -DSKIA_LIBRARY_DIR="$skia/out/Release-$arch" > "$home/configure.log" ||
  { tail -n 30 "$home/configure.log" >&2; false; }
"$ninja" -C "$build" ${NORDTAL_DESIGNER_JOBS:+-j "$NORDTAL_DESIGNER_JOBS"} aseprite
built="$build/bin/Nordtal Designer.app"

step "Bootstrap tools"
bash "$src/misc/designer/fetch-tools.sh" macos "$built/Contents/Resources/data"

step "Install"
apps=/Applications
[ -w "$apps" ] || { apps=$HOME/Applications; mkdir -p "$apps"; }
app="$apps/Nordtal Designer.app"
# The new app is copied next to the old one first, so a failed copy
# leaves the old one in place
rm -rf "$app.new" "$app.old"
ditto "$built" "$app.new"
[ ! -e "$app" ] || mv "$app" "$app.old"
mv "$app.new" "$app" || { [ ! -e "$app.old" ] || mv "$app.old" "$app"; false; }
rm -rf "$app.old"
current=
echo "Nordtal Designer is installed in $apps. Run the same line again to update it."
