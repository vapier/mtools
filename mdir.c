/*
 * mdir.c:
 * Display an MSDOS directory
 */

#include "sysincludes.h"
#include "msdos.h"
#include "vfat.h"
#include "mtools.h"
#include "file.h"
#include "mainloop.h"
#include "fs.h"
#include "codepage.h"

static int recursive;
static int wide;
static int all;
static int concise;
static int fast=0;

static char *dirPath;
static char currentDrive;
static Stream_t *currentDir;

static int filesInDir; /* files in current dir */
static int filesOnDrive; /* files on drive */
	
static int dirsOnDrive; /* number of listed directories on this drive */

static int debug = 0; /* debug mode */

static int bytesInDir;
static int bytesOnDrive;
static Stream_t *RootDir;	


static char shortname[13];
static char longname[VBUFSIZE];


/*
 * Print an MSDOS directory date stamp.
 */
static inline void print_date(struct directory *dir)
{
	char year[5];
	char day[3];
	char month[3];
	char *p;

	sprintf(year, "%04d", DOS_YEAR(dir));
	sprintf(day, "%02d", DOS_DAY(dir));
	sprintf(month, "%02d", DOS_MONTH(dir));

	for(p=mtools_date_string; *p; p++) {
		if(!strncasecmp(p, "yyyy", 4)) {
			printf("%04d", DOS_YEAR(dir));
			p+= 3;
			continue;
		} else if(!strncasecmp(p, "yy", 2)) {
			printf("%02d", DOS_YEAR(dir) % 100);
			p++;
			continue;
		} else if(!strncasecmp(p, "dd", 2)) {
			printf("%02d", DOS_DAY(dir));
			p++;
			continue;
		} else if(!strncasecmp(p, "mm", 2)) {
			printf("%02d", DOS_MONTH(dir));
			p++;
			continue;
		}
		putchar(*p);
	}
}

/*
 * Print an MSDOS directory time stamp.
 */
static inline void print_time(struct directory *dir)
{
	char am_pm;
	int hour = DOS_HOUR(dir);
       
	if(!mtools_twenty_four_hour_clock) {
		am_pm = (hour >= 12) ? 'p' : 'a';
		if (hour > 12)
			hour = hour - 12;
		if (hour == 0)
			hour = 12;
	} else
		am_pm = ' ';

	printf("%2d:%02d%c", hour, DOS_MINUTE(dir), am_pm);
}

/*
 * Return a number in dotted notation
 */
static const char *dotted_num(unsigned long num, int width, char **buf)
{
	int      len;
	register char *srcp, *dstp;
	int size;

	size = width + width;
	*buf = malloc(size+1);

	if (*buf == NULL)
		return "";
	
	/* Create the number in maximum width; make sure that the string
	 * length is not exceeded (in %6ld, the result can be longer than 6!)
	 */
	sprintf(*buf, "%.*ld", size, num);

	for (srcp=*buf; srcp[1] != '\0'; ++srcp)
		if (srcp[0] == '0')
			srcp[0] = ' ';
		else
			break;
	
	len = strlen(*buf);
	srcp = (*buf)+len;
	dstp = (*buf)+len+1;

	for ( ; dstp >= (*buf)+4 && isdigit (srcp[-1]); ) {
		srcp -= 3;  /* from here we copy three digits */
		dstp -= 4;  /* that's where we put these 3 digits */
	}

	/* now finally copy the 3-byte blocks to their new place */
	while (dstp < (*buf) + len) {
		dstp[0] = srcp[0];
		dstp[1] = srcp[1];
		dstp[2] = srcp[2];
		if (dstp + 3 < (*buf) + len)
			/* use spaces instead of dots: they place both
			 * Americans and Europeans */
			dstp[3] = ' ';		
		srcp += 3;
		dstp += 4;
	}

	return (*buf) + len-width;
}

