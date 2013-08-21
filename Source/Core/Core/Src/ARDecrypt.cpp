// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// Most of the code in this file is from:
// GCNcrypt - Gamecube AR Crypto Program
// Copyright (C) 2003-2004 Parasyte


#include "ARDecrypt.h"
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ActionReplay
{

// Alphanumeric filter for text<->bin conversion
const char *filter = "0123456789ABCDEFGHJKMNPQRTUVWXYZILOS";

u32 genseeds[0x20];


const u8 bitstringlen[0x08] = {
	0x06, 0x0A, 0x0C, 0x11, 0x11, 0x08, 0x07, 0x20,
};

const u8 gentable0[0x38] = {
	0x39, 0x31, 0x29, 0x21, 0x19, 0x11, 0x09, 0x01,
	0x3A, 0x32, 0x2A, 0x22, 0x1A, 0x12, 0x0A, 0x02,
	0x3B, 0x33, 0x2B, 0x23, 0x1B, 0x13, 0x0B, 0x03,
	0x3C, 0x34, 0x2C, 0x24, 0x3F, 0x37, 0x2F, 0x27,
	0x1F, 0x17, 0x0F, 0x07, 0x3E, 0x36, 0x2E, 0x26,
	0x1E, 0x16, 0x0E, 0x06, 0x3D, 0x35, 0x2D, 0x25,
	0x1D, 0x15, 0x0D, 0x05, 0x1C, 0x14, 0x0C, 0x04,
};
const u8 gentable1[0x08] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};
const u8 gentable2[0x10] = {
	0x01, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
	0x0F, 0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1C,
};
const u8 gentable3[0x30] = {
	0x0E, 0x11, 0x0B, 0x18, 0x01, 0x05, 0x03, 0x1C,
	0x0F, 0x06, 0x15, 0x0A, 0x17, 0x13, 0x0C, 0x04,
	0x1A, 0x08, 0x10, 0x07, 0x1B, 0x14, 0x0D, 0x02,
	0x29, 0x34, 0x1F, 0x25, 0x2F, 0x37, 0x1E, 0x28,
	0x33, 0x2D, 0x21, 0x30, 0x2C, 0x31, 0x27, 0x38,
	0x22, 0x35, 0x2E, 0x2A, 0x32, 0x24, 0x1D, 0x20,
};

const u16 crctable0[0x10] = {
	0x0000, 0x1081, 0x2102, 0x3183, 0x4204, 0x5285, 0x6306, 0x7387,
	0x8408, 0x9489, 0xA50A, 0xB58B, 0xC60C, 0xD68D, 0xE70E, 0xF78F,
};
const u16 crctable1[0x10] = {
	0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
	0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
};

const u8 gensubtable[0x08] = {
	0x34, 0x1C, 0x84, 0x9E, 0xFD, 0xA4, 0xB6, 0x7B,
};


