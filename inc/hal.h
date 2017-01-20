/*
 * File Name  : hal.h
 *
 * This file contains Intermodule communication APIs
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

#ifndef _UCCP420WLAN_HAL_H_
#define _UCCP420WLAN_HAL_H_

#define _PACKED_  __attribute__((__packed__))

#define HOST_MOD_ID 0
#define UMAC_MOD_ID 1
#define LMAC_MOD_ID 2
#define MODULE_MAX 3

#define MAX_RX_BUF_PTR_PER_CMD (16)
#define MAX_DATA_SIZE_12K (12 * 1024)
#define MAX_DATA_SIZE_8K (8 * 1024)
#define MAX_DATA_SIZE_2K (2 * 1024)

#define NUM_TX_DESC 12
#define NUM_FRAMES_IN_TX_DESC 32
#define NUM_BYTES_PER_FRAME 9
#define TX_DESC_HAL_SIZE (NUM_FRAMES_IN_TX_DESC * NUM_BYTES_PER_FRAME)

#define HAL_PRIV_DATA_SIZE 8

#define HAL_HOST_ZONE_DMA_LEN (64 * 1024 * 1024)
#define HAL_SHARED_MEM_MAX_MSG_SIZE 60
#define HAL_SHARED_MEM_MAX_TX_SIZE 0xD80

/* Command, Event, Tx Data and Buff mappping offsets */
#define HAL_COMMAND_OFFSET (0)
#define HAL_EVENT_OFFSET (HAL_COMMAND_OFFSET + HAL_SHARED_MEM_MAX_MSG_SIZE)
#define HAL_TX_DATA_OFFSET (HAL_EVENT_OFFSET   + HAL_SHARED_MEM_MAX_MSG_SIZE)
#define HAL_AXD_DATA_OFFSET (HAL_TX_DATA_OFFSET + HAL_SHARED_MEM_MAX_TX_SIZE)

#define HAL_GRAM_CMD_START ((hpriv->gram_mem_addr) + HAL_COMMAND_OFFSET)
#define HAL_GRAM_EVENT_START ((hpriv->gram_mem_addr) + HAL_EVENT_OFFSET)
#define HAL_GRAM_TX_DATA_START ((hpriv->gram_mem_addr) + HAL_TX_DATA_OFFSET)
#define HAL_AXD_DATA_START ((hpriv->gram_mem_addr) + HAL_AXD_DATA_OFFSET)

#define HAL_GRAM_CMD_LEN (HAL_GRAM_CMD_START + 8)
#define HAL_GRAM_TX_DATA_LEN (HAL_GRAM_TX_DATA_START + 0)
#define HAL_GRAM_TX_DATA_OFFSET	(HAL_GRAM_TX_DATA_START + 3)
#define HAL_GRAM_TX_DATA_ADDR (HAL_GRAM_TX_DATA_START + 6)

#define HAL_HOST_BOUNCE_BUF_LEN (4 * 1024 * 1024)
#define HAL_HOST_NON_BOUNCE_BUF_LEN (60 * 1024 * 1024)

/*RPU DUMP Regions and Commands*/
#define UCCP_REGION_TYPE_COREA 0
#define UCCP_REGION_TYPE_COREB 1

enum hal_rpu_testmode_cmd {
	HAL_RPU_TM_CMD_ALL	= 0,
	HAL_RPU_TM_CMD_GRAM	= 1,
	HAL_RPU_TM_CMD_COREA	= 2,
	HAL_RPU_TM_CMD_COREB	= 3,
	HAL_RPU_TM_CMD_PERIP	= 4,
	HAL_RPU_TM_CMD_SYSBUS	= 5,
};

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
int reset_hal_params(void);
typedef int (*msg_handler)(void *, unsigned char);

struct hal_ops_tag {
	int (*init)(void *);
	int (*deinit)(void *);
	int (*start)(void);
	int (*stop)(void);
	void (*register_callback)(msg_handler, unsigned char);
	void (*send)(void*, unsigned char, unsigned char, void*);
	int (*init_bufs)(unsigned int, unsigned int, unsigned int,
			 unsigned int);
	void (*deinit_bufs)(void);
	int (*map_tx_buf)(int, int, unsigned char *, int);
	int (*unmap_tx_buf)(int, int);
	int (*reset_hal_params)(void);
	void (*set_mem_region)(unsigned int);
	void (*request_mem_regions)(unsigned char **,
				    unsigned char **,
				    unsigned char **);
	void (*enable_irq_wake)(void);
	void (*disable_irq_wake)(void);
	int (*get_dump_gram)(long *dump_start);
	int (*get_dump_core)(unsigned long *dump_start,
			     unsigned char region_type);
	int (*get_dump_perip)(unsigned long *dump_start);
	int (*get_dump_sysbus)(unsigned long *dump_start);
	int (*get_dump_len)(unsigned long);
	int (*update_axd_timestamps)(void);
	unsigned int (*get_axd_buf_phy_addr)(void);
};

extern struct hal_ops_tag hal_ops;
#endif /* _UCCP420WLAN_HAL_H_ */

/* EOF */
