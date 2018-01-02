# zc706_pcie
## Communication Between ZC706 and Motherboard
### Goal
ZC706 and the motherboard (in our case a host platform containing a high end Intel i7-5980x on Gigabyte motherboard with 4 PCIe 3.0 slots) be able to communicate at a high data transfer speed using PCIe bus which involves the ability to initiate read and write from both the ends .
### Design
The block diagram of the custom logic that is implemented in the PL is done in manner such that the fpga does not freeze while doing high size data transfer using PCIe bus . IP's or Block's used : 

#### Zynq Processign System - processing_system_0 
This block represents the ARM CORTEX A9 present on SoC . It provides two master AXI ports using which the PS can control the interfaces connected to it through the interconnect in the PL . Using 4 Slave AXI HP0-3 ports the peripherals in the PL can access the contents of DDR and also other features that PS provides to the Master ports present in the PL . It provides the clocking to the system through FCLK_CLK0 in this design this port has been set to produce a clock of 125 MHz . In this design one M\_AXI\_GP0 and S_AXI_HP0 are used to meet the needs of the design . 
The interfaces/ports used : 
* `M_AXI_GP0` - The PS using this port through the AXI Interconnect will be able to configure the behaviour of the CDMA through its Slave S\_AXI\_LITE  interfaces . It will also be able to access/modify the data stored in BRAM through its S\_AXI interface . In order to make debugging easier in the current design this master port can also access the S\_AXI\_CTL of the AXI PCIE since it will make debugging easier at initial stages of the execution of the design though it can be ruled out i.e. on that specific interconnect can be removed S\_AXI\_CTL of the AXI PCIE  since it is not absolute necessity for the Master ports on that interconnect to access this Slave port
* `S_AXI_HP0` - Slave HP0(also stands for High Performance) port allows high speed access to the DDR needed by the CDMA engine. 

#### AXI MEMORY MAPPED PCIE  - axi\_pcie\_0
The following features are some of the many provided by this block which have been used by our design :
* Support for upto 4 lane(x4) Gen 2.0 PCIe specification
* Maximum Payload Size (MPS) up to 256 bytes though a maximum 
* PCIe access to memory-mapped AXI4 space
* Tracks and manages Transaction Layer Packets (TLPs) completion processing
* Compliant with Advanced RISC Machine (ARM) Advanced Microcontroller Bus Architecture 4 (AMBA) AXI4 specification 
* Three PCIe 32-bit or 64-bit PCIe Base Address Registers (BARs) as Endpoint are supported out of the 6 in 32 bit mode supported by the original PCIe specification 
* Support for Multiple Vectorq(upto 32) Messaged Signaled Interrupts (MSIs) though only one was sufficient for our needs

The two PCIE BARS used in the design have been internally mapped to small memory-mapped AXI space. Using this mapping the host motherboard will be able to access the registers of the various peripherals that have been mapped in this address space and also whose slave AXI ports are connected to the Master AXI port of this IP or block .

The IP also provides upto 6 AXI BARS which will allow the Zynq to access a portion of the HOST 64 bit space allowing it to read / write to it . The way how this works is that whenever there is data available on the AXI interface and has the destination address of that of S\_AXI\_BAR0 it will be will be sent to host after some formatting of adding packet headers according PCIe TLP specification but with destination address of that as specified in the AXIBAR2PCIEBAR configuration register. In our case AXI BAR0 and BAR1 are used so the 64-bit physical address of the buffers that these two BARs map to on the host will written in these translation registers.

The interfaces/ports used : 
* `M_AXI` - Master AXI interface connected to the same Slave AXI interfaces like PS Master port . It will have 1 AXI BARS which will allow the Zynq to access a portion of the HOST 64 bit space allowing it to read / write some portion of it . It will also have 2 PCIE BAR(though the second one has been added to check the scalability of the device driver which will be explained later) allowing the userspace to access mapped 32KiB of BRAM , 4 KiB of CDMA control space and 4 KiB of PCI control space. Though in this case as explained in the PS section this master port is also connected to the S\_AXI\_CTL port 
*  `S_AXI` - Using this port AXI BAR0 and BAR1 can be accessed and hence data using these ports can be sent to the host . 
*  `S_AXI_CTL` - To allow the master interfaces to get the details of the PCIe link status and to configure the address translation that it does on the inbound and outbound traffic as explained before . 

