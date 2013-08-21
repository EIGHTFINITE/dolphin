// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "DSPJitRegCache.h"
#include "../DSPEmitter.h"
#include "../DSPMemoryMap.h"

using namespace Gen;

static void *reg_ptr(int reg)
{
	switch(reg)
	{
	case DSP_REG_AR0:
	case DSP_REG_AR1:
	case DSP_REG_AR2:
	case DSP_REG_AR3:
		return &g_dsp.r.ar[reg - DSP_REG_AR0];
	case DSP_REG_IX0:
	case DSP_REG_IX1:
	case DSP_REG_IX2:
	case DSP_REG_IX3:
		return &g_dsp.r.ix[reg - DSP_REG_IX0];
	case DSP_REG_WR0:
	case DSP_REG_WR1:
	case DSP_REG_WR2:
	case DSP_REG_WR3:
		return &g_dsp.r.wr[reg - DSP_REG_WR0];
	case DSP_REG_ST0:
	case DSP_REG_ST1:
	case DSP_REG_ST2:
	case DSP_REG_ST3:
		return &g_dsp.r.st[reg - DSP_REG_ST0];
	case DSP_REG_ACH0:
	case DSP_REG_ACH1:
		return &g_dsp.r.ac[reg - DSP_REG_ACH0].h;
	case DSP_REG_CR:     return &g_dsp.r.cr;
	case DSP_REG_SR:     return &g_dsp.r.sr;
	case DSP_REG_PRODL:  return &g_dsp.r.prod.l;
	case DSP_REG_PRODM:  return &g_dsp.r.prod.m;
	case DSP_REG_PRODH:  return &g_dsp.r.prod.h;
	case DSP_REG_PRODM2: return &g_dsp.r.prod.m2;
	case DSP_REG_AXL0:
	case DSP_REG_AXL1:
		return &g_dsp.r.ax[reg - DSP_REG_AXL0].l;
	case DSP_REG_AXH0:
	case DSP_REG_AXH1:
		return &g_dsp.r.ax[reg - DSP_REG_AXH0].h;
	case DSP_REG_ACL0:
	case DSP_REG_ACL1:
		return &g_dsp.r.ac[reg - DSP_REG_ACL0].l;
	case DSP_REG_ACM0:
	case DSP_REG_ACM1:
		return &g_dsp.r.ac[reg - DSP_REG_ACM0].m;
	case DSP_REG_AX0_32:
	case DSP_REG_AX1_32:
		return &g_dsp.r.ax[reg - DSP_REG_AX0_32].val;
#ifdef _M_X64
	case DSP_REG_ACC0_64:
	case DSP_REG_ACC1_64:
		return &g_dsp.r.ac[reg - DSP_REG_ACC0_64].val;
	case DSP_REG_PROD_64:
		return &g_dsp.r.prod.val;
#endif
	default:
		_assert_msg_(DSPLLE, 0, "cannot happen");
		return NULL;
	}
}

#define STATIC_REG_ACCS
//#undef STATIC_REG_ACCS

DSPJitRegCache::DSPJitRegCache(DSPEmitter &_emitter)
	: emitter(_emitter), temporary(false), merged(false)
{
	for(unsigned int i = 0; i < NUMXREGS; i++)
	{
		xregs[i].guest_reg = DSP_REG_STATIC;
		xregs[i].pushed = false;
	}

	xregs[RAX].guest_reg = DSP_REG_STATIC;// reserved for MUL/DIV
	xregs[RDX].guest_reg = DSP_REG_STATIC;// reserved for MUL/DIV
	xregs[RCX].guest_reg = DSP_REG_STATIC;// reserved for shifts

	xregs[RBX].guest_reg = DSP_REG_STATIC;//extended op backing store

	xregs[RSP].guest_reg = DSP_REG_STATIC;//stack pointer

	xregs[RBP].guest_reg = DSP_REG_NONE;//definitely usable in dsplle because
										//all external calls are protected

	xregs[RSI].guest_reg = DSP_REG_NONE;
	xregs[RDI].guest_reg = DSP_REG_NONE;

#ifdef _M_X64
#ifdef STATIC_REG_ACCS
	xregs[R8].guest_reg = DSP_REG_STATIC;//acc0
	xregs[R9].guest_reg = DSP_REG_STATIC;//acc1
#else
	xregs[R8].guest_reg = DSP_REG_NONE;
	xregs[R9].guest_reg = DSP_REG_NONE;
#endif
	xregs[R10].guest_reg = DSP_REG_NONE;
	xregs[R11].guest_reg = DSP_REG_NONE;
	xregs[R12].guest_reg = DSP_REG_NONE;
	xregs[R13].guest_reg = DSP_REG_NONE;
	xregs[R14].guest_reg = DSP_REG_NONE;
	xregs[R15].guest_reg = DSP_REG_NONE;
#endif

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		regs[i].mem = reg_ptr(i);
		regs[i].size = 0;
		regs[i].dirty = false;
		regs[i].used = false;
		regs[i].last_use_ctr = -1;
		regs[i].parentReg = DSP_REG_NONE;
		regs[i].shift = 0;
		regs[i].host_reg = INVALID_REG;

		regs[i].loc = M(regs[i].mem);
	}

	for(unsigned int i = 0; i < 32; i++)
		regs[i].size = 2;
	//special composite registers
