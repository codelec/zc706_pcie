#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include "unistd.h"
#include "inttypes.h"
#include <sys/stat.h>
#include <sys/types.h>
#define MKDEV(ma,mi)  (((ma) << 20) | (mi))

#define BAR0_S_AXI_CDMA_CDMACR 0x8000
#define BAR0_S_AXI_CDMA_CDMASR 0x8004
#define BAR0_S_AXI_CDMA_CURDESC_PTR 0x8008
#define BAR0_S_AXI_CDMA_CURDESC_PTR_MSB 0x800C
#define BAR0_S_AXI_CDMA_TAILDESC_PTR 0x8010
#define BAR0_S_AXI_CDMA_TAILDESC_PTR_MSB 0x8014
#define BAR0_S_AXI_CDMA_SA 0x8018
#define BAR0_S_AXI_CDMA_SA_MSB 0x801C
#define BAR0_S_AXI_CDMA_DA 0x8020
#define BAR0_S_AXI_CDMA_DA_MSB 0x8024
#define BAR0_S_AXI_CDMA_BTT 0x8028

#define BAR0_S_AXI_PCIE_AXIBAR0 0x920C
#define BAR0_S_AXI_PCIE_AXIBAR0_MSB 0x9208
#define BAR0_S_AXI_PCIE_AXIBAR1 0x9214
#define BAR0_S_AXI_PCIE_AXIBAR1_MSB 0x9210

#define CDMA_S_AXI_PCIE_AXIBAR0 0x4100920C
#define CDMA_S_AXI_PCIE_AXIBAR0_MSB 0x41009208
#define CDMA_S_AXI_PCIE_AXIBAR1 0x41009214
#define CDMA_S_AXI_PCIE_AXIBAR1_MSB 0x41009210

#define CDMA_DESC_NEXTDESC_PTR 0x00
#define CDMA_DESC_NEXTDESC_PTR_MSB 0x04
#define CDMA_DESC_SA 0x08
#define CDMA_DESC_SA_MSB 0x0C
#define CDMA_DESC_DA 0x10
#define CDMA_DESC_DA_MSB 0x14
#define CDMA_DESC_CONTROL 0x18
#define CDMA_DESC_STATUS 0x1C

#define BAR0_BRAM 0x0000
#define BAR0 64*1024
#define AXIBAR0 64*1024
#define AXIBAR1 8*1024*1024

void dma_engine_sg_init(uint32_t *bar0,int num_desc)
{
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00000004; // Soft reset cdma 
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00001000;//clear any previous ioc interrupt
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00004000;//clear any previous err interrupt
	temp = bar0[BAR0_S_AXI_CDMA_CDMACR/4];
	temp = temp | 0x00005008;// enable sg mode and ioc or err interrupt generation 
	temp = temp & 0xFF00FFFF;
	temp = temp | (num_desc << 16);
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = temp;
}

void dma_engine_dma_init()
{
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00000004; // Soft reset cdma 
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00001000;//clear any previous ioc interrupt
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00004000;//clear any previous err interrupt
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00005000;
}

void build_store_desc(uint32_t *store_desc_at,uint32_t nxt,uint32_t sa,uint32_t da,uint32_t btt)
{
	store_desc_at[CDMA_DESC_NEXTDESC_PTR_MSB/4]=0;
	store_desc_at[CDMA_DESC_DA_MSB/4]=0;
	store_desc_at[CDMA_DESC_SA_MSB/4]=0
	store_desc_at[CDMA_DESC_STATUS/4]=0;
	store_desc_at[CDMA_DESC_NEXTDESC_PTR/4]=nxt;
	store_desc_at[CDMA_DESC_DA/4]=da;
	store_desc_at[CDMA_DESC_SA/4]=sa;
	store_desc_at[CDMA_DESC_CONTROL/4]=btt;
}
//count in terms of bytes 
void print_data(uint32_t *offset,uint32_t base,int count)
{
	int i;
	for (i = 0x0; i <= count; i+=4)
		printf("%08x : %08x\n",base + i,offset[i/4]);
}

void print_result_not_completed(uint32_t *first_desc_start,uint32_t base,short num)
{
	int i;
	for (i = 0x1C; i <= num; i+=0x40)
		if(first_desc_start[i/4] != 0x80000000)
			printf("%08x : %08x\n",base + i,first_desc_start[i/4]);
}

void start_sg(uint32_t *bar0,uint32_t first_desc_start,short num)
{
	bar0[BAR0_S_AXI_CDMA_CURDESC_PTR/4]=first_desc_start;
	bar0[BAR0_S_AXI_CDMA_CURDESC_PTR_MSB/4]=0x0;
	bar0[BAR0_S_AXI_CDMA_TAILDESC_PTR/4]=first_desc_start + ((num - 1) * 64);
	bar0[BAR0_S_AXI_CDMA_TAILDESC_PTR_MSB/4]=0x0;
}

void start_dmaa(uint32_t *bar0,uint32_t sa,uint32_t da ,uint32_t btt)
{
	bar0[BAR0_S_AXI_CDMA_SA/4] = sa;
	bar0[BAR0_S_AXI_CDMA_SA_MSB/4] = 0;
	bar0[BAR0_S_AXI_CDMA_DA/4] = da;
	bar0[BAR0_S_AXI_CDMA_DA_MSB/4] = 0;
	bar0[BAR0_S_AXI_CDMA_BTT/4] = btt;
}

int main(int argc, char const *argv[])
{
	int i,f1,f2,f;
	uint32_t off_f = 0,off_f1 = 0,off_f2 = 0;
	volatile uint32_t *f1mem,*f2mem,*bmem;
 	dev_t dev1 = MKDEV(244,3),dev2 = MKDEV(244,4);
	double cpu_time;
	uint32_t temp = 0;
	f = open("/dev/my_pci0",O_RDWR);
	if(f<0){
		printf("Can't open the file /dev/my_pci0\n");
		exit(1);
	}
	bmem = mmap (0,BAR0,PROT_WRITE,MAP_SHARED,f,0x0);
	
	f1 = open("/dev/my_pci1",O_RDWR);
	if(f1<0){
		if(!mknod("/dev/my_pci1",S_IFCHR,dev1)){
			f1 = open("/dev/my_pci1",O_RDWR);
			goto opened;
		}
		printf("Can't open the file /dev/my_pci1\n");
		exit(1);
	}
opened:	
	f2 = open("/dev/my_pci2",O_RDWR);
	if(f2<0){
		if(!mknod("/dev/my_pci2",S_IFCHR,dev2)){
			f2 = open("/dev/my_pci2",O_RDWR);
			goto opened1;
		}
		printf("Can't open the file /dev/my_pci2\n");
		exit(1);
	}
opened1:
	f1mem = mmap (0,AXIBAR0,PROT_WRITE,MAP_SHARED,f1,0x0);
	if(f1mem == NULL){
		printf("mmap /dev/my_pci1 returned NULL\n");
		exit(1);
	}
	f2mem = mmap (0,AXIBAR1,PROT_WRITE,MAP_SHARED,f2,0x0);
	if(f2mem == NULL){
		printf("mmap /dev/my_pci2 returned NULL\n");
		exit(1);
	}

//////////////////////////// CUSTOM LOGIC ////////////////////////

////////////////////////////	          ////////////////////////

	close (f2);
	close (f1);
	close (f);
	return 0;
}
