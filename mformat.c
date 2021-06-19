/*  Copyright 1986-1992 Emmet P. Gray.
 *  Copyright 1994,1996-2009 Alain Knaff.
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
 * mformat.c
 */

#define DONT_NEED_WAIT

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "mainloop.h"
#include "fsP.h"
#include "file.h"
#include "plain_io.h"
#include "nameclash.h"
#include "buffer.h"
#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif
#include "stream.h"
#include "partition.h"
#include "open_image.h"
#include "file_name.h"
#include "lba.h"

#ifndef abs
#define abs(x) ((x)>0?(x):-(x))
#endif

#ifdef OS_linux
#include "linux/hdreg.h"
#include "linux/fs.h"

#endif

/**
 * Narrow down quantity of sectors to 32bit quantity, and bail out if
 * it doesn't fit in 32 bits
 */
static uint32_t mt_off_t_to_sectors(mt_off_t raw_sect) {
	/* Number of sectors must fit into 32bit value */
	if (raw_sect > UINT32_MAX) {
		fprintf(stderr, "Too many sectors for FAT %08x%08x\n",
			(uint32_t)(raw_sect>>32), (uint32_t)raw_sect);
		exit(1);
	}
	return (uint32_t) raw_sect;
}


static uint16_t init_geometry_boot(union bootsector *boot, struct device *dev,
				   uint8_t sectors0,
				   uint8_t rate_0, uint8_t rate_any,
				   uint32_t *tot_sectors, int keepBoot)
{
	int nb_renum;
	int sector2;
	int sum;

	set_word(boot->boot.nsect, dev->sectors);
	set_word(boot->boot.nheads, dev->heads);

#ifdef HAVE_ASSERT_H
	assert(*tot_sectors != 0);
#endif

	if (*tot_sectors <= UINT16_MAX && dev->hidden <= UINT16_MAX){
		set_word(boot->boot.psect, (uint16_t) *tot_sectors);
		set_dword(boot->boot.bigsect, 0);
		set_word(boot->boot.nhs, (uint16_t) dev->hidden);
	} else if(*tot_sectors <= UINT32_MAX){
		set_word(boot->boot.psect, 0);
		set_dword(boot->boot.bigsect, (uint32_t) *tot_sectors);
		set_dword(boot->boot.nhs, dev->hidden);
	} else {
		fprintf(stderr, "Too many sectors %u\n", *tot_sectors);
		exit(1);
	}

	if (dev->use_2m & 0x7f){
		uint16_t bootOffset;
		uint8_t j;
		uint8_t size2;
		uint16_t i;
		strncpy(boot->boot.banner, "2M-STV04", 8);
		boot->boot.ext.old.res_2m = 0;
		boot->boot.ext.old.fmt_2mf = 6;
		if ( dev->sectors % ( ((1 << dev->ssize) + 3) >> 2 ))
			boot->boot.ext.old.wt = 1;
		else
			boot->boot.ext.old.wt = 0;
		boot->boot.ext.old.rate_0= rate_0;
		boot->boot.ext.old.rate_any= rate_any;
		if (boot->boot.ext.old.rate_any== 2 )
			boot->boot.ext.old.rate_any= 1;
		i=76;

		/* Infp0 */
		set_word(boot->boot.ext.old.Infp0, i);
		boot->bytes[i++] = sectors0;
		boot->bytes[i++] = 108;
		for(j=1; j<= sectors0; j++)
			boot->bytes[i++] = j;

		set_word(boot->boot.ext.old.InfpX, i);

		boot->bytes[i++] = 64;
		boot->bytes[i++] = 3;
		nb_renum = i++;
		sector2 = dev->sectors;
		size2 = dev->ssize;
		j=1;
		while( sector2 ){
			while ( sector2 < (1 << size2) >> 2 )
				size2--;
			boot->bytes[i++] = 128 + j;
			boot->bytes[i++] = j++;
			boot->bytes[i++] = size2;
			sector2 -= (1 << size2) >> 2;
		}
		boot->bytes[nb_renum] = (uint8_t)(( i - nb_renum - 1 )/3);

		set_word(boot->boot.ext.old.InfTm, i);

		sector2 = dev->sectors;
		size2= dev->ssize;
		while(sector2){
			while ( sector2 < 1 << ( size2 - 2) )
				size2--;
			boot->bytes[i++] = size2;
			sector2 -= 1 << (size2 - 2 );
		}

		set_word(boot->boot.ext.old.BootP,i);
		bootOffset = i;

		/* checksum */
		for (sum=0, j=64; j<i; j++)
			sum += boot->bytes[j];/* checksum */
		boot->boot.ext.old.CheckSum=(unsigned char)-sum;
		return bootOffset;
	} else {
		if(!keepBoot) {
			boot->boot.jump[0] = 0xeb;
			boot->boot.jump[1] = 0;
			boot->boot.jump[2] = 0x90;
			strncpy(boot->boot.banner, mformat_banner, 8);
			/* It looks like some versions of DOS are
			 * rather picky about this, and assume default
			 * parameters without this, ignoring any
			 * indication about cluster size et al. */
		}
		return 0;
	}
}


