#include "lora_screen.h"

#define DEFAULT_SCREEN_ID 2         // 默认模式 页面ID
#define DEFAULT_DEPTH_LABEL_ID 3    // 默认模式 显示深度 显示框ID
#define DEFAULT_SEDIMENT_LABEL_ID 4 // 默认模式 显示沉积物 显示框ID

#define VMM_SCREEN_ID 3            // VDIM模式 页面ID
#define VMM_SEDIMENT_LABEL_1_ID 8  // VMM 模式1 显示沉积物1 显示框ID
#define VMM_SEDIMENT_LABEL_2_ID 9  // VMM 模式1 显示沉积物2 显示框ID
#define VMM_SEDIMENT_LABEL_3_ID 10 // VMM 模式1 显示沉积物3 显示框ID
#define VMM_SEDIMENT_LABEL_4_ID 11 // VMM 模式1 显示沉积物4 显示框ID
#define VMM_SEDIMENT_LABEL_5_ID 12 // VMM 模式1 显示沉积物5 显示框ID 平均值

#define VDIM_SCREEN_ID 4           // VDIM模式 页面ID
#define VDIM_SEDIMENT_LABEL_1_ID 5 // VDIM 模式1 显示沉积物1 显示框ID

#define MANUAL_SCREEN_ID 5       // 手动模式 页面ID
#define MANUAL_SCREEN_LABEL_ID 3 // 手动模式 显示深度 显示框ID

#define STATE_SCREEN_ID 0
#define STATE_SCREEN_LABEL_NISHA_ID 2 // 泥沙传感器状态
#define STATE_SCREEN_LABEL_MOTOR_ID 5 // 电机状态
#define STATE_SCREEN_LABEL_RELAY_ID 6 // 继电器状态
#define STATE_SCREEN_LABEL_DEPTH_ID 7 // 深度状态
PacketResult ProcLoraData(uint8_t *recvBuffer, uint16_t recvLen, WORD *wUsed, PacketHandler handler)
{
    *wUsed = 0;

    if (recvLen < 6)
    {
        // 至少需要 2 字节报头 + 4 字节报尾 = 6 字节
        return DATA_TOO_SHORT;
    }

    for (int i = 0; i <= recvLen - 2; i++)
    {
        if (recvBuffer[i] == 0xEE && recvBuffer[i + 1] == 0xB1)
        {
            // 报头后至少要有4字节才能包含报尾
            uint16_t remaining = recvLen - (i + 2);
            if (remaining < 4)
            {
                *wUsed = i;
                return PACKET_NOT_FOUND;
            }

            // 在报头后寻找报尾 0xFF 0xFC 0xFF 0xFF
            for (int j = i + 2; j <= recvLen - 4; j++)
            {
                if (recvBuffer[j] == 0xFF &&
                    recvBuffer[j + 1] == 0xFC &&
                    recvBuffer[j + 2] == 0xFF &&
                    recvBuffer[j + 3] == 0xFF)
                {
                    int packetLength = j + 4 - i;
                    *wUsed = packetLength;

                    // 调用回调处理数据部分
                    if (handler)
                    {
                        handler(recvBuffer + i + 2, j - (i + 2));
                    }

                    return PACKET_OK;
                }
            }

            // 找到报头但未找到报尾，丢弃报头前的数据
            *wUsed = i;
            return PACKET_NOT_FOUND;
        }
    }

    // 没有找到任何有效的报头
    *wUsed = recvLen;
    return PACKET_NOT_FOUND;
}
void deal_with_screen_data(uint8_t *recvBuffer, uint16_t recvLen)
{
    ts_printf("Screen original data:");
    print_data(recvLen, recvBuffer);
    ts_printf("\n");
    switch (recvBuffer[2])
    {
    case 0x02: // 默认模式
        /* code */
        deal_with_screen_default_mode(recvBuffer, recvLen);
        break;
    case 0x03: // 模式1
        /* code */
        deal_with_screen_mode_one(recvBuffer, recvLen);
        break;
    case 0x04: // 模式2
        /* code */
        deal_with_screen_mode_two(recvBuffer, recvLen);
        break;
    case 0x05: // 手动模式
        /* code */
        deal_with_screen_mode_manual(recvBuffer, recvLen);
        break;

    default:
        break;
    }
}

