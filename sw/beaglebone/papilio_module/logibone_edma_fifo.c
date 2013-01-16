#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>   /* copy_to_user */
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <mach/memory.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/edma.h>

#include "hw_cm_per.h"
#include "hw_gpmc.h"
#include "soc_AM335x.h"

/*
#define CS_ON	0
#define CS_OFF	7
#define ADV_ON	0
#define ADV_OFF	2
#define WR_CYC	7
#define WR_ON	3
#define WR_OFF ((CS_ON + CS_OFF)-WR_ON)
#define RD_CYC	7
#define OE_ON	3
#define OE_OFF ((CS_ON + CS_OFF)-OE_ON)
#define RD_ACC_TIME 6
#define WRDATAONADMUX 3  //number of cycle before taking control of data bus (when add/data multiplexing)


//       <--7-->
//CS1	\_______/
//	<-2>_____
//ADV	\__/
//	____ <-4->
//WR	     \___/
//	____ <-4->
//WR	     \___/
*/
// following settings were also tested and proved to work (faster)

#define CS_ON	0
#define CS_OFF	5
#define ADV_ON	0
#define ADV_OFF	2
#define WR_CYC	5
#define WR_ON	3
#define WR_OFF 5
#define RD_CYC	5
#define OE_ON	3
#define OE_OFF 5
#define RD_ACC_TIME 5
#define WRDATAONADMUX 3  //number of cycle before taking control of data bus (when add/data multiplexing)


//       <--4-->
//CS1	\_______/
//	<-1>_____
//ADV	\__/
//	____ <-2->
//WR	     \___/
//	____ <-2->
//WR	     \___/





/* Use 'p' as magic number */
#define LOGIBONE_FIFO_IOC_MAGIC 'p'
/* Please use a different 8-bit number in your code */
#define LOGIBONE_FIFO_RESET _IO(LOGIBONE_FIFO_IOC_MAGIC, 0)


#define LOGIBONE_FIFO_PEEK _IOR(LOGIBONE_FIFO_IOC_MAGIC, 1, short)
#define LOGIBONE_FIFO_NB_FREE _IOR(LOGIBONE_FIFO_IOC_MAGIC, 2, short)
#define LOGIBONE_FIFO_NB_AVAILABLE _IOR(LOGIBONE_FIFO_IOC_MAGIC, 3, short)
#define LOGIBONE_FIFO_MODE _IO(LOGIBONE_FIFO_IOC_MAGIC, 4)
#define LOGIBONE_DIRECT_MODE _IO(LOGIBONE_FIFO_IOC_MAGIC, 5)


//writing to fifo A reading from fifo B

#define FPGA_BASE_ADDR	 0x09000000
#define FIFO_SIZE_OFFSET	4
#define NB_AVAILABLE_A_OFFSET	5
#define NB_AVAILABLE_B_OFFSET	6
#define PEEK_OFFSET	7
#define READ_OFFSET	0
#define WRITE_OFFSET	0

#define ACCESS_SIZE	4  // fifo read write register is on 2 bits address space to allow 4 word burst



char * gDrvrName = "LOGIBONE_fifo";
unsigned char gDrvrMajor = 246 ;
unsigned char gDrvrMinor = 0 ;
unsigned char nbDevices  = 1 ;

volatile unsigned short * gpmc_cs1_pointer ;
volatile unsigned short * gpmc_cs1_virt ;


dma_addr_t dmaphysbuf = 0;
dma_addr_t dmapGpmcbuf = 0x09000000;
static volatile int irqraised1 = 0;

unsigned char * readBuffer ;
unsigned char * readBuffer ;

#define BUFFER_SIZE 2048 



int setupGPMCClock(void) ;
int setupGPMCNonMuxed(void) ;
unsigned short int getNbFree(void);
unsigned short int getNbAvailable(void);
int edma_memtomemcpy(int count, unsigned long src_addr, unsigned long trgt_addr,int event_queue,  enum address_mode mode);
static void dma_callback(unsigned lch, u16 ch_status, void *data);



