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
#include <linux/of.h>

#include <linux/dma-mapping.h>  
#include <linux/mm.h>

MODULE_AUTHOR("Kosana Mina Matija");
MODULE_DESCRIPTION("FPM IP core driver");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_NAME "fpm_driver" 
#define BUFF_SIZE 20

/* -------------------------------------- */
/* --------FPM IP RELATED MACROS--------- */
/* -------------------------------------- */






/* -------------------------------------- */
/* ----------DMA RELATED MACROS---------- */
/* -------------------------------------- */

#define MAX_PKT_LEN				4
#define MM2S_DMACR_REG				0x00
#define MM2S_SA_REG				0x18
#define MM2S_LENGTH_REG			0x28
#define MM2S_STATUS_REG			0x04

#define S2MM_DMACR_REG				0x30
#define S2MM_DA_REG				0x48
#define S2MM_LENGTH_REG			0x58
#define S2MM_STATUS_REG			0x34

#define DMACR_RUN_STOP				1<<1
#define DMACR_RESET				1<<2
#define IOC_IRQ_EN				1<<12
#define ERR_IRQ_EN				1<<14


/* -------------------------------------- */
/* --------FUNCTION DECLARATIONS--------- */
/* -------------------------------------- */

static int  fpm_probe(struct platform_device *pdev);
static int  fpm_remove(struct platform_device *pdev);
int         fpm_open(struct inode *pinode, struct file *pfile);
int         fpm_close(struct inode *pinode, struct file *pfile);
ssize_t     fpm_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t     fpm_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int  fpm_mmap(struct file *f, struct vm_area_struct *vma_s);

static int  __init fpm_init(void);
static void __exit fpm_exit(void);

static irqreturn_t dma0_MM2S_isr(int irq, void* dev_id);
static irqreturn_t dma1_MM2S_isr(int irq, void* dev_id);
static irqreturn_t dma2_S2MM_isr(int irq, void* dev_id);

int dma_init(void __iomem *base_address);
unsigned int dma_simple_write1(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address); 
unsigned int dma_simple_write2(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address); 
unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address);

float castBinToFloat(u32 binaryValue);
uint32_t castFloatToBin(float floatNumber);

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
static struct device *my_device;
static struct cdev *my_cdev;
static struct fpm_info *dma0_p = NULL;
static struct fpm_info *dma1_p = NULL;
static struct fpm_info *dma2_p = NULL;

struct file_operations my_fops = {
	.owner 		= THIS_MODULE,
	.open 		= fpm_open,
	.release 	= fpm_close,
	.read 		= fpm_read,
	.write 		= fpm_write,
	.mmap		= fpm_mmap
};

static struct of_device_id fpm_of_match[] = {
	{ .compatible = "xlnx,axi-dma-0", },
	{ .compatible = "xlnx,axi-dma-1", },
	{ .compatible = "xlnx,axi-dma-2", },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, fpm_of_match);

static struct platform_driver fpm_driver = {
	.driver = {
		.name 			= DRIVER_NAME,
		.owner 			= THIS_MODULE,
		.of_match_table	= fpm_of_match,
	},
	.probe		= fpm_probe,
	.remove		= fpm_remove,
};

dma_addr_t tx_phy_buffer;
u32 tx_vir_buffer;
int device_fsm = 0;
int transaction_over0 = 0;
int transaction_over1 = 0;
int transaction_over2 = 0;

/* -------------------------------------- */
/* -------INIT AND EXIT FUNCTIONS-------- */
/* -------------------------------------- */

