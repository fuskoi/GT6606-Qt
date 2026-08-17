#include "SectionParam.h"
#include "Command_proc.h"
#include "ReportAck.h"
#include "WebSeriver.h"
#include "HydrologyRound.h"
#include "OtaUpgrade.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#define SECTION_JSON_BUF_SIZE  (16 * 1024)
#define RUN_JSON_BUF_SIZE      (64 * 1024)

/* 璁＄畻鍓嶆寜 GB/T 8170 鍥涜垗鍏叆浜旀垚鍙屼慨绾?*/
/* 浠呭叆鍙ｅ師濮嬫暟鎹笌鏈€缁堢粨鏋滀慨绾︼紱涓棿鎻掑€?姘存繁/鍒嗘闈㈢Н绛変笉淇害 */
/* 修约规则与网页 DecimalUtil 对齐（见 HydrologyRound.h） */

CrossSectionData g_stCrossSection;
RunSectionData g_stRunSection;
uint8_t g_u8CrossSectionReady = 0;
uint8_t g_u8RunSectionReady = 0;

static int read_le_i32(const uint8_t* pBuf, uint16_t u16Len, uint16_t* pOffset, int32_t* pValue)
{
    if (*pOffset + 4 > u16Len) {
        return ERROR;
    }
    memcpy(pValue, pBuf + *pOffset, sizeof(int32_t));
    *pOffset = (uint16_t)(*pOffset + 4);
    return SUCCESS;
}

static int read_le_i16(const uint8_t* pBuf, uint16_t u16Len, uint16_t* pOffset, int16_t* pValue)
{
    if (*pOffset + 2 > u16Len) {
        return ERROR;
    }
    memcpy(pValue, pBuf + *pOffset, sizeof(int16_t));
    *pOffset = (uint16_t)(*pOffset + 2);
    return SUCCESS;
}

static int read_u8(const uint8_t* pBuf, uint16_t u16Len, uint16_t* pOffset, uint8_t* pValue)
{
    if (*pOffset + 1 > u16Len) {
        return ERROR;
    }
    *pValue = pBuf[*pOffset];
    *pOffset = (uint16_t)(*pOffset + 1);
    return SUCCESS;
}

static int parse_auto_flow_mode(const uint8_t* pBuf, uint16_t u16Len, uint16_t* pOffset, AutoFlowTimeMode* pMode)
{
    int16_t i = 0;
    int16_t timeCount = 0;

    memset(pMode, 0, sizeof(*pMode));
    if (read_le_i32(pBuf, u16Len, pOffset, &pMode->i32Amplitude) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBuf, u16Len, pOffset, &pMode->i32WaterLevel) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i16(pBuf, u16Len, pOffset, &pMode->i16DepthCount) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i16(pBuf, u16Len, pOffset, &timeCount) != SUCCESS) {
        return ERROR;
    }
    if (timeCount < 0 || timeCount > MAX_AUTO_FLOW_TIME_COUNT) {
        return ERROR;
    }
    pMode->i16TimeCount = timeCount;
    for (i = 0; i < timeCount; i++) {
        if (read_le_i16(pBuf, u16Len, pOffset, &pMode->ai16FlowTimes[i]) != SUCCESS) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static int build_data_file_path(const char* fileName, char* outPath, size_t outSize)
{
    if (!GetAppDir(outPath) || outSize < strlen(fileName) + 2) {
        return ERROR;
    }
    strcat(outPath, "/");
    strcat(outPath, fileName);
    return SUCCESS;
}

typedef struct ParamFileHeader
{
    uint32_t u32Magic;
    uint32_t u32Version;
    uint32_t u32BodySize;
} ParamFileHeader;

static int save_struct_file(const char* fileName, uint32_t u32Magic, uint32_t u32Version,
                            const void* pBody, uint32_t u32BodySize)
{
    char filePath[MAX_PATH] = {0};
    ParamFileHeader stHead;
    FILE* fp = NULL;

    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "ota busy, skip save %s", fileName != NULL ? fileName : "?");
        return ERROR;
    }

    if (pBody == NULL || u32BodySize == 0 ||
        build_data_file_path(fileName, filePath, sizeof(filePath)) != SUCCESS) {
        return ERROR;
    }
    fp = fopen(filePath, "wb");
    if (fp == NULL) {
        LOG(LOG_ERROR, "save %s failed", fileName);
        return ERROR;
    }
    stHead.u32Magic = u32Magic;
    stHead.u32Version = u32Version;
    stHead.u32BodySize = u32BodySize;
    if (fwrite(&stHead, sizeof(stHead), 1, fp) != 1 ||
        fwrite(pBody, u32BodySize, 1, fp) != 1) {
        fclose(fp);
        return ERROR;
    }
    fclose(fp);
    return SUCCESS;
}

static int load_struct_file(const char* fileName, uint32_t u32Magic, uint32_t u32Version,
                            void* pBody, uint32_t u32BodySize)
{
    char filePath[MAX_PATH] = {0};
    ParamFileHeader stHead;
    FILE* fp = NULL;

    if (pBody == NULL || u32BodySize == 0 ||
        build_data_file_path(fileName, filePath, sizeof(filePath)) != SUCCESS) {
        return ERROR;
    }
    fp = fopen(filePath, "rb");
    if (fp == NULL) {
        return ERROR;
    }
    if (fread(&stHead, sizeof(stHead), 1, fp) != 1 ||
        stHead.u32Magic != u32Magic ||
        stHead.u32Version != u32Version ||
        stHead.u32BodySize != u32BodySize ||
        fread(pBody, u32BodySize, 1, fp) != 1) {
        fclose(fp);
        return ERROR;
    }
    fclose(fp);
    return SUCCESS;
}