enum LOGIBONE_fifo_read_mode{
	fifo,
	direct
} read_mode;

unsigned int fifo_size ;


void orShortRegister(unsigned short int value, volatile unsigned int * port){
	unsigned short oldVal ;
	oldVal = ioread32(port);
	iowrite32(oldVal | value, port);
}

int setupGPMCClock(void){
	volatile unsigned int * prcm_reg_pointer ;
	printk("Configuring Clock for GPMC \n");  
	if (check_mem_region(SOC_PRCM_REGS + CM_PER_GPMC_CLKCTRL/4, 4)) {
	    printk("%s: memory already in use\n", gDrvrName);
	    return -EBUSY;
	}
	request_mem_region(SOC_PRCM_REGS + CM_PER_GPMC_CLKCTRL, 4, gDrvrName);
	

	prcm_reg_pointer = ioremap_nocache(SOC_PRCM_REGS + CM_PER_GPMC_CLKCTRL, sizeof(int));
	//enable clock to GPMC module
	
	orShortRegister(CM_PER_GPMC_CLKCTRL_MODULEMODE_ENABLE, prcm_reg_pointer);
	//check to see if enabled
	printk("CM_PER_GPMC_CLKCTRL value :%x \n",ioread32(prcm_reg_pointer)); 
	while((ioread32(prcm_reg_pointer) & 
	CM_PER_GPMC_CLKCTRL_IDLEST) != (CM_PER_GPMC_CLKCTRL_IDLEST_FUNC << CM_PER_GPMC_CLKCTRL_IDLEST_SHIFT));
	printk("GPMC clock is running \n");
	iounmap(prcm_reg_pointer);
	release_mem_region(SOC_PRCM_REGS + CM_PER_GPMC_CLKCTRL/4, 4);

	return 1;
}

