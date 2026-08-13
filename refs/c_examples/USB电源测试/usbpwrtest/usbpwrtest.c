#include <stdio.h>
#include <linux/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdbool.h>

char usbpwr = 0x0f;

char Hex2ASCII(char data)
{
	if(data < 10) return (data+0x30);
	else return (data-10+0x41);
}

char usbPowerSet(bool port1, bool port2, bool port3, bool port4) //true: 打开电源 flase: 关闭电源
{
	if(port1)	usbpwr |= 0x01;
	else usbpwr &= ~0x01;
	
	if(port2)	usbpwr |= (0x01 << 1);
	else usbpwr &= ~(0x01 << 1);
		
	if(port3)	usbpwr |= (0x01 << 2);
	else usbpwr &= ~(0x01 << 2);
		
	if(port4)	usbpwr |= (0x01 << 3);
	else usbpwr &= ~(0x01 << 3);
		
	usbpwr &= 0x0f;
	
	return Hex2ASCII(usbpwr);
}

int main(int argc, char *argv[])
{
	int fd;
	char buf;

	fd=open("/dev/usbpwr", O_RDWR);	
	if(fd<0)
	{
		printf("usbpwr open error\n");
		exit(1);
	}
	
	buf = usbPowerSet(false,false,false,false);
	write(fd,&buf,1);
	sleep(2);
	
	buf = usbPowerSet(true,true,true,true);
	write(fd,&buf,1);
	
	close(fd);

	exit(0);
}

