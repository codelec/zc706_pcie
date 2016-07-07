#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/mod_devicetable.h>

//PCI IDs below are not registered! Use only for experiments 

#define PCI_VENDOR_ID_MY 0xABBA
#define PCI_DEVICE_ID_MY 0x7022
#define PCI_SUBSYSTEM_VENDOR_ID_MY 0x10EE
#define PCI_SUBSYSTEM_DEVICE_ID_MY 0x0007
#define CLASS_CODE 0x058000

static DEFINE_PCI_DEVICE_TABLE(my_pci_tbl) = {
	{PCI_VENDOR_ID_MY,PCI_DEVICE_ID_MY,PCI_ANY_ID,PCI_ANY_ID,0,0,0},{0,}};
MODULE_DEVICE_TABLE(pci,my_pci_tbl);
#define BRAM_SIZE 32768 // Size of BRAM 32KiB 32*1024 B 
#define DEVICE_NAME "my_pci"
//for now the number of BAR only one
#define N_OF_RES (1)
//change the foll after more bars needed
#define CURR_RES 0
#define N_BUF 2
static short res_nums[]={0};
static resource_size_t mmio_start[N_OF_RES] , mmio_end[N_OF_RES] , 
			mmio_flags[N_OF_RES] , mmio_len[N_OF_RES] ;
static uint32_t *bmem = NULL;
static void *dmabuf_descriptor = NULL;
dev_t my_dev = 0;
struct cdev *my_cdev = NULL;
static struct class *my_class = NULL;
static uint64_t baraddr[N_OF_RES];
static struct pci_dev *glob_pci ;
static dma_addr_t dmaddr_descriptor , dmaddr_buf[N_BUF];
static void *dmakvirt_buf[N_BUF] , *dmakvirt_descriptor;
///////////////////////////
static uint32_t size_buf1,size_buf2;
static ssize_t size_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", size_buf1);
}

static ssize_t size_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;
	ret = kstrtoint(buf, 10, &size_buf1);
	if (ret < 0)
		return ret;
	if((size_buf1 > 1073741824) || (size_buf1 < 4096))
		return -EINVAL;
	return count;
}

static struct kobj_attribute size_buf1_attribute =
	__ATTR(size_buf1, 0664, size_show, size_store);

static ssize_t size_show1(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", size_buf2);
}

static ssize_t size_store1(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int ret;
	ret = kstrtoint(buf, 10, &size_buf2);
	if (ret < 0)
		return ret;
	if((size_buf2 > 1073741824) || (size_buf2 < 4096))
		return -EINVAL;
	return count;
}

static struct kobj_attribute size_buf2_attribute =
	__ATTR(size_buf2, 0664, size_show1, size_store1);

static ssize_t b_show(struct kobject *kobj, struct kobj_attribute *attr,
		      char *buf)
{
	if(attr == (&buf_bram_attribute))
		sprintf(buf, "%llx\n", dmaddr_descriptor);
	if(attr == (&buf1_attribute))
		sprintf(buf, "%llx\n", dmaddr_buf[0]);
	if(attr == (&buf2_attribute))
		sprintf(buf, "%llx\n", dmaddr_buf[1]);
}

static struct kobj_attribute buf_bram_attribute =
	__ATTR(buf_bram, 0440, b_show, NULL);
static struct kobj_attribute buf1_attribute =
	__ATTR(buf1, 0440, b_show, NULL);
static struct kobj_attribute buf2_attribute =
	__ATTR(buf2, 0440, b_show, NULL);

static struct attribute *attrs[] = {
	&size_buf1_attribute.attr,
	&size_buf2_attribute.attr,
	&buf_bram_attribute,
	&buf1_attribute,
	&buf2_attribute,
	NULL,	/* need to NULL terminate the list of attributes */
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};
static const struct attribute_group *attrr_group[]={
	&attr_group,
	NULL,
};

