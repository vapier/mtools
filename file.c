#include "sysincludes.h"
#include "msdos.h"
#include "stream.h"
#include "mtools.h"
#include "fsP.h"
#include "file.h"
#include "htable.h"
#include "dirCache.h"

typedef struct File_t {
	Class_t *Class;
	int refs;
	struct Fs_t *Fs;	/* Filesystem that this fat file belongs to */
	Stream_t *Buffer;

	int (*map)(struct File_t *this, off_t where, size_t *len, int mode);
	size_t FileSize;

	size_t preallocatedSize;
	int preallocatedClusters;

	/* Absolute position of first cluster of file */
	unsigned int FirstAbsCluNr;

	/* Absolute position of previous cluster */
	unsigned int PreviousAbsCluNr;

	/* Relative position of previous cluster */
	unsigned int PreviousRelCluNr;
	struct directory dir;
	direntry_t direntry;
	int hint;
	struct dirCache_t *dcp;
} File_t;

static Class_t FileClass;
T_HashTable *filehash;

static File_t *getUnbufferedFile(Stream_t *Stream)
{
	while(Stream->Class != &FileClass)
		Stream = Stream->Next;
	return (File_t *) Stream;
}

struct dirCache_t **getDirCacheP(Stream_t *Stream)
{
	return &getUnbufferedFile(Stream)->dcp;
}

direntry_t *getDirentry(Stream_t *Stream)
{
	return &getUnbufferedFile(Stream)->direntry;
}


static int recalcPreallocSize(File_t *This)
{
	size_t currentClusters, neededClusters;
	int clus_size;
	int neededPrealloc;
	Fs_t *Fs = This->Fs;
	int r;

	if(This->FileSize & 0xc0000000) {
		fprintf(stderr, "Bad filesize\n");
	}
	if(This->preallocatedSize & 0xc0000000) {
		fprintf(stderr, "Bad preallocated size %x\n", 
			This->preallocatedSize);
	}

	clus_size = Fs->cluster_size * Fs->sector_size;

	currentClusters = (This->FileSize + clus_size - 1) / clus_size;
	neededClusters = (This->preallocatedSize + clus_size - 1) / clus_size;
	neededPrealloc = neededClusters - currentClusters;
	if(neededPrealloc < 0)
		neededPrealloc = 0;
	r = fsPreallocate(Fs, neededPrealloc - This->preallocatedClusters);
	if(r)
		return r;
	This->preallocatedClusters = neededPrealloc;
	return 0;
}

void printFat(Stream_t *Stream)
{
	File_t *This = getUnbufferedFile(Stream);
	unsigned long n;

	n = This->FirstAbsCluNr;
	if(!n) {
		printf("Root directory\n");
		return;
	}

	do {
		printf("<%lu> ",n);
		n = fatDecode(This->Fs, n);
	} while (n <= This->Fs->last_fat && n != 1);
}

static int normal_map(File_t *This, off_t where, size_t *len, int mode)
{
	int offset;
	off_t end;
	int NrClu; /* number of clusters to read */
	unsigned int RelCluNr;
	unsigned int CurCluNr;
	unsigned int NewCluNr;
	unsigned int AbsCluNr;
	int clus_size;
	Fs_t *Fs = This->Fs;

	clus_size = Fs->cluster_size * Fs->sector_size;
	offset = where % clus_size;

	if (mode == MT_READ)
		maximize(*len, This->FileSize - where);
	if (*len == 0 )
		return 0;

	if (This->FirstAbsCluNr < 2){
		if( mode == MT_READ || *len == 0){
			*len = 0;
			return 0;
		}
		NewCluNr = get_next_free_cluster(This->Fs, 1);
		if (NewCluNr == 1 ){
			errno = ENOSPC;
			return -2;
		}
		hash_remove(filehash, (void *) This, This->hint);
		This->FirstAbsCluNr = NewCluNr;
		hash_add(filehash, (void *) This, &This->hint);
		fatAllocate(This->Fs, NewCluNr, Fs->end_fat);
	}

	RelCluNr = where / clus_size;
	
	if (RelCluNr >= This->PreviousRelCluNr){
		CurCluNr = This->PreviousRelCluNr;
		AbsCluNr = This->PreviousAbsCluNr;
	} else {
		CurCluNr = 0;
		AbsCluNr = This->FirstAbsCluNr;
	}


	NrClu = (offset + *len - 1) / clus_size;
	while (CurCluNr <= RelCluNr + NrClu){
		if (CurCluNr == RelCluNr){
			/* we have reached the beginning of our zone. Save
			 * coordinates */
			This->PreviousRelCluNr = RelCluNr;
			This->PreviousAbsCluNr = AbsCluNr;
		}
		NewCluNr = fatDecode(This->Fs, AbsCluNr);
		if (NewCluNr == 1 || NewCluNr == 0){
			fprintf(stderr,"Fat problem while decoding %d %x\n", 
				AbsCluNr, NewCluNr);
			exit(1);
		}
		if(CurCluNr == RelCluNr + NrClu)			
			break;
		if (NewCluNr > Fs->last_fat && mode == MT_WRITE){
			/* if at end, and writing, extend it */
			NewCluNr = get_next_free_cluster(This->Fs, AbsCluNr);
			if (NewCluNr == 1 ){ /* no more space */
				errno = ENOSPC;
				return -2;
			}
			fatAppend(This->Fs, AbsCluNr, NewCluNr);
		}

		if (CurCluNr < RelCluNr && NewCluNr > Fs->last_fat){
			*len = 0;
			return 0;
		}

		if (CurCluNr >= RelCluNr && NewCluNr != AbsCluNr + 1)
			break;
		CurCluNr++;
		AbsCluNr = NewCluNr;
	}

	maximize(*len, (1 + CurCluNr - RelCluNr) * clus_size - offset);
	
	end = where + *len;
	if(batchmode && mode == MT_WRITE && end >= This->FileSize) {
		*len += ROUND_UP(end, clus_size) - end;
	}

	if((*len + offset) / clus_size + This->PreviousAbsCluNr-2 >
		Fs->num_clus) {
		fprintf(stderr, "cluster too big\n");
		exit(1);
	}

	return ((This->PreviousAbsCluNr-2) * Fs->cluster_size+Fs->clus_start) *
			Fs->sector_size + offset;
}


