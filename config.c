#include "sysincludes.h"
#include "mtools.h"
#include "codepage.h"
#include "mtoolsPaths.h"

/* global variables */
/* they are not really harmful here, because there is only one configuration
 * file per invocations */

#define MAX_LINE_LEN 256

/* scanner */
static char buffer[MAX_LINE_LEN+1]; /* buffer for the whole line */
static char *pos; /* position in line */
static char *token; /* last scanned token */
static int token_length; /* length of the token */
static FILE *fp; /* file pointer for configuration file */
static int linenumber; /* current line number. Only used for printing
			* error messages */
static const char *filename; /* current file name. Only used for printing
			      * error messages */
static int file_nr=0;


static int flag_mask; /* mask of currently set flags */

/* devices */
static int cur_devs; /* current number of defined devices */
static int cur_dev; /* device being filled in. If negative, none */
static int trusted=0; /* is the currently parsed device entry trusted? */
static int nr_dev; /* number of devices that the current table can hold */
struct device *devices; /* the device table */
static int token_nr; /* number of tokens in line */

/* "environment" variables */
int mtools_skip_check=0;
int mtools_fat_compatibility=0;
int mtools_ignore_short_case=0;
int mtools_rate_0=0;
int mtools_rate_any=0;
int mtools_no_vfat=0;
int mtools_numeric_tail=1;
int mtools_dotted_dir=0;
int mtools_twenty_four_hour_clock=1;
char *mtools_date_string="mm-dd-yyyy";
char *country_string=0;

typedef struct switches_l {
	const char *name;
	caddr_t address;
	enum {
		INT,
		STRING
	} type;
} switches_t;

static switches_t switches[] = {
    { "MTOOLS_LOWER_CASE", (caddr_t) & mtools_ignore_short_case, INT },
    { "MTOOLS_FAT_COMPATIBILITY", (caddr_t) & mtools_fat_compatibility, INT },
    { "MTOOLS_SKIP_CHECK", (caddr_t) & mtools_skip_check, INT },
    { "MTOOLS_NO_VFAT", (caddr_t) & mtools_no_vfat, INT },
    { "MTOOLS_RATE_0", (caddr_t) &mtools_rate_0, INT },
    { "MTOOLS_RATE_ANY", (caddr_t) &mtools_rate_any, INT },
    { "MTOOLS_NAME_NUMERIC_TAIL", (caddr_t) &mtools_numeric_tail, INT },
    { "MTOOLS_DOTTED_DIR", (caddr_t) &mtools_dotted_dir, INT },
    { "MTOOLS_TWENTY_FOUR_HOUR_CLOCK", 
      (caddr_t) &mtools_twenty_four_hour_clock, INT },
    { "MTOOLS_DATE_STRING",
      (caddr_t) &mtools_date_string, STRING },
    { "COUNTRY", (caddr_t) &country_string, STRING }
};

typedef struct {
    const char *name;
    int flag;
} flags_t;

static flags_t openflags[] = {
#ifdef O_SYNC
    { "sync",		O_SYNC },
#endif
#ifdef O_NDELAY
    { "nodelay",	O_NDELAY },
#endif
#ifdef O_EXCL
    { "exclusive",	O_EXCL }
#endif
};

static flags_t misc_flags[] = {
#ifdef USE_XDF
	{ "use_xdf",		USE_XDF_FLAG },
#endif
	{ "scsi",			SCSI_FLAG },
	{ "nolock",			NOLOCK_FLAG },
	{ "mformat_only",	MFORMAT_ONLY_FLAG },
	{ "privileged",		PRIV_FLAG },
	{ "vold",			VOLD_FLAG }
};

