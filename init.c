/*  Copyright 1986-1992 Emmet P. Gray.
 *  Copyright 1996-2002,2006-2009 Alain Knaff.
 *  This file is part of mtools.
 *
 *  Mtools is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Mtools is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Mtools.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Initialize an MSDOS diskette.  Read the boot sector, and switch to the
 * proper floppy disk device to match the format on the disk.  Sets a bunch
 * of global variables.  Returns 0 on success, or 1 on failure.
 */

#include "sysincludes.h"
#include "msdos.h"
#include "stream.h"
#include "mtools.h"
#include "fsP.h"
#include "buffer.h"
#include "file_name.h"
#include "open_image.h"

#define FULL_CYL

/*
 * Read the boot sector.  We glean the disk parameters from this sector.
 */
static int read_boot(Stream_t *Stream, union bootsector * boot, size_t size)
{
	size_t boot_sector_size; /* sector size, as stored in boot sector */

	/* read the first sector, or part of it */
	if(!size)
		size = BOOTSIZE;
	if(size > MAX_BOOT)
		size = MAX_BOOT;

	if (force_read(Stream, boot->characters, 0, size) != (ssize_t) size)
		return -1;

	boot_sector_size = WORD(secsiz);		
	if(boot_sector_size < sizeof(boot->bytes)) {
		/* zero rest of in-memory boot sector */
		memset(boot->bytes+boot_sector_size, 0,
		       sizeof(boot->bytes) - boot_sector_size);
	}

	return 0;
}

static int fs_flush(Stream_t *Stream)
{
	DeclareThis(Fs_t);

	fat_write(This);
	return 0;
}

static doscp_t *get_dosConvert(Stream_t *Stream)
{
  DeclareThis(Fs_t);
  return This->cp;
}

Class_t FsClass = {
	read_pass_through, /* read */
	write_pass_through, /* write */
	fs_flush,
	fs_free, /* free */
	0, /* set geometry */
	get_data_pass_through,
	0, /* pre allocate */
	get_dosConvert, /* dosconvert */
	0 /* discard */
};

/**
 * Get media type byte from boot sector (BIOS Parameter Block 2) or
 * from FAT (if media byte from BPB 2 looks fishy)
 * Return the media byte + 0x100 if found in BPB 2, or as is if found in FAT.
 */
static int get_media_type(Stream_t *St, union bootsector *boot)
{
	int media;

	media = boot->boot.descr;
	if(media < 0xf0){
		char temp[512];
		/* old DOS disk. Media descriptor in the first FAT byte */
		/* we assume 512-byte sectors here */
		if (force_read(St,temp,(mt_off_t) 512,512) == 512)
			media = (unsigned char) temp[0];
		else
			media = 0;
	} else
		media += 0x100;
	return media;
}


Stream_t *GetFs(Stream_t *Fs)
{
	while(Fs && Fs->Class != &FsClass)
		Fs = Fs->Next;
	return Fs;
}

static void boot_to_geom(struct device *dev, int media,
			 union bootsector *boot) {
	uint32_t tot_sectors;
	int BootP, Infp0, InfpX, InfTm;
	int j;
	unsigned char sum;
	uint16_t sect_per_track;
	struct label_blk_t *labelBlock;

	dev->ssize = 2; /* allow for init_geom to change it */
	dev->use_2m = 0x80; /* disable 2m mode to begin */

	if(media == 0xf0 || media >= 0x100){		
		dev->heads = WORD(nheads);
		dev->sectors = WORD(nsect);
		tot_sectors = DWORD(bigsect);
		SET_INT(tot_sectors, WORD(psect));
		sect_per_track = dev->heads * dev->sectors;
		if(sect_per_track == 0) {
		    if(mtools_skip_check) {
			/* add some fake values if sect_per_track is
			 * zero. Indeed, some atari disks lack the
			 * geometry values (i.e. have zeroes in their
			 * place). In order to avoid division by zero
			 * errors later on, plug 1 everywhere
			 */
			dev->heads = 1;
			dev->sectors = 1;
			sect_per_track = 1;
		    } else {
			fprintf(stderr, "The devil is in the details: zero number of heads or sectors\n");
			exit(1);
		    }
		}
		dev->tracks = tot_sectors / sect_per_track;
		if(tot_sectors % sect_per_track)
			/* round size up */
			dev->tracks++;
		
		BootP = WORD(ext.old.BootP);
		Infp0 = WORD(ext.old.Infp0);
		InfpX = WORD(ext.old.InfpX);
		InfTm = WORD(ext.old.InfTm);
		
		if(WORD(fatlen)) {
			labelBlock = &boot->boot.ext.old.labelBlock;
		} else {
			labelBlock = &boot->boot.ext.fat32.labelBlock;
		}

		if (boot->boot.descr >= 0xf0 &&
		    has_BPB4 &&
		    strncmp( boot->boot.banner,"2M", 2 ) == 0 &&
		    BootP < 512 && Infp0 < 512 && InfpX < 512 && InfTm < 512 &&
		    BootP >= InfTm + 2 && InfTm >= InfpX && InfpX >= Infp0 && 
		    Infp0 >= 76 ){
			for (sum=0, j=63; j < BootP; j++) 
				sum += boot->bytes[j];/* checksum */
			dev->ssize = boot->bytes[InfTm];
			if (!sum && dev->ssize <= 7){
				dev->use_2m = 0xff;
				dev->ssize |= 0x80; /* is set */
			}
		}
		dev->sector_size = WORD(secsiz);
	} else
		if(setDeviceFromOldDos(media, dev) < 0)
			exit(1);
}

