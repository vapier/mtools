#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "file.h"

/*
 * Copy the data from source to target
 */

int copyfile(Stream_t *Source, Stream_t *Target)
{
	char buffer[8*16384];
	int pos;
	int ret, retw;
	size_t len;

	if (!Source){
		fprintf(stderr,"Couldn't open source file\n");
		return -1;
	}

	if (!Target){
		fprintf(stderr,"Couldn't open target file\n");
		return -1;
	}

	pos = 0;
	GET_DATA(Source, 0, &len, 0, 0);
	while(pos < len){
		ret = READS(Source, buffer, pos, 8*16384);
		if (ret < 0 ){
			perror("file read");
			return -1;
		}
		if(got_signal)
			return -1;
		if (ret == 0)
			break;
		if ((retw = force_write(Target, buffer, pos, ret)) != ret){
			if(retw < 0 )
				perror("write in copy");
			else
				fprintf(stderr,
					"Short write %d instead of %d\n", retw,
					ret);
			if(errno == ENOSPC)
				got_signal = 1;
			return ret;
		}
		pos += ret;
	}
	return pos;
}