static struct {
    const char *name;
    signed char fat_bits;
    int tracks;
    unsigned short heads;
    unsigned short sectors;
} default_formats[] = {
    { "hd514",			12, 80, 2, 15 },
    { "high-density-5-1/4",	12, 80, 2, 15 },
    { "1.2m",			12, 80, 2, 15 },
    
    { "hd312",			12, 80, 2, 18 },
    { "high-density-3-1/2",	12, 80, 2, 18 },
    { "1.44m",	 		12, 80, 2, 18 },

    { "dd312",			12, 80, 2, 9 },
    { "double-density-3-1/2",	12, 80, 2, 9 },
    { "720k",			12, 80, 2, 9 },

    { "dd514",			12, 40, 2, 9 },
    { "double-density-5-1/4",	12, 40, 2, 9 },
    { "360k",			12, 40, 2, 9 },

    { "320k",			12, 40, 2, 8 },
    { "180k",			12, 40, 1, 9 },
    { "160k",			12, 40, 1, 8 }
};

#define OFFS(x) ((caddr_t)&((struct device *)0)->x)

static switches_t dswitches[]= {
    { "FILE", OFFS(name), STRING },
    { "OFFSET", OFFS(offset), INT },
    { "PARTITION", OFFS(partition), INT },
    { "FAT", OFFS(fat_bits), INT },
    { "FAT_BITS", OFFS(fat_bits), INT },
    { "MODE", OFFS(mode), INT },
    { "TRACKS",  OFFS(tracks), INT },
    { "CYLINDERS",  OFFS(tracks), INT },
    { "HEADS", OFFS(heads), INT },
    { "SECTORS", OFFS(sectors), INT },
    { "HIDDEN", OFFS(hidden), INT },
    { "PRECMD", OFFS(precmd), STRING },
    { "BLOCKSIZE", OFFS(blocksize), INT }
};

static void syntax(const char *msg)
{
    fprintf(stderr,"Syntax error at line %d column %ld in file %s: %s\n",
	    linenumber, (long)(token - buffer), filename, msg);
    exit(1);
}

static void get_env_conf(void)
{
    char *s;
    int i;

    for(i=0; i< sizeof(switches) / sizeof(*switches); i++) {
	s = getenv(switches[i].name);
	if(s) {
	    if(switches[i].type == INT)
		* ((int *)switches[i].address) = strtoul(s,0,0);
	    else if (switches[i].type == STRING)
		* ((char **)switches[i].address) = s;
	}
    }
}

static int getline(void)
{
	if(!fgets(buffer, MAX_LINE_LEN, fp))
		return -1;
	linenumber++;
	pos = buffer;
	token_nr = 0;
	buffer[MAX_LINE_LEN] = '\0';
	if(strlen(buffer) == MAX_LINE_LEN)
		syntax("line too long");
	return 0;
}
		
static void skip_junk(int expect)
{
	while(!pos || !*pos || strchr(" #\n\t", *pos)) {
		if(!pos || !*pos || *pos == '#') {
			if(getline()) {
				pos = 0;
				if(expect)
					syntax("end of file unexpected");
				return;
			}
		} else
			pos++;
	}
	token_nr++;
}

/* get the next token */
static char *get_next_token(void)
{
	skip_junk(0);
	if(!pos) {
		token_length = 0;
		token = 0;
		return 0;
	}
	token = pos;
	token_length = strcspn(token, " \t\n#:=");
	pos += token_length;
	return token;
}

static int match_token(const char *template)
{
	return (strlen(template) == token_length &&
		!strncasecmp(template, token, token_length));
}

static void expect_char(char c)
{
	char buf[11];

	skip_junk(1);
	if(*pos != c) {
		sprintf(buf, "expected %c", c);
		syntax(buf);
	}
	pos++;
}

static char *get_string(void)
{
	char *end, *str;

	skip_junk(1);
	if(*pos != '"')
		syntax(" \" expected");
	str = pos+1;
	end = strchr(str, '\"');
	if(!end)
		syntax("unterminated string constant");
	*end = '\0';
	pos = end+1;
	return str;
}

static int get_number(void)
{
	char *last;
	int n;

	skip_junk(1);
	last = pos;
	n=strtoul(pos, &pos, 0);
	if(last == pos)
		syntax("numeral expected");
	pos++;
	token_nr++;
	return n;
}

/* purge all entries pertaining to a given drive from the table */
static void purge(char drive, int fn)
{
	int i,j;

	drive = toupper(drive);
	for(j=0, i=0; i < cur_devs; i++) {
		if(devices[i].drive != drive ||
		   devices[i].file_nr == fn)
			devices[j++] = devices[i];
	}
	cur_devs = j;
}

