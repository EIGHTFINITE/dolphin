// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _JIT64ASM_H
#define _JIT64ASM_H

#include "../JitCommon/JitAsmCommon.h"

// In Dolphin, we don't use inline assembly. Instead, we generate all machine-near
// code at runtime. In the case of fixed code like this, after writing it, we write
// protect the memory, essentially making it work just like precompiled code.

// There are some advantages to this approach:
//   1) No need to setup an external assembler in the build.
//   2) Cross platform, as long as it's x86/x64.
//   3) Can optimize code at runtime for the specific CPU model.
// There aren't really any disadvantages other than having to maintain a x86 emitter,
// which we have to do anyway :)
// 
// To add a new asm routine, just add another const here, and add the code to Generate.
// Also, possibly increase the size of the code buffer.

class Jit64AsmRoutineManager : public CommonAsmRoutines
{
private:
	void Generate();
	void GenerateCommon();

public:
	void Init() {
		AllocCodeSpace(8192);
		Generate();
		WriteProtect();
	}

	void Shutdown() {
		FreeCodeSpace();
	}
};

extern Jit64AsmRoutineManager asm_routines;

#endif  // _JIT64ASM_H