/* Init function being called and executed only once by insmod command. */
static int __init fpm_init(void) {
	int ret = 0;
	printk(KERN_INFO "[fpm_init] Initialize Module \"%s\"\n", DRIVER_NAME);

	/* Dynamically allocate MAJOR and MINOR numbers. */
	ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fpm_region");
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
	printk(KERN_INFO "[fpm_init] Successful class chardev create!\n");
	/* Secondly, device_create is used to create devices in a region. */
	my_device = device_create(my_class, NULL, MKDEV(MAJOR(my_dev_id), 0), NULL, "fpmult");
	if(my_device == NULL) {
		goto fail_1;
	}
	printk(KERN_INFO "[fpm_init] Device fpmult created\n");

	my_cdev = cdev_alloc();	
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if(ret) {
		printk(KERN_ERR "[fpm_init] Failed to add cdev\n");
		goto fail_2;
	}
	printk(KERN_INFO "[fpm_init] Module init done\n");

	tx_vir_buffer = dma_alloc_coherent(my_device, MAX_PKT_LEN, &tx_phy_buffer, GFP_KERNEL);
	printk(KERN_INFO "[fpm_init] Virtual and physical addresses coherent starting at %x and ending at %x\n", tx_phy_buffer, tx_phy_buffer+(uint)(MAX_PKT_LEN));
	if(!tx_vir_buffer) {
		printk(KERN_ALERT "[fpm_init] Could not allocate dma_alloc_coherent");
		goto fail_3;
	}
	else {
		printk("[fpm_init] Successfully allocated memory for transaction buffer\n");
	}
	printk(KERN_INFO "[fpm_init] Memory reset.\n");
	return platform_driver_register(&fpm_driver);
	
	fail_3:
		cdev_del(my_cdev);
	fail_2:
		device_destroy(my_class, MKDEV(MAJOR(my_dev_id),0));
	fail_1:
		class_destroy(my_class);
	fail_0:
		unregister_chrdev_region(my_dev_id, 1);
	return -1;
} 

/* Exit function being called and executed only once by rmmod command. */
static void __exit fpm_exit(void) {
	/* Exit Device Module */
	platform_driver_unregister(&fpm_driver);
	cdev_del(my_cdev);
	device_destroy(my_class, MKDEV(MAJOR(my_dev_id),0));
	class_destroy(my_class);
	unregister_chrdev_region(my_dev_id, 1);
	printk(KERN_INFO "[fpm_exit] Exit module finished\"%s\".\n", DRIVER_NAME);
}

module_init(fpm_init);	
module_exit(fpm_exit);  

/* -------------------------------------- */
/* -----PROBE AND REMOVE FUNCTIONS------- */
/* -------------------------------------- */