#ifdef _M_X64
#ifdef STATIC_REG_ACCS
	regs[DSP_REG_ACC0_64].host_reg = R8;
	regs[DSP_REG_ACC1_64].host_reg = R9;
#endif
	for(unsigned int i = 0; i < 2; i++)
	{
		regs[i+DSP_REG_ACC0_64].size = 8;
		regs[i+DSP_REG_ACL0].parentReg = i+DSP_REG_ACC0_64;
		regs[i+DSP_REG_ACM0].parentReg = i+DSP_REG_ACC0_64;
		regs[i+DSP_REG_ACH0].parentReg = i+DSP_REG_ACC0_64;
		regs[i+DSP_REG_ACL0].shift = 0;
		regs[i+DSP_REG_ACM0].shift = 16;
		regs[i+DSP_REG_ACH0].shift = 32;
	}

	regs[DSP_REG_PROD_64].size = 8;
	regs[DSP_REG_PRODL].parentReg = DSP_REG_PROD_64;
	regs[DSP_REG_PRODM].parentReg = DSP_REG_PROD_64;
	regs[DSP_REG_PRODH].parentReg = DSP_REG_PROD_64;
	regs[DSP_REG_PRODM2].parentReg = DSP_REG_PROD_64;
	regs[DSP_REG_PRODL].shift = 0;
	regs[DSP_REG_PRODM].shift = 16;
	regs[DSP_REG_PRODH].shift = 32;
	regs[DSP_REG_PRODM2].shift = 48;
#endif

	for(unsigned int i = 0; i < 2; i++)
	{
		regs[i+DSP_REG_AX0_32].size = 4;
		regs[i+DSP_REG_AXL0].parentReg = i+DSP_REG_AX0_32;
		regs[i+DSP_REG_AXH0].parentReg = i+DSP_REG_AX0_32;
		regs[i+DSP_REG_AXL0].shift = 0;
		regs[i+DSP_REG_AXH0].shift = 16;
	}

	use_ctr = 0;
}

DSPJitRegCache::DSPJitRegCache(const DSPJitRegCache &cache)
	: emitter(cache.emitter), temporary(true), merged(false)
{
	memcpy(xregs,cache.xregs,sizeof(xregs));
	memcpy(regs,cache.regs,sizeof(regs));
}

DSPJitRegCache& DSPJitRegCache::operator=(const DSPJitRegCache &cache)
{
	_assert_msg_(DSPLLE, &emitter == &cache.emitter, "emitter does not match");
	_assert_msg_(DSPLLE, temporary, "register cache not temporary??");
	merged = false;
	memcpy(xregs,cache.xregs,sizeof(xregs));
	memcpy(regs,cache.regs,sizeof(regs));

	return *this;
}

DSPJitRegCache::~DSPJitRegCache()
{
	_assert_msg_(DSPLLE, !temporary || merged, "temporary cache not merged");
}

void DSPJitRegCache::drop()
{
	merged = true;
}

