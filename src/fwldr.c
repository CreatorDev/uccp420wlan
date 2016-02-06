/*
 * File Name  : fwldr.c
 *
 * This file contains contains functions related to firmware loading
 * functionality.
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

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <fwldr.h>

struct fwload_priv  *fpriv, fpv;

static unsigned short fwldr_read_le2(unsigned char *buf);
static unsigned int fwldr_read_le4(unsigned char *buf);
static unsigned fwldr_virt_to_linear_off(unsigned page_size,
					 unsigned offset);

static void fwldr_soft_reset(unsigned int thrd_num);

static void fwldr_load_mem(unsigned int dst_addr,
		    unsigned int len,
		    unsigned char *src_buf);

static void fwldr_start_thrd(unsigned int thrd_number,
			unsigned int stack_ptr,
			unsigned int program_ctr,
			unsigned int catch_state_addr);

static void fwldr_stop_thrd(unsigned int thrd_num);

static void fwldr_zero_mem(unsigned int dst_addr,
		    unsigned int len);

static unsigned int fwldr_config_read(unsigned int dst_addr);

static void fwldr_config_write(unsigned int dst_addr,
			unsigned int val);

static enum uccp_mem_region fwldr_chk_region(unsigned int src_addr,
				      int length);

static int fwldr_parser(const unsigned char *fw_data);

static int fwldr_wait_for_completion(void);

static void dir_mem_cpy(unsigned int addr,
			unsigned char *data,
			unsigned int len);

static void dir_mem_set(unsigned int addr,
			unsigned char data,
			unsigned int len);

static void dir_mem_write(unsigned int addr,
			 unsigned int data);

static void core_mem_cpy(unsigned int addr,
			 unsigned char *data,
			 unsigned int len);

static void core_mem_set(unsigned int addr,
			 unsigned int data,
			 unsigned int len);

/* dir_mem_cpy
 *
 * Perform a memcpy of 'len' bytes from 'src_addr' to the UCCP memory location
 * pointed by 'dst_addr'.
 *
 * dst_addr is always a 4 byte aligned address
 * data is always a 4 byte aligned address
 * len is always a multiple of 4 when dst_addr is of type 0xB4xxxxxx
 * len may NOT be a multiple of 4 when dst_addr is of type 0xB7xxxxxx
 *
 *
 * When dst_addr is of type 0xB4xxxxxx, perform only 32 bit writes to these
 * locations
 *
 */
static void dir_mem_cpy(unsigned int addr,
			unsigned char *data,
			unsigned int len)
{
	int i;
	unsigned long offset = (unsigned long)addr & UCCP_OFFSET_MASK;
	unsigned long base = ((unsigned long)addr & UCCP_BASE_MASK) >> 24;
	unsigned int *data_addr = (unsigned int *)data;
	unsigned char *gram_byte_addr, *data_byte_addr;

	if ((fpriv->gram_b4_addr ==  NULL) && (base == UCCP_GRAM_MSB)) {

		/* The HAL didn't provide a virtual address for 0xB4xxxxxx alias
		 * Convert into 0xB7 and do the writes by ignoring MSB of the
		 * 32-bit word
		 */

		addr &= 0x00FFFFFF;
		addr |= 0xB7000000;

		data_byte_addr = (unsigned char *)data_addr;
		gram_byte_addr = (void *)(fpriv->gram_addr + (offset / 4) * 3);

		hal_ops.set_mem_region(addr);

		if (len % 4 == 0) {
			for (i = 0; i < len / 4; i++) {
				memcpy(gram_byte_addr, data_byte_addr + 1, 3);
				gram_byte_addr += 3;
				data_byte_addr += 4;
			}
		} else {
			fwldr_dbg_err("%s:Unexpected length(base:%lx)\n",
				      __func__, base);
		}

	} else {

		hal_ops.set_mem_region(addr);
		if (len % 4 == 0) {
			for (i = 0; i < len / 4; i++) {
				fwload_uccp_write(fpriv, base, offset,
						  data_addr[i]);
				offset += 4;
			}
		} else {
			if (base == UCCP_GRAM_PACKED)
				memcpy((void *)(fpriv->gram_addr + offset),
				       (void *)data_addr, len);
			else
				fwldr_dbg_err("%s:Unexpected length(%lx)\n",
					      __func__, base);
		}
	}
}


/* dir_mem_set
 *
 * Perform a memset of 'len' bytes with value of 'val' to the UCCP memory
 * location pointed by 'dst_addr'.
 *
 * dst_addr is always a 4 byte aligned address
 * len is always a multiple of 4 when dst_addr is of type 0xB4xxxxxx
 * len may NOT be a multiple of 4 when dst_addr is of type 0xB7xxxxxx
 *
 *
 * When dst_addr is of type 0xB4xxxxxx, perform only 32 bit writes to these
 * locations
 *
 */
