/*
 * Copyright (C) 2013-2014 Renesas Solutions Corp.
 * Copyright (C) 2009 Freescale Semiconductor, Inc.
 *
 * Author: Mingkai Hu (Mingkai.hu@freescale.com)
 * Based on stmicro.c by Wolfgang Denk (wd@denx.de),
 * TsiChung Liew (Tsi-Chung.Liew@freescale.com),
 * and  Jason McMullan (mcmullan@netapp.com)
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <malloc.h>
#include <spi_flash.h>

#include "spi_flash_internal.h"


#if defined(CONFIG_RTK772X)
#define	ENABLE_32BIT_ADDRESS
#define	USE_BAR
#elif defined(CONFIG_CPU_RZA1)
#define	ENABLE_32BIT_ADDRESS
#define	USE_4CMD
#else
#undef	ENABLE_32BIT_ADDRESS
#undef	USE_BAR
#undef	USE_4CMD
#endif


/* S25FLxx-specific commands */
#if defined(ENABLE_32BIT_ADDRESS) && defined(USE_4CMD)
#define CMD_S25FLXX_PP		0x12	/* CMD_S25FLXX_4PP */
#define CMD_S25FLXX_SE		0xdc	/* CMD_S25FLXX_4SE */
#define CMD_S25FLXX_FAST_READ	0x0c	/* CMD_S25FLXX_4FAST_READ */
#else
#define CMD_S25FLXX_PP		0x02	/* Page Program */
#define CMD_S25FLXX_SE		0xd8	/* Sector Erase */
#define CMD_S25FLXX_FAST_READ	0x0b	/* Read Data Bytes at Higher Speed */
#endif
#define CMD_S25FLXX_RDSR	0x05	/* Read Status Register */
#define CMD_S25FLXX_WREN	0x06	/* Write Enable */

#if defined(ENABLE_32BIT_ADDRESS) && defined(USE_BAR)
#define	CMD_S25FLXX_BRWR	0x17		/* Bank Register Write */
#define	DAT_S25FLXX_BRWR_E	(1 << 7)	/* Bank Write Enable */
#define	DAT_S25FLXX_BRWR_D	(0 << 7)	/* Bank Write Disable */
#endif // EXT_ENABLE_EXTADD

#define SPSN_ID_S25FL008A	0x0213
#define SPSN_ID_S25FL016A	0x0214
#define SPSN_ID_S25FL032A	0x0215
#define SPSN_ID_S25FL064A	0x0216
#define SPSN_ID_S25FL128P	0x2018
#define	SPSN_ID_S25FL512S	0x0220

#define SPSN_EXT_ID_S25FL128P_256KB	0x0300
#define SPSN_EXT_ID_S25FL128P_64KB	0x0301
#define SPSN_EXT_ID_S25FL032P		0x4d00

#define SPANSION_SR_WIP		(1 << 0)	/* Write-in-Progress */

struct spansion_spi_flash_params {
	u16 idcode1;
	u16 idcode2;
	u16 page_size;
	u16 pages_per_sector;
	u16 nr_sectors;
	const char *name;
};

struct spansion_spi_flash {
	struct spi_flash flash;
	const struct spansion_spi_flash_params *params;
};

static inline struct spansion_spi_flash *to_spansion_spi_flash(struct spi_flash
							     *flash)
{
	return container_of(flash, struct spansion_spi_flash, flash);
}

static const struct spansion_spi_flash_params spansion_spi_flash_table[] = {
	{
		.idcode1 = SPSN_ID_S25FL008A,
		.idcode2 = 0,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 16,
		.name = "S25FL008A",
	},
	{
		.idcode1 = SPSN_ID_S25FL016A,
		.idcode2 = 0,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 32,
		.name = "S25FL016A",
	},
	{
		.idcode1 = SPSN_ID_S25FL032A,
		.idcode2 = 0,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 64,
		.name = "S25FL032A",
	},
	{
		.idcode1 = SPSN_ID_S25FL064A,
		.idcode2 = 0,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 128,
		.name = "S25FL064A",
	},
	{
		.idcode1 = SPSN_ID_S25FL128P,
		.idcode2 = SPSN_EXT_ID_S25FL128P_64KB,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 256,
		.name = "S25FL128P_64K",
	},
	{
		.idcode1 = SPSN_ID_S25FL128P,
		.idcode2 = SPSN_EXT_ID_S25FL128P_256KB,
		.page_size = 256,
		.pages_per_sector = 1024,
		.nr_sectors = 64,
		.name = "S25FL128P_256K",
	},
	{
		.idcode1 = SPSN_ID_S25FL032A,
		.idcode2 = SPSN_EXT_ID_S25FL032P,
		.page_size = 256,
		.pages_per_sector = 256,
		.nr_sectors = 64,
		.name = "S25FL032P",
	},
	{
		.idcode1 = SPSN_ID_S25FL512S,
		.idcode2 = SPSN_EXT_ID_S25FL032P,
		.page_size = 512,
		.pages_per_sector = 512,
		.nr_sectors = 256,
		.name = "S25FL512S",
	},
};

