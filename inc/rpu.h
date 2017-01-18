/* RPU Porting information: Based on RPU Config.
 * These are the only values which need to be modified as per
 * a) host memory map
 * b) HOSt <->MCU interrupt configuration.
 * c) RPU Config specific details
 */

/*********************************************************************
 * RPU Config Specific Details
 *********************************************************************
 */
#define HAL_UCCP_GRAM_BASE 0xB7000000
#define HAL_UCCP_GRAM_LEN 0x1eac0
#define HAL_SHARED_MEM_OFFSET 0x45ffc
/* fwldr.c converts these to HOST addresses
 * so pass RPU addresses here.
 * From: uccrunTime/Platform/configs
 */
#define UCCP_COREA_REGION_START 0x80880000
#define UCCP_COREA_REGION_LEN 0x4C000

#define UCCP_COREB_REGION_START 0x82000000
#define UCCP_COREB_REGION_LEN 0x4C000
/*********************************************************************
 */

/********************************************************************
 * HOST processor to Meta Processor on RPU Communications Registers
 ********************************************************************
 * Refer: Volt UCCP.Technical Reference Manual.pdf
 * The host_to_mtx_cmd register is written to by the host in order to
 * send data to the META. The act of writing causes an event on the META
 * (the host_int interrupt occurs). The META handles this event by reading
 * host_to_mtx_cmd, collecting the message data.
 *
 * The META clears the interrupt and in the process acknowledges reception
 * of the message by writing to the mtx_to_host_ack register. The host checks
 * for this acknowledgement by reading host_to_mtx_cmd, checking the state of
 * the HOST_INT bit.
 *
 * A message initiated by the META and destined for the host uses the same
 * scheme, but utilising the mtx_to_host_cmd and host_to_mtx_ack registers and
 * by responding to the mtx_int interrupt.
 */

/* SYSBUS - System Control - REGSYSREG
 * UCCP_CORE_REG is a subset of System Bus Registers
 */
#define HAL_UCCP_CORE_REG_OFFSET	0x400

/* Register HOST_TO_MTX_CMD */
#define HOST_TO_MTX_CMD 0x0030
#define HOST_TO_MTX_CMD_ADDR ((hpriv->uccp_mem_addr) + \
				    HOST_TO_MTX_CMD)
#define MTX_HOST_INT_SHIFT 31

/* Register MTX_TO_HOST_CMD */
#define MTX_TO_HOST_CMD 0x0034
#define MTX_TO_HOST_CMD_ADDR ((hpriv->uccp_mem_addr) + \
				    MTX_TO_HOST_CMD)

/* Register HOST_TO_MTX_ACK */
#define HOST_TO_MTX_ACK 0x0038
#define HOST_TO_MTX_ACK_ADDR ((hpriv->uccp_mem_addr) + \
				    HOST_TO_MTX_ACK)
#define MTX_INT_CLR_SHIFT 31

/* Register MTX_TO_HOST_ACK */
#define MTX_TO_HOST_ACK 0x003C
#define MTX_TO_HOST_ACK_ADDR ((hpriv->uccp_mem_addr) + \
				    MTX_TO_HOST_ACK)

/* Register MTX_INT_ENABLE
 * Enable INT line within META Block
 */
#define MTX_INT_ENABLE 0x0044
#define MTX_INT_ENABLE_ADDR ((hpriv->uccp_mem_addr) + \
				   MTX_INT_ENABLE)
#define MTX_INT_EN_SHIFT 31

/* System Level Interrupt Control for each block.
 * Enable INT line for META block.
 */
#define SYS_INT_ENAB 0x0000
#define SYS_INT_ENAB_ADDR ((hpriv->uccp_mem_addr) + SYS_INT_ENAB)
#define SYS_INT_MTX_IRQ_ENAB_SHIFT 15

/*********************************************************************
 */
