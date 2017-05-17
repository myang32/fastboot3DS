#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "mem_map.h"
#include "arm9/main.h"
#include "arm9/ncsd.h"
#include "arm9/sdmmc.h"
#include "arm9/spiflash.h"
#include "arm9/crypto.h"
#include "arm9/ndma.h"
#include "arm9/timer.h"
#include "util.h"
#include "arm9/dev.h"
#include "arm9/partitions.h"


// SD card device
bool sdmmc_sd_init(void);
bool sdmmc_sd_read_sector(u32 sector, u32 count, void *buf);
bool sdmmc_sd_write_sector(u32 sector, u32 count, const void *buf);
bool sdmmc_sd_close(void);
bool sdmmc_sd_is_active(void);
u32  sdmmc_sd_get_sector_count(void);

static dev_struct dev_sd = {
	"sd",
	false,
	sdmmc_sd_init,
	sdmmc_sd_read_sector,
	sdmmc_sd_write_sector,
	sdmmc_sd_close,
	sdmmc_sd_is_active,
	sdmmc_sd_get_sector_count
};
const dev_struct *dev_sdcard = &dev_sd;


// Raw NAND device
bool sdmmc_rnand_init(void);
bool sdmmc_rnand_read_sector(u32 sector, u32 count, void *buf);
bool sdmmc_rnand_write_sector(u32 sector, u32 count, const void *buf);
bool sdmmc_rnand_close(void);
bool sdmmc_rnand_is_active(void);
u32  sdmmc_rnand_get_sector_count(void);

static dev_struct dev_rnand = {
	"rnand",
	false,
	sdmmc_rnand_init,
	sdmmc_rnand_read_sector,
	sdmmc_rnand_write_sector,
	sdmmc_rnand_close,
	sdmmc_rnand_is_active,
	sdmmc_rnand_get_sector_count
};
const dev_struct *dev_rawnand = &dev_rnand;


// Decrypted NAND device
typedef struct {
	dev_struct dev;
	u32 twlCounter[4];
	u32 ctrCounter[4];
	AES_ctx twlAesCtx;
	AES_ctx ctrAesCtx;
} dev_dnand_struct;

bool sdmmc_dnand_init(void);
bool sdmmc_dnand_read_sector(u32 sector, u32 count, void *buf);
bool sdmmc_dnand_write_sector(u32 sector, u32 count, const void *buf);
bool sdmmc_dnand_close(void);
bool sdmmc_dnand_is_active(void);

// gcc throws a bullshit warning about missing braces here.
// Seems to be https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119
static dev_dnand_struct dev_dnand = {
	{
		"dnand",
		false,
		sdmmc_dnand_init,
		sdmmc_dnand_read_sector,
		sdmmc_dnand_write_sector,
		sdmmc_dnand_close,
		sdmmc_dnand_is_active,
		NULL
	},
	{0},
	{0},
	{0},
	{0}
};
const dev_struct *dev_decnand = &dev_dnand.dev;


// wifi flash device
bool wififlash_init(void);
bool wififlash_read_sector(u32 sector, u32 count, void *buf);
bool wififlash_close(void);
bool wififlash_is_active(void);
u32  wififlash_get_sector_count(void);

dev_struct dev_wififlash = {
	"nvram",
	false,
	wififlash_init,
	wififlash_read_sector,
	NULL,
	wififlash_close,
	wififlash_is_active,
	wififlash_get_sector_count
};
const dev_struct *dev_flash = &dev_wififlash;


// -------------------------------- sd card glue functions --------------------------------
bool sdmmc_sd_init(void)
{
	//printf("sdmmc_sd_init\n");
	//if(!dev_rnand.initialized && !dev_sd.initialized && !dev_dnand.dev.initialized)
	sdmmc_init();
	dev_rnand.initialized = dev_sd.initialized = dev_dnand.dev.initialized = false;
	
	if(!dev_sd.initialized)
	{
		// thanks yellows8
		*((vu16*)0x10000020) = (*((vu16*)0x10000020) & ~0x1u) | 0x200u;

		if(!sdmmc_sd_is_active())
		{
			TIMER_sleep(300); // Wait until the SD bus times out
			if(!sdmmc_sd_is_active()) return false; // Check again if a SD card is inserted
		}

		if(SD_Init()) return false;
		dev_sd.initialized = true;
		//printf("sd init: success!\n");
	}
	//else printf("sd init: nothing to do!\n");
	return true;
}

bool sdmmc_sd_read_sector(u32 sector, u32 count, void *buf)
{
	if(!dev_sd.initialized)
		return false;
	return !sdmmc_sdcard_readsectors(sector, count, buf);
}

bool sdmmc_sd_write_sector(u32 sector, u32 count, const void *buf)
{
	if(!dev_sd.initialized)
		return false;
	return !sdmmc_sdcard_writesectors(sector, count, buf);
}

