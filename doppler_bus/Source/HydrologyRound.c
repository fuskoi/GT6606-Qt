#include "HydrologyRound.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t hydrology_pow10(unsigned n)
{
    static const int64_t table[] = {
        1LL, 10LL, 100LL, 1000LL, 10000LL, 100000LL,
        1000000LL, 10000000LL, 100000000LL, 1000000000LL, 10000000000LL
    };

    if (n > 10U) {
        n = 10U;
    }
    return table[n];
}

static int64_t hydrology_parse_scaled(const char* s, unsigned u8DecimalPlaces)
{
    const char* dot = strchr(s, '.');
    int64_t i64IntPart = 0;
    int64_t i64FracPart = 0;
    unsigned u8Index = 0;
    char acIntBuf[32];
    size_t uIntLen = 0;

    if (dot == NULL) {
        i64IntPart = strtoll(s, NULL, 10);
        return i64IntPart * hydrology_pow10(u8DecimalPlaces);
    }

    uIntLen = (size_t)(dot - s);
    if (uIntLen == 0) {
        i64IntPart = 0;
    } else {
        if (uIntLen >= sizeof(acIntBuf)) {
            uIntLen = sizeof(acIntBuf) - 1U;
        }
        memcpy(acIntBuf, s, uIntLen);
        acIntBuf[uIntLen] = '\0';
        i64IntPart = strtoll(acIntBuf, NULL, 10);
    }

    for (u8Index = 0; u8Index < u8DecimalPlaces; u8Index++) {
        int iDigit = 0;
        if (dot[1U + u8Index] >= '0' && dot[1U + u8Index] <= '9') {
            iDigit = dot[1U + u8Index] - '0';
        }
        i64FracPart = i64FracPart * 10 + iDigit;
    }

    return i64IntPart * hydrology_pow10(u8DecimalPlaces) + i64FracPart;
}

/* DecimalUtil.toString：四舍六入，5 后有数进位，5 后无数奇进偶舍；先对绝对值修约 */
int64_t hydrology_scale_round(double value, unsigned u8DecimalPlaces)
{
    char acBuf[96];
    char* dot = NULL;
    unsigned u8DiscardPos = 0;
    int iDiscardDigit = 0;
    int iLastRetainDigit = 0;
    int iRoundUp = 0;
    size_t uLen = 0;
    char* pTail = NULL;
    int64_t i64Scaled = 0;
    int iSign = 1;

    if (value < 0.0) {
        iSign = -1;
        value = -value;
    }
    /* 与 DecimalUtil 一致：scale 0~5 */
    if (u8DecimalPlaces > 5U) {
        u8DecimalPlaces = 5U;
    }

    snprintf(acBuf, sizeof(acBuf), "%.*f", (int)u8DecimalPlaces + 10, value);
    dot = strchr(acBuf, '.');
    if (dot == NULL) {
        i64Scaled = (int64_t)(value * (double)hydrology_pow10(u8DecimalPlaces));
        return iSign * i64Scaled;
    }

    u8DiscardPos = (unsigned)(dot - acBuf) + 1U + u8DecimalPlaces;
    uLen = strlen(acBuf);
    while (uLen > 0U && acBuf[uLen - 1U] == '0') {
        uLen--;
    }
    acBuf[uLen] = '\0';

    if (u8DiscardPos >= uLen) {
        return iSign * hydrology_parse_scaled(acBuf, u8DecimalPlaces);
    }

    iDiscardDigit = acBuf[u8DiscardPos] - '0';
    if (u8DecimalPlaces == 0U) {
        if (u8DiscardPos > 0U && acBuf[u8DiscardPos - 1U] != '.') {
            iLastRetainDigit = acBuf[u8DiscardPos - 1U] - '0';
        }
    } else {
        iLastRetainDigit = acBuf[u8DiscardPos - 1U] - '0';
    }

    if (iDiscardDigit < 5) {
        iRoundUp = 0;
    } else if (iDiscardDigit > 5) {
        iRoundUp = 1;
    } else {
        iRoundUp = 0;
        for (pTail = acBuf + u8DiscardPos + 1; *pTail != '\0'; pTail++) {
            if (*pTail >= '1' && *pTail <= '9') {
                iRoundUp = 1;
                break;
            }
        }
        if (!iRoundUp && (iLastRetainDigit % 2 != 0)) {
            iRoundUp = 1;
        }
    }

    acBuf[u8DiscardPos] = '\0';
    i64Scaled = hydrology_parse_scaled(acBuf, u8DecimalPlaces);
    if (iRoundUp) {
        i64Scaled += 1;
    }
    return iSign * i64Scaled;
}

