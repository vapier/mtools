/*
 * mmove.c
 * Renames/moves an MSDOS file
 *
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
	const char *fromname;
	int nowarn;
	int interactive;
	int verbose;
	MainParam_t mp;

	direntry_t *entry;
	ClashHandling_t ch;
} Arg_t;


/*
 * Open the named file for read, create the cluster chain, return the
 * directory structure or NULL on error.
 */
int renameit(char *dosname,
	     char *longname,
	     void *arg0,
	     direntry_t *targetEntry)
{
	Arg_t *arg = (Arg_t *) arg0;
	int fat;

	targetEntry->dir = arg->entry->dir;
	strncpy(targetEntry->dir.name, dosname, 8);
	strncpy(targetEntry->dir.ext, dosname + 8, 3);

	if(targetEntry->dir.attr & 0x10) {
		direntry_t *movedEntry;

		/* get old direntry. It is important that we do this
		 * on the actual direntry which is stored in the file,
		 * and not on a copy, because we will modify it, and the
		 * modification should be visible at file 
		 * de-allocation time */
		movedEntry = getDirentry(arg->mp.File);
		if(movedEntry->Dir != targetEntry->Dir) {
			/* we are indeed moving it to a new directory */
			direntry_t subEntry;
			Stream_t *oldDir;
			/* we have a directory here. Change its parent link */
			
			initializeDirentry(&subEntry, arg->mp.File);
			if(vfat_lookup(&subEntry, "..", 2, ACCEPT_DIR,
				       NULL, NULL))
				fprintf(stderr,
					" Directory has no parent entry\n");
			else {
				GET_DATA(targetEntry->Dir, 0, 0, 0, &fat);
				subEntry.dir.start[1] = (fat >> 8) & 0xff;
				subEntry.dir.start[0] = fat & 0xff;
				dir_write(&subEntry);
				if(arg->verbose){
					fprintf(stderr,
						"Easy, isn't it? I wonder why DOS can't do this.\n");
				}
			}
			
			/* wipe out original entry */			
			movedEntry->dir.name[0] = DELMARK;
			dir_write(movedEntry);
			
			/* free the old parent, allocate the new one. */
			oldDir = movedEntry->Dir;
			*movedEntry = *targetEntry;
			COPY(targetEntry->Dir);
			FREE(&oldDir);
			return 0;
		}
	}

	/* wipe out original entry */
	arg->mp.direntry->dir.name[0] = DELMARK;
	dir_write(arg->mp.direntry);
	return 0;
}



static int rename_file(direntry_t *entry, MainParam_t *mp)
/* rename a messy DOS file to another messy DOS file */
{
	int result;
	Stream_t *targetDir;
	char *shortname;
	const char *longname;

	Arg_t * arg = (Arg_t *) (mp->arg);

	arg->entry = entry;
	targetDir = mp->targetDir;

	if (targetDir == entry->Dir){
		arg->ch.ignore_entry = -1;
		arg->ch.source = entry->entry;
	} else {
		arg->ch.ignore_entry = -1;
		arg->ch.source = -2;
	}

	longname = mpPickTargetName(mp);
	shortname = 0;
	result = mwrite_one(targetDir, longname, shortname,
			    renameit, (void *)arg, &arg->ch);
	if(result == 1)
		return GOT_ONE;
	else
		return ERROR_ONE;
}


static int rename_directory(direntry_t *entry, MainParam_t *mp)
{
	int ret;

	/* moves a DOS dir */
	if(isSubdirOf(mp->targetDir, mp->File)) {
		fprintf(stderr, "Cannot move directory ");
		fprintPwd(stderr, entry);
		fprintf(stderr, " into one of its own subdirectories (");
		fprintPwd(stderr, getDirentry(mp->targetDir));
		fprintf(stderr, ")\n");
		return ERROR_ONE;
	}

	if(entry->entry == -3) {
		fprintf(stderr, "Cannot move a root directory: ");
		fprintPwd(stderr, entry);
		return ERROR_ONE;
	}

	ret = rename_file(entry, mp);
	if(ret & ERROR_ONE)
		return ret;
	
	return ret;
}

static int rename_oldsyntax(direntry_t *entry, MainParam_t *mp)
{
	int result;
	Stream_t *targetDir;
	const char *shortname, *longname;

	Arg_t * arg = (Arg_t *) (mp->arg);
	arg->entry = entry;
	targetDir = entry->Dir;

	arg->ch.ignore_entry = -1;
	arg->ch.source = entry->entry;

	if(!strcasecmp(mp->shortname, arg->fromname)){
		longname = mp->longname;
		shortname = mp->targetName;
	} else {
		longname = mp->targetName;
		shortname = 0;
	}
	result = mwrite_one(targetDir, longname, shortname,
			    renameit, (void *)arg, &arg->ch);
	if(result == 1)
		return GOT_ONE;
	else
		return ERROR_ONE;
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

void mmove(int argc, char **argv, int oldsyntax)
{
	Arg_t arg;
	int c;
	char shortname[13];
	char longname[VBUFSIZE];
	char def_drive;
	int i;
	int interactive;

	/* get command line options */

	init_clash_handling(& arg.ch);

	interactive = 0;

	/* get command line options */
	arg.verbose = 0;
	arg.nowarn = 0;
	arg.interactive = 0;
	while ((c = getopt(argc, argv, "invorsamORSAM")) != EOF) {
		switch (c) {
			case 'i':
				arg.interactive = 1;
				break;
			case 'n':
				arg.nowarn = 1;
				break;
			case 'v':	/* dummy option for mcopy */
				arg.verbose = 1;
				break;
			case '?':
				usage();
			default:
				if(handle_clash_options(&arg.ch, c))
					usage();
				break;
		}
	}

	if (argc - optind < 2)
		usage();

	init_mp(&arg.mp);		
	arg.mp.arg = (void *) &arg;
	arg.mp.openflags = O_RDWR;

	/* look for a default drive */
	def_drive = '\0';
	for(i=optind; i<argc; i++)
		if(argv[i][0] && argv[i][1] == ':' ){
			if(!def_drive)
				def_drive = toupper(argv[i][0]);
			else if(def_drive != toupper(argv[i][0])){
				fprintf(stderr,
					"Cannot move files across different drives\n");
				exit(1);
			}
		}

	if(def_drive)
		*(arg.mp.mcwd) = def_drive;

	if (oldsyntax && (argc - optind != 2 || strpbrk(":/", argv[argc-1])))
		oldsyntax = 0;

	arg.mp.lookupflags = 
	  ACCEPT_PLAIN | ACCEPT_DIR | DO_OPEN_DIRS | NO_DOTS;

	if (!oldsyntax){
		target_lookup(&arg.mp, argv[argc-1]);
		arg.mp.callback = rename_file;
		arg.mp.dirCallback = rename_directory;
	} else {
		/* do not look up the target; it will be the same dir as the
		 * source */
		arg.fromname = argv[optind];
		if(arg.fromname[0] && arg.fromname[1] == ':')
			arg.fromname += 2;
		arg.fromname = _basename(arg.fromname);
		arg.mp.targetName = strdup(argv[argc-1]);
		arg.mp.callback = rename_oldsyntax;
	}


	arg.mp.longname = longname;
	arg.mp.shortname = shortname;

	exit(main_loop(&arg.mp, argv + optind, argc - optind - 1));
}
