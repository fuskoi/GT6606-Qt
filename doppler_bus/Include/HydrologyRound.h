#ifndef HYDROLOGY_ROUND_H
#define HYDROLOGY_ROUND_H

#include <stdint.h>

/*
 * 与网页 DecimalUtil 对齐：
 * - hydrology_round：四舍六入五成双（奇进偶舍），scale 0~5
 * - 下列按量级自适应小数位（见 DecimalUtil.toStr*）
 */
int64_t hydrology_scale_round(double value, unsigned u8DecimalPlaces);
int32_t hydrology_scale_round_i32(double value, unsigned u8DecimalPlaces);
int16_t hydrology_scale_round_i16(double value, unsigned u8DecimalPlaces);
double hydrology_round(double value, unsigned u8DecimalPlaces);

double hydrology_round_water(double value);           /* 水位/高程：固定 2 位 */
double hydrology_round_area(double value);            /* 面积：<10→2, [10,100)→1, ≥100→0 */
double hydrology_round_start_distance(double value);  /* 起点距：≥100→0, 否则 1 */
double hydrology_round_water_depth(double value);     /* 水深：<5→2, [5,100)→1, ≥100→0 */
double hydrology_round_velocity(double value);        /* 流速：<1→3, 否则 2 */
double hydrology_round_water_surface(double value);   /* 水面宽：<5→2, [5,100)→1, ≥100→0 */
double hydrology_round_flow(double value);            /* 流量：<1→3, [1,10)→2, [10,100)→1, ≥100→0 */
double hydrology_round_runoff(double value);          /* 径流量等：按 DecimalUtil.toStrRunoff */

#endif
