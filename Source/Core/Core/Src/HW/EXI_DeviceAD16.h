// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _EXIDEVICE_AD16_H
#define _EXIDEVICE_AD16_H

class CEXIAD16 : public IEXIDevice
{
public:
	CEXIAD16();
	virtual void SetCS(int _iCS);
	virtual bool IsPresent();
	virtual void DoState(PointerWrap &p);

private:
	enum 
	{
		init	= 0x00,
		write	= 0xa0,
		read	= 0xa2
	};

	union UAD16Reg
	{
		u32 U32;
		u32 U8[4];
	};

	// STATE_TO_SAVE
	u32 m_uPosition;
	u32 m_uCommand;
	UAD16Reg m_uAD16Register;

	virtual void TransferByte(u8& _uByte);
};

#endif

