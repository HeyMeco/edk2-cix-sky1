/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The memory configuration ("CDCB", bootloader1 sub-image 3) as the DDR
 * driver reads it (CIX driver/ddr/src/Lib/Cdcb.c): a 32-byte quick
 * configuration, a global header at 0x20, the block table at 0x80 and the
 * blocks, each with a GUID, a board mask and a checksum.  The board is
 * found from GPIO straps or the quick configuration and mapped to a board
 * mask that selects the board-specific blocks.  Translated from CIX's SE
 * firmware debug build, function by function.
 */
#include "mem_types.h"

#define FILE_CODE		0x2001		/* drv_ddr_assert file code */
#define CDCB_SIGNATURE		0x42434443	/* "CDCB" */
#define CDCB_MAJOR_VER		2
#define CDCB_HEADER_OFFSET	0x20
#define CDCB_BLOCKTB_OFFSET	0x60		/* from the global header */
#define CDCB_BUF_SIZE		0x1000
#define GUID_BDID_GET_METHOD	0x1000
#define GUID_BOARDID_MAP	0x1009

/* CIX keeps these in one block, in this order */
static uint8_t *mCdcbBuf;			/* the whole image, read once */
static uint32_t mCdcbBufSize;
static MEM_QUICK_CONFIG *QuickConfigBuf;	/* CdcbGetQuickConfig's buf */
static MEM_CONFIG_BLOCK_TABLE_ENTRY *mCdcbBlockEntry;
static uint8_t mCdcbBlockCount;
static uint16_t mBoardMask;

/*
 * CdcbUpdateBlockBuf: recompute a block's checksum and write the block back
 * to flash.  CIX passes the new checksum byte, not the block size, as the
 * length to write (so between 0 and 255 bytes of the block get written);
 * kept as it is.
 */
void CdcbUpdateBlockBuf(void *buf)
{
	MEM_CONFIG_BLOCK_HEADER *Header = buf;
	uint8_t *p = buf, checksum = 0;
	uint32_t i;

	Header->BlockChecksum = 0;
	for (i = 0; i < Header->BlockSize; i++)
		checksum += p[i];
	checksum = ~checksum;
	Header->BlockChecksum = checksum;
	set_memcfg_img(buf, p - mCdcbBuf, checksum);
}

/* CdcbDeallocateBuffer: free the image.  CIX leaves the quick
 * configuration and block table pointers into it as they are. */
void CdcbDeallocateBuffer(void)
{
	if (mCdcbBuf) {
		free(mCdcbBuf);
		mCdcbBuf = NULL;
		mCdcbBufSize = 0;
	}
}

/* CdcbLoadBuffer: *buf = the image at offset; the first call reads the
 * whole image into a 4 KiB buffer (len is not used; CIX does not check
 * that the image fits) */
static uint32_t CdcbLoadBuffer(uint8_t **buf, uint32_t offset, uint32_t len)
{
	if (!mCdcbBuf) {
		mCdcbBuf = malloc(CDCB_BUF_SIZE);
		if (!mCdcbBuf) {
			log_print(0x80000000, "ERROR: Fail to allocate buffer for memory configuration table.\n");
			drv_ddr_assert(FILE_CODE, 97);		/* CIX's line numbers */
			return 0xffffffff;
		}
		if (!get_memcfg_img(mCdcbBuf, &mCdcbBufSize)) {
			log_print(0x80000000, "ERROR: Fail to load memory configuration table. errorcode %x\n", 0);
			drv_ddr_assert(FILE_CODE, 102);
			CdcbDeallocateBuffer();
			return 0xffffffff;
		}
	}
	*buf = mCdcbBuf + offset;
	return 0;
}

/* CdcbGetQuickConfig */
MEM_QUICK_CONFIG *CdcbGetQuickConfig(void)
{
	if (!QuickConfigBuf && CdcbLoadBuffer((uint8_t **)&QuickConfigBuf, 0, sizeof(MEM_QUICK_CONFIG)))
		QuickConfigBuf = NULL;
	return QuickConfigBuf;
}