const u32 table0[0x40] = {
	0x01010400, 0x00000000, 0x00010000, 0x01010404, 0x01010004, 0x00010404, 0x00000004, 0x00010000,
	0x00000400, 0x01010400, 0x01010404, 0x00000400, 0x01000404, 0x01010004, 0x01000000, 0x00000004,
	0x00000404, 0x01000400, 0x01000400, 0x00010400, 0x00010400, 0x01010000, 0x01010000, 0x01000404,
	0x00010004, 0x01000004, 0x01000004, 0x00010004, 0x00000000, 0x00000404, 0x00010404, 0x01000000,
	0x00010000, 0x01010404, 0x00000004, 0x01010000, 0x01010400, 0x01000000, 0x01000000, 0x00000400,
	0x01010004, 0x00010000, 0x00010400, 0x01000004, 0x00000400, 0x00000004, 0x01000404, 0x00010404,
	0x01010404, 0x00010004, 0x01010000, 0x01000404, 0x01000004, 0x00000404, 0x00010404, 0x01010400,
	0x00000404, 0x01000400, 0x01000400, 0x00000000, 0x00010004, 0x00010400, 0x00000000, 0x01010004,
};
const u32 table1[0x40] = {
	0x80108020, 0x80008000, 0x00008000, 0x00108020, 0x00100000, 0x00000020, 0x80100020, 0x80008020,
	0x80000020, 0x80108020, 0x80108000, 0x80000000, 0x80008000, 0x00100000, 0x00000020, 0x80100020,
	0x00108000, 0x00100020, 0x80008020, 0x00000000, 0x80000000, 0x00008000, 0x00108020, 0x80100000,
	0x00100020, 0x80000020, 0x00000000, 0x00108000, 0x00008020, 0x80108000, 0x80100000, 0x00008020,
	0x00000000, 0x00108020, 0x80100020, 0x00100000, 0x80008020, 0x80100000, 0x80108000, 0x00008000,
	0x80100000, 0x80008000, 0x00000020, 0x80108020, 0x00108020, 0x00000020, 0x00008000, 0x80000000,
	0x00008020, 0x80108000, 0x00100000, 0x80000020, 0x00100020, 0x80008020, 0x80000020, 0x00100020,
	0x00108000, 0x00000000, 0x80008000, 0x00008020, 0x80000000, 0x80100020, 0x80108020, 0x00108000,
};
const u32 table2[0x40] = {
	0x00000208, 0x08020200, 0x00000000, 0x08020008, 0x08000200, 0x00000000, 0x00020208, 0x08000200,
	0x00020008, 0x08000008, 0x08000008, 0x00020000, 0x08020208, 0x00020008, 0x08020000, 0x00000208,
	0x08000000, 0x00000008, 0x08020200, 0x00000200, 0x00020200, 0x08020000, 0x08020008, 0x00020208,
	0x08000208, 0x00020200, 0x00020000, 0x08000208, 0x00000008, 0x08020208, 0x00000200, 0x08000000,
	0x08020200, 0x08000000, 0x00020008, 0x00000208, 0x00020000, 0x08020200, 0x08000200, 0x00000000,
	0x00000200, 0x00020008, 0x08020208, 0x08000200, 0x08000008, 0x00000200, 0x00000000, 0x08020008,
	0x08000208, 0x00020000, 0x08000000, 0x08020208, 0x00000008, 0x00020208, 0x00020200, 0x08000008,
	0x08020000, 0x08000208, 0x00000208, 0x08020000, 0x00020208, 0x00000008, 0x08020008, 0x00020200,
};
const u32 table3[0x40] = {
	0x00802001, 0x00002081, 0x00002081, 0x00000080, 0x00802080, 0x00800081, 0x00800001, 0x00002001,
	0x00000000, 0x00802000, 0x00802000, 0x00802081, 0x00000081, 0x00000000, 0x00800080, 0x00800001,
	0x00000001, 0x00002000, 0x00800000, 0x00802001, 0x00000080, 0x00800000, 0x00002001, 0x00002080,
	0x00800081, 0x00000001, 0x00002080, 0x00800080, 0x00002000, 0x00802080, 0x00802081, 0x00000081,
	0x00800080, 0x00800001, 0x00802000, 0x00802081, 0x00000081, 0x00000000, 0x00000000, 0x00802000,
	0x00002080, 0x00800080, 0x00800081, 0x00000001, 0x00802001, 0x00002081, 0x00002081, 0x00000080,
	0x00802081, 0x00000081, 0x00000001, 0x00002000, 0x00800001, 0x00002001, 0x00802080, 0x00800081,
	0x00002001, 0x00002080, 0x00800000, 0x00802001, 0x00000080, 0x00800000, 0x00002000, 0x00802080,
};
const u32 table4[0x40] = {
	0x00000100, 0x02080100, 0x02080000, 0x42000100, 0x00080000, 0x00000100, 0x40000000, 0x02080000,
	0x40080100, 0x00080000, 0x02000100, 0x40080100, 0x42000100, 0x42080000, 0x00080100, 0x40000000,
	0x02000000, 0x40080000, 0x40080000, 0x00000000, 0x40000100, 0x42080100, 0x42080100, 0x02000100,
	0x42080000, 0x40000100, 0x00000000, 0x42000000, 0x02080100, 0x02000000, 0x42000000, 0x00080100,
	0x00080000, 0x42000100, 0x00000100, 0x02000000, 0x40000000, 0x02080000, 0x42000100, 0x40080100,
	0x02000100, 0x40000000, 0x42080000, 0x02080100, 0x40080100, 0x00000100, 0x02000000, 0x42080000,
	0x42080100, 0x00080100, 0x42000000, 0x42080100, 0x02080000, 0x00000000, 0x40080000, 0x42000000,
	0x00080100, 0x02000100, 0x40000100, 0x00080000, 0x00000000, 0x40080000, 0x02080100, 0x40000100,
};
const u32 table5[0x40] = {
	0x20000010, 0x20400000, 0x00004000, 0x20404010, 0x20400000, 0x00000010, 0x20404010, 0x00400000,
	0x20004000, 0x00404010, 0x00400000, 0x20000010, 0x00400010, 0x20004000, 0x20000000, 0x00004010,
	0x00000000, 0x00400010, 0x20004010, 0x00004000, 0x00404000, 0x20004010, 0x00000010, 0x20400010,
	0x20400010, 0x00000000, 0x00404010, 0x20404000, 0x00004010, 0x00404000, 0x20404000, 0x20000000,
	0x20004000, 0x00000010, 0x20400010, 0x00404000, 0x20404010, 0x00400000, 0x00004010, 0x20000010,
	0x00400000, 0x20004000, 0x20000000, 0x00004010, 0x20000010, 0x20404010, 0x00404000, 0x20400000,
	0x00404010, 0x20404000, 0x00000000, 0x20400010, 0x00000010, 0x00004000, 0x20400000, 0x00404010,
	0x00004000, 0x00400010, 0x20004010, 0x00000000, 0x20404000, 0x20000000, 0x00400010, 0x20004010,
};
const u32 table6[0x40] = {
	0x00200000, 0x04200002, 0x04000802, 0x00000000, 0x00000800, 0x04000802, 0x00200802, 0x04200800,
	0x04200802, 0x00200000, 0x00000000, 0x04000002, 0x00000002, 0x04000000, 0x04200002, 0x00000802,
	0x04000800, 0x00200802, 0x00200002, 0x04000800, 0x04000002, 0x04200000, 0x04200800, 0x00200002,
	0x04200000, 0x00000800, 0x00000802, 0x04200802, 0x00200800, 0x00000002, 0x04000000, 0x00200800,
	0x04000000, 0x00200800, 0x00200000, 0x04000802, 0x04000802, 0x04200002, 0x04200002, 0x00000002,
	0x00200002, 0x04000000, 0x04000800, 0x00200000, 0x04200800, 0x00000802, 0x00200802, 0x04200800,
	0x00000802, 0x04000002, 0x04200802, 0x04200000, 0x00200800, 0x00000000, 0x00000002, 0x04200802,
	0x00000000, 0x00200802, 0x04200000, 0x00000800, 0x04000002, 0x04000800, 0x00000800, 0x00200002,
};
const u32 table7[0x40] = {
	0x10001040, 0x00001000, 0x00040000, 0x10041040, 0x10000000, 0x10001040, 0x00000040, 0x10000000,
	0x00040040, 0x10040000, 0x10041040, 0x00041000, 0x10041000, 0x00041040, 0x00001000, 0x00000040,
	0x10040000, 0x10000040, 0x10001000, 0x00001040, 0x00041000, 0x00040040, 0x10040040, 0x10041000,
	0x00001040, 0x00000000, 0x00000000, 0x10040040, 0x10000040, 0x10001000, 0x00041040, 0x00040000,
	0x00041040, 0x00040000, 0x10041000, 0x00001000, 0x00000040, 0x10040040, 0x00001000, 0x00041040,
	0x10001000, 0x00000040, 0x10000040, 0x10040000, 0x10040040, 0x10000000, 0x00040000, 0x10001040,
	0x00000000, 0x10041040, 0x00040040, 0x10000040, 0x10040000, 0x10001000, 0x10001040, 0x00000000,
	0x10041040, 0x00041000, 0x00041000, 0x00001040, 0x00001040, 0x00040040, 0x10000000, 0x10041000,
};


