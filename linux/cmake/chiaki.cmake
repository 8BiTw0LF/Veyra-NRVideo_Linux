# chiaki-ng v1.10.1 (AGPL-3.0-only with OpenSSL exception): the PlayStation Remote Play protocol library, built into
# VeyAmux for "PlayStation Remote Play" (src/RemotePlay.cpp). Only chiaki-ng's lib/ and the protocol helpers it needs
# (nanopb, jerasure, gf-complete) are vendored in third_party/chiaki-ng; see third_party/chiaki-ng/VEYAMUX_README.md.
set(CHIAKI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chiaki-ng")

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
pkg_check_modules(CHIAKI_DEPS REQUIRED IMPORTED_TARGET opus json-c miniupnpc libevent)

# gf-complete + jerasure (Reed-Solomon FEC for the video stream)
add_library(veyamux_jerasure STATIC
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf.c ${CHIAKI_DIR}/third-party/gf-complete/src/gf_wgen.c
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w4.c ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w8.c
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w16.c ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w32.c
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w64.c ${CHIAKI_DIR}/third-party/gf-complete/src/gf_w128.c
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf_rand.c ${CHIAKI_DIR}/third-party/gf-complete/src/gf_general.c
  ${CHIAKI_DIR}/third-party/gf-complete/src/gf_cpu.c
  ${CHIAKI_DIR}/third-party/jerasure/src/galois.c ${CHIAKI_DIR}/third-party/jerasure/src/jerasure.c
  ${CHIAKI_DIR}/third-party/jerasure/src/reed_sol.c ${CHIAKI_DIR}/third-party/jerasure/src/cauchy.c
  ${CHIAKI_DIR}/third-party/jerasure/src/liberation.c)
target_include_directories(veyamux_jerasure PUBLIC ${CHIAKI_DIR}/third-party/gf-complete/include ${CHIAKI_DIR}/third-party/jerasure/include)
target_compile_options(veyamux_jerasure PRIVATE -w)
set_target_properties(veyamux_jerasure PROPERTIES POSITION_INDEPENDENT_CODE ON)

# nanopb (protobuf messages of the control channel); takion.pb.c/h are generated from lib/protobuf/takion.proto
add_library(veyamux_nanopb STATIC
  ${CHIAKI_DIR}/third-party/nanopb/pb_common.c ${CHIAKI_DIR}/third-party/nanopb/pb_decode.c
  ${CHIAKI_DIR}/third-party/nanopb/pb_encode.c ${CHIAKI_DIR}/generated/takion.pb.c)
target_include_directories(veyamux_nanopb PUBLIC ${CHIAKI_DIR}/third-party/nanopb ${CHIAKI_DIR}/generated)
target_compile_definitions(veyamux_nanopb PUBLIC PB_C99_STATIC_ASSERT)
set_target_properties(veyamux_nanopb PROPERTIES POSITION_INDEPENDENT_CODE ON)

# chiaki lib
set(CHIAKI_LIB_ENABLE_OPUS 1)
set(CHIAKI_LIB_ENABLE_PI_DECODER 0)
set(CHIAKI_LIB_ENABLE_FFMPEG_DECODER 0)
configure_file(${CHIAKI_DIR}/lib/config.h.in ${CMAKE_BINARY_DIR}/chiaki/include/chiaki/config.h)
file(GLOB CHIAKI_LIB_SOURCES ${CHIAKI_DIR}/lib/src/*.c ${CHIAKI_DIR}/lib/src/remote/*.c)
list(FILTER CHIAKI_LIB_SOURCES EXCLUDE REGEX "/(ffmpegdecoder|pidecoder|aia_mbedtls)\\.c$")
add_library(veyamux_chiaki STATIC ${CHIAKI_LIB_SOURCES})
target_include_directories(veyamux_chiaki PUBLIC ${CHIAKI_DIR}/lib/include ${CMAKE_BINARY_DIR}/chiaki/include)
target_compile_options(veyamux_chiaki PRIVATE -w)
target_link_libraries(veyamux_chiaki PUBLIC veyamux_nanopb veyamux_jerasure OpenSSL::Crypto OpenSSL::SSL CURL::libcurl
  PkgConfig::CHIAKI_DEPS Threads::Threads)