int32_t hydrology_scale_round_i32(double value, unsigned u8DecimalPlaces)
{
    int64_t i64Scaled = hydrology_scale_round(value, u8DecimalPlaces);

    if (i64Scaled > (int64_t)INT32_MAX) {
        return INT32_MAX;
    }
    if (i64Scaled < (int64_t)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)i64Scaled;
}

int16_t hydrology_scale_round_i16(double value, unsigned u8DecimalPlaces)
{
    int64_t i64Scaled = hydrology_scale_round(value, u8DecimalPlaces);

    if (i64Scaled > (int64_t)INT16_MAX) {
        return INT16_MAX;
    }
    if (i64Scaled < (int64_t)INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)i64Scaled;
}

double hydrology_round(double value, unsigned u8DecimalPlaces)
{
    static const double adPow10[] = {
        1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0
    };

    if (u8DecimalPlaces > 5U) {
        u8DecimalPlaces = 5U;
    }
    return (double)hydrology_scale_round(value, u8DecimalPlaces) / adPow10[u8DecimalPlaces];
}

static double hydrology_abs_f(double value)
{
    return (value < 0.0) ? -value : value;
}

/* DecimalUtil.toStrWater */
double hydrology_round_water(double value)
{
    return hydrology_round(value, 2U);
}

/* DecimalUtil.toStrArea */
double hydrology_round_area(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a < 10.0f) {
        return hydrology_round(value, 2U);
    }
    if (a < 100.0f) {
        return hydrology_round(value, 1U);
    }
    return hydrology_round(value, 0U);
}

/* DecimalUtil.toStrStartDistance */
double hydrology_round_start_distance(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a >= 100.0f) {
        return hydrology_round(value, 0U);
    }
    return hydrology_round(value, 1U);
}

/* DecimalUtil.toStrWaterDepth */
double hydrology_round_water_depth(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a >= 100.0f) {
        return hydrology_round(value, 0U);
    }
    if (a >= 5.0f) {
        return hydrology_round(value, 1U);
    }
    return hydrology_round(value, 2U);
}

/* DecimalUtil.toStrVelocity */
double hydrology_round_velocity(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a < 1.0f) {
        return hydrology_round(value, 3U);
    }
    return hydrology_round(value, 2U);
}

/* DecimalUtil.toStrWaterSurface */
double hydrology_round_water_surface(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a < 5.0f) {
        return hydrology_round(value, 2U);
    }
    if (a < 100.0f) {
        return hydrology_round(value, 1U);
    }
    return hydrology_round(value, 0U);
}

/* DecimalUtil.toStrFlow */
double hydrology_round_flow(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a < 1.0f) {
        return hydrology_round(value, 3U);
    }
    if (a < 10.0f) {
        return hydrology_round(value, 2U);
    }
    if (a < 100.0f) {
        return hydrology_round(value, 1U);
    }
    return hydrology_round(value, 0U);
}

/* DecimalUtil.toStrRunoff（≥1000 走 0 位） */
double hydrology_round_runoff(double value)
{
    float a = (float)hydrology_abs_f(value);

    if (a < 1.0f) {
        return hydrology_round(value, 4U);
    }
    if (a < 10.0f) {
        return hydrology_round(value, 3U);
    }
    if (a < 100.0f) {
        return hydrology_round(value, 2U);
    }
    if (a < 1000.0f) {
        return hydrology_round(value, 1U);
    }
    return hydrology_round(value, 0U);
}