static int is_struct_file_valid(const char* fileName, uint32_t u32Magic, uint32_t u32Version,
                                uint32_t u32BodySize)
{
    char filePath[MAX_PATH] = {0};
    ParamFileHeader stHead;
    FILE* fp = NULL;

    if (build_data_file_path(fileName, filePath, sizeof(filePath)) != SUCCESS) {
        return 0;
    }
    fp = fopen(filePath, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fread(&stHead, sizeof(stHead), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return (stHead.u32Magic == u32Magic &&
            stHead.u32Version == u32Version &&
            stHead.u32BodySize == u32BodySize) ? 1 : 0;
}

static int is_json_data_file_empty(const char* fileName, const char* marker)
{
    char filePath[MAX_PATH] = {0};
    struct stat st;
    FILE* fp = NULL;
    char head[32] = {0};
    size_t readLen = 0;

    if (build_data_file_path(fileName, filePath, sizeof(filePath)) != SUCCESS) {
        return 1;
    }
    if (stat(filePath, &st) != 0 || st.st_size <= 0) {
        return 1;
    }
    fp = fopen(filePath, "rb");
    if (fp == NULL) {
        return 1;
    }
    readLen = fread(head, 1, sizeof(head) - 1, fp);
    fclose(fp);
    if (readLen == 0) {
        return 1;
    }
    head[readLen] = '\0';
    if (strchr(head, '{') == NULL) {
        return 1;
    }
    if (marker != NULL && strstr(head, marker) == NULL) {
        return 1;
    }
    return 0;
}

static int log_cross_section_parsed(const CrossSectionData* pData)
{
    if (pData == NULL) {
        return ERROR;
    }
    LOG(LOG_DEBUG,
        "parse section: id=%d bank=%u bottom=%d water_base=%d origin=%d",
        pData->i32SectionId,
        pData->u8BankFlag,
        pData->i32BottomElevation,
        pData->i32WaterLevelBase,
        pData->i32OriginDistance);
    LOG(LOG_DEBUG,
        "parse section banks start=%u/%u/%u end=%u/%u/%u",
        pData->stStartBank.u8High,
        pData->stStartBank.u8Mid,
        pData->stStartBank.u8Low,
        pData->stEndBank.u8High,
        pData->stEndBank.u8Mid,
        pData->stEndBank.u8Low);
    return SUCCESS;
}

static int log_run_section_parsed(const RunSectionData* pData)
{
    int16_t i = 0;
    int16_t showCount = 0;

    if (pData == NULL) {
        return ERROR;
    }
    LOG(LOG_DEBUG, "parse run section: line_count=%d", pData->i16SpeedLineCount);
    showCount = pData->i16SpeedLineCount;
    if (showCount > 3) {
        showCount = 3;
    }
    for (i = 0; i < showCount; i++) {
        LOG(LOG_DEBUG,
            "parse run line[%d]: dist=%d bed=%d coef=%u/%u/%u",
            i,
            pData->astLines[i].i32StartDistance,
            pData->astLines[i].i32RiverbedElevation,
            pData->astLines[i].u8HighCoef,
            pData->astLines[i].u8MidCoef,
            pData->astLines[i].u8LowCoef);
    }
    return SUCCESS;
}

static int save_text_file(const char* fileName, const char* text)
{
    char filePath[MAX_PATH] = {0};
    FILE* fp = NULL;
    size_t len = 0;

    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "ota busy, skip save %s", fileName != NULL ? fileName : "?");
        return ERROR;
    }
    if (fileName == NULL || text == NULL ||
        build_data_file_path(fileName, filePath, sizeof(filePath)) != SUCCESS) {
        return ERROR;
    }
    len = strlen(text);
    fp = fopen(filePath, "wb");
    if (fp == NULL) {
        LOG(LOG_ERROR, "save %s failed", fileName);
        return ERROR;
    }
    if (fwrite(text, 1, len, fp) != len) {
        fclose(fp);
        return ERROR;
    }
    fclose(fp);
    return SUCCESS;
}

static int append_auto_flow_json(char* buf, size_t bufSize, size_t* pUsed,
                                 const char* keyName, const char* waterKey,
                                 const AutoFlowTimeMode* pMode)
{
    size_t used = 0;
    int n = 0;
    int16_t i = 0;

    if (buf == NULL || pUsed == NULL || keyName == NULL || waterKey == NULL || pMode == NULL) {
        return ERROR;
    }
    used = *pUsed;
    n = snprintf(buf + used, bufSize - used,
                 ",\n  \"%s\": {\n"
                 "    \"变幅值\": %d,\n"
                 "    \"%s\": %d,\n"
                 "    \"测深数量\": %d,\n"
                 "    \"时间个数\": %d,\n"
                 "    \"测流时间\": [",
                 keyName,
                 pMode->i32Amplitude,
                 waterKey,
                 pMode->i32WaterLevel,
                 (int)pMode->i16DepthCount,
                 (int)pMode->i16TimeCount);
    if (n < 0 || (size_t)n >= bufSize - used) {
        return ERROR;
    }
    used += (size_t)n;
    for (i = 0; i < pMode->i16TimeCount; i++) {
        n = snprintf(buf + used, bufSize - used, "%s%d",
                     (i == 0) ? "" : ", ",
                     (int)pMode->ai16FlowTimes[i]);
        if (n < 0 || (size_t)n >= bufSize - used) {
            return ERROR;
        }
        used += (size_t)n;
    }
    n = snprintf(buf + used, bufSize - used, "]\n  }");
    if (n < 0 || (size_t)n >= bufSize - used) {
        return ERROR;
    }
    used += (size_t)n;
    *pUsed = used;
    return SUCCESS;
}

static int save_cross_section_json(const CrossSectionData* pData)
{
    char* buf = NULL;
    size_t used = 0;
    int n = 0;
    int ret = ERROR;

    if (pData == NULL) {
        return ERROR;
    }
    buf = (char*)malloc(SECTION_JSON_BUF_SIZE);
    if (buf == NULL) {
        return ERROR;
    }
    n = snprintf(buf, SECTION_JSON_BUF_SIZE,
                 "{\n"
                 "  \"断面id\": %d,\n"
                 "  \"左右岸标记\": %u,\n"
                 "  \"大底高程\": %d,\n"
                 "  \"断面大底高程（双水位使用）\": %d,\n"
                 "  \"水位基值\": %d,\n"
                 "  \"流速仪安装高度(固定)\": %d,\n"
                 "  \"流速仪水平位置(固定)\": %d,\n"
                 "  \"坐标起点距0点\": %d,\n"
                 "  \"起点岸系数\": { \"高\": %u, \"中\": %u, \"低\": %u },\n"
                 "  \"终点岸系数\": { \"高\": %u, \"中\": %u, \"低\": %u }",
                 pData->i32SectionId,
                 (unsigned)pData->u8BankFlag,
                 pData->i32BottomElevation,
                 pData->i32SectionBottomElevation,
                 pData->i32WaterLevelBase,
                 pData->i32MeterInstallHeight,
                 pData->i32MeterHorizontalPos,
                 pData->i32OriginDistance,
                 (unsigned)pData->stStartBank.u8High,
                 (unsigned)pData->stStartBank.u8Mid,
                 (unsigned)pData->stStartBank.u8Low,
                 (unsigned)pData->stEndBank.u8High,
                 (unsigned)pData->stEndBank.u8Mid,
                 (unsigned)pData->stEndBank.u8Low);
    if (n < 0 || (size_t)n >= SECTION_JSON_BUF_SIZE) {
        free(buf);
        return ERROR;
    }
    used = (size_t)n;
    if (append_auto_flow_json(buf, SECTION_JSON_BUF_SIZE, &used,
                              "自动测流时间高", "水位", &pData->stAutoHigh) != SUCCESS ||
        append_auto_flow_json(buf, SECTION_JSON_BUF_SIZE, &used,
                              "自动测流时间中", "水位", &pData->stAutoMid) != SUCCESS ||
        append_auto_flow_json(buf, SECTION_JSON_BUF_SIZE, &used,
                              "自动测流时间低", "水位（占位）", &pData->stAutoLow) != SUCCESS) {
        free(buf);
        return ERROR;
    }
    n = snprintf(buf + used, SECTION_JSON_BUF_SIZE - used, "\n}\n");
    if (n < 0 || (size_t)n >= SECTION_JSON_BUF_SIZE - used) {
        free(buf);
        return ERROR;
    }
    ret = save_text_file(SHICHAFA_JSON_FILE, buf);
    free(buf);
    return ret;
}