static int root_map(File_t *This, off_t where, size_t *len, int mode)
{
	Fs_t *Fs = This->Fs;

	if(Fs->dir_len * Fs->sector_size < where) {
		*len = 0;
		errno = ENOSPC;
		return -2;
	}

	maximize(*len, Fs->dir_len * Fs->sector_size - where);
	return Fs->dir_start * Fs->sector_size + where;
}
	

static int read_file(Stream_t *Stream, char *buf, off_t where, size_t len)
{
	DeclareThis(File_t);
	int pos;

	Stream_t *Disk = This->Fs->Next;
	
	pos = This->map(This, where, &len, MT_READ);
	if(pos < 0)
		return pos;
	return READS(Disk, buf, pos, len);
}

static int write_file(Stream_t *Stream, char *buf, off_t where, size_t len)
{
	DeclareThis(File_t);
	int pos;
	int ret;
	size_t requestedLen;
	Stream_t *Disk = This->Fs->Next;

	requestedLen = len;
	pos = This->map(This, where, &len, MT_WRITE);
	if( pos < 0)
		return pos;
	if(batchmode)
		ret = force_write(Disk, buf, pos, len);
	else
		ret = WRITES(Disk, buf, pos, len);
	if(ret > requestedLen)
		ret = requestedLen;
	if ( ret > 0 && where + ret > This->FileSize )
		This->FileSize = where + ret;
	recalcPreallocSize(This);
	return ret;
}


/*
 * Convert an MSDOS time & date stamp to the Unix time() format
 */

static int month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
static inline long conv_stamp(struct directory *dir)
{
	struct tm *tmbuf;
	long tzone, dst;
	time_t accum;

	accum = DOS_YEAR(dir) - 1970; /* years past */

	/* days passed */
	accum = accum * 365L + month[DOS_MONTH(dir)-1] + DOS_DAY(dir);

	/* leap years */
	accum += (DOS_YEAR(dir) - 1972) / 4L;

	/* back off 1 day if before 29 Feb */
	if (!(DOS_YEAR(dir) % 4) && DOS_MONTH(dir) < 3)
	        accum--;
	accum = accum * 24L + DOS_HOUR(dir); /* hours passed */
	accum = accum * 60L + DOS_MINUTE(dir); /* minutes passed */
	accum = accum * 60L + DOS_SEC(dir); /* seconds passed */

	/* correct for Time Zone */
#ifdef HAVE_GETTIMEOFDAY
	{
		struct timeval tv;
		struct timezone tz;
		
		gettimeofday(&tv, &tz);
		tzone = tz.tz_minuteswest * 60L;
	}
#else
#ifdef HAVE_TZSET
	{
#ifndef OS_ultrix
		/* Ultrix defines this to be a different type */
		extern long timezone;
#endif
		tzset();
		tzone = (long) timezone;
	}
#else
	tzone = 0;
#endif /* HAVE_TZSET */
#endif /* HAVE_GETTIMEOFDAY */
	
	accum += tzone;

	/* correct for Daylight Saving Time */
	tmbuf = localtime(&accum);
	dst = (tmbuf->tm_isdst) ? (-60L * 60L) : 0L;
	accum += dst;
	
	return accum;
}


static int get_file_data(Stream_t *Stream, long *date, size_t *size,
			 int *type, int *address)
{
	DeclareThis(File_t);

	if(date)
		*date = conv_stamp(& This->dir);
	if(size)
		*size = This->FileSize;
	if(type)
		*type = This->dir.attr & 0x10;
	if(address)
		*address = This->FirstAbsCluNr;
	return 0;
}


static int free_file(Stream_t *Stream)
{
	DeclareThis(File_t);
	Fs_t *Fs = This->Fs;
	fsPreallocate(Fs, -This->preallocatedClusters);       
	FREE(&This->direntry.Dir);
	freeDirCache(Stream);
	return hash_remove(filehash, (void *) Stream, This->hint);
}


