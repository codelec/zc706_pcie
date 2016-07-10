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
#include <linux/interrupt.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
//PCI IDs below are not registered! Use only for experiments 

#define PCI_VENDOR_ID_MY 0xABBA
#define PCI_DEVICE_ID_MY 0x7022
#define PCI_SUBSYSTEM_VENDOR_ID_MY 0x10EE
#define PCI_SUBSYSTEM_DEVICE_ID_MY 0x0007
#define CLASS_CODE 0x058000

static DEFINE_PCI_DEVICE_TABLE(my_pci_tbl) = {
	{PCI_VENDOR_ID_MY,PCI_DEVICE_ID_MY,PCI_ANY_ID,PCI_ANY_ID,0,0,0},{0,}};
MODULE_DEVICE_TABLE(pci,my_pci_tbl);
#define DEVICE_NAME "my_pci"
//for now the number of BAR only one
#define N_OF_RES (2)
//since 64 bits BARS used
static short res_nums[N_OF_RES]={0,2};
#define N_BUF 2
static resource_size_t mmio_start[N_OF_RES] , mmio_end[N_OF_RES] , 
			mmio_flags[N_OF_RES] , mmio_len[N_OF_RES] ;
static uint32_t *bmem = NULL;
dev_t my_dev = 0;
struct cdev *my_cdev = NULL;
static struct class *my_class = NULL;
static struct pci_dev *glob_pci ;
static dma_addr_t dmaddr_buf[N_BUF];
static void *dmakvirt_buf[N_BUF];
static uint32_t size_buf[N_BUF];
static struct siginfo sinfo;
pid_t pid_user;
struct task_struct *task;

static ssize_t attr_show(struct kobject *, struct kobj_attribute *,char *);
static ssize_t attr_store(struct kobject *, struct kobj_attribute *,const char *,size_t);
static struct kobj_attribute size_buf1_attribute =
	__ATTR(size_buf1, 0664, attr_show, attr_store);
static struct kobj_attribute size_buf2_attribute =
	__ATTR(size_buf2, 0664, attr_show, attr_store);
static struct kobj_attribute reg_inter_attribute =
	__ATTR(reg_interrupt, 0664, attr_show, attr_store);	
static struct kobj_attribute buf1_attribute =
	__ATTR(buf1, 0440, attr_show, NULL);
static struct kobj_attribute buf2_attribute =
	__ATTR(buf2, 0440, attr_show, NULL);

static ssize_t attr_show(struct kobject *kobj, struct kobj_attribute *attr , char *buf)
{
	if(attr == (&size_buf1_attribute))
		return sprintf(buf, "%d\n", size_buf[0]);
	if(attr == (&size_buf2_attribute))
		return sprintf(buf, "%d\n", size_buf[1]);
	if(attr == (&buf1_attribute))
		return sprintf(buf, "%llx\n", dmaddr_buf[0]);
	if(attr == (&buf2_attribute))
		return sprintf(buf, "%llx\n", dmaddr_buf[1]);
	return -EINVAL;
}

static ssize_t attr_store(struct kobject *kobj, struct kobj_attribute *attr , const char *buf, size_t count)
{
	int ret = 0;
	if(attr == (&size_buf1_attribute))
		ret = kstrtoint(buf, 10, &size_buf[0]);
	else if(attr == (&size_buf2_attribute))
		ret = kstrtoint(buf, 10, &size_buf[1]);
	else if(attr == (&reg_inter_attribute))
		ret = kstrtoint(buf, 10, &pid_user);
	if (ret < 0)
		return ret;
	return count;
}
static struct attribute *attrs[] = {
	&size_buf1_attribute.attr,
	&size_buf2_attribute.attr,
	&buf1_attribute.attr,
	&buf2_attribute.attr,
	&reg_inter_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};
static const struct attribute_group *attrr_group[]={
	&attr_group,
	NULL,
};

static int devfs_open(struct inode *inode , struct file *filp)
{
	nonseekable_open(inode , filp);
	return 0;
}

static int devfs_release(struct inode *inode , struct file *filp)
{
	uint8_t minor_filp = iminor(filp->f_path.dentry->d_inode) - 3;
	if(minor_filp >= 0){
		if(!set_memory_wb((unsigned long )dmakvirt_buf[minor_filp],size_buf[minor_filp] >> PAGE_SHIFT))
			printk("ATTR of %d changed back to WB !!\n",minor_filp);
		dma_free_coherent(&glob_pci->dev,size_buf[minor_filp],dmakvirt_buf[minor_filp],dmaddr_buf[minor_filp]);
		printk("DMA Descriptor buffer %d free'ed\n",minor_filp);
	}
	return 0;
}
static int devfs_mmap(struct file *filp , struct vm_area_struct *vma)
{
	uint8_t minor_filp = iminor(filp->f_path.dentry->d_inode);
	unsigned long vsize ;
	vsize = vma->vm_end - vma->vm_start ;
	if(minor_filp<3){
		if(vsize > mmio_len[minor_filp])
			return -EINVAL;
		io_remap_pfn_range(vma,vma->vm_start, (mmio_start[minor_filp]) >> PAGE_SHIFT , vsize, vma->vm_page_prot);
		printk ("<1>BAR%u seems to be mapped\n",minor_filp);	
	}
	else {
		uint8_t minor_temp = minor_filp - 3;
		dmakvirt_buf[minor_temp] = dma_alloc_coherent(&glob_pci->dev,size_buf[minor_temp],&dmaddr_buf[minor_temp],GFP_USER);
		if((dmakvirt_buf[minor_filp]==NULL) || (vsize > size_buf[minor_temp])){
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

struct file_operations devfsops = {
	.owner = THIS_MODULE,
	.open = devfs_open,
	.mmap = devfs_mmap,
	.release = devfs_release
};
static irqreturn_t irq_handler(int irq,void *dev_id)
{
	task = pid_task(find_vpid(pid_user),PIDTYPE_PID);
	if(task != NULL)
		send_sig_info(SIGUSR1,&sinfo,task);
	else
		bmem[0x0000/4]=0xFFFFFFFF; // just for debugging 
	return IRQ_HANDLED;
}

static int my_pci_probe(struct pci_dev *pdev , const struct pci_device_id *ent)
{
	int i , res = -1;
	glob_pci = pdev;
	if(pci_enable_device(pdev))
	{
		dev_err(&pdev->dev,"Can't enable PCI device , aborting\n");
		goto err1;
	}
	//For now seperate threaded function not enabled
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
	sinfo.si_signo = SIGIO;
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