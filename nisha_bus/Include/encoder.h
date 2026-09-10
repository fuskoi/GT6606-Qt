#ifndef __ENCODER_H__
#define __ENCODER_H__
#include "uart.h"
#define WHEEL_CIRCUM_METAL 17.3   // CM
#define WHEEL_CIRCUM_RUBBER 14.13 // CM
#define COMMON_RESOLUTION 1024.0
#define HAICHAO_RESOLUTION 4096.0
typedef struct encoder
{
    uint32_t value;
    float distance;
    float wheel_circumference;
    float resolution;
    bool online;
    void (*isEncoderOnline)(bool *online);

} encoder_info_t;

typedef enum
{
    ENCODER_GETDATA,
    ENCODER_HEARTBEAT,
    ENCODER_SETCOUNTINGMODE,
    ENCODER_CLEARDATA, // 清零位置(255 零点)
} uart_encoder_task_name;

void *IsEncoderOnline(bool *online);

bool isEncoderOnline(void);

void GetEncoderDistance(float *distance);

void set_encoder_online(bool online);

// #include "crc_modbus.h"
int PassiveEncoderDataProcess(uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);
int EncoderSendCmd(uart_encoder_task_name taskName, int comfd);

int send_encodercmd_getvalue(int fd);

int send_encodercmd_setcountingmode(int fd);

int send_encodercmd_clearvalue(int fd);

int ActiveEncoderDataProcess(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);

bool DealWithEncoderTimeOut(int taskName);

int SubmitEncoderTask(int taskName);

int process_encoder_data(BYTE *pBuffer);

void carsystem_init_encodervaluecheck(uint8_t *recvBuffer);

void carsystem_init_encodervaluecheck_timeout(void);

void carsystem_init_encodercheck(uint8_t *cvBuffer);

void carsystem_init_encodercheck_timeout(void);

void carsys_backhome_encodervalue(uint8_t *recvBuffer);

void carsys_backhome_encodervalue_timeout(void);

void carsys_clearencoder_resetvalue(void);

void carsys_clearencoder_resetvalue_timeout(void);

#endif // !__ENCODER_H__