/**
 * Tries out one device definition for the given drive number
 * Parameters
 *  - dev: device definition to try
 *  - mode: file open mode
 *  - out_dev: device parameters (geometry, etc.) are returned here
 *  - boot: boot sector is read from the disk into this structure
 *  - name: "name" of device definition (returned)
 *  - media: media byte is returned here (ored with 0x100 if there is a
 *    BIOS Parameter block present)
 *  - maxSize: maximal size supported by (physical) drive returned here
 *  - try_writable: whether to try opening it writable from the get-go,
 *     even if not specified as writable in mode (used for mlabel)
 *  - isRop: whether device is read-only is returned here
 * Return value:
 *  - a Stream allowing to read from this device, must be closed by caller
 *
 * If a geometry change is needed, drive is re-opened RW, as geometry
 * change ioctl needs write access. However, in such case, the lock
 * acquired is still only a read lock.
 */
static Stream_t *try_device(struct device *dev,
			    int mode, struct device *out_dev,
			    union bootsector *boot,
			    char *name, int *media, mt_size_t *maxSize,
			    int *isRop, int try_writable,
			    char *errmsg)
{
	int retry_write;
	int have_read_bootsector=0;
	int modeFlags = mode & ~O_ACCMODE;
	int openMode;
	int lockMode;

	*out_dev = *dev;
	expand(dev->name,name);
#ifdef USING_NEW_VOLD
	strcpy(name, getVoldName(dev, name));
#endif

	if(try_writable) {
		/* Caller asks up to try first read-write, and only fall back
		 * if not feasible */
		openMode = O_RDWR | modeFlags;
	} else {
		openMode = mode;
	}
	lockMode = openMode;

	for(retry_write=0; retry_write<2; retry_write++) {
		Stream_t *Stream;
		int r;
		int geomFailure=0;

		if(retry_write)
			mode |= O_RDWR;

		Stream = OpenImage(out_dev, dev, name, openMode, errmsg,
				   0, lockMode,
				   maxSize, &geomFailure, 0, NULL);
		if(Stream == NULL) {
			if(geomFailure && (mode & O_ACCMODE) == O_RDONLY) {
				/* Our first attempt was to open read-only,
				   but this resulted in failure setting the 
				   geometry */
				openMode = modeFlags | O_RDWR;
				continue;
			}

			if(try_writable &&
			   (errno == EPERM ||
			    errno == EACCES ||
			    errno == EROFS)) {
				/* Our first attempt was to open
				 * read-write, but this resulted in a
				 * read-protection problem */
				lockMode = openMode = modeFlags | O_RDONLY;
				continue;
			}
			return NULL;
		}
		if(!have_read_bootsector) {
			/* read the boot sector */
			if ((r=read_boot(Stream, boot, out_dev->blocksize)) < 0){
				sprintf(errmsg,
					"init %c: could not read boot sector",
					dev->drive);
				FREE(&Stream);
				return NULL;
			}

			if((*media= get_media_type(Stream, boot)) <= 0xf0 ){
				if (boot->boot.jump[2]=='L')
					sprintf(errmsg,
						"diskette %c: is Linux LILO, not DOS",
						dev->drive);
				else
					sprintf(errmsg,"init %c: non DOS media", dev->drive);
				FREE(&Stream);
				return NULL;
			}
			have_read_bootsector=1;
		}

		/* set new parameters, if needed */
		errno = 0;
		boot_to_geom(out_dev, *media, boot);
		if(SET_GEOM(Stream, out_dev, dev)){
			if(errno == EBADF || errno == EPERM) {
				/* Retry with write */
				FREE(&Stream);
				openMode = modeFlags | O_RDWR;
				continue;
			}
			if(errno)
#ifdef HAVE_SNPRINTF
				snprintf(errmsg, 199,
					 "Can't set disk parameters for %c: %s",
					 dev->drive, strerror(errno));
#else
			sprintf(errmsg,
				"Can't set disk parameters for %c: %s",
				drive, strerror(errno));
#endif
			else
				sprintf(errmsg,
					"Can't set disk parameters for %c",
					dev->drive);
			FREE(&Stream);
			return NULL;
		}
		if(isRop) {
			*isRop = (openMode & O_ACCMODE) == O_RDONLY;
		}
		return Stream;
	}
	return NULL;
}


