/*
 * mattrib.c
 * Change MSDOS file attribute flags
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "mainloop.h"

typedef struct Arg_t {
	char add;
	unsigned char remove;
	struct MainParam_t mp;
	int recursive;
	int doPrintName;
} Arg_t;

int concise;

static int attrib_file(direntry_t *entry, MainParam_t *mp)
{
	Arg_t *arg=(Arg_t *) mp->arg;

	entry->dir.attr = (entry->dir.attr & arg->remove) | arg->add;
	dir_write(entry);
	return GOT_ONE;
}


static int view_attrib(direntry_t *entry, MainParam_t *mp)
{
	printf("  ");
	if(entry->dir.attr & 0x20)
		putchar('A');
	else
		putchar(' ');
	fputs("  ",stdout);
	if(entry->dir.attr & 0x4)
		putchar('S');
	else
		putchar(' ');
	if(entry->dir.attr & 0x2)
		putchar('H');
	else
		putchar(' ');
	if(entry->dir.attr & 0x1)
		putchar('R');
	else
		putchar(' ');
	printf("     ");
	fprintPwd(stdout, entry);
	printf("\n");
	return GOT_ONE;
}


static int concise_view_attrib(direntry_t *entry, MainParam_t *mp)
{
	Arg_t *arg=(Arg_t *) mp->arg;

	if(entry->dir.attr & 0x20)
		putchar('A');
	if(entry->dir.attr & 0x10)
		putchar('D');	
	if(entry->dir.attr & 0x4)
		putchar('S');
	if(entry->dir.attr & 0x2)
		putchar('H');
	if(entry->dir.attr & 0x1)
		putchar('R');
	if(arg->doPrintName) {
		putchar(' ');
		fprintPwd(stdout, entry);
	}
	putchar('\n');
	return GOT_ONE;
}

static int recursive_attrib(direntry_t *entry, MainParam_t *mp)
{
	mp->callback(entry, mp);
	return mp->loop(mp->File, mp, "*");
}


static void usage(void) NORETURN;
static void usage(void)
{
	fprintf(stderr, "Mtools version %s, dated %s\n", 
		mversion, mdate);
	fprintf(stderr, 
		"Usage: %s [-p] [-a|+a] [-h|+h] [-r|+r] [-s|+s] msdosfile [msdosfiles...]\n",
		progname);
	exit(1);
}

static int letterToCode(int letter)
{
	switch (toupper(letter)) {
		case 'A':
			return 0x20;
		case 'H':
			return 0x2;
		case 'R':
			return 0x1;
		case 'S':
			return 0x4;
		default:
			usage();
	}
}


void mattrib(int argc, char **argv, int type)
{
	Arg_t arg;
	int view;
	int c;
	int concise;
	char *ptr;

	arg.add = 0;
	arg.remove = 0xff;
	arg.recursive = 0;
	arg.doPrintName = 1;
	view = 0;
	concise = 0;

	while ((c = getopt(argc, argv, "/ahrsAHRSX")) != EOF) {
		switch (c) {
			default:
				arg.remove &= ~letterToCode(c);
				break;
			case '/':
				arg.recursive = 1;
				break;
			case 'X':
				concise = 1;
				break;
			case '?':
				usage();
		}
	}

	for(;optind < argc;optind++) {
		switch(argv[optind][0]) {
			case '+':
				for(ptr = argv[optind] + 1; *ptr; ptr++)
					arg.add |= letterToCode(*ptr);
				continue;
			case '-':
				for(ptr = argv[optind] + 1; *ptr; ptr++)
					arg.remove &= ~letterToCode(*ptr);
				continue;
		}
		break;
	}

	if(arg.remove == 0xff && !arg.add)
		view = 1;

	if (optind >= argc)
		usage();

	init_mp(&arg.mp);
	if(view){
		if(concise) {
			arg.mp.callback = concise_view_attrib;
			arg.doPrintName = (argc - optind > 1 ||
					   arg.recursive ||
					   strpbrk(argv[optind], "*[?") != 0);
		} else
			arg.mp.callback = view_attrib;
		arg.mp.openflags = O_RDONLY;
	} else {
		arg.mp.callback = attrib_file;
		arg.mp.openflags = O_RDWR;
	}

	if(arg.recursive)
		arg.mp.dirCallback = recursive_attrib;

	arg.mp.arg = (void *) &arg;
	arg.mp.lookupflags = ACCEPT_PLAIN | ACCEPT_DIR;
	if(arg.recursive)
		arg.mp.lookupflags |= DO_OPEN_DIRS | NO_DOTS;
	exit(main_loop(&arg.mp, argv + optind, argc - optind));
}
