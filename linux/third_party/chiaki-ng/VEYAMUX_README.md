# chiaki-ng v1.10.1 subset (PlayStation Remote Play)

The PlayStation Remote Play protocol library of chiaki-ng (https://github.com/streetpea/chiaki-ng, tag v1.10.1,
commit f30da0aaeb96fc0026cc03683153f682352e7d7c), built into VeyAmux by `cmake/chiaki.cmake` and used by
`src/RemotePlay.cpp`. Licence: **AGPL-3.0-only with the OpenSSL exception** (`LICENSES/AGPL-3.0-only-OpenSSL.txt`,
`COPYING`). Because VeyAmux links it, the VeyAmux program as distributed is covered by the AGPL-3.0 as well (VeyAmux's own
code stays GPL-3.0; GPL-3.0 section 13 allows the combination).

What is here, unchanged:
- `lib/include`, `lib/src` (without `pidecoder.c` and `remote/holepunch-test.c`), `lib/config.h.in`,
  `lib/protobuf/takion.proto`
- `generated/takion.pb.c`, `generated/takion.pb.h`: generated from `takion.proto` with protoc and the nanopb generator
  of the commit below (`protoc -o takion.pb takion.proto && nanopb_generator.py takion.pb`), so building VeyAmux needs
  neither protoc nor Python protobuf.
- `third-party/nanopb` (pb_*.c/h only): https://github.com/nanopb/nanopb commit 160d4f09e5fabb2b66aa2dea32d4f38ace2c4b3f,
  zlib licence (`LICENSE.txt`)
- `third-party/jerasure` (include, src): https://github.com/streetpea/jerasure commit 505ccb4bc69eee8d7ea40a6089a056b99671134f,
  BSD-3-Clause (`License.txt`)
- `third-party/gf-complete` (include, src): https://github.com/streetpea/gf-complete commit fa54a4670a5705c84abf6c24b92b0cd479625478,
  BSD-3-Clause (`License.txt`)

Not used: chiaki-ng's Qt GUI, its video decoders (VeyAmux decodes with its own FFmpeg) and the PSN internet connection
(holepunch; compiled, not called). VeyAmux's own code only uses the library's public API (discovery, registration,
wake-up, session, controller state, Opus decoder).
