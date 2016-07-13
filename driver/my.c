#include <linux/kernel.h>                //   |
#include <linux/module.h>                //   |  BASIC NECESSITY
#include <linux/init.h>                  //   |  
#include <linux/kobject.h> //kobject used to build the hierarchy seen in /sys
#include <linux/string.h> // kstrtoint
#include <linux/sysfs.h> // class attributes in sysfs
#include <linux/device.h> // struct dev used by many functions
#include <linux/fs.h> // devfs file_operations 
#include <linux/cdev.h> // cdev_add , alloc_chrdev_region
#include <linux/pci.h> // pci_device_enable , pci_msi_enable and all other pci_*
#include <linux/interrupt.h> // request_irq 
#include <asm/cacheflush.h> // set_memory_uc ,  set_memory_wb , set_memory_wc
#include <linux/sched.h> // send_sig_info , signal attributes 

// PCI IDs below are not registered! Use only for experiments 
// PCI_*_ID changed to make sure only this module gets loaded
#define PCI_VENDOR_ID_MY 0xABBA
#define PCI_DEVICE_ID_MY 0x7022
//kernel pci device driver support registry 
static DEFINE_PCI_DEVICE_TABLE(my_pci_tbl) = {
	{PCI_VENDOR_ID_MY,PCI_DEVICE_ID_MY,PCI_ANY_ID,PCI_ANY_ID,0,0,0},{0,}};
MODULE_DEVICE_TABLE(pci,my_pci_tbl);
#define DEVICE_NAME "my_pci"
#define N_BUF 2 //NUM of AXIBAR2PCIE used
#define N_OF_RES 2 //NUM of PCIE BAR
//since 64 bits BARS used
static short res_nums[N_OF_RES]={0,2};
static resource_size_t mmio_start[N_OF_RES] , mmio_end[N_OF_RES] , 
			mmio_flags[N_OF_RES] , mmio_len[N_OF_RES] ;
static uint32_t *bmem = NULL; //just to test the access of PCIE BAR
dev_t my_dev = 0;//stores the major and minor allocated to the character device
struct cdev *my_cdev = NULL;//package of all the contents to access the character device
static struct class *my_class = NULL;
static struct pci_dev *glob_pci; //encapsulation of struct dev with pci specific addons
static dma_addr_t dmaddr_buf[N_BUF]; //dma bus address of buffer handed over to device 
static void *dmakvirt_buf[N_BUF]; //virtual address of buffer mapped into kernel address space 
static uint32_t size_buf[N_BUF];
static struct siginfo sinfo;//to store info of signal to be sent to user space 
pid_t pid_user;//pid of user space process to trigger signal to incase of interrupt 
struct task_struct *task;//task_struct of user space process

static ssize_t attr_show(struct kobject *, struct kobj_attribute *,char *);
static ssize_t attr_store(struct kobject *, struct kobj_attribute *,const char *,size_t);

static struct kobj_attribute size_buf0_attribute =
	__ATTR(size_buf0, 0664, attr_show, attr_store);
static struct kobj_attribute size_buf1_attribute =
	__ATTR(size_buf1, 0664, attr_show, attr_store);
static struct kobj_attribute reg_inter_attribute =
	__ATTR(reg_interrupt, 0664, attr_show, attr_store);	
static struct kobj_attribute buf0_attribute =
	__ATTR(buf0, 0440, attr_show, NULL);
static struct kobj_attribute buf1_attribute =
	__ATTR(buf1, 0440, attr_show, NULL);

/**
 * @brief      Based on the value of attr the correct kernel variable is filled
 *             into buffer
 */
