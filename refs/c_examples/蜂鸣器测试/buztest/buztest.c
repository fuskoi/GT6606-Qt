#include <stdio.h>
#include <linux/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

char buf;

#define BUZZER_ON	buf = '1'; write(fd,&buf,1)	//开蜂鸣器
#define BUZZER_OFF	buf = '0'; write(fd,&buf,1)	//关蜂鸣器

int main(int argc, char *argv[])
{
	int fd;

	fd=open("/dev/buzzer", O_RDWR);	
	if(fd<0)
	{
  	printf("buzzer open error");
  	printf("\n");
  	exit(1);
	}
  

	while(1)
	{
		BUZZER_ON;
		usleep(500000);
		BUZZER_OFF;
		usleep(500000);
	}

  exit(0);
}