/* Probe function attempts to find and match a device connected to system with a driver that exists in a system */
/* If successful, memory space will be allocated for a device */
static int fpm_probe(struct platform_device *pdev)  {
	struct resource *r_mem;
	int rc = 0;

	switch(device_fsm){
		case 0:
			/* Get physical register address space from device tree */
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma0_p\n");
			
			/* Allocate memory space for structure fpm_info */ 
			dma0_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma0_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma0 device\n");
				return -ENOMEM;
			}
			/* Put phisical addresses in fpm_info structure */
			dma0_p->mem_start = r_mem->start;
			dma0_p->mem_end = r_mem->end;
			/* Reserve that memory space for this driver */
			if(!request_mem_region(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1,	"dma0_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma0_p->mem_start);
				rc = -EBUSY;
				goto error01;
			}
			/* Remap physical addresses to virtual addresses */
			dma0_p->base_addr = ioremap(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1);
			if (!dma0_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error02;
			}
			printk(KERN_INFO "[fpm_probe] dma0 base address start at %x\n", dma0_p->base_addr);
			
			dma0_p->irq_num = platform_get_irq(pdev, 0);
			if(!dma0_p->irq_num) {
				printk(KERN_ERR "[fpm_probe] Could not get IRQ resource for dma0\n");
				rc = -ENODEV;
				goto error03;
			}
			if (request_irq(dma0_p->irq_num, dma0_MM2S_isr, 0, "dma0_device", dma0_p)) {
				printk(KERN_ERR "[fpm_probe] Could not register IRQ %d\n", dma0_p->irq_num);
				return -EIO;
				goto error03;
			}
			else {
				printk(KERN_INFO "[fpm_probe] Registered IRQ %d\n", dma0_p->irq_num);
			}
			enable_irq(dma0_p->irq_num);
			
			/* INIT DMA */
			dma_init(dma0_p->base_addr);
			printk(KERN_NOTICE "[fpm_probe] fpm platform driver registered - dma0\n");
			device_fsm++;
			return 0;

			error03:
				iounmap(dma0_p->base_addr);
			error02:
				release_mem_region(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1);
				kfree(dma0_p);
			error01:
				return rc;	
		break;
		case 1:
			/* Get physical register address space from device tree */
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma1_p\n");
			
			/* Allocate memory space for structure fpm_info */ 
			dma1_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma1_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma1 device\n");
				return -ENOMEM;
			}
			/* Put phisical addresses in fpm_info structure */
			dma1_p->mem_start = r_mem->start;
			dma1_p->mem_end = r_mem->end;
			/* Reserve that memory space for this driver */
			if(!request_mem_region(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1,	"dma1_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma1_p->mem_start);
				rc = -EBUSY;
				goto error11;
			}
			/* Remap physical addresses to virtual addresses */
			dma1_p->base_addr = ioremap(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1);
			if (!dma1_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error12;
			}
			printk(KERN_INFO "[fpm_probe] dma1 base address start at %x\n", dma1_p->base_addr);
			
			dma1_p->irq_num = platform_get_irq(pdev, 0);
			if(!dma1_p->irq_num) {
				printk(KERN_ERR "[fpm_probe] Could not get IRQ resource for dma1\n");
				rc = -ENODEV;
				goto error13;
			}
			if (request_irq(dma1_p->irq_num, dma1_MM2S_isr, 0, "dma1_device", dma1_p)) {
				printk(KERN_ERR "[fpm_probe] Could not register IRQ %d\n", dma1_p->irq_num);
				return -EIO;
				goto error13;
			}
			else {
				printk(KERN_INFO "[fpm_probe] Registered IRQ %d\n", dma1_p->irq_num);
			}
			enable_irq(dma1_p->irq_num);
			
			/* INIT DMA */
			dma_init(dma1_p->base_addr);
			printk(KERN_NOTICE "[fpm_probe] fpm platform driver registered - dma1\n");
			device_fsm++;
			return 0;
			
			error13:
				iounmap(dma1_p->base_addr);
			error12:
				release_mem_region(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1);
				kfree(dma1_p);
			error11:
				return rc;	
		break;
		case 2:			
			/* Get physical register address space from device tree */
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma2_p\n");

			/* Allocate memory space for structure fpm_info */ 
			dma2_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma2_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma2 device\n");
				return -ENOMEM;
			}
			/* Put phisical addresses in fpm_info structure */
			dma2_p->mem_start = r_mem->start;
			dma2_p->mem_end = r_mem->end;
			/* Reserve that memory space for this driver */
			if(!request_mem_region(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1,	"dma2_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma2_p->mem_start);
				rc = -EBUSY;
				goto error21;
			}
			/* Remap physical addresses to virtual addresses */
			dma2_p->base_addr = ioremap(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1);
			if (!dma2_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error22;
			}
			printk(KERN_INFO "[fpm_probe] dma2 base address start at %x\n", dma2_p->base_addr);
			
			dma2_p->irq_num = platform_get_irq(pdev, 0);
			if(!dma2_p->irq_num) {
				printk(KERN_ERR "[fpm_probe] Could not get IRQ resource for dma2\n");
				rc = -ENODEV;
				goto error23;
			}
			if (request_irq(dma2_p->irq_num, dma2_S2MM_isr, 0, "dma2_device", dma2_p)) {
				printk(KERN_ERR "[fpm_probe] Could not register IRQ %d\n", dma2_p->irq_num);
				return -EIO;
				goto error23;
			}
			else {
				printk(KERN_INFO "[fpm_probe] Registered IRQ %d\n", dma2_p->irq_num);
			}
			enable_irq(dma2_p->irq_num);
			
			/* INIT DMA */
			dma_init(dma2_p->base_addr);		
			printk(KERN_NOTICE "[fpm_probe] fpm platform driver registered - dma2\n");	
			return 0;
			
			error23:
				iounmap(dma2_p->base_addr);
			error22:
				release_mem_region(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1);
				kfree(dma2_p);
			error21:
				return rc;	
		
		break;
		default:
			printk(KERN_NOTICE "[fpm_probe] Devices weren't be detected\n");
			return -1;
		break;
	}
}