static int comp_fat_bits(Fs_t *Fs, int estimate,
			 unsigned long tot_sectors, int fat32)
{
	int needed_fat_bits;

	needed_fat_bits = 12;

#define MAX_DISK_SIZE(bits,clusters) \
	TOTAL_DISK_SIZE((bits), Fs->sector_size, (clusters), \
			Fs->num_fat, MAX_BYTES_PER_CLUSTER/Fs->sector_size)

	if(tot_sectors > MAX_DISK_SIZE(12u, FAT12-1))
		needed_fat_bits = 16;
	if(fat32 || tot_sectors > MAX_DISK_SIZE(16u, FAT16-1))
		needed_fat_bits = 32;

#undef MAX_DISK_SIZE

	if(abs(estimate) && abs(estimate) < needed_fat_bits) {
		if(fat32) {
			fprintf(stderr,
				"Contradiction between FAT size on command line and FAT size in conf file\n");
			exit(1);
		}
		fprintf(stderr,
			"Device too big for a %d bit FAT\n",
			estimate);
		exit(1);
	}

	if(!estimate) {
		unsigned int min_fat16_size;

		if(needed_fat_bits > 12)
			return needed_fat_bits;
		min_fat16_size = DISK_SIZE(16, Fs->sector_size, FAT12,
					   Fs->num_fat, 1);
		if(tot_sectors < min_fat16_size)
			return 12;
 		else if(Fs->cluster_size == 0 &&
			tot_sectors >= 2* min_fat16_size)
 			return 16; /* heuristics */
 	}

 	return estimate;
}


/*
 * According to Microsoft "Hardware White Paper", "Microsoft
 * Extensible Formware Initiative", "FAT32 File System Specification",
 * Version 1.03, December 6, 2000:
 * If (CountofClusters < 4085) { // 0x0ff5
 *  // Volume is FAT12
 * } else if (CountofClusters < 65525) { // 0xfff5
 *  // Volume is FAT16
 * } else {
 *  //Volume is FAT32
 * }
 *
 * This document can be found at the following URL:
 * https://staff.washington.edu/dittrich/misc/fatgen103.pdf
 * The relevant passus is on page 15.
 *
 * Actually, experimentations with Windows NT 4 show that the
 * cutoff is 4087 rather than 4085... This is Microsoft after all.
 * Not sure what the other Microsoft OS'es do though...
 */
static void calc_fat_bits2(Fs_t *Fs, unsigned long tot_sectors, int fat_bits,
			   int may_change_cluster_size,
			   int may_change_root_size)
{
	unsigned long rem_sect;

	/*
	 * the "remaining sectors" after directory and boot
	 * hasve been accounted for.
	 */
	rem_sect = tot_sectors - Fs->dir_len - Fs->fat_start;
	switch(abs(fat_bits)) {
		case 0:

#define MY_DISK_SIZE(bits,clusters) \
			DISK_SIZE( (bits), Fs->sector_size, (clusters), \
				   Fs->num_fat, Fs->cluster_size)

			if(rem_sect >= MY_DISK_SIZE(16, FAT12+2))
				/* big enough for FAT16
				 * We take a margin of 2, because NT4
				 * misbehaves, and starts considering a disk
				 * as FAT16 only if it is larger than 4086
				 * sectors, rather than 4084 as it should
				 */
				set_fat16(Fs);
			else if(rem_sect <= MY_DISK_SIZE(12, FAT12-1))
				 /* small enough for FAT12 */
				 set_fat12(Fs);
			else {
				/* "between two chairs",
				 * augment cluster size, and
				 * settle it */
				if(may_change_cluster_size &&
				   Fs->cluster_size * Fs->sector_size * 2
				   <= MAX_BYTES_PER_CLUSTER)
					Fs->cluster_size <<= 1;
				else if(may_change_root_size) {
					Fs->dir_len +=
						rem_sect - MY_DISK_SIZE(12, FAT12-1);
				}
				set_fat12(Fs);
			}
			break;
#undef MY_DISK_SIZE

		case 12:
			set_fat12(Fs);
			break;
		case 16:
			set_fat16(Fs);
			break;
		case 32:
			set_fat32(Fs);
			break;
	}
}

static __inline__ void format_root(Fs_t *Fs, char *label, union bootsector *boot)
{
	Stream_t *RootDir;
	char *buf;
	unsigned int i;
	struct ClashHandling_t ch;
	unsigned int dirlen;

	init_clash_handling(&ch);
	ch.name_converter = label_name_uc;
	ch.ignore_entry = -2;

	buf = safe_malloc(Fs->sector_size);
	RootDir = OpenRoot((Stream_t *)Fs);
	if(!RootDir){
		fprintf(stderr,"Could not open root directory\n");
		exit(1);
	}

	memset(buf, '\0', Fs->sector_size);

	if(Fs->fat_bits == 32) {
		/* on a FAT32 system, we only write one sector,
		 * as the directory can be extended at will...*/
		dirlen = Fs->cluster_size;
		fatAllocate(Fs, Fs->rootCluster, Fs->end_fat);
	} else
		dirlen = Fs->dir_len;
	for (i = 0; i < dirlen; i++)
		WRITES(RootDir, buf, sectorsToBytes((Stream_t*)Fs, i),
			   Fs->sector_size);

	ch.ignore_entry = 1;
	if(label[0])
		mwrite_one(RootDir,label, 0, labelit, NULL,&ch);

	FREE(&RootDir);
	if(Fs->fat_bits == 32)
		set_word(boot->boot.dirents, 0);
	else
		set_word(boot->boot.dirents,
			 (uint16_t) (Fs->dir_len * (Fs->sector_size / 32)));
	free(buf);
}


#ifdef USE_XDF
static void xdf_calc_fat_size(Fs_t *Fs, uint32_t tot_sectors,
			      int fat_bits)
{
	unsigned int rem_sect;

	rem_sect = tot_sectors - Fs->dir_len - Fs->fat_start - 2 * Fs->fat_len;

	if(Fs->fat_len) {
		/* an XDF disk, we know the fat_size and have to find
		 * out the rest. We start with a cluster size of 1 and
		 * keep doubling until everything fits into the
		 * FAT. This will occur eventually, as our FAT has a
		 * minimal size of 1 */
		for(Fs->cluster_size = 1; 1 ; Fs->cluster_size <<= 1) {
			Fs->num_clus = rem_sect / Fs->cluster_size;
			if(abs(fat_bits) == 16 || Fs->num_clus >= FAT12)
				set_fat16(Fs);
			else
				set_fat12(Fs);
			if (Fs->fat_len >= NEEDED_FAT_SIZE(Fs))
				return;
		}
	}
	fprintf(stderr,"Internal error while calculating Xdf fat size\n");
	exit(1);
}
#endif