void DSPJitRegCache::flushRegs(DSPJitRegCache &cache, bool emit)
{
	cache.merged = true;

	unsigned int i;

	//drop all guest register not used by cache
	for(i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		regs[i].used = false;//used is restored later
		if (regs[i].loc.IsSimpleReg() &&
			!cache.regs[i].loc.IsSimpleReg())
			movToMemory(i);
	}

	//try to move guest regs in the wrong host reg to the correct one
	int movcnt;
	do
	{
		movcnt = 0;
		for(i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
		{
			X64Reg simple = regs[i].loc.GetSimpleReg();
			X64Reg simple_cache = cache.regs[i].loc.GetSimpleReg();

			if (simple_cache != simple && xregs[simple_cache].guest_reg == DSP_REG_NONE)
			{
				movToHostReg(i, simple_cache, true);
				movcnt++;
			}
		}
	} while (movcnt != 0);

	//free all host regs that are not used for the same guest reg
	for(i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (cache.regs[i].loc.GetSimpleReg() !=
			regs[i].loc.GetSimpleReg() &&
			regs[i].loc.IsSimpleReg())
			movToMemory(i);
	}

	//load all guest regs that are in memory and should be in host reg
	for(i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (cache.regs[i].loc.IsSimpleReg())
		{
			movToHostReg(i, cache.regs[i].loc.GetSimpleReg(), true);
			rotateHostReg(i, cache.regs[i].shift, true);
		}
		else if(cache.regs[i].loc.IsImm())
		{
			// TODO: Immediates?
		}

		regs[i].used = cache.regs[i].used;
		regs[i].dirty |= cache.regs[i].dirty;
		regs[i].last_use_ctr = cache.regs[i].last_use_ctr;
	}

	//sync the freely used xregs
	if (!emit) {
		for(i = 0; i < NUMXREGS; i++) {
			if (cache.xregs[i].guest_reg == DSP_REG_USED &&
			    xregs[i].guest_reg == DSP_REG_NONE)
				xregs[i].guest_reg = DSP_REG_USED;
			if (cache.xregs[i].guest_reg == DSP_REG_NONE &&
			    xregs[i].guest_reg == DSP_REG_USED)
				xregs[i].guest_reg = DSP_REG_NONE;
		}
	}

	//consistency checks
	for(i = 0; i < NUMXREGS; i++)
	{
		_assert_msg_(DSPLLE,
			     xregs[i].guest_reg == cache.xregs[i].guest_reg,
			     "cache and current xreg guest_reg mismatch for %d", i);
	}

	for(i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		_assert_msg_(DSPLLE,
			     regs[i].loc.IsImm() == cache.regs[i].loc.IsImm(),
			     "cache and current reg loc mismatch for %x", i);
		_assert_msg_(DSPLLE,
			     regs[i].loc.GetSimpleReg() == cache.regs[i].loc.GetSimpleReg(),
			     "cache and current reg loc mismatch for %x", i);
		_assert_msg_(DSPLLE,
			     regs[i].dirty || !cache.regs[i].dirty,
			     "cache and current reg dirty mismatch for %x", i);
		_assert_msg_(DSPLLE,
			     regs[i].used == cache.regs[i].used,
			     "cache and current reg used mismatch for %x", i);
		_assert_msg_(DSPLLE,
			     regs[i].shift == cache.regs[i].shift,
			     "cache and current reg shift mismatch for %x", i);
	}

	use_ctr = cache.use_ctr;
}

void DSPJitRegCache::flushMemBackedRegs()
{
	//also needs to undo any dynamic changes to static allocated regs
	//this should have the same effect as
	//merge(DSPJitRegCache(emitter));

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		_assert_msg_(DSPLLE, !regs[i].used,
			     "register %x still in use", i);

		if (regs[i].used)
			emitter.INT3();

		if (regs[i].host_reg != INVALID_REG)
		{
			movToHostReg(i,regs[i].host_reg,true);
			rotateHostReg(i, 0, true);
		}
		else if (regs[i].parentReg == DSP_REG_NONE)
		{
			movToMemory(i);
		}
	}
}

