/**
 * 本地打包尾部：网页仍传整文件，设备验完剥尾再激活。
 *
 * 布局：[ELF 本体][OtaPkgTrailer 48 字节]
 *   magic[8]     = "DPLRPKG1"（与 Doppler/TEL-15 同格式，靠 product_id 区分产品）
 *   product_id   = LE u32，通辽泥沙升降默认 'NSHJ' = 0x4E53484A
 *   bin_size     = LE u32，ELF 字节数
 *   sha256[32]   = ELF 本体的 SHA256
 */
#ifndef LIBOTA_PKG_H
#define LIBOTA_PKG_H

#include <stdint.h>

#define OTA_PKG_MAGIC        "DPLRPKG1"
#define OTA_PKG_MAGIC_LEN    8U
#define OTA_PKG_TRAILER_SIZE 48U
#define OTA_PKG_PRODUCT_NSHJ 0x4E53484Au /* 'NSHJ' */

#pragma pack(push, 1)
typedef struct OtaPkgTrailer {
    char     magic[OTA_PKG_MAGIC_LEN];
    uint32_t product_id;
    uint32_t bin_size;
    uint8_t  sha256[32];
} OtaPkgTrailer;
#pragma pack(pop)

/* 校验 path 尾部并截断为纯 ELF；expect_product 为 0 则不检查产品 ID。成功 0 */
int ota_pkg_verify_and_strip(const char *path, uint32_t expect_product);

#endif