static int flush_file(Stream_t *Stream)
{
	DeclareThis(File_t);
	direntry_t *entry = &This->direntry;

	if(isRootDir(Stream)) {
		return 0;
	}

	if(This->FirstAbsCluNr != getStart(entry->Dir, &entry->dir)) {
		set_word(entry->dir.start, This->FirstAbsCluNr & 0xffff);
		set_word(entry->dir.startHi, This->FirstAbsCluNr >> 16);
		dir_write(entry);
	}
	return 0;
}


static int pre_allocate_file(Stream_t *Stream, size_t size)
{
	DeclareThis(File_t);

	if(size > This->FileSize &&
	   size > This->preallocatedSize) {
		This->preallocatedSize = size;
		return recalcPreallocSize(This);
	} else
		return 0;
}

static Class_t FileClass = {
	read_file, 
	write_file, 
	flush_file, /* flush */
	free_file, /* free */
	0, /* get_geom */
	get_file_data,
	pre_allocate_file
};

static unsigned int getAbsCluNr(File_t *This)
{
	if(This->FirstAbsCluNr)
		return This->FirstAbsCluNr;
	if(isRootDir((Stream_t *) This))
		return 0;
	return 1;
}

static unsigned int func1(void *Stream)
{
	DeclareThis(File_t);

	return getAbsCluNr(This) ^ (long) This->Fs;
}

static unsigned int func2(void *Stream)
{
	DeclareThis(File_t);

	return getAbsCluNr(This);
}

static int comp(void *Stream, void *Stream2)
{
	DeclareThis(File_t);

	File_t *This2 = (File_t *) Stream2;

	return This->Fs != This2->Fs ||
		getAbsCluNr(This) != getAbsCluNr(This2);
}

static void init_hash(void)
{
	static int is_initialised=0;
	
	if(!is_initialised){
		make_ht(func1, func2, comp, 20, &filehash);
		is_initialised = 1;
	}
}


static Stream_t *_internalFileOpen(Stream_t *Dir, unsigned int first, 
				   size_t size, direntry_t *entry)
{
	Stream_t *Stream = GetFs(Dir);
	DeclareThis(Fs_t);
	File_t Pattern;
	File_t *File;

	init_hash();
	This->refs++;

	if(first != 1){
		/* we use the illegal cluster 1 to mark newly created files.
		 * do not manage those by hashtable */
		Pattern.Fs = This;
		Pattern.Class = &FileClass;
		if(first)
			Pattern.map = normal_map;
		else
			Pattern.map = root_map;
		Pattern.FirstAbsCluNr = first;
		if(!hash_lookup(filehash, (T_HashTableEl) &Pattern, 
				(T_HashTableEl **)&File, 0)){
			File->refs++;
			This->refs--;
			return (Stream_t *) File;
		}
	}

	File = New(File_t);
	if (!File)
		return NULL;
	File->dcp = 0;
	File->preallocatedClusters = 0;
	File->preallocatedSize = 0;
	/* memorize dir for date and attrib */
	File->direntry = *entry;
	if(entry->entry == -3)
		File->direntry.Dir = (Stream_t *) File; /* root directory */
	else
		COPY(File->direntry.Dir);

	File->Class = &FileClass;
	File->Fs = This;
	if(first)
		File->map = normal_map;
	else
		File->map = root_map; /* FAT 12/16 root directory */
	if(first == 1)
		File->FirstAbsCluNr = 0;
	else
		File->FirstAbsCluNr = first;

	File->PreviousRelCluNr = 0xffff;
	File->FileSize = size;
	File->refs = 1;
	File->Buffer = 0;
	hash_add(filehash, (void *) File, &File->hint);
	return (Stream_t *) File;
}

Stream_t *OpenRoot(Stream_t *Dir)
{
	unsigned int num;
	direntry_t entry;
	size_t size;
	Stream_t *file;

	num = fat32RootCluster(Dir);

	/* make the directory entry */
	entry.entry = -3;
	entry.name[0] = '\0';
	mk_entry("/", 0x10, num, 0, 0, &entry.dir);

	if(num)
		size = countBytes(Dir, num);
	else {
		Fs_t *Fs = (Fs_t *) GetFs(Dir);
		size = Fs->dir_len * Fs->sector_size;
	}
	file = _internalFileOpen(Dir, num, size, &entry);
	bufferize(&file);
	return file;
}


Stream_t *OpenFileByDirentry(direntry_t *entry)
{
	Stream_t *file;
	unsigned int first;
	size_t size;

	first = getStart(entry->Dir, &entry->dir);

	if(!first && (entry->dir.attr & 0x10))
		return OpenRoot(entry->Dir);
	if (entry->dir.attr & 0x10 )
		size = countBytes(entry->Dir, first);
	else 
		size = FILE_SIZE(&entry->dir);
	file = _internalFileOpen(entry->Dir, first, size, entry);
	if(entry->dir.attr & 0x10) {
		bufferize(&file);
		if(first == 1)
			dir_grow(file, 0);
	}

	return file;
}


int isRootDir(Stream_t *Stream)
{
	File_t *This = getUnbufferedFile(Stream);

	return This->map == root_map;
}
