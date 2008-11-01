/*
 * Various character set conversions used by mtools
 */
#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"

#include <iconv.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "file_name.h"


#ifdef HAVE_ICONV_H
#include <iconv.h>

struct doscp_t {
	iconv_t from;
	iconv_t to;
};

doscp_t *cp_open(int codepage)
{
	char dosCp[6];
	doscp_t *ret;
	iconv_t *from;
	iconv_t *to;

	if(codepage == 0)
		codepage = mtools_default_codepage;
	if(codepage < 0 || codepage > 999) {
		fprintf(stderr, "Bad codepage %d\n", codepage);
		return NULL;
	}
	sprintf(dosCp, "CP%d", codepage);

	from = iconv_open("WCHAR_T", dosCp);
	to   =  iconv_open(dosCp, "WCHAR_T");
	if(from == (iconv_t)-1 || to == (iconv_t)-1) {
		fprintf(stderr, "Bad codepage %d %s\n", codepage,
			strerror(errno));
		return NULL;
	}

	ret = New(doscp_t);
	if(ret == NULL)
		return ret;
	ret->from = from;
	ret->to   = to;
	return ret;
}

int dos_to_wchar(doscp_t *cp, char *dos, wchar_t *wchar, size_t len)
{
	int r;
	size_t in_len=len;
	size_t out_len=len*sizeof(wchar_t);
	wchar_t *dptr=wchar;
	r=iconv(cp->from, &dos, &in_len, (char **)&dptr, &out_len);
	if(r < 0)
		return r;
	*dptr = L'\0';
	return dptr-wchar;
}


void wchar_to_dos(doscp_t *cp,
		  wchar_t *wchar, char *dos, size_t len, int *mangled)
{
	int r;
	size_t in_len=len*sizeof(wchar_t);
	size_t out_len=len;

	while(in_len > 0) {
		r=iconv(cp->to, (char**)&wchar, &in_len, &dos, &out_len);
		if(r >= 0 || errno != EILSEQ) {
			/* everything transformed, or error that is _not_ a bad
			 * character */
			break;
		}
		*mangled = 1;

		if(dos)
			*dos++ = '_';
		in_len--;

		wchar++;
		out_len--;
	}
}

#else

#include "codepage.h"

struct doscp_t {
	unsigned char *from_dos;
	unsigned char to_dos[0x80];
};

doscp_t *cp_open(int codepage)
{
	doscp_t *ret;
	int i;
	Codepage_t *cp;

	if(codepage == 0)
		codepage = 850;

	ret = New(doscp_t);
	if(ret == NULL)
		return ret;

	for(cp=codepages; cp->nr ; cp++)
		if(cp->nr == codepage) {
			ret->from_dos = cp->tounix;
			break;
		}

	if(ret->from_dos == NULL) {
		fprintf(stderr, "Bad codepage %d\n", codepage);
		free(ret);
		return NULL;
	}

	for(i=0; i<0x80; i++) {
		char native = ret->from_dos[i];
		if(! (native & 0x80))
			continue;
		ret->to_dos[native & 0x7f] = 0x80 | i;
	}
	return ret;
}

int dos_to_wchar(doscp_t *cp, char *dos, wchar_t *wchar, size_t len)
{
	int i;

	for(i=0; i<len && dos[i]; i++) {
		char c = dos[i];
		if(c >= ' ' && c <= '~')
			wchar[i] = c;
		else {
			wchar[i] = cp->from_dos[c & 0x7f];
		}
	}
	wchar[i] = '\0';
	return i;
}


void wchar_to_dos(doscp_t *cp,
		  wchar_t *wchar, char *dos, size_t len, int *mangled)
{
	int i;
	for(i=0; i<len && wchar[i]; i++) {
		char c = wchar[i];
		if(c >= ' ' && c <= '~')
			dos[i] = c;
		else {
			dos[i] = cp->to_dos[c & 0x7f];
			if(dos[i] == '\0') {
				dos[i]='_';
				*mangled=1;
			}
		}
	}
}

#endif


#ifndef HAVE_WCHAR_H

typedef int mbstate_t;

static inline size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
	*s = wc;
	return 1;
}

static inline size_t mbrtowc(wchar_t *pwc, const char *s, 
			     size_t n, mbstate_t *ps)
{
	*pwc = *s;
	return 1;
}

#endif

/**
 * Convert wchar string to native, converting at most len wchar characters
 * Returns number of generated native characters
 */
int wchar_to_native(const wchar_t *wchar, char *native, size_t len)
{
	int i;
	char *dptr = native;
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	for(i=0; i<len && wchar[i] != 0; i++) {
		int r = wcrtomb(dptr, wchar[i], &ps);
		if(r < 0 && errno == EILSEQ) {
			r=1;
			*dptr='_';
		}
		if(r < 0)
			return r;
		dptr+=r;
	}
	*dptr='\0';
	return dptr-native;
}

/**
 * Convert native string to wchar string, converting at most len wchar
 * characters. If end is supplied, stop conversion when source pointer
 * exceeds end. Returns number of converted wchars
 */
int native_to_wchar(const char *native, wchar_t *wchar, size_t len,
		    char *end, int *mangled)
{
	mbstate_t ps;
	int i;
	memset(&ps, 0, sizeof(ps));

	for(i=0; i<len && (native < end || !end); i++) {
		int r = mbrtowc(wchar+i, native, len, &ps);
		if(r < 0) {
			/* Unconvertible character. Just pretend it's Latin1
			   encoded (if valid Latin1 character) or substitue
			   with an underscore if not
			*/
			char c = *native;
			if(c >= '\xa0' && c < '\xff')
				wchar[i] = c & 0xff;
			else
				wchar[i] = '_';
			memset(&ps, 0, sizeof(ps));
			r=1;
		}
		if(r == 0)
			break;
		native += r;
	}
	if(mangled && end && native < end)
		*mangled |= 3;
	wchar[i]='\0';
	return i;
}