int setupGPMCNonMuxed(void){
	unsigned int temp = 0;
	unsigned short int csNum = 1 ;	
	volatile unsigned int * gpmc_reg_pointer ;

	printk("Configuring GPMC for non muxed access \n");	


	if (check_mem_region(SOC_GPMC_0_REGS, 720)) {
	    printk("%s: memory already in use\n", gDrvrName);
	    return -EBUSY;
	}
	request_mem_region(SOC_GPMC_0_REGS, 720, gDrvrName);
	gpmc_reg_pointer = ioremap_nocache(SOC_GPMC_0_REGS,  720);



	printk("GPMC_REVISION value :%x \n", ioread32(gpmc_reg_pointer + GPMC_REVISION/4)); 
	
	orShortRegister(GPMC_SYSCONFIG_SOFTRESET, gpmc_reg_pointer + GPMC_SYSCONFIG/4 ) ;
	printk("Trying to reset GPMC \n"); 
	printk("GPMC_SYSSTATUS value :%x \n", ioread32(gpmc_reg_pointer + GPMC_SYSSTATUS/4)); 
	while((ioread32(gpmc_reg_pointer + GPMC_SYSSTATUS/4) & 
		GPMC_SYSSTATUS_RESETDONE) == GPMC_SYSSTATUS_RESETDONE_RSTONGOING){
		printk("GPMC_SYSSTATUS value :%x \n", ioread32(gpmc_reg_pointer + 
		GPMC_SYSSTATUS/4));
	}
	printk("GPMC reset \n");
	temp = ioread32(gpmc_reg_pointer + GPMC_SYSCONFIG/4);
	temp &= ~GPMC_SYSCONFIG_IDLEMODE;
	temp |= GPMC_SYSCONFIG_IDLEMODE_NOIDLE << GPMC_SYSCONFIG_IDLEMODE_SHIFT;
	iowrite32(temp, gpmc_reg_pointer + GPMC_SYSCONFIG/4);
	iowrite32(0x00, gpmc_reg_pointer + GPMC_IRQENABLE/4) ;
	iowrite32(0x00, gpmc_reg_pointer + GPMC_TIMEOUT_CONTROL/4);

	iowrite32((0x0 |
	(GPMC_CONFIG1_0_DEVICESIZE_SIXTEENBITS <<
		GPMC_CONFIG1_0_DEVICESIZE_SHIFT ) |
	(GPMC_CONFIG1_0_ATTACHEDDEVICEPAGELENGTH_FOUR <<
		GPMC_CONFIG1_0_ATTACHEDDEVICEPAGELENGTH_SHIFT ) |
	(GPMC_CONFIG1_0_MUXADDDATA_MUX << GPMC_CONFIG1_0_MUXADDDATA_SHIFT )), 
	gpmc_reg_pointer + GPMC_CONFIG1(csNum)/4) ;	//Address/Data multiplexed


	iowrite32((0x0 |
    	(CS_ON) |	// CS_ON_TIME
        (CS_OFF << GPMC_CONFIG2_0_CSRDOFFTIME_SHIFT) |	// CS_DEASSERT_RD
        (CS_OFF << GPMC_CONFIG2_0_CSWROFFTIME_SHIFT)),	//CS_DEASSERT_WR
	gpmc_reg_pointer + GPMC_CONFIG2(csNum)/4)  ;	

	iowrite32((0x0 |
        (ADV_ON << GPMC_CONFIG3_0_ADVONTIME_SHIFT) | //ADV_ASSERT
	(ADV_OFF << GPMC_CONFIG3_0_ADVRDOFFTIME_SHIFT) | //ADV_DEASSERT_RD
	(ADV_OFF << GPMC_CONFIG3_0_ADVWROFFTIME_SHIFT)), //ADV_DEASSERT_WR
	gpmc_reg_pointer + GPMC_CONFIG3(csNum)/4) ; 

	iowrite32( (0x0 |
	    (OE_ON << GPMC_CONFIG4_0_OEONTIME_SHIFT) |	//OE_ASSERT
	    (OE_OFF << GPMC_CONFIG4_0_OEOFFTIME_SHIFT) |	//OE_DEASSERT
	    (WR_ON << GPMC_CONFIG4_0_WEONTIME_SHIFT)| //WE_ASSERT
	    (WR_OFF << GPMC_CONFIG4_0_WEOFFTIME_SHIFT)), //WE_DEASSERT
	gpmc_reg_pointer + GPMC_CONFIG4(csNum)/4)  ; 

	iowrite32((0x0 |
	    (RD_CYC << GPMC_CONFIG5_0_RDCYCLETIME_SHIFT)|	//CFG_5_RD_CYCLE_TIM
	    (WR_CYC << GPMC_CONFIG5_0_WRCYCLETIME_SHIFT)|	//CFG_5_WR_CYCLE_TIM
	    (RD_ACC_TIME << GPMC_CONFIG5_0_RDACCESSTIME_SHIFT)),	// CFG_5_RD_ACCESS_TIM
	gpmc_reg_pointer + GPMC_CONFIG5(csNum)/4)  ;  

	iowrite32( (0x0 |
		(0 << GPMC_CONFIG6_0_CYCLE2CYCLESAMECSEN_SHIFT) |
		(0 << GPMC_CONFIG6_0_CYCLE2CYCLEDELAY_SHIFT) | //CYC2CYC_DELAY
	    (WRDATAONADMUX << GPMC_CONFIG6_0_WRDATAONADMUXBUS_SHIFT)| //WR_DATA_ON_ADMUX
	    (0 << GPMC_CONFIG6_0_WRACCESSTIME_SHIFT)), //CFG_6_WR_ACCESS_TIM
	gpmc_reg_pointer + GPMC_CONFIG6(csNum)/4) ;  

	iowrite32(( 0x09 << GPMC_CONFIG7_0_BASEADDRESS_SHIFT) | //CFG_7_BASE_ADDR
        (0x1 << GPMC_CONFIG7_0_CSVALID_SHIFT) |
        (0x0f << GPMC_CONFIG7_0_MASKADDRESS_SHIFT), //CFG_7_MASK
	gpmc_reg_pointer + GPMC_CONFIG7(csNum)/4);  
	iounmap(gpmc_reg_pointer);
	release_mem_region(SOC_GPMC_0_REGS, 720);
	return 1;
}