/* CdcbGetGlobalHeader */
uint32_t CdcbGetGlobalHeader(MEM_CONFIG_HEADER **header)
{
	return CdcbLoadBuffer((uint8_t **)header, CDCB_HEADER_OFFSET, sizeof(MEM_CONFIG_HEADER));
}

/* CdcbDeallocateBlockBuf: blocks live in the image buffer */
void CdcbDeallocateBlockBuf(uint8_t **buf)
{
}

/*
 * CdcbLocateBlockBuf: the block with this GUID for the board: one whose
 * board mask has the board's bit, else the last one for all boards
 * (0xffff); checksum checked.
 */
uint32_t CdcbLocateBlockBuf(uint32_t guid, uint32_t *size, uint8_t **buf)
{
	MEM_CONFIG_BLOCK_HEADER *BlockHeader;
	uint32_t index, offset = 0, i;
	uint8_t checksum;

	if (!mCdcbBlockEntry || !mCdcbBlockCount) {
		log_print(0x80000000, "Error: CDCB driver not initialized\n");
		drv_ddr_assert(FILE_CODE, 176);
		return 0xffffffff;
	}
	for (index = 0; index < mCdcbBlockCount; index++) {
		if (mCdcbBlockEntry[index].Guid != guid)
			continue;
		/* CIX goes on with the header pointer as it was if this fails */
		CdcbLoadBuffer((uint8_t **)&BlockHeader, mCdcbBlockEntry[index].Offset + CDCB_HEADER_OFFSET,
			       sizeof(*BlockHeader));
		if (BlockHeader->BoardMask == 0xffff) {
			offset = mCdcbBlockEntry[index].Offset;
			*size = BlockHeader->BlockSize;
		} else if (BlockHeader->BoardMask & mBoardMask) {
			offset = mCdcbBlockEntry[index].Offset;
			*size = BlockHeader->BlockSize;
			break;
		}
	}
	if (!offset) {
		*buf = NULL;
		return 0xffffffff;
	}
	if (CdcbLoadBuffer(buf, offset + CDCB_HEADER_OFFSET, *size))
		*buf = NULL;
	if (*size) {
		checksum = 0;
		for (i = 0; i < *size; i++)
			checksum += (*buf)[i];
		if (checksum == 0xff)
			return 0;
	}
	log_print(0x80000000, "Error: CDCB Block GUID 0x%04x checksum failed!\n", guid);
	drv_ddr_assert(FILE_CODE, 211);
	return 0xffffffff;
}

/*
 * CdcbDriverInit: check the image, load the block table and work out the
 * board mask.  Board ID: from GPIO straps (method 1, block 0x1000: up to
 * eight GPIOs, 0xff unused, when its type byte is 0) or from the quick
 * configuration (method 2, with the board revision in bits 15:8); a board
 * ID read from GPIOs is stored in the quick configuration the first time
 * (and the image written back), and a later different one only noted in
 * BoardIdChanged.  The board ID map (block 0x1009) gives the board mask;
 * without one the mask is 1 << board ID.
 */