void DSPJitRegCache::flushRegs()
{
	flushMemBackedRegs();

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (regs[i].host_reg != INVALID_REG)
			movToMemory(i);
	}

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		_assert_msg_(DSPLLE,
			     !regs[i].loc.IsSimpleReg(),
			     "register %x is still a simple reg", i);
	}

	_assert_msg_(DSPLLE,
		     xregs[RSP].guest_reg == DSP_REG_STATIC,
		     "wrong xreg state for %d", RSP);
	_assert_msg_(DSPLLE,
		     xregs[RBX].guest_reg == DSP_REG_STATIC,
		     "wrong xreg state for %d", RBX);
	_assert_msg_(DSPLLE,
		     xregs[RBP].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", RBP);
	_assert_msg_(DSPLLE,
		     xregs[RSI].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", RSI);
	_assert_msg_(DSPLLE,
		     xregs[RDI].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", RDI);
#ifdef _M_X64
#ifdef STATIC_REG_ACCS
	_assert_msg_(DSPLLE,
		     xregs[R8].guest_reg == DSP_REG_STATIC,
		     "wrong xreg state for %d", R8);
	_assert_msg_(DSPLLE,
		     xregs[R9].guest_reg == DSP_REG_STATIC,
		     "wrong xreg state for %d", R9);
#else
	_assert_msg_(DSPLLE,
		     xregs[R8].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R8);
	_assert_msg_(DSPLLE,
		     xregs[R9].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R9);
#endif
	_assert_msg_(DSPLLE,
		     xregs[R10].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R10);
	_assert_msg_(DSPLLE,
		     xregs[R11].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R11);
	_assert_msg_(DSPLLE,
		     xregs[R12].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R12);
	_assert_msg_(DSPLLE,
		     xregs[R13].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R13);
	_assert_msg_(DSPLLE,
		     xregs[R14].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R14);
	_assert_msg_(DSPLLE,
		     xregs[R15].guest_reg == DSP_REG_NONE,
		     "wrong xreg state for %d", R15);
#endif

	use_ctr = 0;
}

static u64 ebp_store;

void DSPJitRegCache::loadRegs(bool emit)
{
	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (regs[i].host_reg != INVALID_REG)
			movToHostReg(i,regs[i].host_reg, emit);
	}

	if (emit)
	{
#ifdef _M_X64
		emitter.MOV(64, M(&ebp_store), R(RBP));
#else
		emitter.MOV(32, M(&ebp_store), R(EBP));
#endif
	}
}

void DSPJitRegCache::saveRegs()
{
	flushRegs();

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (regs[i].host_reg != INVALID_REG)
			movToMemory(i);
	}

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		_assert_msg_(DSPLLE,
			     !regs[i].loc.IsSimpleReg(),
			     "register %x is still a simple reg", i);
	}

#ifdef _M_X64
	emitter.MOV(64, R(RBP), M(&ebp_store));
#else
	emitter.MOV(32, R(EBP), M(&ebp_store));
#endif
}

void DSPJitRegCache::pushRegs()
{
	flushMemBackedRegs();

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (regs[i].host_reg != INVALID_REG)
			movToMemory(i);
	}

	int push_count = 0;
	for(unsigned int i = 0; i < NUMXREGS; i++)
	{
		if (xregs[i].guest_reg == DSP_REG_USED)
			push_count++;
	}

	//hardcoding alignment to 16 bytes
#ifdef _M_X64
	if (push_count & 1)
		emitter.SUB(64,R(RSP),Imm32(8));
#else
	if (push_count & 3)
		emitter.SUB(32,R(ESP),Imm32(16 - 4 * (push_count & 3)));
#endif

	for(unsigned int i = 0; i < NUMXREGS; i++)
	{
		if (xregs[i].guest_reg == DSP_REG_USED)
		{
			emitter.PUSH((X64Reg)i);
			xregs[i].pushed = true;
			xregs[i].guest_reg = DSP_REG_NONE;
		}
	}

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		_assert_msg_(DSPLLE,
			     !regs[i].loc.IsSimpleReg(),
			     "register %x is still a simple reg", i);
	}

	for(unsigned int i = 0; i < NUMXREGS; i++)
	{
		_assert_msg_(DSPLLE,
			     xregs[i].guest_reg == DSP_REG_NONE ||
			     xregs[i].guest_reg == DSP_REG_STATIC,
			     "register %x is still used", i);
	}

#ifdef _M_X64
	emitter.MOV(64, R(RBP), M(&ebp_store));
#else
	emitter.MOV(32, R(EBP), M(&ebp_store));
#endif
}

void DSPJitRegCache::popRegs() {
#ifdef _M_X64
	emitter.MOV(64, M(&ebp_store), R(RBP));
#else
	emitter.MOV(32, M(&ebp_store), R(EBP));
#endif
	int push_count = 0;
	for(unsigned int i = 0; i < NUMXREGS; i++)
	{
		if (xregs[i].pushed)
			push_count++;
	}

	for(int i = NUMXREGS-1; i >= 0; i--)
	{
		if (xregs[i].pushed)
		{
			emitter.POP((X64Reg)i);
			xregs[i].pushed = false;
			xregs[i].guest_reg = DSP_REG_USED;
		}
	}

	//hardcoding alignment to 16 bytes
#ifdef _M_X64
	if (push_count & 1)
		emitter.ADD(64,R(RSP),Imm32(8));
#else
	if (push_count & 3)
		emitter.ADD(32,R(ESP),Imm32(16 - 4 * (push_count & 3)));
#endif

	for(unsigned int i = 0; i <= DSP_REG_MAX_MEM_BACKED; i++)
	{
		if (regs[i].host_reg != INVALID_REG)
			movToHostReg(i,regs[i].host_reg, true);
	}
}

