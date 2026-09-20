#include "Common.h"
#include "TaskList.h"
#include "WebSeriver.h"
#include "LinkList.h"
#include "Encoder.h"
#include "Sensor.h"
#include "CRC.h"
#include "log.h"
#include <ctype.h>
#include "Relay.h"
#include "timer_manager.h"
#include "RS485_ICD.h"
#include "zigbee_dataproc.h"
#define WATCHDOG_DEV "/dev/watchdog"
struct DEVICE_LIST g_stDevicelist;
ROTATOR_CONFIG g_stRotatorConfig;
DEVICE_ADDR g_stDeviceAddr;
MEASURETIME g_stMeasureTime;
BYTE g_aStationID[12] = {0};
BYTE g_CenterStationID = 0;
BYTE g_RTUID[5] = {0};
pthread_mutex_t g_lock[MAX_UART_NUM];
WATER_LEVEL_CONFIG g_stWaterLevelConfig;
WEB_LINK_CONFIG g_stWebLinkConfig = {
    .u8WebLinkMode = WEB_LINK_MODE_SOCKET,
    .szSocketServerIP = "192.168.101.50",
    .u16SocketServerPort = 6104
};
uint16_t g_CarDistenceAdjust = 0;         //小车运动误差补偿
uint16_t g_FishDistenceAdjust = 0;        //铅鱼运动误差补偿
int g_WatchDogFd = 0;
uint64_t g_timeout_Intervel = 60;
const uint16_t g_aSpeedRpm[7] = {100,500,1000,1500,2000,2500,3000};



/*
*函数名称:GetAppDir
*函数功能:获取安装根目录（配置/数据所在目录）
*输   入:无
*输   出:char* appDir  安装根目录
*返   回:true - 成功   false - 失败
*说   明:首装与程序同目录；OTA 后程序在 ota/slot_x，配置仍在上上级安装根
*/
static void strip_ota_slot_suffix(char *dir)
{
    char *p;

    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    p = strstr(dir, "/ota/slot_");
    if (p == NULL) {
        return;
    }
    *p = '\0';
    if (dir[0] == '\0') {
        dir[0] = '/';
        dir[1] = '\0';
    }
}

bool GetAppDir(char* appDir)
{
    char path[MAX_PATH];
    char *slash;
    ssize_t n;

    if (appDir == NULL) {
        return false;
    }
    memset(appDir, 0, MAX_PATH);
    memset(path, 0, sizeof(path));

    n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        slash = strrchr(path, '/');
        if (slash != NULL && slash != path) {
            *slash = '\0';
            strip_ota_slot_suffix(path);
            strncpy(appDir, path, MAX_PATH - 1);
            return true;
        }
    }

    memset(path, 0, sizeof(path));
    if (getcwd(path, sizeof(path)) && path[0] != '\0' &&
        !(path[0] == '/' && path[1] == '\0')) {
        strip_ota_slot_suffix(path);
        strncpy(appDir, path, MAX_PATH - 1);
        return true;
    }

    appDir[0] = '/';
    appDir[1] = '\0';
    return true;
}

void TrimLeft(char* pString)
{
    int nLength = strlen(pString);

    int i = 0;
    for(i = 0; i < nLength; i++)
    {
        if(pString[i] == ' ')
        {
            continue;
        }
        else
        {
            break;
        }
    }
    if(i > 0)
    {
        memcpy(pString,pString + i,nLength - i);
        pString[nLength - i] = 0;
    }
}

void TrimRight(char* pString)
{
    int nLength = strlen(pString);
    int i = 0;
    for(i = nLength - 1; i >=  0; i--)
    {
        if(pString[i] == ' ' || pString[i] == '\r' ||pString[i] == '\n')
        {
            pString[i] = 0;
        }
        else
        {
            break;
        }
    }
}

void TrimStr(char* pStr)
{
    TrimRight(pStr);
    TrimLeft(pStr);
}


/**
 * @brief 将字符串转换为BCD格式并存储在缓冲区中
 * 
 * @param str 输入的字符串，长度必须是偶数
 * @param buffer 指向存储BCD码的缓冲区，大小应为 strlen(str) / 2
 * @return int 返回成功转换的字节数，若输入字符串长度不是偶数则返回 -1
 */
