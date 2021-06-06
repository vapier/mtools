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
 * filter to support filesystems stored at an offset into their image
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "offset.h"

typedef struct Offset_t {
	struct Class_t *Class;
	int refs;
	struct Stream_t *Next;
	struct Stream_t *Buffer;

	mt_off_t offset;
} Offset_t;

static ssize_t offset_read(Stream_t *Stream, char *buf,
			  mt_off_t start, size_t len)
{
	DeclareThis(Offset_t);	
	return READS(This->Next, buf, start+This->offset, len);
}

static ssize_t offset_write(Stream_t *Stream, char *buf,
			   mt_off_t start, size_t len)
{
	DeclareThis(Offset_t);	
	return WRITES(This->Next, buf, start+This->offset, len);
}

static Class_t OffsetClass = {
	offset_read,
	offset_write,
	0, /* flush */
	0, /* free */
	set_geom_pass_through, /* set_geom */
	0, /* get_data */
	0, /* pre-allocate */
	get_dosConvert_pass_through, /* dos convert */
	0, /* discard */
};

Stream_t *OpenOffset(Stream_t *Next, struct device *dev, off_t offset,
		     char *errmsg, mt_size_t *maxSize) {
	Offset_t *This;

	This = New(Offset_t);
	if (!This){
		printOom();
		return 0;
	}
	memset((void*)This, 0, sizeof(Offset_t));
	This->Class = &OffsetClass;
	This->refs = 1;
	This->Next = Next;

	This->offset = offset;

	if(maxSize) {
		if(This->offset > (mt_off_t) *maxSize) {
			if(errmsg)
				sprintf(errmsg,"init: Big disks not supported");
			goto exit_0;
		}
		
		*maxSize -= (mt_size_t) This->offset;
	}

	if(dev->tot_sectors) {
		mt_off_t offs_sectors = This->offset /
			(dev->sector_size ? dev->sector_size : 512);
		if(dev->tot_sectors < offs_sectors) {
			if(errmsg)
				sprintf(errmsg,"init: Offset bigger than base image");
			goto exit_0;
		}
		dev->tot_sectors -= offs_sectors;
	}
		
	return (Stream_t *) This;
 exit_0:
	Free(This);
	return NULL;
}