static int save_run_section_json(const RunSectionData* pData)
{
    char* buf = NULL;
    size_t used = 0;
    int n = 0;
    int16_t i = 0;
    int ret = ERROR;

    if (pData == NULL) {
        return ERROR;
    }
    buf = (char*)malloc(RUN_JSON_BUF_SIZE);
    if (buf == NULL) {
        return ERROR;
    }
    n = snprintf(buf, RUN_JSON_BUF_SIZE,
                 "{\n"
                 "  \"测速数量\": %d,\n"
                 "  \"测速数组\": [\n",
                 (int)pData->i16SpeedLineCount);
    if (n < 0 || (size_t)n >= RUN_JSON_BUF_SIZE) {
        free(buf);
        return ERROR;
    }
    used = (size_t)n;
    for (i = 0; i < pData->i16SpeedLineCount; i++) {
        const SpeedLineItem* pLine = &pData->astLines[i];

        n = snprintf(buf + used, RUN_JSON_BUF_SIZE - used,
                     "    { \"起点距\": %d, \"河底高程\": %d, \"高水系数\": %u, \"中水系数\": %u, \"低水系数\": %u }%s\n",
                     pLine->i32StartDistance,
                     pLine->i32RiverbedElevation,
                     (unsigned)pLine->u8HighCoef,
                     (unsigned)pLine->u8MidCoef,
                     (unsigned)pLine->u8LowCoef,
                     (i + 1 < pData->i16SpeedLineCount) ? "," : "");
        if (n < 0 || (size_t)n >= RUN_JSON_BUF_SIZE - used) {
            free(buf);
            return ERROR;
        }
        used += (size_t)n;
    }
    n = snprintf(buf + used, RUN_JSON_BUF_SIZE - used, "  ]\n}\n");
    if (n < 0 || (size_t)n >= RUN_JSON_BUF_SIZE - used) {
        free(buf);
        return ERROR;
    }
    ret = save_text_file(YUNXING_JSON_FILE, buf);
    free(buf);
    return ret;
}

static int save_cross_section_file(const CrossSectionData* pData)
{
    if (pData == NULL) {
        return ERROR;
    }
    if (save_struct_file(SHICHAFA_DATA_FILE,
                         SECTION_FILE_MAGIC,
                         SECTION_FILE_VER,
                         pData,
                         (uint32_t)sizeof(CrossSectionData)) != SUCCESS) {
        return ERROR;
    }
    if (save_cross_section_json(pData) != SUCCESS) {
        LOG(LOG_WARNING, "save %s failed (binary ok)", SHICHAFA_JSON_FILE);
    }
    return SUCCESS;
}

static int save_run_section_file(const RunSectionData* pData)
{
    if (pData == NULL) {
        return ERROR;
    }
    if (save_struct_file(YUNXING_DATA_FILE,
                         RUN_FILE_MAGIC,
                         RUN_FILE_VER,
                         pData,
                         (uint32_t)sizeof(RunSectionData)) != SUCCESS) {
        return ERROR;
    }
    if (save_run_section_json(pData) != SUCCESS) {
        LOG(LOG_WARNING, "save %s failed (binary ok)", YUNXING_JSON_FILE);
    }
    return SUCCESS;
}

static int read_text_file(const char* filePath, char** ppBuf, size_t* pLen)
{
    struct stat st;
    FILE* fp = NULL;
    char* buf = NULL;
    size_t readLen = 0;

    if (ppBuf == NULL || pLen == NULL) {
        return ERROR;
    }
    *ppBuf = NULL;
    *pLen = 0;
    if (stat(filePath, &st) != 0 || st.st_size <= 0) {
        return ERROR;
    }
    buf = (char*)malloc((size_t)st.st_size + 1U);
    if (buf == NULL) {
        return ERROR;
    }
    fp = fopen(filePath, "rb");
    if (fp == NULL) {
        free(buf);
        return ERROR;
    }
    readLen = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (readLen == 0) {
        free(buf);
        return ERROR;
    }
    buf[readLen] = '\0';
    *ppBuf = buf;
    *pLen = readLen;
    return SUCCESS;
}

