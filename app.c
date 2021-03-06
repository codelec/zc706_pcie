#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <sys/sysmacros.h>

/*
 *Do change the following MACROS if in case any changes are made to the design 
 */

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

//Virtual Address mentioned anywhere below refers to address or pointer returned by mmap system call 

bool conditio = false;
clock_t begin , end ;

/**
 * @brief      This function is executed when the when the user space receives the signal 
 *				from the driver (kernel) and this function is registered as the signal handler  
 *
 * @param[in]  num   The number Signal number is received as the parameter
 */
void my_action(int num)
{
	printf("Interrupt recieved by userspace \n");
	conditio = true;
	end = clock();
}

/**
 * @brief      intializes and resets the dma engine by clearing any previous 
 * 				error or ioc interrupts by writing 1 to that specific bit 
 *
 * @param      bar0      virtual address of the MMIO region of the PCIE BAR0 which is received as 
 * 						return address from the mmap call file with (MAJOR,0) assuming CDMA control 
 * 						registers is mapped in this BAR
 * @param[in]  num_desc  it takes as input the number of descriptors the SG engine
 * 						will serve just then since this is used as down counter after 
 * 						which IOC interrupt is raised 
 */
void dma_engine_sg_init(uint32_t *bar0,int num_desc)
{
	uint32_t temp ;
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00000004; // Soft reset cdma 
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00001000;//clear any previous ioc interrupt
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00004000;//clear any previous err interrupt
	temp = bar0[BAR0_S_AXI_CDMA_CDMACR/4];
	temp = temp | 0x00005008;// enable sg mode and ioc or err interrupt generation 
	temp = temp & 0xFF00FFFF;
	temp = temp | (num_desc << 16);
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = temp;
}

/**
 * @brief      Initializes the CDMA for the Simple DMA mode by soft reset and clearing any previous interrupts
 *
 * @param      bar0  virtual address of the MMIO region of the PCIE BAR0
 */
void dma_engine_dma_init(uint32_t *bar0)
{
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00000004; // Soft reset cdma 
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00001000;//clear any previous ioc interrupt
	bar0[BAR0_S_AXI_CDMA_CDMASR/4] = 0x00004000;//clear any previous err interrupt
	bar0[BAR0_S_AXI_CDMA_CDMACR/4] = bar0[BAR0_S_AXI_CDMA_CDMACR/4] | 0x00005000;
}

/**
 * @brief      Builds and stores descriptor at the appropriate position 
 *
 * @param      store_desc_at  Virtual address of the desired region(MMIO BAR or RAM Buffer) where 
 * 								the descriptors are to be stored from where the CDMA SG port will be 
 * 								fetching them (Storing it in RAM buffer has been found to be a better approach from experiments)
 * @param[in]  nxt            Address of the next descriptor it should be an address in 32 bit AXI address 
 * 								Space which is accessible by the Master SG port of CDMA engine
 * @param[in]  sa             AXI address Space' Source Addresss from where the data is to be fetched 
 * @param[in]  da             AXI address Space' Destination Addresss from where the data is to be stored 
 * @param[in]  btt            Bytes to transfer
 */
void build_store_desc(uint32_t *store_desc_at,uint32_t nxt,uint32_t sa,uint32_t da,uint32_t btt)
{
	store_desc_at[CDMA_DESC_NEXTDESC_PTR_MSB/4]=0;
	store_desc_at[CDMA_DESC_DA_MSB/4]=0;
	store_desc_at[CDMA_DESC_SA_MSB/4]=0;
	store_desc_at[CDMA_DESC_STATUS/4]=0;
	store_desc_at[CDMA_DESC_NEXTDESC_PTR/4]=nxt;
	store_desc_at[CDMA_DESC_DA/4]=da;
	store_desc_at[CDMA_DESC_SA/4]=sa;
	store_desc_at[CDMA_DESC_CONTROL/4]=btt;
}

// @param      offset  Virtual address offset starting from where the data is to printed 
// @param[in]  base    AXI address Space' address of where the data is to be printed just used for printing purposes
// @param[in]  count   count in terms of bytes
//
void print_data(uint32_t *offset,uint32_t base,int count)
{
	int i;
	for (i = 0x0; i < count; i+=4)
		printf("%08x : %08x\n",base + i,offset[i/4]);
}

/**
 * @brief      After every descriptor is completed SG engine writes 0x80000000 to Status offset 
 * 				of that descriptor or if not then a valid error as shown in Xilinx Manual of AXI CDMA 
 * 				So this function will print those address whose descriptor was not successfully completed
 * 				along with the corresponding data that is stored there by SG engine 
 *
 * @param      first_desc_start  Virtual Address of the first descriptor
 * @param[in]  base              As above
 * @param[in]  num               Number of descriptors of whose 
 */
