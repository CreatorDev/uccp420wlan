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

#include <linux/interrupt.h>
#include <linux/skbuff.h>

#include <hal.h>

#if defined(__cplusplus)
extern "C"
{
#endif /* __cplusplus */

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
	unsigned long uccp_sysbus_len;
	unsigned long uccp_perip_base;
	unsigned long uccp_perip_len;
	unsigned long uccp_pkd_gram_base;
	unsigned long uccp_pkd_gram_len;
	unsigned long uccp_gram_base;
	unsigned long uccp_gram_len;

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
};


#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* _UCCP420WLAN_HAL_HOSTPORT_H_ */

/* EOF */