int str_to_bcd(const char *str, uint8_t *buffer) 
{
    size_t len = strlen(str);
    
    // 确保字符串长度是偶数
    if (len % 2 != 0) 
    {
        LOG(LOG_ERROR,"str_to_bcd: Invalid input string length");
        return ERROR;
    }

    // 遍历字符串，每两个字符一组
    size_t i;
    for ( i= 0; i < len / 2; i++) 
    {
        char high_nibble = str[i * 2];
        char low_nibble = str[i * 2 + 1];

        // 检查字符是否为有效的十六进制字符
        if ((high_nibble < '0' || high_nibble > '9') && (high_nibble < 'A' || high_nibble > 'F') && (high_nibble < 'a' || high_nibble > 'f')) 
        {
            LOG(LOG_ERROR,"str_to_bcd: Invalid input string character");
            return ERROR;
        }
        if ((low_nibble < '0' || low_nibble > '9') && (low_nibble < 'A' || low_nibble > 'F') && (low_nibble < 'a' || low_nibble > 'f')) 
        {
            LOG(LOG_ERROR,"str_to_bcd: Invalid input string character");
            return -ERROR;
        }

        // 转换字符为数值
        uint8_t high_val = (high_nibble >= '0' && high_nibble <= '9') ? (high_nibble - '0') : (10 + toupper(high_nibble) - 'A');
        uint8_t low_val = (low_nibble >= '0' && low_nibble <= '9') ? (low_nibble - '0') : (10 + toupper(low_nibble) - 'A');

        // 组合成BCD码
        buffer[i] = (high_val << 4) | low_val;
    }

    return len / 2;
}
/*
*函数名称:ReadConfig
*函数功能:读取配置表
*输   入:无  
*输   出:无
*返   回:true - 成功读取配置表   false - 读取配置表失败
*/
bool ReadConfig(void)
{
    char configFileName[256];
    char tempBuffer[1024];
    char findChar = '=';
    char keyStr[1024];
    char valueStr[1024];
    memset(configFileName,0,256);
    if(!GetAppDir(configFileName))
    {
        LOG(LOG_ERROR, "GetConfigAppDir Error");
        return false;
    }
    sprintf(configFileName,"%s%s",configFileName,CONFIG_FILE_PATH);
    LOG(LOG_INFO,"config file Path is %s",configFileName);
    FILE* pFile = fopen(configFileName,"rb");
    if(NULL == pFile)
    {
        LOG(LOG_ERROR,"config file open error!");
        return false;
    }
    while(true)
    {
        memset(tempBuffer,0,1024);
        if(NULL == fgets(tempBuffer, 1024, pFile))
        {
            break;
        }
        char* pValue = strchr(tempBuffer, findChar);
        if(NULL == pValue)
        {
            continue;
        }
        int nPos = pValue - tempBuffer;
        memset(keyStr,0,1024);
        memset(valueStr,0,1024);
        memcpy(keyStr,tempBuffer,nPos);
        memcpy(valueStr,tempBuffer + nPos + 1,strlen(tempBuffer) - nPos - 1);
        TrimStr(keyStr);
        TrimStr(valueStr);
        if(keyStr[0] == '#' || strlen(keyStr) == 0 || strlen(valueStr) == 0)
        {
            continue;
        }

        if(strcasecmp(keyStr,"DC_MOTOR") == 0)
        {
            g_stDevicelist.DC_Motor = atoi(valueStr) -1;
            g_stDevicelist.UartList[g_stDevicelist.DC_Motor].staticUartType = DC_MOTOR_TYPE;
            LOG(LOG_INFO,"DC_Motor  uart COM is COM%d",g_stDevicelist.DC_Motor);
            //g_stDevicelist.UartList[g_stDevicelist.DC_Motor].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);
        }
        if(strcasecmp(keyStr,"HOST_COMPUTER") == 0)
        {
            g_stDevicelist.HOST_COMPUTER = atoi(valueStr) -1;
            LOG(LOG_INFO,"HOST_COMPUTER  uart COM is COM%d",g_stDevicelist.HOST_COMPUTER);
            g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].staticUartType = DTU_TYPE;
            //g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);

        }
        if(strcasecmp(keyStr,"ZigBee") == 0)
        {
            g_stDevicelist.ZigBee = atoi(valueStr) -1;
            LOG(LOG_INFO,"ZigBee  uart COM is COM%d",g_stDevicelist.ZigBee);
            g_stDevicelist.UartList[g_stDevicelist.ZigBee].staticUartType = ZIGBEE_TYPE;
            //g_stDevicelist.UartList[g_stDevicelist.ZigBee].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);

        }

        if(strcasecmp(keyStr,"Encoder") == 0)
        {
            g_stDevicelist.Encoder = atoi(valueStr) -1;
            LOG(LOG_INFO,"Encoder  uart COM is COM%d",g_stDevicelist.Encoder);
            g_stDevicelist.UartList[g_stDevicelist.Encoder].staticUartType = ENCODER_TYPE;
            //g_stDevicelist.UartList[g_stDevicelist.ZigBee].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);

        }
		if(strcasecmp(keyStr,"Relay") == 0)
        {
            g_stDevicelist.Relay = atoi(valueStr) -1;
            LOG(LOG_INFO,"Relay  uart COM is COM%d",g_stDevicelist.Relay);
            g_stDevicelist.UartList[g_stDevicelist.Relay].staticUartType = RELAY_TYPE;
            //g_stDevicelist.UartList[g_stDevicelist.ZigBee].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);

        }
		if(strcasecmp(keyStr,"Sensor") == 0)
        {
            g_stDevicelist.Sensor = atoi(valueStr) -1;
            LOG(LOG_INFO,"Sensor  uart COM is COM%d",g_stDevicelist.Sensor);
            g_stDevicelist.UartList[g_stDevicelist.Sensor].staticUartType = SENSOR_TYPE;
            //g_stDevicelist.UartList[g_stDevicelist.ZigBee].ringfifo = kfifo_init(RECEVIE_BUFFER_LENGTH);

        }
		


        else if(strcasecmp(keyStr,"Baudrate1") == 0)
        {
            g_stDevicelist.UartList[0].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM1 Baudrate is %d",g_stDevicelist.UartList[0].uartpara.Baudrate);
        }
        else if(strcasecmp(keyStr,"Baudrate2") == 0)
        {
            g_stDevicelist.UartList[1].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM2 Baudrate is %d",g_stDevicelist.UartList[1].uartpara.Baudrate);
        }
        else if(strcasecmp(keyStr,"Baudrate3") == 0)
        {
            g_stDevicelist.UartList[2].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM3 Baudrate is %d",g_stDevicelist.UartList[2].uartpara.Baudrate);
        }
        else if(strcasecmp(keyStr,"Baudrate4") == 0)
        {
            g_stDevicelist.UartList[3].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM4 Baudrate is %d",g_stDevicelist.UartList[3].uartpara.Baudrate);
        }
        else if(strcasecmp(keyStr,"Baudrate5") == 0)
        {
            g_stDevicelist.UartList[4].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM5 Baudrate is %d",g_stDevicelist.UartList[4].uartpara.Baudrate);
        }
        else if(strcasecmp(keyStr,"Baudrate6") == 0)
        {
            g_stDevicelist.UartList[5].uartpara.Baudrate = atoi(valueStr);
            LOG(LOG_INFO,"COM6 Baudrate is %d",g_stDevicelist.UartList[5].uartpara.Baudrate);
        }
		if(strcasecmp(keyStr,"StationID") == 0)
		{
            int len = 0;
            //651协议中的站码按照bcd码编写，自定义协议中的站码按照字符串的ASCII码编写
            len = str_to_bcd(valueStr,g_RTUID + 1);
            memcpy(g_aStationID ,valueStr,len * 2);
            //memcpy(g_aStationID + sizeof(g_aStationID) - (len * 2),valueStr,len * 2);
            
            LOG(LOG_DEBUG,"g_RTUID is");
            LOG_ARRAY(LOG_DEBUG,g_RTUID,sizeof(g_RTUID),"%02x");
            LOG(LOG_DEBUG,"StationID is");
            LOG_ARRAY(LOG_DEBUG,g_aStationID,sizeof(g_aStationID),"%02x"); 
		}
		if(strcasecmp(keyStr,"CenterStationID") == 0)
		{
			g_CenterStationID = (BYTE)atoi(valueStr);
			LOG(LOG_INFO,"CenterStationID is %d", g_CenterStationID);
		}
		if(strcasecmp(keyStr,"Motor_Drive_car_Addr") == 0)
		{
			g_stDeviceAddr.u8Motor_Drive_car = atoi(valueStr);
            LOG(LOG_DEBUG,"g_stDeviceAddr.u8Motor_Drive_car is %d",g_stDeviceAddr.u8Motor_Drive_car);
		}
		if(strcasecmp(keyStr,"Motor_Drive_fish_Addr") == 0)
		{
			g_stDeviceAddr.u8Motor_Drive_fish = atoi(valueStr);
            LOG(LOG_DEBUG,"g_stDeviceAddr.u8Motor_Drive_fish is %d",g_stDeviceAddr.u8Motor_Drive_fish);
		}
		if(strcasecmp(keyStr,"Encoder_car_Addr") == 0)
		{
			g_stDeviceAddr.u8Encoder_car = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Encoder_car is %d",g_stDeviceAddr.u8Encoder_car);
		}
		if(strcasecmp(keyStr,"Encoder_fish_Addr") == 0)
		{
			g_stDeviceAddr.u8Encoder_fish = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Encoder_fish is %d",g_stDeviceAddr.u8Encoder_fish);
		}
		if(strcasecmp(keyStr,"Relay_2_port_power_Addr") == 0)
		{
			g_stDeviceAddr.u8Relay_2_port_power = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Relay_2_port_power is %d",g_stDeviceAddr.u8Relay_2_port_power);
		}
		if(strcasecmp(keyStr,"Relay_2_port_break_Addr") == 0)
		{
			g_stDeviceAddr.u8Relay_2_port2_break = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Relay_2_port2_break is %d",g_stDeviceAddr.u8Relay_2_port2_break);
		}
		if(strcasecmp(keyStr,"Relay_8_port_Addr") == 0)
		{
			g_stDeviceAddr.u8Relay_8_port = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Relay_8_port is %d",g_stDeviceAddr.u8Relay_8_port);
		}
		if(strcasecmp(keyStr,"Rotator_Addr") == 0)
		{
			g_stDeviceAddr.u8Rotator_addr = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8Rotator_addr is %d",g_stDeviceAddr.u8Rotator_addr);
		}
		if(strcasecmp(keyStr,"WaterLevel_Addr") == 0)
		{
			g_stDeviceAddr.u8WaterLevel = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.u8WaterLevel is %d",g_stDeviceAddr.u8WaterLevel);
		}
        if(strcasecmp(keyStr,"Coulomb_Addr") == 0)
		{
			g_stDeviceAddr.u8CoulombAddr = atoi(valueStr);
            LOG(LOG_INFO,"g_stDeviceAddr.Coulomb_Addr is %d",g_stDeviceAddr.u8CoulombAddr);
		}
        if(strcasecmp(keyStr,"TouchScreenAddr") == 0)
		{
			g_stDeviceAddr.u8TouchScreenAddr = atoi(valueStr);
            LOG(LOG_DEBUG,"g_stDeviceAddr.u8TouchScreenAddr is %d",g_stDeviceAddr.u8TouchScreenAddr);
		}
        if(strcasecmp(keyStr,"Electroc_door_Addr") == 0)
		{
			g_stDeviceAddr.u8Electroc_door = atoi(valueStr);
            LOG(LOG_DEBUG,"g_stDeviceAddr.u8Electroc_door is %d",g_stDeviceAddr.u8Electroc_door);
		}

        if(strcasecmp(keyStr,"RotatorMeasureTime") == 0)
		{
			g_stMeasureTime.u8Rotator = atoi(valueStr) ;
            g_stRotatorConfig.u16Meaduration = g_stMeasureTime.u8Rotator;
            LOG(LOG_DEBUG,"Rotator Measure time  is %d Seconds",g_stMeasureTime.u8Rotator);
		}
        if(strcasecmp(keyStr,"WaterBase") == 0)
		{
			g_stWaterLevelConfig.u16WaterBase= atoi(valueStr) ;
            LOG(LOG_INFO,"WaterBase  is %d cm",g_stWaterLevelConfig.u16WaterBase);
		}

        if(strcasecmp(keyStr,"WaterType") == 0)
		{
			g_stWaterLevelConfig.u16WaterType= atoi(valueStr) ;
            LOG(LOG_INFO,"u16WaterType  is %d cm",g_stWaterLevelConfig.u16WaterType);
		}


        if(strcasecmp(keyStr,"CarDistenseAdjust") == 0)   //小车运动误差补偿
		{
			g_CarDistenceAdjust = atoi(valueStr) ;
            LOG(LOG_INFO,"g_CarDistenceAdjust  is %d ",g_CarDistenceAdjust);
		}
        if(strcasecmp(keyStr,"FishDistenseAdjust") == 0)  //铅鱼运动误差补偿
		{
			g_FishDistenceAdjust = atoi(valueStr) ;
            LOG(LOG_INFO,"g_FishDistenceAdjust  is %d ",g_FishDistenceAdjust);
		}

        if(strcasecmp(keyStr,"a_value") == 0)
		{
			g_stRotatorConfig.fa_value = atof(valueStr);
            LOG(LOG_DEBUG,"g_stRotatorConfig.fa_value  is %f ",g_stRotatorConfig.fa_value);
		}
        if(strcasecmp(keyStr,"b_value") == 0)
		{
			g_stRotatorConfig.fb_value = atof(valueStr);
            LOG(LOG_DEBUG,"g_stRotatorConfig.fb_value  is %f ",g_stRotatorConfig.fb_value);
		}
        if(strcasecmp(keyStr,"Circel_value") == 0)
		{
			g_stRotatorConfig.u16n_value = atof(valueStr);
            LOG(LOG_DEBUG,"g_stRotatorConfig.fCircel_value  is %f ",g_stRotatorConfig.u16n_value);
		}
        if(strcasecmp(keyStr,"StandbyTime") == 0)
		{
			g_timeout_Intervel = atoi(valueStr);
            LOG(LOG_DEBUG,"StandbyTime  is %d minutes",g_timeout_Intervel );
            g_timeout_Intervel = g_timeout_Intervel * 60  ;
            
		}
        if(strcasecmp(keyStr,"FlowLeftBankCoef") == 0)
        {
            g_stFlowCalcConfig.fLeftBankCoef = (float)atof(valueStr);
            LOG(LOG_INFO,"FlowLeftBankCoef is %.3f", g_stFlowCalcConfig.fLeftBankCoef);
        }
        if(strcasecmp(keyStr,"FlowRightBankCoef") == 0)
        {
            g_stFlowCalcConfig.fRightBankCoef = (float)atof(valueStr);
            LOG(LOG_INFO,"FlowRightBankCoef is %.3f", g_stFlowCalcConfig.fRightBankCoef);
        }
        if(strcasecmp(keyStr,"FlowLineCoef") == 0)
        {
            g_stFlowCalcConfig.fLineCoef = (float)atof(valueStr);
            LOG(LOG_INFO,"FlowLineCoef is %.3f", g_stFlowCalcConfig.fLineCoef);
        }
        if(strcasecmp(keyStr,"FlowMatchTolCm") == 0)
        {
            g_stFlowCalcConfig.u32MatchTolCm = (uint32_t)atoi(valueStr);
            LOG(LOG_INFO,"FlowMatchTolCm is %u cm", g_stFlowCalcConfig.u32MatchTolCm);
        }
        if (strcasecmp(keyStr, "WebLinkMode") == 0) {
            g_stWebLinkConfig.u8WebLinkMode =
                (atoi(valueStr) != 0) ? WEB_LINK_MODE_SOCKET : WEB_LINK_MODE_DTU;
            LOG(LOG_INFO, "WebLinkMode is %s",
                (g_stWebLinkConfig.u8WebLinkMode == WEB_LINK_MODE_SOCKET) ?
                    "socket" : "dtu");
        }
        if (strcasecmp(keyStr, "SocketServerIP") == 0) {
            memset(g_stWebLinkConfig.szSocketServerIP, 0,
                   sizeof(g_stWebLinkConfig.szSocketServerIP));
            strncpy(g_stWebLinkConfig.szSocketServerIP, valueStr,
                    sizeof(g_stWebLinkConfig.szSocketServerIP) - 1U);
            LOG(LOG_INFO, "SocketServerIP is %s", g_stWebLinkConfig.szSocketServerIP);
        }
        if (strcasecmp(keyStr, "SocketServerPort") == 0) {
            g_stWebLinkConfig.u16SocketServerPort = (uint16_t)atoi(valueStr);
            LOG(LOG_INFO, "SocketServerPort is %u",
                (unsigned)g_stWebLinkConfig.u16SocketServerPort);
        }
    }
    fclose(pFile);
    return true;
}