static void grow(void)
{
	if(cur_devs >= nr_dev - 2) {
		nr_dev = (cur_devs + 2) << 1;
		if(!(devices=Grow(devices, nr_dev, struct device))){
			printOom();
			exit(1);
		}
	}
}
	

static void init_drive(void)
{
	memset((char *)&devices[cur_dev], 0, sizeof(struct device));
	devices[cur_dev].ssize = 2;
}

/* prepends a device to the table */
static void prepend(void)
{
	int i;

	grow();
	for(i=cur_devs; i>0; i--)
		devices[i] = devices[i-1];
	cur_dev = 0;
	cur_devs++;
	init_drive();
}


/* appends a device to the table */
static void append(void)
{
	grow();
	cur_dev = cur_devs;
	cur_devs++;
	init_drive();
}


static void finish_drive_clause(void)
{
	if(cur_dev == -1) {
		trusted = 0;
		return;
	}
	if(!devices[cur_dev].name)
		syntax("missing filename");
	if((devices[cur_dev].tracks ||
	    devices[cur_dev].heads ||
	    devices[cur_dev].sectors) &&
	   (!devices[cur_dev].tracks ||
	    !devices[cur_dev].heads ||
	    !devices[cur_dev].sectors))
		syntax("incomplete geometry: either indicate all of track/heads/sectors or none of them");
	devices[cur_dev].file_nr = file_nr;
	if(! (flag_mask & PRIV_FLAG) && IS_SCSI(&devices[cur_dev]))
		devices[cur_dev].misc_flags |= PRIV_FLAG;
	if(!trusted)
		devices[cur_dev].misc_flags &= ~PRIV_FLAG;
	trusted = 0;
	cur_dev = -1;
}

static int set_var(struct switches_l *switches, int nr,
		   caddr_t base_address)
{
	int i;
	for(i=0; i < nr; i++) {
		if(match_token(switches[i].name)) {
			expect_char('=');
			if(switches[i].type == INT)
				* ((int *)((long)switches[i].address+base_address)) = 
					get_number();
			else if (switches[i].type == STRING)
				* ((char**)((long)switches[i].address+base_address))=
				   strdup(get_string());
			return 0;
		}
	}
	return 1;
}

static int set_openflags(struct device *dev)
{
	int i;

	for(i=0; i < sizeof(openflags) / sizeof(*openflags); i++) {
		if(match_token(openflags[i].name)) {
			dev->mode |= openflags[i].flag;
			return 0;
		}
	}
	return 1;
}

static int set_misc_flags(struct device *dev)
{
	int i;

	for(i=0; i < sizeof(misc_flags) / sizeof(*misc_flags); i++) {
		if(match_token(misc_flags[i].name)) {
			flag_mask |= misc_flags[i].flag;
			skip_junk(0);
			if(pos && *pos == '=') {
				pos++;
				switch(get_number()) {
					case 0:
						return 0;
					case 1:
						break;
					default:
						syntax("expected 0 or 1");
				}
			}
			dev->misc_flags |= misc_flags[i].flag;
			return 0;
		}
	}
	return 1;
}

static int set_def_format(struct device *dev)
{
	int i;

	for(i=0; i < sizeof(default_formats)/sizeof(*default_formats); i++) {
		if(match_token(default_formats[i].name)) {
			if(!dev->ssize)
				dev->ssize = 2;
			if(!dev->tracks)
				dev->tracks = default_formats[i].tracks;
			if(!dev->heads)
				dev->heads = default_formats[i].heads;
			if(!dev->sectors)
				dev->sectors = default_formats[i].sectors;
			if(!dev->fat_bits)
				dev->fat_bits = default_formats[i].fat_bits;
			return 0;
		}
	}
	return 1;
}    

static void get_codepage(void)
{
	int i;
	unsigned short n;

	if(!Codepage)
		Codepage = New(Codepage_t);
	for(i=0; i<128; i++) {
		n = get_number();
		if(n > 0xff)
			n = 0x5f;
		Codepage->tounix[i] = n;
	}	
}

