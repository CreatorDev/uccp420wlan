/*
 * File Name  : fwldr.h
 *
 * File Description: This file contains definitions used for firmware loader
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

#ifndef _FWLDR_H_
#define _FWLDR_H_

#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include "hal.h"
#include "version.h"

#ifdef DRIVER_DEBUG

#define fwldr_dbg_err(...) pr_err(__VA_ARGS__)
#define fwldr_dbg_info(...) pr_info(__VA_ARGS__)
#define fwldr_dbg_dump(...) pr_info(__VA_ARGS__)

#else
#define fwldr_dbg_err(...) do { } while (0)
#define fwldr_dbg_info(...) do { } while (0)
#define fwldr_dbg_dump(...) do { } while (0)

#endif

#define FWLDR_NUM_BINS 2
#define FWLDR_HW_BIN "img/uccp420wlan/MCP_LOADER.ldr"
#define FWLDR_FW_BIN "img/uccp420wlan/MAC_LOADER.ldr"

#define FWLDR_PLRCRD_WORDS 16  /* 64-bit WORDS */
#define FWLDR_PLRCRD_BYTES (FWLDR_PLRCRD_WORDS * 8)
#define FWLDR_PLRCRD_TRAIL_BYTES 8
#define FWLDR_PLRCRD_DATA_BYTES (FWLDR_PLRCRD_BYTES - FWLDR_PLRCRD_TRAIL_BYTES)


/******************************************************************************
* These constants are used to access various fields within an L1 record as well
* as other constants that are used.
******************************************************************************/

/* The maximum number of bytes in an L1 record. */
#define FWLDR_L1_MAXSIZE 32

/* The maximum number of bytes in an L2 record. */
#define FWLDR_L2_MAXSIZE 4096

/* The size in bytes of the 'cmd' field in an L1 Record. */
#define FWLDR_L1_CMD_SIZE 2

/* The size in bytes of the 'length' field in an L1 Record. */
#define FWLDR_L1_LEN_SIZE 2

/* The size in bytes of the 'next' field in an L1 Record. */
#define FWLDR_L1_NXT_SIZE 4

/* The size in bytes of the 'arg1' field in an L1 Record. */
#define FWLDR_L1_ARG1_SIZE 4

/* The size in bytes of the 'arg2' field in an L1 Record. */
#define FWLDR_L1_ARG2_SIZE 4

/* The size in bytes of the 'l2offset' field in an L1 Record. */
#define FWLDR_L1_L2OFF_SIZE 4

/* The size in bytes of the 'xsum' field in an L1 Record. */
#define FWLDR_L1_XSUM_SIZE 2

/* The offset in bytes of the 'cmd' field in an L1 record. */
#define FWLDR_L1_CMD_OFF 0

/* The offset in bytes of the 'length' field in an L1 record. */
#define FWLDR_L1_LEN_OFF (FWLDR_L1_CMD_OFF + FWLDR_L1_CMD_SIZE)

/* The offset in bytes of the 'next' field in an L1 record. */
#define FWLDR_L1_NXT_OFF (FWLDR_L1_LEN_OFF + FWLDR_L1_LEN_SIZE)

/* The offset in bytes of the 'arg1' field in an L1 record. */
#define FWLDR_L1_ARG1_OFF (FWLDR_L1_NXT_OFF + FWLDR_L1_NXT_SIZE)

/* The offset in bytes of the 'arg2' field in an L1 record. */
#define FWLDR_L1_ARG2_OFF (FWLDR_L1_ARG1_OFF + FWLDR_L1_ARG1_SIZE)

/* The following is the value used to terminate a chain of L1 records */
#define FWLDR_L1_TERMINATE 0xffffffff


/******************************************************************************
* These constants are used to access various fields within an L2 record as well
* as other constants that are used.
******************************************************************************/

/* The size in bytes of the 'cmd' field in an L2 Record. */
#define FWLDR_L2_CMD_SIZE 2

/* The size in bytes of the 'length' field in an L2 Record. */
#define FWLDR_L2_LEN_SIZE 2

/* The size in bytes of the 'xsum' field in an L2 Record. */
#define FWLDR_L2_XSUM_SIZE 2

/* The offset in bytes from the beginning of an L2 record to the data
 * payload
 */
#define FWLDR_L2_DATA (FWLDR_L2_CMD_SIZE + FWLDR_L2_LEN_SIZE)


/******************************************************************************
* Various combined values...
******************************************************************************/

/* Sizes of common items between L1 and L2 records */
#define FWLDR_L1_L2LEN_SIZE FWLDR_L2_LEN_SIZE