static int spansion_wait_ready(struct spi_flash *flash, unsigned long timeout)
{
	struct spi_slave *spi = flash->spi;
	unsigned long timebase;
	int ret;
	u8 status;


	timebase = get_timer(0);
	do {
		ret = spi_flash_cmd(spi, CMD_S25FLXX_RDSR, &status, sizeof(status));
		if (ret)
			return -1;
		if ((status & SPANSION_SR_WIP) == 0)
			break;
	} while (get_timer(timebase) < timeout);

	if ((status & SPANSION_SR_WIP) == 0)
		return 0;

	/* Timed out */
	return -1;
}

static int spansion_read_fast(struct spi_flash *flash,
			     u32 offset, size_t len, void *buf)
{
	struct spansion_spi_flash *spsn = to_spansion_spi_flash(flash);
	unsigned long page_addr;
	unsigned long page_size;
	u8 cmd[5];

	/* Handle memory-mapped SPI */
	if (flash->memory_map)
		memcpy(buf, flash->memory_map + offset, len);
	
	page_size = spsn->params->page_size;
	page_addr = offset / page_size;

	cmd[0] = CMD_S25FLXX_FAST_READ;
#if defined(ENABLE_32BIT_ADDRESS)
	cmd[1] = (offset >> 24) & 0xff;
	cmd[2] = (offset >> 16) & 0xff;
	cmd[3] = (offset >>  8) & 0xff;
	cmd[4] = (offset >>  0) & 0xff;
#else
	cmd[1] = page_addr >> 8;
	cmd[2] = page_addr;
	cmd[3] = offset % page_size;
	cmd[4] = 0x00;
#endif

	debug("READ: 0x%x => cmd = { 0x%02x 0x%02x%02x%02x%02x } len = 0x%x\n",
		 offset, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], len);

	return spi_flash_read_common(flash, cmd, sizeof(cmd), buf, len);
}