static int json_get_int32(const char* json, const char* key, int32_t* pValue)
{
    char pattern[96] = {0};
    const char* p = NULL;

    if (json == NULL || key == NULL || pValue == NULL) {
        return ERROR;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (p == NULL) {
        return ERROR;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    *pValue = (int32_t)atoi(p);
    return SUCCESS;
}

static int json_get_uint8(const char* json, const char* key, uint8_t* pValue)
{
    int32_t value = 0;
    if (json_get_int32(json, key, &value) != SUCCESS) {
        return ERROR;
    }
    *pValue = (uint8_t)value;
    return SUCCESS;
}

static int json_get_bank_coef(const char* json, const char* blockName, BankCoef3* pBank)
{
    char pattern[96] = {0};
    const char* p = NULL;

    snprintf(pattern, sizeof(pattern), "\"%s\":", blockName);
    p = strstr(json, pattern);
    if (p == NULL) {
        return ERROR;
    }
    p = strchr(p, '{');
    if (p == NULL) {
        return ERROR;
    }
    if (sscanf(p, "{ \"高\": %hhu, \"中\": %hhu, \"低\": %hhu }",
               &pBank->u8High, &pBank->u8Mid, &pBank->u8Low) != 3) {
        return ERROR;
    }
    return SUCCESS;
}

static int load_cross_section_file(void)
{
    if (load_struct_file(SHICHAFA_DATA_FILE,
                         SECTION_FILE_MAGIC,
                         SECTION_FILE_VER,
                         &g_stCrossSection,
                         (uint32_t)sizeof(g_stCrossSection)) == SUCCESS) {
        g_u8CrossSectionReady = 1;
        return SUCCESS;
    }

    /* 兼容旧版 JSON 文件，收到网页下发后会自动改写为二进制 */
    {
        char filePath[MAX_PATH] = {0};
        char* json = NULL;
        size_t jsonLen = 0;

        if (build_data_file_path(SHICHAFA_DATA_FILE, filePath, sizeof(filePath)) != SUCCESS) {
            return ERROR;
        }
        if (read_text_file(filePath, &json, &jsonLen) != SUCCESS) {
            return ERROR;
        }
        if (json[0] != '{') {
            free(json);
            return ERROR;
        }

        memset(&g_stCrossSection, 0, sizeof(g_stCrossSection));
        if (json_get_int32(json, "断面id", &g_stCrossSection.i32SectionId) != SUCCESS ||
            json_get_uint8(json, "左右岸标记", &g_stCrossSection.u8BankFlag) != SUCCESS ||
            json_get_int32(json, "大底高程", &g_stCrossSection.i32BottomElevation) != SUCCESS ||
            json_get_int32(json, "断面大底高程（双水位使用）", &g_stCrossSection.i32SectionBottomElevation) != SUCCESS ||
            json_get_int32(json, "水位基值", &g_stCrossSection.i32WaterLevelBase) != SUCCESS ||
            json_get_int32(json, "流速仪安装高度(固定)", &g_stCrossSection.i32MeterInstallHeight) != SUCCESS ||
            json_get_int32(json, "流速仪水平位置(固定)", &g_stCrossSection.i32MeterHorizontalPos) != SUCCESS ||
            json_get_int32(json, "坐标起点距0点", &g_stCrossSection.i32OriginDistance) != SUCCESS ||
            json_get_bank_coef(json, "起点岸系数", &g_stCrossSection.stStartBank) != SUCCESS ||
            json_get_bank_coef(json, "终点岸系数", &g_stCrossSection.stEndBank) != SUCCESS) {
            free(json);
            return ERROR;
        }
        free(json);
        LOG(LOG_WARNING, "loaded legacy json %s, auto-flow thresholds may be zero until next downlink",
            SHICHAFA_DATA_FILE);
        g_u8CrossSectionReady = 1;
        return SUCCESS;
    }
}

static int load_run_section_file(void)
{
    if (load_struct_file(YUNXING_DATA_FILE,
                         RUN_FILE_MAGIC,
                         RUN_FILE_VER,
                         &g_stRunSection,
                         (uint32_t)sizeof(g_stRunSection)) == SUCCESS) {
        g_u8RunSectionReady = 1;
        return SUCCESS;
    }

    /* 兼容旧版 JSON 文件 */
    {
        char filePath[MAX_PATH] = {0};
        char* json = NULL;
        size_t jsonLen = 0;
        const char* p = NULL;
        int16_t i = 0;
        int32_t lineCount = 0;

        if (build_data_file_path(YUNXING_DATA_FILE, filePath, sizeof(filePath)) != SUCCESS) {
            return ERROR;
        }
        if (read_text_file(filePath, &json, &jsonLen) != SUCCESS) {
            return ERROR;
        }
        if (json[0] != '{') {
            free(json);
            return ERROR;
        }

        memset(&g_stRunSection, 0, sizeof(g_stRunSection));
        if (json_get_int32(json, "测速数量", &lineCount) != SUCCESS) {
            free(json);
            return ERROR;
        }
        if (lineCount < 0 || lineCount > MAX_SPEED_LINE_COUNT) {
            free(json);
            return ERROR;
        }
        g_stRunSection.i16SpeedLineCount = (int16_t)lineCount;

        p = strstr(json, "\"测速数组\"");
        if (p == NULL) {
            free(json);
            return ERROR;
        }
        p = strchr(p, '{');
        for (i = 0; i < g_stRunSection.i16SpeedLineCount; i++) {
            SpeedLineItem* pLine = &g_stRunSection.astLines[i];
            if (p == NULL) {
                free(json);
                return ERROR;
            }
            if (sscanf(p,
                       "{ \"起点距\": %d, \"河底高程\": %d, \"高水系数\": %hhu, \"中水系数\": %hhu, \"低水系数\": %hhu }",
                       &pLine->i32StartDistance,
                       &pLine->i32RiverbedElevation,
                       &pLine->u8HighCoef,
                       &pLine->u8MidCoef,
                       &pLine->u8LowCoef) != 5) {
                free(json);
                return ERROR;
            }
            p = strchr(p + 1, '{');
        }
        free(json);
        g_u8RunSectionReady = 1;
        return SUCCESS;
    }
}

static int parse_cross_section_body(const uint8_t* pBody, uint16_t u16BodyLen, CrossSectionData* pOut)
{
    uint16_t offset = 0;

    if (pBody == NULL || pOut == NULL) {
        return ERROR;
    }
    memset(pOut, 0, sizeof(*pOut));
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32SectionId) != SUCCESS) {
        return ERROR;
    }
    if (read_u8(pBody, u16BodyLen, &offset, &pOut->u8BankFlag) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32BottomElevation) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32SectionBottomElevation) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32WaterLevelBase) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32MeterInstallHeight) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32MeterHorizontalPos) != SUCCESS) {
        return ERROR;
    }
    if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->i32OriginDistance) != SUCCESS) {
        return ERROR;
    }
    if (read_u8(pBody, u16BodyLen, &offset, &pOut->stStartBank.u8High) != SUCCESS ||
        read_u8(pBody, u16BodyLen, &offset, &pOut->stStartBank.u8Mid) != SUCCESS ||
        read_u8(pBody, u16BodyLen, &offset, &pOut->stStartBank.u8Low) != SUCCESS ||
        read_u8(pBody, u16BodyLen, &offset, &pOut->stEndBank.u8High) != SUCCESS ||
        read_u8(pBody, u16BodyLen, &offset, &pOut->stEndBank.u8Mid) != SUCCESS ||
        read_u8(pBody, u16BodyLen, &offset, &pOut->stEndBank.u8Low) != SUCCESS) {
        return ERROR;
    }
    if (parse_auto_flow_mode(pBody, u16BodyLen, &offset, &pOut->stAutoHigh) != SUCCESS ||
        parse_auto_flow_mode(pBody, u16BodyLen, &offset, &pOut->stAutoMid) != SUCCESS ||
        parse_auto_flow_mode(pBody, u16BodyLen, &offset, &pOut->stAutoLow) != SUCCESS) {
        return ERROR;
    }
    return SUCCESS;
}

static int parse_run_section_body(const uint8_t* pBody, uint16_t u16BodyLen, RunSectionData* pOut)
{
    uint16_t offset = 0;
    int16_t lineCount = 0;
    int16_t i = 0;

    if (pBody == NULL || pOut == NULL) {
        return ERROR;
    }
    memset(pOut, 0, sizeof(*pOut));
    if (read_le_i16(pBody, u16BodyLen, &offset, &lineCount) != SUCCESS) {
        return ERROR;
    }
    if (lineCount < 0 || lineCount > MAX_SPEED_LINE_COUNT) {
        return ERROR;
    }
    pOut->i16SpeedLineCount = lineCount;
    for (i = 0; i < lineCount; i++) {
        if (read_le_i32(pBody, u16BodyLen, &offset, &pOut->astLines[i].i32StartDistance) != SUCCESS ||
            read_le_i32(pBody, u16BodyLen, &offset, &pOut->astLines[i].i32RiverbedElevation) != SUCCESS ||
            read_u8(pBody, u16BodyLen, &offset, &pOut->astLines[i].u8HighCoef) != SUCCESS ||
            read_u8(pBody, u16BodyLen, &offset, &pOut->astLines[i].u8MidCoef) != SUCCESS ||
            read_u8(pBody, u16BodyLen, &offset, &pOut->astLines[i].u8LowCoef) != SUCCESS ||
            read_u8(pBody, u16BodyLen, &offset, &pOut->astLines[i].u8Reserved) != SUCCESS) {
            return ERROR;
        }
    }
    return SUCCESS;
}

int SectionParam_IsCrossSectionFileEmpty(void)
{
    if (is_struct_file_valid(SHICHAFA_DATA_FILE,
                             SECTION_FILE_MAGIC,
                             SECTION_FILE_VER,
                             (uint32_t)sizeof(CrossSectionData))) {
        return 0;
    }
    return is_json_data_file_empty(SHICHAFA_DATA_FILE, "断面id");
}

int SectionParam_IsRunSectionFileEmpty(void)
{
    if (is_struct_file_valid(YUNXING_DATA_FILE,
                             RUN_FILE_MAGIC,
                             RUN_FILE_VER,
                             (uint32_t)sizeof(RunSectionData))) {
        return 0;
    }
    return is_json_data_file_empty(YUNXING_DATA_FILE, "测速数量");
}

