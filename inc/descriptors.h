/*
 * File Name  : descriptor.h
 *
 * File Description: This file contains information about TX and RX descriptors
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

#ifndef _UCCP420WLAN_DESCRIPTOR_H_
#define _UCCP420WLAN_DESCRIPTOR_H_

#define NUM_ACS			5

#define NUM_TX_DESCS_PER_AC	2	/* reserved TX descriptors per AC
					 * (must be multiple of 2, minimum of 2
					 * and maximum of 4)
					 */
#define NUM_SPARE_TX_DESCS	2	/* Descriptors shared between ACs
					 * (at least 1 and maximum of 2)
					 */

#define NUM_TX_DESCS    ((NUM_ACS *  NUM_TX_DESCS_PER_AC) + NUM_SPARE_TX_DESCS)
/* Max size of a sub-frame in an AMPDU */
#define MAX_AMPDU_SUBFRAME_SIZE 1500

/* Max no of sub frames in an AMPDU */
#define MAX_SUBFRAMES_IN_AMPDU_HT 24	/* HT */
#define MAX_SUBFRAMES_IN_AMPDU_VHT 24	/* VHT */

#define NUM_CTRL_DESCS		2

#define NUM_RX_BUFS_2K		256
#define NUM_RX_BUFS_12K		16

#endif /* _UCCP420WLAN_DESCRIPTOR_H_ */
/* EOF */
