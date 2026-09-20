#ifndef _RINGFIFO_H
#define _RINGFIFO_H

#include <string.h>

#pragma pack(push, 1)


typedef struct kfifo {
    unsigned char *buffer;		/**/
    unsigned int size;			/* */
    unsigned int in;			/*  */
    unsigned int out;			/* */
}*KFIFO_ID;

typedef struct _kfifo_head {
    uint8_t u8Head1_1;
    uint8_t u8Head1_2;
    uint8_t u8Head2_1;
    uint8_t u8Head2_2;
    uint8_t u8Head3_1;
    uint8_t u8Head3_2;
    uint8_t u8Head4_1;
    uint8_t u8Head4_2;
    
}kfifo_head;

#pragma pack(pop)
struct kfifo* kfifo_init(unsigned int size);
unsigned int kfifo_put(struct kfifo *fifo, unsigned char *buffer, unsigned int len);
unsigned int kfifo_get(struct kfifo *fifo, unsigned char *buffer, unsigned int len);
unsigned int kfifo_preview(struct kfifo *fifo, unsigned char *buffer, unsigned int len);
unsigned int kfifo_len(struct kfifo *fifo);
unsigned int kfifo_lenLeft(struct kfifo *fifo);
void kfifo_clear(struct kfifo *fifo);
void kfifo_free(struct kfifo *fifo);
bool uartFindFrameHead(KFIFO_ID fifo,uint8_t framehead1,uint8_t framehead2);
uint8_t uartFindDeviceType(KFIFO_ID fifo,uint8_t type1head1,uint8_t type1head2,uint8_t type2head1,uint8_t type2head2);
uint8_t uartFindMultiDevice(KFIFO_ID fifo,kfifo_head stfifohead);








#endif


