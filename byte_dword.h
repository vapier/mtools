#ifndef BYTE_DWORD
#define BYTE_DWORD
static Dword byte2dword(Byte* val)
{
	Dword l;
	l = (val[0] << 24) + (val[1] << 16) + (val[2] << 8) + val[3];
	
#ifdef DEBUG
	fprintf(stderr, "byte2dword(): %ld, 0x%x\n", l, (unsigned int) l);
#endif		
	
	return l;
}	

static void dword2byte(Dword parm, Byte* rval)
{
	rval[0] = (parm >> 24) & 0xff;
	rval[1] = (parm >> 16) & 0xff;
	rval[2] = (parm >> 8)  & 0xff;
	rval[3] = parm         & 0xff;
}

#endif