#### BRAM
BLOCK RAM is used to store the translation address for the outbound traffic i.e. AXIBAR2PCIEBAR that will be modified after every DMA transfer out of the batch of SG transfers . It will be stored here by HOST CPU since it is mapped to PCIE BAR before the Scatter Gather starts . During the initial stages of development the Block RAM is also used to store the descriptors since it is easier to debug the contents stored in the block ram since they can be viewed via XMD console .

#### Processor System Reset Module
It is used to provide customized resets to the entire processing system , including the processor , interconnect and the peripherals . In our case it is used to drive the external reset from the FCLK\_RESET0\_N to the peripherals and interconnects so that they can be started in the correct mode that is some peripherals used in the system need to be reset for the first 15 - 16 clock cycles before being started . Two of these blocks are used since two different sources of clock are present in the system FCLK\_CLK0 and the other being clock that is fed in through the PCIe pins by the Root Complex i.e. HOST motherboard . 

#### CDMA
Central Direct Memory Access IP is the dma engine in the system that can be configured to do Simple or Scatter/Gather DMA transfer  . 

`Simple` Simple mode can initiated by writing to Source Address , Destination Address and Bytes To Transfer (BTT) , as soon as BTT is written the DMA starts. After the data transfer the contents of CDMASR will indicate whether the transfer was successful or not . If not then was it because of Unaccessible AXI Memory Mapped Slave or some other reason . Every bit of status and control register is very important and is documented very well in the PG034 Xilinx Manual  reveal that .  

`Scatter/Gather` Using Scatter/Gather mode a set of descriptors will orchestrate the data transfer source and destination address with each descriptor having its own unique address's . 
Scatter Gather mode is used for transfers wherein the size of the data to be transfered is huge that is in terms of 100 MiB's so that with least CPU intervention data can be transfered effectively . 
One of the major aims to do so is  due to limitation of CDMA to be able to drive only lower 23 bits of address and hence be able to transfer only an upper limit of 8 MiB at one go . 
So with use of Scatter/Gather descriptors a set of descriptors can be configured to transfer many 8 MiB chunks of data in one go though there is one drawback to this mode which will be discussed later. SG mode can be configured by giving it an address to the first out of a set of descriptors each containing the Address to the Next Descriptor , Source address and Destination Address and Bytes To Transfer(Control offset) . After preparing the set of descriptors the address of the first descriptor should written to CURDESC_PNTR in the CDMA Register Map and at TAILDESC_PNTR address of the last descriptor out of the batch . Writing the TAILDESC_PNTR initiates SG.

`Cyclic BD` Another offshoot of the SG mode is the Cylic BD in which the set of descriptors specified are executed in a cyclic  form i.e. again execute the first descriptor after the last one . In this mode the writing anything to the TAILDESC\_PNTR will initiate the data transfer . This mode is commonly used in case of frame buffers wherein the buffers are continuously updated at regular intervals by the graphics card and correspondinly the data is transfered at regular intervals from the CARD to the HOST . To enable this mode a BIT 6 in CDMACR register should be set . 

#### Address Space
The address space allocated to the slave interfaces with each of them being specific to the master connected to that interconnect