X64Reg DSPJitRegCache::makeABICallSafe(X64Reg reg)
{
	if (reg != RBP)
	{
		return reg;
	}

	int rbp_guest = xregs[RBP].guest_reg;
	xregs[RBP].guest_reg = DSP_REG_USED;
	X64Reg safe = findSpillFreeXReg();
	_assert_msg_(DSPLLE, safe != INVALID_REG, "could not find register");
	if (safe == INVALID_REG)
		emitter.INT3();
	xregs[RBP].guest_reg = rbp_guest;
#ifdef _M_X64
	emitter.MOV(64,R(safe),R(reg));
#else
	emitter.MOV(32,R(safe),R(reg));
#endif
	return safe;
}

void DSPJitRegCache::movToHostReg(int reg, X64Reg host_reg, bool load)
{
	_assert_msg_(DSPLLE, reg >= 0 && reg <= DSP_REG_MAX_MEM_BACKED,
		     "bad register name %x", reg);
	_assert_msg_(DSPLLE, regs[reg].parentReg == DSP_REG_NONE,
		     "register %x is proxy for %x", reg, regs[reg].parentReg);
	_assert_msg_(DSPLLE, !regs[reg].used,
		     "moving to host reg in use guest reg %x!", reg);
	X64Reg old_reg = regs[reg].loc.GetSimpleReg();
	if (old_reg == host_reg)
		return;

	if (xregs[host_reg].guest_reg != DSP_REG_STATIC)
		xregs[host_reg].guest_reg = reg;

	if (load)
	{
		switch(regs[reg].size)
		{
		case 2:
			emitter.MOV(16, R(host_reg), regs[reg].loc); break;
		case 4:
			emitter.MOV(32, R(host_reg), regs[reg].loc); break;
#ifdef _M_X64
		case 8:
			emitter.MOV(64, R(host_reg), regs[reg].loc); break;
#endif
		default:
			_assert_msg_(DSPLLE, 0, "unsupported memory size");
			break;
		}
	}

	regs[reg].loc = R(host_reg);
	if (old_reg != INVALID_REG &&
		xregs[old_reg].guest_reg != DSP_REG_STATIC)
		xregs[old_reg].guest_reg = DSP_REG_NONE;
}

void DSPJitRegCache::movToHostReg(int reg, bool load)
{
	_assert_msg_(DSPLLE, reg >= 0 && reg <= DSP_REG_MAX_MEM_BACKED,
		     "bad register name %x", reg);
	_assert_msg_(DSPLLE, regs[reg].parentReg == DSP_REG_NONE,
		     "register %x is proxy for %x", reg, regs[reg].parentReg);
	_assert_msg_(DSPLLE, !regs[reg].used,
		     "moving to host reg in use guest reg %x!", reg);

	if (regs[reg].loc.IsSimpleReg())
		return;

	X64Reg tmp;
	if (regs[reg].host_reg != INVALID_REG)
		tmp = regs[reg].host_reg;
	else
		tmp = findSpillFreeXReg();
	
	if (tmp == INVALID_REG)
		return;

	movToHostReg(reg, tmp, load);
}

void DSPJitRegCache::rotateHostReg(int reg, int shift, bool emit)
{
	_assert_msg_(DSPLLE, reg >= 0 && reg <= DSP_REG_MAX_MEM_BACKED,
		     "bad register name %x", reg);
	_assert_msg_(DSPLLE, regs[reg].parentReg == DSP_REG_NONE,
		     "register %x is proxy for %x", reg, regs[reg].parentReg);
	_assert_msg_(DSPLLE, regs[reg].loc.IsSimpleReg(),
		     "register %x is not a simple reg", reg);
	_assert_msg_(DSPLLE, !regs[reg].used,
		     "rotating in use guest reg %x!", reg);

	if (shift > regs[reg].shift && emit)
	{
		switch(regs[reg].size)
		{
		case 2:
			emitter.ROR(16, regs[reg].loc,
					Imm8(shift - regs[reg].shift));
			break;
		case 4:
			emitter.ROR(32, regs[reg].loc,
					Imm8(shift - regs[reg].shift));
			break;
#ifdef _M_X64
		case 8:
			emitter.ROR(64, regs[reg].loc,
					Imm8(shift - regs[reg].shift));
			break;
#endif
		}
	}
	else if (shift < regs[reg].shift && emit)
	{
		switch(regs[reg].size)
		{
		case 2:
			emitter.ROL(16, regs[reg].loc,
					Imm8(regs[reg].shift - shift));
			break;
		case 4:
			emitter.ROL(32, regs[reg].loc,
					Imm8(regs[reg].shift - shift));
			break;
#ifdef _M_X64
		case 8:
			emitter.ROL(64, regs[reg].loc,
					Imm8(regs[reg].shift - shift));
			break;
#endif
		}
	}
	regs[reg].shift = shift;
}

