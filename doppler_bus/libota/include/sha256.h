#ifndef LIBOTA_SHA256_H
#define LIBOTA_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} Sha256Ctx;

void sha256_init(Sha256Ctx *ctx);
void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx *ctx, uint8_t hash[32]);
/* 对文件前 file_len 字节做 SHA256；成功 0，失败 -1 */
int sha256_file_prefix(const char *path, uint32_t file_len, uint8_t hash[32]);

#endif