/*
*函数名称:OpenUart
*函数功能:打开串口
*输   入:uint8_t u8PortSeri  串口序号   int baudrate    波特率
*输   出:无
*返   回:串口句柄
*/
int OpenUart(uint8_t u8PortSeri,int baudrate)
{
    //struct Uart_Param UartList[6];
    char * UartName[MAX_UART_NUM] = {"/dev/ttyS1","/dev/ttyS2","/dev/ttyS3","/dev/ttyS4","/dev/ttyS5","/dev/ttyS6"};
    int fd = open(UartName[u8PortSeri],O_RDWR | O_NOCTTY);//| O_NDELAY
    if(-1 == fd)
    {
        perror("open uart error");
        LOG(LOG_ERROR,"open uart error",errno);
        return ERROR;
    }
    g_stDevicelist.UartList[u8PortSeri].i32fd = fd;
    	
    return fd;
}

/*
*函数名称:SetUartProperties
*函数功能:设置串口属性
*输   入:uint8_t u8PortSeri  串口序号   int baudrate    波特率
*输   出:无
*返   回:true - 设置成功   false - 设置失败
*/
int SetUartProperties (int32_t fd, uint32_t baudrate)//
{
    int status;
    speed_t Set_speed = 0;
    struct termios option;
    //printf("set fd is %d,baudrate is %d\n",fd,baudrate);
    memset(&option,0,sizeof (option));
    tcgetattr(fd, &option); 
    switch(baudrate)
    {
        case 1200:
        Set_speed = B1200;
        break;
    case 1800:
        Set_speed = B1800;
        break;
    case 2400:
        Set_speed = B2400;
        break;
    case 4800:
        Set_speed = B4800;
        break;
    case 9600:
        Set_speed = B9600;
        break;
    case 19200:
        Set_speed = B19200;
        break;
    case 38400:
        Set_speed = B38400;
        break;
    case 57600:
        Set_speed = B57600;
        break;
    case 115200:
        Set_speed = B115200;
        break;
    case 230400:
        Set_speed = B230400;
        break;
    default: // 默认配置为 9600
        Set_speed = B9600;
        printf("default baud rate: 9600\n");
        break;
    }
	cfsetispeed(&option, Set_speed); // 设置输入波特率
	cfsetospeed(&option, Set_speed); // 设置输出波特率

    option.c_cflag |= CLOCAL;	//本地连接
	option.c_cflag |= CREAD;	// 启用接收器
	option.c_cflag &= ~CRTSCTS; // 禁用硬件流控
    option.c_iflag &= ~(ICRNL | IXON);//解决特定字符收不到的问题，比如0x11,0x13
    option.c_cflag &= ~PARENB;  // 禁用奇偶校验

    option.c_cflag &= ~CSTOPB;  //~1位停止位

    option.c_cflag &= ~CSIZE;  //屏蔽字符大小位 

    option.c_cflag |= CS8;   //8个数据位 


	option.c_oflag &= ~OPOST;						   // 设置输出模式
	option.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 设置输入模式

	option.c_cc[VTIME] = 1; /// 100ms
	option.c_cc[VMIN] =  0;	//0 byte wait

    tcflush(fd,TCIOFLUSH);
    status = tcsetattr(fd, TCSANOW, &option);

    if (status < 0)
    {
        LOG(LOG_ERROR,"tcsetattr error:%s", strerror(errno));       
        return ERROR;
 
    }

    status  =  tcgetattr(fd,&option);
    if (status != 0)
    {
        LOG(LOG_ERROR,"tcsetattr error:%s", strerror(errno));
        return ERROR;
 
    } 
    return true;

}