/* The size in bytes of an L1 record when it contains no data */
#define FWLDR_L1_BASIC_SIZE (FWLDR_L1_CMD_SIZE + \
			     FWLDR_L1_LEN_SIZE + \
			     FWLDR_L1_NXT_SIZE + \
			     FWLDR_L1_L2OFF_SIZE + \
			     FWLDR_L1_L2LEN_SIZE + \
			     FWLDR_L1_XSUM_SIZE)

/* The size in bytes of an L2 record when it contains no data */
#define FWLDR_L2_BASIC_SIZE (FWLDR_L2_CMD_SIZE + \
			     FWLDR_L2_LEN_SIZE + \
			     FWLDR_L2_XSUM_SIZE)


/* Offsets in bytes from the end of an L1 record for various fields */
#define FWLDR_L1_L2LEN_OFF (FWLDR_L1_XSUM_SIZE + FWLDR_L1_L2LEN_SIZE)
#define FWLDR_L1_L2OFF_OFF (FWLDR_L1_L2LEN_OFF + FWLDR_L1_L2OFF_SIZE)

#define UCCP_GRAM_BASE	    0xB7000000

#define UCCP_SLAVE_PORT_OFFSET 0x3C000
#define UCCP_OFFSET_MASK    0x00FFFFFF
#define UCCP_BASE_MASK      0xFF000000
#define UCCP_SYSBUS_REG     0x02
#define UCCP_GRAM_PACKED    0xB7
#define UCCP_GRAM_MSB       0xB4


#define MTX_REG_INDIRECT(unit, reg) (((reg & 0x7) << 4) | (unit & 0xF))

#define MTX_PC_REG_IND_ADDR        MTX_REG_INDIRECT(5, 0)
#define MTX_A0STP_REG_IND_ADDR     MTX_REG_INDIRECT(3, 0)

#define MTX_PCX_REG_IND_ADDR MTX_REG_INDIRECT(5, 1)
#define MTX_TXMASK_REG_IND_ADDR MTX_REG_INDIRECT(7, 1)
#define MTX_TXMASKI_REG_IND_ADDR MTX_REG_INDIRECT(7, 3)
#define MTX_TXPOLL_REG_IND_ADDR MTX_REG_INDIRECT(7, 4)
#define MTX_TXPOLLI_REG_IND_ADDR MTX_REG_INDIRECT(7, 6)
#define MTX_TXSTAT_REG_IND_ADDR MTX_REG_INDIRECT(7, 0)
#define MTX_TXSTATI_REG_IND_ADDR MTX_REG_INDIRECT(7, 2)

#define REG_IND_READ_FLAG (1 << 16)

#define MTX_TXPRIVEXT_ADDR 0x048000E8
#define MTX_TXSTATUS_ADDR 0x48000010
#define	MTX_TXENABLE_ADDR 0x04800000
#define	MTX_START_EXECUTION 1
#define	MTX_STOP_EXECUTION 0

#define MTX_TXUXXRXDT 0x0480FFF0
#define MTX_TXUXXRXRQ 0x0480FFF8

#define MSLV_BASE_ADDR 0x0203C000

/* DATA Exchange Register */
#define MSLVDATAX (MSLV_BASE_ADDR + 0x2000)

/* DATA Transfer Register */
#define MSLVDATAT (MSLV_BASE_ADDR + 0x2040)

/* Control Register 0 */
#define MSLVCTRL0 (MSLV_BASE_ADDR + 0x2080)

/* Soft Reset register */
#define MSLVSRST (MSLV_BASE_ADDR + 0x2600)

#define SLAVE_ADDR_MODE_MASK 0xFFFFFFFC
#define SLAVE_SINGLE_WRITE 0x00
#define SLAVE_SINGLE_READ 0x01
#define SLAVE_BLOCK_WRITE 0x02
#define SLAVE_BLOCK_READ 0x03

/* Control Register 1 */
#define MSLVCTRL1 (MSLV_BASE_ADDR + 0x20c0)

#define MSLVCTRL1_POLL_MASK 0x07000000
#define MSLAVE_READY(v) ((v & MSLVCTRL1_POLL_MASK) == MSLVCTRL1_POLL_MASK)
#define LTP_THREAD_NUM 0 /* Since, only one thread exists */

/* Thread completion signature */
#define UCCP_THRD_EXEC_SIG_OFFSET 0x00066CBC
#define UCCP_THRD_EXEC_SIG 0x5A5A5A5A

#define MAX_LOAD_MEM_LEN 4096


enum fwldr_status {
	FWLDR_SUCCESS,
	FWLDR_FAIL
};

/* Cmd or Tag values used in the L1/L2 records */
enum fwldr_cmd_tag_l1_l2 {
	FWLDR_L1_CMD_LOAD_MEM = 0x0000, /* Command - L1 LoadMem. */
	FWLDR_L1_CMD_START_THRDS = 0x0003, /* Command - L1 StartThrds. */
	FWLDR_L1_CMD_ZERO_MEM = 0x0004, /* Command - L1 ZeroMem. */
	FWLDR_L1_CMD_CONFIG = 0x0005, /* Command - L1 Config. */
	FWLDR_L1_CMD_FILENAME = 0x0010, /* Command - L1 FileName. */
};