void print_result_not_completed(uint32_t *first_desc_start,uint32_t base,short num)
{
	int i;
	for (i = 0x1C; i <= num; i+=0x40)
		if(first_desc_start[i/4] != 0x80000000)
			printf("%08x : %08x\n",base + i,first_desc_start[i/4]);
}



/**
 * @brief     Function to fill first descriptor AXI address to the appropriate register
 * 
 *
 * @param      bar0              As used in the above functions 
 * @param[in]  first_desc_start  AXI address of the first descriptor in the SG chain 
 * @param[in]  num               Number of such descriptors
 */
void start_sg(uint32_t *bar0,uint32_t first_desc_start,short num)
{
	bar0[BAR0_S_AXI_CDMA_CURDESC_PTR/4]=first_desc_start;
	bar0[BAR0_S_AXI_CDMA_CURDESC_PTR_MSB/4]=0x0;
	bar0[BAR0_S_AXI_CDMA_TAILDESC_PTR/4]=first_desc_start + ((num - 1) * 64);
	bar0[BAR0_S_AXI_CDMA_TAILDESC_PTR_MSB/4]=0x0;
}

/**
 * @brief      Starts a Simple DMA along with some details 
 *
 * @param      bar0  As used in the above functions
 * @param[in]  sa    AXI address Space' Source Addresss from where the data is to be fetched 
 * @param[in]  da    AXI address Space' Destination Addresss from where the data is to be stored 
 * @param[in]  btt   Bytes to transfer
 */
void start_dma(uint32_t *bar0,uint32_t sa,uint32_t da ,uint32_t btt)
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
	float sizee = 0;
	FILE *tempf;
	uint32_t off_f = 0,off_f1 = 0,off_f2 = 0;
	volatile uint32_t *f1mem,*f2mem,*bmem;
	uint64_t f1mem_phy , f2mem_phy;
	dev_t dev1 = makedev(244,3);
	dev_t dev2 = makedev(244,4);
	double cpu_time;
	uint32_t temp = 0;
	f = open("/dev/my_pci0",O_RDWR);
	if(f<0){
		printf("Can't open the file /dev/my_pci0\n");
		exit(1);
	}
	bmem = mmap (0,BAR0,PROT_WRITE,MAP_SHARED,f,0x0);
	if(bmem == NULL){
		printf("mmap /dev/my_pci0 returned NULL\n");
		exit(1);
	}
	f1 = open("/dev/my_pci1",O_RDWR);
	if(f1<0){
		if(!mknod("/dev/my_pci1",S_IFCHR | 0660,dev1)){
			f1 = open("/dev/my_pci1",O_RDWR);
			if(f1 > 0)
				goto opened;
		}
		printf("Can't open the file /dev/my_pci1\n");
		exit(1);
	}
opened:	
	f2 = open("/dev/my_pci2",O_RDWR);
	if(f2<0){
		if(!mknod("/dev/my_pci2",S_IFCHR | 0660,dev2)){
			f2 = open("/dev/my_pci2",O_RDWR);
			if(f2 > 0)
				goto opened;
		}
		printf("Can't open the file /dev/my_pci2\n");
		exit(1);
	}
opened1:
	tempf = fopen("/sys/class/my_class/my_pci0/size_buf0","r+");
	fprintf(tempf, "%d",AXIBAR0);
	fclose(tempf);
	tempf = fopen("/sys/class/my_class/my_pci0/size_buf1","r+");
	fprintf(tempf, "%d",AXIBAR1);
	fclose(tempf);
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
	tempf = fopen("/sys/class/my_class/my_pci0/buf0","r");
	fscanf(tempf, "%lx",&f1mem_phy);
	fclose(tempf);
	tempf = fopen("/sys/class/my_class/my_pci0/buf1","r");
	fscanf(tempf, "%lx",&f2mem_phy);
	fclose(tempf);
	tempf = fopen("/sys/class/my_class/my_pci0/reg_interrupt","r+");
	fprintf(tempf, "%d",getpid());
	fclose(tempf);
	signal(SIGUSR1,my_action);
	printf("CDMA STATUS : %x\n",bmem[BAR0_S_AXI_CDMA_CDMASR/4]);
//////////////////////////// CUSTOM LOGIC ////////////////////////



////////////////////////////	          ////////////////////////
	begin = clock();
	while(!conditio);
	sizee = (float)BAR0 / (1024*1024*1024);
	float timee = (float)(end - begin)/CLOCKS_PER_SEC;
	printf("Success:%x Bravo!!!!!! Time elapsed since inception : %.6f SPEED:%.4fGiB/s\n",bmem[BAR0_S_AXI_CDMA_CDMASR/4],timee , sizee/timee);
	close (f2);
	close (f1);
	close (f);
	return 0;
}