int LOGIBONE_fifo_open(struct inode *inode, struct file *filp)
{
    read_mode = fifo ;
    request_mem_region(FPGA_BASE_ADDR, 256 * sizeof(short), gDrvrName); //TODO: may block EDMA transfer ...
    gpmc_cs1_virt = ioremap_nocache(FPGA_BASE_ADDR, 256* sizeof(int)); //TODO: may block EDMA transfer ...
    gpmc_cs1_pointer = (unsigned short *) FPGA_BASE_ADDR ;
    fifo_size = gpmc_cs1_virt[FIFO_SIZE_OFFSET] ;
    printk("%s: Open: module opened\n",gDrvrName);
    printk("fifo size : %d\n",fifo_size);
    return 0;
}

int LOGIBONE_fifo_release(struct inode *inode, struct file *filp)
{
    printk("%s: Release: module released\n",gDrvrName);
    iounmap(gpmc_cs1_virt);
    release_mem_region(FPGA_BASE_ADDR, 256 * sizeof(short));
    return 0;
}


unsigned short int getNbAvailable(void){
	//printk("getting nb available \n");
	return gpmc_cs1_virt[NB_AVAILABLE_B_OFFSET] ;  
}

unsigned short int getNbFree(void){
	//printk("getting nb free \n");
	fifo_size = gpmc_cs1_virt[FIFO_SIZE_OFFSET] ;
	return fifo_size - gpmc_cs1_virt[NB_AVAILABLE_A_OFFSET] ;
}

ssize_t LOGIBONE_fifo_write(struct file *filp, const char *buf, size_t count,
                       loff_t *f_pos)
{
	unsigned short int * writeBuffer ;
	unsigned short int nbFree ;
	unsigned long src_addr, trgt_addr ;
	unsigned int ret = 0;
	//printk("writing to fifo \n");
	writeBuffer =  (unsigned short int *) dma_alloc_coherent (NULL, count,
					&dmaphysbuf, 0);
	trgt_addr = (unsigned long) gpmc_cs1_pointer ;
	src_addr = (unsigned long) writeBuffer ;
	// Now it is safe to copy the data from user space.
	if (writeBuffer == NULL || copy_from_user(writeBuffer, buf, count) )  {
		ret = -1;
		printk("%s: LOGIBONE_fifo write: Failed copy to user.\n",gDrvrName);
		goto exit;
	}
	if(read_mode == fifo){
		nbFree = getNbFree();

		if(nbFree == 0){
			ret = -1 ; 
			printk("No room to write in fifo \n");
			goto exit;   
		}
		//printk("%d data slots are free to write \n", nbFree);
		if(edma_memtomemcpy(nbFree, src_addr, trgt_addr, 0, INCR) < 0){
			printk("%s: LOGIBONE_fifo write: Failed to trigger EDMA transfer.\n",gDrvrName);
		goto exit;		
		}
		ret = nbFree;
	}else{
		if(edma_memtomemcpy(count, src_addr, trgt_addr, 0, INCR) < 0){
			printk("%s: LOGIBONE_fifo write: Failed to trigger EDMA transfer.\n",gDrvrName);
			goto exit;		
		}
		ret = count ;
	}
	exit:
	dma_free_coherent(NULL, count, writeBuffer,
				dmaphysbuf); // free coherent before copy to user
	return (ret);
}