/* Enumerates all possible types of configuration commands */
enum fwldr_conf_cmd {
	FWLDR_CONF_CMD_PAUSE = 0x0000, /* Pause */
	FWLDR_CONF_CMD_READ, /* Read */
	FWLDR_CONF_CMD_WRITE, /* Write */
	FWLDR_CONF_CMD_MEMSET, /* MemSet */
	FWLDR_CONF_CMD_MEMCHK, /* MemChk */
	FWLDR_CONF_CMD_USER, /* User */
};

/* Information contained within an .ldr file */
enum fwldr_ldr_sec {
	FWLDR_SEC_NONE = 0, /* Element is undefined */
	FWLDR_SEC_BOOT_HEADER, /* Boot header */
	FWLDR_LDR_CODE, /* Secondary loader executable */
	FWLDR_SEC_DATA_L1, /* Secondary loader top level data stream */
	FWLDR_SEC_DATA_L2 /* Secondary loader raw data stream */
};

enum uccp_mem_region {
	UCCP_MEM_CORE,
	UCCP_MEM_DIRECT,
	UCCP_MEM_ERR
};

struct fwldr_bootdevhdr {
	unsigned int  dev_id; /* Value used to verify access to boot device */
	unsigned int  sl_code; /* Offset to secondary loader code */
#define BOOTDEV_SLCSECURE_BIT 0x80000000
#define BOOTDEV_SLCCRITICAL_BIT 0x40000000

	unsigned int  sl_data; /* Offset to data used by secondary loader */
	unsigned short pl_ctrl; /* Primary loader control */
#define BOOTDEV_PLCREMAP_BITS 0x00FF
#define BOOTDEV_PLCREMAP_S 0

	unsigned short CRC; /* CRC value */
};

struct fwldr_load_mem_info {
	unsigned int dst_addr;
	unsigned int len;
	unsigned char *src_buf;
};

struct fwldr_thrd_info {
	unsigned int thrd_num;
	unsigned int stack_ptr;
	unsigned int prog_ctr;
	unsigned int catch_state_addr;
};

struct fwldr_cfg_rw {
	unsigned int addr;
	unsigned int val;
};

/*  Represents a secondary loader top level data stream record. */
struct fwldr_sec_ldr_l1_record {
	unsigned short cmd_tag; /* Command TagMember comment goes here */
	unsigned short len; /* Total length os this record */
	unsigned short crc; /* X25 CRC checksum for this record (including the
			     * checksum itself)
			     */
	unsigned int nxt; /* Offset within the .ldr to the next L1RECORD */
	unsigned int arg1; /* The first command argument for the command */
	unsigned int arg2; /* The second command argument for the command */
	unsigned int l2_offset; /* Offset within the .ldr to the corresponding
				 * raw data record
				 */
	unsigned int l2_len; /* The expected length of the raw data record */
};

struct fwldr_memhdr_tag {
	struct fwldr_memhdr_tag *p_next;
	unsigned int addr; /* Target byte address */
	unsigned char *data; /* Data block pointer */
	unsigned int len; /* Len in bytes of data block */

};

struct fwload_priv {
	unsigned char           *gram_addr;
	unsigned char           *core_addr;
	unsigned char           *gram_b4_addr;
};

int fwldr_load_fw(const unsigned char *fw_data, int i);

static inline void fwload_uccp_read(struct fwload_priv *fpriv,
				    unsigned long base,
				    unsigned long offset,
				    unsigned int *data)
{
	if (base == UCCP_SYSBUS_REG)
		*data = readl((void __iomem *)fpriv->core_addr +
			  (offset - UCCP_SLAVE_PORT_OFFSET));
	else if (base == UCCP_GRAM_PACKED)
		*data = readl((void __iomem *)fpriv->gram_addr + (offset));
	else if (base == UCCP_GRAM_MSB)
		*data = readl((void __iomem *)fpriv->gram_b4_addr + (offset));
}

static inline void fwload_uccp_write(struct fwload_priv *fpriv,
				     unsigned long base,
				     unsigned long offset,
				     unsigned int data)
{
	if (base == UCCP_SYSBUS_REG)
		writel(data, (void __iomem *)(fpriv->core_addr +
					  (offset - UCCP_SLAVE_PORT_OFFSET)));
	else if (base == UCCP_GRAM_PACKED)
		writel(data, (void __iomem *)(fpriv->gram_addr + (offset)));
	else if (base == UCCP_GRAM_MSB)
		writel(data, (void __iomem *)(fpriv->gram_b4_addr + (offset)));
}

#endif /* _FWLDR_H_ */