void SectionParam_ReportSectionId(void)
{
    /* 断面号来自已下发的断面文件；尚无断面时上报 0，由平台再下发 100/101 */
    int32_t i32ReportId = 0;

    if (g_u8CrossSectionReady) {
        i32ReportId = g_stCrossSection.i32SectionId;
    }

    LOG(LOG_DEBUG, "report section id=%d (%s)",
        i32ReportId,
        SectionParam_IsCrossSectionFileEmpty() ? "data empty" : SHICHAFA_DATA_FILE);
    ReportAck_Send((uint8_t)REPORT_DUANMIAN_ID,
                   (uint8_t*)&i32ReportId, (uint16_t)sizeof(i32ReportId));
}

int SectionParam_HandleDownlink(uint8_t u8Command, const uint8_t* pBody, uint16_t u16BodyLen)
{
    CrossSectionData stCross;
    RunSectionData stRun;

    if (pBody == NULL || u16BodyLen == 0) {
        return ERROR;
    }

    switch (u8Command) {
    case SERVER_CROSS_SECTION_NEW:
        if (parse_cross_section_body(pBody, u16BodyLen, &stCross) != SUCCESS) {
            LOG(LOG_ERROR, "parse CROSS_SECTION_NEW failed, len=%u", u16BodyLen);
            return ERROR;
        }
        log_cross_section_parsed(&stCross);
        g_stCrossSection = stCross;
        g_u8CrossSectionReady = 1;
        if (save_cross_section_file(&stCross) != SUCCESS) {
            return ERROR;
        }
        LOG(LOG_DEBUG, "saved section binary+json: %s / %s", SHICHAFA_DATA_FILE, SHICHAFA_JSON_FILE);
        ReportAck_OnDownlinkCmd(u8Command);
        return SUCCESS;

    case SERVER_RUN_SECTION_NEW:
        if (parse_run_section_body(pBody, u16BodyLen, &stRun) != SUCCESS) {
            LOG(LOG_ERROR, "parse RUN_SECTION_NEW failed, len=%u", u16BodyLen);
            return ERROR;
        }
        log_run_section_parsed(&stRun);
        g_stRunSection = stRun;
        g_u8RunSectionReady = 1;
        if (save_run_section_file(&stRun) != SUCCESS) {
            return ERROR;
        }
        LOG(LOG_DEBUG, "saved run section binary+json: %s / %s", YUNXING_DATA_FILE, YUNXING_JSON_FILE);
        ReportAck_OnDownlinkCmd(u8Command);
        return SUCCESS;

    default:
        return ERROR;
    }
}

uint8_t SectionParam_GetWaterMode(int32_t i32WaterScaled)
{
    if (!g_u8CrossSectionReady) {
        return 0;
    }
    if (i32WaterScaled >= g_stCrossSection.stAutoHigh.i32WaterLevel) {
        return 2;
    }
    if (i32WaterScaled >= g_stCrossSection.stAutoMid.i32WaterLevel) {
        return 1;
    }
    return 0;
}

const CrossSectionData* SectionParam_GetCrossSection(void)
{
    return g_u8CrossSectionReady ? &g_stCrossSection : NULL;
}

const RunSectionData* SectionParam_GetRunSection(void)
{
    return g_u8RunSectionReady ? &g_stRunSection : NULL;
}