static void calc_fat_size(Fs_t *Fs, uint32_t tot_sectors)
{
	uint32_t rem_sect;
	uint32_t real_rem_sect;
	uint32_t numerator;
	uint32_t denominator;
	uint32_t corr=0; /* correct numeric overflow */
	unsigned int fat_nybbles;
	unsigned int slack;
	int printGrowMsg=1; /* Should we print "growing FAT" messages ?*/

#ifdef DEBUG
	fprintf(stderr, "Fat start=%d\n", Fs->fat_start);
	fprintf(stderr, "tot_sectors=%lu\n", tot_sectors);
	fprintf(stderr, "dir_len=%d\n", Fs->dir_len);
#endif
	real_rem_sect = rem_sect = tot_sectors - Fs->dir_len - Fs->fat_start;

	/* Cheat a little bit to address the _really_ common case of
	   odd number of remaining sectors while both nfat and cluster size
	   are even... */
	if(rem_sect         %2 == 1 &&
	   Fs->num_fat      %2 == 0 &&
	   Fs->cluster_size %2 == 0)
		rem_sect--;

#ifdef DEBUG
	fprintf(stderr, "Rem sect=%lu\n", rem_sect);
#endif

	if(Fs->fat_bits == 0) {
		fprintf(stderr, "Weird, fat bits = 0\n");
		exit(1);
	}


	/* See fat_size_calculation.tex or
	   (http://ftp.gnu.org/software/mtools/manual/fat_size_calculation.pdf)
	   for an explantation about why the stuff below works...
	*/

	fat_nybbles = Fs->fat_bits / 4;
	numerator   = rem_sect+2*Fs->cluster_size;
	/* Might overflow, but will be cancelled out below. As the
	   operation is unsigned, a posteriori fixup is allowable, as
	   wrap-around is part of the spec. For *signed* quantities,
	   this hack would be incorrect, as it would be "undefined
	   behavior" */
	
	denominator =
	  Fs->cluster_size * Fs->sector_size * 2 +
	  Fs->num_fat * fat_nybbles;

	if(fat_nybbles == 3)
		numerator *= fat_nybbles;
	else
		/* Avoid numerical overflows, divide the denominator
		 * rather than multiplying the numerator */
		denominator = denominator / fat_nybbles;

	/* Substract denominator from numerator to "cancel out" an
	   unsigned integer overflow which might have happened with
	   total number of sectors very near maximum (2^32-1) and huge
	   cluster size. This substraction removes 1 from the result
	   of the following division, so we will add 1 again after the
	   division. However, we only do this if (original) numerator
	   is bigger than denominator though, as otherwise we risk the
	   inverse problem of going below 0 on small disks */
	if(rem_sect > denominator) {
		numerator -=  denominator;
		corr++;
	}
	
#ifdef DEBUG
	fprintf(stderr, "Numerator=%lu denominator=%lu\n",
		numerator, denominator);
#endif

	Fs->fat_len = (numerator-1)/denominator+1+corr;
	Fs->num_clus = (rem_sect-(Fs->fat_len*Fs->num_fat))/Fs->cluster_size;

	/* Apply upper bounds for FAT bits */
	if(Fs->fat_bits == 16 && Fs->num_clus >= FAT16)
		Fs->num_clus = FAT16-1;
	if(Fs->fat_bits == 12 && Fs->num_clus >= FAT12)
		Fs->num_clus = FAT12-1;

	/* A safety, if above math is correct, this should not be happen...*/
	if(Fs->num_clus > (Fs->fat_len * Fs->sector_size * 2 /
			   fat_nybbles - 2)) {
		fprintf(stderr,
			"Fat size miscalculation, shrinking num_clus from %d ",
			Fs->num_clus);
		Fs->num_clus = (Fs->fat_len * Fs->sector_size * 2 /
				fat_nybbles - 2);
		fprintf(stderr, " to %d\n", Fs->num_clus);
	}
#ifdef DEBUG
	fprintf(stderr, "Num_clus=%d fat_len=%d nybbles=%d\n",
		Fs->num_clus, Fs->fat_len, fat_nybbles);
#endif

	if ( Fs->num_clus < FAT16 && Fs->fat_bits > 16 ){
		fprintf(stderr,"Too few clusters for this fat size."
			" Please choose a 16-bit fat in your /etc/mtools.conf"
			" or .mtoolsrc file\n");
		exit(1);
	}

	/* As the number of clusters is specified nowhere in the boot sector,
	 * it will be calculated by removing everything else from total number
	 * of sectors. This means that if we reduced the number of clusters
	 * above, we will have to grow the FAT in order to take up any excess
	 * sectors... */
#ifdef HAVE_ASSERT_H
	assert(rem_sect >= Fs->num_clus * Fs->cluster_size +
	       Fs->fat_len * Fs->num_fat);
#endif
	slack = rem_sect -
		Fs->num_clus * Fs->cluster_size -
		Fs->fat_len * Fs->num_fat;
	if(slack >= Fs->cluster_size) {
		/* This can happen under two circumstances:
		   1. We had to reduce num_clus because we reached maximum
		   number of cluster for FAT12 or FAT16
		*/
		if(printGrowMsg) {
			fprintf(stderr, "Slack=%d\n", slack);
			fprintf(stderr, "Growing fat size from %d",
				Fs->fat_len);
		}
		Fs->fat_len +=
			(slack - Fs->cluster_size) / Fs->num_fat + 1;
		if(printGrowMsg) {
			fprintf(stderr,
				" to %d in order to take up excess cluster area\n",
				Fs->fat_len);
		}
		Fs->num_clus = (rem_sect-(Fs->fat_len*Fs->num_fat))/
			Fs->cluster_size;

	}

#ifdef HAVE_ASSERT_H
	/* Fat must be big enough for all clusters */
	assert( ((Fs->num_clus+2) * fat_nybbles) <=
		(Fs->fat_len*Fs->sector_size*2));

	/* num_clus must be big enough to cover rest of disk, or else further
	 * users of the filesystem will assume a bigger num_clus, which might
	 * be too big for fat_len */
	assert(Fs->num_clus ==
	       (real_rem_sect - Fs->num_fat * Fs->fat_len) / Fs->cluster_size);
#endif
}