static inline void print_volume_label(Stream_t *Dir, char drive)
{
	Stream_t *Stream = GetFs(Dir);
	direntry_t entry;
	DeclareThis(FsPublic_t);
	char shortname[13];
	char longname[VBUFSIZE];

	RootDir = OpenRoot(Stream);
	if(concise)
		return;
	
	/* find the volume label */

	initializeDirentry(&entry, Dir);
	if(vfat_lookup(&entry, 0, 0, ACCEPT_LABEL | MATCH_ANY,
		       shortname, longname) )
		printf(" Volume in drive %c has no label", drive);
	else if (*longname)
		printf(" Volume in drive %c is %s (abbr=%s)",
		       drive, longname, shortname);
	else
		printf(" Volume in drive %c is %s",
		       drive, shortname);
	if(This->serialized)
		printf("\n Volume Serial Number is %04lX-%04lX",
		       (This->serial_number >> 16) & 0xffff, 
		       This->serial_number & 0xffff);
}


static void printSummary(int files, int bytes)
{
	if(!filesInDir)
		printf("No files\n");
	else {		
		char *s1;
		printf("      %3d file", files);
		if(files == 1)
			putchar(' ');
		else
			putchar('s');
		printf("       %s bytes\n",
		       dotted_num(bytes,13, &s1));
		if(s1)
			free(s1);
	}
}

static void leaveDirectory(void);

static void leaveDrive(void)
{
	if(!currentDrive)
		return;
	leaveDirectory();
	if(!concise) {
		char *s1;

		if(dirsOnDrive > 1) {
			printf("\nTotal files listed:\n");
			printSummary(filesOnDrive, bytesOnDrive);
		}
		if(RootDir && !fast) {
			int blocks = getfree(RootDir);
			printf("                      %s bytes free\n\n",
			       dotted_num(blocks,13, &s1));
		}
		if(s1)
			free(s1);
	}
	FREE(&RootDir);
	currentDrive = '\0';
}


static void enterDrive(Stream_t *Dir, char drive)
{
	if(currentDrive == drive)
		return; /* still the same */
	
	leaveDrive();
	
	print_volume_label(Dir, drive);

	currentDrive = drive;
	bytesOnDrive = 0;
	filesOnDrive = 0;
	dirsOnDrive = 0;
}

static char *emptyString="<out-of-memory>";

static void leaveDirectory(void)
{
	if(!currentDir)
		return;
	if(dirPath && dirPath != emptyString)
		free(dirPath);
	if(wide)
		putchar('\n');

	if(!concise)
		printSummary(filesInDir, bytesInDir);
	FREE(&currentDir);
}

static void enterDirectory(Stream_t *Dir)
{
	char drive;
	if(currentDir == Dir)
		return; /* still the same directory */

	leaveDirectory();

	drive = getDrive(Dir);
	enterDrive(Dir, drive);
	currentDir = COPY(Dir);

	dirPath = getPwd(getDirentry(Dir));
	if(!dirPath)
		dirPath=emptyString;
	if(!dirPath[3] && concise)
		dirPath[2]='\0';

	/* print directory title */
	if(!concise)
		printf("\nDirectory for %s\n", dirPath);

	if(!wide && !concise)
		printf("\n");

	dirsOnDrive++;
	bytesInDir = 0;
	filesInDir = 0;
}


static int list_file(direntry_t *entry, MainParam_t *mp)
{
	size_t size;
	int i;
	int Case;

	if(!all && (entry->dir.attr & 0x6))
		return 0;

	if(concise && isSpecial(entry->name))
		return 0;

	enterDirectory(entry->Dir);
	if (wide) {
		if(filesInDir % 5)
			putchar(' ');				
		else
			putchar('\n');
	}
	
	if(entry->dir.attr & 0x10){
		size = 0;
	} else
		size = FILE_SIZE(&entry->dir);
	
	Case = entry->dir.Case;
	if(!(Case & (BASECASE | EXTCASE)) && 
	   mtools_ignore_short_case)
		Case |= BASECASE | EXTCASE;
	
	if(Case & EXTCASE){
		for(i=0; i<3;i++)
			entry->dir.ext[i] = tolower(entry->dir.ext[i]);
	}
	to_unix(entry->dir.ext,3);
	if(Case & BASECASE){
		for(i=0; i<8;i++)
			entry->dir.name[i] = tolower(entry->dir.name[i]);
	}
	to_unix(entry->dir.name,8);
	if(wide){
		if(entry->dir.attr & 0x10)
			printf("[%s]%*s", shortname,
			       (int) (15 - 2 - strlen(shortname)), "");
		else
			printf("%-15s", shortname);
	} else if(!concise) {				
		/* is a subdirectory */
		if(mtools_dotted_dir)
			printf("%-13s", shortname);
		else
			printf("%-8.8s %-3.3s ",
			       entry->dir.name, 
			       entry->dir.ext);
		if(entry->dir.attr & 0x10)
			printf("<DIR>    ");
		else
			printf(" %8ld", (long) size);
		printf(" ");
		print_date(&entry->dir);
		printf("  ");
		print_time(&entry->dir);

		if(debug)
			printf(" %s %d ", entry->dir.name, START(&entry->dir));
		
		if(*longname)
			printf(" %s", longname);
		printf("\n");
	} else {
		printf("%s/%s", dirPath, entry->name);
		if(entry->dir.attr & 0x10)
			putchar('/');
		putchar('\n');
	}

	filesOnDrive++;
	filesInDir++;

	bytesOnDrive += size;
	bytesInDir += size;
	return GOT_ONE;
}