void deal_with_screen_default_mode(uint8_t *recvBuffer, uint16_t recvLen)
{
    float depth;
    BYTE ascii_buffer[64];
    size_t ascii_len;
    switch (recvBuffer[4])
    {
    case 0x05: // 设置深度
        /* code */
        depth = parse_packet(recvBuffer + 5, recvLen - 5);
        set_default_mode_para_depth(depth * 1000);
        set_default_mode_keep_depth(depth * 1000);
        save_keep_depth_in_file(depth * 1000);
        printf("default depth: %.2fM\n", depth);
        printf("default depth: %dmm\n", get_default_mode_para_depth());

        break;
    case 0x06: // 启动
        /* code */
        if (get_default_mode_para_depth() <= 100)
        {
            break;
        }
        NiShaUD_Default_Mode_Start_Task(NULL);
        break;
    default:
        break;
    }
}

void deal_with_screen_mode_one(uint8_t *recvBuffer, uint16_t recvLen)
{
    float depth_1;
    float depth_2;
    float depth_3;
    float depth_4;
    switch (recvBuffer[4])
    {
    case 0x03: // 设置深度1
        /* code */
        depth_1 = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_one_para_depth(1, depth_1 * 1000);
        printf("mode one depth1: %.2fM\n", depth_1);
        printf("mode one depth1: %dmm\n", get_mode_one_para_depth(1));
        break;
    case 0x04: // 设置深度2
        /* code */
        depth_2 = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_one_para_depth(2, depth_2 * 1000);
        printf("mode one depth2: %.2fM\n", depth_2);
        printf("mode one depth2: %dmm\n", get_mode_one_para_depth(2));
        break;
    case 0x05: // 设置深度3
        /* code */
        depth_3 = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_one_para_depth(3, depth_3 * 1000);
        printf("mode one depth3: %.2fM\n", depth_3);
        printf("mode one depth3: %dmm\n", get_mode_one_para_depth(3));
        break;
    case 0x06: // 设置深度4
        /* code */
        depth_4 = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_one_para_depth(4, depth_4 * 1000);
        printf("mode one depth4: %.2fM\n", depth_4);
        printf("mode one depth4: %dmm\n", get_mode_one_para_depth(4));
        break;
    case 0x07: // 开始
        /* code */
        NiShaUD_VMM_Mode_Start_Task(NULL);
        break;
    default:
        break;
    }
}

void deal_with_screen_mode_two(uint8_t *recvBuffer, uint16_t recvLen)
{
    float depth_A;
    float depth_B;
    switch (recvBuffer[4])
    {
    case 0x03: // 设置深度A
        /* code */
        depth_A = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_two_para_depth(1, depth_A * 1000);
        printf("mode two depthA: %.2fM\n", depth_A);
        printf("mode two depthA: %dmm\n", get_mode_two_para_depth(1));
        break;
    case 0x04: // 设置深度B
        /* code */
        depth_B = parse_packet(recvBuffer + 5, recvLen - 5);
        set_mode_two_para_depth(2, depth_B * 1000);
        printf("mode two depthB: %.2fM\n", depth_B);
        printf("mode two depthB: %dmm\n", get_mode_two_para_depth(2));
        break;
    case 0x06: // 开始
        /* code */
        NiShaUD_VDIM_Mode_Start_Task(NULL);
        break;
    default:
        break;
    }
}

void deal_with_screen_mode_manual(uint8_t *recvBuffer, uint16_t recvLen)
{

    switch (recvBuffer[4])
    {
    case 0x04: // 上升
        /* code */
        if (0x00 == recvBuffer[7]) // 停止
        {
            NiShaUD_Manual_Mode_Stop_Task(NULL);
        }
        if (0x01 == recvBuffer[7]) // 开始
        {
            NiShaUD_Stop_Running_Task();
            sleep_ms(10);
            set_dc_motor_speed(600);
            NiShaUD_Manual_Mode_Up_Task(NULL);
        }
        break;
    case 0x05: // 下降
        /* code */
        if (0x00 == recvBuffer[7]) // 停止
        {
            NiShaUD_Manual_Mode_Stop_Task(NULL);
        }
        if (0x01 == recvBuffer[7]) // 开始
        {
            NiShaUD_Stop_Running_Task();
            sleep_ms(10);
            set_dc_motor_speed(600);
            NiShaUD_Manual_Mode_Down_Task(NULL);
        }
        break;
    case 0x06: // 停止
        /* code */
        ts_printf("run touch stop!\n");
        NiShaUD_Stop_Running_Task();
        sleep_ms(10);
        NiShaUD_Manual_Mode_Stop_Task(NULL);
        break;
    default:
        break;
    }
}

