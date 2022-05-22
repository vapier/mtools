/*  Copyright 1996-1999,2001,2002,2007-2009,2022 Alain Knaff.
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
 * System includes for mtools
 */

#ifndef SYSINCLUDES_H
#define SYSINCLUDES_H

#define _GNU_SOURCE

#include "config.h"


/* OS/2 needs __inline__, but for some reason is not autodetected */
#ifdef __EMX__
# ifndef inline
#  define inline __inline__
# endif
#endif

/***********************************************************************/
/*                                                                     */
/* OS dependencies which cannot be covered by the autoconfigure script */
/*                                                                     */
/***********************************************************************/


#ifdef OS_aux
/* A/UX needs POSIX_SOURCE, just as AIX does. Unlike SCO and AIX, it seems
 * to prefer TERMIO over TERMIOS */
#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#ifndef POSIX_SOURCE
# define POSIX_SOURCE
#endif

#endif


/* On AIX, we have to prefer strings.h, as string.h lacks a prototype
 * for strcasecmp. On Solaris, string.h lacks a prototype for strncasecmp_l.
 * On most other architectures, it's string.h which seems to be more complete */
#if ((defined OS_aix || defined OS_solaris) && defined HAVE_STRINGS_H)
# undef HAVE_STRING_H
#endif


#ifdef OS_ultrix
/* on ultrix, if termios present, prefer it instead of termio */
# ifdef HAVE_TERMIOS_H
#  undef HAVE_TERMIO_H
# endif
#endif

#ifdef OS_linux_gnu
/* RMS strikes again */
# ifndef OS_linux
#  define OS_linux
# endif
#endif

/* For compiling with MingW, use the following configure line

ac_cv_func_setpgrp_void=yes ../mtools/configure --build=i386-linux-gnu --host=i386-mingw32 --disable-floppyd --without-x --disable-raw-term --srcdir ../mtools

 */
#ifdef OS_mingw32
#ifndef OS_mingw32msvc
#define OS_mingw32msvc
#endif
#endif

#ifndef HAVE_CADDR_T
typedef void *caddr_t;
#endif


/***********************************************************************/
/*                                                                     */
/* Compiler dependencies                                               */
/*                                                                     */
/***********************************************************************/


#if defined __GNUC__ && defined __STDC__
/* gcc -traditional doesn't have PACKED, UNUSED and NORETURN */
# define PACKED __attribute__ ((packed))
# if __GNUC__ == 2 && __GNUC_MINOR__ > 6 || __GNUC__ >= 3
/* gcc 2.6.3 doesn't have "unused" */		/* mool */
#  define UNUSED(x) x __attribute__ ((unused));x
#  define UNUSEDP __attribute__ ((unused))
# endif
# define NORETURN __attribute__ ((noreturn))
# if __GNUC__ >= 8
#  define NONULLTERM __attribute__ ((nonstring))
# endif
#endif

#ifndef UNUSED
# define UNUSED(x) x
# define UNUSEDP /* */
#endif

#ifndef PACKED
# define PACKED /* */
#endif

#ifndef NORETURN
# define NORETURN /* */
#endif

#ifndef NONULLTERM
# define NONULLTERM /* */
#endif

/***********************************************************************/
/*                                                                     */
/* Include files                                                       */
/*                                                                     */
/***********************************************************************/

#ifdef HAVE_ASSERT_H
# include <assert.h>
#endif

#ifdef HAVE_FEATURES_H
# include <features.h>
#endif


#include <sys/types.h>

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#ifdef HAVE_STDARG_H
# include <stdarg.h>
#endif

#if HAVE_STDBOOL_H
# include <stdbool.h>
#else
# if ! HAVE__BOOL
#  ifdef __cplusplus
typedef bool _Bool;
#   else
typedef unsigned char _Bool;
#  endif
# endif
# define bool _Bool
# define false 0
# define true 1
# define __bool_true_false_are_defined 1
#endif

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_LIBC_H
# include <libc.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#endif

#if !HAVE_DECL_OPTARG
extern char *optarg;
extern int optind;
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# ifndef sunos
# include <sys/ioctl.h>
#endif
#endif
/* if we don't have sys/ioctl.h, we rely on unistd to supply a prototype
 * for it. If it doesn't, we'll only get a (harmless) warning. The idea
 * is to get mtools compile on as many platforms as possible, but to not
 * suppress warnings if the platform is broken, as long as these warnings do
 * not prevent compilation */

