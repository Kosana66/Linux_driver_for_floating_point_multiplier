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
#include <linux/wait.h>
#include <linux/semaphore.h>

MODULE_AUTHOR("Kosana Mina Matija");
MODULE_DESCRIPTION("FPM IP core driver");
MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_NAME 	"fpm_driver" 
#define BUFF_SIZE 	200
#define NIZ_SIZE 	5

int counter_out = 0;
int pos_out = 0;
int endRead = 0;
int pos_in = 0;
int cnt_in = 0;
int cnt = 0;
struct semaphore sem;

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

#define DMACR_RUN_STOP				1
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

int dma_init0(void __iomem *base_address);
int dma_init1(void __iomem *base_address);
int dma_init2(void __iomem *base_address);
unsigned int dma_simple_write1(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address); 
unsigned int dma_simple_write2(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address); 
unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address);

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
u32 *tx_vir_buffer;
int device_fsm = 0;
volatile int transaction_over0 = 0;
volatile int transaction_over1 = 0;
volatile int transaction_over2 = 0;
u32 izlazni_niz[NIZ_SIZE];
u32 ulazni_niz[NIZ_SIZE * 2];

/* -------------------------------------- */
/* -------INIT AND EXIT FUNCTIONS-------- */
/* -------------------------------------- */

