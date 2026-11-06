#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# System 37: the three SMK-37 packages (SMK-37 Pro, SMK-37 Elite, MKE-P37) into build/release/ with their SHA-256,
# the site with the installer and the editor in build/system37-site/, then the FM-1 build last (the reference the
# host tests use: tests/run_tests.sh reads build/felucca.*). Linux x86-64 or WSL, as ./build.sh.
#   tools/build_pro.sh [--release X.Y] [--no-fm1]
#   JIELI_TOOLCHAIN, AC79_SDK as for ./build.sh
set -e
cd "$(dirname "$0")/.."
REL="" FM1=1
while [ $# -gt 0 ]; do
    case "$1" in
        --release) REL="$2"; shift 2 ;;
        --no-fm1) FM1=0; shift ;;
        *) echo "tools/build_pro.sh [--release X.Y] [--no-fm1]"; exit 2 ;;
    esac
done
VER=$(sed -n 's/^#define FELUCCA_VERSION "SLOOP PRO \([^"]*\)".*/\1/p' firmware/src/felucca.c | head -1)
[ -n "$REL" ] && VER="$REL"
mkdir -p build/release
for m in pro elite mke; do
    sfx=""; [ "$m" = pro ] || sfx="-$m"
    echo "== System 37 $VER, MODEL=$m"
    if [ -n "$REL" ]; then
        BOARD=smk37 MODEL=$m FELUCCA_SLICE=0 sh build.sh --release "$REL"
        cp "build/system37$sfx-$REL.fwsc" "build/release/system37$sfx.fwsc"
    else
        BOARD=smk37 MODEL=$m FELUCCA_SLICE=0 sh build.sh
        cp "build/system37$sfx.fwsc" build/release/
    fi
done
(cd build/release && sha256sum system37.fwsc system37-elite.fwsc system37-mke.fwsc | tee SHA256SUMS.txt)
python3 web/make_site.py --pro "$VER" build/system37-site build/release/system37.fwsc build/release/system37-elite.fwsc \
    build/release/system37-mke.fwsc
if [ "$FM1" = 1 ]; then
    echo "== the FM-1 reference build (for tests/run_tests.sh)"
    FELUCCA_SLICE=0 sh build.sh
fi
echo "packages: build/release/   site: build/system37-site/ (python3 -m http.server 8767 --directory build/system37-site)"