#ifdef HAVE_TIME_H
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# endif
#endif

#ifndef NO_TERMIO
# ifdef HAVE_TERMIO_H
#  include <termio.h>
# elif defined HAVE_SYS_TERMIO_H
#  include <sys/termio.h>
# endif
# if !defined OS_ultrix || !(defined HAVE_TERMIO_H || defined HAVE_TERMIO_H)
/* on Ultrix, avoid double inclusion of both termio and termios */
#  ifdef HAVE_TERMIOS_H
#   include <termios.h>
#  elif defined HAVE_SYS_TERMIOS_H
#   include <sys/termios.h>
#  endif
# endif
# ifdef HAVE_STTY_H
#  include <sgtty.h>
# endif
#endif


#if defined(OS_aux) && !defined(_SYSV_SOURCE)
/* compiled in POSIX mode, this is left out unless SYSV */
#define	NCC	8
struct termio {
	unsigned short	c_iflag;	/* input modes */
	unsigned short	c_oflag;	/* output modes */
	unsigned short	c_cflag;	/* control modes */
	unsigned short	c_lflag;	/* line discipline modes */
	char	c_line;			/* line discipline */
	unsigned char	c_cc[NCC];	/* control chars */
};
extern int ioctl(int fildes, int request, void *arg);
#endif


#ifdef HAVE_MNTENT_H
# include <mntent.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

/* Can only be done here, as BSD is defined in sys/param.h :-( */
#if defined BSD || defined __BEOS__
/* on BSD and on BEOS, we prefer gettimeofday, ... */
# ifdef HAVE_GETTIMEOFDAY
#  undef HAVE_TZSET
# endif
#else /* BSD */
/* ... elsewhere we prefer tzset */
# ifdef HAVE_TZSET
#  undef HAVE_GETTIMEOFDAY
# endif
#endif


#include <sys/stat.h>

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#ifndef OS_mingw32msvc
#include <pwd.h>
#endif


#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif

#ifdef HAVE_MEMORY_H
# include <memory.h>
#endif

#ifdef HAVE_MALLOC_H
# include <malloc.h>
#endif

#ifdef HAVE_IO_H
# include <io.h>
#endif

#ifdef HAVE_SIGNAL_H
# include <signal.h>
#else
# ifdef HAVE_SYS_SIGNAL_H
#  include <sys/signal.h>
# endif
#endif

#ifdef HAVE_UTIME_H
# include <utime.h>
#endif

#ifdef HAVE_SYS_WAIT_H
# ifndef DONT_NEED_WAIT
#  include <sys/wait.h>
# endif
#endif

#ifdef HAVE_WCHAR_H
# include <wchar.h>
# ifndef HAVE_PUTWC
#  define putwc(c,f) fprintf((f),"%lc",(c))
# endif
#else
# define wcscmp strcmp
# define wcscasecmp strcasecmp
# define wcsdup strdup
# define wcslen strlen
# define wcschr strchr
# define wcspbrk strpbrk
# define wchar_t char
# define wint_t unsigned int
# define putwc putc
#endif

#ifdef HAVE_WCTYPE_H
# include <wctype.h>
#else
# define towupper(x) toupper(x)
# define towlower(x) tolower(x)
# define iswupper(x) isupper(x)
# define iswlower(x) islower(x)
# define iswcntrl(x) iscntrl(x)
#endif

#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#ifdef HAVE_XLOCALE_H
# include <xlocale.h>
#endif

#ifndef INADDR_NONE
#define INADDR_NONE (-1)
#endif


#ifdef sgi
#define MSGIHACK __EXTENSIONS__
#undef __EXTENSIONS__
#endif
#include <math.h>
#ifdef sgi
#define __EXTENSIONS__ MSGIHACK
#undef MSGIHACK
#endif

/* missing functions */
#ifndef HAVE_SRANDOM
# ifdef OS_mingw32msvc
#  define srandom srand
# else
#  define srandom srand48
# endif
#endif

#ifndef HAVE_RANDOM
# ifdef OS_mingw32msvc
#  define random (long)rand
# else
#  define random (long)lrand48
# endif
#endif