void generateseeds(u32 *seeds, const u8 *seedtable, u8 doreverse)
{
	int i,j;
	u32 tmp3;
	u8 array0[0x38],array1[0x38],array2[0x08];
	u8 tmp,tmp2;

	i = 0;
	while (i < 0x38)
	{
		tmp = (gentable0[i] - 1);
		array0[i++] = ((u32)(0-(seedtable[tmp>>3] & gentable1[tmp&7])) >> 31);
	}

	i = 0;
	while (i < 0x10)
	{
		memset(array2,0,8);
		tmp2 = gentable2[i];

		for (j = 0; j < 0x38; j++)
		{
			tmp = (tmp2+j);

			if (j > 0x1B)
			{
				if (tmp > 0x37) tmp-=0x1C;
			}
			else if (tmp > 0x1B) tmp-=0x1C;

			array1[j] = array0[tmp];
		}
		for (j = 0; j < 0x30; j++)
		{
			if (!array1[gentable3[j]-1]) continue;
			tmp = (((j*0x2AAB)>>16) - (j>>0x1F));
			array2[tmp] |= (gentable1[j-(tmp*6)]>>2);
		}
		seeds[i<<1] = ((array2[0]<<24)|(array2[2]<<16)|(array2[4]<<8)|array2[6]);
		seeds[(i<<1)+1] = ((array2[1]<<24)|(array2[3]<<16)|(array2[5]<<8)|array2[7]);
		i++;
	}

	if (!doreverse)
	{
		j = 0x1F;
		for (i = 0; i < 16; i+=2)
		{
			tmp3 = seeds[i];
			seeds[i] = seeds[j-1];
			seeds[j-1] = tmp3;

			tmp3 = seeds[i+1];
			seeds[i+1] = seeds[j];
			seeds[j] = tmp3;
			j-=2;
		}
	}
}

