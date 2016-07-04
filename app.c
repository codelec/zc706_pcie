#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#define IOC_SET 0x00001000
int main(int argc, char const *argv[])
{
	uint32_t temp = 0;
	int f = open("/dev/my_pci0",O_RDWR | O_APPEND);
	if(f<0){
		printf("Can't open the file /dev/my_pci0\n");
		exit(1);
	}
	volatile uint32_t *m = mmap (0,0x10000,PROT_WRITE,MAP_SHARED,f,0x0);
	m[0x8004/4] = IOC_SET;
	temp = m[0x8000/4];
	m[0x8000/4] = temp | IOC_SET;
	m[0x8018/4]=0x30000030;
	m[0x801C/4]=0x00000000;
	m[0x8020/4]=0x30000040;
	m[0x8024/4]=0x00000000;
	m[0x8028/4]=0x00000009;
	return 0;
}