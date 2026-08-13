
#include "Common.h"
#include "log.h"
#define is_power_of_2(x) ((x) != 0&& (((x)&((x)-1)) == 0))		 //判断缓冲区大小是不是2的幂次方	
#define min(a,b) (((a) < (b)) ? (a) : (b))




/*
*函数名称:kfifo_init
*函数功能:环形缓冲区初始化
*输    入: unsigned int size  缓冲区大小
*输   出:无
*返   回:kfifo* 环形缓冲区指针
*/
struct kfifo* kfifo_init(unsigned int size)
{
	int errno;
	struct kfifo *fifo = NULL;
	if(!is_power_of_2(size))										
	{
		fprintf(stderr,"fifo size must be power of 2.\n");			
		return fifo;
	}
	fifo = (struct kfifo *)malloc(sizeof(struct kfifo));			
	if(fifo==NULL)
	{
		fprintf(stderr,"failed to malloc fifo struct memeroy,errno:%u,reason:%s\n",errno,strerror(errno));
		return fifo;
	}
	memset(fifo,0,sizeof(struct kfifo));
	fifo->buffer = (unsigned char *)malloc(size);					
	if(fifo->buffer==NULL)
	{
		fprintf(stderr,"failed to malloc fifo data memeroy,errno:%u,reason:%s\n",errno,strerror(errno));
		free(fifo);
		return NULL;
	}
	fifo->size = size;
	fifo->in = 0;
	fifo->out = 0;
	return fifo;
}

/*
*函数名称:kfifo_put
*函数功能:向环形缓冲区写入数据
*输   入: struct kfifo *fifo  缓冲区指针  unsigned char *buffer 需要写入的数据  unsigned int len  写入数据的大小（字节）
*输   出:无
*返   回:写入缓冲区的数据长度
*/
unsigned int kfifo_put(struct kfifo *fifo, unsigned char *buffer, unsigned int len)
{
	unsigned int l;														

	len = min(len, fifo->size - fifo->in + fifo->out);					
	__sync_synchronize();												

	l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));			
	memcpy(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l);	

    
	memcpy(fifo->buffer, buffer + l, len - l);							
	__sync_synchronize();

	fifo->in += len;

	return len;
}

/*
*函数名称:kfifo_put
*函数功能:向环形缓冲区写入数据
*输   入:unsigned char *buffer 需要写入的数据  unsigned int len  写入数据的大小（字节）
*输   出:struct kfifo *fifo  缓冲区指针
*返   回:写入缓冲区的数据长度
*/
unsigned int kfifo_get(struct kfifo *fifo, unsigned char *buffer, unsigned int len)

{
	unsigned int l;														
    len = min(len, fifo->in - fifo->out);								
	__sync_synchronize();												

	l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));			
	memcpy(buffer,fifo->buffer + (fifo->out & (fifo->size - 1)), l);	

	memcpy(buffer + l,fifo->buffer , len - l);							
	__sync_synchronize();

	fifo->out += len;

	return len;
}


/*
*函数名称:kfifo_preview
*函数功能:不出队的情况下查看缓冲区中的数据
*输   入:struct kfifo *fifo  缓冲区指针  unsigned int len  需要查看的数据的大小（字节）
*输   出: unsigned char *buffer 查看到的数据
*返   回:获取到的数据长度
*/
unsigned int kfifo_preview(struct kfifo *fifo, unsigned char *buffer, unsigned int len)

{
	unsigned int l;														

    len = min(len, fifo->in - fifo->out);								

	__sync_synchronize();												
	l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));			
	memcpy(buffer,fifo->buffer + (fifo->out & (fifo->size - 1)), l);	

	memcpy(buffer + l,fifo->buffer , len - l);							
	__sync_synchronize();
	
	return len;
}

/*
*函数名称:kfifo_len
*函数功能:获取缓冲区的数据长度
*输   入:struct kfifo *fifo  缓冲区指针
*输   出:无
*返   回:缓冲区数据长度
*/
unsigned int kfifo_len(struct kfifo *fifo)
{
	unsigned int uLen = 0;
	uLen = fifo->in	 - fifo->out;
	//printf("kfifo_len is %d\n",uLen);
	return (uLen);
}