void buildseeds()
{
	generateseeds(genseeds,gensubtable,0);
}

void getcode(u32 *src, u32 *addr, u32 *val)
{
	*addr = Common::swap32(src[0]);
	*val = Common::swap32(src[1]);
}

void setcode(u32 *dst, u32 addr, u32 val)
{
	dst[0] = Common::swap32(addr);
	dst[1] = Common::swap32(val);
}

u16 gencrc16(u32 *codes, u16 size)
{
	u16 ret=0;
	u8 tmp=0,tmp2;
	int i;

	if (size > 0)
	{
		while (tmp < size)
		{
			for (i = 0; i < 4; i++)
			{
				tmp2 = ((codes[tmp] >> (i<<3))^ret);
				ret = ((crctable0[(tmp2>>4)&0x0F]^crctable1[tmp2&0x0F])^(ret>>8));
			}
			tmp++;
		}
	}
	return ret;
}

u8 verifycode(u32 *codes, u16 size)
{
	u16 tmp;

	tmp = gencrc16(codes,size);
	return (((tmp>>12)^(tmp>>8)^(tmp>>4)^tmp)&0x0F);
}

void unscramble1(u32 *addr, u32 *val)
{
	u32 tmp;

	*val = _rotl(*val,4);

	tmp = ((*addr^*val)&0xF0F0F0F0);
	*addr ^= tmp;
	*val = _rotr((*val^tmp),0x14);

	tmp = ((*addr^*val)&0xFFFF0000);
	*addr ^= tmp;
	*val = _rotr((*val^tmp),0x12);

	tmp = ((*addr^*val)&0x33333333);
	*addr ^= tmp;
	*val = _rotr((*val^tmp),6);

	tmp = ((*addr^*val)&0x00FF00FF);
	*addr ^= tmp;
	*val = _rotl((*val^tmp),9);

	tmp = ((*addr^*val)&0xAAAAAAAA);
	*addr = _rotl((*addr^tmp),1);
	*val ^= tmp;
}

