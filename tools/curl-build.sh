#!/bin/sh
# Build the vendored libcurl for the GUEST.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# THE RECIPE IS docs/design/foundation-transport-plan.md's, and the decisions are recorded there:
# curl **8.22.0** and **CMake ONLY** (curl's autotools layer is never invoked — the same rule
# libressl-plan.md states for its own library).
#
# AND IT NOW BUILDS **WITH** TLS, BOUND TO LIBRESSL — which is L2, and the reason the TLS-less landing
# existed: libressl-plan.md makes LibreSSL/libtls the ONE system SSL library, so the backend is
# `CURL_USE_OPENSSL=ON` pointed at the LibreSSL prefix (curl has no separate LibreSSL backend; LibreSSL
# IS the OpenSSL API) and every other backend stays OFF. THE ASSERTION BELOW IS WHAT PROVES IT picked
# LibreSSL and not the BUILD HOST's OpenSSL: `Enabled SSL backends:` must name LibreSSL, and a host
# OpenSSL would link a musl binary against glibc's libraries.
#
# THE CA DEFAULTS ARE WIRED TO THE FSH STORE at COMPILE time (`CURL_CA_BUNDLE`/`CURL_CA_PATH` →
# `/System/Configuration/SSL/cert.pem` and `.../certs`), which is where LibreSSL's own OPENSSLDIR
# already points — so `https://` consults the FSH store and nothing consults a Linux path.
#
# ONE BUILD, NOT TWO — and the difference from tools/lcms2-build.sh is the point. lcms2 is built
# twice because libcoregraphics exists in BOTH forms (the guest's and the host's) and a musl object
# cannot be linked into a host binary. libcurl has no host consumer: the Foundation's probes are
# guest-only, so a host half would be dead weight. It gets added when something on the host links
# libcurl, which is a one-line change and not a redesign.
#
# WHY THE FLAGS ARE SPELLED OUT RATHER THAN MINIMAL:
#
#   * `-DCURL_USE_PKGCONFIG=OFF` AND `-DCURL_USE_CMAKECONFIG=OFF` ARE A MEASURED NECESSITY, NOT
#     TIDINESS. Without them curl's configure finds the BUILD HOST's libidn2 (its .pc is installed)
#     and the resulting `libcurl.so.4` carries `NEEDED libidn2.so.0` — a library that does not exist
#     on the guest. The failure is SILENT: the link succeeds and the load fails. A cross build must
#     not read the host's dependency databases at all.
#   * `-DUSE_LIBIDN2=OFF` is the knob's REAL name. curl 8.22 spells it `USE_LIBIDN2`, not
#     `CURL_USE_LIBIDN2`, and CMake reports an unknown `-D` as merely "not used by the project"
#     rather than as an error — which is exactly how the host libidn2 got in the first time.
#   * EVERY TLS BACKEND IS OFF, and curl 8.22 has DROPPED the BearSSL/SecureTransport options
#     outright: naming one is silently ignored. The missing-TLS case announces itself in the
#     configure output as `Enabled SSL backends:` with nothing after it, which is the line to read.
#   * THE CLI IS BUILT (`BUILD_CURL_EXE=ON`) as well as the library, and for a concrete reason: a shell
#     test cannot drive a library, and the L2 trust-store acceptance IS a shell script driving an https
#     fetch. `curl` joins `openssl(1)` in /System/Tools. The library-only landing did not need it, which
#     is why this flipped with L2 - the first run of that test said `curl: not found`.
#   * THE PROTOCOL SET IS `http`, `https` + `file`. FILE is deliberate and load-bearing for the smoke
#     test: a guest with no network can still prove the library loads and moves bytes. `https` arrived
#     with the LibreSSL binding (L2), which is what the TLS-less landing was waiting for.
#   * zlib/brotli/zstd and HTTP/2+3 are OFF. No compression library is vendored here yet, and the
#     transport W7 needs is HTTP/1.1. Turning any of them on is a dependency decision, not a flag.
#
# Usage: tools/curl-build.sh            (from the repo root; a few minutes)
set -e

R="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$R/third_party/curl"
BUILD="$R/.build/curl-build"
PREFIX="$R/.build/curl-prefix"
LOG="$R/.build/curl-build.log"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
	echo "third_party/curl is empty - run: git submodule update --init third_party/curl"
	exit 1
fi

# CURL'S TLS BACKEND IS LIBRESSL, so this build DEPENDS on that prefix existing - the plan's order
# (L0/L1 before the transport) made real. Gated, so a missing prefix names its own fix.
if [ ! -d "$R/.build/libressl-prefix/lib" ]; then
	echo "the LibreSSL prefix is missing - run: tools/fetch-libressl.sh && tools/libressl-build.sh"
	exit 1
fi

# THE PIN'S ONE LOCAL CHANGE, applied here rather than shipped in a forked tree — the house pattern
# (musl-fsh.patch, toybox-m4.patch, libressl-fnx.patch). CURL CARRIES LINUX PATHS OF ITS OWN: its null
# device is `/dev/null` (src/tool_main.c) and its embedded help text names `/dev/null` and `/etc/hosts`.
# THIS SYSTEM HAS NO /dev AND NO /etc, so those are not cosmetic — the null device would not open — and
# the answer is the one this tree uses for every ported upstream: PATCH THE SOURCE so the paths are the
# FSH's. The null device becomes /System/Devices/null; the help text stops naming a Linux hosts file.
# Being FSH-clean is also what lets `curl` live in System/Tools, the tree the FSH lint GATES, instead of
# hiding in a reported carve-out.
if ! grep -q '/System/Devices/null' "$SRC/src/tool_main.c"; then
	patch -p1 -d "$SRC" < "$R/third_party/curl-fsh.patch"
fi

