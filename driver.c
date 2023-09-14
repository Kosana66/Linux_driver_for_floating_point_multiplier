/* Kernel headers */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>

MODULE_AUTHOR("Kosana Mina Matija");
MODULE_DESCRIPTION("FPM IP core driver");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_NAME "fpm_driver" 
#define DEVICE_NAME "fpm_device"
#define BUFF_SIZE 20

/* -------------------------------------- */
/* --------FPM IP RELATED MACROS--------- */
/* -------------------------------------- */






/* -------------------------------------- */
/* ----------DMA RELATED MACROS---------- */
/* -------------------------------------- */





/* -------------------------------------- */
/* --------FUNCTION DECLARATIONS--------- */
/* -------------------------------------- */

static int  fpm_probe(struct platform_device *pdev);
static int  fpm_remove(struct platform_device *pdev);
int         fpm_open(struct inode *pinode, struct file *pfile);
int         fpm_close(struct inode *pinode, struct file *pfile);
ssize_t     fpm_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t     fpm_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);

static int  __init fpm_init(void);
static void __exit fpm_exit(void);

static irqreturn_t fpm_isr(int irq, void* dev_id);
irq_handler_t fpm_handler_irq = &fpm_isr;

/* -------------------------------------- */
/* -----------GLOBAL VARIABLES----------- */
/* -------------------------------------- */

struct fpm_info {
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
	int irq_num;
};

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device_fpm;
static struct cdev *my_cdev;
static struct fpm_info *fpm_p = NULL;

struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = fpm_open,
	.release = fpm_close,
	.read = fpm_read,
	.write = fpm_write
};

static struct of_device_id fpm_of_match[] = {
	{ .compatible = "fpm_ip", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, fpm_of_match);

static struct platform_driver fpm_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= fpm_of_match,
	},
	.probe		= fpm_probe,
	.remove		= fpm_remove,
};

/* -------------------------------------- */
/* -------INIT AND EXIT FUNCTIONS-------- */
/* -------------------------------------- */

