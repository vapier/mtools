#ifndef FILE_NAME_H
#define FILE_NAME_H

#include <sysincludes.h>
#include "mtools.h"

/**
 * raw dos-name coming straight from the directory entry
 * MYFILE  TXT
 */
struct dos_name_t {
  char base[8];
  char ext[3];
  char sentinel;
};

int dos_to_wchar(doscp_t *fromDos, char *dos, wchar_t *wchar, size_t len);
void wchar_to_dos(doscp_t *toDos, wchar_t *wchar, char *dos, size_t len, int *mangled);

doscp_t *cp_open(int codepage);

int wchar_to_native(const wchar_t *wchar, char *native, size_t len);
int native_to_wchar(const char *native, wchar_t *wchar, size_t len,
		    char *end, int *mangled);

#endif