void DSPJitRegCache::movToMemory(int reg)
{
	_assert_msg_(DSPLLE, reg >= 0 && reg <= DSP_REG_MAX_MEM_BACKED,
		     "bad register name %x", reg);
	_assert_msg_(DSPLLE, regs[reg].parentReg == DSP_REG_NONE,
		     "register %x is proxy for %x", reg, regs[reg].parentReg);
	_assert_msg_(DSPLLE, !regs[reg].used,
		     "moving to memory in use guest reg %x!", reg);

	if (regs[reg].used)
		emitter.INT3();

	if (!regs[reg].loc.IsSimpleReg() &&
	    !regs[reg].loc.IsImm())
		return;

	//but first, check for any needed rotations
	if (regs[reg].loc.IsSimpleReg())
	{
		rotateHostReg(reg, 0, true);
	}
	else
	{
		// TODO: Immediates?
	}

	_assert_msg_(DSPLLE, regs[reg].shift == 0, "still shifted??");

	//move to mem
	OpArg tmp = M(regs[reg].mem);

	if (regs[reg].dirty)
	{
		switch(regs[reg].size)
		{
		case 2:
			emitter.MOV(16, tmp, regs[reg].loc); break;
		case 4:
			emitter.MOV(32, tmp, regs[reg].loc); break;
#ifdef _M_X64
		case 8:
			emitter.MOV(64, tmp, regs[reg].loc); break;
#endif
		default:
			_assert_msg_(DSPLLE, 0, "unsupported memory size");
			break;
		}
		regs[reg].dirty = false;
	}

	if (regs[reg].loc.IsSimpleReg())
	{
		X64Reg hostreg = regs[reg].loc.GetSimpleReg();
		if (xregs[hostreg].guest_reg != DSP_REG_STATIC)
			xregs[hostreg].guest_reg = DSP_REG_NONE;
	}

	regs[reg].last_use_ctr = -1;
	regs[reg].loc = tmp;
}

void DSPJitRegCache::getReg(int reg, OpArg &oparg, bool load)
{
	int real_reg;
	int shift;
	if (regs[reg].parentReg != DSP_REG_NONE)
	{
		real_reg = regs[reg].parentReg;

		// always load and rotate since we need the other
		// parts of the register
		load = true;

		shift = regs[reg].shift;
	}
	else
	{
		real_reg = reg;
		shift = 0;
	}

	_assert_msg_(DSPLLE, !regs[real_reg].used,
		     "register %x already in use", real_reg);

	if (regs[real_reg].used)
		emitter.INT3();
	// no nead to actually emit code for load or rotate if caller doesn't
	// use the contents, but see above for a reason to force the load
	movToHostReg(real_reg, load);

	// TODO: actually handle INVALID_REG
	_assert_msg_(DSPLLE, regs[real_reg].loc.IsSimpleReg(),
		     "did not get host reg for %x", reg);

	rotateHostReg(real_reg, shift, load);
	oparg = regs[real_reg].loc;
	regs[real_reg].used = true;

	//do some register specific fixup
	switch(reg)
	{
#ifdef _M_X64
	case DSP_REG_ACC0_64:
	case DSP_REG_ACC1_64:
	{
		if (load)
		{
			//need to do this because interpreter only does 48 bits
			//(and putReg does the same)
			emitter.SHL(64, oparg, Imm8(64-40));//sign extend
			emitter.SAR(64, oparg, Imm8(64-40));
		}
	}
	break;
#endif
	default:
		break;
	}
}