/**
 * Tries out all device definitions for the given drive letter, until one
 * is found that is able to read from the device
 * Parameters
 *  - drive: drive letter to check
 *  - mode: file open mode
 *  - out_dev: device parameters (geometry, etc.) are returned here
 *  - boot: boot sector is read from the disk into this structure
 *  - name: "name" of device definition (returned)
 *  - media: media byte is returned here (ored with 0x100 if there is a
 *    BIOS Parameter block present)
 *  - maxSize: maximal size supported by (physical) drive returned here
 *  - isRop: whether device is read-only is returned here
 * Return value:
 *  - a Stream allowing to read from this device, must be closed by caller
 */
Stream_t *find_device(char drive, int mode, struct device *out_dev,
		      union bootsector *boot,
		      char *name, int *media, mt_size_t *maxSize,
		      int *isRop)
{
	char errmsg[200];
	struct device *dev;
	
	sprintf(errmsg, "Drive '%c:' not supported", drive);
					/* open the device */
	for (dev=devices; dev->name; dev++) {
		Stream_t *Stream;
		int isRo;
		isRo=0;
		if (dev->drive != drive)
			continue;

		Stream = try_device(dev, mode, out_dev,
				    boot,
				    name, media, maxSize,
				    &isRo, isRop != NULL,
				    errmsg);
		if(Stream) {
			if(isRop)
				*isRop = isRo;
			return Stream;
		}
	}

	/* print error msg if needed */
	fprintf(stderr,"%s\n",errmsg);
	return NULL;
}


