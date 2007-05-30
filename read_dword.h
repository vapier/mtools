#ifndef READ_DWORD
#define READ_DWORD

static Dword read_dword(int handle) 
{
	Byte val[4];
	
	read(handle, (char *)val, 4);

	return byte2dword(val);
}
#endif