static int fpm_remove(struct platform_device *pdev)  {
	switch(device_fsm){
		case 0:
			printk(KERN_ALERT "[fpm_remove] dma0_p device platform driver removed\n");
			iowrite32(0, dma0_p->base_addr);
			free_irq(dma0_p->irq_num, dma0_p);
			iounmap(dma0_p->base_addr);
			release_mem_region(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1);
			kfree(dma0_p);
			printk(KERN_INFO "[fpm_remove] Succesfully removed dma0_p device platform driver\n");
			--device_fsm;
		break;
		case 1:
			printk(KERN_ALERT "[fpm_remove] dma1_p device platform driver removed\n");
			iowrite32(0, dma1_p->base_addr);
			free_irq(dma1_p->irq_num, dma1_p);
			iounmap(dma1_p->base_addr);
			release_mem_region(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1);
			kfree(dma1_p);
			printk(KERN_INFO "[fpm_remove] Succesfully removed dma1_p device platform driver\n");
			--device_fsm;
		break;
		case 2:
			printk(KERN_ALERT "[fpm_remove] dma2_p device platform driver removed\n");
			iowrite32(0, dma2_p->base_addr);
			free_irq(dma2_p->irq_num, dma2_p);
			iounmap(dma2_p->base_addr);
			release_mem_region(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1);
			kfree(dma2_p);
			printk(KERN_INFO "[fpm_remove] Succesfully removed dma2_p device platform driver\n");
			--device_fsm;
		break;
		default:
			return -1;
		break;	
	}
	return 0;
}

/* -------------------------------------- */
/* ------OPEN AND CLOSE FUNCTIONS-------- */
/* -------------------------------------- */

int fpm_open(struct inode *pinode, struct file *pfile) {
	printk(KERN_INFO "[fpm_open] Succesfully opened driver\n");
	return 0;
}

int fpm_close(struct inode *pinode, struct file *pfile) {
	printk(KERN_INFO "[fpm_close] Succesfully closed driver\n");
	return 0;
}

/* -------------------------------------- */
/* -------READ AND WRITE FUNCTIONS------- */
/* -------------------------------------- */

ssize_t fpm_read(struct file *pfile, char __user *buf, size_t length, loff_t *offset) {		
	char buff[BUFF_SIZE];
	u32 output;
	float res;
	dma_simple_read(tx_phy_buffer, output, dma2_p->base_addr);
	res = castBinToFloat(output);
	length = scnprintf(buff, BUFF_SIZE, "Res: %f\n", res);
	ret = copy_to_user(buf, buff, length);
	if(ret) {
		printk(KERN_WARNING "[fpm_read] Copy to user failed\n");
		return -EFAULT;
	}
	printk(KERN_INFO "[fpm_read] Succesfully read driver\n");
	return 0;
}

