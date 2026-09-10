#ifndef __LORA_SCREEN_H__
#define __LORA_SCREEN_H__

#include "uart.h"
typedef int (*SendFunction)(const uint8_t *data, size_t len);
typedef enum
{
    PACKET_OK = 0,
    DATA_TOO_SHORT,
    PACKET_NOT_FOUND,
} PacketResult;

typedef void (*PacketHandler)(uint8_t *data, uint16_t len);

PacketResult ProcLoraData(uint8_t *recvBuffer, uint16_t recvLen, WORD *wUsed, PacketHandler handler);

void deal_with_screen_data(uint8_t *recvBuffer, uint16_t recvLen);

void deal_with_screen_default_mode(uint8_t *recvBuffer, uint16_t recvLen);

void default_mode_send_depth_to_screen(float depth);

void default_mode_send_sediment_to_screen(float sediment);

void VMM_mode_send_sediment_to_screen(float sediment, uint16_t label_id);

void VDIM_mode_send_sediment_to_screen(float sediment);

void manual_mode_send_depth_to_screen(float depth);

void send_sensor_state_to_screen(void);

void deal_with_screen_mode_one(uint8_t *recvBuffer, uint16_t recvLen);

void deal_with_screen_mode_two(uint8_t *recvBuffer, uint16_t recvLen);

void deal_with_screen_mode_manual(uint8_t *recvBuffer, uint16_t recvLen);

float ascii_to_float(const char *str, size_t len);

int ascii_to_int(const char *str, size_t len);

size_t float_to_ascii(float num, uint8_t *buffer, size_t buffer_size, int decimal_places);

float parse_packet(const unsigned char *packet, int packet_len);

uint8_t *build_packet(uint16_t seq1, uint16_t seq2, const uint8_t *payload, size_t payload_len, size_t *out_len);

uint8_t *build_state_packet(uint16_t seq1, uint16_t seq2, const uint8_t *payload, size_t payload_len, size_t *out_len);

int send_custom_packet(uint16_t screen_id, uint16_t button_id, const uint8_t *payload, size_t payload_len, SendFunction send_func);

int serial_send_function(const uint8_t *data, size_t len);

#endif // !