void test111(void)
{
	uint32_t temp;
	bmem[0x8000/4] = bmem[0x8000/4] | 0x00000004;
	//while (bmem[0x8000/4] & 0x00000004);
	//printk("Wait \n");
	//printk("Wait \n");
	printk("S:%x\n",bmem[0x8004/4]);
	bmem[0x8004/4] = 0x00001000;
	bmem[0x8004/4] = 0x00004000;
	printk("C:%x\n",bmem[0x8000/4]);
	printk("S:%x\n",bmem[0x8004/4]);
	temp = bmem[0x8000/4];
	temp = temp | 0x00005008;
	temp = temp & 0xFF00FFFF;
	temp = temp | 0x00020000;
	bmem[0x8000/4] = temp;
	printk("C:%x\n",bmem[0x8000/4]);
	printk("S:%x\n",bmem[0x8004/4]);
	bmem[0x920C/4]=(dmaddr & 0x00000000FFFFFFFF);
	bmem[0x9208/4]=dmaddr >> 32;

/*	bmem[0x0040/4]=0x41000080;
	bmem[0x0044/4]=0x00000000;
	bmem[0x0048/4]=0x41007FFC;
	bmem[0x004C/4]=0x00000000;
	bmem[0x0050/4]=0x41008208;
	bmem[0x0054/4]=0x00000000;
	bmem[0x0058/4]=0x00000004;

	bmem[0x0000/4]=0x41000040;
	bmem[0x0004/4]=0x00000000;
	bmem[0x0008/4]=0x41007FF8;
	bmem[0x000C/4]=0x00000000;
	bmem[0x0010/4]=0x4100820C;
	bmem[0x0014/4]=0x00000000;
	bmem[0x0018/4]=0x00000004;*/
	
	bmem[0x4000/4]=0x41114111;
	bmem[0x4004/4]=0x51115111;
	bmem[0x4008/4]=0x61114678;

	bmem[0x0080/4]=0x820000C0;
	bmem[0x0084/4]=0x00000000;
	bmem[0x0088/4]=0x82004000;
	bmem[0x008C/4]=0x00000000;
	bmem[0x0090/4]=0x82004020;
	bmem[0x0094/4]=0x00000000;
	bmem[0x0098/4]=0x0000000A;
	bmem[0x009C/4]=0x00000000;

	bmem[0x00C0/4]=0x82000050;
	bmem[0x00C4/4]=0x00000000;
	bmem[0x00C8/4]=0x82004020;
	bmem[0x00CC/4]=0x00000000;
	bmem[0x00D0/4]=0x82004040;
	bmem[0x00D4/4]=0x00000000;
	bmem[0x00D8/4]=0x0000000A;
	bmem[0x00DC/4]=0x00000000;

	bmem[0x8008/4]=0x82000080;
	bmem[0x800C/4]=0x00000000;
	bmem[0x8010/4]=0x820000C0;
	bmem[0x8014/4]=0x00000000;

	// bmem[0x0000/4]=0x41223344;
	// bmem[0x0004/4]=0x51223344;
	// bmem[0x0008/4]=0x61223374;

	// bmem[0x8018/4]=0x41000000;
	// bmem[0x801C/4]=0x00000000;
	// bmem[0x8020/4]=0x40000000;
	// bmem[0x8024/4]=0x00000000;
	// bmem[0x8028/4]=0x0000000A;

	printk("<1>Test Complete!!!");
}

///////////////////////////////////

static int devfs_open(struct inode *inode , struct file *file);
int devfs_mmap(struct file *filp , struct vm_area_struct *vma);


struct file_operations devfsops = {
	.owner = THIS_MODULE,
	.open = devfs_open,
	.mmap = devfs_mmap,
	.release = devfs_release
};

static int devfs_open(struct inode *inode , struct file *filp)
{
	nonseekable_open(inode , filp);
	return 0;
}