static unsigned char bootprog[]=
{0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0xfc, 0xb9, 0x00, 0x01,
 0xbe, 0x00, 0x7c, 0xbf, 0x00, 0x80, 0xf3, 0xa5, 0xea, 0x00, 0x00,
 0x00, 0x08, 0xb8, 0x01, 0x02, 0xbb, 0x00, 0x7c, 0xba, 0x80, 0x00,
 0xb9, 0x01, 0x00, 0xcd, 0x13, 0x72, 0x05, 0xea, 0x00, 0x7c, 0x00,
 0x00, 0xcd, 0x19};

static __inline__ void inst_boot_prg(union bootsector *boot, uint16_t offset)
{
	memcpy((char *) boot->boot.jump + offset,
	       (char *) bootprog, sizeof(bootprog) /sizeof(bootprog[0]));
	if(offset - 2 < 0x80) {
	  /* short jump */
	  boot->boot.jump[0] = 0xeb;
	  boot->boot.jump[1] = (uint8_t) (offset -2);
	  boot->boot.jump[2] = 0x90;
	} else {
	  /* long jump, if offset is too large */
	  boot->boot.jump[0] = 0xe9;
	  boot->boot.jump[1] = (uint8_t) (offset - 3);
	  boot->boot.jump[2] = (uint8_t) ( (offset - 3) >> 8);
	}
	set_word(boot->boot.jump + offset + 20, offset + 24);
}

static void calc_cluster_size(struct Fs_t *Fs, uint32_t tot_sectors,
			      int fat_bits)

{
	unsigned int max_clusters; /* maximal possible number of sectors for
				   * this FAT entry length (12/16/32) */
	unsigned int max_fat_size; /* maximal size of the FAT for this FAT
				    * entry length (12/16/32) */
	unsigned int rem_sect; /* remaining sectors after we accounted for
				* the root directory and boot sector(s) */

	switch(abs(fat_bits)) {
		case 12:
			max_clusters = FAT12-1;
			max_fat_size = Fs->num_fat *
				FAT_SIZE(12, Fs->sector_size, max_clusters);
			break;
		case 16:
		case 0: /* still hesititating between 12 and 16 */
			max_clusters = FAT16-1;
			max_fat_size = Fs->num_fat *
				FAT_SIZE(16, Fs->sector_size, max_clusters);
			break;
		case 32:
			/*
			   FAT32 cluster sizes for disks with 512 block size
			   according to Microsoft specification fatgen103.doc:
			
			   32.5 MB - 260 MB   cluster_size =  1
			    260 MB -   8 GB   cluster_size =  8
			      8 GB -  16 GB   cluster_size = 16
			     16 GB -  32 GB   cluster_size = 32
			     32 GB -   2 TB   cluster_size = 64
			
			   Below calculation is generalized and does not depend
			   on 512 block size.
			 */
			Fs->cluster_size = tot_sectors > 32*1024*1024*2 ? 64 :
			                   tot_sectors > 16*1024*1024*2 ? 32 :
			                   tot_sectors >  8*1024*1024*2 ? 16 :
			                   tot_sectors >     260*1024*2 ? 8 : 1;
			return;
		default:
			fprintf(stderr,"Bad fat size\n");
			exit(1);
	}

	if(tot_sectors <= Fs->fat_start + Fs->num_fat + Fs->dir_len + 0u) {
		/* we need at least enough sectors to fit boot, fat and root
		 * dir */
		fprintf(stderr, "Not enough sectors\n");
		exit(1);
	}

	rem_sect = tot_sectors - Fs->dir_len - Fs->fat_start;

	/* double the cluster size until we can fill up the disk with
	 * the maximal number of sectors of this size */
	while(Fs->cluster_size * max_clusters  + max_fat_size < rem_sect) {
		if(Fs->cluster_size > 64) {
			/* bigger than 64. Should fit */
			fprintf(stderr,
				"Internal error while calculating cluster size\n");
			exit(1);
		}
		Fs->cluster_size <<= 1;
	}
}


static int old_dos_size_to_geom(size_t size,
				unsigned int *cyls,
				unsigned short *heads,
				unsigned short *sects)
{
	struct OldDos_t *params = getOldDosBySize(size);
	if(params != NULL) {
		*cyls = params->tracks;
		*heads = params->heads;
		*sects = params->sectors;
		return 0;
	} else
		return 1;
}