static void dir_mem_set(unsigned int addr,
			unsigned char data,
			unsigned int len)
{
	int i;
	unsigned long offset = (unsigned long)addr & UCCP_OFFSET_MASK;
	unsigned long base = ((unsigned long)addr & UCCP_BASE_MASK) >> 24;
	unsigned char *gram_byte_addr;

	if ((fpriv->gram_b4_addr ==  NULL) && (base == UCCP_GRAM_MSB)) {

		/* The HAL didn't provide a virtual address for 0xB4xxxxxx alias
		 * Convert into 0xB7 and do the writes by ignoring MSB of the
		 * 32-bit word
		 */

		addr &= 0x00FFFFFF;
		addr |= 0xB7000000;

		gram_byte_addr = (void *)(fpriv->gram_addr + (offset / 4) * 3);

		hal_ops.set_mem_region(addr);

		if (len % 4 == 0) {
			memset(gram_byte_addr, data, (len / 4) * 3);
		} else {
			fwldr_dbg_err("%s :Unexpected length (base : %lx)\n",
				      __func__, base);
		}

	} else {

		hal_ops.set_mem_region(addr);

		if (len % 4 == 0) {
			for (i = 0; i <= len / 4; i++) {
				fwload_uccp_write(fpriv, base, offset, data);
				offset += 4;
			}
		} else if (base == UCCP_GRAM_PACKED) {
			memset((void *)(fpriv->gram_addr + offset),
			       data, len);
		} else {
			fwldr_dbg_err("%s: Unexpected length (base %lx)\n",
				      __func__, base);
		}
	}
}

/* Perform 'len' 32 bit reads from a UCCP memory location 'addr'
 * 'addr' is always a 4 byte aligned address
 */
void dir_mem_read(unsigned int addr,
			 unsigned int *data,
			 unsigned int len)
{
	int i = 0;
	unsigned long offset = (unsigned long)addr & UCCP_OFFSET_MASK;
	unsigned long base = ((unsigned long)addr & UCCP_BASE_MASK) >> 24;

	hal_ops.set_mem_region(addr);

	for (i = 0; i <= len / 4; i++) {
		fwload_uccp_read(fpriv, base, offset, data+i);
		offset += 4;
	}
}

/* 32 bit write to UCCP memory location 'addr'
 * 'addr' is always a 4 byte aligned address
 */
static void dir_mem_write(unsigned int addr,
			 unsigned int data)
{
	unsigned long offset = (unsigned long)addr & UCCP_OFFSET_MASK;
	unsigned long base = ((unsigned long)addr & UCCP_BASE_MASK) >> 24;

	hal_ops.set_mem_region(addr);
	fwload_uccp_write(fpriv, base, offset, data);
}


static void core_mem_cpy(unsigned int addr,
			 unsigned char *data,
			 unsigned int len)
{
	unsigned int i = 0;
	unsigned int *src_data = (unsigned int *)data;
	unsigned int flag = 0;
	unsigned int val = 0;

	/* Poll MSLVCTRL1 */
	do {
		dir_mem_read(MSLVCTRL1, &val, 1);
	} while (!MSLAVE_READY(val));

	if (len > 1)
		flag = SLAVE_BLOCK_WRITE;
	else
		flag = SLAVE_SINGLE_WRITE;

	dir_mem_write(MSLVCTRL0,
			((addr & SLAVE_ADDR_MODE_MASK) | flag));

	for (i = 0; i < len / 4; i++) {
		do {
			dir_mem_read(MSLVCTRL1, &val, 1);
		} while (!MSLAVE_READY(val));

		if (data != NULL)
			dir_mem_write(MSLVDATAT, src_data[i]);
		else
			dir_mem_write(MSLVDATAT, 0x00);
	}

}


static void core_mem_set(unsigned int addr,
			 unsigned int data,
			 unsigned int len)
{
	unsigned int flag = 0;
	unsigned int val = 0;

	/* Poll MSLVCTRL1 */
	do {
		dir_mem_read(MSLVCTRL1, &val, 1);
	} while (!MSLAVE_READY(val));

	if (len > 1)
		flag = SLAVE_BLOCK_WRITE;
	else
		flag = SLAVE_SINGLE_WRITE;

	dir_mem_write(MSLVCTRL0,
			((addr & SLAVE_ADDR_MODE_MASK) | flag));
	dir_mem_write(MSLVDATAT, data);
}