/*
*函数名称:InitAllUart
*函数功能:初始化所有串口，开启串口接收线程
*输   入:无
*输   出:无
*返   回:true - 成功   false - 失败
*/
int InitAllUart()
{
    int i = 0;
    int ret = SUCCESS;
    
    for(i = 0;i< MAX_UART_NUM;i++)
    {
        ret = OpenUart(i,g_stDevicelist.UartList[i].uartpara.Baudrate);
        if(-1 ==  ret)
        {
            printf("Uart %d Open Error\n",i);
            return ERROR;
        }

        
        SetUartProperties(ret,g_stDevicelist.UartList[i].uartpara.Baudrate);
        /* HOST(DTU) 需容纳 OTA 单帧(~2KB+)，其它口保持原大小 */
        if (i == g_stDevicelist.HOST_COMPUTER) {
            g_stDevicelist.UartList[i].ringfifo = kfifo_init(16384);
        } else {
            g_stDevicelist.UartList[i].ringfifo = kfifo_init(MAX_FILE_LEN);
        }
        printf("Uart %d open success!,fd is %d,baundate is %d\n",i+1,g_stDevicelist.UartList[i].i32fd,g_stDevicelist.UartList[i].uartpara.Baudrate);
    }
    startUartReceiveThread(&g_stDevicelist);
    return ret;
}