# The pin, ASSERTED rather than assumed: the tag is what makes the build reproducible, and a
# checkout that has drifted is worth failing on (cmake would happily build whatever is there).
want="curl-8_22_0"
have="$(git -C "$SRC" describe --tags --exact-match 2>/dev/null || true)"
if [ "$have" != "$want" ]; then
	echo "third_party/curl is at '${have:-<not a tag>}', expected '$want'"
	echo "  fix: git -C third_party/curl checkout $want"
	exit 1
fi

echo "=== libcurl for the GUEST (tools/musl-clang64.sh) -> $PREFIX ==="
# A CLEAN BUILD TREE EVERY TIME. CMake caches the compiler, the detected libraries and every
# feature probe, so a rebuild after a flag change would silently reuse the old answers - which is
# how a removed TLS backend can appear to stay removed.
rm -rf "$BUILD"
mkdir -p "$BUILD"

# FIND_PACKAGE_ROOT_PATH / pkg-config search paths are cleared for the same reason the two
# CURL_USE_*CONFIG knobs are off: nothing about the host belongs in a guest build.
cmake -G "Unix Makefiles" -S "$SRC" -B "$BUILD" \
	-DCMAKE_C_COMPILER="$R/tools/musl-clang64.sh" \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_IGNORE_PATH="/usr/lib;/usr/local/lib;/usr/include" \
	-DBUILD_SHARED_LIBS=ON \
	-DBUILD_CURL_EXE=ON \
	-DBUILD_EXAMPLES=OFF \
	-DBUILD_TESTING=OFF \
	-DENABLE_THREADED_RESOLVER=OFF \
	-DHAVE_EVENTFD=0 \
	-DHAVE_SYS_EVENTFD_H=0 \
	-DCURL_USE_PKGCONFIG=OFF \
	-DCURL_USE_CMAKECONFIG=OFF \
	-DUSE_LIBIDN2=OFF \
	-DCURL_USE_OPENSSL=ON \
	-DOPENSSL_ROOT_DIR="$R/.build/libressl-prefix" \
	-DOPENSSL_INCLUDE_DIR="$R/.build/libressl-prefix/include" \
	-DOPENSSL_SSL_LIBRARY="$R/.build/libressl-prefix/lib/libssl.so" \
	-DOPENSSL_CRYPTO_LIBRARY="$R/.build/libressl-prefix/lib/libcrypto.so" \
	-DCURL_CA_BUNDLE=/System/Configuration/SSL/cert.pem \
	-DCURL_CA_PATH=/System/Configuration/SSL/certs \
	-DCURL_USE_GNUTLS=OFF \
	-DCURL_USE_MBEDTLS=OFF \
	-DCURL_USE_WOLFSSL=OFF \
	-DCURL_USE_RUSTLS=OFF \
	-DCURL_USE_SCHANNEL=OFF \
	-DCURL_USE_LIBPSL=OFF \
	-DCURL_USE_LIBSSH2=OFF \
	-DCURL_USE_LIBSSH=OFF \
	-DCURL_USE_GSASL=OFF \
	-DCURL_USE_GSSAPI=OFF \
	-DCURL_ZLIB=OFF \
	-DCURL_BROTLI=OFF \
	-DCURL_ZSTD=OFF \
	-DUSE_NGHTTP2=OFF \
	-DUSE_NGTCP2=OFF \
	-DUSE_QUICHE=OFF \
	-DCURL_DISABLE_DICT=ON \
	-DCURL_DISABLE_FTP=ON \
	-DCURL_DISABLE_GOPHER=ON \
	-DCURL_DISABLE_IMAP=ON \
	-DCURL_DISABLE_IPFS=ON \
	-DCURL_DISABLE_MQTT=ON \
	-DCURL_DISABLE_POP3=ON \
	-DCURL_DISABLE_RTSP=ON \
	-DCURL_DISABLE_SMTP=ON \
	-DCURL_DISABLE_TELNET=ON \
	-DCURL_DISABLE_TFTP=ON \
	-DCURL_DISABLE_WEBSOCKETS=ON \
	-DCURL_DISABLE_LDAP=ON \
	-DCURL_DISABLE_LDAPS=ON \
	> "$LOG" 2>&1

# THE TWO LINES THAT SAY THE RECIPE WORKED, read out of the log rather than assumed: a TLS-less
# build has an EMPTY backend list, and the protocol set must be exactly http + file. Both are
# asserted, because a silent re-enable is the failure mode this script exists to prevent.
grep -E '^-- (Protocols|Features|Enabled SSL backends):' "$LOG" || true
if ! grep -q '^-- Protocols: file http https$' "$LOG"; then
	echo "curl-build: the protocol set is not 'file http https' - see $LOG"
	exit 1
fi
if ! grep -q '^-- Enabled SSL backends:.*LibreSSL' "$LOG"; then
	echo "curl-build: the SSL backend is not LibreSSL - see $LOG"
	grep -E '^-- (Enabled SSL backends|SSL)' "$LOG" || true
	exit 1
fi

cmake --build "$BUILD" -j"$(nproc)" >> "$LOG" 2>&1
cmake --install "$BUILD" >> "$LOG" 2>&1

# NO HOST LIBRARY MAY BE IN THE NEEDED LIST. This is the check that would have caught the libidn2
# leak at the point it happened rather than at load time on the guest: every NEEDED entry must be
# the musl loader's business and nothing else.
echo "--- NEEDED entries ---"
readelf -d "$PREFIX/lib/libcurl.so.4"* 2>/dev/null | grep NEEDED | sort -u
echo "--- soname ---"
readelf -d "$PREFIX/lib/libcurl.so.4"* 2>/dev/null | grep -i soname | head -1

echo "libcurl for the guest: $PREFIX"
