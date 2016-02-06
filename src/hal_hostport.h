/*
 * File Name  : hal_hostport.h
 *
 * This file contains the definitions specific to HOSPORT comms
 *
 * Copyright (c) 2011, 2012, 2013, 2014 Imagination Technologies Ltd.
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#ifndef _UCCP420WLAN_HAL_HOSTPORT_H_
#define _UCCP420WLAN_HAL_HOSTPORT_H_

#include <linux/skbuff.h>
#include <linux/interrupt.h>

#include <hal.h>

#define _PACKED_  __attribute__((__packed__))

#define MAX_RX_BUF_PTR_PER_CMD (16)
#define MAX_DATA_SIZE_12K (12 * 1024)
#define MAX_DATA_SIZE_8K (8 * 1024)
#define MAX_DATA_SIZE_2K (2 * 1024)

#define NUM_TX_DESC 12
#define NUM_FRAMES_IN_TX_DESC 32
#define NUM_BYTES_PER_FRAME 9
#define TX_DESC_HAL_SIZE (NUM_FRAMES_IN_TX_DESC * NUM_BYTES_PER_FRAME)

#if defined(__cplusplus)
extern "C"
{
#endif /* __cplusplus */

struct buf_info {
	dma_addr_t dma_buf;
	void __iomem *src_ptr;
	unsigned int dma_buf_len;
	unsigned int dma_buf_priv;   /* Is the DMA buffer in our private area */
	struct sk_buff *skb;
} _PACKED_;

struct hal_tx_data {
	unsigned int data_len:24;
	unsigned long address:24;
	unsigned long offset:24;
} _PACKED_;

struct hal_priv {
	/* UCCP Host RAM mappings*/
	void __iomem *base_addr_uccp_host_ram;
	void __iomem *tx_base_addr_uccp_host_ram;
	void __iomem *rx_base_addr_uccp_host_ram;

	/* UCCP and GRAM mappings */
	unsigned long uccp_mem_addr;
	unsigned long gram_mem_addr;
	unsigned long uccp_sysbus_base_addr;
	unsigned long uccp_perip_base_addr;
	unsigned long gram_base_addr;
	unsigned long shm_offset;
	unsigned long hal_disabled;
	unsigned long gram_b4_addr;

	/* DTS entries */
	unsigned long uccp_sysbus_base;
	unsigned long uccp_sysbus_len;		/* HAL_HOST_UCCP_LEN */
	unsigned long uccp_perip_base;		/* HAL_PERIP_BASE */
	unsigned long uccp_perip_len;		/* HOST_PERIP_BASE_LEN */
	unsigned long uccp_pkd_gram_base;
	unsigned long uccp_pkd_gram_len;
	unsigned long uccp_gram_base;		/* b4addr */
	unsigned long uccp_gram_len;		/* b4addr length */

	/* TX */
	struct sk_buff_head txq;
	struct tasklet_struct tx_tasklet;
	unsigned short cmd_cnt;
	struct buf_info *tx_buf_info;
	struct hal_tx_data *hal_tx_data;

	/* RX */
	struct sk_buff_head rxq;
	struct tasklet_struct rx_tasklet;
	struct tasklet_struct recv_tasklet;
	unsigned short event_cnt;
	msg_handler rcv_handler;
	struct buf_info *rx_buf_info;

	/* Buffers info from IF layer*/
	unsigned int tx_bufs;
	unsigned int rx_bufs_2k;
	unsigned int rx_bufs_12k;
	unsigned int max_data_size;

	/* Temp storage to refill first and process next*/
	struct sk_buff_head refillq;
	int irq;
	/* physical address mapping for address */
	unsigned long           wifi_t0_addr;
	unsigned long           wifi_t1_addr;
};

struct hal_hdr {
	/*! 0xffffffff - hal command or hal event
	 *  0x0 - lmac command or lmac event
	 */
	unsigned int id;
	/*! Data pointer of commands with payload
	 *  this field is valid only if descriptor id
	 *  of command header is set to some value
	 *  other.
	 */
	unsigned int data_ptr;
} _PACKED_;

struct hal_rx_pkt_info {
	/* Rx descriptor */
	unsigned int desc;
	unsigned int ptr;
} _PACKED_;

struct hal_rx_command {
	unsigned int rx_pkt_cnt;
	struct hal_rx_pkt_info rx_pkt[MAX_RX_BUF_PTR_PER_CMD];
} _PACKED_;

struct cmd_hal {
	struct hal_hdr hdr;
	struct hal_rx_command rx_pkt_data;
} _PACKED_;

struct event_hal {
	struct hal_hdr hdr;
	unsigned int rx_pkt_cnt;
	unsigned int rx_pkt_desc[16];
} _PACKED_;


int _uccp420wlan_80211if_init(struct proc_dir_entry **);
void _uccp420wlan_80211if_exit(void);

/*Porting information:
 * HAL_UCCP_IRQ_LINE: This is the interrupt number assigned to UCCP host port
 *                    interrupt.
 * HAL_HOST_UCCP_RAM_START: This is the physical address of the start of
 *                          Host RAM which is reserved for UCCP
 * HAL_HOST_ZONE_DMA_START: This is the physical address of the start of 64MB
 *                          ZONE_DMA area which is currently assigned a dummy
 *                          value of 0xABABABAB. TSB needs to provide the actual
 *                          value for this.
 *
 * These are the only values which need to be modified as per host memory
 * map and interrupt configuration.
 * The values for HAL_SHARED_MEM_OFFSET, HAL_WLAN_GRAM_LEN,  HAL_COMMAND_OFFSET,
 * and  HAL_EVENT_OFFSET can be changed by IMG in future software releases.
 */