bool sdmmc_sd_close(void)
{
	dev_sd.initialized = false;
	return true;
}

bool sdmmc_sd_is_active(void)
{
	return (sdmmc_read16(REG_SDSTATUS0) & TMIO_STAT0_SIGSTATE);
}

u32 sdmmc_sd_get_sector_count(void)
{
	return getMMCDevice(1)->total_size;
}


// -------------------------------- raw nand glue functions --------------------------------
bool sdmmc_rnand_init(void)
{
	if(!dev_rnand.initialized && !dev_sd.initialized && !dev_dnand.dev.initialized)
		sdmmc_init();
	
	if(!dev_rnand.initialized && !dev_dnand.dev.initialized) {
		if(Nand_Init()) return false;
		dev_rnand.initialized = true;
	}
	return true;
}

bool sdmmc_rnand_read_sector(u32 sector, u32 count, void *buf)
{
	if(!dev_rnand.initialized && !sdmmc_rnand_init())
		return false;
	return !sdmmc_nand_readsectors(sector, count, buf);
}

bool sdmmc_rnand_write_sector(u32 sector, u32 count, const void *buf)
{
	if(!dev_rnand.initialized && !sdmmc_rnand_init())
		return false;
	return !sdmmc_nand_writesectors(sector, count, buf);
}

bool sdmmc_rnand_close(void)
{
	dev_rnand.initialized = false;
	return true;
}

bool sdmmc_rnand_is_active(void)
{
	return dev_rnand.initialized;
}

u32 sdmmc_rnand_get_sector_count(void)
{
	return getMMCDevice(0)->total_size;
}


// ------------------------------ decrypted nand glue functions ------------------------------
bool sdmmc_dnand_init(void)
{
	NCSD_header header;
	size_t temp;
	extern u32 ctr_nand_sector;

	if(!dev_rnand.initialized && !dev_sd.initialized && !dev_dnand.dev.initialized)
		sdmmc_init();

	if(!dev_dnand.dev.initialized)
	{
		if(!dev_rnand.initialized)
		{
			Nand_Init();
			dev_rnand.initialized = true;
		}

		// Read NCSD header
		if(sdmmc_nand_readsectors(0, 1, (void*)&header)) return false;

		// Check "NCSD" magic
		if(header.magic != 0x4453434E) return false;

		// Collect partition infos...
		for(int i = 0; i < MAX_PARTITIONS; i++)
		{
			u8 type = header.partFsType[i];
			size_t sector = header.partitions[i].mediaOffset;
			
			size_t index = partitionAdd(sector, header.partitions[i].mediaSize, type);

			switch(type)
			{
				case 1:
					if(i == 0)
					{
						partitionSetKeyslot(index, 0x03); // TWL NAND partition
						partitionSetName(index, "twln");
					}
					else if(i == 4)	// CTR NAND partition
					{
						if(bootInfo.unit_is_new3ds)
							partitionSetKeyslot(index, 0x05); // TODO: Load N3DS keyY
						else
							partitionSetKeyslot(index, 0x04);
						
						partitionSetName(index, "nand");
						
						// Set CTR NAND partition offset for diskio.c
						ctr_nand_sector = sector;
					}
					break;
				case 3: // firmX
					/* NOTE: This assumes there's not more than two firmware partitions! */
					partitionSetKeyslot(index, 0x06);
					if(partitionGetIndex("firm0", &temp))
						partitionSetName(index, "firm1");
					else
						partitionSetName(index, "firm0");
					break;
				case 4: // AGB_FIRM savegame
					partitionSetKeyslot(index, 0x07);
					partitionSetName(index, "agb");
					break;
				default: // Unused
					partitionSetKeyslot(index, 0xFF);
					partitionSetName(index, "invalid");
			}
		}

		// Hash NAND CID to create the CTRs for crypto
		u32 hash[8];
		sha((u32*)0x01FFCD84, 16, hash, SHA_INPUT_BIG | SHA_MODE_1, SHA_OUTPUT_BIG);
		memcpy(dev_dnand.twlCounter, hash, 16);
		sha((u32*)0x01FFCD84, 16, hash, SHA_INPUT_BIG | SHA_MODE_256, SHA_OUTPUT_LITTLE);
		memcpy(dev_dnand.ctrCounter, hash, 16);

		// TWL keyslot 0x03 keyX
		u32 twlKeyX[4];
		twlKeyX[0] = (*((u32*)0x01FFB808) ^ 0xB358A6AF) | 0x80000000;
		twlKeyX[1] = 0x544E494E; // "NINT"
		twlKeyX[2] = 0x4F444E45; // "ENDO"
		twlKeyX[3] = *((u32*)0x01FFB80C) ^ 0x08C267B7;
		AES_setKey(AES_INPUT_LITTLE | AES_INPUT_REVERSED, 3, AES_KEY_TYPE_X, twlKeyX, false, false);

		// TWL keyslot 0x03 keyY
		u32 twlKeyY[4];
		for(int i = 0; i < 3; i++) twlKeyY[i] = ((u32*)0x01FFD3C8)[i];
		twlKeyY[3] = 0xE1A00005;
		AES_setKey(AES_INPUT_LITTLE | AES_INPUT_REVERSED, 3, AES_KEY_TYPE_Y, twlKeyY, false, true);

		// Crypt settings
		AES_setCryptParams(&dev_dnand.twlAesCtx, AES_OUTPUT_LITTLE | AES_INPUT_LITTLE | AES_OUTPUT_REVERSED |
		                   AES_INPUT_REVERSED | AES_MODE_CTR);
		AES_setCryptParams(&dev_dnand.ctrAesCtx, AES_OUTPUT_BIG | AES_INPUT_BIG | AES_OUTPUT_NORMAL |
		                   AES_INPUT_NORMAL | AES_MODE_CTR);

		dev_dnand.dev.initialized = true;
	}

	return true;
}

