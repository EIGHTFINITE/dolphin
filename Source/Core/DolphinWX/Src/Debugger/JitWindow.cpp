// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/thread.h>
#include <wx/listctrl.h>

#include "JitWindow.h"
#include "HW/CPU.h"
#include "PowerPC/PowerPC.h"
#include "PowerPC/JitCommon/JitBase.h"
#include "PowerPC/JitCommon/JitCache.h"
#include "PowerPC/PPCAnalyst.h"
#include "PowerPCDisasm.h"
#include "Host.h"
#include "disasm.h"

#include "Debugger/PPCDebugInterface.h"
#include "Debugger/Debugger_SymbolMap.h"

#include "Core.h"
#include "StringUtil.h"
#include "LogManager.h"
#include "../WxUtils.h"

#include "../Globals.h"

enum
{
	IDM_REFRESH_LIST = 23350,
	IDM_PPC_BOX,
	IDM_X86_BOX,
	IDM_NEXT,
	IDM_PREV,
	IDM_BLOCKLIST,
};

BEGIN_EVENT_TABLE(CJitWindow, wxPanel)
	//EVT_TEXT(IDM_ADDRBOX, CJitWindow::OnAddrBoxChange)
	//EVT_LISTBOX(IDM_SYMBOLLIST, CJitWindow::OnSymbolListChange)
	//EVT_HOST_COMMAND(wxID_ANY, CJitWindow::OnHostMessage)
	EVT_BUTTON(IDM_REFRESH_LIST, CJitWindow::OnRefresh)
END_EVENT_TABLE()

CJitWindow::CJitWindow(wxWindow* parent, wxWindowID id, const wxPoint& pos,
		const wxSize& size, long style, const wxString& name)
: wxPanel(parent, id, pos, size, style, name)
{
	wxBoxSizer* sizerBig   = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerSplit = new wxBoxSizer(wxHORIZONTAL);
	sizerSplit->Add(ppc_box = new wxTextCtrl(this, IDM_PPC_BOX, _T("(ppc)"),
				wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE), 1, wxEXPAND);
	sizerSplit->Add(x86_box = new wxTextCtrl(this, IDM_X86_BOX, _T("(x86)"),
				wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE), 1, wxEXPAND);
	sizerBig->Add(block_list = new JitBlockList(this, IDM_BLOCKLIST,
				wxDefaultPosition, wxSize(100, 140),
				wxLC_REPORT | wxSUNKEN_BORDER | wxLC_ALIGN_LEFT | wxLC_SINGLE_SEL | wxLC_SORT_ASCENDING),
				0, wxEXPAND);
	sizerBig->Add(sizerSplit, 2, wxEXPAND);
//	sizerBig->Add(memview, 5, wxEXPAND);
//	sizerBig->Add(sizerRight, 0, wxEXPAND | wxALL, 3);
	sizerBig->Add(button_refresh = new wxButton(this, IDM_REFRESH_LIST, _("&Refresh")));
//	sizerRight->Add(addrbox = new wxTextCtrl(this, IDM_ADDRBOX, _T("")));
//	sizerRight->Add(new wxButton(this, IDM_SETPC, _("S&et PC")));

	SetSizer(sizerBig);

	sizerSplit->Fit(this);
	sizerBig->Fit(this);
}

void CJitWindow::OnRefresh(wxCommandEvent& /*event*/) {
	block_list->Update();
}

void CJitWindow::ViewAddr(u32 em_address)
{
	Show(true);
	Compare(em_address);
	SetFocus();
}