void DSPJitRegCache::putReg(int reg, bool dirty)
{
	int real_reg = reg;
	if (regs[reg].parentReg != DSP_REG_NONE)
		real_reg = regs[reg].parentReg;

	OpArg oparg = regs[real_reg].loc;

	switch(reg)
	{
	case DSP_REG_ACH0:
	case DSP_REG_ACH1:
	{
		if (dirty)
		{
			//no need to extend to full 64bit here until interpreter
			//uses that
			if (oparg.IsSimpleReg())
			{
				//register is already shifted correctly
				//(if at all)

				// sign extend from the bottom 8 bits.
#ifndef _M_X64
				//cannot use movsx with SPL, BPL, SIL or DIL
				//on 32 bit
				if (oparg.GetSimpleReg() == RSP ||
					oparg.GetSimpleReg() == RBP ||
					oparg.GetSimpleReg() == RSI ||
					oparg.GetSimpleReg() == RDI)
				{
					emitter.SHL(16,oparg,Imm8(8));
					emitter.SAR(16,oparg,Imm8(8));
				}
				else
#endif
				{
					emitter.MOVSX(16, 8,
								oparg.GetSimpleReg(),
								oparg);
				}
			}
			else if (oparg.IsImm())
			{
				// TODO: Immediates?
			}
			else
			{
				//this works on the memory, so use reg instead
				//of real_reg, since it has the right loc
				X64Reg tmp;
				getFreeXReg(tmp);
				// sign extend from the bottom 8 bits.
				emitter.MOVSX(16, 8, tmp, regs[reg].loc);
				emitter.MOV(16, regs[reg].loc, R(tmp));
				putXReg(tmp);
			}
		}
	}
	break;
#ifdef _M_X64
	case DSP_REG_ACC0_64:
	case DSP_REG_ACC1_64:
	{
		if (dirty)
		{
			emitter.SHL(64, oparg, Imm8(64-40));//sign extend
			emitter.SAR(64, oparg, Imm8(64-40));
		}
	}
	break;
#endif
	default:
		break;
	}

	regs[real_reg].used = false;

	if (regs[real_reg].loc.IsSimpleReg())
	{
		regs[real_reg].dirty |= dirty;
		regs[real_reg].last_use_ctr = use_ctr;
		use_ctr++;
	}
}

void DSPJitRegCache::readReg(int sreg, X64Reg host_dreg, DSPJitSignExtend extend)
{
	OpArg reg;
	getReg(sreg, reg);

	switch(regs[sreg].size)
	{
	case 2:
		switch(extend)
		{
#ifdef _M_X64
		case SIGN: emitter.MOVSX(64, 16, host_dreg, reg); break;
		case ZERO: emitter.MOVZX(64, 16, host_dreg, reg); break;
#else
		case SIGN: emitter.MOVSX(32, 16, host_dreg, reg); break;
		case ZERO: emitter.MOVZX(32, 16, host_dreg, reg); break;
#endif
		case NONE: emitter.MOV(16, R(host_dreg), reg); break;
		}
		break;
	case 4:
#ifdef _M_X64
		switch(extend)
		{
		case SIGN: emitter.MOVSX(64, 32, host_dreg, reg); break;
		case ZERO: emitter.MOVZX(64, 32, host_dreg, reg); break;
		case NONE: emitter.MOV(32, R(host_dreg), reg); break;
		}
#else
		emitter.MOV(32, R(host_dreg), reg); break;
#endif
		break;
#ifdef _M_X64
	case 8:
		emitter.MOV(64, R(host_dreg), reg); break;
		break;
#endif
	default:
		_assert_msg_(DSPLLE, 0, "unsupported memory size");
		break;
	}
	putReg(sreg, false);
}

void DSPJitRegCache::writeReg(int dreg, OpArg arg)
{
	OpArg reg;
	getReg(dreg, reg, false);
	if (arg.IsImm())
	{
		switch(regs[dreg].size)
		{
		case 2: emitter.MOV(16, reg, Imm16(arg.offset)); break;
		case 4: emitter.MOV(32, reg, Imm32(arg.offset)); break;
#ifdef _M_X64
		case 8:
			if ((s32)arg.offset == (s64)arg.offset)
				emitter.MOV(64, reg, Imm32(arg.offset));
			else
				emitter.MOV(64, reg, Imm64(arg.offset));
			break;
#endif
		default:
			_assert_msg_(DSPLLE, 0, "unsupported memory size");
			break;
		}
	}
	else
	{
		switch(regs[dreg].size)
		{
		case 2: emitter.MOV(16, reg, arg); break;
		case 4: emitter.MOV(32, reg, arg); break;
#ifdef _M_X64
		case 8: emitter.MOV(64, reg, arg); break;
#endif
		default:
			_assert_msg_(DSPLLE, 0, "unsupported memory size");
			break;
		}
	}
	putReg(dreg, true);
}