uint32_t CdcbDriverInit(void)
{
	MEM_CONFIG_BDID_GET_METHOD *MemCfgBdIdGetMethod = NULL;
	MEM_CONFIG_BLOCK_BOARDID_MAP *MemCfgBlkBoardIdMap;
	BDID_GET_METHOD_GPIO_S *BdidGpio;
	MEM_CONFIG_HEADER *GlobalHeader;
	MEM_QUICK_CONFIG *QuickConfig;
	uint32_t size, BoardId, i, count, status, PcbId;
	uint8_t errorcode, checksum, bit, GpioIndex;

	postcode(0x9110);
	QuickConfig = CdcbGetQuickConfig();
	SetMemLogFilter(QuickConfig->LogFilter);
	if (QuickConfig->BootEnv == 1)
		return 0;
	if (CdcbGetGlobalHeader(&GlobalHeader)) {
		errorcode = 1;
		goto fail;
	}
	if (GlobalHeader->Signature != CDCB_SIGNATURE) {
		errorcode = 2;
		goto fail;
	}
	if (GlobalHeader->HeaderSize == 0) {
		errorcode = 3;
		goto fail;
	}
	checksum = 0;
	for (i = 0; i < GlobalHeader->HeaderSize; i++)
		checksum += ((uint8_t *)GlobalHeader)[i];
	if (checksum != 0xff) {
		errorcode = 3;
		goto fail;
	}
	if (GlobalHeader->MajorVer != CDCB_MAJOR_VER) {
		log_print(0x8000, "Error: CDCB ver %d.%d unsupported!\n\tExpected version: %d.%d\n",
			  GlobalHeader->MajorVer, GlobalHeader->MinorVer, CDCB_MAJOR_VER, 0);
		errorcode = 4;
		goto fail;
	}
	mCdcbBlockCount = GlobalHeader->BlockCount;
	status = CdcbLoadBuffer((uint8_t **)&mCdcbBlockEntry, CDCB_BLOCKTB_OFFSET, mCdcbBlockCount * 4);
	if (status) {
		mCdcbBlockEntry = NULL;
		errorcode = 5;
		goto fail;
	}

	if (QuickConfig->BoardIdGettingMethod == 0 || QuickConfig->BoardIdGettingMethod == 0xff) {
		mBoardMask = 0;
		return status;
	}
	if (QuickConfig->BoardIdChanged != 0xff || QuickConfig->BoardRevIdChanged != 0xff) {
		log_print(0, "Board ID changed, please reset the system and clear flash to update board ID.\n");
		BoardId = QuickConfig->BoardId | QuickConfig->BoardRevId << 8;
	} else if (QuickConfig->BoardIdGettingMethod == 1) {
		/* CIX does not check that the block is there */
		CdcbLocateBlockBuf(GUID_BDID_GET_METHOD, &size, (uint8_t **)&MemCfgBdIdGetMethod);
		BoardId = 0;
		if (MemCfgBdIdGetMethod->GetMethod == 0) {
			BdidGpio = (BDID_GET_METHOD_GPIO_S *)MemCfgBdIdGetMethod->buf;
			for (bit = 0; bit < 8; bit++) {
				GpioIndex = BdidGpio->GpioIndex[bit];
				if (GpioIndex != 0xff)
					BoardId |= get_soc_gpio(GpioIndex) << bit;
			}
		}
		if (QuickConfig->BoardId != 0xff) {
			if (QuickConfig->BoardId != BoardId) {
				log_print(0, "Board ID changed, please reset the system and clear flash to "
					  "update board ID.\n");
				QuickConfig->BoardIdChanged = BoardId;
				BoardId = QuickConfig->BoardId;
				set_memcfg_img(mCdcbBuf, 0, sizeof(MEM_QUICK_CONFIG));
			}
		} else {
			QuickConfig->BoardId = BoardId;
			set_memcfg_img(mCdcbBuf, 0, sizeof(MEM_QUICK_CONFIG));
		}
	} else if (QuickConfig->BoardIdGettingMethod == 2) {
		BoardId = QuickConfig->BoardId | QuickConfig->BoardRevId << 8;
	} else {
		BoardId = 0;
	}

	if (CdcbLocateBlockBuf(GUID_BOARDID_MAP, &size, (uint8_t **)&MemCfgBlkBoardIdMap)) {
		mBoardMask = 1 << BoardId;
		return status;
	}
	/* a match: the map entry's PCB ID and the board ID agree outside its mask */
	count = (size - sizeof(MEM_CONFIG_BLOCK_HEADER)) / sizeof(BOARD_ID_MAP_ENTRY);
	for (i = 0; i < count; i++) {
		PcbId = MemCfgBlkBoardIdMap->BoardIdMap[i].PcbId;
		if ((PcbId | MemCfgBlkBoardIdMap->BoardIdMap[i].Mask) ==
		    (MemCfgBlkBoardIdMap->BoardIdMap[i].Mask | BoardId)) {
			mBoardMask = 1 << MemCfgBlkBoardIdMap->BoardIdMap[i].BoardId;
			return status;
		}
	}
	/* no match: the board mask stays as it was */
	log_print(0x8000, "\tError: BoardId 0x%x not found in mapping table. Use default config\n", BoardId);
	return status;

fail:
	log_print(0x80000000, "\tError: Fail to init mem cfg buffer. errorcode %d\n", errorcode);
	drv_ddr_assert(FILE_CODE, 286);
	return 0xffffffff;
}