static void get_toupper(void)
{
	int i;

	if(!mstoupper)
		mstoupper = safe_malloc(128);
	for(i=0; i<128; i++)
		mstoupper[i] = get_number();
}

static void parse_old_device_line(char drive)
{
	char name[MAXPATHLEN];
	int items;
	long offset;

	/* finish any old drive */
	finish_drive_clause();

	/* purge out data of old configuration files */
	purge(drive, file_nr);
	
	/* reserve slot */
	append();
	items = sscanf(token,"%c %s %i %i %i %i %li",
		       &devices[cur_dev].drive,name,&devices[cur_dev].fat_bits,
		       &devices[cur_dev].tracks,&devices[cur_dev].heads,
		       &devices[cur_dev].sectors, &offset);
	devices[cur_dev].offset = offset;
	switch(items){
		case 2:
			devices[cur_dev].fat_bits = 0;
			/* fall thru */
		case 3:
			devices[cur_dev].sectors = 0;
			devices[cur_dev].heads = 0;
			devices[cur_dev].tracks = 0;
			/* fall thru */
		case 6:
			devices[cur_dev].offset = 0;
			/* fall thru */
		default:
			break;
		case 0:
		case 1:
		case 4:
		case 5:
			syntax("bad number of parameters");
			exit(1);
	}
	if(!devices[cur_dev].tracks){
		devices[cur_dev].sectors = 0;
		devices[cur_dev].heads = 0;
	}
	
	devices[cur_dev].drive = toupper(devices[cur_dev].drive);
	if (!(devices[cur_dev].name = strdup(name))) {
		printOom();
		exit(1);
	}
	finish_drive_clause();
	pos=0;
}

static int parse_one(int privilege)
{
	int action=0;

	get_next_token();
	if(!token)
		return 0;

	if((match_token("drive") && ((action = 1)))||
	   (match_token("drive+") && ((action = 2))) ||
	   (match_token("+drive") && ((action = 3))) ||
	   (match_token("clear_drive") && ((action = 4))) ) {
		/* finish off the previous drive */
		finish_drive_clause();

		get_next_token();
		if(token_length != 1)
			syntax("drive letter expected");

		if(action==1 || action==4)
			/* replace existing drive */			
			purge(token[0], file_nr);
		if(action==4)
			return 1;
		if(action==3)
			prepend();
		else
			append();
		memset((char*)(devices+cur_dev), 0, sizeof(*devices));
		trusted = privilege;
		flag_mask = 0;
		devices[cur_dev].drive = toupper(token[0]);
		expect_char(':');
		return 1;
	}
	if(token_nr == 1 && token_length == 1) {
		parse_old_device_line(token[0]);
		return 1;
	}
	if(match_token("default_fucase")) {
		free(mstoupper);
		mstoupper=0;
	}
	if(match_token("default_tounix")) {
		Free(Codepage);
		Codepage = 0;
	}
	if(match_token("fucase")) {
		expect_char(':');
		get_toupper();
		return 1;
	}
	if(match_token("tounix")) {
		expect_char(':');
		get_codepage();
		return 1;
	}
	
	if((cur_dev < 0 || 
	    (set_var(dswitches,
		     sizeof(dswitches)/sizeof(*dswitches),
		     (caddr_t)&devices[cur_dev]) &&
	     set_openflags(&devices[cur_dev]) &&
	     set_misc_flags(&devices[cur_dev]) &&
	     set_def_format(&devices[cur_dev]))) &&
	   set_var(switches,
		   sizeof(switches)/sizeof(*switches), 0))
		syntax("unrecognized keyword");
	return 1;
}

static int parse(const char *name, int privilege)
{
	filename = name;
	fp = fopen(filename, "r");
	if(!fp)
		return 0;
	file_nr++;
	linenumber = 0;
	pos = 0;
	token = 0;
	cur_dev = -1; /* no current device */

	while(parse_one(privilege));
	finish_drive_clause();
	fclose(fp);
	return 1;
}