static ssize_t attr_show(struct kobject *kobj, struct kobj_attribute *attr , char *buf)
{
	if(attr == (&size_buf0_attribute))
		return sprintf(buf, "%d\n", size_buf[0]);
	if(attr == (&size_buf1_attribute))
		return sprintf(buf, "%d\n", size_buf[1]);
	if(attr == (&reg_inter_attribute))
		return sprintf(buf, "%d\n", pid_user);
	if(attr == (&buf0_attribute))
		return sprintf(buf, "%llx\n", dmaddr_buf[0]);
	if(attr == (&buf1_attribute))
		return sprintf(buf, "%llx\n", dmaddr_buf[1]);
	return -EINVAL;
}

/**
 * @brief     	Based on the value of attr the correct kernel variable is updated
 */
static ssize_t attr_store(struct kobject *kobj, struct kobj_attribute *attr , const char *buf, size_t count)
{
	int ret = 0;
	if(attr == (&size_buf0_attribute))
		ret = kstrtoint(buf, 10, &size_buf[0]);
	else if(attr == (&size_buf1_attribute))
		ret = kstrtoint(buf, 10, &size_buf[1]);
	else if(attr == (&reg_inter_attribute))
		ret = kstrtoint(buf, 10, &pid_user);
	if (ret < 0)
		return ret;
	return count;
}

//attribute array makes it easier to add attributes to /sys
static struct attribute *attrs[] = {
	&size_buf0_attribute.attr,
	&size_buf1_attribute.attr,
	&buf0_attribute.attr,
	&buf1_attribute.attr,
	&reg_inter_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};
//pack the attribute array into an attribute group 
static struct attribute_group attr_group = {
	.attrs = attrs,
};
//the form in which it is to be added to struct class 
static const struct attribute_group *attrr_group[]={
	&attr_group,
	NULL,
};

/**
 * @brief      custom function called when /dev/my_pci* is opened
 */
static int devfs_open(struct inode *inode , struct file *filp)
{
	nonseekable_open(inode , filp);
	return 0;
}

/**
 * @brief     	called when /dev/my_pci* or any character file with
 * 				MAJOR (major of device) and MINOR in 0 to 9 closed
 * 				It releases the buffer on RAM that is allocated by
 * 				only for files when they are mmaped who have a 
 * 				minor number of >=3 
 *
 * @param      inode  The inode
 * @param      filp   file pointer used to get the inode of the file which is just closed
 */
static int devfs_release(struct inode *inode , struct file *filp)
{
	short minor_filp = iminor(filp->f_path.dentry->d_inode) - 3; //detects the minor number from the inode  
	if((minor_filp >= 0) && (dmakvirt_buf[minor_filp] != NULL)){
		//before releasing the buffer the memory type should be changed back to write back 
		if(!set_memory_wb((unsigned long )dmakvirt_buf[minor_filp],size_buf[minor_filp] >> PAGE_SHIFT))
			printk("ATTR of %d changed back to WB !!\n",minor_filp);
		//free the buffer using the api that allocated it 
		dma_free_coherent(&glob_pci->dev,size_buf[minor_filp],dmakvirt_buf[minor_filp],dmaddr_buf[minor_filp]);
		printk("DMA Descriptor buffer %d free'ed\n",minor_filp);
	}
	return 0;
}
/*
 * @brief      Based on the minor number of the file it is detected whether the PCIE BAR(0,1,2) or RAM BUFFER(3,4,5,6,7,8)
 * 				should mapped into the given virtual memory area . 
 * 				Incase of RAM Buffer the allocated memory type is changed to uncached  
 * 				 
 *
 * @param      filp  The filp
 * @param      vma   Virtual Memory Address pointer in which space the newly allocated 
 * 					buffer would be mapped into for the user 
 *
 * @return    0 on success or -EINVAL on invalid request or -ENOMEM incase of not enough memory available 
 */
