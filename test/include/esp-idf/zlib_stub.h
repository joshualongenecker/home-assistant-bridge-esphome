#ifndef ESP_ZLIB_STUB_H
#define ESP_ZLIB_STUB_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char Bytef;
typedef unsigned int uInt;

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_FINISH 4

typedef struct z_stream_s {
    const Bytef *next_in;
    uInt avail_in;
    Bytef *next_out;
    uInt avail_out;
    size_t total_out;
    int stub_result;
} z_stream;

static inline int inflateInit2(z_stream *strm, int) {
    (void)strm;
    return Z_OK;
}

static inline int inflate(z_stream *strm, int flush) {
    (void)flush;
    return strm->stub_result;
}

static inline void inflateEnd(z_stream *strm) {
    (void)strm;
}

#ifdef __cplusplus
}
#endif

#endif