#ifndef HAVE_STRCHR
# define strchr index
#endif

#ifndef HAVE_STRRCHR
# define strrchr rindex
#endif

#ifndef HAVE_STRSTR
extern char * strstr (const char* haystack, const char *needle);
#endif

#ifndef HAVE_STRDUP
extern char *strdup(const char *str);
#endif /* HAVE_STRDUP */

#ifndef HAVE_STRNDUP
extern char *strndup(const char *s, size_t n);
#endif /* HAVE_STRDUP */

#ifndef HAVE_MEMCPY
extern char *memcpy(char *s1, const char *s2, size_t n);
#endif

#ifndef HAVE_MEMSET
extern char *memset(char *s, char c, size_t n);
#endif /* HAVE_MEMSET */


#ifndef HAVE_STRPBRK
extern char *strpbrk(const char *string, const char *brkset);
#endif /* HAVE_STRPBRK */


#ifndef HAVE_STRTOUL
unsigned long strtoul(const char *string, char **eptr, int base);
#endif /* HAVE_STRTOUL */

#ifndef HAVE_STRSPN
size_t strspn(const char *s, const char *accept);
#endif /* HAVE_STRSPN */

#ifndef HAVE_STRCSPN
size_t strcspn(const char *s, const char *reject);
#endif /* HAVE_STRCSPN */

#ifndef HAVE_STRERROR
char *strerror(int errno);
#endif

#ifndef HAVE_ATEXIT
int atexit(void (*function)(void));

#ifndef HAVE_ON_EXIT
void myexit(int code) NORETURN;
#define exit myexit
#endif

#endif


#ifndef HAVE_MEMMOVE
# define memmove(DST, SRC, N) bcopy(SRC, DST, N)
#endif

#ifndef HAVE_STRCASECMP
int strcasecmp(const char *s1, const char *s2);
#endif

#ifndef HAVE_STRNCASECMP
int strncasecmp(const char *s1, const char *s2, size_t n);
#endif

#ifndef HAVE_GETPASS
char *getpass(const char *prompt);
#endif

#ifdef HAVE_WCHAR_H

# ifndef HAVE_WCSDUP
wchar_t *wcsdup(const wchar_t *wcs);
# endif

# ifndef HAVE_WCSCASECMP
int wcscasecmp(const wchar_t *s1, const wchar_t *s2);
# endif

# ifndef HAVE_WCSNLEN
size_t wcsnlen(const wchar_t *wcs, size_t l);
# endif

#endif

#if 0
#ifndef HAVE_BASENAME
const char *basename(const char *filename);
#endif
#endif

const char *_basename(const char *filename);

void _stripexe(char *filename);

#ifndef __STDC__
# ifndef signed
#  define signed /**/
# endif
#endif /* !__STDC__ */

#ifndef UINT8_MAX
#define UINT8_MAX (0xff)
#endif

#ifndef UINT16_MAX
#define UINT16_MAX (0xffff)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX (0xffffffffU)
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_RDWR | O_WRONLY)
#endif


#if defined OS_hpux || \
    defined OS_sunos || defined OS_solaris || \
    defined OS_linux || \
    (defined _SCO_DS) && (defined SCSIUSERCMD) || \
    defined sgi || \
    (defined OS_freebsd) && (__FreeBSD__ >= 2) || \
    defined(OS_netbsd) || defined(OS_netbsdelf)
# define HAVE_SCSI
#endif


/***************************************************************************/
/*                                                                         */
/* Prototypes for systems where the functions exist but not the prototypes */
/*                                                                         */
/***************************************************************************/



/* prototypes which might be missing on some platforms, even if the functions
 * are present.  Do not declare argument types, in order to avoid conflict
 * on platforms where the prototypes _are_ correct.  Indeed, for most of
 * these, there are _several_ "correct" parameter definitions, and not all
 * platforms use the same.  For instance, some use the const attribute for
 * strings not modified by the function, and others do not.  By using just
 * the return type, which rarely changes, we avoid these problems.
 */

/* Correction:  Now it seems that even return values are not standardized :-(
  For instance  DEC-ALPHA, OSF/1 3.2d uses ssize_t as a return type for read
  and write.  NextStep uses a non-void return value for exit, etc.  With the
  advent of 64 bit system, we'll expect more of these problems in the future.
  Better uncomment the lot, except on SunOS, which is known to have bad
  incomplete files.  Add other OS'es with incomplete include files as needed
  */