static void calc_fs_parameters(struct device *dev, uint32_t tot_sectors,
			       struct Fs_t *Fs, union bootsector *boot)
{
	struct OldDos_t *params=NULL;
	if(dev->fat_bits == 0 || abs(dev->fat_bits) == 12)
		params = getOldDosByParams(dev->tracks,dev->heads,dev->sectors,
					   Fs->dir_len, Fs->cluster_size);
	if(params != NULL) {
		boot->boot.descr = params->media;
		Fs->cluster_size = params->cluster_size;
		Fs->dir_len = params->dir_len;
		Fs->fat_len = params->fat_len;
		Fs->fat_bits = 12;
	} else {
		int may_change_cluster_size = (Fs->cluster_size == 0);
		int may_change_root_size = (Fs->dir_len == 0);

		/* a non-standard format */
		if(DWORD(nhs) || tot_sectors % (dev->sectors * dev->heads))
			boot->boot.descr = 0xf8;
		else
			boot->boot.descr = 0xf0;


		if(!Fs->cluster_size) {
			if (dev->heads == 1)
				Fs->cluster_size = 1;
			else {
				Fs->cluster_size = (tot_sectors > 2000 ) ? 1:2;
				if (dev->use_2m & 0x7f)
					Fs->cluster_size = 1;
			}
		}

		if(!Fs->dir_len) {
			if (dev->heads == 1)
				Fs->dir_len = 4;
			else
				Fs->dir_len = (tot_sectors > 2000) ? 32 : 7;
		}

		calc_cluster_size(Fs, tot_sectors, dev->fat_bits);
#ifdef USE_XDF
		if(Fs->fat_len)
			xdf_calc_fat_size(Fs, tot_sectors, dev->fat_bits);
		else
#endif
		{
			calc_fat_bits2(Fs, tot_sectors, dev->fat_bits,
				       may_change_cluster_size,
				       may_change_root_size);
			calc_fat_size(Fs, tot_sectors);
		}
	}

	set_word(boot->boot.fatlen, (uint16_t) Fs->fat_len);
}



static void calc_fs_parameters_32(uint32_t tot_sectors,
				  struct Fs_t *Fs, union bootsector *boot)
{
	unsigned long num_clus;
	if(DWORD(nhs))
		boot->boot.descr = 0xf8;
	else
		boot->boot.descr = 0xf0;

	if(!Fs->cluster_size)
		calc_cluster_size(Fs, tot_sectors, 32);
	Fs->dir_len = 0;
	num_clus = tot_sectors / Fs->cluster_size;
	/* Maximal number of clusters on FAT32 is 0xffffff6 */
	if (num_clus > 0xffffff6) {
		fprintf(stderr, "Too many clusters\n");
		exit(1);
	}
	Fs->num_clus = (unsigned int) num_clus;
	set_fat32(Fs);
	calc_fat_size(Fs, tot_sectors);
	set_word(boot->boot.fatlen, 0);
	set_dword(boot->boot.ext.fat32.bigFat, Fs->fat_len);
}




static void usage(int ret) NORETURN;
static void usage(int ret)
{
	fprintf(stderr,
		"Mtools version %s, dated %s\n", mversion, mdate);
	fprintf(stderr,
		"Usage: %s [-V] [-t tracks] [-h heads] [-n sectors] "
		"[-v label] [-1] [-4] [-8] [-f size] "
		"[-N serialnumber] "
		"[-k] [-B bootsector] [-r root_dir_len] [-L fat_len] "
		"[-F] [-I fsVersion] [-C] [-c cluster_size] "
		"[-H hidden_sectors] "
#ifdef USE_XDF
		"[-X] "
#endif
		"[-S hardsectorsize] [-M softsectorsize] [-3] "
		"[-2 track0sectors] [-0 rate0] [-A rateany] [-a]"
		"device\n", progname);
	exit(ret);
}

