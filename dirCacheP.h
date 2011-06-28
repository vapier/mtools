#ifndef DIRCACHEP_H
#define DIRCACHEP_H

struct dirCacheEntry_t {
	dirCacheEntryType_t type;
	unsigned int beginSlot;
	unsigned int endSlot;
	wchar_t *shortName;
	wchar_t *longName;
	struct directory dir;
	int isAtEnd;
} ;

#endif /* DIRCACHEP_H */