void core_mem_read(unsigned int addr,
			  unsigned int *data,
			  unsigned int len)
{
	unsigned int i = 0;
	unsigned int val = 0;

	/* Poll MSLVCTRL1 */
	do {
		dir_mem_read(MSLVCTRL1, &val, 1);
	} while (!MSLAVE_READY(val));

	dir_mem_write(MSLVCTRL0,
			((addr & SLAVE_ADDR_MODE_MASK) | SLAVE_BLOCK_READ));

	for (i = 0; i < len-1; i++) {
		do {
			dir_mem_read(MSLVCTRL1, &val, 1);
		} while (!MSLAVE_READY(val));

		dir_mem_read(MSLVDATAT, &data[i], 1);
	}

	/* Read the last word */
	do {
		dir_mem_read(MSLVCTRL1, &val, 1);
	} while (!MSLAVE_READY(val));

	dir_mem_read(MSLVDATAX, &data[len-1], 1);


}

int rpudump_init(void)
{
	fpriv = &fpv;

	hal_ops.request_mem_regions(&fpriv->gram_addr,
				    &fpriv->sysbus_addr,
				    &fpriv->gram_b4_addr);
	return 0;
}

int fwldr_load_fw(const unsigned char *fw_data, int i)
{
	struct fwldr_cfg_rw rw_v;
	int err = FWLDR_SUCCESS;

	fpriv = &fpv;
	hal_ops.request_mem_regions(&fpriv->gram_addr,
				    &fpriv->sysbus_addr,
				    &fpriv->gram_b4_addr);


	fwldr_soft_reset(LTP_THREAD_NUM);

	memset(&rw_v, 0, sizeof(rw_v));
	rw_v.addr = UCCP_GRAM_BASE + UCCP_THRD_EXEC_SIG_OFFSET;
	rw_v.val = 0x00;
	fwldr_config_write(rw_v.addr, rw_v.val);

	err = fwldr_parser(fw_data);

	if (err != FWLDR_SUCCESS) {
		pr_err("FW load failed\n");
		return err;
	}

	if (!fwldr_wait_for_completion()) {
		pr_err("FW load timed out waiting for completion\n");
		return FWLDR_FAIL;
	}
	if (!i)
		fwldr_stop_thrd(LTP_THREAD_NUM);
	return err;
}


static void fwldr_load_mem(unsigned int dst_addr,
		    unsigned int len,
		    unsigned char *src_buf)
{
	enum uccp_mem_region mem_region = UCCP_MEM_ERR;
	int i = 0;

	mem_region = fwldr_chk_region(dst_addr, len);

	fwldr_dbg_info("%s dst_addr = 0x%X, length = 0x%X, srcaddr = 0x%X\n",
		       __func__, dst_addr, len, (unsigned int)src_buf);

	fwldr_dbg_info("Dump upto 16 bytes\n");

	if (0 != (dst_addr % 4))
		fwldr_dbg_info("Destination Address is not 4 - byte aligned\n");

	for (i = 0; i < 16; i += 2)
		fwldr_dbg_dump("0x%X \t 0x%X\n", src_buf[i], src_buf[i + 1]);

	switch (mem_region) {
	case UCCP_MEM_CORE:
		core_mem_cpy(dst_addr, src_buf, len);
		break;

	case UCCP_MEM_DIRECT:
		dir_mem_cpy(dst_addr, src_buf, len);
		break;

	default:
		fwldr_dbg_err("Region unknown. Skipped writing\n");
		break;
	}
}


static void fwldr_start_thrd(unsigned int thrd_num,
		      unsigned int stack_ptr,
		      unsigned int prog_ctr,
		      unsigned int catch_state_addr)
{
	fwldr_dbg_info("%s PC = 0x%X,\tSP = 0x%X\n",
		       __func__, prog_ctr, stack_ptr);

	/* Program Counter */
	core_mem_set(MTX_TXUXXRXDT, prog_ctr, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_PC_REG_IND_ADDR, 1);

	/* Stack Pointer */
	core_mem_set(MTX_TXUXXRXDT, stack_ptr, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_A0STP_REG_IND_ADDR, 1);

	/* Thread Enable */
	core_mem_set(MTX_TXENABLE_ADDR, MTX_START_EXECUTION, 1);

	fwldr_dbg_info("Thread %d is Enabled\n", thrd_num);
}


static void fwldr_stop_thrd(unsigned int thrd_num)
{
	unsigned int val;

	/* Thread Disable */
	core_mem_set(MTX_TXENABLE_ADDR, MTX_STOP_EXECUTION, 1);

	core_mem_read(MTX_TXENABLE_ADDR, &val, 1);

	while ((val & 0x2) != 0x2) {
		core_mem_read(MTX_TXENABLE_ADDR, &val, 1);

		fwldr_dbg_info("%s val = 0x%X\n", __func__, val);

	}

	fwldr_dbg_info("TXENABLE = 0x%X\n", val);
	fwldr_dbg_info("Thread %d is Stopped\n", thrd_num);
}


