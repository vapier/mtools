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

static ssize_t partition_read(Stream_t *Stream, char *buf,
			  mt_off_t start, size_t len)
{
	DeclareThis(Partition_t);
	return READS(This->Next, buf, start+This->offset, len);
}

static ssize_t partition_write(Stream_t *Stream, char *buf,
			       mt_off_t start, size_t len)
{
	DeclareThis(Partition_t);	
	return WRITES(This->Next, buf, start+This->offset, len);
}

static int partition_geometry(Stream_t *Stream, struct device *dev, 
			      struct device *orig_dev,
			      int media, union bootsector *boot)
{
	return 0;
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
		*size = This->size;
	return 0;
}

static Class_t PartitionClass = {
	partition_read,
	partition_write,
	0, /* flush */
	0, /* free */
	partition_geometry, /* set_geom */
	partition_data, /* get_data */
	0, /* pre-allocate */
	get_dosConvert_pass_through, /* dos convert */
	0, /* discard */
};

Stream_t *OpenPartition(Stream_t *Next, struct device *dev,
			char *errmsg, mt_size_t *maxSize) {
	Partition_t *This;

	if(dev && (dev->partition > 4)) {
	    fprintf(stderr, 
		    "Invalid partition %d (must be between 0 and 4), ignoring it\n", 
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

	while(dev && dev->partition && dev->partition <= 4) {
		int has_activated;
		unsigned int last_end, j;
		unsigned char buf[2048];
		struct partition *partTable=(struct partition *)(buf+ 0x1ae);
		size_t partOff;
		
		/* read the first sector, or part of it */
		if (force_read(This->Next, (char*) buf, 0, 512) != 512)
			break;
		if( _WORD(buf+510) != 0xaa55)
			break;

		partOff = BEGIN(partTable[dev->partition]);
		if (maxSize) {
			if (partOff > *maxSize >> 9) {
				Free(This);
				if(errmsg)
					sprintf(errmsg,"init: Big disks not supported");
				return NULL;
			}
			*maxSize -= (mt_size_t) partOff << 9;
		}
			
		This->offset = (mt_off_t) partOff << 9;
		This->size = END(partTable[dev->partition])+1-
			BEGIN(partTable[dev->partition]);
		if(!partTable[dev->partition].sys_ind) {
			if(errmsg)
				sprintf(errmsg,
					"init: non-existant partition");
			Free(This);
			return NULL;
		}

		/* CHS Info left by recent partitioning tools are
		   completely unreliable => just use standard LBA 
		   geometry */
		if(!dev->sectors)
			dev->heads = 16;
		if(!dev->sectors)
			dev->sectors = 63;

		dev->tot_sectors = PART_SIZE(partTable[dev->partition]);

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
		break;
		/* NOTREACHED */
	}
	return (Stream_t *) This;
}