#if (defined OS_sunos || defined OS_ultrix)
int read();
int write();
int fflush();
char *strdup();
int strcasecmp();
int strncasecmp();
char *getenv();
unsigned long strtoul();
int pclose();
void exit();
char *getpass();
int atoi();
FILE *fdopen();
FILE *popen();
#endif

#ifndef MAXPATHLEN
# ifdef PATH_MAX
#  define MAXPATHLEN PATH_MAX
# else
#  define MAXPATHLEN 1024
# endif
#endif


#ifndef OS_linux
# undef USE_XDF
#endif

#ifdef NO_XDF
# undef USE_XDF
#endif

#ifdef __EMX__
#define INCL_BASE
#define INCL_DOSDEVIOCTL
#include <os2.h>
#endif

#ifdef OS_nextstep
/* nextstep doesn't have this.  Unfortunately, we cannot test its presence
   using AC_EGREP_HEADER, as we don't know _which_ header to test, and in
   the general case utime.h might be non-existent */
struct utimbuf
{
  time_t actime,modtime;
};
#endif

/* NeXTStep doesn't have these */
#if !defined(S_ISREG) && defined (_S_IFMT) && defined (_S_IFREG)
#define S_ISREG(mode)   (((mode) & (_S_IFMT)) == (_S_IFREG))
#endif

#if !defined(S_ISDIR) && defined (_S_IFMT) && defined (_S_IFDIR)
#define S_ISDIR(mode)   (((mode) & (_S_IFMT)) == (_S_IFDIR))
#endif


#ifdef OS_aix
/* AIX has an offset_t time, but somehow it is not scalar ==> forget about it
 */
# undef HAVE_OFFSET_T
#endif

#if (defined (CPU_m68000) && defined (OS_sysv))
/* AT&T UnixPC lacks prototypes for some system functions */
void *calloc(size_t nmemb, size_t size);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int access(const char *pathname, int mode);
int open(const char *path, int oflag, ...);
int close(int fildes);
int lockf(int fd, int cmd, off_t len);
off_t lseek(int fd, off_t offset, int whence);
int ioctl(int fd, unsigned long request, ...);
int fcntl(int fd, int cmd, ...);
int pipe(int pipefd[2]);
int dup(int oldfd);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *statbuf);
#define S_ISREG(x) ((x)&S_IFREG)
#define S_ISDIR(x) ((x)&S_IFDIR)
#define S_ISLNK(x) (0)
int toupper(int c);
int tolower(int c);
void srand48(long seed);
long lrand48(void);
time_t time(time_t *);
long strtol(const char *str, char **ptr, int base);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *s, const char *format, ...);
int vfprintf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int fclose(FILE *stream);
void perror(const char *s);
int fflush(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
void exit(int status);
int fputs(char *, FILE *stream);
int fputc(int c, FILE *stream);
int fgetc(FILE *stream);
int isatty(int fd);
int atexit(void (*function)(void));
int getuid(void);
int geteuid(void);
int getegid(void);
int getgid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
void bcopy(const void *src, void *dest, size_t n);
int _flsbuf(unsigned char x, FILE *p);
pid_t fork(void);
int execl(const char *pathname, const char *arg, ...);
int execvp(const char *file, char *const argv[]);
pid_t wait(int *stat_loc);
int kill(pid_t pid, int sig);
char *getpass(const char *prompt);
int getopt(int argc, char * const argv[], const char *optstring);
char *getenv(const char *name);
char *getlogin(void);
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);
int unlink(const char *pathname);
int access(const char *pathname, int mode);
int mkdir (const char* file, int mode);
unsigned int sleep(unsigned int seconds);
#endif

#ifndef HAVE_LSTAT
#define lstat stat
#endif

#ifdef HAVE_STAT64
#define MT_STAT stat64
#define MT_LSTAT lstat64
#define MT_FSTAT fstat64
#else
#define MT_STAT stat
#define MT_LSTAT lstat
#define MT_FSTAT fstat
#endif


#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef __GNUC__
#ifndef __inline__
#define __inline__ inline
#endif
#endif

#endif
