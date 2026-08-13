#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

char buf;

#define FEED_WATCHDOG	buf = '1'; write(fdw,&buf,1)	//Œππ∑
#define CLOSE_WDT buf = 'V'; write(fdw,&buf,1);close(fdw)	//πÿ±’ø¥√≈π∑


int main(int argc, char **argv)
{       

	int fdw;
	
	if((fdw=open("/dev/watchdog",O_RDWR))==-1)
	{
		printf("Can not open /dev/watchdog\n");
		exit(1);
	}

	printf("enable wdt!\n");		

	while(1){
		sleep(1);
		FEED_WATCHDOG;
	}

	printf("disable wdt!\n");
	CLOSE_WDT;      
	return 0;

}