static int devfs_release(struct inode *inode , struct file *filp)
{
	uint8_t minor_filp = iminor(filp->f_path.dentry->d_inode) - 7;
	if(minor_filp == 0)
		dma_free_coherent(&pdev->dev,BRAM_SIZE,dmakvirt_descriptor,dmaddr_descriptor);
	else if((minor_filp == 1) || (minor_filp == 2))
		dma_free_coherent(&pdev->dev,(minor_filp == 1)?size_buf1:size_buf2,dmakvirt_buf[minor_filp],dmaddr_buf[minor_filp]);
	printk("DMA Descriptor buffer area free'ed\n");
	return 0;
}
static int devfs_mmap(struct file *filp , struct vm_area_struct *vma)
{
	uint8_t minor_filp = iminor(filp->f_path.dentry->d_inode) - 7;
	unsigned long vsize ;
	vsize = vma->vm_end - vma->vm_start ;
	if(minor_filp<6){
		if(vsize > mmio_len[CURR_RES])
			return -EINVAL;
		io_remap_pfn_range(vma,vma->vm_start, (mmio_start[minor_filp]) >> PAGE_SHIFT , vsize, vma->vm_page_prot);
		printk ("<1>BAR%u seems to be mapped\n",minor_filp);	
	}
	else if(minor_filp == 0){
		//allocatte and mmap BRAM buffer 
		dmakvirt_descriptor = dma_alloc_coherent(&glob_pci->dev,BRAM_SIZE,&dmaddr_descriptor,GFP_USER);
		if((dmakvirt_descriptor==NULL) || (vsize > BRAM_SIZE)){
			printk( "No DMA buffer allocated\n");
			return -ENOMEM;
		}
		if(!dma_mmap_coherent(&glob_pci->dev,vma,dmakvirt_descriptor,dmaddr_descriptor,vsize)){
			printk( "Allocated DMA Descriptor buffer at phys:%llx virt:%llx\n",dmaddr_descriptor,dmakvirt_descriptor);
      		return 0;
		}
		return -EINVAL;
		//remap_pfn_range(vma,vma->vm_start, virt_to_phys() >> PAGE_SHIFT , vsize, vma->vm_page_prot);
	}
	else if((minor_filp == 1) || (minor_filp == 2)){
		uint32_t asize = (minor_filp == 1)?size_buf1:size_buf2;
		dmakvirt_buf[minor_filp] = dma_alloc_coherent(&glob_pci->dev,asize,&dmaddr_buf[minor_filp],GFP_USER);
		if((dmakvirt_buf[minor_filp]==NULL) || (vsize > asize){
			printk(KERN_ERR "No DMA buffer allocated or Invalid mmap size\n");
			return -ENOMEM;
		}if(!dma_mmap_coherent(&glob_pci->dev,vma,dmakvirt_buf[minor_filp],dmaddr_buf[minor_filp],vsize)){
			printk( "Allocated DMA Descriptor buffer at phys:%llx virt:%llx\n",dmaddr_buf[minor_filp],dmakvirt_buf[minor_filp]);
      		return 0;
		}
		return -EINVAL;
		//remap_pfn_range(vma,vma->vm_start, dmaddr_buf[minor_filp] >> PAGE_SHIFT , vsize, vma->vm_page_prot);
		printk("Allocated DMA buffer %d at phys:%llx virt:%llx\n",minor_filp,dmaddr_buf[minor_filp],dmakvirt_buf[minor_filp]);
	}
	return -EINVAL;
}

static irqreturn_t irq_handler(int irq,void *dev_id)
{
	bmem[0x0000/4]=0x82222232; // just for debugging 
	return IRQ_HANDLED;
}

static int my_pci_probe(struct pci_dev *pdev , const struct pci_device_id *ent)
{
	int i , res = -1;
	glob_pci = pdev;
	printk ("<1>before pci enable \n");
	if(pci_enable_device(pdev))
	{
		dev_err(&pdev->dev,"Can't enable PCI device , aborting\n");
		goto err1;
	}
	//For now seperate threaded function not enabled
	printk ("<1>after pci enable \n"); 
	pci_enable_msi(pdev);
	if(request_irq(pdev->irq, irq_handler, 0, "MY_PCI_FPGA_CARD", pdev))
		printk( "IRQ:%d not registerd ",pdev->irq);
	printk ("<1>after irq \n");
	for(i=0;i<N_OF_RES;i++)
	{
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
	if(pci_set_consistent_dma_mask(pdev,DMA_BIT_MASK(64))){
		if(pci_set_consistent_dma_mask(pdev,DMA_BIT_MASK(32)))
		dev_info(&pdev->dev,"Unable to obtain 64bit dma for consistent allocations \n");
		goto err1;
	}
	res = pci_request_regions(pdev,DEVICE_NAME);
	if(res)
		goto err1;
	pci_set_master(pdev);
	bmem = ioremap(mmio_start[CURR_RES],mmio_len[CURR_RES]);
	if(!bmem){
		printk(KERN_ERR "Mapping of memory for %s BAR%d failed",
			DEVICE_NAME,CURR_RES);
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
	if(!(my_class -> dev_groups)){
		printk("It is NULL2\n");
		my_class -> dev_groups = attrr_group;
	}
	res = alloc_chrdev_region(&my_dev,0,1,DEVICE_NAME);
	if(res){
		printk ("<1>Allocation of the device number for %s failed\n",DEVICE_NAME);
		goto err1;
	}
	my_cdev = cdev_alloc();
	if(my_cdev == NULL){
		printk ("<1> Allocation of cdev for %s failed \n",DEVICE_NAME);
		goto err1;
	}
	my_cdev -> ops = &devfsops ;
	my_cdev -> owner = THIS_MODULE;
	res = cdev_add (my_cdev , my_dev , 1);
	if(res){
		printk("<1> Registration of the device number for %s failed \n",DEVICE_NAME);
		goto err1;
	}
	device_create(my_class , NULL , my_dev ,NULL,"my_pci%d",MINOR(my_dev));
	printk( "MAJOR %s is %d\n",DEVICE_NAME,MAJOR(my_dev));

	//////////////////////
	test111();
	//////////////////////

	printk ("Done Probing \n");
	return 0;
err1:
	if (bmem){
		iounmap(bmem);
		bmem = NULL ;
	}
	if (dmabuf_descriptor)
		dma_free_coherent(&pdev->dev,BRAM_SIZE,dmabuf_descriptor,dmaddr);
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
	if (dmabuf_descriptor)
		dma_free_coherent(&pdev->dev,BRAM_SIZE,dmabuf_descriptor,dmaddr);
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
	printk("inserted my\n");
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