#define HAL_HOST_UCCP_LEN 0x0003E800
#define HAL_UCCP_GRAM_BASE 0xB7000000

#define HAL_UCCP_CORE_REG_OFFSET		0x400

/* Register HOST_TO_UCCP_CORE_CMD */
#define HOST_TO_UCCP_CORE_CMD 0x0030
#define HOST_TO_UCCP_CORE_CMD_ADDR ((hpriv->uccp_mem_addr) + \
				    HOST_TO_UCCP_CORE_CMD)
#define UCCP_CORE_HOST_INT_SHIFT 31

/* Register UCCP_CORE_TO_HOST_CMD */
#define UCCP_CORE_TO_HOST_CMD 0x0034
#define UCCP_CORE_TO_HOST_CMD_ADDR ((hpriv->uccp_mem_addr) + \
				    UCCP_CORE_TO_HOST_CMD)

/* Register HOST_TO_UCCP_CORE_ACK */
#define HOST_TO_UCCP_CORE_ACK 0x0038
#define HOST_TO_UCCP_CORE_ACK_ADDR ((hpriv->uccp_mem_addr) + \
				    HOST_TO_UCCP_CORE_ACK)
#define UCCP_CORE_INT_CLR_SHIFT 31

/* Register UCCP_CORE_TO_HOST_ACK */
#define UCCP_CORE_TO_HOST_ACK 0x003C
#define UCCP_CORE_TO_HOST_ACK_ADDR ((hpriv->uccp_mem_addr) + \
				    UCCP_CORE_TO_HOST_ACK)

/* Register UCCP_CORE_INT_ENABLE */
#define UCCP_CORE_INT_ENABLE 0x0044
#define UCCP_CORE_INT_ENABLE_ADDR ((hpriv->uccp_mem_addr) + \
				   UCCP_CORE_INT_ENABLE)
#define UCCP_CORE_INT_EN_SHIFT 31

#define UCCP_CORE_INT_ENAB 0x0000
#define UCCP_CORE_INT_ENAB_ADDR ((hpriv->uccp_mem_addr) + UCCP_CORE_INT_ENAB)
#define UCCP_CORE_INT_IRQ_ENAB_SHIFT 15

/******************************************************************************/
#define HAL_SHARED_MEM_OFFSET 0x45ffc
#define HAL_SHARED_MEM_MAX_MSG_SIZE 60
#define HAL_WLAN_GRAM_LEN 0x1eac0

/* Command, Event, Tx Data and Buff mappping offsets */
#define HAL_COMMAND_OFFSET (0)
#define HAL_EVENT_OFFSET (HAL_COMMAND_OFFSET + HAL_SHARED_MEM_MAX_MSG_SIZE)
#define HAL_TX_DATA_OFFSET (HAL_EVENT_OFFSET   + HAL_SHARED_MEM_MAX_MSG_SIZE)

#define HAL_GRAM_CMD_START ((hpriv->gram_mem_addr) + HAL_COMMAND_OFFSET)
#define HAL_GRAM_EVENT_START ((hpriv->gram_mem_addr) + HAL_EVENT_OFFSET)
#define HAL_GRAM_TX_DATA_START ((hpriv->gram_mem_addr) + HAL_TX_DATA_OFFSET)

#define HAL_GRAM_CMD_LEN (HAL_GRAM_CMD_START + 8)
#define HAL_GRAM_TX_DATA_LEN (HAL_GRAM_TX_DATA_START + 0)
#define HAL_GRAM_TX_DATA_OFFSET	(HAL_GRAM_TX_DATA_START + 3)
#define HAL_GRAM_TX_DATA_ADDR (HAL_GRAM_TX_DATA_START + 6)

#define HAL_HOST_BOUNCE_BUF_LEN (4 * 1024 * 1024)
#define HAL_HOST_NON_BOUNCE_BUF_LEN (60 * 1024 * 1024)

/* Shift the amount of movement of each the offset */

/* 32bit - valid offset 8bit  - start pos 0bit*/
#define WLN_CGN_HOST_SYS_ADDR_SHIFT    (24)

#define HAL_HOST_ZONE_DMA_START 0xABABABAB
#define HAL_HOST_ZONE_DMA_LEN (64 * 1024 * 1024)

/*RPU DUMP Regions and Commands*/
#define UCCP_REGION_TYPE_COREA 0
#define UCCP_REGION_TYPE_COREB 1

/* fwldr.c converts these to HOST addresses
 * so pass RPU addresses here.
 * From: uccrunTime/Platform/configs
 */
#define UCCP_COREA_REGION_START 0x80880000
#define UCCP_COREA_REGION_LEN 0x4C000

#define UCCP_COREB_REGION_START 0x82000000
#define UCCP_COREB_REGION_LEN 0x4C000

/* Interrupt number assigned to UCCP host port interrupt */
#define HAL_IRQ_LINE 74

enum hal_rpu_testmode_cmd {
	HAL_RPU_TM_CMD_ALL	= 0,
	HAL_RPU_TM_CMD_GRAM	= 1,
	HAL_RPU_TM_CMD_COREA	= 2,
	HAL_RPU_TM_CMD_COREB	= 3,
	HAL_RPU_TM_CMD_PERIP	= 4,
	HAL_RPU_TM_CMD_SYSBUS	= 5,
};

int reset_hal_params(void);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* _UCCP420WLAN_HAL_HOSTPORT_H_ */

/* EOF */
