/*
 * mmd.c
 * Makes an MSDOS directory
 */


#define LOWERCASE

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "vfat.h"
#include "mainloop.h"
#include "plain_io.h"
#include "nameclash.h"
#include "file.h"
#include "fs.h"

/*
 * Preserve the file modification times after the fclose()
 */

typedef struct Arg_t {
	char *target;
	int nowarn;
	int interactive;
	int verbose;
	int silent;
	MainParam_t mp;

	Stream_t *SrcDir;
	int entry;
	ClashHandling_t ch;
	Stream_t *targetDir;
} Arg_t;


typedef struct CreateArg_t {
	Stream_t *Dir;
	Stream_t *NewDir;
	unsigned char attr;
	time_t mtime;
} CreateArg_t;

/*
 * Open the named file for read, create the cluster chain, return the
 * directory structure or NULL on error.
 */
int makeit(char *dosname,
	    char *longname,
	    void *arg0,
	    direntry_t *targetEntry)
{
	Stream_t *Target;
	CreateArg_t *arg = (CreateArg_t *) arg0;
	int fat;
	direntry_t subEntry;	

	/* will it fit? At least one sector must be free */
	if (!getfreeMin(targetEntry->Dir, 512))
		return -1;
	
	mk_entry(dosname, 0x10, 1, 0, arg->mtime, &targetEntry->dir);
	Target = OpenFileByDirentry(targetEntry);
	if(!Target){
		fprintf(stderr,"Could not open Target\n");
		return -1;
	}

	/* this allocates the first cluster for our directory */

	initializeDirentry(&subEntry, Target);

	subEntry.entry = 1;
	GET_DATA(targetEntry->Dir, 0, 0, 0, &fat);
	mk_entry("..         ", 0x10, fat, 0, arg->mtime, &subEntry.dir);
	dir_write(&subEntry);

	subEntry.entry = 0;
	GET_DATA(Target, 0, 0, 0, &fat);
	mk_entry(".          ", 0x10, fat, 0, arg->mtime, &subEntry.dir);
	dir_write(&subEntry);

	mk_entry(dosname, 0x10 | arg->attr, fat, 0, arg->mtime, 
		 &targetEntry->dir);
	arg->NewDir = Target;
	return 0;
}


static void usage(void)
{
	fprintf(stderr,
		"Mtools version %s, dated %s\n", mversion, mdate);
	fprintf(stderr,
		"Usage: %s [-itnmvV] file targetfile\n", progname);
	fprintf(stderr,
		"       %s [-itnmvV] file [files...] target_directory\n", 
		progname);
	exit(1);
}

Stream_t *createDir(Stream_t *Dir, const char *filename, ClashHandling_t *ch, 
		    unsigned char attr, time_t mtime)
{
	CreateArg_t arg;
	int ret;

	arg.Dir = Dir;
	arg.attr = attr;
	arg.mtime = mtime;

	if (!getfreeMin(Dir, 1))
		return NULL;

	ret = mwrite_one(Dir, filename,0, makeit, &arg, ch);
	if(ret < 1)
		return NULL;
	else
		return arg.NewDir;
}

static int createDirCallback(direntry_t *entry, MainParam_t *mp)
{
	Stream_t *ret;
	time_t now;

	ret = createDir(mp->File, mp->targetName, 
			&((Arg_t *)(mp->arg))->ch, 0x10, 
			getTimeNow(&now));
	if(ret == NULL)
		return ERROR_ONE;
	else {
		FREE(&ret);
		return GOT_ONE;
	}
	
}

void mmd(int argc, char **argv, int type)
{
	Arg_t arg;
	int c;

	/* get command line options */

	init_clash_handling(& arg.ch);

	/* get command line options */
	arg.nowarn = 0;
	arg.interactive = 0;
	arg.silent = 0;
	while ((c = getopt(argc, argv, "XinvorsamORSAM")) != EOF) {
		switch (c) {
			case 'i':
				arg.interactive = 1;
				break;
			case 'n':
				arg.nowarn = 1;
				break;
			case 'v':
				arg.verbose = 1;
				break;
			case 'X':
				arg.silent = 1;
				break;
			case '?':
				usage();
			default:
				if(handle_clash_options(&arg.ch, c))
					usage();
				break;
		}
	}

	if (argc - optind < 1)
		usage();

	init_mp(&arg.mp);
	arg.mp.arg = (void *) &arg;
	arg.mp.openflags = O_RDWR;
	arg.mp.callback = createDirCallback;
	arg.mp.lookupflags = OPEN_PARENT | DO_OPEN_DIRS;
	exit(main_loop(&arg.mp, argv + optind, argc - optind));
}