void unscramble2(u32 *addr, u32 *val)
{
	u32 tmp;

	*val = _rotr(*val,1);

	tmp = ((*addr^*val)&0xAAAAAAAA);
	*val ^= tmp;
	*addr = _rotr((*addr^tmp),9);

	tmp = ((*addr^*val)&0x00FF00FF);
	*val ^= tmp;
	*addr = _rotl((*addr^tmp),6);

	tmp = ((*addr^*val)&0x33333333);
	*val ^= tmp;
	*addr = _rotl((*addr^tmp),0x12);

	tmp = ((*addr^*val)&0xFFFF0000);
	*val ^= tmp;
	*addr = _rotl((*addr^tmp),0x14);

	tmp = ((*addr^*val)&0xF0F0F0F0);
	*val ^= tmp;
	*addr = _rotr((*addr^tmp),4);
}

void decryptcode(u32 *seeds, u32 *code)
{
	u32 addr,val;
	u32 tmp,tmp2;
	int i=0;

	getcode(code,&addr,&val);
	unscramble1(&addr,&val);
	while (i < 32)
	{
		tmp = (_rotr(val,4)^seeds[i++]);
		tmp2 = (val^seeds[i++]);
		addr ^= (table6[tmp&0x3F]^table4[(tmp>>8)&0x3F]^table2[(tmp>>16)&0x3F]^table0[(tmp>>24)&0x3F]^table7[tmp2&0x3F]^table5[(tmp2>>8)&0x3F]^table3[(tmp2>>16)&0x3F]^table1[(tmp2>>24)&0x3F]);

		tmp = (_rotr(addr,4)^seeds[i++]);
		tmp2 = (addr^seeds[i++]);
		val ^= (table6[tmp&0x3F]^table4[(tmp>>8)&0x3F]^table2[(tmp>>16)&0x3F]^table0[(tmp>>24)&0x3F]^table7[tmp2&0x3F]^table5[(tmp2>>8)&0x3F]^table3[(tmp2>>16)&0x3F]^table1[(tmp2>>24)&0x3F]);
	}
	unscramble2(&addr,&val);
	setcode(code,val,addr);
}

bool getbitstring(u32 *ctrl, u32 *out, u8 len)
{
	u32 tmp=(ctrl[0]+(ctrl[1]<<2));

	*out = 0;
	while (len--)
	{
		if (ctrl[2] > 0x1F)
		{
			ctrl[2] = 0;
			ctrl[1]++;
			tmp = (ctrl[0]+(ctrl[1]<<2));
		}
		if (ctrl[1] >= ctrl[3]) return false;
		*out = ((*out<<1) | ((tmp >> (0x1F-ctrl[2])) & 1));
		ctrl[2]++;
	}
	return true;
}

bool batchdecrypt(u32 *codes, u16 size)
{
	u32 tmp,*ptr=codes;
	u32 tmparray[4] = { 0 },tmparray2[8] = { 0 };

	// Not required
	//if (size & 1) return 0;
	//if (!size) return 0;

	tmp = (size >> 1);
	while (tmp--)
	{
		decryptcode(genseeds,ptr);
		ptr+=2;
	}

	tmparray[0] = *codes;
	tmparray[1] = 0;
	tmparray[2] = 4; // Skip crc
	tmparray[3] = size;
	getbitstring(tmparray,tmparray2+1,11);	// Game id
	getbitstring(tmparray,tmparray2+2,17);	// Code id
	getbitstring(tmparray,tmparray2+3,1);	// Master code
	getbitstring(tmparray,tmparray2+4,1);	// Unknown
	getbitstring(tmparray,tmparray2+5,2);	// Region

	// Grab gameid and region from the last decrypted code
	// Maybe check this against dolphin's GameID? - "code is for wrong game" type msg
	//gameid = tmparray2[1];
	//region = tmparray2[5];

	tmp = codes[0];
	codes[0] &= 0x0FFFFFFF;
	if ((tmp>>28) != verifycode(codes,size)) return false;

	return true;

	// Unfinished (so says Parasyte :p )
}