static int devfs_mmap(struct file *filp , struct vm_area_struct *vma)
{
	short minor_filp = iminor(filp->f_path.dentry->d_inode);
	unsigned long vsize ;
	vsize = vma->vm_end - vma->vm_start ;
	if(minor_filp<3){
		if(vsize > mmio_len[minor_filp])
			return -EINVAL;
		io_remap_pfn_range(vma,vma->vm_start, (mmio_start[minor_filp]) >> PAGE_SHIFT , vsize, vma->vm_page_prot);
		printk ("<1>BAR%u Start:%llx Vsize:%lu seems to be mapped\n",minor_filp,mmio_start[minor_filp],vsize);	
		return 0;	
	}
	else {
		uint8_t minor_temp = minor_filp - 3;
		dmakvirt_buf[minor_temp] = dma_alloc_coherent(&glob_pci->dev,size_buf[minor_temp],&dmaddr_buf[minor_temp],GFP_USER);
		if((dmakvirt_buf[minor_temp]==NULL) || (vsize > size_buf[minor_temp])){
			printk(KERN_ERR "No DMA buffer %d allocated or Invalid mmap size\n",minor_temp);
			return -ENOMEM;
		}
		if(!set_memory_uc((unsigned long )dmakvirt_buf[minor_temp],size_buf[minor_temp] >> PAGE_SHIFT))
			printk("ATTR of %d seems to changed !!\n",minor_temp);
		if(!dma_mmap_coherent(&glob_pci->dev,vma,dmakvirt_buf[minor_temp],dmaddr_buf[minor_temp],vsize)){
			printk( "Allocated DMA Descriptor buffer %d at phys:%llx virt:%llx\n",minor_temp,dmaddr_buf[minor_temp],dmakvirt_buf[minor_temp]);
      		return 0;
		}
	}
	return -EINVAL;
}
/* Following file operations apply to character device files created with major
 * (MAJOR of device allocted at runtime based on the availability) and minor
 * number in between 0 to 9 as registed during pci probing
 */
struct file_operations devfsops = {
	.owner = THIS_MODULE,
	.open = devfs_open,
	.mmap = devfs_mmap,
	.release = devfs_release
};

/**
 * @brief      Sends the signal to user space process which has registered with its pid in the reg_interrupt var 
 *				if incase an invalid pid is found just to indicate an error at 0 location data is written  
 *				so that it can be debugged
 */
static irqreturn_t irq_handler(int irq,void *dev_id)
{
	task = pid_task(find_vpid(pid_user),PIDTYPE_PID);
	if(task != NULL)
		send_sig_info(SIGUSR1,&sinfo,task);
	else
		bmem[0x0000/4]=0xFFFFFFFF; // just for debugging 
	return IRQ_HANDLED;
}



/**
 * @brief      The function is executed when a valid PCI device is detected with the same DEVICE and VENDOR ID as 
 * 				registered in the device table 
 *
 */