static int __init fpm_init(void) {
	int ret = 0;
	printk(KERN_INFO "[fpm_init] Initialize Module \"%s\"\n", DRIVER_NAME);
	ret = alloc_chrdev_region(&my_dev_id, 0, 1, "fpm_region");
	if(ret) {
		printk(KERN_ALERT "[fpm_init] Failed CHRDEV!\n");
		return -1;
	}
	printk(KERN_INFO "[fpm_init] Successful CHRDEV!\n");
	my_class = class_create(THIS_MODULE, "fpm_class");
	if(my_class == NULL) {
		printk(KERN_ALERT "[fpm_init] Failed class create!\n");
		goto fail_0;
	}
	printk(KERN_INFO "[fpm_init] Successful class chardev create!\n");
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
	tx_vir_buffer = dma_alloc_coherent(my_device, MAX_PKT_LEN, &tx_phy_buffer, GFP_DMA | GFP_KERNEL);
	printk(KERN_INFO "VIRTAUL %#x\n", *tx_vir_buffer);
	printk(KERN_INFO "[fpm_init] Virtual and physical addresses coherent starting at %#x and ending at %#x\n", tx_phy_buffer, tx_phy_buffer+(uint)(MAX_PKT_LEN));
	if(!tx_vir_buffer) {
		printk(KERN_ALERT "[fpm_init] Could not allocate dma_alloc_coherent");
		goto fail_3;
	}
	else {
		printk("[fpm_init] Successfully allocated memory for transaction buffer\n");
	}
	*tx_vir_buffer = 0;
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

static int fpm_probe(struct platform_device *pdev)  {
	struct resource *r_mem;
	int rc = 0;
	switch(device_fsm){
		case 0:
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma0_p\n");
			dma0_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma0_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma0 device\n");
				return -ENOMEM;
			}
			dma0_p->mem_start = r_mem->start;
			dma0_p->mem_end = r_mem->end;
			if(!request_mem_region(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1,	"dma0_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma0_p->mem_start);
				rc = -EBUSY;
				goto error01;
			}
			dma0_p->base_addr = ioremap(dma0_p->mem_start, dma0_p->mem_end - dma0_p->mem_start + 1);
			if (!dma0_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error02;
			}
			printk(KERN_INFO "[fpm_probe] dma0 base address start at %#x\n", (u32)dma0_p->base_addr);
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
			dma_init0(dma0_p->base_addr);
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
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma1_p\n");
			dma1_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma1_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma1 device\n");
				return -ENOMEM;
			}
			dma1_p->mem_start = r_mem->start;
			dma1_p->mem_end = r_mem->end;
			if(!request_mem_region(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1,	"dma1_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma1_p->mem_start);
				rc = -EBUSY;
				goto error11;
			}
			dma1_p->base_addr = ioremap(dma1_p->mem_start, dma1_p->mem_end - dma1_p->mem_start + 1);
			if (!dma1_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error12;
			}
			printk(KERN_INFO "[fpm_probe] dma1 base address start at %#x\n", (u32)dma1_p->base_addr);
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
			dma_init1(dma1_p->base_addr);
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
			r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
			if(!r_mem){
				printk(KERN_ALERT "[fpm_probe] Failed to get reg resource.\n");
				return -ENODEV;
			}
			printk(KERN_ALERT "[fpm_probe] Probing dma2_p\n");
			dma2_p = (struct fpm_info *) kmalloc(sizeof(struct fpm_info), GFP_KERNEL);
			if(!dma2_p) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate dma2 device\n");
				return -ENOMEM;
			}
			dma2_p->mem_start = r_mem->start;
			dma2_p->mem_end = r_mem->end;
			if(!request_mem_region(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1,	"dma2_device")) {
				printk(KERN_ALERT "[fpm_probe] Could not lock memory region at %p\n",(void *)dma2_p->mem_start);
				rc = -EBUSY;
				goto error21;
			}
			dma2_p->base_addr = ioremap(dma2_p->mem_start, dma2_p->mem_end - dma2_p->mem_start + 1);
			if (!dma2_p->base_addr) {
				printk(KERN_ALERT "[fpm_probe] Could not allocate memory\n");
				rc = -EIO;
				goto error22;
			}
			printk(KERN_INFO "[fpm_probe] dma2 base address start at %#x\n",(u32) dma2_p->base_addr);
			
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
			dma_init2(dma2_p->base_addr);		
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
	int ret = 0; 

	if(endRead) {
		endRead = 0;
		counter_out = 0;
		return 0;
	}
	if(pos_out > 0) {
		if(counter_out < pos_out) {
			length = scnprintf(buff, BUFF_SIZE, "RES %d: %#x\n", counter_out, izlazni_niz[counter_out]);
			ret = copy_to_user(buf, buff, length);
			if(ret) {
				printk(KERN_WARNING "[fpm_read] Copy to user failed\n");
				return -EFAULT;
			}
			counter_out++;
		}
		if(counter_out == pos_out) {
			endRead = 1;
			counter_out = 0;
			pos_out = 0;
			pos_in = 0;
			cnt_in = 0;
			cnt = 0;
		}
	}	
	else {
		printk(KERN_INFO "[fpm_read] Driver is empty\n");
	}
	//printk(KERN_INFO "[fpm_read] Succesfully read driver\n");
	return length;

}

ssize_t fpm_write(struct file *pfile, const char __user *buf, size_t length, loff_t *offset) {
/*	char buff[BUFF_SIZE];
	int ret = 0;
	int ceoDeo1, ceoDeo2, razlomljeniDeo1, razlomljeniDeo2;
	u32 sign, exp, mantissa;
	exp = 0;
	ret = copy_from_user(buff, buf, length);
    	if (ret) {
       		 printk(KERN_WARNING "[fpm_write] copy from user failed\n");
       		 return -EFAULT;
   	}
	buff[length] = '\0'; 
	ret = sscanf(buff, "%d.%d, %d.%d ", &ceoDeo1, &ceoDeo2, &razlomljeniDeo1, &razlomljeniDeo2);
	if(ret != 4)r
		printk(kERN_WARNING "[fpm_write] parsing failed\n");
        	return -EFAULT;
	i}
	sign = (ceoDeo1 < 0) ? 1 : 0;
	while(ceoDeo1 >= 2) {
		ceoDeo1 /= 2;
		exp++;
	}	
	exp += 127;
	  */
	


	
	

	
	char buff[BUFF_SIZE];
	int ret;
	char str1[50];
	char str2[50];
	long int tmp1, tmp2;
	if(pos_in < (NIZ_SIZE*2-1)) {
		ret = copy_from_user(buff, buf, length);
    		if (ret) {
       			 printk(KERN_WARNING "[fpm_write] copy from user failed\n");
       			 return -EFAULT;
   		}
		buff[length] = '\0';
		ret = sscanf(buff, "%50[^,], %50[^ ] ", str1, str2);
		if(ret != 2){
			printk(KERN_WARNING "[fpm_write] parsing failed\n");
       		 	return -EFAULT;
		}
		if(str1[0] == '0' && str1[1] == 'x') {
			ret = kstrtol(str1 + 2, 16, &tmp1);
			ulazni_niz[pos_in] = (u32)tmp1;
			printk(KERN_INFO "[fpm_write] POS %d: %#x\n", pos_in, ulazni_niz[pos_in]);	
			pos_in++;
		}	
		if(str2[0] == '0' && str2[1] == 'x') {
			ret = kstrtol(str2 + 2, 16, &tmp2);
			ulazni_niz[pos_in] = (u32)tmp2;
			printk(KERN_INFO "[fpm_write] POS %d: %#x\n", pos_in, ulazni_niz[pos_in]);
			pos_in++;
		} 
		if(cnt == 0) {
			cnt++;
			*tx_vir_buffer = ulazni_niz[cnt_in++];
			transaction_over0 = 1;
			dma_simple_write1(tx_phy_buffer, MAX_PKT_LEN, dma0_p->base_addr);
			*tx_vir_buffer = ulazni_niz[cnt_in++];
			transaction_over1 = 1;
			dma_simple_write2(tx_phy_buffer, MAX_PKT_LEN, dma1_p->base_addr);		
		}
	}
	else {
		printk(KERN_WARNING "[fpm_write] Driver is full\n");
		cnt = 1;
	}

	
	return length;

	
	
	

	
	
	
	
	
	
	
	
	
	
	
	
	
	
/*	
	
	char buff[BUFF_SIZE];
	int ret;
	char str1[50];
	char str2[50];
	long int tmp1, tmp2;
	if(pos_in < (NIZ_SIZE*2-1)) {
	//	if(down_interruptible(&sem))
	//		return -ERESTARTSYS;
		ret = copy_from_user(buff, buf, length);
    		if (ret) {
       			 printk(KERN_WARNING "[fpm_write] copy from user failed\n");
       			 return -EFAULT;
   		}
		buff[length] = '\0';
		ret = sscanf(buff, "%50[^,], %50[^ ] ", str1, str2);
		if(ret != 2){
			printk(KERN_WARNING "[fpm_write] parsing failed\n");
       		 	return -EFAULT;
		}
		if(str1[0] == '0' && str1[1] == 'x') {
			ret = kstrtol(str1 + 2, 16, &tmp1);
			ulazni_niz[pos_in] = (u32)tmp1;
			*tx_vir_buffer = ulazni_niz[pos_in];
			transaction_over1 = 1;
			dma_simple_write2(tx_phy_buffer, MAX_PKT_LEN, dma1_p->base_addr);
			printk(KERN_INFO "[fpm_write] POS %d: %#x\n", pos_in, ulazni_niz[pos_in]);	
		
			}
		pos_in++;
		if(str2[0] == '0' && str2[1] == 'x') {
			ret = kstrtol(str2 + 2, 16, &tmp2);
			ulazni_niz[pos_in] = (u32)tmp2;
			printk(KERN_INFO "[fpm_write] POS %d: %#x\n", pos_in, ulazni_niz[pos_in]);
			*tx_vir_buffer = ulazni_niz[pos_in];
			transaction_over0 = 1;
			dma_simple_write1(tx_phy_buffer, MAX_PKT_LEN, dma0_p->base_addr);
		}
		pos_in++; 
		dma_simple_read(tx_phy_buffer, MAX_PKT_LEN, dma2_p->base_addr);
		izlazni_niz[pos_out] = *tx_vir_buffer;
		printk(KERN_INFO "[fpm_write] RES %d: %#x\n", pos_out, izlazni_niz[pos_out]);
		pos_out++;
	//	up(&sem);
	}
	else {
		printk(KERN_WARNING "[fpm_write] Driver is full\n");
	}
	return length;



*/
	printk(KERN_INFO "[fpm_write] Succesfully wrote in driver\n");
	return length;
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
	ret = dma_mmap_coherent(my_device, vma_s, (void *)tx_vir_buffer, tx_phy_buffer, length);
	if(ret < 0) {
		printk(KERN_ERR "[fpm_dma_mmap] Memory map DMA failed\n");
		return ret;
	}
	return 0;
}

/* -------------------------------------- */
/* ------------DMA FUNCTIONS------------- */
/* -------------------------------------- */

int dma_init0(void __iomem *base_address) {
	u32 MM2S_DMACR_val = 0;
	u32 enInterrupt = 0;
	iowrite32(0x0, base_address + MM2S_DMACR_REG);
	iowrite32(DMACR_RESET, base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	enInterrupt = MM2S_DMACR_val | IOC_IRQ_EN | ERR_IRQ_EN;
	iowrite32(enInterrupt, base_address + MM2S_DMACR_REG);	
	printk(KERN_INFO "[dma0_init] Successfully initialized DMA0 \n");
	return 0;
}
int dma_init1(void __iomem *base_address) {
	u32 MM2S_DMACR_val = 0;
	u32 enInterrupt = 0;
	iowrite32(0x0, base_address + MM2S_DMACR_REG);
	iowrite32(DMACR_RESET, base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	enInterrupt = MM2S_DMACR_val | IOC_IRQ_EN | ERR_IRQ_EN;
	iowrite32(enInterrupt, base_address + MM2S_DMACR_REG);	
	printk(KERN_INFO "[dma1_init] Successfully initialized DMA1 \n");
	return 0;
}
int dma_init2(void __iomem *base_address) {
	u32 S2MM_DMACR_val = 0;
	u32 enInterrupt = 0;
	iowrite32(0x0, base_address + S2MM_DMACR_REG);
	iowrite32(DMACR_RESET, base_address + S2MM_DMACR_REG);
	S2MM_DMACR_val = ioread32(base_address + S2MM_DMACR_REG);
	enInterrupt = S2MM_DMACR_val | IOC_IRQ_EN | ERR_IRQ_EN;
	iowrite32(enInterrupt, base_address + S2MM_DMACR_REG);	
	printk(KERN_INFO "[dma2_init] Successfully initialized DMA2 \n");
	return 0;
}


unsigned int dma_simple_write1(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 MM2S_DMACR_val = 0;
	u32 enInterrupt = 0;
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	enInterrupt = MM2S_DMACR_val | IOC_IRQ_EN | ERR_IRQ_EN;
	iowrite32(enInterrupt, base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val |= DMACR_RUN_STOP;
	iowrite32(MM2S_DMACR_val, base_address + MM2S_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + MM2S_SA_REG);
	iowrite32(pkt_len, base_address + MM2S_LENGTH_REG);
	while(transaction_over0 == 1);
	printk(KERN_INFO "[dma_simple_write1] Successfully wrote in DMA0 \n");
	return 0;
	
}
unsigned int dma_simple_write2(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 MM2S_DMACR_val = 0;
	u32 enInterrupt = 0;
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	enInterrupt = MM2S_DMACR_val | IOC_IRQ_EN | ERR_IRQ_EN;
	iowrite32(enInterrupt, base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val = ioread32(base_address + MM2S_DMACR_REG);
	MM2S_DMACR_val |= DMACR_RUN_STOP;
	iowrite32(MM2S_DMACR_val, base_address + MM2S_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + MM2S_SA_REG);
	iowrite32(pkt_len, base_address + MM2S_LENGTH_REG);	
	while(transaction_over1 == 1);
	printk(KERN_INFO "[dma_simple_write2] Successfully wrote in DMA1 \n");
	dma_simple_read(tx_phy_buffer, MAX_PKT_LEN, dma2_p->base_addr);
	return 0;
}
unsigned int dma_simple_read(dma_addr_t TxBufferPtr, unsigned int pkt_len, void __iomem *base_address) {
	u32 S2MM_DMACR_value;
	S2MM_DMACR_value = ioread32(base_address + S2MM_DMACR_REG);
	S2MM_DMACR_value |= DMACR_RUN_STOP; 
	iowrite32(S2MM_DMACR_value, base_address + S2MM_DMACR_REG);
	iowrite32((u32)TxBufferPtr, base_address + S2MM_DA_REG);
	iowrite32(pkt_len, base_address + S2MM_LENGTH_REG);
	transaction_over2 = 1;
	while(transaction_over2 == 1);
	izlazni_niz[pos_out] = *tx_vir_buffer;
	printk(KERN_INFO "[fpm_write] RES %d: %#x\n", pos_out, izlazni_niz[pos_out]);
	printk(KERN_INFO "[dma_simple_read] Successfully read from DMA2 \n");	
	pos_out++;
	if(cnt_in < (pos_in - 1)) {
		*tx_vir_buffer = ulazni_niz[cnt_in++];
		transaction_over0 = 1;
		dma_simple_write1(tx_phy_buffer, MAX_PKT_LEN, dma0_p->base_addr);
		*tx_vir_buffer = ulazni_niz[cnt_in++];
		transaction_over1 = 1;
		dma_simple_write2(tx_phy_buffer, MAX_PKT_LEN, dma1_p->base_addr);		
	} 
	else {
		cnt = 0;
	}
	return 0;
}

/* -------------------------------------- */
/* ------INTERRUPT SERVICE ROUTINES------ */
/* -------------------------------------- */

static irqreturn_t dma0_MM2S_isr(int irq, void* dev_id) {
	unsigned int IrqStatus;  
	IrqStatus = ioread32(dma0_p->base_addr + MM2S_STATUS_REG);
	iowrite32(IrqStatus | 0x00007000, dma0_p->base_addr + MM2S_STATUS_REG);
	transaction_over0 = 0;
	printk(KERN_INFO "[dma0_isr] Finished DMA0 MM2S transaction!\n");
	return IRQ_HANDLED;
}
static irqreturn_t dma1_MM2S_isr(int irq, void* dev_id) {
	unsigned int IrqStatus;  
	IrqStatus = ioread32(dma1_p->base_addr + MM2S_STATUS_REG);
	iowrite32(IrqStatus | 0x00007000, dma1_p->base_addr + MM2S_STATUS_REG);
	transaction_over1 = 0;
	printk(KERN_INFO "[dma1_isr] Finished DMA1 MM2S transaction!\n");
	return IRQ_HANDLED;
}
static irqreturn_t dma2_S2MM_isr(int irq, void* dev_id){
	unsigned int IrqStatus;  
	IrqStatus = ioread32(dma2_p->base_addr + S2MM_STATUS_REG);
	iowrite32(IrqStatus | 0x00007000, dma2_p->base_addr + S2MM_STATUS_REG);
	transaction_over2 = 0;
	printk(KERN_INFO "[dma2_isr] Finished DMA2 S2MM transaction!\n");
	return IRQ_HANDLED;
}