/* Init function being called and executed only once by insmod command. */
static int __init fpm_init(void)
{
	int ret = 0;
	int i = 0;

	printk(KERN_INFO "[fpm_init] Initialize Module \"%s\"\n", DEVICE_NAME);

	/* Dynamically allocate MAJOR and MINOR numbers. */
	ret = alloc_chrdev_region(&my_dev_id, 0, 2, "fpm_region");
	if(ret) {
		printk(KERN_ALERT "[fpm_init] Failed CHRDEV!\n");
		return -1;
	}
	printk(KERN_INFO "[fpm_init] Successful CHRDEV!\n");

	/* Creating NODE file */

	/* Firstly, class_create is used to create class to be used as a parametar going forward. */
	my_class = class_create(THIS_MODULE, "fpm_class");
	if(my_class == NULL) {
		printk(KERN_ALERT "[fpm_init] Failed class create!\n");
		goto fail_0;
	}
	printk(KERN_INFO "[fpm_init] Successful class chardev1 create!\n");

	/* Secondly, device_create is used to create devices in a region. */
	my_device_fpm = device_create(my_class, NULL, MKDEV(MAJOR(my_dev_id), 0), NULL, "fpm-ip");
	if(my_device_fpm == NULL) {
		goto fail_1;
	}
	printk(KERN_INFO "[fpm_init] Device fpm-ip created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 2);
	if(ret) {
		printk(KERN_ERR "[fpm_init] Failed to add cdev\n");
		goto fail_2;
	}
	printk(KERN_INFO "[fpm_init] Module init done\n");

	return platform_driver_register(&fpm_driver);

	fail_2:
		device_destroy(my_class, MKDEV(MAJOR(my_dev_id),1));
	fail_1:
		class_destroy(my_class);
	fail_0:
		unregister_chrdev_region(my_dev_id, 2);
	return -1;
} 

/* Exit function being called and executed only once by rmmod command. */
static void __exit fpm_exit(void)
{
	/* Exit Device Module */
	platform_driver_unregister(&fpm_driver);
	device_destroy(my_class, MKDEV(MAJOR(my_dev_id),1));
	class_destroy(my_class);
	unregister_chrdev_region(my_dev_id, 2);
	printk(KERN_INFO "[fpm_exit] Exit device module finished\"%s\".\n", DEVICE_NAME);
}

module_init(fpm_init);	
module_exit(fpm_exit);  

/* -------------------------------------- */
/* -----PROBE AND REMOVE FUNCTIONS------- */
/* -------------------------------------- */

/* Probe function attempts to find and match a device connected to system with a driver that exists in a system */
/* If successful, memory space will be allocated for a device */
static int fpm_probe(struct platform_device *pdev) 
{
	struct resource *r_mem;
	int rc = 0;
	
	/* Get physical register address space from device tree */
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if(!r_mem){
		printk(KERN_ALERT "fpm_probe: Failed to get reg resource.\n");
		return -ENODEV;
	}
	
	printk(KERN_ALERT "[fpm_probe] Probing fpm_p\n");
	
	/* Allocate memory space for structure fpm_info */ 
	fpm_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
	if(!fpm_p) {
		printk(KERN_ALERT "[fpm_probe] Could not allocate fpm device\n");
		return -ENOMEM;
	}

	/* Put phisical addresses in fpm_info structure */
	fpm_p->mem_start = r_mem->start;
	fpm_p->mem_end = r_mem->end;

	/* Reserve that memory space for this driver */
	if(!request_mem_region(fpm_p->mem_start, fpm_p->mem_end - fpm_p->mem_start + 1,	DEVICE_NAME)) {
		printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)fpm_p->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	/* Remap physical addresses to virtual addresses */
	fpm_p->base_addr = ioremap(fpm_p->mem_start, fpm_p->mem_end - fpm_p->mem_start + 1);
	if (!fpm_p->base_addr) {
		printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
		rc = -EIO;
		goto error2;
	}
	
	printk(KERN_INFO "[fpm_probe] fpm-ip base address start at %x\n", fpm_p->base_addr);
		
	/* Get irq number */
	fpm_p->irq_num = platform_get_irq(pdev, 0);
	if(!fpm_p->irq_num) {
		printk(KERN_ERR "[fpm_probe] Could not get IRQ resource\n");
		rc = -ENODEV;
		goto error2;
	}

	if (request_irq(fpm_p->irq_num, fpm_isr, IRQF_TRIGGER_RISING, DEVICE_NAME, fpm_p)) {
		printk(KERN_ERR "[fpm_probe] Could not register IRQ %d\n", fpm_p->irq_num);
		return -EIO;
		goto error3;
	}
	else {
		printk(KERN_INFO "[fpm_probe] Registered IRQ %d\n", fpm_p->irq_num);
	}

	enable_irq(fpm_p->irq_num);

	iowrite32(IP_COMMAND_RESET, fpm_p->base_addr);
	printk(KERN_INFO "[fpm_probe] fpm IP reset\n");

	printk(KERN_NOTICE "[fpm_probe] fpm platform driver registered - fpm-ip \n");
	return 0;

	error3:
		iounmap(fpm_p->base_addr);
	error2:
		release_mem_region(fpm_p->mem_start, fpm_p->mem_end - fpm_p->mem_start + 1);
		kfree(fpm_p);
	error1:
		return rc;			
}

static int fpm_remove(struct platform_device *pdev) 
{
	printk(KERN_ALERT "[fpm_remove] fpm_p device platform driver removed\n");
	iowrite32(0, fpm_p->base_addr);
	free_irq(fpm_p->irq_num, fpm_p);
	printk(KERN_INFO "[fpm_remove] IRQ number for fpm free\n");
	iounmap(fpm_p->base_addr);
	release_mem_region(fpm_p->mem_start, fpm_p->mem_end - fpm_p->mem_start + 1);
	kfree(fpm_p);

	printk(KERN_INFO "[fpm_remove] Succesfully removed driver\n");
	return 0;
}

/* -------------------------------------- */
/* ------OPEN AND CLOSE FUNCTIONS-------- */
/* -------------------------------------- */

int fpm_open(struct inode *pinode, struct file *pfile)
{
	printk(KERN_INFO "[fpm_open] Succesfully opened driver\n");
	return 0;
}

int fpm_close(struct inode *pinode, struct file *pfile)
{
	printk(KERN_INFO "[fpm_close] Succesfully closed driver\n");
	return 0;
}

/* -------------------------------------- */
/* -------READ AND WRITE FUNCTIONS------- */
/* -------------------------------------- */

ssize_t fpm_read(struct file *pfile, char __user *buf, size_t length, loff_t *offset)
{		
	printk(KERN_INFO "[fpm_read] Succesfully read driver\n");
	return 0;
}

ssize_t fpm_write(struct file *pfile, const char __user *buf, size_t length, loff_t *offset)
{
	printk(KERN_INFO "[fpm_write] Succesfully wrote in driver\n");
	return 0;
}

/* -------------------------------------- */
/* ------INTERRUPT SERVICE ROUTINES------ */
/* -------------------------------------- */

static irqreturn_t fpm_isr(int irq, void *dev_id)
{
	printk(KERN_INFO "[fpm_isr] IP finished operation\n");
	return 0;
}