#define BURST_SIZE 4
#define MIN_TRANSFER 1024
ssize_t LOGIBONE_fifo_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	unsigned short int * readBuffer ;
	unsigned long src_addr, trgt_addr ;
	int nbAvailable ;
	int nbToRead, index = 0; 
	int ret = 0 ;
	nbToRead = count/sizeof(unsigned short) ;
	readBuffer = (unsigned short int *) dma_alloc_coherent (NULL, count,
					&dmaphysbuf, 0);
	src_addr = (unsigned long) gpmc_cs1_pointer ;
	trgt_addr = (unsigned long) readBuffer ;
	if(read_mode == fifo){		
		while(index < (count/sizeof(unsigned short))){
			nbAvailable = getNbAvailable() ;
			while(nbAvailable < MIN_TRANSFER && nbAvailable > fifo_size){			
				if(nbAvailable >= fifo_size){
					printk("error while reading nb available : %d \n", nbAvailable);			
				}				
				nbAvailable = getNbAvailable() ;
			}
			if(nbAvailable > (count/sizeof(unsigned short)) - index){
				nbAvailable =  ((count/sizeof(unsigned short)) - index) ;			
			} 

			if(edma_memtomemcpy(nbAvailable*sizeof(unsigned short), src_addr, trgt_addr, 0, INCR) < 0){
				printk("%s: LOGIBONE_fifo read: Failed to trigger EDMA transfer.\n",gDrvrName);
				goto exit ;
			}
			index += nbAvailable ;
		}		
		dma_free_coherent(NULL,  count, readBuffer,
				dmaphysbuf);
		// Now it is safe to copy the data to user space.
		if (index == 0 || copy_to_user(buf, readBuffer, (index * sizeof(unsigned short))) )  {
			ret = -1;
			goto exit;
		}

		
		ret = index * sizeof(unsigned short) ;
	}else{
		if(edma_memtomemcpy(count,src_addr, trgt_addr, 0, INCR) < 0){
			printk("%s: LOGIBONE_fifo read: Failed to trigger EDMA transfer.\n",gDrvrName);
			goto exit ;
		}
		dma_free_coherent(NULL, count, readBuffer,
				dmaphysbuf); // free coherent before copy to user
		if (copy_to_user(buf, readBuffer, count) )  {
			ret = -1;
			goto exit;
		}
		ret = count ;	
	}
	exit:
	return ret;
}


long LOGIBONE_fifo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
		
	switch(cmd){
		case LOGIBONE_FIFO_RESET :
			printk("fifo ioctl : reset \n");
			gpmc_cs1_virt[NB_AVAILABLE_A_OFFSET] = 0 ;
			gpmc_cs1_virt[NB_AVAILABLE_B_OFFSET] = 0 ;
			return 0 ;
		case LOGIBONE_FIFO_PEEK :
			printk("fifo ioctl : peek \n");
			return  gpmc_cs1_virt[PEEK_OFFSET] ;
		case LOGIBONE_FIFO_NB_FREE :
			printk("fifo ioctl : free \n");
			return getNbFree() ;
		case LOGIBONE_FIFO_NB_AVAILABLE :
			printk("fifo ioctl : available \n");
			return getNbAvailable() ;
		case LOGIBONE_FIFO_MODE :
			printk("switching to fifo mode \n");
			read_mode = fifo ;
			return 0 ;	
		case LOGIBONE_DIRECT_MODE :
			printk("switching to direct mode \n");
			read_mode = direct ;
			return 0 ;
		default: /* redundant, as cmd was checked against MAXNR */
			printk("unknown command %d \n", cmd);
			return -ENOTTY;
	}
}

struct file_operations LOGIBONE_fifo_ops = {
    .read =   LOGIBONE_fifo_read,
    .write =  LOGIBONE_fifo_write,
    .compat_ioctl =  LOGIBONE_fifo_ioctl,
    .unlocked_ioctl = LOGIBONE_fifo_ioctl,
    .open =   LOGIBONE_fifo_open,
    .release =  LOGIBONE_fifo_release,
};