static void fwldr_zero_mem(unsigned int dst_addr,
		    unsigned int len)
{
	int mem_region = 0;

	fwldr_dbg_info("%s DestAddr = 0x%X, length = 0x%X\n",
		       __func__, dst_addr, len);

	if (0 != (dst_addr % 4))
		fwldr_dbg_info("Destination Address is not 4 - byte aligned");

	mem_region = fwldr_chk_region(dst_addr, len);

	switch (mem_region) {
	case UCCP_MEM_CORE:
		core_mem_cpy(dst_addr, NULL, len);
		break;

	case UCCP_MEM_DIRECT:
		dir_mem_set(dst_addr, 0x00, len);
		break;

	default:
		fwldr_dbg_err("Region unknown. Skipped setting\n");
		break;
	}
}


static unsigned int fwldr_config_read(unsigned int dst_addr)
{
	int mem_region = 0;
	int val = 0;

	fwldr_dbg_info("%s dst_addr = 0x%X\n", __func__, dst_addr);

	if (0 != (dst_addr % 4))
		fwldr_dbg_info("Destination Address is not 4 - byte aligned");

	mem_region = fwldr_chk_region(dst_addr, 0);

	switch (mem_region) {
	case UCCP_MEM_CORE:
		core_mem_read(dst_addr, &val, 1);
		return val;

	case UCCP_MEM_DIRECT:
		dir_mem_read(dst_addr, &val, 1);
		return val;

	default:
		fwldr_dbg_err("Region unknown. Skipped reading\n");
		return 0;
	}

	return 0;
}


static void fwldr_config_write(unsigned int dst_addr,
			unsigned int val)
{
	int mem_region = 0;

	fwldr_dbg_info("%s dst_addr = 0x%X,\tValue = 0x%X\n",
		       __func__, dst_addr, val);

	if (0 != (dst_addr % 4))
		fwldr_dbg_info("Destination Address is not 4 - byte aligned");

	mem_region = fwldr_chk_region(dst_addr, 0);


	switch (mem_region) {
	case UCCP_MEM_CORE:
		core_mem_set(dst_addr, val, 1);
		break;

	case UCCP_MEM_DIRECT:
		dir_mem_write(dst_addr, val);
		break;

	default:
		fwldr_dbg_err("Region unknown. Skipped writing\n");
		break;
	}

}


static enum uccp_mem_region fwldr_chk_region(unsigned int src_addr, int len)
{
	unsigned int dst_addr = src_addr + len;

	if (((src_addr >= 0x03000000) && (src_addr <= 0x04FFFFFF))  ||
	    ((src_addr >= 0x02009000) && (src_addr <= 0x0203BFFF))  ||
	    ((src_addr >= 0x80000000) && (src_addr <= 0x87FFFFFF))) {
		if (len != 0) {
			if (((dst_addr >= 0x03000000) &&
			     (dst_addr <= 0x04FFFFFF)) ||
			    ((dst_addr >= 0x02009000) &&
			     (dst_addr <= 0x0203BFFF)) ||
			    ((dst_addr >= 0x80000000) &&
			     (dst_addr <= 0x87FFFFFF)))
				return UCCP_MEM_CORE;
			else
				return UCCP_MEM_ERR;
		}

		return UCCP_MEM_CORE;
	} else if ((src_addr & 0xFF000000) == 0xB0000000) {
		return UCCP_MEM_ERR;
	} else {
		return UCCP_MEM_DIRECT;
	}
}