float ascii_to_float(const char *str, size_t len)
{
    float result = 0.0;
    float decimal = 0.0;
    float scale = 0.1;
    int sign = 1;
    size_t i = 0;

    while (i < len && isspace(str[i]))
    {
        i++;
    }

    if (i < len && str[i] == '-')
    {
        sign = -1;
        i++;
    }
    else if (i < len && str[i] == '+')
    {
        i++;
    }

    // 整数部分
    while (i < len && isdigit(str[i]))
    {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    // 小数部分
    if (i < len && str[i] == '.')
    {
        i++;
        while (i < len && isdigit(str[i]))
        {
            decimal += (str[i] - '0') * scale;
            scale *= 0.1;
            i++;
        }
    }

    return sign * (result + decimal);
}

int ascii_to_int(const char *str, size_t len)
{
    int result = 0;
    int sign = 1;
    size_t i = 0;

    while (i < len && isspace(str[i]))
    {
        i++; // 跳过空格
    }

    if (i < len)
    {
        if (str[i] == '-')
        {
            sign = -1;
            i++;
        }
        else if (str[i] == '+')
        {
            i++;
        }
    }

    while (i < len && isdigit(str[i]))
    {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return sign * result;
}

size_t int_to_ascii(int num, uint8_t *buffer, size_t buffer_size)
{
    if (buffer_size == 0 || buffer == NULL)
        return 0;

    int index = 0;
    int is_negative = 0;

    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }

    // 处理特殊情况：0
    if (num == 0)
    {
        if (buffer_size > 0)
            buffer[0] = '0';
        return 1;
    }

    // 提取数字并倒序存储
    while (num > 0 && index < buffer_size - 1)
    {
        buffer[index++] = (num % 10) + '0';
        num /= 10;
    }

    if (is_negative && index < buffer_size)
    {
        buffer[index++] = '-';
    }

    // 反转字符顺序
    for (int i = 0; i < index / 2; i++)
    {
        uint8_t temp = buffer[i];
        buffer[i] = buffer[index - i - 1];
        buffer[index - i - 1] = temp;
    }

    return index;
}

size_t float_to_ascii(float num, uint8_t *buffer, size_t buffer_size, int decimal_places)
{
    if (buffer_size == 0 || buffer == NULL || decimal_places < 0)
        return 0;

    int is_negative = 0;
    if (num < 0)
    {
        is_negative = 1;
        num = -num;
    }

    int integer_part = (int)num;
    float decimal_part = num - integer_part;

    // 计算整数部分长度
    int int_len = 0;
    if (integer_part == 0)
    {
        int_len = 1;
    }
    else
    {
        int temp = integer_part;
        while (temp > 0)
        {
            temp /= 10;
            int_len++;
        }
    }

    // 总长度 = 整数部分 + 小数点 + 小数部分 + 负号（如果有的话）
    size_t total_len = int_len + 1 + decimal_places + (is_negative ? 1 : 0);
    if (total_len >= buffer_size)
        return 0; // 缓冲区不足

    int index = 0;

    // 添加负号
    if (is_negative)
    {
        buffer[index++] = '-';
    }

    // 添加整数部分
    if (integer_part == 0)
    {
        buffer[index++] = '0';
    }
    else
    {
        int temp = integer_part;
        // 倒序存储
        while (temp > 0)
        {
            buffer[index++] = (temp % 10) + '0';
            temp /= 10;
        }
        // 反转整数部分
        for (int i = 0; i < (index - (is_negative ? 1 : 0)) / 2; i++)
        {
            int start = (is_negative ? 1 : 0);
            uint8_t temp = buffer[start + i];
            buffer[start + i] = buffer[start + index - i - 1];
            buffer[start + index - i - 1] = temp;
        }
    }

    // 添加小数点
    if (decimal_places > 0)
    {
        buffer[index++] = '.';
    }

    // 添加小数部分
    for (int i = 0; i < decimal_places; i++)
    {
        decimal_part *= 10;
        buffer[index++] = (int)decimal_part + '0';
        decimal_part -= (int)decimal_part;
    }

    return index;
}

#define MAX_PACKET_LEN 256

float parse_packet(const unsigned char *packet, int packet_len)
{
    // 查找起始和结束位置
    int start_index = -1;
    int end_index = -1;

    for (int i = 0; i < packet_len; i++)
    {
        if (packet[i] == 0x11 && start_index == -1)
        {
            start_index = i + 1; // 起始符后一位开始有效数据
        }
        if (packet[i] == 0x00 && start_index != -1)
        {
            end_index = i;
            break; // 找到第一个0x00就停止
        }
    }

    if (start_index == -1 || end_index == -1 || start_index >= end_index)
    {
        printf("Invalid packet format.\n");
        return 0.0f;
    }

    // 提取ASCII字符部分
    int str_len = end_index - start_index;
    char *str = (char *)malloc(str_len + 1);
    memcpy(str, &packet[start_index], str_len);
    str[str_len] = '\0'; // 字符串结尾

    // 转换为浮点数
    float result = atof(str);

    free(str);
    return result;
}

uint8_t *build_packet(uint16_t seq1, uint16_t seq2,
                      const uint8_t *payload, size_t payload_len,
                      size_t *out_len)
{
    const size_t header_len = 3;
    const size_t footer_len = 4;
    const size_t seq_len = 2 + 2;
    const size_t total_len = header_len + seq_len + payload_len + footer_len;

    uint8_t *packet = (uint8_t *)malloc(total_len);
    if (!packet)
    {
        *out_len = 0;
        return NULL;
    }

    size_t offset = 0;

    // 帧头
    packet[offset++] = 0xEE;
    packet[offset++] = 0xB1;
    packet[offset++] = 0x10;

    // 序号1（大端）
    packet[offset++] = (seq1 >> 8) & 0xFF;
    packet[offset++] = seq1 & 0xFF;

    // 序号2（大端）
    packet[offset++] = (seq2 >> 8) & 0xFF;
    packet[offset++] = seq2 & 0xFF;

    // 正文
    if (payload && payload_len > 0)
    {
        memcpy(packet + offset, payload, payload_len);
        offset += payload_len;
    }

    // 帧尾
    packet[offset++] = 0xFF;
    packet[offset++] = 0xFC;
    packet[offset++] = 0xFF;
    packet[offset++] = 0xFF;

    *out_len = total_len;
    return packet;
}

uint8_t *build_state_packet(uint16_t seq1, uint16_t seq2,
                            const uint8_t *payload, size_t payload_len,
                            size_t *out_len)
{
    const size_t header_len = 3;
    const size_t footer_len = 4;
    const size_t seq_len = 2 + 2;
    const size_t total_len = header_len + seq_len + payload_len + footer_len;

    uint8_t *packet = (uint8_t *)malloc(total_len);
    if (!packet)
    {
        *out_len = 0;
        return NULL;
    }

    size_t offset = 0;

    // 帧头
    packet[offset++] = 0xEE;
    packet[offset++] = 0xB1;
    packet[offset++] = 0x23;

    // 序号1（大端）
    packet[offset++] = (seq1 >> 8) & 0xFF;
    packet[offset++] = seq1 & 0xFF;

    // 序号2（大端）
    packet[offset++] = (seq2 >> 8) & 0xFF;
    packet[offset++] = seq2 & 0xFF;

    // 正文
    if (payload && payload_len > 0)
    {
        memcpy(packet + offset, payload, payload_len);
        offset += payload_len;
    }

    // 帧尾
    packet[offset++] = 0xFF;
    packet[offset++] = 0xFC;
    packet[offset++] = 0xFF;
    packet[offset++] = 0xFF;

    *out_len = total_len;
    return packet;
}

int send_custom_packet(uint16_t screen_id, uint16_t button_id,
                       const uint8_t *payload, size_t payload_len,
                       SendFunction send_func)
{
    size_t packet_len;
    uint8_t *packet = build_packet(screen_id, button_id, payload, payload_len, &packet_len);
    // ts_printf("Sending packet\n");
    if (!packet)
    {
        fprintf(stderr, "Failed to build packet.\n");
        return -1;
    }

    // 调用发送函数
    if (send_func)
    {
        int result = send_func(packet, packet_len);
        // ts_printf("Packet sent\n");
        free(packet);
        return result;
    }
    else
    {
        fprintf(stderr, "No send function provided.\n");
        free(packet);
        return -1;
    }
}

int send_custom_state(uint16_t screen_id, uint16_t button_id,
                      const uint8_t *payload, size_t payload_len,
                      SendFunction send_func)
{
    size_t packet_len;
    uint8_t *packet = build_state_packet(screen_id, button_id, payload, payload_len, &packet_len);
    // ts_printf("Sending packet\n");
    if (!packet)
    {
        fprintf(stderr, "Failed to build packet.\n");
        return -1;
    }

    // 调用发送函数
    if (send_func)
    {
        int result = send_func(packet, packet_len);
        // ts_printf("Packet sent\n");
        free(packet);
        return result;
    }
    else
    {
        fprintf(stderr, "No send function provided.\n");
        free(packet);
        return -1;
    }
}

// 发送函数封装（用于串口）
int serial_send_function(const uint8_t *data, size_t len)
{
    // ts_printf("serial_send_function\n");
    int serial_fd; // 假设全局变量 serial_fd 是已打开的串口文件描述符
    serial_fd = get_device_port_fd(LORA);
    // ts_printf("serial_fd: %d\n", serial_fd);
    if (serial_fd < 0)
        return -1;

    ssize_t bytes_sent = write(serial_fd, data, len);
    if (bytes_sent < 0)
    {
        perror("Error writing to serial port");
        return -1;
    }
    else if ((size_t)bytes_sent != len)
    {
        fprintf(stderr, "Warning: Not all bytes sent. Sent: %ld / Expected: %zu\n", bytes_sent, len);
        return -1;
    }
    return 0;
}

void default_mode_send_depth_to_screen(float depth)
{
    BYTE ascii_buffer[64];
    size_t ascii_len;
    ascii_len = float_to_ascii(depth, ascii_buffer, 64, 3);
    send_custom_packet(DEFAULT_SCREEN_ID, DEFAULT_DEPTH_LABEL_ID, ascii_buffer, ascii_len, serial_send_function);
}

void default_mode_send_sediment_to_screen(float sediment)
{
    BYTE ascii_buffer[64];
    size_t ascii_len;
    ascii_len = float_to_ascii(sediment, ascii_buffer, 64, 3);
    send_custom_packet(DEFAULT_SCREEN_ID, DEFAULT_SEDIMENT_LABEL_ID, ascii_buffer, ascii_len, serial_send_function);
}
/// @brief 模式1
/// @param sediment
/// @param label_id VMM_SEDIMENT_LABEL_1_ID/VMM_SEDIMENT_LABEL_2_ID/VMM_SEDIMENT_LABEL_3_ID/VMM_SEDIMENT_LABEL_4_ID/VMM_SEDIMENT_LABEL_5_ID
void VMM_mode_send_sediment_to_screen(float sediment, uint16_t label_id)
{
    BYTE ascii_buffer[64];
    size_t ascii_len;
    ascii_len = float_to_ascii(sediment, ascii_buffer, 64, 3);
    send_custom_packet(VMM_SCREEN_ID, label_id, ascii_buffer, ascii_len, serial_send_function);
}
// 模式2
void VDIM_mode_send_sediment_to_screen(float sediment)
{
    BYTE ascii_buffer[64];
    size_t ascii_len;
    ascii_len = float_to_ascii(sediment, ascii_buffer, 64, 3);
    send_custom_packet(VDIM_SCREEN_ID, VDIM_SEDIMENT_LABEL_1_ID, ascii_buffer, ascii_len, serial_send_function);
}
// 手动模式
void manual_mode_send_depth_to_screen(float depth)
{
    BYTE ascii_buffer[64];
    size_t ascii_len;
    ascii_len = float_to_ascii(depth, ascii_buffer, 64, 3);
    send_custom_packet(MANUAL_SCREEN_ID, MANUAL_SCREEN_LABEL_ID, ascii_buffer, ascii_len, serial_send_function);
}

void send_sensor_state_to_screen(void)
{
    uint8_t online = 0;
    uint8_t offline = 1;
    // send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_NISHA_ID, &offline, 1, serial_send_function);
    if (is_sediment_sensor_online())
    {
        ts_printf("Nisha online\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_NISHA_ID, &online, 1, serial_send_function);
    }
    else
    {
        ts_printf("Nisha offline\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_NISHA_ID, &offline, 1, serial_send_function);
    }

    if (is_dc_motor_online())
    {
        ts_printf("DC motor online\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_MOTOR_ID, &online, 1, serial_send_function);
    }
    else
    {
        ts_printf("DC motor offline\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_MOTOR_ID, &offline, 1, serial_send_function);
    }
    if (isRelayModuleOnline())
    {
        ts_printf("Relay module online\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_RELAY_ID, &online, 1, serial_send_function);
    }
    else
    {
        ts_printf("Relay module offline\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_RELAY_ID, &offline, 1, serial_send_function);
    }

    if (is_depth_sensor_online())
    {
        ts_printf("Depth sensor online\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_DEPTH_ID, &online, 1, serial_send_function);
    }
    else
    {
        ts_printf("Depth sensor offline\n");
        send_custom_state(STATE_SCREEN_ID, STATE_SCREEN_LABEL_DEPTH_ID, &offline, 1, serial_send_function);
    }
    ts_printf("\n");
}