static int LOGIBONE_fifo_init(void)
{
	dev_t dev = 0;
	struct cdev cdev ;
	int result ;
	setupGPMCClock();
	if(setupGPMCNonMuxed() < 0 ){
		printk(KERN_WARNING "%s: can't initialize gpmc \n",gDrvrName);
		return -1;		
	}

	if (check_mem_region(FPGA_BASE_ADDR, 256 * sizeof(short)) ){
	    printk("%s: memory already in use\n", gDrvrName);
	    return -EBUSY;
	}
	

	if (gDrvrMajor) {
		dev = MKDEV(gDrvrMajor, gDrvrMinor);
		result = register_chrdev(gDrvrMajor, gDrvrName, &LOGIBONE_fifo_ops);
	} else {
		result = alloc_chrdev_region(&dev, gDrvrMinor, nbDevices, gDrvrName);
		gDrvrMajor = MAJOR(dev);
     		cdev_init(&cdev, &LOGIBONE_fifo_ops);
         	result = cdev_add (&cdev, MKDEV(gDrvrMajor, 0), 1);
         /* Fail gracefully if need be */
		 if (result)
		         printk(KERN_NOTICE "Error %d adding LOGIBONE_fifo%d", result, 0);
	}
	if (result < 0) {
		printk(KERN_WARNING "%s: can't get major %d\n",gDrvrName,gDrvrMajor);
		return -1;
	}
	printk(KERN_INFO"%s: Init: module registered with major number %d \n", gDrvrName, gDrvrMajor);

	printk("%s driver is loaded\n", gDrvrName);

	return 0;
}

static void LOGIBONE_fifo_exit(void)
{


    unregister_chrdev(gDrvrMajor, gDrvrName);
    printk(/*KERN_ALERT*/ "%s driver is unloaded\n", gDrvrName);
}




/* DMA Channel, Mem-2-Mem Copy, ASYNC Mode, FIFO Mode */

int edma_memtomemcpy(int count, unsigned long src_addr, unsigned long trgt_addr,  int event_queue,  enum address_mode mode)
{
	int result = 0;
	unsigned int dma_ch = 0;
	struct edmacc_param param_set;

	result = edma_alloc_channel (EDMA_CHANNEL_ANY, dma_callback, NULL, event_queue);
	//result = edma_alloc_channel (AM33XX_DMA_GPM, dma_callback, NULL, event_queue);
	if (result < 0) {
		printk ("\nedma3_memtomemcpytest_dma::edma_alloc_channel failed for dma_ch, error:%d\n", result);
		return result;
	}

	dma_ch = result;
	edma_set_src (dma_ch, src_addr, mode, W64BIT);
	edma_set_dest (dma_ch, trgt_addr, mode, W64BIT);
	edma_set_src_index (dma_ch, 0, 0); // always copy from same location
	edma_set_dest_index (dma_ch, count, count); // increase by transfer size on each copy
	/* A Sync Transfer Mode */
	edma_set_transfer_params (dma_ch, count, 1, 1, 1, ASYNC); //one block of one fame of one array of count bytes

	/* Enable the Interrupts on Channel 1 */
	edma_read_slot (dma_ch, &param_set);
	param_set.opt |= ITCINTEN;
	param_set.opt |= TCINTEN;
	param_set.opt |= EDMA_TCC(EDMA_CHAN_SLOT(dma_ch));
	edma_write_slot (dma_ch, &param_set);

	result = edma_start(dma_ch);
	if (result != 0) {
		printk ("edma copy for logibone_fifo failed \n");
		
	}
	while (irqraised1 == 0u) schedule();
	//irqraised1 = -1 ;

	/* Check the status of the completed transfer */
	if (irqraised1 < 0) {
		/* Some error occured, break from the FOR loop. */
		printk ("edma copy for logibone_fifo: Event Miss Occured!!!\n");
	}else{
		printk ("edma copy for logibone_fifo: Copy done !\n");
	}
	edma_stop(dma_ch);
	edma_free_channel(dma_ch);

	return result;
}

static void dma_callback(unsigned lch, u16 ch_status, void *data)
{	
	switch(ch_status) {
	case DMA_COMPLETE:
		irqraised1 = 1;
		printk ("\n From Callback 1: Channel %d status is: %u\n",
				lch, ch_status);
		break;
	case DMA_CC_ERROR:
		irqraised1 = -1;
		printk ("\nFrom Callback 1: DMA_CC_ERROR occured "
				"on Channel %d\n", lch);
		break;
	default:
		break;
	}
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jonathan Piat <piat.jonathan@gmail.com>");

module_init(LOGIBONE_fifo_init);
module_exit(LOGIBONE_fifo_exit);
