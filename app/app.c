#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define MAX_PKT_LEN (4)

//#define MMAP
int main(void) {
	int output[5] = {5, 4, 3, 2, 1};
	#ifdef MMAP
	int fd;
	int *p;
	fd = open("/dev/fpmult", O_RDWR | O_NDELAY);
	if(fd < 0) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}
	write(fd, "0x40000000, 0x40400000 ", strlen("0x40000000, 0x65400000 "));	
	p = (int *)mmap(0, MAX_PKT_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd , 0);
//	memcpy(output[0], p, MAX_PKT_LEN);
	munmap(p, MAX_PKT_LEN);
	printf("	REZULTAT JE: %#x\n", output[0]);
	close(fd);
	if(fd < 0) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}
	#else
	FILE *fp;
	fp = fopen("/dev/fpmult","a");
	if(fp == NULL) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}
	fprintf(fp, "0x12344000, 0x45600000 ");	
	fprintf(fp, "0x24450000, 0x40407866 ");	
	fprintf(fp, "0x43440000, 0x75680870 ");	
	fprintf(fp, "0x56770000, 0x86650000 ");	
	fprintf(fp, "0x56000000, 0x46770000 ");	

	fclose(fp);
	if(fp == NULL) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}
	#endif
	return 0;
}



