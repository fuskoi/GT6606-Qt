#include <stdio.h>
#include <linux/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define KEYPRESSTIME	30

int main(int argc, char *argv[])
{
	int fd;
  int key_flag = 0;

	fd=open("/dev/key", O_RDWR);	
	if(fd<0)
	{
  	printf("key open error");
  	printf("\n");
  	exit(1);
	}
  

	while(1)
	{
		usleep(100000);

		if(ioctl(fd, 0, 0)){
			if(key_flag == 0){
				key_flag = 1;
				printf("key press down!\n");
			}
		}
		else{
			if(key_flag == 1){
				key_flag = 0;
				printf("key press up!\n");
			}
		}
	}

  exit(0);
}