### DEVICE DRIVER and USER SPACE APPLICATION
#### DEVICE DRIVER
The device driver developed for this application uses various PCI and DMA API's provided by the linux kernel . 
The device is registered as a character device since it does transactions at byte level . The other category of device is block device wherein the transaction is done in terms of blocks (generally 512B) for eg. hard disk . 
The steps followed in the device driver:
* The pci device is registered in the pci device table which is refered to by the kernel when a new pci device is detected 
* When the pci device is detected the following steps are performed
  * pci device is enabled by accessing and storing the configuration parameters configured the bios in the struct pci\_dev 
  * MSI is registered with the kernel so that when an MSI interrupt is received the kernel executes a specific function 
  * The dma mask is set -> It gives the kernel an idea of how many bits of the address is the device able to flip  which gives an idea of how much region max the device can access at any point 
  * A device class is created and registered
  * The attribute group that contains the various files the user space application can access and modify which is internally routed to different variables Sinside this kernel module using sys filesystem along with functions that will be called to perform operations on these special files
  * The pci device is registered as character device and added to the class . Every character device is allocated a Major and a set of minor device (which depends on how much the driver demands) . Also a set device file operations is also registered which are different from those that are registered for sysfs . These functions will be called when a corresponding common file operation like open , mmap or release is called by the user space on the files that are created with MAJOR number that this device is registered . But the minor number of the file created should belong to the range that was reserved earlier .
  * A device is created which can be understood as device file corresponding to a specific major and minor number is created in the /dev though many more such files can be later created using the mknod command depending on the number of minor numbers reserved in the previous step 
  * A signal is registered by the kernel with a specific signal number which will be used to signal the user space application when an interrupt has arrived . The actual signal generation will be done in the interrupt handler 

The device driver also generates some files in the folder `/sys/class/my\_class/my_pci0/` . Some of these files are used to change the internal kernel variables :
1. reg\_interrupt - the userspace process writes his pid in this file to so that the kernel knows which process to signal on receiving the interrupt 
2. size\_buf0 - user to enter the size of the buffer 0 that is the one corresponding to AXIBAR2PCIEBAR0
3. buf0 - user gets the physical address corresponding to the buffer0 