int GetVal(const char *flt, char chr)
{
	int ret = (int)(strchr(flt,chr) - flt);
	switch (ret)
	{
	case 32: // 'I'
	case 33: // 'L'
		ret = 1;
		break;
	case 34: // 'O'
		ret = 0;
		break;
	case 35: // 'S'
		ret = 5;
		break;
	}
	return ret;
}

int alphatobin(u32 *dst, std::vector<std::string> alpha, int size)
{
	int i,j=0,k;
	int ret=0,org=(size+1);
	u32 bin[2];
	u8 parity;

	while (size)
	{
		bin[0]=0;
		for (i = 0; i < 6; i++)
		{
			bin[0] |= (GetVal(filter,alpha[j>>1][i]) << (((5-i)*5)+2));
		}
		bin[0] |= (GetVal(filter,alpha[j>>1][6]) >> 3);
		dst[j++] = bin[0];

		bin[1]=0;
		for (i = 0; i < 6; i++)
		{
			bin[1] |= (GetVal(filter,alpha[j>>1][i+6]) << (((5-i)*5)+4));
		}
		bin[1] |= (GetVal(filter,alpha[j>>1][12]) >> 1);
		dst[j++] = bin[1];

		//verify parity bit
		k=0;
		parity=0;
		for (i = 0; i < 64; i++)
		{
			if (i == 32) k++;
			parity ^= (bin[k] >> (i-(k<<5)));
		}
		if ((parity&1) != (GetVal(filter,alpha[(j-2)>>1][12])&1)) ret=(org-size);

		size--;
	}

	return ret;
}

void DecryptARCode(std::vector<std::string> vCodes, std::vector<AREntry> &ops)
{
	// The almighty buildseeds() function!! without this, the crypto routines are useless
	buildseeds();

	u32 uCodes[1200];
	u32 i,ret;

	for(i = 0; i < vCodes.size(); ++i)
	{
		transform(vCodes[i].begin(), vCodes[i].end(), vCodes[i].begin(), toupper);
		//PanicAlert("Encrypted AR Code\n%s", vCodes[i].c_str());
	}

	if ((ret=alphatobin(uCodes, vCodes, (int)vCodes.size())))
	{
		PanicAlertT("Action Replay Code Decryption Error:\nParity Check Failed\n\nCulprit Code:\n%s", vCodes[ret].c_str());
		batchdecrypt(uCodes, (u16)vCodes.size()<<1);
	}
	else if (!batchdecrypt(uCodes, (u16)vCodes.size()<<1))
	{
		// Commented out since we just send the code anyways and hope for the best XD
		//PanicAlert("Action Replay Code Decryption Error:\nCRC Check Failed\n\n"
		//	"First Code in Block(should be verification code):\n%s", vCodes[0].c_str());

		for (i = 0; i < (vCodes.size()<<1); i+=2)
		{
			AREntry op;
			op.cmd_addr = uCodes[i];
			op.value = uCodes[i+1];
			ops.push_back(op);
			//PanicAlert("Decrypted AR Code without verification code:\n%08X %08X", uCodes[i], uCodes[i+1]);
		}
	}
	else
	{
		// Skip passing the verification code back
		for (i = 2; i < (vCodes.size()<<1); i+=2)
		{
			AREntry op;
			op.cmd_addr = uCodes[i];
			op.value = uCodes[i+1];
			ops.push_back(op);
			//PanicAlert("Decrypted AR Code:\n%08X %08X", uCodes[i], uCodes[i+1]);
		}
	}
}

} //namespace ActionReplay