void read_config(void)
{
	char *homedir;
	char *envConfFile;
	char conf_file[MAXPATHLEN+sizeof(CFG_FILE1)];

	
	/* copy compiled-in devices */
	file_nr = 0;
	cur_devs = nr_const_devices;
	nr_dev = nr_const_devices + 2;
	devices = NewArray(nr_dev, struct device);
	if(!devices) {
		printOom();
		exit(1);
	}
	if(nr_const_devices)
		memcpy(devices, const_devices,
		       nr_const_devices*sizeof(struct device));

	(void) ((parse(CONF_FILE,1) | 
		 parse(LOCAL_CONF_FILE,1) |
		 parse(SYS_CONF_FILE,1)) ||
		(parse(OLD_CONF_FILE,1) | 
		 parse(OLD_LOCAL_CONF_FILE,1)));
	/* the old-name configuration files only get executed if none of the
	 * new-name config files were used */

	homedir = get_homedir();
	if ( homedir ){
		strncpy(conf_file, homedir, MAXPATHLEN );
		conf_file[MAXPATHLEN]='\0';
		strcat( conf_file, CFG_FILE1);
		parse(conf_file,0);
	}
	memset((char *)&devices[cur_devs],0,sizeof(struct device));

	envConfFile = getenv("MTOOLSRC");
	if(envConfFile)
		parse(envConfFile,0);

	/* environmental variables */
	get_env_conf();
	if(mtools_skip_check)
		mtools_fat_compatibility=1;
	init_codepage();
}

void mtoolstest(int argc, char **argv, int type)
{
	/* testing purposes only */
	struct device *dev;
	int i,j;
	char drive='\0';

	if(argc > 1 && argv[1][0] && argv[1][1] == ':') {
		drive = toupper(argv[1][0]);
	}

	for (dev=devices; dev->name; dev++) {
		if(drive && drive != dev->drive)
			continue;
		printf("drive %c:\n", dev->drive);
		printf("\t#fn=%d mode=%d\n",
		       dev->file_nr, dev->mode);
		printf("\tfile=\"%s\" fat_bits=%d \n",
		       dev->name,dev->fat_bits);
		printf("\ttracks=%d heads=%d sectors=%d hidden=%d\n",
		       dev->tracks, dev->heads, dev->sectors, dev->hidden);
		printf("\toffset=0x%lx\n", (long) dev->offset);
		printf("\tpartition=%d\n", dev->partition);

		if(dev->misc_flags)
			printf("\t");

		if(IS_SCSI(dev))
			printf("scsi ");
		if(IS_PRIVILEGED(dev))
			printf("privileged");
		if(IS_MFORMAT_ONLY(dev))
			printf("mformat_only ");
		if(SHOULD_USE_VOLD(dev))
			printf("vold ");
#ifdef USE_XDF
		if(SHOULD_USE_XDF(dev))
			printf("use_xdf ");
#endif
		if(dev->misc_flags)
			printf("\n");

		if(dev->mode)
			printf("\t");
#ifdef O_SYNC
		if(dev->mode & O_SYNC)
			printf("sync ");
#endif
#ifdef O_NDELAY
		if((dev->mode & O_NDELAY))
			printf("nodelay ");
#endif
#ifdef O_EXCL
		if((dev->mode & O_EXCL))
			printf("exclusive ");
#endif
		if(dev->mode)
			printf("\n");

		if(dev->precmd)
			printf("\tprecmd=%s\n", dev->precmd);

		printf("\n");
	}
	
	printf("tounix:\n");
	for(i=0; i < 16; i++) {
		putchar('\t');
		for(j=0; j<8; j++)
			printf("0x%02x ",
			       (unsigned char)Codepage->tounix[i*8+j]);
		putchar('\n');
	}
	printf("\nfucase:\n");
	for(i=0; i < 16; i++) {
		putchar('\t');
		for(j=0; j<8; j++)
			printf("0x%02x ",
			       (unsigned char)mstoupper[i*8+j]);
		putchar('\n');
	}
	if(country_string)
		printf("COUNTRY=%s\n", country_string);
	printf("mtools_fat_compatibility=%d\n",mtools_fat_compatibility);
	printf("mtools_skip_check=%d\n",mtools_skip_check);
	printf("mtools_lower_case=%d\n",mtools_ignore_short_case);

	exit(0);
}
