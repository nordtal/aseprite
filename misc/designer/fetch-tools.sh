#!/usr/bin/env bash
# Puts the tools of the bootstrap extension into <data>/extensions/
# nordtal-bootstrap/tools: gh on every platform, MinGit on Windows.
# The versions and checksums are the vendors' release assets.
#
#   bash misc/designer/fetch-tools.sh macos|windows <data folder>
set -euo pipefail

platform=$1
tools=$2/extensions/nordtal-bootstrap/tools

GH_VERSION=2.103.2
GH_MACOS_SHA256=5416000f5d17580a187339e0c4068a01ca5c87f7d4fe6ca64933696f967bc418
GH_WINDOWS_SHA256=fef9fcdd8fdabd0501ab8b3d99eac976f504ab55d01bef9e8ef428a5d900ddda
MINGIT_TAG=v2.56.0.windows.3
MINGIT_ZIP=MinGit-2.56.0.3-64-bit.zip
MINGIT_SHA256=2e5619966e588fde5f8122c5557cad50c0fbafcaed736689f40ec1dfb1d78ea7

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# fetch <url> <sha256> <folder>: unzips the archive into the folder
fetch() {
  curl -fsSL -o "$work/a.zip" "$1"
  # Git for Windows' bash has sha256sum, macOS has shasum
  if command -v sha256sum > /dev/null; then
    echo "$2  $work/a.zip" | sha256sum -c - > /dev/null
  else
    echo "$2  $work/a.zip" | shasum -a 256 -c - > /dev/null
  fi
  rm -rf "$3" "$work/x"
  mkdir -p "$3" "$work/x"
  unzip -q "$work/a.zip" -d "$work/x"
  # gh's macOS archive has a top folder, the others don't
  local top=("$work"/x/*)
  if [[ ${#top[@]} -eq 1 && -d ${top[0]} && ! -d $work/x/bin && ! -d $work/x/cmd ]]; then
    cp -R "${top[0]}"/. "$3"
  else
    cp -R "$work"/x/. "$3"
  fi
  rm -f "$work/a.zip"
}

gh_url=https://github.com/cli/cli/releases/download/v$GH_VERSION
case $platform in
  macos)
    fetch "$gh_url/gh_${GH_VERSION}_macOS_arm64.zip" $GH_MACOS_SHA256 "$tools/gh"
    test -x "$tools/gh/bin/gh"
    ;;
  windows)
    fetch "$gh_url/gh_${GH_VERSION}_windows_amd64.zip" $GH_WINDOWS_SHA256 "$tools/gh"
    test -f "$tools/gh/bin/gh.exe"
    fetch "https://github.com/git-for-windows/git/releases/download/$MINGIT_TAG/$MINGIT_ZIP" \
          $MINGIT_SHA256 "$tools/mingit"
    test -f "$tools/mingit/cmd/git.exe"
    ;;
  *)
    echo "usage: $0 macos|windows <data folder>" >&2
    exit 1
    ;;
esac