ssize_t fpm_write(struct file *pfile, const char __user *buf, size_t length, loff_t *offset) {
	char buff[BUFF_SIZE];
	char hexString1[10];
	char hexString2[10];
	char string1[300];
	char string2[300];
	double doubleNum1, doubleNum2;
	float floatNum1, floatNum2;
	u32 operand1, operand2;
	int ret = 0;
	ret = copy_from_user(buff, buf, length);  
    if (ret) {
        printk(KERN_WARNING "[fpm_write] Copy from user failed\n");
        return -EFAULT;
    }
	buff[length] = '\0';
	ret = sscanf(buff, "%300[^,], %300[^ ] ", &string1, &string2);
	if(ret != 2){
		printk(KERN_WARNING "[fpm_write] Parsing failed\n");
        return -EFAULT;
	}
	ret = kstrtod(string1, 0, &doubleNum1);
	if(ret < 0) {
		printk(KERN_WARNING "[fpm_write] Converting first operand in double failed\n");
        return -EFAULT;
	}
	floatNum1 = (float)doubleNum1;
	operand1 = castFloatToBin(floatNum1);
	transaction_over0 = 1;
	dma_simple_write1(tx_phy_buffer, operand1, dma0_p->base_addr);

	ret = kstrtod(string2, 0, &doubleNum2);
	if(ret < 0) {
		printk(KERN_WARNING "[fpm_write] Converting second operand in double failed\n");
        return -EFAULT;
	}
	floatNum2 = (float)doubleNum2;
	operand2 = castFloatToBin(floatNum2);
	transaction_over1 = 1;
	dma_simple_write2(tx_phy_buffer, operand2, dma1_p->base_addr);
	printk(KERN_INFO "[fpm_write] Succesfully wrote in driver\n");
	return 0;
}

/* -------------------------------------- */
/* ------------MMAP FUNCTION------------- */
/* -------------------------------------- */

static int fpm_mmap(struct file *f, struct vm_area_struct *vma_s) {
	int ret = 0;
	long length = vma_s->vm_end - vma_s->vm_start;
	printk(KERN_INFO "[fpm_dma_mmap] DMA TX Buffer is being memory mapped\n");

	if(length > MAX_PKT_LEN) {
		return -EIO;
		printk(KERN_ERR "[fpm_dma_mmap] Trying to mmap more space than it's allocated\n");
	}

	ret = dma_mmap_coherent(my_device, vma_s, tx_vir_buffer, tx_phy_buffer, length);
	if(ret < 0) {
		printk(KERN_ERR "[fpm_dma_mmap] Memory map DMA failed\n");
		return ret;
	}
	return 0;
}

/* -------------------------------------- */
/* ------------DMA FUNCTIONS------------- */
/* -------------------------------------- */

int dma_init(void __iomem *base_address) {
	iowrite32(DMACR_RESET | IOC_IRQ_EN | ERR_IRQ_EN;, base_address + MM2S_DMACR_REG);
	iowrite32(DMACR_RESET | IOC_IRQ_EN | ERR_IRQ_EN, base_address + S2MM_DMACR_REG);
	printk(KERN_INFO "[dma_init] Successfully initialized DMA \n");
	return 0;
}

unsigned int dma_simple_write1(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 MM2S_DMACR_value;
	MM2S_DMACR_value = ioread32(base_address + MM2S_DMACR_REG);
	iowrite32(MM2S_DMACR_value | DMACR_RUN_STOP, base_address + MM2S_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + MM2S_SA_REG);
	iowrite32(pkt_len, base_address + MM2S_LENGTH_REG);
	printk(KERN_INFO "[dma_simple_write1] Successfully wrote DMA1 \n");
	return 0;
	
}
unsigned int dma_simple_write2(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 MM2S_DMACR_value;
	MM2S_DMACR_value = ioread32(base_address + MM2S_DMACR_REG);
	iowrite32(MM2S_DMACR_value | DMACR_RUN_STOP, base_address + MM2S_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + MM2S_SA_REG);
	iowrite32(pkt_len, base_address + MM2S_LENGTH_REG);
	printk(KERN_INFO "[dma_simple_write2] Successfully wrote DMA2 \n");
	return 0;
}
unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 S2MM_DMACR_value;
	S2MM_DMACR_value = ioread32(base_address + S2MM_DMACR_REG);
	iowrite32(S2MM_DMACR_value | DMACR_RUN_STOP, base_address + S2MM_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + S2MM_DA_REG);
	iowrite32(pkt_len, base_address + S2MM_LENGTH_REG);
	printk(KERN_INFO "[dma_simple_read] Successfully read DMA \n");
	return 0;
}