static int spansion_write(struct spi_flash *flash,
			 u32 offset, size_t len, const void *buf)
{
	struct spansion_spi_flash *spsn = to_spansion_spi_flash(flash);
	unsigned long page_addr;
	unsigned long byte_addr;
	unsigned long page_size;
	size_t chunk_len;
	size_t actual;
	int ret;
#if defined(ENABLE_32BIT_ADDRESS)
	u8 cmd[5]; u32 u32WriteAddr;
#else
	u8 cmd[4];
#endif

	page_size = spsn->params->page_size;
	page_addr = offset / page_size;
	byte_addr = offset % page_size;

	ret = spi_claim_bus(flash->spi);
	if (ret) {
		debug("SF: Unable to claim SPI bus\n");
		return ret;
	}

	ret = 0;
	for (actual = 0; actual < len; actual += chunk_len) {
		chunk_len = min(len - actual, page_size - byte_addr);

		if (flash->spi->max_write_size)
			chunk_len = min(chunk_len, flash->spi->max_write_size);
		
		cmd[0] = CMD_S25FLXX_PP;
#if defined(ENABLE_32BIT_ADDRESS)
		u32WriteAddr = offset + actual;
		cmd[1] = (u32WriteAddr >> 24) & 0xff;
		cmd[2] = (u32WriteAddr >> 16) & 0xff;
		cmd[3] = (u32WriteAddr >>  8) & 0xff;
		cmd[4] = (u32WriteAddr >>  0) & 0xff;
#else
		cmd[1] = page_addr >> 8;
		cmd[2] = page_addr;
		cmd[3] = byte_addr;
#endif

		debug
		    ("PP: 0x%p => cmd = { 0x%02x 0x%02x%02x%02x } chunk_len = %d\n",
		     buf + actual, cmd[0], cmd[1], cmd[2], cmd[3], chunk_len);

		ret = spi_flash_cmd(flash->spi, CMD_S25FLXX_WREN, NULL, 0);
		if (ret < 0) {
			debug("SF: Enabling Write failed\n");
			break;
		}

#if defined(ENABLE_32BIT_ADDRESS)
		ret = spi_flash_cmd_write(flash->spi, cmd, sizeof(cmd) / sizeof(u8),
#else
		ret = spi_flash_cmd_write(flash->spi, cmd, 4,
#endif
					  buf + actual, chunk_len);
		if (ret < 0) {
			debug("SF: SPANSION Page Program failed\n");
			break;
		}

		ret = spansion_wait_ready(flash, SPI_FLASH_PROG_TIMEOUT);
		if (ret < 0) {
			debug("SF: SPANSION page programming timed out\n");
			break;
		}

		byte_addr += chunk_len;
		if (byte_addr == page_size) {
			page_addr++;
			byte_addr = 0;
		}
	}

	debug("SF: SPANSION: Successfully programmed %u bytes @ 0x%x\n",
	      len, offset);

	spi_release_bus(flash->spi);
	return ret;
}

int spansion_erase(struct spi_flash *flash, u32 offset, size_t len)
{
	struct spansion_spi_flash *spsn = to_spansion_spi_flash(flash);
	unsigned long sector_size;
	size_t actual;
	int ret;
#if defined(ENABLE_32BIT_ADDRESS)
	u8 cmd[5]; u32 u32EraseAddr;
#else
	u8 cmd[4];
#endif

	/*
	 * This function currently uses sector erase only.
	 * probably speed things up by using bulk erase
	 * when possible.
	 */

	sector_size = spsn->params->page_size * spsn->params->pages_per_sector;

	if (offset % sector_size || len % sector_size) {
		debug("SF: Erase offset/length not multiple of sector size\n");
		return -1;
	}

	cmd[0] = CMD_S25FLXX_SE;
	cmd[2] = 0x00;
	cmd[3] = 0x00;

	ret = spi_claim_bus(flash->spi);
	if (ret) {
		debug("SF: Unable to claim SPI bus\n");
		return ret;
	}

	ret = 0;
	for (actual = 0; actual < len; actual += sector_size) {

#if defined(ENABLE_32BIT_ADDRESS)
		u32EraseAddr = offset + actual;
		cmd[1] = (u32EraseAddr >> 24) & 0xff;
		cmd[2] = (u32EraseAddr >> 16) & 0xff;
		cmd[3] = (u32EraseAddr >>  8) & 0xff;
		cmd[4] = (u32EraseAddr >>  0) & 0xff;
#else
		cmd[1] = (offset + actual) >> 16;
#endif

		ret = spi_flash_cmd(flash->spi, CMD_S25FLXX_WREN, NULL, 0);
		if (ret < 0) {
			debug("SF: Enabling Write failed\n");
			break;
		}

#if defined(ENABLE_32BIT_ADDRESS)
		ret = spi_flash_cmd_write(flash->spi, cmd, sizeof(cmd) / sizeof(u8), NULL, 0);
#else
		ret = spi_flash_cmd_write(flash->spi, cmd, 4, NULL, 0);
#endif
		if (ret < 0) {
			debug("SF: SPANSION page erase failed\n");
			break;
		}

		/* Up to 2 seconds */
		ret = spansion_wait_ready(flash, SPI_FLASH_PAGE_ERASE_TIMEOUT);
		if (ret < 0) {
			debug("SF: SPANSION page erase timed out\n");
			break;
		}
	}

	debug("SF: SPANSION: Successfully erased %u bytes @ 0x%x\n",
	      len, offset);

	spi_release_bus(flash->spi);
	return ret;
}

struct spi_flash *spi_flash_probe_spansion(struct spi_slave *spi, u8 *idcode)
{
	const struct spansion_spi_flash_params *params;
	struct spansion_spi_flash *spsn;
	unsigned int i;
	unsigned short jedec, ext_jedec;

	jedec = idcode[1] << 8 | idcode[2];
	ext_jedec = idcode[3] << 8 | idcode[4];

	for (i = 0; i < ARRAY_SIZE(spansion_spi_flash_table); i++) {
		params = &spansion_spi_flash_table[i];
		if (params->idcode1 == jedec) {
			if (params->idcode2 == ext_jedec)
				break;
		}
	}

	if (i == ARRAY_SIZE(spansion_spi_flash_table)) {
		debug("SF: Unsupported SPANSION ID %04x %04x\n", jedec, ext_jedec);
		return NULL;
	}

	spsn = spi_flash_alloc(struct spansion_spi_flash, spi, params->name);
	if (!spsn) {
		debug("SF: Failed to allocate memory\n");
		return NULL;
	}

	spsn->params = params;
	spsn->flash.spi = spi;
	spsn->flash.name = params->name;
	spsn->flash.memory_map = 0;

	spsn->flash.write = spansion_write;
	spsn->flash.erase = spansion_erase;
	spsn->flash.read = spansion_read_fast;
	spsn->flash.size = params->page_size * params->pages_per_sector
	    * params->nr_sectors;
	spsn->flash.sector_size = params->page_size * params->pages_per_sector;
	printf("SF: Detected %s with page size %u, total ",
	params->name, params->page_size);
	print_size(spsn->flash.size, "");

	if (spsn->flash.memory_map)
		printf(", mapped at %p", spsn->flash.memory_map);
	puts("\n");
	
#if defined(ENABLE_32BIT_ADDRESS) && defined(USE_BAR)
	{
		int ret;
		u8 u8Cmd, u8Param;
		u8Cmd	= CMD_S25FLXX_BRWR;
		u8Param	= DAT_S25FLXX_BRWR_E;

		ret = spi_flash_cmd_write(spi,
			&u8Cmd, sizeof(u8Cmd), &u8Param, sizeof(u8Param));
		if(ret < 0){
			printf("SF: Failed to Bank Register Write command. (%d)\n", ret);
			free(spsn);
			return NULL;
		}
	}
#endif

	return &spsn->flash;
}