bool sdmmc_dnand_read_sector(u32 sector, u32 count, void *buf)
{
	size_t index;
	u8 keyslot;
	
	if(!dev_dnand.dev.initialized && !sdmmc_dnand_init())
		return false;

	if(!partitionFind(sector, count, &index))
		return false;
		
	partitionGetKeyslot(index, &keyslot);

	if(keyslot == 0xFF)
		return false;	// unknown partition type

	AES_ctx *ctx;
	AES_selectKeyslot(keyslot, true);
	if(keyslot == 0x03)
	{
		ctx = &dev_dnand.twlAesCtx;
		AES_setCtrIvNonce(ctx, dev_dnand.twlCounter, AES_INPUT_LITTLE | AES_INPUT_REVERSED | AES_MODE_CTR, sector<<9);
	}
	else
	{
		ctx = &dev_dnand.ctrAesCtx;
		AES_setCtrIvNonce(ctx, dev_dnand.ctrCounter, AES_INPUT_LITTLE | AES_INPUT_NORMAL | AES_MODE_CTR, sector<<9);
	}
	
	if(sdmmc_nand_readsectors(sector, count, buf)) return false;
	AES_crypt(ctx, buf, buf, count<<9);

	return true;
}

bool sdmmc_dnand_write_sector(u32 sector, u32 count, const void *buf)
{	
	size_t index;
	u8 keyslot;
	
	if(!dev_dnand.dev.initialized && !sdmmc_dnand_init())
		return false;

	if(!partitionFind(sector, count, &index))
		return false;
		
	partitionGetKeyslot(index, &keyslot);

	if(keyslot == 0xFF)
		return false;	// unknown partition type

	AES_ctx *ctx;
	ctx = &dev_dnand.ctrAesCtx;
	
	if(!count)
		return false;
	
	const size_t crypto_buf_size = min(count<<9, 0x1000);
	void *crypto_buf = malloc(crypto_buf_size);
	if(!crypto_buf)
		return false;
	
	AES_selectKeyslot(keyslot, true);
	AES_setCtrIvNonce(ctx, dev_dnand.ctrCounter, AES_INPUT_LITTLE | AES_INPUT_NORMAL | AES_MODE_CTR, sector<<9);
	
	do {
		size_t crypt_size = min(count<<9, crypto_buf_size);
		
		AES_crypt(ctx, buf, crypto_buf, crypt_size);
		if(sdmmc_nand_writesectors(sector, crypt_size >> 9, crypto_buf))
		{
			free(crypto_buf);
			return false;
		}
		
		sector += crypt_size >> 9;
		count -= crypt_size >> 9;
		buf += crypt_size;
	} while(count);
	
	free(crypto_buf);
	
	return true;
}

bool sdmmc_dnand_close(void)
{
	dev_dnand.dev.initialized = false;
	return true;
}

bool sdmmc_dnand_is_active(void)
{
	return sdmmc_rnand_is_active();
}


// ------------------------------ wifi flash glue functions ------------------------------

bool wififlash_init(void)
{
	if(dev_wififlash.initialized) return true;
	if(!spiflash_get_status()) return false;
	dev_wififlash.initialized = true;
	return true;
}

bool wififlash_read_sector(u32 sector, u32 count, void *buf)
{
	if(!dev_wififlash.initialized) return false;
	spiflash_read(sector<<9, count<<9, buf);
	return true;
}

bool wififlash_close(void)
{
	// nothing to do here..?
	dev_wififlash.initialized = false;
	return true;
}

bool wififlash_is_active(void)
{
	if(dev_wififlash.initialized) return true;
	return wififlash_init();
}

u32 wififlash_get_sector_count(void)
{
	return 0x20000>>9;
}