2,3 to be repeated depending on the number of AXIBAR2PCIEBAR being used.
To understand the device driver source code completely the Chapter 3,12,14 of LDD3 should be referred. 
#### USER SPACE APPLICATION
The user space application is much less privileged than the kernel but it is still needed since the operations in the kernel mode can be very fatal if not done properly , so a user space application is developed . 
1. Various functions are provided which making formatting the descriptors and initializing the appropriate control registers of the IP's to make them function in the desired way .
2. The initial control flow of the this application is as follows :
    * The /dev/my\_pci0 corresponds to the PCIE BAR 0 is opened mmaped into the user space as uncached virtual memory area 
    * Other files such as the /dev/my\_pci* can be created depending on the number PCI BARS registered or the number of buffers that need to be allocated though all of them need to be created with the same major number but with different but unique minor numbers . Using these files the memory mapped I/O or RAM memory region(dma'able buffer) can be accessed.   
    * To access the contents of the file they need to be  mmaped into the userspace after which it can access the corresponding regions . If a buffer is to mapped the minor number of the file should be greater than 2 . If PCIE BAR(MMIO) is to mapped then the minor number should 0(BAR0) or 1(BAR1) or 2(BAR2). 
    * Signal handler is registered now so that when the kernel receives the MSI interrupt it informs the userspace about it . 
    * The userspace custom logic should be put in using the various helper functions that are already created. After which the application waits for the signal from the kernel so that it can the close the opened files and hopefully successfully terminate . 
```bash
cd zc706_pcie/driver
make
sudo insmod my.ko
dmesg | tail # to see whether driver is properly loaded or not
cd ..
gcc app.c
sudo ./a.out 
```
### How Some Issues Were Solved
1. `AXI MEMORY MAPPED PCIE not being detected by the host motherboard` - We sent sent both REFCLK and axi\_aclk\_out to LEDS , from where it was found that none of them were glowing . So we later figured out that RECLK needs to powered from differential pair of clocks which come from the Root Complex i.e. motherboard through the PCIe pins(seemed like we don't go through them well before) 
2. `Scatter/Gather not working when the Source and Destination of that descriptor belong to the same IP or Block` - We were not able to figure this out until we used ILA(Integrated Logic Analyzer) Core AXI IP using which we were able to signals of the channels we were suspicious about . It was found that Source and Destination Address do appear  on the corresponding channels of that IP or Block to which the destination address belongs to . But after the address appeared there was no further data seen on any of the channels .  
3. `Not able to write at the desired physical address in the RAM` - Initially we thought that the memory and the cache's were not coherent or synchronized enough and so we were neither able to read or write the data at the desired location from the host . One of the reasons why we were thinking in that manner was that from the Zynq we were able to write and then getting the same data on read from the desired physical address but not accessible from the host CPU . So we tried changing the memory type of the allocated buffer using set\_memory\_uc() for uncached (or another memory type that could be useful was set\_memory\_wb() for writeback) but with no luck . So it was definitely not something to do with x86\/PAT . Later after some random tweaking we found that changing some bits i.e. making them zero of the physical address written in AXIBAR2PCIEBAR made no change to the read data i.e. that same data was being read . So we found the bits changing whom the there was a change in the read data so after analyzing the situation the following was the inference : the AXI BAR used was configured to have a size of 8MiB which occupies 23 bits in byte addressed memory , so the AXI MEMORY MAPPED PCIE was expecting a physical address in AXIBAR2PCIEBAR which was 8MiB addressed that is its lower 23 bits be 0 . Since at that point we were not able to allocate buffers of more than 128KiB the best way to access the same physical address as initially desired was by changing the offset form 0x40000000 accordingly where the AXI BAR was mapped in the AXI address space .  

4. `Not able to allocate buffers of large size` - Initially on allocating buffers of more than 128KiB in size resulted in kernel  oops . By googling we were able to find about CMA (contiguous memory allocator) that was added into mainline linux in 3.5 . Contiguous Memory Allocation is widely used by many Graphics Driver to allocated huge sized buffers . This mechanism is used by the kernel to reserve a buffer of that amount at boot time . So that it can allocated to the driver later . This API need not be explicitly called since it is used DMA ALLOCATION API internally . 
There are two ways how CMA can be used : 
    * CMA It should enabled from the config menu at before compiling the kernel .  
    * Pass the kernel a boot time parameter of cma=64M if 64MiB of buffer is needed . This boot time paramter can be added by the following steps
```bash
sudo gedit /etc/default/grub
# GRUB_CMDLINE_LINUX_DEFAULT on this line after splash
# add the cma=64M before the closing quotes
```  
Even after giving the boot time parameter we were not able to get huge buffer size so we then compiled the kernel but with some changes in the config file found in /boot CMA was configured to be able to allocated contiguous buffers but with size less than 50% of the total size . CMA alignment was changed to 12 from the defualt 8 since we needed the buffers to atleast 4MiB aligned . After changing this config and recompiling and installing this kernel we were able to boot with a system that was allowing us to allocate buffers of more than 128 MiB though the maximum allocatable buffer was not checked . 


### RESULT
The following data gives the average speed after carrying out many test 
* `Simple` - A 4MiB data was read from the Zynq DDR to the Host Buffer at a speed of 500MiB/s
* `Scatter Gather` - 4 descriptors each transfering data of 4MiB in size from the Zynq DDR to the Host Buffer with another descriptor after two of above descriptors above changing the AXIBAR2PCIEBAR  so that it can point to the next 8MiB . All the 5 descriptors were stored in another Host Buffer from where they were fetched by the CDMA engine through another AXIBAR2PCIEBAR . The average speed received was 500MiB/s 

## REFERENCES
http://googoolia.com/mynotes/2013/11/14/xilinx-zynq-boot-linux-over-network/ 

http://www.xilinx.com/support/documentation/ip_documentation/axi_pcie/v2_6/pg055-axi-bridge-pcie.pdf

http://www.xilinx.com/support/documentation/ip_documentation/axi_cdma/v4_1/pg034-axi-cdma.pdf

http://www.xilinx.com/support/documentation/user_guides/ug821-zynq-7000-swdev.pdf

https://www.kernel.org/doc/htmldocs/kernel-api/ch09s05.html

https://www.kernel.org/doc/htmldocs/device-drivers/ch02s04.html
