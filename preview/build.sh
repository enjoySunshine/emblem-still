#!/bin/sh
# Copyright 2026 Google Inc. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the COPYING file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS. All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
# -----------------------------------------------------------------------------
#
# Builds the WebAssembly encoder/decoder used by preview/pack.html.
#
# Requires the Emscripten SDK (emcc) on the PATH, e.g.:
#   source /path/to/emsdk/emsdk_env.sh
#
# The optimization flags mirror the release settings of makefile.unix
# (-O3 -DNDEBUG).
#
# Usage: preview/build.sh [output directory]

set -e

SRC_DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT_DIR=${1:-"$SRC_DIR/preview"}
cd "$SRC_DIR"

if ! command -v emcc >/dev/null 2>&1; then
  echo "emcc not found: install the Emscripten SDK and source emsdk_env.sh" >&2
  exit 1
fi

# Encoder, decoder, utilities and the animation muxer. Platform-specific SIMD
# sources (SSE2/NEON/MSA/MIPS) are left out: they do not build for wasm32 and
# the portable C paths are used instead.
SOURCES="
  src/enc/alpha_enc.c
  src/enc/analysis_enc.c
  src/enc/backward_references_cost_enc.c
  src/enc/backward_references_enc.c
  src/enc/config_enc.c
  src/enc/cost_enc.c
  src/enc/filter_enc.c
  src/enc/frame_enc.c
  src/enc/histogram_enc.c
  src/enc/iterator_enc.c
  src/enc/near_lossless_enc.c
  src/enc/picture_csp_enc.c
  src/enc/picture_enc.c
  src/enc/picture_psnr_enc.c
  src/enc/picture_rescale_enc.c
  src/enc/picture_tools_enc.c
  src/enc/predictor_enc.c
  src/enc/quant_enc.c
  src/enc/syntax_enc.c
  src/enc/token_enc.c
  src/enc/tree_enc.c
  src/enc/vp8l_enc.c
  src/enc/webp_enc.c
  src/dec/alpha_dec.c
  src/dec/buffer_dec.c
  src/dec/frame_dec.c
  src/dec/idec_dec.c
  src/dec/io_dec.c
  src/dec/quant_dec.c
  src/dec/tree_dec.c
  src/dec/vp8_dec.c
  src/dec/vp8l_dec.c
  src/dec/webp_dec.c
  src/demux/anim_decode.c
  src/demux/demux.c
  src/mux/anim_encode.c
  src/mux/muxedit.c
  src/mux/muxinternal.c
  src/mux/muxread.c
  src/dsp/alpha_processing.c
  src/dsp/cost.c
  src/dsp/cpu.c
  src/dsp/dec.c
  src/dsp/dec_clip_tables.c
  src/dsp/enc.c
  src/dsp/filters.c
  src/dsp/lossless.c
  src/dsp/lossless_enc.c
  src/dsp/rescaler.c
  src/dsp/ssim.c
  src/dsp/upsampling.c
  src/dsp/yuv.c
  src/utils/bit_reader_utils.c
  src/utils/bit_writer_utils.c
  src/utils/color_cache_utils.c
  src/utils/filters_utils.c
  src/utils/huffman_encode_utils.c
  src/utils/huffman_utils.c
  src/utils/palette.c
  src/utils/quant_levels_dec_utils.c
  src/utils/quant_levels_utils.c
  src/utils/random_utils.c
  src/utils/rescaler_utils.c
  src/utils/thread_utils.c
  src/utils/utils.c
  sharpyuv/sharpyuv.c
  sharpyuv/sharpyuv_cpu.c
  sharpyuv/sharpyuv_csp.c
  sharpyuv/sharpyuv_dsp.c
  sharpyuv/sharpyuv_gamma.c
"

emcc -O3 -DNDEBUG \
  -I"$SRC_DIR" -I"$SRC_DIR/src" \
  -sWASM=1 -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=PackModule \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS=_pk_encode,_pk_encode_anim,_pk_decode,_pk_decode_anim,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,UTF8ToString \
  -o "$OUT_DIR/pack_wasm.js" \
  preview/pack_wasm.c $SOURCES

echo "built $OUT_DIR/pack_wasm.js and $OUT_DIR/pack_wasm.wasm"
