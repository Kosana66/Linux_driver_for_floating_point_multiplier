#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <math.h>

#define MAX_PKT_LEN (4)

uint floatToHex(float num);
float hexToFloat(uint number);
int main(void) {
	int fd;
	int *p;

	int tmp;
	float rez;
	float br1, br2;
	uint hex1, hex2;
	char combinedStr[24];
	
	printf("Unesite dva decimalna broja u formatu br1, br2: ");
	scanf("%f, %f", &br1, &br2);
	printf("\n");
	printf("Unesen br1: %f\n", br1);
	printf("Unesen br2: %f\n", br2);
	hex1 = floatToHex(br1);
	hex2 = floatToHex(br2);
	snprintf(combinedStr, sizeof(combinedStr), "%#x, %#x;", hex1, hex2);
	
	fd = open("/dev/fpmult", O_RDWR | O_NDELAY);
	if(fd < 0) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}
	write(fd, combinedStr, strlen(combinedStr));	
	close(fd);
	if(fd < 0) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}
	fd = open("/dev/fpmult", O_RDWR | O_NDELAY);
	if(fd < 0) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}

	p = (int *)mmap(0, 4, PROT_READ, MAP_SHARED, fd , 0);
	if(p == MAP_FAILED) {
		printf("MAP FAILED from app\n");
	}
	tmp = *p;	
	munmap(p, MAX_PKT_LEN);
	rez = hexToFloat(tmp);
	printf("REZULTAT IZ APLIKACIJE: %f\n", rez);
	close(fd);
	if(fd < 0) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}


	FILE *fp;
	fp = fopen("/dev/fpmult","a");
	if(fp == NULL) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}
	fprintf(fp, "0x40000000, 0x40400000;0x45452333, 0x23410000;0x45452633, 0x25410000;0x25452633, 0x31410000; 0x25452633, 0x31410000;0x40000000, 0x40400000;" );		
	fclose(fp);
	if(fp == NULL) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}


	fp = fopen("/dev/fpmult","a");
	if(fp == NULL) {
		printf("Can't open /dev/fpmult for write\n");
		return -1;
	}
	fprintf(fp, "0x40000000, 0x40400000;0x45452333, 0x23410000;0x45452633, 0x25410000;0x25452633, 0x31410000;" );		
	fclose(fp);
	if(fp == NULL) {
		printf("Can't close /dev/fpmult for open\n");
		return -1;
	}



	return 0;
}


uint floatToHex(float num) {
	uint newNum;
	if(num == 0) {
		newNum = 0x0;
		return newNum;
	}

	int sign = (num < 0) ? 1: 0;
	int exp = 0;
	float normalizedValue = fabs(num);
	while(normalizedValue >= 2.0) {
		normalizedValue /= 2.0;
		exp++;	
	}
	while(normalizedValue < 1.0) {
		normalizedValue *= 2.0;
		exp--;	
	}
	exp += 127;
	
	uint mantissa = (uint)((normalizedValue - 1.0) * pow(2, 23));
	
	newNum = (sign << 31) | (exp << 23) | mantissa;
	return newNum;
}


float hexToFloat(uint number) {
	float newNumber;
	uint sign = (number >> 31) & 0x1; 
	uint exp = (number >> 23) & 0xFF; 
	uint mantissa = number & 0x7FFFFF;
	exp -= 127;
	
	uint exponentValue = 1;
	for(uint i = 1; i <= exp; i++) {
		exponentValue *= 2;
	}
	float mantissaValue = 1.0 + ((float)mantissa / pow(2, 23));
	newNumber = exponentValue  * mantissaValue;
	if(sign == 1) {
		newNumber *= -1;
	}
	return newNumber;	
}
