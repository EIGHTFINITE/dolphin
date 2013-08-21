// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _JITASMCOMMON_H
#define _JITASMCOMMON_H

#include "../JitCommon/Jit_Util.h"
#include "Thunk.h"

class CommonAsmRoutinesBase  {
public:

	const u8 *fifoDirectWrite8;
	const u8 *fifoDirectWrite16;
	const u8 *fifoDirectWrite32;
	const u8 *fifoDirectWriteFloat;
	const u8 *fifoDirectWriteXmm64;

	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;
	const u8 *dispatcherPcInEAX;

	const u8 *fpException;
	const u8 *testExceptions;
	const u8 *testExternalExceptions;
	const u8 *dispatchPcInEAX;
	const u8 *doTiming;

	const u8 *breakpointBailout;

	// In: array index: GQR to use.
	// In: ECX: Address to read from.
	// Out: XMM0: Bottom two 32-bit slots hold the read value,
	//            converted to a pair of floats.
	// Trashes: EAX ECX EDX
	const u8 **pairedLoadQuantized;

	// In: array index: GQR to use.
	// In: ECX: Address to write to.
	// In: XMM0: Bottom two 32-bit slots hold the pair of floats to be written.
	// Out: Nothing.
	// Trashes: EAX ECX EDX
	const u8 **pairedStoreQuantized;

	// In: array index: GQR to use.
	// In: ECX: Address to write to.
	// In: XMM0: Bottom 32-bit slot holds the float to be written.
	const u8 **singleStoreQuantized;

};

class CommonAsmRoutines : public CommonAsmRoutinesBase, public EmuCodeBlock
{
protected:
	void GenQuantizedLoads();
	void GenQuantizedStores();
	void GenQuantizedSingleStores();

public:
	void GenFifoWrite(int size);
	void GenFifoXmm64Write();
	void GenFifoFloatWrite();

private:
	ThunkManager thunks;

};

#endif