Stream_t *fs_init(char drive, int mode, int *isRop)
{
	uint32_t blocksize;
	int media;
	size_t disk_size = 0;	/* In case we don't happen to set this below */
	uint32_t tot_sectors;
	char name[EXPAND_BUF];
	unsigned int cylinder_size;
	struct device dev;
	mt_size_t maxSize;

	union bootsector boot;

	Fs_t *This;

	This = New(Fs_t);
	if (!This)
		return NULL;

	This->Direct = NULL;
	This->Next = NULL;
	This->refs = 1;
	This->Buffer = 0;
	This->Class = &FsClass;
	This->preallocatedClusters = 0;
	This->lastFatSectorNr = 0;
	This->lastFatAccessMode = 0;
	This->lastFatSectorData = 0;
	This->drive = drive;
	This->last = 0;

	This->Direct = find_device(drive, mode, &dev, &boot, name, &media,
				   &maxSize, isRop);
	if(!This->Direct)
		return NULL;

	cylinder_size = dev.heads * dev.sectors;
	This->serialized = 0;
	if ((media & ~7) == 0xf8){
		/* This bit of code is only entered if there is no BPB, or
		 * else result of the AND would be 0x1xx
		 */
		struct OldDos_t *params=getOldDosByMedia(media);
		if(params == NULL)
			return NULL;
		This->cluster_size = params->cluster_size;
		tot_sectors = cylinder_size * params->tracks;
		This->fat_start = 1;
		This->fat_len = params->fat_len;
		This->dir_len = params->dir_len;
		This->num_fat = 2;
		This->sector_size = 512;
		This->sectorShift = 9;
		This->sectorMask = 511;
		This->fat_bits = 12;
	} else {
		struct label_blk_t *labelBlock;
		unsigned int i;
		
		This->sector_size = WORD_S(secsiz);
		if(This->sector_size > MAX_SECTOR){
			fprintf(stderr,"init %c: sector size too big\n", drive);
			return NULL;
		}

		i = log_2(This->sector_size);

		if(i == 24) {
			fprintf(stderr,
				"init %c: sector size (%d) not a small power of two\n",
				drive, This->sector_size);
			return NULL;
		}
		This->sectorShift = i;
		This->sectorMask = This->sector_size - 1;

		/*
		 * all numbers are in sectors, except num_clus
		 * (which is in clusters)
		 */
		tot_sectors = WORD_S(psect);
		if(!tot_sectors)
			tot_sectors = DWORD_S(bigsect);	

		This->cluster_size = boot.boot.clsiz;
		This->fat_start = WORD_S(nrsvsect);
		This->fat_len = WORD_S(fatlen);
		This->dir_len = WORD_S(dirents) * MDIR_SIZE / This->sector_size;
		This->num_fat = boot.boot.nfat;

		if (This->fat_len) {
			labelBlock = &boot.boot.ext.old.labelBlock;
		} else {
			labelBlock = &boot.boot.ext.fat32.labelBlock;
		}

		if(has_BPB4) {
			This->serialized = 1;
			This->serial_number = _DWORD(labelBlock->serial);
		}
	}

	if (tot_sectors >= (maxSize >> This->sectorShift)) {
		fprintf(stderr, "Big disks not supported on this architecture\n");
		exit(1);
	}

	/* full cylinder buffering */
#ifdef FULL_CYL
	disk_size = (dev.tracks) ? cylinder_size : 512;
#else /* FULL_CYL */
	disk_size = (dev.tracks) ? dev.sectors : 512;
#endif /* FULL_CYL */

#if (defined OS_sysv4 && !defined OS_solaris)
	/*
	 * The driver in Dell's SVR4 v2.01 is unreliable with large writes.
	 */
        disk_size = 0;
#endif /* (defined sysv4 && !defined(solaris)) */

#ifdef OS_linux
	disk_size = cylinder_size;
#endif

#if 1
	if(disk_size > 256) {
		disk_size = dev.sectors;
		if(dev.sectors % 2)
			disk_size <<= 1;
	}
#endif
	if (disk_size % 2)
		disk_size *= 2;

	if(!dev.blocksize || dev.blocksize < This->sector_size)
		blocksize = This->sector_size;
	else
		blocksize = dev.blocksize;
	if (disk_size)
		This->Next = buf_init(This->Direct,
				      8 * disk_size * blocksize,
				      disk_size * blocksize,
				      This->sector_size);
	else
		This->Next = This->Direct;

	if (This->Next == NULL) {
		perror("init: allocate buffer");
		This->Next = This->Direct;
	}

	/* read the FAT sectors */
	if(fat_read(This, &boot, tot_sectors, dev.use_2m&0x7f)){
		This->num_fat = 1;
		FREE(&This->Next);
		Free(This->Next);
		return NULL;
	}

	/* Set the codepage */
	This->cp = cp_open(dev.codepage);
	if(This->cp == NULL) {
		fs_free((Stream_t *)This);
		FREE(&This->Next);
		Free(This->Next);
		return NULL;
	}

	return (Stream_t *) This;
}

char getDrive(Stream_t *Stream)
{
	DeclareThis(Fs_t);

	if(This->Class != &FsClass)
		return getDrive(GetFs(Stream));
	else
		return This->drive;
}

/*
 * Upper layer asks to pre-allocated more additional clusters
 * Parameters:
 *   size: new additional clusters to pre-allocate
 * Return:
 *   0  if pre-allocation was granted
 *  -1  if not enough clusters could be found
 */
int fsPreallocateClusters(Fs_t *Fs, uint32_t size)
{
	if(size > 0 && getfreeMinClusters((Stream_t *)Fs, size) != 1)
		return -1;

	Fs->preallocatedClusters += size;
	return 0;
}

/*
 * Upper layer wants to release some clusters that it had
 * pre-allocated before Usually done because they have now been really
 * allocated, and thus pre-allocation needs to be released to prevent
 * counting them twice.
 * Parameters:
 *   size: new additional clusters to pre-allocate
 */
void fsReleasePreallocateClusters(Fs_t *Fs, uint32_t size)
{
	Fs->preallocatedClusters -= size;
}
