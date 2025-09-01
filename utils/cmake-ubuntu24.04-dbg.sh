#  Copyright (C) 2023-2025, Xiadnoring (Timur Zajnullin).
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#  DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# 1) check

LAST_DIRECTORY=$(pwd | awk -F '/' '{print $NF}' 2>&1)

if ! [ "$LAST_DIRECTORY" = "manapi-http" ]; then
  echo "Please, retry from 'manapi-http' directory"
  exit 0
fi

ROOT=$(pwd)

# 2) configure
cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug -DMANAPIHTTP_GRPC_DEPENDENCY=OFF -DMANAPIHTTP_DISABLE_TRACE_HARD=ON \
      -DMANAPIHTTP_BUILD_TYPE=lib -DMANAPIHTTP_JSON_DEBUG=ON -DMANAPIHTTP_NGHTTP2_DEPENDENCY=ON -DMANAPIHTTP_NGHTTP3_DEPENDENCY=OFF \
      -DMANAPIHTTP_OPENSSL_DEPENDENCY=ON -DMANAPIHTTP_WOFLSSL_DEPENDENCY=OFF -DMANAPIHTTP_BROTLI_DEPENDENCY=ON -DMANAPIHTTP_ZSTD_DEPENDENCY=ON \
      -DMANAPIHTTP_ZLIB_DEPENDENCY=ON -DBUILD_SHARED_LIBS=ON -DMANAPIHTTP_STD_BACKTRACE_DEPENDENCY=OFF -DCMAKE_INSTALL_PREFIX=$(pwd)/package/usr \
      -DMANAPIHTTP_INSTALL_DIR=/x86_64-linux-gnu

# 3) compile
cmake --build build-dbg -j10

# 4) install
rm -rf package/usr
cmake --install build-dbg

# 5) fake root

# 5.1) pkgconfig
find package/usr/lib/pkgconfig -name 'manapihttp*.*' -exec sed -i -e 's|'$ROOT'/package||g' {} \;
# 5.2) cmake
find package/usr/lib/cmake/manapihttp -name '*.*' -exec sed -i -e 's|'$ROOT'/package||g' {} \;
# 5.3) includes
find package/usr/include/x86_64-linux-gnu/manapihttp -name '*.*' -exec sed -i -e 's|'$ROOT'/package||g' {} \;

# 6) Package
dpkg-deb --root-owner-group --build ./package manapihttp-vx.x.xubuntu24.04-dbg.deb

# 7) clean up
if [ -f include/ManapiParams.hpp ]; then
  rm include/ManapiParams.hpp
fi