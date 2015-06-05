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

#define HOST_MOD_ID 0
#define UMAC_MOD_ID 1
#define LMAC_MOD_ID 2
#define MODULE_MAX 3

#define HAL_PRIV_DATA_SIZE 8

typedef int (*msg_handler)(void *, unsigned char);

struct hal_ops_tag {
	int (*init)(void *);
	int (*deinit)(void *);
	int (*start)(struct proc_dir_entry *);
	int (*stop)(struct proc_dir_entry *);
	void (*register_callback)(msg_handler, unsigned char);
	void (*send)(void*, unsigned char, unsigned char, void*);
	int (*init_bufs)(unsigned int, unsigned int, unsigned int,
			 unsigned int);
	void (*deinit_bufs)(void);
	int (*map_tx_buf)(int, int, unsigned char *, int);
	int (*unmap_tx_buf)(int, int);
	int (*reset_hal_params)(void);
	void (*set_mem_region)(unsigned int);
	void (*request_mem_regions)(unsigned char **, unsigned char **,
				    unsigned char **);
};

extern struct hal_ops_tag hal_ops;
#endif /* _UCCP420WLAN_HAL_H_ */

/* EOF */