void CJitWindow::Compare(u32 em_address)
{
	u8 *xDis = new u8[1<<18];
	memset(xDis, 0, 1<<18);

	disassembler x64disasm;
	x64disasm.set_syntax_intel();

	int block_num = jit->GetBlockCache()->GetBlockNumberFromStartAddress(em_address);
	if (block_num < 0)
	{
		for (int i = 0; i < 500; i++)
		{
			block_num = jit->GetBlockCache()->GetBlockNumberFromStartAddress(em_address - 4 * i);
			if (block_num >= 0)
				break;
		}

		if (block_num >= 0)
		{
			JitBlock *block = jit->GetBlockCache()->GetBlock(block_num);
			if (!(block->originalAddress <= em_address &&
						block->originalSize + block->originalAddress >= em_address))
				block_num = -1;
		}

		// Do not merge this "if" with the above - block_num changes inside it.
		if (block_num < 0)
		{
			ppc_box->SetValue(StrToWxStr(StringFromFormat("(non-code address: %08x)",
							em_address)));
			x86_box->SetValue(StrToWxStr(StringFromFormat("(no translation)")));
			delete[] xDis;
			return;
		}
	}
	JitBlock *block = jit->GetBlockCache()->GetBlock(block_num);

	// 800031f0
	// == Fill in x86 box

	const u8 *code = (const u8 *)jit->GetBlockCache()->GetCompiledCodeFromBlock(block_num);
	u64 disasmPtr = (u64)code;
	int size = block->codeSize;
	const u8 *end = code + size;
	char *sptr = (char*)xDis;

	int num_x86_instructions = 0;
	while ((u8*)disasmPtr < end)
	{
#ifdef _M_X64
		disasmPtr += x64disasm.disasm64(disasmPtr, disasmPtr, (u8*)disasmPtr, sptr);
#else
		disasmPtr += x64disasm.disasm32(disasmPtr, disasmPtr, (u8*)disasmPtr, sptr);
#endif
		sptr += strlen(sptr);
		*sptr++ = 13;
		*sptr++ = 10;
		num_x86_instructions++;
	}
	x86_box->SetValue(StrToWxStr((char*)xDis));

	// == Fill in ppc box
	u32 ppc_addr = block->originalAddress;
	PPCAnalyst::CodeBuffer code_buffer(32000);
	PPCAnalyst::BlockStats st;
	PPCAnalyst::BlockRegStats gpa;
	PPCAnalyst::BlockRegStats fpa;
	bool broken_block = false;
	u32 merged_addresses[32];
	const int capacity_of_merged_addresses = sizeof(merged_addresses) / sizeof(merged_addresses[0]);
	int size_of_merged_addresses;
	if (PPCAnalyst::Flatten(ppc_addr, &size, &st, &gpa, &fpa, broken_block, &code_buffer, size, merged_addresses, capacity_of_merged_addresses, size_of_merged_addresses) != 0xffffffff)
	{
		sptr = (char*)xDis;
		for (int i = 0; i < size; i++)
		{
			const PPCAnalyst::CodeOp &op = code_buffer.codebuffer[i];
			char temp[256];
			DisassembleGekko(op.inst.hex, op.address, temp, 256);
			sptr += sprintf(sptr, "%08x %s\n", op.address, temp);
		}

		// Add stats to the end of the ppc box since it's generally the shortest.
		sptr += sprintf(sptr, "\n");

		// Add some generic analysis
		if (st.isFirstBlockOfFunction)
			sptr += sprintf(sptr, "(first block of function)\n");
		if (st.isLastBlockOfFunction)
			sptr += sprintf(sptr, "(first block of function)\n");

		sptr += sprintf(sptr, "%i estimated cycles\n", st.numCycles);

		sptr += sprintf(sptr, "Num instr: PPC: %i  x86: %i  (blowup: %i%%)\n",
				size, num_x86_instructions, 100 * (num_x86_instructions / size - 1));
		sptr += sprintf(sptr, "Num bytes: PPC: %i  x86: %i  (blowup: %i%%)\n",
				size * 4, block->codeSize, 100 * (block->codeSize / (4 * size) - 1));

		ppc_box->SetValue(StrToWxStr((char*)xDis));
	}
	else
	{
		ppc_box->SetValue(StrToWxStr(StringFromFormat(
						"(non-code address: %08x)", em_address)));
		x86_box->SetValue("---");
	}

	delete[] xDis;
}

void CJitWindow::Update()
{

}

void CJitWindow::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_NOTIFYMAPLOADED:
			//NotifyMapLoaded();
			break;
	}
}

// JitBlockList
//================

enum {
	COLUMN_ADDRESS,
	COLUMN_PPCSIZE,
	COLUMN_X86SIZE,
	COLUMN_NAME,
	COLUMN_FLAGS,
	COLUMN_NUMEXEC,
	COLUMN_COST,  // (estimated as x86size * numexec)
};

JitBlockList::JitBlockList(wxWindow* parent, const wxWindowID id,
		const wxPoint& pos, const wxSize& size, long style)
	: wxListCtrl(parent, id, pos, size, style) // | wxLC_VIRTUAL)
{
	Init();
}

void JitBlockList::Init()
{
	InsertColumn(COLUMN_ADDRESS, _("Address"));
	InsertColumn(COLUMN_PPCSIZE, _("PPC Size"));
	InsertColumn(COLUMN_X86SIZE, _("x86 Size"));
	InsertColumn(COLUMN_NAME, _("Symbol"));
	InsertColumn(COLUMN_FLAGS, _("Flags"));
	InsertColumn(COLUMN_NUMEXEC, _("NumExec"));
	InsertColumn(COLUMN_COST, _("Cost"));
}

void JitBlockList::Update()
{
}