static void fwldr_soft_reset(unsigned int thrd_num)
{
	unsigned int val, temp;
	unsigned int retries = 3;

	/* If the thread is running, then stop it and clear the registers,
	 * otherwise do nothing
	 */
	core_mem_read(MTX_TXENABLE_ADDR, &val, 1);

	fwldr_dbg_info("Resetting UCCP420\n");

	/* Soft Reset */
	dir_mem_read(MSLVSRST, &val, 1);
	dir_mem_write(MSLVSRST, (val | 1));

	/* Wait for 16 core clock cycles. Core runs at 320MHz */
	udelay(10);

	/* Clear the Soft Reset */
	dir_mem_write(MSLVSRST, (val & 0xFFFFFFFE));

	/* Give additional 20 ms for the DA to do its own reset */
	mdelay(20);

	/* Clear the Minim Bit in PrivExt */
	core_mem_set(MTX_TXPRIVEXT_ADDR, 0, 1);

	/* Set the PCX value i to 0 */
	core_mem_set(MTX_TXUXXRXDT, 0, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_PCX_REG_IND_ADDR, 1);


	/* Clear TXPOLL{I} to clear TXSTAT{I}
	 * Writing 0xFFFFFFFF clears TXSTATI, but TXMASKI must
	 * be all set too for this to work.
	 */
	core_mem_set(MTX_TXUXXRXDT, 0xFFFFFFFF, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXMASK_REG_IND_ADDR, 1);

	core_mem_set(MTX_TXUXXRXDT, 0xFFFFFFFF, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXMASKI_REG_IND_ADDR, 1);

	core_mem_set(MTX_TXUXXRXDT, 0xFFFFFFFF, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXPOLL_REG_IND_ADDR, 1);

	core_mem_set(MTX_TXUXXRXDT, 0xFFFFFFFF, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXPOLLI_REG_IND_ADDR, 1);

	/* Clear TXMASK and TXMASKI */
	core_mem_set(MTX_TXUXXRXDT, 0x0, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXMASK_REG_IND_ADDR, 1);


	core_mem_set(MTX_TXUXXRXDT, 0x0, 1);
	core_mem_set(MTX_TXUXXRXRQ, MTX_TXMASKI_REG_IND_ADDR, 1);

	/* Ensure all kicks are cleared */
	core_mem_set(MTX_TXUXXRXRQ,
			     (MTX_TXPOLLI_REG_IND_ADDR | REG_IND_READ_FLAG),
			     1);

	core_mem_read(MTX_TXUXXRXDT, &temp, 1);

	while (temp && retries--) {
		core_mem_set(MTX_TXUXXRXDT, 0x2, 1);

		core_mem_set(MTX_TXUXXRXRQ,
				     MTX_TXPOLLI_REG_IND_ADDR,
				     1);

		core_mem_set(MTX_TXUXXRXRQ,
				     (MTX_TXPOLLI_REG_IND_ADDR |
				      REG_IND_READ_FLAG),
				     1);

		core_mem_read(MTX_TXUXXRXDT, &temp, 1);
	}

	/* Reset TXSTATUS */
	core_mem_set(MTX_TXSTATUS_ADDR, 0x00020000, 1);

	fwldr_dbg_info("Soft Reset core\n");
}


/* Reads a 16-bit little endian value from the specified position in a buffer */
static unsigned short fwldr_read_le2(unsigned char *buf)
{
	unsigned short val = 0;

	val  = buf[0];
	val |= buf[1] << 8;

	return val;
}


/* Reads a 32-bit little endian value from the specified position in a buffer */
static unsigned int fwldr_read_le4(unsigned char *buf)
{
	unsigned int val = 0;

	val = buf[0];
	val |= buf[1] << 8;
	val |= buf[2] << 16;
	val |= buf[3] << 24;

	return val;
}


/* Converts a virtual (paged) offset to a linear (non-paged) offset */
static unsigned fwldr_virt_to_linear_off(unsigned page_size, unsigned offset)
{
	static unsigned virt_page_size;

	unsigned val = offset;

	if (page_size) {
		if (virt_page_size == 0) {
			virt_page_size = 1;
			while (virt_page_size < page_size)
				virt_page_size <<= 1;
		}

		val = ((offset / virt_page_size) * page_size) +
			(offset % virt_page_size);
	}

	return val;
}


