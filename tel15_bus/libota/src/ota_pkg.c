#include "ota_pkg.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t load_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int ota_pkg_verify_and_strip(const char *path, uint32_t expect_product)
{
    FILE *fp = NULL;
    long file_size = 0;
    uint8_t raw[OTA_PKG_TRAILER_SIZE];
    uint8_t calc[32];
    uint8_t mag[4];
    uint32_t product_id = 0;
    uint32_t bin_size = 0;

    if (path == NULL) {
        return -1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    file_size = ftell(fp);
    if (file_size < (long)(OTA_PKG_TRAILER_SIZE + 4U)) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, -((long)OTA_PKG_TRAILER_SIZE), SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(raw, 1, sizeof(raw), fp) != sizeof(raw)) {
        fclose(fp);
        return -1;
    }
    if (memcmp(raw, OTA_PKG_MAGIC, OTA_PKG_MAGIC_LEN) != 0) {
        fclose(fp);
        return -1;
    }
    product_id = load_u32_le(raw + 8);
    bin_size = load_u32_le(raw + 12);
    if (expect_product != 0U && product_id != expect_product) {
        fclose(fp);
        return -1;
    }
    if ((long)bin_size + (long)OTA_PKG_TRAILER_SIZE != file_size) {
        fclose(fp);
        return -1;
    }
    if (bin_size < 4U) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(mag, 1, 4, fp) != 4) {
        fclose(fp);
        return -1;
    }
    if (mag[0] != 0x7FU || mag[1] != 'E' || mag[2] != 'L' || mag[3] != 'F') {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (sha256_file_prefix(path, bin_size, calc) != 0) {
        return -1;
    }
    if (memcmp(calc, raw + 16, 32) != 0) {
        return -1;
    }

    if (truncate(path, (off_t)bin_size) != 0) {
        return -1;
    }
    (void)chmod(path, 0755);
    return 0;
}
