// Copyright 2026 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// WebAssembly entry points for the preview/pack.html packing preview.
//
// All buffers exchanged with JavaScript are 32-bit ARGB pixels stored with a
// little-endian layout, i.e. the byte order is B, G, R, A. That is what
// WebPPictureImportBGRA() and WebPDecodeBGRA() expect, and it is also the
// layout of the *.argb fixtures used by the page.

#include <stdlib.h>
#include <string.h>

#include "src/webp/decode.h"
#include "src/webp/demux.h"
#include "src/webp/encode.h"
#include "src/webp/mux.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define PK_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define PK_EXPORT
#endif

static uint8_t* AllocCopy(const uint8_t* data, size_t size, int* const out_size) {
  uint8_t* const copy = (uint8_t*)malloc(size);
  if (copy == NULL) return NULL;
  if (size > 0) memcpy(copy, data, size);
  *out_size = (int)size;
  return copy;
}

// Encodes one BGRA image. The returned bitstream is malloc'd; the caller owns
// it (and frees it through _free).
PK_EXPORT uint8_t* pk_encode(const uint8_t* const bgra, int width, int height,
                             float quality, int method,
                             int* const out_size) {
  WebPPicture pic;
  WebPConfig config;
  WebPMemoryWriter writer;
  uint8_t* result = NULL;
  *out_size = 0;

  if (!WebPPictureInit(&pic) || !WebPConfigInit(&config)) return NULL;
  pic.width = width;
  pic.height = height;
  pic.use_argb = 1;
  if (!WebPPictureImportBGRA(&pic, bgra, width * 4)) {
    WebPPictureFree(&pic);
    return NULL;
  }
  config.lossless = 1;
  config.quality = quality;
  config.method = method;
  config.exact = 1;

  WebPMemoryWriterInit(&writer);
  pic.writer = WebPMemoryWrite;
  pic.custom_ptr = &writer;
  if (WebPEncode(&config, &pic)) {
    result = AllocCopy(writer.mem, writer.size, out_size);
  }
  WebPPictureFree(&pic);
  WebPMemoryWriterClear(&writer);
  return result;
}

// Encodes 'nframes' frames of size 'frame_width' x 'height' laid out
// horizontally in 'bgra', with timestamps 0, 50, 100, ... and an end timestamp
// of 50 * nframes.
PK_EXPORT uint8_t* pk_encode_anim(const uint8_t* const bgra, int strip_width,
                                  int height, int frame_width, int nframes,
                                  float quality, int method,
                                  int* const out_size) {
  WebPAnimEncoderOptions options;
  WebPAnimEncoder* enc;
  WebPData data;
  uint8_t* frame = NULL;
  uint8_t* result = NULL;
  int i;
  *out_size = 0;

  if (nframes <= 0 || frame_width <= 0 || frame_width > strip_width) return NULL;
  frame = (uint8_t*)malloc((size_t)frame_width * height * 4);
  if (frame == NULL) return NULL;
  if (!WebPAnimEncoderOptionsInit(&options)) {
    free(frame);
    return NULL;
  }
  enc = WebPAnimEncoderNew(frame_width, height, &options);
  if (enc == NULL) {
    free(frame);
    return NULL;
  }
  for (i = 0; i < nframes; ++i) {
    WebPPicture pic;
    WebPConfig config;
    int y;
    int ok;
    for (y = 0; y < height; ++y) {
      memcpy(frame + (size_t)y * frame_width * 4,
             bgra + (size_t)y * strip_width * 4 + (size_t)i * frame_width * 4,
             (size_t)frame_width * 4);
    }
    if (!WebPPictureInit(&pic) || !WebPConfigInit(&config)) break;
    pic.width = frame_width;
    pic.height = height;
    pic.use_argb = 1;
    if (!WebPPictureImportBGRA(&pic, frame, frame_width * 4)) {
      WebPPictureFree(&pic);
      break;
    }
    config.lossless = 1;
    config.quality = quality;
    config.method = method;
    config.exact = 1;
    ok = WebPAnimEncoderAdd(enc, &pic, i * 50, &config);
    WebPPictureFree(&pic);
    if (!ok) break;
  }
  free(frame);
  if (i == nframes) {
    WebPDataInit(&data);
    if (WebPAnimEncoderAdd(enc, NULL, nframes * 50, NULL) &&
        WebPAnimEncoderAssemble(enc, &data)) {
      result = AllocCopy(data.bytes, data.size, out_size);
      WebPDataClear(&data);
    }
  }
  WebPAnimEncoderDelete(enc);
  return result;
}

// Decodes a still image to BGRA. The returned buffer is malloc'd.
PK_EXPORT uint8_t* pk_decode(const uint8_t* const data, int size, int* out_width,
                             int* out_height) {
  int width, height;
  uint8_t* const pixels =
      WebPDecodeBGRA(data, (size_t)size, &width, &height);
  *out_width = width;
  *out_height = height;
  return pixels;
}

// Decodes every frame of an animation to full-canvas BGRA. Frames are
// concatenated; the timestamps are returned in a separate malloc'd array.
PK_EXPORT uint8_t* pk_decode_anim(const uint8_t* const data, int size,
                                  int* out_count, int* out_width,
                                  int* out_height, int** out_timestamps,
                                  int* out_timestamp_bytes) {
  WebPAnimDecoderOptions options;
  WebPAnimDecoder* dec;
  WebPData webp_data;
  WebPAnimInfo info;
  uint8_t* frames = NULL;
  int* timestamps = NULL;
  size_t frame_bytes;
  int count = 0;
  int capacity = 0;

  *out_count = 0;
  *out_width = 0;
  *out_height = 0;
  *out_timestamps = NULL;
  *out_timestamp_bytes = 0;
  if (!WebPAnimDecoderOptionsInit(&options)) return NULL;
  options.color_mode = MODE_BGRA;
  options.use_threads = 0;
  webp_data.bytes = data;
  webp_data.size = (size_t)size;
  dec = WebPAnimDecoderNew(&webp_data, &options);
  if (dec == NULL) return NULL;
  if (!WebPAnimDecoderGetInfo(dec, &info)) {
    WebPAnimDecoderDelete(dec);
    return NULL;
  }
  frame_bytes = (size_t)info.canvas_width * info.canvas_height * 4;
  while (WebPAnimDecoderHasMoreFrames(dec)) {
    uint8_t* buf = NULL;
    int timestamp = 0;
    uint8_t* grown;
    if (!WebPAnimDecoderGetNext(dec, &buf, &timestamp)) break;
    if (count == capacity) {
      const int new_capacity = (capacity == 0) ? 8 : capacity * 2;
      grown = (uint8_t*)realloc(frames, frame_bytes * new_capacity);
      if (grown == NULL) break;
      frames = grown;
      grown = (uint8_t*)realloc(timestamps, sizeof(int) * new_capacity);
      if (grown == NULL) break;
      timestamps = (int*)grown;
      capacity = new_capacity;
    }
    memcpy(frames + frame_bytes * count, buf, frame_bytes);
    timestamps[count] = timestamp;
    ++count;
  }
  WebPAnimDecoderDelete(dec);
  if (count == 0) {
    free(frames);
    free(timestamps);
    return NULL;
  }
  *out_count = count;
  *out_width = (int)info.canvas_width;
  *out_height = (int)info.canvas_height;
  *out_timestamps = timestamps;
  *out_timestamp_bytes = (int)(sizeof(int) * count);
  return frames;
}