/* -------------------------------------- */
/* ------INTERRUPT SERVICE ROUTINES------ */
/* -------------------------------------- */

static irqreturn_t dma0_MM2S_isr(int irq, void* dev_id) {
	unsigned int IrqStatus;  
	/* DMA0 transaction has been complited and interrupt occures, flag needs to be cleared */
	/* Clearing MM2S flag */
	IrqStatus = ioread32(dma0_p->base_addr + MM2S_STATUS_REG);
	iowrite32(IrqStatus | 0x00005000, dma0_p->base_addr + MM2S_STATUS_REG);
	/* Tell rest of the code that interrupt has happened */
	transaction_over0 = 0;
	printk(KERN_INFO "[dma0_isr] Finished DMA0 MM2S transaction!\n");
	return IRQ_HANDLED;
}
static irqreturn_t dma1_MM2S_isr(int irq, void* dev_id) {
	unsigned int IrqStatus;  
	/* DMA1 transaction has been complited and interrupt occures, flag needs to be cleared */
	/* Clearing MM2S flag */
	IrqStatus = ioread32(dma1_p->base_addr + MM2S_STATUS_REG);
	iowrite32(IrqStatus | 0x00005000, dma1_p->base_addr + MM2S_STATUS_REG);
	/* Tell rest of the code that interrupt has happened */
	transaction_over1 = 0;
	printk(KERN_INFO "[dma1_isr] Finished DMA1 MM2S transaction!\n");
	return IRQ_HANDLED;
}
static irqreturn_t dma2_S2MM_isr(int irq, void* dev_id){
	unsigned int IrqStatus;  
	/* DMA2 transaction has been complited and interrupt occures, flag needs to be cleared */
	/* Clearing S2MM flag */
	IrqStatus = ioread32(dma2_p->base_addr + S2MM_STATUS_REG);
	iowrite32(IrqStatus | 0x00005000, dma2_p->base_addr + S2MM_STATUS_REG);
	/* Tell rest of the code that interrupt has happened */
	transaction_over2 = 0;
	printk(KERN_INFO "[dma2_isr] Finished DMA2 S2MM transaction!\n");
	return IRQ_HANDLED;
}
float castBinToFloat(u32 binaryValue) {
	u32 binaryValue_uint = binaryValue;
	int sign = (binaryValue_uint >> 31) & 0x1;
	if (sign == 1) {
		binaryValue_uint = (~binaryValue_uint) + 1; 
	}
	int integerPart = (binaryValue_uint >> 24) & 0x7F;
	int decimalPart = binaryValue_uint & 0xFFFFFF;

	float floatValue = (float)integerPart + ((float)decimalPart / 16777216);
	if (sign == 1) {
		floatValue = floatValue * (-1);
	}
	return floatValue;
}

u32 castFloatToBin(float floatNumber) 
{
    int sign = (floatNumber >= 0) ? 0 : 1;
    float resolution = 0.0000000596046448;
    float half_of_resolution = 0.0000000298023224;
    int tmp;
    u32 binaryValue;
    if (sign == 0) {
        tmp = floatNumber / resolution;
        if (floatNumber >= tmp * resolution + half_of_resolution) {
			tmp++;
		};
		binaryValue = tmp;
    }
    else {
        tmp = floatNumber / resolution * (-1);
        if (floatNumber <= (-1) * tmp * resolution - half_of_resolution) {
			tmp++;
		}
		binaryValue = 4294967296 - tmp;
    }
    return binaryValue;
}
