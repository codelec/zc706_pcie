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
	volatile uint32_t *bmem = mmap (0,0x10000,PROT_WRITE,MAP_SHARED,f,0x0);
	// m[0x8004/4] = IOC_SET;
	// temp = m[0x8000/4];
	// m[0x8000/4] = temp | IOC_SET;
	// m[0x8018/4]=0x30000030;
	// m[0x801C/4]=0x00000000;
	// m[0x8020/4]=0x30000040;
	// m[0x8024/4]=0x00000000;
	// m[0x8028/4]=0x00000009;
	// printf("%x\n",m[0x920C/4]);
	// printf("%x\n",m[0x9208/4]);
	// printf("%x\n",m[0x0020/4]);
	// printf("%x\n",m[0x0024/4]);
	// printf("%x\n",m[0x0028/4]);
	
	// m[0x8018/4]=0x40000000;
	// m[0x801C/4]=0x00000000;
	// m[0x8020/4]=0x41000020;
	// m[0x8024/4]=0x00000000;
	// m[0x8028/4]=0x0000000A;

	// printf("%x\n",m[0x0000/4]);
	// printf("%x\n",m[0x0004/4]);
	// printf("%x\n",m[0x0008/4]);
	// printf("%x\n",m[0x0020/4]);
	// printf("%x\n",m[0x0024/4]);
	// printf("%x\n",m[0x0028/4]);

	////////////////////////////
	bmem[0x8000/4] = bmem[0x8000/4] | 0x00000004;
	bmem[0x8004/4] = 0x00001000;
	bmem[0x8004/4] = 0x00004000;
	temp = bmem[0x8000/4];
	temp = temp | 0x00005008;
	temp = temp & 0xFF00FFFF;
	temp = temp | 0x00020000;
	bmem[0x8000/4] = temp;

	bmem[0x4010/4]=0x00000000;
	bmem[0x4014/4]=0x41114111;
	bmem[0x4018/4]=0x51115111;
	bmem[0x401C/4]=0x61114678;


	printf("%x\n",bmem[0x4010/4]);
	printf("%x\n",bmem[0x4014/4]);
	printf("%x\n",bmem[0x4018/4]);
	printf("%x\n",bmem[0x401C/4]);
	printf("%x\n",bmem[0x4080/4]);
	printf("%x\n",bmem[0x4084/4]);
	printf("%x\n",bmem[0x4088/4]);
	printf("%x\n",bmem[0x408C/4]);

	bmem[0x0080/4]=0x820000C0;
	bmem[0x0084/4]=0x00000000;
	bmem[0x0088/4]=0x82004010;
	bmem[0x008C/4]=0x00000000;
	bmem[0x0090/4]=0x40000000;
	bmem[0x0094/4]=0x00000000;
	bmem[0x0098/4]=0x0000000F;
	bmem[0x009C/4]=0x00000000;

	bmem[0x00C0/4]=0x82000050;
	bmem[0x00C4/4]=0x00000000;
	bmem[0x00C8/4]=0x40000000;
	bmem[0x00CC/4]=0x00000000;
	bmem[0x00D0/4]=0x82004080;
	bmem[0x00D4/4]=0x00000000;
	bmem[0x00D8/4]=0x0000000E;
	bmem[0x00DC/4]=0x00000000;

	bmem[0x8008/4]=0x82000080;
	bmem[0x800C/4]=0x00000000;
	bmem[0x8010/4]=0x820000C0;
	bmem[0x8014/4]=0x00000000;

	printf("%x\n",bmem[0x4080/4]);
	printf("%x\n",bmem[0x4084/4]);
	printf("%x\n",bmem[0x4088/4]);
	printf("%x\n",bmem[0x408C/4]);
	printf("9C:%x\n",bmem[0x009C/4]);
	printf("DC:%x\n",bmem[0x00DC/4]);


	return 0;
}