/*
*函数名称:kfifo_lenLeft
*函数功能:缓冲区剩余数据数据长度
*输   入:struct kfifo *fifo  缓冲区指针
*输   出:无
*返   回:缓冲区数据长度
*/
unsigned int kfifo_lenLeft(struct kfifo *fifo)
{
	unsigned int uLeft = 0;
	uLeft = fifo->size - fifo->in + fifo->out;
	//printf("kfifo_lenLeft is %d\n",kfifo_lenLeft);
	return (uLeft);
}

/*
*函数名称:kfifo_clear
*函数功能:清除缓冲区
*输   入:struct kfifo *fifo  缓冲区指针
*输   出:无
*返   回:无
*/
void kfifo_clear(struct kfifo *fifo)
{
	fifo->in=fifo->out=0 ;
}

void kfifo_free(struct kfifo *fifo)
{
	if(fifo)
	{
		if(fifo->buffer)
		{
			free(fifo->buffer);
			fifo->buffer = NULL;
		}
		free(fifo);
		fifo = NULL;
	}
}

/*
*函数名称:uartFindFrameHead
*函数功能:查找帧头
*输   入:KFIFO_ID fifo    要查找的缓冲区  uint8_t framehead1   帧头第1个字节   uint8_t framehead2 帧头第2个字节  
*输   出:无
*返   回:true - 找到相匹配的帧头   false - 没找到匹配的帧头
*/
bool uartFindFrameHead(KFIFO_ID fifo,uint8_t framehead1,uint8_t framehead2)
{
	bool flag = false;
	unsigned char ui8_head[2];

	while (kfifo_len(fifo) > 4)
	{
		kfifo_preview(fifo, ui8_head, 2); 
		if ((ui8_head[0] == framehead1) && (ui8_head[1] == framehead2))
		{
			flag = true;
			break; 
		}
		else
		{
			kfifo_get(fifo, ui8_head, 1); 
		}
	}
	return flag;
}
uint8_t uartFindDeviceType(KFIFO_ID fifo,uint8_t type1head1,uint8_t type1head2,uint8_t type2head1,uint8_t type2head2)
{
	uint8_t  DeviceType = 0;
	unsigned char ui8_head[2];


	while (kfifo_len(fifo) > 4)
	{
		kfifo_preview(fifo, ui8_head, 2); 
		LOG(LOG_INFO,"ui8_head");
		LOG_ARRAY(LOG_INFO,ui8_head,2,"%02x");
		if ((ui8_head[0] == type1head1) && (ui8_head[1] == type1head2))
		{
			DeviceType = 1;
			break; 
		}
		else if ((ui8_head[0] == type2head1) && (ui8_head[1] == type2head2))
		{
			DeviceType = 2;
			break; 
		}
		else
		{
			kfifo_get(fifo, ui8_head, 1); 
		}
	}
	return DeviceType;
}


uint8_t uartFindMultiDevice(KFIFO_ID fifo,kfifo_head stfifohead)
{
	uint8_t  DeviceType = 0;
	unsigned char ui8_head[2];

	while (kfifo_len(fifo) > 4)
	{
		kfifo_preview(fifo, ui8_head, 2); 
		LOG(LOG_INFO,"ui8_head");
		LOG_ARRAY(LOG_INFO,ui8_head,2,"%02x");
		if ((ui8_head[0] == stfifohead.u8Head1_1) && (ui8_head[1] == stfifohead.u8Head1_2))
		{
			DeviceType = 1;
			break; 
		}
		else if ((ui8_head[0] == stfifohead.u8Head2_1) && (ui8_head[1] == stfifohead.u8Head2_2))
		{
			DeviceType = 2;
			break; 
		}
		else if((ui8_head[0] == stfifohead.u8Head3_1) && (ui8_head[1] == stfifohead.u8Head3_2))
		{
			DeviceType = 3;
			break; 
		}
		else if((ui8_head[0] == stfifohead.u8Head4_1) && (ui8_head[1] == stfifohead.u8Head4_2))
		{
			DeviceType = 4;
			break; 
		}
		else
		{
			kfifo_get(fifo, ui8_head, 1); 
		}
	}
	return DeviceType;
}