static int fwldr_parser(const unsigned char *fw_data)
{
	int quit = 0;
	signed int nxt = 0;
	signed int file_offset = 0;
	signed int page_size = 0;
	signed int orig_offset = 0;
	signed int prev_offset = 0;
	struct fwldr_bootdevhdr boot_dev_hdr;
	char info_buf[256];
	char *cfg_str = NULL;
	char cfg_buf[256];
	char *cfg_buf_ptr = NULL;
	struct fwldr_sec_ldr_l1_record l1_rec;
	struct fwldr_load_mem_info lm_v;
	struct fwldr_cfg_rw rw_v;
	unsigned char l1_buf[FWLDR_L1_MAXSIZE];
	signed int seek_off;
	char *str_curr = NULL;
	char *str_end = NULL;
	char *str_new = NULL;
	int buf_len = 0;
	int res = FWLDR_SUCCESS;

	/* Lets really do it */
	memcpy(&boot_dev_hdr, fw_data, sizeof(struct fwldr_bootdevhdr));

	fwldr_dbg_info("DevID:  0x%08X\n", boot_dev_hdr.dev_id);
	fwldr_dbg_info("SLCode: 0x%08X\n", boot_dev_hdr.sl_code);
	fwldr_dbg_info("SLData: 0x%08X\n", boot_dev_hdr.sl_data);
	fwldr_dbg_info("PLCtrl: 0x%04X\n", boot_dev_hdr.pl_ctrl);
	fwldr_dbg_info("CRC:    0x%04X\n", boot_dev_hdr.CRC);
	fwldr_dbg_info("%d", sizeof(boot_dev_hdr));
	fwldr_dbg_info("\n");

	file_offset = fwldr_virt_to_linear_off(page_size, boot_dev_hdr.sl_code);
	orig_offset = fwldr_virt_to_linear_off(page_size, boot_dev_hdr.sl_data);

	nxt = sizeof(struct fwldr_bootdevhdr);

	do {
		unsigned char sec_ldr_code[FWLDR_PLRCRD_BYTES];

		memcpy(&sec_ldr_code,
		       fw_data + file_offset,
		       FWLDR_PLRCRD_BYTES);

		nxt = fwldr_read_le4(&sec_ldr_code[FWLDR_PLRCRD_DATA_BYTES]);

		file_offset = fwldr_virt_to_linear_off(page_size,
						       nxt);
	} while (nxt);

	file_offset = orig_offset;

	while (!quit) {
		unsigned char *l2_buf = NULL, *l2_blk = NULL;
		unsigned int l2_len = 0U;

		memcpy(&l1_buf,
		       fw_data + file_offset,
		       FWLDR_L1_MAXSIZE);

		l1_rec.cmd_tag = fwldr_read_le2(&l1_buf[FWLDR_L1_CMD_OFF]);
		l1_rec.len = fwldr_read_le2(&l1_buf[FWLDR_L1_LEN_OFF]);
		l1_rec.nxt = fwldr_read_le4(&l1_buf[FWLDR_L1_NXT_OFF]);

		if ((l1_rec.len > FWLDR_L1_MAXSIZE) ||
		    (l1_rec.len < FWLDR_L1_L2OFF_OFF) ||
		    (l1_rec.len < FWLDR_L1_L2LEN_OFF)) {
			fwldr_dbg_err("Maximum L1 length exceeded\n");
			res = FWLDR_FAIL;
			break;
		}

		/* Extract generic L1 fields */
		l1_rec.l2_offset = fwldr_read_le4(&l1_buf[l1_rec.len -
						  FWLDR_L1_L2OFF_OFF]);

		l1_rec.l2_len = fwldr_read_le2(&l1_buf[l1_rec.len -
					       FWLDR_L1_L2LEN_OFF]);

		if (l1_rec.l2_len > FWLDR_L2_BASIC_SIZE) {
			/* Read the L2 data */
			seek_off = fwldr_virt_to_linear_off(page_size,
							    l1_rec.l2_offset);

			if (l1_rec.l2_len > FWLDR_L2_MAXSIZE) {
				fwldr_dbg_err("Maximum L2 length exceeded\n");
				res = FWLDR_FAIL;
				break;
			}

			l2_blk = kmalloc(l1_rec.l2_len + 1,
					 GFP_KERNEL);

			if (l2_blk == NULL) {
				res = FWLDR_FAIL;
				break;
			}

			memcpy(l2_blk,
			       fw_data + seek_off,
			       l1_rec.l2_len);

			l2_blk[l1_rec.l2_len] = '\0';

			l2_buf = l2_blk +
				(FWLDR_L2_CMD_SIZE + FWLDR_L2_LEN_SIZE);

			l2_len = l1_rec.l2_len - FWLDR_L2_BASIC_SIZE;
		}

		switch (l1_rec.cmd_tag) {
		case FWLDR_L1_CMD_LOAD_MEM:
			if (!l2_buf) {
				fwldr_dbg_err("Invalid params to Load Mem\n");
				res = FWLDR_FAIL;
				quit = 1;
				break;
			}

			/* Load mem record */
			l1_rec.arg1 =
				fwldr_read_le4(&l1_buf[FWLDR_L1_ARG1_OFF]);

			snprintf(info_buf,
				 sizeof(info_buf),
				 "%-12s: Addr: 0x%08X: Size: 0x%08X\n",
				 "LoadMem", l1_rec.arg1, l2_len);

			lm_v.dst_addr = l1_rec.arg1;
			lm_v.len = l2_len;
			lm_v.src_buf = l2_buf;

			fwldr_load_mem(lm_v.dst_addr,
				       lm_v.len,
				       lm_v.src_buf);
			break;

		case FWLDR_L1_CMD_START_THRDS:
			/* Start each thread with initial SP */
			if (!l2_buf) {
				fwldr_dbg_err("%s : %d Invalid params\n",
					      __func__, __LINE__);
				res = FWLDR_FAIL;
				quit = 1;
				break;
			}

			cfg_buf[0] = '\0';
			cfg_buf_ptr = cfg_buf;

			while (l2_len > 0) {
				struct fwldr_thrd_info tinfo_v;

				snprintf(cfg_buf_ptr,
					 sizeof(cfg_buf),
					 "\tThrd %d: SP: 0x%08X: PC: 0x%08X: Catch: 0x%08X\n",
					 fwldr_read_le4(l2_buf),
					 fwldr_read_le4(l2_buf + 4),
					 fwldr_read_le4(l2_buf + 8),
					 fwldr_read_le4(l2_buf + 12));

				tinfo_v.thrd_num = fwldr_read_le4(l2_buf);
				tinfo_v.stack_ptr = fwldr_read_le4(l2_buf + 4);
				tinfo_v.prog_ctr = fwldr_read_le4(l2_buf + 8);
				tinfo_v.catch_state_addr =
					fwldr_read_le4(l2_buf + 12);

				fwldr_start_thrd(tinfo_v.thrd_num,
						 tinfo_v.stack_ptr,
						 tinfo_v.prog_ctr,
						 tinfo_v.catch_state_addr);

				l2_buf += (4 * sizeof(unsigned int));
				l2_len -= (4 * sizeof(unsigned int));
				cfg_buf_ptr += strlen(cfg_buf_ptr);
			}

			snprintf(info_buf,
				 sizeof(info_buf),
				 "%-12s:\n%s",
				 "StartThrds",
				 cfg_buf);

			break;

		case FWLDR_L1_CMD_ZERO_MEM:
			/* Zero memory */
			l1_rec.arg1 =
				fwldr_read_le4(&l1_buf[FWLDR_L1_ARG1_OFF]);
			l1_rec.arg2 =
				fwldr_read_le4(&l1_buf[FWLDR_L1_ARG2_OFF]);

			snprintf(info_buf,
				 sizeof(info_buf),
				 "%-12s: Addr: 0x%08X: Size: 0x%08X\n",
				 "ZeroMem",
				 l1_rec.arg1,
				 l1_rec.arg2);

			lm_v.dst_addr = l1_rec.arg1;
			lm_v.len = l1_rec.arg2;

			fwldr_zero_mem(lm_v.dst_addr, lm_v.len);

			break;

		case FWLDR_L1_CMD_CONFIG:
			/* Configuration commands */
			buf_len = (l1_rec.l2_len / 8) * 40;

			if (!l2_buf) {
				fwldr_dbg_err("%s:%d:Invalid params\n",
					      __func__, __LINE__);
				res = FWLDR_FAIL;
				quit = 1;
				break;
			}

			cfg_str = kmalloc(buf_len, GFP_KERNEL);

			if (cfg_str) {
				str_curr = cfg_str;
				str_end = cfg_str + buf_len;
			}

			do {
				int rec_len = 0, len = 0;
				unsigned int cmd = fwldr_read_le4(l2_buf);

				if ((str_curr && cfg_str) &&
				    ((str_end - str_curr) < 256)) {
					size_t pos = str_curr - cfg_str;

					/* Extend buffer */
					buf_len *= 2;

					str_new = krealloc(cfg_str,
							   buf_len,
							   GFP_KERNEL);

					if (str_new == NULL) {
						fwldr_dbg_err("%s : %d %s\n",
							__func__,
							__LINE__,
							"realloc failed");
						kfree(cfg_str);
						cfg_str = NULL;
						str_curr = NULL;
						str_end = NULL;
					} else {
						cfg_str	= str_new;
						/* Relocate pointers */
						str_curr = cfg_str +
							   pos;
						str_end = cfg_str +
							  buf_len;
					}
				}

				switch (cmd) {
				case FWLDR_CONF_CMD_PAUSE:
					rec_len = 8;
					/* TODO: Calculate the exact delay */
					mdelay(2);
					break;
				case FWLDR_CONF_CMD_READ:
					rec_len = 8;

					rw_v.addr = fwldr_read_le4(&l2_buf[4]);
					rw_v.val = fwldr_config_read(rw_v.addr);

					if (str_curr) {
						len = snprintf(str_curr,
							       buf_len,
							       "\tRead : 0x%08X\n",
							       rw_v.addr);
					}


					/* Value read is in rw_v */
					break;

				case FWLDR_CONF_CMD_WRITE:
					rec_len = 12;

					rw_v.addr = fwldr_read_le4(&l2_buf[4]);
					rw_v.val = fwldr_read_le4(&l2_buf[8]);

					if (str_curr) {
						len = snprintf(str_curr,
							       buf_len,
							       "\tWrite: 0x%08X: 0x%08X\n",
							       rw_v.addr,
							       rw_v.val);
					}


					fwldr_config_write(rw_v.addr, rw_v.val);

					break;

				case FWLDR_CONF_CMD_USER:
					if (str_curr) {
						unsigned int v1 = 0;
						unsigned int v2 = 0;
						unsigned int v3 = 0;
						unsigned int v4 = 0;

						v1 =
						    fwldr_read_le4(&l2_buf[4]),
						v2 =
						    fwldr_read_le4(&l2_buf[8]),
						v3 =
						    fwldr_read_le4(&l2_buf[12]),
						v4 =
						    fwldr_read_le4(&l2_buf[16]),

						len = snprintf(str_curr,
							       buf_len,
							       "\tUser: 0x%08X: 0x%08X: 0x%08X: 0x%08X\n",
							       v1,
							       v2,
							       v3,
							       v4);
					}

					rec_len = 20;
					break;

				default:
					if (str_curr) {
						len = snprintf(str_curr,
							       buf_len,
							       "\tUnknown: %08X (%d bytes remain)\n",
							       cmd,
							       l2_len);
					}
					break;
				}

				if ((rec_len == 0) || (res == FWLDR_FAIL))
					break;

				if (str_curr)
					str_curr += len;

				l2_buf += rec_len;
				l2_len -= rec_len;
			} while (l2_len > 0);

			snprintf(info_buf, sizeof(info_buf),
				 "%-12s: %d bytes %s\n", "Config",
				 (unsigned int)(l2_buf-l2_blk),
				 ((l2_len != 0) ? ": ERROR!!" : ""));
			break;

		case FWLDR_L1_CMD_FILENAME:
			if (!l2_blk) {
				fwldr_dbg_err("Invalid params to Filename\n");
				res = FWLDR_FAIL;
				quit = 1;
				break;
			}

			snprintf(info_buf,
				 sizeof(info_buf),
				 "%-12s: %s\n",
				 "FileName",
				 l2_blk + 8);
			break;

		default:
			/* Not expected */
			snprintf(info_buf,
				 sizeof(info_buf),
				 "%-12s\n",
				 "Unknown");
			break;
		}

		kfree(l2_blk);

		if (cfg_str) {
			fwldr_dbg_info("0x%08X: %s%s",
				       file_offset,
				       info_buf,
				       cfg_str);
			kfree(cfg_str);
			cfg_str = NULL;
		} else {
			fwldr_dbg_info("0x%08X: %s ",
				       file_offset,
				       info_buf);
		}

		if (l1_rec.nxt == FWLDR_L1_TERMINATE) {
			unsigned int overlay_off = 0;

			/* There is the possibility of further overlays.
			 * Without additional information, the best guess is
			 * that they start immediately after the L2 data of the
			 * last record.
			 */
			l1_rec.l2_offset = fwldr_read_le4(&l1_buf[l1_rec.len -
							  FWLDR_L1_L2OFF_OFF]);
			l1_rec.l2_len = fwldr_read_le2(&l1_buf[l1_rec.len -
						       FWLDR_L1_L2LEN_OFF]);

			overlay_off = fwldr_virt_to_linear_off(page_size,
							      l1_rec.l2_offset);
			overlay_off += l1_rec.l2_len;

			/* Round to next 32-bit boundary */
			overlay_off += 3;
			overlay_off &= ~3;

			fwldr_dbg_info("\n");
			fwldr_dbg_info("Possible next L1 Record 0x%08X\n",
				       overlay_off);

			quit = 1;
		} else {
			/* Move on to next L1 record */
			prev_offset = file_offset;
			file_offset = fwldr_virt_to_linear_off(page_size,
							       l1_rec.nxt);

			if (file_offset <= prev_offset) {
				/* Possibly incorrect page size specified.
				 * Stopping
				 */
				fwldr_dbg_err("Out of sequence record found\n");
				quit = 1;
			}
		}
	}

	return res;
}


static int fwldr_wait_for_completion(void)
{
	struct fwldr_cfg_rw rw_v;
	int result = 1;
	unsigned int i = 0;

	rw_v.addr = UCCP_GRAM_BASE + UCCP_THRD_EXEC_SIG_OFFSET;

	do {
		rw_v.val = fwldr_config_read(rw_v.addr);
		/* Sleep for 10 ms */
		mdelay(10);

		i++;

	} while ((UCCP_THRD_EXEC_SIG != rw_v.val) && (i < 1000));

	if (i == 1000)
		result = 0;

	return result;
}