static int my_pci_probe(struct pci_dev *pdev , const struct pci_device_id *ent)
{
	int i , res = -1;
	glob_pci = pdev;
	//just the wakes the device 
	if(pci_enable_device(pdev))
	{
		dev_err(&pdev->dev,"Can't enable PCI device , aborting\n");
		goto err1;
	}
	pci_enable_msi(pdev);
	//For now seperate threaded function not enabled 
	if(request_irq(pdev->irq, irq_handler, 0, "MY_PCI_FPGA_CARD", pdev))
		printk( "IRQ:%d not registerd ",pdev->irq);
	for(i=0;i<N_OF_RES;i++)
	{
		//pci_reasource_* corresponds to accessing BAR characteristics
		//such as the base address , length and flags 
		mmio_start[i]=pci_resource_start(pdev,res_nums[i]);
		mmio_end[i]=pci_resource_end(pdev,res_nums[i]);
		mmio_flags[i]=pci_resource_flags(pdev,res_nums[i]);
		mmio_len[i]=pci_resource_len(pdev,res_nums[i]);
		printk( "Resource: %d start:%llx, end:%llx, flags:%llx, len=%llx\n",
        	i,mmio_start[i],mmio_end[i], mmio_flags[i], mmio_len[i]);
		if(!(mmio_flags[i] & IORESOURCE_MEM)){
			dev_err(&pdev->dev,"region %i not an MMIO resource aborting\n",i);
			res = -ENODEV;
			goto err1;
		}
	}
	//dma mask used to indicate how many bits of the DMA address can the device flip 
	//in this case it is set to 64 since the DMA address generated is memory mapped 
	//by AXI MEMORY MAPPED PCIE to host 64 bit address 
	if(dma_set_mask_and_coherent(&(pdev->dev),DMA_BIT_MASK(64))){
		if(dma_set_mask_and_coherent(&(pdev->dev),DMA_BIT_MASK(32)))
		dev_info(&pdev->dev,"Unable to obtain 64bit dma for consistent allocations \n");
		goto err1;
	}
	res = pci_request_regions(pdev,DEVICE_NAME);
	if(res)
		goto err1;
	pci_set_master(pdev);
	bmem = ioremap(mmio_start[0],mmio_len[0]);
	if(!bmem){
		printk(KERN_ERR "Mapping of memory for %s BAR%d failed",
			DEVICE_NAME,0);
		res = -ENOMEM;
		goto err1;
	}
	if (!(bmem[0x8004/4] & 0x8))
	{
		printk( "Either not able to read or SG not configured \n");
		goto err1;
	}
	my_class = class_create(THIS_MODULE,"my_class");
	if(IS_ERR(my_class)){
		printk(KERN_ERR "Error creating my_class class \n");
		res = PTR_ERR(my_class);
		goto err1;
	}
	if(!(my_class -> dev_groups))
		my_class -> dev_groups = attrr_group;
	res = alloc_chrdev_region(&my_dev,0,10,DEVICE_NAME);
	if(res){
		printk (KERN_ERR " Allocation of the device number for %s failed\n",DEVICE_NAME);
		goto err1;
	}
	my_cdev = cdev_alloc();
	if(my_cdev == NULL){
		printk (KERN_ERR " Allocation of cdev for %s failed \n",DEVICE_NAME);
		goto err1;
	}
	my_cdev -> ops = &devfsops ;
	my_cdev -> owner = THIS_MODULE;
	res = cdev_add (my_cdev , my_dev , 10);
	if(res){
		printk(KERN_ERR " Registration of the device number for %s failed \n",DEVICE_NAME);
		goto err1;
	}
	device_create(my_class , NULL , my_dev ,NULL,"my_pci%d",MINOR(my_dev));
	printk( "MAJOR %s is %d\n",DEVICE_NAME,MAJOR(my_dev));
	printk ("Done Probing \n");
	memset(&sinfo,0,sizeof(struct siginfo));
	sinfo.si_signo = SIGUSR1;
	sinfo.si_code = SI_USER;
	return 0;
err1:
	if (bmem){
		iounmap(bmem);
		bmem = NULL ;
	}
	return res;
}
void my_pci_remove(struct pci_dev *pdev)
{
	free_irq(pdev->irq,NULL);
	if(my_dev && my_class)
		device_destroy(my_class , my_dev);
	if(my_cdev){
		cdev_del(my_cdev);
		my_cdev = NULL ;
	}
	unregister_chrdev_region(my_dev , 1);
	if(my_class){
		class_destroy(my_class);
		my_class = NULL;
	}
	if (bmem){
		iounmap(bmem);
		bmem = NULL ;
	}
	pci_disable_msi(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	printk( "Hopefully removed ");
}
static struct pci_driver my_pci_driver = {
	.name = DEVICE_NAME,
	.id_table = my_pci_tbl,
	.probe = my_pci_probe,
	.remove = my_pci_remove ,
};

static int __init my_pci_init(void)
{
	return pci_register_driver(&my_pci_driver);
}
static void __exit my_pci_exit(void)
{
	if(my_class != NULL)
		my_pci_remove(glob_pci);
	pci_unregister_driver(&my_pci_driver);
}
module_init(my_pci_init);
module_exit(my_pci_exit);
MODULE_LICENSE("GPL");