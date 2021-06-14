/*  Copyright 2021 Alain Knaff.
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
 * Buffer read/write module
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "partition.h"

typedef struct Partition_t {
	struct Class_t *Class;
	int refs;
	struct Stream_t *Next;
	struct Stream_t *Buffer;

	mt_off_t offset; /* Offset, in bytes */
	uint32_t size; /* size, in sectors */

	uint8_t pos;
	
	uint8_t sectors;
	uint8_t heads;
	uint16_t cyclinders;
} Partition_t;


static int limit_size(Partition_t *This, mt_off_t start, size_t *len)
{
	if(start > This->size)
		return -1;
	maximize(*len, (size_t) (This->size - start));
	return 0;
}

static ssize_t partition_read(Stream_t *Stream, char *buf,
			      mt_off_t start, size_t len)
{
	DeclareThis(Partition_t);
	limit_size(This, start, &len);
	return READS(This->Next, buf, start+This->offset, len);
}

static ssize_t partition_write(Stream_t *Stream, char *buf,
			       mt_off_t start, size_t len)
{
	DeclareThis(Partition_t);	
	limit_size(This, start, &len);
	return WRITES(This->Next, buf, start+This->offset, len);
}

static int partition_data(Stream_t *Stream, time_t *date, mt_size_t *size,
			  int *type, uint32_t *address)
{
	DeclareThis(Partition_t);

	if(date || type || address) {
		int ret = GET_DATA(This->Next, date, NULL, type, address);
		if(ret < 0)
			return ret;
	}
	if(size)
		*size = (size_t) This->size * 512;
	return 0;
}


static int partition_geom(Stream_t *Stream, struct device *dev, 
			  UNUSEDP struct device *orig_dev)
{
	DeclareThis(Partition_t);

	if(!dev->tot_sectors)
		dev->tot_sectors = This->size;

	return 0;
}

static Class_t PartitionClass = {
	partition_read,
	partition_write,
	0, /* flush */
	0, /* free */
	partition_geom, /* set_geom */
	partition_data, /* get_data */
	0, /* pre-allocate */
	get_dosConvert_pass_through, /* dos convert */
	0, /* discard */
};

Stream_t *OpenPartition(Stream_t *Next, struct device *dev,
			char *errmsg, mt_size_t *maxSize) {
	Partition_t *This;
	int has_activated;
	unsigned int last_end, j;
	unsigned char buf[2048];
	struct partition *partTable=(struct partition *)(buf+ 0x1ae);
	size_t partOff;
	struct partition *partition;

	if(!dev || (dev->partition > 4) || (dev->partition <= 0)) {
	    fprintf(stderr, 
		    "Invalid partition %d (must be between 1 and 4), ignoring it\n", 
		    dev->partition);
	    return NULL;
	}

	This = New(Partition_t);
	if (!This){
		printOom();
		return 0;
	}
	memset((void*)This, 0, sizeof(Partition_t));
	This->Class = &PartitionClass;
	This->refs = 1;
	This->Next = Next;

		
	/* read the first sector, or part of it */
	if (force_read(This->Next, (char*) buf, 0, 512) != 512)
		goto exit_0;
	if( _WORD(buf+510) != 0xaa55) {
		/* Not a partition table */
		if(errmsg)
			sprintf(errmsg,
				"Device does not have a BIOS partition table\n");
		goto exit_0;
	}
	partition = &partTable[dev->partition];
	if(!partition->sys_ind) {
		if(errmsg)
			sprintf(errmsg,
				"Partition %d does not exist\n",
				dev->partition);
		goto exit_0;
	}
	
	partOff = BEGIN(partition);
	if (maxSize) {
		if (partOff > *maxSize >> 9) {
			if(errmsg)
				sprintf(errmsg,"init: Big disks not supported");
			goto exit_0;
		}
		*maxSize -= (mt_size_t) partOff << 9;
		maximize(*maxSize, (mt_size_t) (PART_SIZE(partition)) << 9);
	}

	This->offset = (mt_off_t) partOff << 9;
	dev->tot_sectors = This->size = PART_SIZE(partition);

	if(!mtools_skip_check &&
	   consistencyCheck((struct partition *)(buf+0x1ae), 0, 0,
			    &has_activated, &last_end, &j, dev, 0)) {
		fprintf(stderr,
			"Warning: inconsistent partition table\n");
		fprintf(stderr,
			"Possibly unpartitioned device\n");
		fprintf(stderr,
			"\n*** Maybe try without partition=%d in "
			"device definition ***\n\n",
			dev->partition);
		fprintf(stderr,
			"If this is a PCMCIA card, or a disk "
			"partitioned on another computer, this "
			"message may be in error: add "
			"mtools_skip_check=1 to your .mtoolsrc "
			"file to suppress this warning\n");
	}
	return (Stream_t *) This;
 exit_0:
	Free(This);
	return NULL;
}