void mformat(int argc, char **argv, int dummy UNUSEDP) NORETURN;
void mformat(int argc, char **argv, int dummy UNUSEDP)
{
	int r; /* generic return value */
	Fs_t *Fs;
	unsigned int hs;
	int hs_set;
	unsigned int arguse_2m = 0;
	uint8_t sectors0=18; /* number of sectors on track 0 */
	int create = 0;
	uint8_t rate_0, rate_any;
	int mangled;
	uint8_t argssize=0; /* sector size */
	uint16_t msize=0;
	int fat32 = 0;
	struct label_blk_t *labelBlock;
	size_t bootOffset;

#ifdef USE_XDF
	unsigned int i;
	int format_xdf = 0;
	struct xdf_info info;
#endif
	union bootsector boot;
	char *bootSector=0;
	int c;
	int keepBoot = 0;
	struct device used_dev;
	unsigned int argtracks;
	uint16_t argheads, argsectors;
	uint32_t tot_sectors=0;
	size_t blocksize;

	char drive, name[EXPAND_BUF];

	char label[VBUFSIZE];

	dos_name_t shortlabel;
	struct device *dev;
	char errmsg[2100];

	uint32_t serial;
 	int serial_set;
	uint16_t fsVersion;
	uint8_t mediaDesc=0;
	bool haveMediaDesc=false;
	
	mt_size_t maxSize;

	int Atari = 0; /* should we add an Atari-style serial number ? */

	uint16_t backupBoot = 6;
	int backupBootSet = 0;

	uint8_t resvSects = 0;
	
	char *endptr;

	hs = hs_set = 0;
	argtracks = 0;
	argheads = 0;
	argsectors = 0;
	arguse_2m = 0;
	argssize = 0x2;
	label[0] = '\0';
	serial_set = 0;
	serial = 0;
	fsVersion = 0;

	Fs = New(Fs_t);
	if (!Fs) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
		
	Fs->cluster_size = 0;
	Fs->refs = 1;
	Fs->dir_len = 0;
	if(getenv("MTOOLS_DIR_LEN")) {
		Fs->dir_len = atou16(getenv("MTOOLS_DIR_LEN"));
	  if(Fs->dir_len <= 0)
	    Fs->dir_len=0;
	}
	Fs->fat_len = 0;
	Fs->num_fat = 2;
	if(getenv("MTOOLS_NFATS")) {
		Fs->num_fat = atou8(getenv("MTOOLS_NFATS"));
	  if(Fs->num_fat <= 0)
	    Fs->num_fat=2;
	}
	Fs->Class = &FsClass;
	rate_0 = mtools_rate_0;
	rate_any = mtools_rate_any;

	/* get command line options */
	if(helpFlag(argc, argv))
		usage(0);
	while ((c = getopt(argc,argv,
			   "i:148f:t:n:v:qub"
			   "kK:R:B:r:L:I:FCc:Xh:s:T:l:N:H:M:S:2:30:Aad:m:"))!= EOF) {
		errno = 0;
		endptr = NULL;
		switch (c) {
			case 'i':
				set_cmd_line_image(optarg);
				break;

			/* standard DOS flags */
			case '1':
				argheads = 1;
				break;
			case '4':
				argsectors = 9;
				argtracks = 40;
				break;
			case '8':
				argsectors = 8;
				argtracks = 40;
				break;
			case 'f':
				r=old_dos_size_to_geom(atoul(optarg),
						       &argtracks, &argheads,
						       &argsectors);
				if(r) {
					fprintf(stderr,
						"Bad size %s\n", optarg);
					exit(1);
				}
				break;
			case 't':
				argtracks = atou16(optarg);
				break;

			case 'T':
				tot_sectors = atoui(optarg);
				break;

			case 'n': /*non-standard*/
			case 's':
				argsectors = atou16(optarg);
				break;

			case 'l': /* non-standard */
			case 'v':
				strncpy(label, optarg, VBUFSIZE-1);
				label[VBUFSIZE-1] = '\0';
				break;

			/* flags supported by Dos but not mtools */
			case 'q':
			case 'u':
			case 'b':
			/*case 's': leave this for compatibility */
				fprintf(stderr,
					"Flag %c not supported by mtools\n",c);
				exit(1);



			/* flags added by mtools */
			case 'F':
				fat32 = 1;
				break;


			case 'S':
				argssize = atou8(optarg) | 0x80;
				if(argssize < 0x80)
					usage(1);
				if(argssize >= 0x87) {
					fprintf(stderr, "argssize must be less than 6\n");
					usage(1);
				}
				break;

#ifdef USE_XDF
			case 'X':
				format_xdf = 1;
				break;
#endif

			case '2':
				arguse_2m = 0xff;
				sectors0 = atou8(optarg);
				break;
			case '3':
				arguse_2m = 0x80;
				break;

			case '0': /* rate on track 0 */
				rate_0 = atou8(optarg);
				break;
			case 'A': /* rate on other tracks */
				rate_any = atou8(optarg);
				break;

			case 'M':
				msize = atou16(optarg);
				if(msize != 512 &&
				   msize != 1024 &&
				   msize != 2048 &&
				   msize != 4096) {
				  fprintf(stderr, "Only sector sizes of 512, 1024, 2048 or 4096 bytes are allowed\n");
				  usage(1);
				}
				break;

			case 'N':
 				serial = strtou32(optarg,&endptr,16);
 				serial_set = 1;
 				break;
			case 'a': /* Atari style serial number */
				Atari = 1;
				break;

			case 'C':
				create = O_CREAT | O_TRUNC;
				break;

			case 'H':
				hs = atoui(optarg);
				hs_set = 1;
				break;

			case 'I':
				fsVersion = strtou16(optarg,&endptr,0);
				break;

			case 'c':
				Fs->cluster_size = atoui(optarg);
				break;

			case 'r':
				Fs->dir_len = strtou16(optarg,&endptr,0);
				break;
			case 'L':
				Fs->fat_len = strtoui(optarg,&endptr,0);
				break;


			case 'B':
				bootSector = optarg;
				break;
			case 'k':
				keepBoot = 1;
				break;
			case 'K':
				backupBoot = atou16(optarg);
				backupBootSet=1;
				if(backupBoot < 2) {
				  fprintf(stderr, "Backupboot must be greater than 2\n");
				  exit(1);
				}
				break;
			case 'R':
				resvSects = atou8(optarg);
				break;
			case 'h':
				argheads = atou16(optarg);
				break;
			case 'd':
				Fs->num_fat = atou8(optarg);
				break;
			case 'm':
				mediaDesc = strtou8(optarg,&endptr,0);
				if(*endptr)
					mediaDesc = strtou8(optarg,&endptr,16);
				if(optarg == endptr || *endptr) {
				  fprintf(stderr, "Bad mediadesc %s\n", optarg);
				  exit(1);
				}
				haveMediaDesc=true;
				break;
			default:
				usage(1);
		}
		check_number_parse_errno((char)c, optarg, endptr);
	}

	if (argc - optind > 1)
		usage(1);
	if(argc - optind == 1) {
	    if(!argv[optind][0] || argv[optind][1] != ':')
		usage(1);
	    drive = ch_toupper(argv[argc -1][0]);
	} else {
	    drive = get_default_drive();
	    if(drive != ':') {
	      /* Use default drive only if it is ":" (image file), as else
		 it would be too dangerous... */
	      fprintf(stderr, "Drive letter missing\n");
	      exit(1);
	    }
	}

	if(argtracks && tot_sectors) {
		fprintf(stderr, "Only one of -t or -T may be specified\n");
		usage(1);
	}

#ifdef USE_XDF
	if(create && format_xdf) {
		fprintf(stderr,"Create and XDF can't be used together\n");
		exit(1);
	}
#endif

	/* check out a drive whose letter and parameters match */
	sprintf(errmsg, "Drive '%c:' not supported", drive);
	Fs->Direct = NULL;
	blocksize = 0;
	for(dev=devices;dev->drive;dev++) {
		FREE(&(Fs->Direct));
		/* drive letter */
		if (dev->drive != drive)
			continue;
		used_dev = *dev;

		SET_INT(used_dev.tracks, argtracks);
		SET_INT(used_dev.heads, argheads);
		SET_INT(used_dev.sectors, argsectors);
		SET_INT(used_dev.use_2m, arguse_2m);
		SET_INT(used_dev.ssize, argssize);
		if(hs_set)
			used_dev.hidden = hs;

		expand(dev->name, name);
#ifdef USING_NEW_VOLD
		strcpy(name, getVoldName(dev, name));
#endif

#ifdef USE_XDF
		if(format_xdf)
			used_dev.misc_flags |= USE_XDF_FLAG;
#endif
		if(tot_sectors)
			used_dev.tot_sectors = tot_sectors;
		
		Fs->Direct = OpenImage(&used_dev, dev, name,
				      O_RDWR|create, errmsg,
				      ALWAYS_GET_GEOMETRY,
				      O_RDWR,
				      &maxSize, NULL,
#ifdef USE_XDF
				      &info
#else
				      NULL
#endif
				      );

#ifdef USE_XDF
		if(Fs->Direct && info.FatSize) {
			if(!Fs->fat_len)
				Fs->fat_len = info.FatSize;
			if(!Fs->dir_len)
				Fs->dir_len = info.RootDirSize;
		}
#endif

		if (!Fs->Direct)
			continue;

		if(!tot_sectors)
			tot_sectors = used_dev.tot_sectors;
		
		Fs->sector_size = 512;
		if( !(used_dev.use_2m & 0x7f)) {
			Fs->sector_size = (uint16_t) (128u << (used_dev.ssize & 0x7f));
		}

		SET_INT(Fs->sector_size, msize);
		{
		    unsigned int j;
		    for(j = 0; j < 31; j++) {
			if (Fs->sector_size == (unsigned int) (1 << j)) {
			    Fs->sectorShift = j;
			    break;
			}
		    }
		    Fs->sectorMask = Fs->sector_size - 1;
		}

		if(!used_dev.blocksize || used_dev.blocksize < Fs->sector_size)
			blocksize = Fs->sector_size;
		else
			blocksize = used_dev.blocksize;

		if(blocksize > MAX_SECTOR)
			blocksize = MAX_SECTOR;

		if((mt_size_t) tot_sectors * blocksize > maxSize) {
			snprintf(errmsg, sizeof(errmsg)-1,
				 "Requested size too large\n");
			FREE(&Fs->Direct);
			continue;
		}


		/* do a "test" read */
		if (!create &&
		    READS(Fs->Direct, &boot.characters, 0, Fs->sector_size) !=
		    (signed int) Fs->sector_size) {
#ifdef HAVE_SNPRINTF
			snprintf(errmsg, sizeof(errmsg)-1,
				 "Error reading from '%s', wrong parameters?",
				 name);
#else
			sprintf(errmsg,
				"Error reading from '%s', wrong parameters?",
				name);
#endif
			FREE(&Fs->Direct);
			continue;
		}
		break;
	}

	/* print error msg if needed */
	if ( dev->drive == 0 ){
		FREE(&Fs->Direct);
		fprintf(stderr,"%s: %s\n", argv[0],errmsg);
		exit(1);
	}

	/* calculate the total number of sectors */
	if(tot_sectors == 0 &&
	   used_dev.heads && used_dev.sectors && used_dev.tracks) {
		uint32_t sect_per_track = used_dev.heads*used_dev.sectors;
		mt_off_t rtot_sectors =
			used_dev.tracks*(mt_off_t)sect_per_track;
		if(rtot_sectors > used_dev.hidden%sect_per_track)
			rtot_sectors -= used_dev.hidden%sect_per_track;
		tot_sectors = mt_off_t_to_sectors(rtot_sectors);
	}

	if(tot_sectors == 0) {
		fprintf(stderr, "Number of sectors not known\n");
		exit(1);
	}

	/* create the image file if needed */
	if (create) {
		WRITES(Fs->Direct, &boot.characters,
		       sectorsToBytes((Stream_t*)Fs, tot_sectors-1),
		       Fs->sector_size);
	}

	/* the boot sector */
	if(bootSector) {
		int fd;
		ssize_t ret;
		
		fd = open(bootSector, O_RDONLY | O_BINARY | O_LARGEFILE);
		if(fd < 0) {
			perror("open boot sector");
			exit(1);
		}
		ret=read(fd, &boot.bytes, blocksize);
		if(ret < 0 || (size_t) ret < blocksize) {
			perror("short read on boot sector");
			exit(1);
		}
		keepBoot = 1;
		close(fd);
	}
	if(!keepBoot && !(used_dev.use_2m & 0x7f))
		memset(boot.characters, '\0', Fs->sector_size);

	Fs->Next = buf_init(Fs->Direct,
			    blocksize * used_dev.heads * used_dev.sectors,
			    blocksize * used_dev.heads * used_dev.sectors,
			    blocksize);
	Fs->Buffer = 0;

	boot.boot.nfat = Fs->num_fat;
	if(!keepBoot)
		set_word(&boot.bytes[510], 0xaa55);

	/* Initialize the remaining parameters */
	set_word(boot.boot.nsect, used_dev.sectors);
	set_word(boot.boot.nheads, used_dev.heads);

	used_dev.fat_bits = comp_fat_bits(Fs,used_dev.fat_bits, tot_sectors, fat32);

	if(!keepBoot && !(used_dev.use_2m & 0x7f)) {
		if(!used_dev.partition) {
			/* install fake partition table pointing to itself */
			struct partition *partTable=(struct partition *)
				(&boot.bytes[0x1ae]);
			setBeginEnd(&partTable[1], 0,
				    used_dev.heads * used_dev.sectors *
				    used_dev.tracks,
				    (uint8_t) used_dev.heads,
				    (uint8_t) used_dev.sectors, 1, 0,
				    used_dev.fat_bits);
		}
	}

	if(used_dev.fat_bits == 32) {
		Fs->primaryFat = 0;
		Fs->writeAllFats = 1;
		if(resvSects) {
			if(resvSects < 3) {
				fprintf(stderr,
					"For FAT 32, reserved sectors need to be at least 3\n");
				resvSects = 32;
			}

			if(resvSects <= backupBoot && !backupBootSet)
				backupBoot = resvSects - 1;
			Fs->fat_start = resvSects;
		} else 
			Fs->fat_start = 32;

		if(Fs->fat_start <= backupBoot) {
			fprintf(stderr,
				"Reserved sectors (%d) must be more than backupBoot (%d)\n", Fs->fat_start, backupBoot);
			backupBoot = 6;
			Fs->fat_start = 32;
		}

		calc_fs_parameters_32(tot_sectors, Fs, &boot);

		Fs->clus_start = Fs->num_fat * Fs->fat_len + Fs->fat_start;

		/* extension flags: mirror fats, and use #0 as primary */
		set_word(boot.boot.ext.fat32.extFlags,0);

		/* fs version.  What should go here? */
		set_word(boot.boot.ext.fat32.fsVersion,fsVersion);

		/* root directory */
		set_dword(boot.boot.ext.fat32.rootCluster, Fs->rootCluster = 2);

		/* info sector */
		set_word(boot.boot.ext.fat32.infoSector, Fs->infoSectorLoc = 1);
		Fs->infoSectorLoc = 1;

		/* no backup boot sector */
		set_word(boot.boot.ext.fat32.backupBoot, backupBoot);

		labelBlock = & boot.boot.ext.fat32.labelBlock;
	} else {
		Fs->infoSectorLoc = 0;
		if(resvSects) {
			if(resvSects < 1) {
				fprintf(stderr,
					"Reserved sectors need to be at least 1\n");
				resvSects = 1;
			}
			Fs->fat_start = resvSects;
		} else 
			Fs->fat_start = 1;
		calc_fs_parameters(&used_dev, tot_sectors, Fs, &boot);
		Fs->dir_start = Fs->num_fat * Fs->fat_len + Fs->fat_start;
		Fs->clus_start = Fs->dir_start + Fs->dir_len;
		labelBlock = & boot.boot.ext.old.labelBlock;

	}

	/* Set the codepage */
	Fs->cp = cp_open(used_dev.codepage);
	if(Fs->cp == NULL)
		exit(1);

	if (!keepBoot)
		/* only zero out physdrive if we don't have a template
		 * bootsector */
		labelBlock->physdrive = 0x00;
	labelBlock->reserved = 0;
	labelBlock->dos4 = 0x29;

	if (!serial_set || Atari)
		init_random();
	if (!serial_set)
		serial=(uint32_t) random();
	set_dword(labelBlock->serial, serial);
	label_name_pc(GET_DOSCONVERT((Stream_t *)Fs),
		      label[0] ? label : "NO NAME    ", 0,
		      &mangled, &shortlabel);
	strncpy(labelBlock->label, shortlabel.base, 8);
	strncpy(labelBlock->label+8, shortlabel.ext, 3);
	sprintf(labelBlock->fat_type, "FAT%2.2d  ", Fs->fat_bits);
	labelBlock->fat_type[7] = ' ';

	set_word(boot.boot.secsiz, Fs->sector_size);
	boot.boot.clsiz = (unsigned char) Fs->cluster_size;
	set_word(boot.boot.nrsvsect, Fs->fat_start);

	bootOffset = init_geometry_boot(&boot, &used_dev, sectors0,
					rate_0, rate_any,
					&tot_sectors, keepBoot);
	if(!bootOffset) {
		bootOffset = ptrdiff((char *) labelBlock, (char*)boot.bytes) +
			sizeof(struct label_blk_t);
	}
	if(Atari) {
		boot.boot.banner[4] = 0;
		boot.boot.banner[5] = (char) random();
		boot.boot.banner[6] = (char) random();
		boot.boot.banner[7] = (char) random();
	}

	if(!keepBoot && bootOffset <= UINT16_MAX)
		inst_boot_prg(&boot, (uint16_t)bootOffset);
	/* Mimic 3.8 behavior, else 2m disk do not work (???)
	 * luferbu@fluidsignal.com (Luis Bustamante), Fri, 14 Jun 2002
	 */
	if(used_dev.use_2m & 0x7f) {
	  boot.boot.jump[0] = 0xeb;
	  boot.boot.jump[1] = 0x80;
	  boot.boot.jump[2] = 0x90;
	}
	if(used_dev.use_2m & 0x7f)
		Fs->num_fat = 1;
	if(haveMediaDesc)
		boot.boot.descr=mediaDesc;
	Fs->lastFatSectorNr = 0;
	Fs->lastFatSectorData = 0;
	zero_fat(Fs, boot.boot.descr);
	Fs->freeSpace = Fs->num_clus;
	Fs->last = 2;

#ifdef USE_XDF
	if(format_xdf)
		for(i=0;
		    i < (info.BadSectors+Fs->cluster_size-1)/Fs->cluster_size;
		    i++)
			fatEncode(Fs, i+2, 0xfff7);
#endif

	format_root(Fs, label, &boot);
	if(WRITES((Stream_t *)Fs, boot.characters,
		  (mt_off_t) 0, Fs->sector_size) < 0) {
		fprintf(stderr, "Error writing boot sector\n");
		exit(1);
	}

	if(Fs->fat_bits == 32 && WORD_S(ext.fat32.backupBoot) != MAX16) {
		if(WRITES((Stream_t *)Fs, boot.characters,
			  sectorsToBytes((Stream_t*)Fs,
					 WORD_S(ext.fat32.backupBoot)),
			  Fs->sector_size) < 0) {
			fprintf(stderr, "Error writing backup boot sector\n");
			exit(1);
		}
	}

	FREE((Stream_t **)&Fs);
#ifdef USE_XDF
	if(format_xdf && isatty(0) && !getenv("MTOOLS_USE_XDF"))
		fprintf(stderr,
			"Note:\n"
			"Remember to set the \"MTOOLS_USE_XDF\" environmental\n"
			"variable before accessing this disk\n\n"
			"Bourne shell syntax (sh, ash, bash, ksh, zsh etc):\n"
			" export MTOOLS_USE_XDF=1\n\n"
			"C shell syntax (csh and tcsh):\n"
			" setenv MTOOLS_USE_XDF 1\n" );
#endif
	exit(0);
}