/*
*函数名称:InitUartTask
*函数功能:初始化串口任务线程
*输   入:无
*输   出:无
*返   回:true - 成功   false - 失败
*/
int InitUartTask()
{
    int i = 0;
    int ret = 0;
    
    for( i = 0;i< MAX_UART_NUM;i++)
    {
        pthread_mutex_init(&g_lock[i], NULL);
        struct TaskItem* pTask = (struct TaskItem*)malloc(sizeof(struct TaskItem) * MAX_ITEM_NUM);
        g_stDevicelist.UartList[i].taskManager.mutex = &g_lock[i];
        InitListManager(MAX_ITEM_NUM,sizeof(struct TaskItem),pTask,&g_stDevicelist.UartList[i].taskManager); 
        ret = StartDealTaskThread(&g_stDevicelist.UartList[i]);//启动串口任务线程

    }
    return ret;

}


int InitWatchDog()
{

    int timeout = 14;  // 超时时间（秒）
    // 打开看门狗设备
    if ((g_WatchDogFd = open(WATCHDOG_DEV, O_RDWR)) < 0) 
    {
        LOG(LOG_ERROR,"Failed to open watchdog");
        exit(EXIT_FAILURE);
    }

    // 设置超时时间
    if (ioctl(g_WatchDogFd, WDIOC_SETTIMEOUT, &timeout) != 0) 
    {
        LOG(LOG_ERROR,"Failed to set timeout");
        close(g_WatchDogFd);
        exit(EXIT_FAILURE);
    }
    LOG(LOG_DEBUG,"Watch dog init success,fd is %d",g_WatchDogFd);
    return g_WatchDogFd;

}