//ordered in order of prefered use
//not all of these are actually available
static X64Reg alloc_order[] = {
#ifdef _M_X64
	R8,R9,R10,R11,R12,R13,R14,R15,RSI,RDI,RBX,RCX,RDX,RAX,RBP
#else
	ESI,EDI,EBX,ECX,EDX,EAX,EBP
#endif
};

X64Reg DSPJitRegCache::spillXReg()
{
	unsigned int i;
	unsigned int max_use_ctr_diff = 0;
	X64Reg least_recent_use_reg = INVALID_REG;
	for(i = 0; i < sizeof(alloc_order)/sizeof(alloc_order[0]); i++)
	{
		X64Reg reg = alloc_order[i];
		if (xregs[reg].guest_reg <= DSP_REG_MAX_MEM_BACKED &&
			!regs[xregs[reg].guest_reg].used)
		{
			unsigned int use_ctr_diff = use_ctr -
				regs[xregs[reg].guest_reg].last_use_ctr;

			if (use_ctr_diff >= max_use_ctr_diff)
			{
				max_use_ctr_diff = use_ctr_diff;
				least_recent_use_reg = reg;
			}
		}
	}

	if (least_recent_use_reg != INVALID_REG)
	{
		movToMemory(xregs[least_recent_use_reg].guest_reg);
		return least_recent_use_reg;
	}

	//just choose one.
	for(i = 0; i < sizeof(alloc_order)/sizeof(alloc_order[0]); i++)
	{
		X64Reg reg = alloc_order[i];
		if (xregs[reg].guest_reg <= DSP_REG_MAX_MEM_BACKED &&
			!regs[xregs[reg].guest_reg].used)
		{
			movToMemory(xregs[reg].guest_reg);
			return reg;
		}
	}

	return INVALID_REG;
}

void DSPJitRegCache::spillXReg(X64Reg reg)
{
	if (xregs[reg].guest_reg <= DSP_REG_MAX_MEM_BACKED)
	{
		_assert_msg_(DSPLLE, !regs[xregs[reg].guest_reg].used,
			     "to be spilled host reg %x(guest reg %x) still in use!",
			     reg, xregs[reg].guest_reg);

		movToMemory(xregs[reg].guest_reg);
	}
	else
	{
		_assert_msg_(DSPLLE, xregs[reg].guest_reg == DSP_REG_NONE,
			     "to be spilled host reg %x still in use!",
			     reg);
	}
}

X64Reg DSPJitRegCache::findFreeXReg()
{
	for(unsigned int i = 0; i < sizeof(alloc_order)/sizeof(alloc_order[0]); i++)
	{
		if (xregs[alloc_order[i]].guest_reg == DSP_REG_NONE)
		{
			return alloc_order[i];
		}
	}
	return INVALID_REG;
}

X64Reg DSPJitRegCache::findSpillFreeXReg()
{
	X64Reg reg = findFreeXReg();
	if (reg == INVALID_REG)
		reg = spillXReg();
	return reg;
}

void DSPJitRegCache::getFreeXReg(X64Reg &reg)
{
	reg = findSpillFreeXReg();

	_assert_msg_(DSPLLE, reg != INVALID_REG, "could not find register");
	if (reg == INVALID_REG)
		emitter.INT3();
	xregs[reg].guest_reg = DSP_REG_USED;
}

void DSPJitRegCache::getXReg(X64Reg reg)
{
	if (xregs[reg].guest_reg == DSP_REG_STATIC)
	{
		ERROR_LOG(DSPLLE, "Trying to get statically used XReg %d", reg);
		return;
	}

	if (xregs[reg].guest_reg != DSP_REG_NONE)
		spillXReg(reg);
	_assert_msg_(DSPLLE, xregs[reg].guest_reg == DSP_REG_NONE, "register already in use");
	xregs[reg].guest_reg = DSP_REG_USED;
}

void DSPJitRegCache::putXReg(X64Reg reg)
{
	if (xregs[reg].guest_reg == DSP_REG_STATIC)
	{
		ERROR_LOG(DSPLLE, "Trying to put statically used XReg %d", reg);
		return;
	}

	_assert_msg_(DSPLLE, xregs[reg].guest_reg == DSP_REG_USED,
		     "putXReg without get(Free)XReg");

	xregs[reg].guest_reg = DSP_REG_NONE;
}
