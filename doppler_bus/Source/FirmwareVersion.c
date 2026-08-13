#include "FirmwareVersion.h"
#include "Command_proc.h"
#include "WebSeriver.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

#ifndef FW_BUILD_EPOCH
static uint32_t fw_pack_compile_time(void)
{
    const char *d = __DATE__;
    const char *t = __TIME__;
    uint32_t y = 0;
    uint32_t mo = 1;
    uint32_t day = 0;
    uint32_t hh = 0;
    uint32_t mm = 0;
    uint32_t ss = 0;
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i = 0;

    y = (uint32_t)((d[7] - '0') * 1000 + (d[8] - '0') * 100 +
                   (d[9] - '0') * 10 + (d[10] - '0'));
    day = (d[4] == ' ') ? (uint32_t)(d[5] - '0')
                        : (uint32_t)((d[4] - '0') * 10 + (d[5] - '0'));
    for (i = 0; i < 12; i++) {
        if (d[0] == months[i * 3] && d[1] == months[i * 3 + 1] &&
            d[2] == months[i * 3 + 2]) {
            mo = (uint32_t)(i + 1);
            break;
        }
    }
    hh = (uint32_t)((t[0] - '0') * 10 + (t[1] - '0'));
    mm = (uint32_t)((t[3] - '0') * 10 + (t[4] - '0'));
    ss = (uint32_t)((t[6] - '0') * 10 + (t[7] - '0'));
    return ((y - 2020U) << 26) | (mo << 22) | (day << 17) |
           (hh << 12) | (mm << 6) | ss;
}
#endif

static uint32_t fw_version_u32(void)
{
#ifdef FW_BUILD_EPOCH
    return (uint32_t)FW_BUILD_EPOCH;
#else
    return fw_pack_compile_time();
#endif
}

void FirmwareVersion_Get(uint8_t out[4])
{
    uint32_t v;

    if (out == NULL) {
        return;
    }
    v = fw_version_u32();
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void FirmwareVersion_Format(char *buf, size_t bufSize)
{
    if (buf == NULL || bufSize == 0) {
        return;
    }
    snprintf(buf, bufSize, "%u", (unsigned)fw_version_u32());
}

void FirmwareVersion_ReportBoot(void *arg)
{
    uint8_t ver[4];
    uint32_t v = 0;

    (void)arg;
    FirmwareVersion_Get(ver);
    memcpy(&v, ver, sizeof(v));
    LOG(LOG_INFO, "boot VERSION_ID=%u (0x%02x%02x%02x%02x)",
        (unsigned)v, ver[3], ver[2], ver[1], ver[0]);
    (void)SendUpLinkToServer(UP_EXPAND_CODE, (uint8_t)REPORT_VERSION_ID,
                             ver, (uint16_t)sizeof(ver), DTU_TYPE);
}