static int list_non_recurs_directory(direntry_t *entry, MainParam_t *mp)
{
	/* list top-level directory
	 *   If this was matched by wildcard in the basename, list it as
	 *   file, otherwise, list it as directory */
	if (mp->basenameHasWildcard) {
		/* wildcard, list it as file */
		return list_file(entry, mp);
	} else {
		/* no wildcard, list it as directory */
		MainParam_t subMp;

		enterDirectory(mp->File);

		subMp = *mp;
		subMp.dirCallback = subMp.callback;
		return mp->loop(mp->File, &subMp, "*") | GOT_ONE;
	}
}


static int list_recurs_directory(direntry_t *entry, MainParam_t *mp)
{
	MainParam_t subMp;
	int ret;

	/* first list the files */
	subMp = *mp;
	subMp.lookupflags = ACCEPT_DIR | ACCEPT_PLAIN;
	subMp.dirCallback = list_file;
	subMp.callback = list_file;

	ret = mp->loop(mp->File, &subMp, "*");

	/* then list subdirectories */
	subMp = *mp;
	subMp.lookupflags = ACCEPT_DIR | NO_DOTS | NO_MSG | DO_OPEN;
	return ret | mp->loop(mp->File, &subMp, "*");
}



static void usage(void)
{
		fprintf(stderr, "Mtools version %s, dated %s\n",
			mversion, mdate);
		fprintf(stderr, "Usage: %s: [-V] [-w] [-a] msdosdirectory\n",
			progname);
		fprintf(stderr,
			"       %s: [-V] [-w] [-a] msdosfile [msdosfiles...]\n",
			progname);
		exit(1);
}


void mdir(int argc, char **argv, int type)
{
	int ret;
	MainParam_t mp;
	int faked;
	int c;
	char *fakedArgv[] = { "." };
	
	concise = 0;
	recursive = 0;
	wide = all = 0;
					/* first argument */
	while ((c = getopt(argc, argv, "waXfd/")) != EOF) {
		switch(c) {
			case 'w':
				wide = 1;
				break;
			case 'a':
				all = 1;
				break;
			case 'X':
				concise = 1;
				recursive = 1;
				break;
			case '/':
				recursive = 1;
				break;
			case 'f':
				fast = 1;
				break;
			case 'd':
				debug = 1;
				break;
			default:
				usage();
		}
	}

	/* fake an argument */
	faked = 0;
	if (optind == argc) {
		argv = fakedArgv;
		argc = 1;
		optind = 0;
	}

	init_mp(&mp);
	currentDrive = '\0';
	currentDir = 0;
	RootDir = 0;
	dirPath = 0;
	if(recursive) {
		mp.lookupflags = ACCEPT_DIR | DO_OPEN_DIRS | NO_DOTS;
		mp.dirCallback = list_recurs_directory;
	} else {
		mp.lookupflags = ACCEPT_DIR | ACCEPT_PLAIN | DO_OPEN_DIRS;
		mp.dirCallback = list_non_recurs_directory;
		mp.callback = list_file;
	}
	mp.longname = longname;
	mp.shortname = shortname;
	ret=main_loop(&mp, argv + optind, argc - optind);
	leaveDirectory();
	leaveDrive();
	exit(ret);
}