void FeedWatchDog(void *arg)
{
    // 检查 g_WatchDogFd 是否有效
    if (g_WatchDogFd < 0) {
        LOG(LOG_ERROR, "Invalid WatchDog file descriptor");
        return;
    }

    char buf = '1';
    ssize_t ret = write(g_WatchDogFd, &buf, 1);

    // 检查 write 是否成功
    if (ret != 1) {
        LOG(LOG_ERROR, "Failed to feed WatchDog: %s", strerror(errno));
        return;
    }

    LOG(LOG_INFO, "WATCH DOG FEED");
}


/*
*函数名称:InitDevice
*函数功能:初始化设备
*输   入:无
*输   出:无
*返   回:true - 成功   false - 失败
*/
int InitDevice()
{
    
    if(!ReadConfig())
    {
        return ERROR;
    }
    if(ERROR == InitAllUart())
    {
        return ERROR;
    }
    if(ERROR == InitUartTask())
    {
        return ERROR;
    }
    
    
    return SUCCESS;
}

void GlobalInit()
{
	g_pstAutoNCSList  =  initLinkList();
	g_pstAutoMudsList =  initLinkList();
	g_pstAutoADCPList =  initLinkList();
	g_pstAutoRotatorList = initLinkList();
	memset(&g_stSensorFlag,0,sizeof(SensorFlag));
    memset(&g_stErrorState,1,sizeof(ErrorState));
    g_MultiFrameFifo= kfifo_init(MAX_SOFTWARE_SIZE);//初始化g_MultiFrameFifo
}
/*
*函数名称:InitMeasureConfig
*函数功能:测量参数初始化，包括断面参数、缆道参数、自动测量参数
*输   入:无
*输   出:无
*返   回:true - 成功   false - 失败
*/
int InitMeasureConfig()
{
    int ret = ERROR;
    int i = 0;
	ret |= ReadSectionPara_new(&g_stSection_new);
	ret |= ReadCablewayPara(&g_stSetCableway);
	ret |= ReadAutoMeasurePara(&g_stAutoMeasure);
    ret |= ReadRotatorConfig(&g_stRotatorConfig);
    for(i = 0;i< 24;i++)
    {
        g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i] = 0x0018 ;
    }
    /* for(i = 0;i<24 ;i++)
    {
        printf("au16AutoMeaTime[%d] is %04x\n",i,g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i]);
    } */
    g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);//获取当前测量模式
    /* for(i = 0;i<24 ;i++)
    {
        printf("au16AutoMeaTime[%d] is %04x\n",i,g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i]);
    } */
    ResetCarEncoderPosition();
    mSleep(500);
    ret |= ResetEncoder(g_stDeviceAddr.u8Encoder_fish);
	GlobalInit();
    ResetRotatorValue(NULL);
    g_EncoderResetFlag = 4;
    InitCarHoriPositionAfterReset();
    LOG(LOG_DEBUG,"Car Encoder dm is %d,fish Encoder dm is %d",g_stSetCableway.stSetCablewayBody.u16HoriEncoderDm,g_stSetCableway.stSetCablewayBody.u16VeriEncoderDm);
    g_CarEncoderDistence = GenerateEncoderDistence( (float)(g_stSetCableway.stSetCablewayBody.u16HoriEncoderDm) /10);//  * 32 / 22 ;//下发的参数直径为毫米，计算其他距离单位为厘米
    g_FishEncoderDistence = GenerateEncoderDistence( (float)(g_stSetCableway.stSetCablewayBody.u16VeriEncoderDm) / 10) ; //下发的参数直径为毫米，计算其他距离单位为厘米
    LOG(LOG_DEBUG,"g_CarEncoderDistence is %.2f,g_FishEncoderDistence is %.2f",g_CarEncoderDistence,g_FishEncoderDistence);
    g_stVeroDIStatusFlag.i32SetRPM = GetFishRunRpm();//铅鱼设定转速
    g_stHoriDIStatusFlag.i32SetRPM = GetCarRunRpm();//小车设定转速
    SyncManuSpeedLevelFromCableway();

    LOG(LOG_DEBUG,"car set rpm is %d . fish set rpm is %d, manu level=%u",
        g_stHoriDIStatusFlag.i32SetRPM, g_stVeroDIStatusFlag.i32SetRPM,
        (unsigned)GetManuSpeedLevel());

	return ret;
}