static int section_compare_double(const void* a, const void* b)
{
    const double da = *(const double*)a;
    const double db = *(const double*)b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static double section_scaled_to_meter(int32_t i32Scaled)
{
    return (double)i32Scaled / 100.0;
}

static int section_sort_profile(double* ax, double* az, int16_t* aiLineIdx, int16_t n)
{
    int16_t i = 0;

    for (i = 1; i < n; i++) {
        double tx = ax[i];
        double tz = az[i];
        int16_t ti = (aiLineIdx != NULL) ? aiLineIdx[i] : 0;
        int16_t k = i;

        while (k > 0 && ax[k - 1] > tx) {
            ax[k] = ax[k - 1];
            az[k] = az[k - 1];
            if (aiLineIdx != NULL) {
                aiLineIdx[k] = aiLineIdx[k - 1];
            }
            k--;
        }
        ax[k] = tx;
        az[k] = tz;
        if (aiLineIdx != NULL) {
            aiLineIdx[k] = ti;
        }
    }
    for (i = 1; i < n; i++) {
        if (ax[i] <= ax[i - 1]) {
            return ERROR;
        }
    }
    return SUCCESS;
}

static int section_build_geometry_profile(const RunSectionData* pRun, double* ax, double* az, int16_t* pn)
{
    int16_t nSrc = 0;
    int16_t n = 0;
    int16_t i = 0;

    if (pRun == NULL || ax == NULL || az == NULL || pn == NULL) {
        return ERROR;
    }
    nSrc = pRun->i16SpeedLineCount;
    for (i = 0; i < nSrc; i++) {
        const SpeedLineItem* pLine = &pRun->astLines[i];

        ax[n] = hydrology_round_start_distance(section_scaled_to_meter(pLine->i32StartDistance));
        az[n] = hydrology_round_water(section_scaled_to_meter(pLine->i32RiverbedElevation));
        n++;
    }
    if (n < 2) {
        return ERROR;
    }
    if (section_sort_profile(ax, az, NULL, n) != SUCCESS) {
        return ERROR;
    }
    *pn = n;
    return SUCCESS;
}

static double section_interp_z_on_profile(const double* ax, const double* az, int16_t n, double fX)
{
    int16_t i = 0;

    if (n <= 0) {
        return 0.0;
    }
    if (fX <= ax[0]) {
        return az[0];
    }
    if (fX >= ax[n - 1]) {
        return az[n - 1];
    }
    for (i = 0; i < n - 1; i++) {
        if (fX >= ax[i] && fX <= ax[i + 1]) {
            double t = (fX - ax[i]) / (ax[i + 1] - ax[i]);
            return az[i] + t * (az[i + 1] - az[i]);
        }
    }
    return az[n - 1];
}

/* 河底以上、顶高程以下的过水面积（分段梯形；过程量按 DecimalUtil） */
static double section_calc_strip_area_below(const double* ax, const double* az, int16_t n,
                                            double fXLeft, double fXRight, double fTopElev)
{
    double afX[MAX_SPEED_LINE_COUNT + 2];
    int16_t nPts = 0;
    int16_t i = 0;
    double fArea = 0.0;

    fXLeft = hydrology_round_water(fXLeft);
    fXRight = hydrology_round_water(fXRight);
    fTopElev = hydrology_round_water(fTopElev);
    if (fXRight <= fXLeft + 1e-9) {
        return 0.0;
    }

    afX[nPts++] = fXLeft;
    for (i = 0; i < n; i++) {
        double xi = hydrology_round_start_distance(ax[i]);

        if (xi > fXLeft + 1e-9 && xi < fXRight - 1e-9) {
            afX[nPts++] = xi;
        }
    }
    afX[nPts++] = fXRight;

    for (i = 0; i < nPts - 1; i++) {
        double x0 = afX[i];
        double x1 = afX[i + 1];
        double dx = hydrology_round_water_depth(x1 - x0);
        double z0 = hydrology_round_water(section_interp_z_on_profile(ax, az, n, x0));
        double z1 = hydrology_round_water(section_interp_z_on_profile(ax, az, n, x1));
        double d0 = hydrology_round_water_depth(fTopElev - z0);
        double d1 = hydrology_round_water_depth(fTopElev - z1);

        if (d0 < 0.0) {
            d0 = 0.0;
        }
        if (d1 < 0.0) {
            d1 = 0.0;
        }
        if (d0 <= 0.0 && d1 <= 0.0) {
            continue;
        }
        /* 段面积由已修约的 d、dx 算出；累加后分区结果再修约 */
        fArea += (d0 + d1) * 0.5 * dx;
    }
    return fArea;
}

/* 高程带 [fBottomElev, fTopElev] 内、且位于河底之上的过水面积（上下区各自积分用） */
static double section_calc_strip_area_layer(const double* ax, const double* az, int16_t n,
                                            double fXLeft, double fXRight,
                                            double fBottomElev, double fTopElev)
{
    double afX[MAX_SPEED_LINE_COUNT + 2];
    int16_t nPts = 0;
    int16_t i = 0;
    double fArea = 0.0;

    fXLeft = hydrology_round_water(fXLeft);
    fXRight = hydrology_round_water(fXRight);
    fBottomElev = hydrology_round_water(fBottomElev);
    fTopElev = hydrology_round_water(fTopElev);
    if (fXRight <= fXLeft + 1e-9 || fTopElev <= fBottomElev + 1e-9) {
        return 0.0;
    }

    afX[nPts++] = fXLeft;
    for (i = 0; i < n; i++) {
        double xi = hydrology_round_start_distance(ax[i]);

        if (xi > fXLeft + 1e-9 && xi < fXRight - 1e-9) {
            afX[nPts++] = xi;
        }
    }
    afX[nPts++] = fXRight;

    for (i = 0; i < nPts - 1; i++) {
        double x0 = afX[i];
        double x1 = afX[i + 1];
        double dx = hydrology_round_water_depth(x1 - x0);
        double z0 = hydrology_round_water(section_interp_z_on_profile(ax, az, n, x0));
        double z1 = hydrology_round_water(section_interp_z_on_profile(ax, az, n, x1));
        double base0 = hydrology_round_water((z0 > fBottomElev) ? z0 : fBottomElev);
        double base1 = hydrology_round_water((z1 > fBottomElev) ? z1 : fBottomElev);
        double d0 = hydrology_round_water_depth(fTopElev - base0);
        double d1 = hydrology_round_water_depth(fTopElev - base1);

        if (d0 < 0.0) {
            d0 = 0.0;
        }
        if (d1 < 0.0) {
            d1 = 0.0;
        }
        if (d0 <= 0.0 && d1 <= 0.0) {
            continue;
        }
        fArea += (d0 + d1) * 0.5 * dx;
    }
    return fArea;
}

static int section_water_edges_at_elevation(const double* ax, const double* az, int16_t n, double fWater,
                                            double* pfLeft, double* pfRight);

static int section_collect_crossings_at_elevation(const double* ax, const double* az, int16_t n,
                                                  double fElev, double* afCross, int* piCrossCount,
                                                  int iCrossMax)
{
    int16_t i = 0;
    int iCrossCount = 0;

    if (ax == NULL || az == NULL || afCross == NULL || piCrossCount == NULL || n < 2) {
        return ERROR;
    }

    for (i = 0; i < n - 1; i++) {
        double x0 = ax[i];
        double x1 = ax[i + 1];
        double z0 = az[i];
        double z1 = az[i + 1];

        if (fabs(z0 - fElev) < 1e-6 && iCrossCount < iCrossMax) {
            afCross[iCrossCount++] = x0;
        }
        if ((z0 - fElev) * (z1 - fElev) <= 0.0 && fabs(z1 - z0) > 1e-9) {
            double t = (fElev - z0) / (z1 - z0);
            double xc = x0 + t * (x1 - x0);
            if (iCrossCount < iCrossMax) {
                afCross[iCrossCount++] = xc;
            }
        }
    }
    if (fabs(az[n - 1] - fElev) < 1e-6 && iCrossCount < iCrossMax) {
        afCross[iCrossCount++] = ax[n - 1];
    }

    *piCrossCount = iCrossCount;
    return SUCCESS;
}

static int section_profile_fully_submerged(const double* az, int16_t n, double fWater)
{
    int16_t i = 0;

    for (i = 0; i < n; i++) {
        if (az[i] >= fWater) {
            return 0;
        }
    }
    return 1;
}

static int section_water_edges_at_elevation(const double* ax, const double* az, int16_t n, double fWater,
                                            double* pfLeft, double* pfRight)
{
    double afCross[MAX_SPEED_LINE_COUNT + 2];
    int iCrossCount = 0;

    if (pfLeft == NULL || pfRight == NULL || n < 2) {
        return ERROR;
    }

    if (section_collect_crossings_at_elevation(ax, az, n, fWater, afCross, &iCrossCount,
                                               (int)(sizeof(afCross) / sizeof(afCross[0]))) != SUCCESS) {
        return ERROR;
    }

    if (iCrossCount >= 2) {
        qsort(afCross, (size_t)iCrossCount, sizeof(double), section_compare_double);
        *pfLeft = hydrology_round_water(afCross[0]);
        *pfRight = hydrology_round_water(afCross[iCrossCount - 1]);
        return SUCCESS;
    }

    if (section_profile_fully_submerged(az, n, fWater)) {
        *pfLeft = hydrology_round_water(ax[0]);
        *pfRight = hydrology_round_water(ax[n - 1]);
        return SUCCESS;
    }

    return ERROR;
}

int SectionParam_CalcWettedArea(int32_t i32WaterScaled, SectionWettedArea* pOut)
{
    double axGeom[MAX_SPEED_LINE_COUNT];
    double azGeom[MAX_SPEED_LINE_COUNT];
    int16_t nGeom = 0;
    double fWater = 0.0;
    double fWaterLeft = 0.0;
    double fWaterRight = 0.0;
    double fTotal = 0.0;
    double fBedMin = 0.0;
    double fBedMax = 0.0;
    int16_t j = 0;

    if (pOut == NULL) {
        return ERROR;
    }
    memset(pOut, 0, sizeof(*pOut));
    if (!g_u8CrossSectionReady || !g_u8RunSectionReady) {
        return ERROR;
    }

    if (section_build_geometry_profile(&g_stRunSection, axGeom, azGeom, &nGeom) != SUCCESS) {
        LOG(LOG_WARNING, "wetted area: geometry profile invalid");
        return ERROR;
    }

    /* 鍏ュ彛鍘熷姘翠綅淇害锛涙按杈?鎻掑€?鍒嗘闈㈢Н涓棿杩囩▼涓嶄慨绾?*/
    fWater = hydrology_round_water(section_scaled_to_meter(i32WaterScaled));
    pOut->fWaterElevation = fWater;
    if (section_water_edges_at_elevation(axGeom, azGeom, nGeom, fWater, &fWaterLeft, &fWaterRight) !=
        SUCCESS) {
        fWaterLeft = axGeom[0];
        fWaterRight = axGeom[nGeom - 1];
    }

    fBedMin = azGeom[0];
    fBedMax = azGeom[0];
    for (j = 1; j < nGeom; j++) {
        if (azGeom[j] < fBedMin) {
            fBedMin = azGeom[j];
        }
        if (azGeom[j] > fBedMax) {
            fBedMax = azGeom[j];
        }
    }
    LOG(LOG_DEBUG,
        "wetted area profile: mode=%u geomLines=%d water=%.2f waterEdge=[%.2f,%.2f] wettedSpan=%.2f bed=[%.2f,%.2f]",
        (unsigned)SectionParam_GetWaterMode(i32WaterScaled),
        (int)nGeom,
        fWater,
        fWaterLeft,
        fWaterRight,
        fWaterRight - fWaterLeft,
        fBedMin,
        fBedMax);

    fTotal = section_calc_strip_area_below(axGeom, azGeom, nGeom, fWaterLeft, fWaterRight, fWater);
    /* 缁撴灉淇害锛氬搴︼紱闈㈢Н鍏堜繚鐣欐湭淇害鍊间緵涓婁笅鍒嗗尯绠?Q */
    pOut->fWaterLeft = fWaterLeft;
    pOut->fWaterRight = fWaterRight;
    pOut->fWidthAtWater = hydrology_round_water_surface(fWaterRight - fWaterLeft);
    pOut->fTotalArea = fTotal;
    pOut->fAreaUpper = 0.0;
    pOut->fAreaLower = fTotal;
    pOut->fSplitElev = fWater;

    LOG(LOG_DEBUG,
        "wetted area result: totalRaw=%.6f widthWater=%.2f",
        pOut->fTotalArea,
        pOut->fWidthAtWater);
    return SUCCESS;
}

static uint8_t section_line_mode_coef(const SpeedLineItem* pLine, uint8_t u8Mode)
{
    if (pLine == NULL) {
        return 0;
    }
    if (u8Mode == 2U) {
        return pLine->u8HighCoef;
    }
    if (u8Mode == 1U) {
        return pLine->u8MidCoef;
    }
    return pLine->u8LowCoef;
}

/* 当前水位模式下有效测线，按河底高程从高到低排序后：
 * 数组[0]=上测线几何，[1]=下测线几何（与传感器编号无关，流速另按 1=下/2=上绑定）
 */
static int section_pick_active_lines_by_bed(uint8_t u8Mode, int16_t* piOutIdx, int16_t* piOutCount)
{
    int16_t i = 0;
    int16_t nActive = 0;
    int16_t aiIdx[MAX_SPEED_LINE_COUNT];
    int32_t aiBed[MAX_SPEED_LINE_COUNT];

    if (piOutIdx == NULL || piOutCount == NULL || !g_u8RunSectionReady) {
        return ERROR;
    }

    memset(aiIdx, 0, sizeof(aiIdx));
    memset(aiBed, 0, sizeof(aiBed));
    for (i = 0; i < g_stRunSection.i16SpeedLineCount; i++) {
        const SpeedLineItem* pLine = &g_stRunSection.astLines[i];
        if (section_line_mode_coef(pLine, u8Mode) == 0U) {
            continue;
        }
        if (nActive >= MAX_SPEED_LINE_COUNT) {
            break;
        }
        aiIdx[nActive] = i;
        aiBed[nActive] = pLine->i32RiverbedElevation;
        nActive++;
    }

    /* 娌冲簳楂樼▼闄嶅簭锛堜笂娴嬬嚎鍦ㄥ墠锛?*/
    for (i = 1; i < nActive; i++) {
        int16_t k = i;
        int16_t tIdx = aiIdx[i];
        int32_t tBed = aiBed[i];
        while (k > 0 && aiBed[k - 1] < tBed) {
            aiIdx[k] = aiIdx[k - 1];
            aiBed[k] = aiBed[k - 1];
            k--;
        }
        aiIdx[k] = tIdx;
        aiBed[k] = tBed;
    }

    /* 鏈€澶氱粦瀹?2 鍙颁紶鎰熷櫒 */
    if (nActive > 2) {
        nActive = 2;
    }
    for (i = 0; i < nActive; i++) {
        piOutIdx[i] = aiIdx[i];
    }
    *piOutCount = nActive;
    return SUCCESS;
}

int SectionParam_CalcTotalDischarge(int32_t i32WaterScaled,
                                    const float* pfSensorVelocities,
                                    const uint8_t* pu8SensorValid,
                                    uint8_t u8SensorMax,
                                    double* pDischargeM3s,
                                    SectionWettedArea* pAreaOut)
{
    SectionWettedArea stArea;
    double axGeom[MAX_SPEED_LINE_COUNT];
    double azGeom[MAX_SPEED_LINE_COUNT];
    int16_t nGeom = 0;
    uint8_t u8Mode = 0;
    int16_t aiActiveIdx[2];
    int16_t i16ActiveCount = 0;
    double fBedUpper = 0.0;
    double fBedLower = 0.0;
    double fSplit = 0.0;
    double fTopLower = 0.0;
    double fAreaLower = 0.0;
    double fAreaUpper = 0.0;
    float fVelUpper = 0.0f;
    float fVelLower = 0.0f;
    double fQUpper = 0.0;
    double fQLower = 0.0;
    int hasUpperVel = 0;
    int hasLowerVel = 0;

    if (pDischargeM3s == NULL || pfSensorVelocities == NULL || pu8SensorValid == NULL ||
        u8SensorMax == 0) {
        return ERROR;
    }
    if (!g_u8CrossSectionReady || !g_u8RunSectionReady) {
        return ERROR;
    }
    if (SectionParam_CalcWettedArea(i32WaterScaled, &stArea) != SUCCESS) {
        return ERROR;
    }
    if (section_build_geometry_profile(&g_stRunSection, axGeom, azGeom, &nGeom) != SUCCESS) {
        return ERROR;
    }

    u8Mode = SectionParam_GetWaterMode(i32WaterScaled);
    memset(aiActiveIdx, 0, sizeof(aiActiveIdx));
    if (section_pick_active_lines_by_bed(u8Mode, aiActiveIdx, &i16ActiveCount) != SUCCESS ||
        i16ActiveCount <= 0) {
        LOG(LOG_WARNING, "total discharge: no active speed lines for mode=%u", (unsigned)u8Mode);
        return ERROR;
    }

    /* 现场从下往上编号：sensor1=下测线，sensor2=上测线 */
    if (u8SensorMax >= 1 && pu8SensorValid[0]) {
        fVelLower = pfSensorVelocities[0];
        if (fVelLower < 0.0f) {
            fVelLower = 0.0f;
        }
        fVelLower = (float)hydrology_round_velocity((double)fVelLower);
        hasLowerVel = 1;
    }
    if (u8SensorMax >= 2 && pu8SensorValid[1]) {
        fVelUpper = pfSensorVelocities[1];
        if (fVelUpper < 0.0f) {
            fVelUpper = 0.0f;
        }
        fVelUpper = (float)hydrology_round_velocity((double)fVelUpper);
        hasUpperVel = 1;
    }

    if (i16ActiveCount >= 2) {
        fBedUpper = hydrology_round_water(
            section_scaled_to_meter(g_stRunSection.astLines[aiActiveIdx[0]].i32RiverbedElevation));
        fBedLower = hydrology_round_water(
            section_scaled_to_meter(g_stRunSection.astLines[aiActiveIdx[1]].i32RiverbedElevation));
        /* Al/Au each integrate the strip; A = Au + Al (not A - Al) */
        fSplit = hydrology_round_water((fBedUpper + fBedLower) * 0.5);
        fTopLower = (stArea.fWaterElevation < fSplit) ? stArea.fWaterElevation : fSplit;

        if (stArea.fWaterElevation <= fBedUpper) {
            LOG(LOG_DEBUG,
                "total discharge: upper zone skipped, water=%.2f <= upperBed=%.2f",
                stArea.fWaterElevation,
                fBedUpper);
            fAreaUpper = 0.0;
            fAreaLower = section_calc_strip_area_below(axGeom, azGeom, nGeom, stArea.fWaterLeft,
                                                       stArea.fWaterRight, stArea.fWaterElevation);
        } else {
            double fLowerLeft = stArea.fWaterLeft;
            double fLowerRight = stArea.fWaterRight;

            /* 下区水边：河床 ∩ split（与同事一致）；上区仍用水位水边 */
            if (section_water_edges_at_elevation(axGeom, azGeom, nGeom, fTopLower,
                                                 &fLowerLeft, &fLowerRight) != SUCCESS) {
                fLowerLeft = stArea.fWaterLeft;
                fLowerRight = stArea.fWaterRight;
            }
            fAreaLower = section_calc_strip_area_below(axGeom, azGeom, nGeom, fLowerLeft,
                                                       fLowerRight, fTopLower);
            fAreaUpper = section_calc_strip_area_layer(axGeom, azGeom, nGeom, stArea.fWaterLeft,
                                                       stArea.fWaterRight, fSplit,
                                                       stArea.fWaterElevation);
            LOG(LOG_DEBUG,
                "zone edges: lower@[%.2f,%.2f](split=%.2f) upper@[%.2f,%.2f](water)",
                fLowerLeft,
                fLowerRight,
                fTopLower,
                stArea.fWaterLeft,
                stArea.fWaterRight);
        }

        stArea.fSplitElev = fSplit;
        stArea.fAreaUpper = fAreaUpper;
        stArea.fAreaLower = fAreaLower;
        stArea.fTotalArea = fAreaUpper + fAreaLower;

        if (fAreaUpper > 0.0 && hasUpperVel) {
            fQUpper = fAreaUpper * (double)fVelUpper;
        }
        if (fAreaLower > 0.0 && hasLowerVel) {
            fQLower = fAreaLower * (double)fVelLower;
        }
        if (fAreaUpper > 0.0 && !hasUpperVel) {
            LOG(LOG_WARNING, "total discharge: upper area>0 but sensor2(upper) invalid");
            return ERROR;
        }
        if (fAreaLower > 0.0 && !hasLowerVel) {
            LOG(LOG_WARNING, "total discharge: lower area>0 but sensor1(lower) invalid");
            return ERROR;
        }
        if (fAreaUpper <= 0.0 && fAreaLower <= 0.0) {
            return ERROR;
        }
    } else {
        /* 仅一条有效测线：整断面 × 对应传感器（优先 sensor1=下，否则 sensor2=上） */
        float fVel = 0.0f;
        uint8_t u8Sensor = 0;

        fBedUpper = hydrology_round_water(
            section_scaled_to_meter(g_stRunSection.astLines[aiActiveIdx[0]].i32RiverbedElevation));
        if (stArea.fWaterElevation <= fBedUpper) {
            LOG(LOG_WARNING, "total discharge: single line not submerged");
            return ERROR;
        }
        u8Sensor = 0;
        if (hasLowerVel) {
            u8Sensor = 0;
            fVel = fVelLower;
        } else if (hasUpperVel) {
            u8Sensor = 1;
            fVel = fVelUpper;
        } else {
            return ERROR;
        }
        stArea.fSplitElev = stArea.fWaterElevation;
        stArea.fAreaUpper = 0.0;
        stArea.fAreaLower = stArea.fTotalArea;
        fQLower = stArea.fTotalArea * (double)fVel;
        fQUpper = 0.0;
        LOG(LOG_DEBUG,
            "total discharge: single active line bed=%.2f sensor%u vel=%.2f",
            fBedUpper,
            (unsigned)(u8Sensor + 1),
            fVel);
    }

    {
        double fQRaw = fQUpper + fQLower;
        double fARaw = stArea.fTotalArea;
        double fAvgVel = 0.0;

        *pDischargeM3s = hydrology_round_flow(fQRaw);
        stArea.fAreaUpper = hydrology_round_area(stArea.fAreaUpper);
        stArea.fAreaLower = hydrology_round_area(stArea.fAreaLower);
        stArea.fTotalArea = hydrology_round_area(fARaw);
        /* 平均流速 = 修约后流量 / 修约后总面积 */
        if (stArea.fTotalArea > 1e-12) {
            fAvgVel = (*pDischargeM3s) / stArea.fTotalArea;
        }
        stArea.fAvgVelocity = hydrology_round_velocity(fAvgVel);
    }

    if (pAreaOut != NULL) {
        *pAreaOut = stArea;
    }

    LOG(LOG_DEBUG,
        "total discharge: water=%.2f split=%.2f Au=%.2f(sensor2_upper=%.3f)->Qu=%.3f "
        "Al=%.2f(sensor1_lower=%.3f)->Ql=%.3f A=%.2f avgVel=%.3f(=Q/A) width=%.2f Q=%.3f m3/s",
        stArea.fWaterElevation,
        stArea.fSplitElev,
        stArea.fAreaUpper,
        fVelUpper,
        fQUpper,
        stArea.fAreaLower,
        fVelLower,
        fQLower,
        stArea.fTotalArea,
        stArea.fAvgVelocity,
        stArea.fWidthAtWater,
        *pDischargeM3s);
    return SUCCESS;
}

int SectionParam_IsCrossSectionReady(void)
{
    return g_u8CrossSectionReady ? 1 : 0;
}

int SectionParam_IsRunSectionReady(void)
{
    return g_u8RunSectionReady ? 1 : 0;
}

int SectionParam_Init(void)
{
    memset(&g_stCrossSection, 0, sizeof(g_stCrossSection));
    memset(&g_stRunSection, 0, sizeof(g_stRunSection));
    g_u8CrossSectionReady = 0;
    g_u8RunSectionReady = 0;

    if (load_cross_section_file() == SUCCESS) {
        LOG(LOG_DEBUG, "loaded %s, section_id=%d", SHICHAFA_DATA_FILE, g_stCrossSection.i32SectionId);
    }
    if (load_run_section_file() == SUCCESS) {
        LOG(LOG_DEBUG, "loaded %s, speed_line_count=%d", YUNXING_DATA_FILE, g_stRunSection.i16SpeedLineCount);
    }

    SectionParam_ReportSectionId();
    return SUCCESS;
}
