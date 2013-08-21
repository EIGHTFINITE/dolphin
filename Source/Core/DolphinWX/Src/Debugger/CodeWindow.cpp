// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// Include
#include "Common.h"

#include <wx/wx.h>

#include "Host.h"

#include "RegisterWindow.h"
#include "BreakpointWindow.h"
#include "MemoryWindow.h"
#include "JitWindow.h"

#include "CodeWindow.h"
#include "CodeView.h"

#include "../WxUtils.h"
#include "FileUtil.h"
#include "Core.h"
#include "HW/Memmap.h"
#include "HLE/HLE.h"
#include "Boot/Boot.h"
#include "LogManager.h"
#include "HW/CPU.h"
#include "PowerPC/PowerPC.h"
#include "PowerPC/JitInterface.h"
#include "Debugger/PPCDebugInterface.h"
#include "Debugger/Debugger_SymbolMap.h"
#include "PowerPC/PPCAnalyst.h"
#include "PowerPC/Profiler.h"
#include "PowerPC/PPCSymbolDB.h"
#include "PowerPC/SignatureDB.h"
#include "PowerPC/PPCTables.h"

#include "ConfigManager.h"

extern "C"  // Bitmaps
{
	#include "../../resources/toolbar_add_memorycheck.c"
	#include "../../resources/toolbar_add_breakpoint.c"
}

// -------
// Main

BEGIN_EVENT_TABLE(CCodeWindow, wxPanel)

	// Menu bar
	EVT_MENU_RANGE(IDM_INTERPRETER, IDM_JITSROFF, CCodeWindow::OnCPUMode)
	EVT_MENU(IDM_FONTPICKER, CCodeWindow::OnChangeFont)
	EVT_MENU_RANGE(IDM_CLEARCODECACHE, IDM_SEARCHINSTRUCTION, CCodeWindow::OnJitMenu)
	EVT_MENU_RANGE(IDM_CLEARSYMBOLS, IDM_PATCHHLEFUNCTIONS, CCodeWindow::OnSymbolsMenu)
	EVT_MENU_RANGE(IDM_PROFILEBLOCKS, IDM_WRITEPROFILE, CCodeWindow::OnProfilerMenu)

	// Toolbar
	EVT_MENU_RANGE(IDM_STEP, IDM_GOTOPC, CCodeWindow::OnCodeStep)
	EVT_TEXT(IDM_ADDRBOX, CCodeWindow::OnAddrBoxChange)

	// Other
	EVT_LISTBOX(ID_SYMBOLLIST,			CCodeWindow::OnSymbolListChange)
	EVT_LISTBOX(ID_CALLSTACKLIST,		CCodeWindow::OnCallstackListChange)
	EVT_LISTBOX(ID_CALLERSLIST,			CCodeWindow::OnCallersListChange)
	EVT_LISTBOX(ID_CALLSLIST,			CCodeWindow::OnCallsListChange)

	EVT_HOST_COMMAND(wxID_ANY,			CCodeWindow::OnHostMessage)

END_EVENT_TABLE()

// Class
CCodeWindow::CCodeWindow(const SCoreStartupParameter& _LocalCoreStartupParameter, CFrame *parent,
	wxWindowID id, const wxPoint& position, const wxSize& size, long style, const wxString& name)
	: wxPanel((wxWindow*)parent, id, position, size, style, name)
	, Parent(parent)
	, m_RegisterWindow(NULL)
	, m_BreakpointWindow(NULL)
	, m_MemoryWindow(NULL)
	, m_JitWindow(NULL)
	, m_SoundWindow(NULL)
	, m_VideoWindow(NULL)
	, codeview(NULL)
{
	InitBitmaps();

	wxBoxSizer* sizerBig   = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLeft  = new wxBoxSizer(wxVERTICAL);

	DebugInterface* di = &PowerPC::debug_interface;

	codeview = new CCodeView(di, &g_symbolDB, this, ID_CODEVIEW);
	sizerBig->Add(sizerLeft, 2, wxEXPAND);
	sizerBig->Add(codeview, 5, wxEXPAND);

	sizerLeft->Add(callstack = new wxListBox(this, ID_CALLSTACKLIST,
				wxDefaultPosition, wxSize(90, 100)), 0, wxEXPAND);
	sizerLeft->Add(symbols = new wxListBox(this, ID_SYMBOLLIST,
				wxDefaultPosition, wxSize(90, 100), 0, NULL, wxLB_SORT), 1, wxEXPAND);
	sizerLeft->Add(calls = new wxListBox(this, ID_CALLSLIST, wxDefaultPosition,
				wxSize(90, 100), 0, NULL, wxLB_SORT), 0, wxEXPAND);
	sizerLeft->Add(callers = new wxListBox(this, ID_CALLERSLIST, wxDefaultPosition,
				wxSize(90, 100), 0, NULL, wxLB_SORT), 0, wxEXPAND);

	SetSizer(sizerBig);

	sizerLeft->Fit(this);
	sizerBig->Fit(this);
}

wxMenuBar *CCodeWindow::GetMenuBar()
{
	return Parent->GetMenuBar();
}

wxAuiToolBar *CCodeWindow::GetToolBar()
{
	return Parent->m_ToolBarDebug;
}

// ----------
// Events

void CCodeWindow::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_NOTIFYMAPLOADED:
			NotifyMapLoaded();
			if (m_BreakpointWindow) m_BreakpointWindow->NotifyUpdate();
			break;

		case IDM_UPDATEDISASMDIALOG:
			Update();
			if (codeview) codeview->Center(PC);
			if (m_RegisterWindow) m_RegisterWindow->NotifyUpdate();
			break;

		case IDM_UPDATEBREAKPOINTS:
			Update();
			if (m_BreakpointWindow) m_BreakpointWindow->NotifyUpdate();
			break;
	}
}

// The Play, Stop, Step, Skip, Go to PC and Show PC buttons go here
void CCodeWindow::OnCodeStep(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_STEP:
			SingleStep();
			break;

		case IDM_STEPOVER:
			StepOver();
			break;

		case IDM_TOGGLE_BREAKPOINT:
			ToggleBreakpoint();
			break;

		case IDM_SKIP:
			PC += 4;
			Update();
			break;

		case IDM_SETPC:
			PC = codeview->GetSelection();
			Update();
			break;

		case IDM_GOTOPC:
			JumpToAddress(PC);
			break;
	}

	UpdateButtonStates();
	// Update all toolbars in the aui manager
	Parent->UpdateGUI();
}

void CCodeWindow::JumpToAddress(u32 _Address)
{
	codeview->Center(_Address);
	UpdateLists();
}

void CCodeWindow::OnCodeViewChange(wxCommandEvent &event)
{
	UpdateLists();
}

void CCodeWindow::OnAddrBoxChange(wxCommandEvent& event)
{
	if (!GetToolBar()) return;

	wxTextCtrl* pAddrCtrl = (wxTextCtrl*)GetToolBar()->FindControl(IDM_ADDRBOX);
	wxString txt = pAddrCtrl->GetValue();

	std::string text(WxStrToStr(txt));
	text = StripSpaces(text);
	if (text.size() == 8)
	{
		u32 addr;
		sscanf(text.c_str(), "%08x", &addr);
		JumpToAddress(addr);
	}

	event.Skip(1);
}

void CCodeWindow::OnCallstackListChange(wxCommandEvent& event)
{
	int index   = callstack->GetSelection();
	if (index >= 0)
	{
		u32 address = (u32)(u64)(callstack->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::OnCallersListChange(wxCommandEvent& event)
{
	int index = callers->GetSelection();
	if (index >= 0)
	{
		u32 address = (u32)(u64)(callers->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::OnCallsListChange(wxCommandEvent& event)
{
	int index = calls->GetSelection();
	if (index >= 0)
	{
		u32 address = (u32)(u64)(calls->GetClientData(index));
		if (address)
			JumpToAddress(address);
	}
}

void CCodeWindow::SingleStep()
{
	if (CCPU::IsStepping())
	{
		JitInterface::InvalidateICache(PC, 4);
		CCPU::StepOpcode(&sync_event);
		wxThread::Sleep(20);
		// need a short wait here
		JumpToAddress(PC);
		Update();
		Host_UpdateLogDisplay();
	}
}

void CCodeWindow::StepOver()
{
	if (CCPU::IsStepping())
	{
		UGeckoInstruction inst = Memory::Read_Instruction(PC);
		if (inst.LK)
		{
			PowerPC::breakpoints.Add(PC + 4, true);
			CCPU::EnableStepping(false);
			JumpToAddress(PC);
			Update();
		}
		else
		{
			SingleStep();
		}

		UpdateButtonStates();
		// Update all toolbars in the aui manager
		Parent->UpdateGUI();
	}
}

void CCodeWindow::ToggleBreakpoint()
{
	if (CCPU::IsStepping())
	{
		if (codeview) codeview->ToggleBreakpoint(codeview->GetSelection());
		Update();
	}
}

void CCodeWindow::UpdateLists()
{
	callers->Clear();
	u32 addr = codeview->GetSelection();
	Symbol *symbol = g_symbolDB.GetSymbolFromAddr(addr);
	if (!symbol)
		return;

	for (int i = 0; i < (int)symbol->callers.size(); i++)
	{
		u32 caller_addr = symbol->callers[i].callAddress;
		Symbol *caller_symbol = g_symbolDB.GetSymbolFromAddr(caller_addr);
		if (caller_symbol)
		{
			int idx = callers->Append(StrToWxStr(StringFromFormat
						("< %s (%08x)", caller_symbol->name.c_str(), caller_addr).c_str()));
			callers->SetClientData(idx, (void*)(u64)caller_addr);
		}
	}

	calls->Clear();
	for (int i = 0; i < (int)symbol->calls.size(); i++)
	{
		u32 call_addr = symbol->calls[i].function;
		Symbol *call_symbol = g_symbolDB.GetSymbolFromAddr(call_addr);
		if (call_symbol)
		{
			int idx = calls->Append(StrToWxStr(StringFromFormat
						("> %s (%08x)", call_symbol->name.c_str(), call_addr).c_str()));
			calls->SetClientData(idx, (void*)(u64)call_addr);
		}
	}
}

void CCodeWindow::UpdateCallstack()
{
	if (Core::GetState() == Core::CORE_STOPPING) return;

	callstack->Clear();

	std::vector<Dolphin_Debugger::CallstackEntry> stack;

	bool ret = Dolphin_Debugger::GetCallstack(stack);

	for (size_t i = 0; i < stack.size(); i++)
	{
		int idx = callstack->Append(StrToWxStr(stack[i].Name));
		callstack->SetClientData(idx, (void*)(u64)stack[i].vAddress);
	}

	if (!ret)
		callstack->Append(StrToWxStr("invalid callstack"));
}

// Create CPU Mode menus
void CCodeWindow::CreateMenu(const SCoreStartupParameter& _LocalCoreStartupParameter, wxMenuBar *pMenuBar)
{
	// CPU Mode
	wxMenu* pCoreMenu = new wxMenu;

	wxMenuItem* interpreter = pCoreMenu->Append(IDM_INTERPRETER, _("&Interpreter core"), 
		StrToWxStr("This is necessary to get break points"
		" and stepping to work as explained in the Developer Documentation. But it can be very"
		" slow, perhaps slower than 1 fps."), 
		wxITEM_CHECK);
	interpreter->Check(_LocalCoreStartupParameter.iCPUCore == 0);
	pCoreMenu->AppendSeparator();

	pCoreMenu->Append(IDM_JITBLOCKLINKING, _("&JIT Block Linking off"),
		_("Provide safer execution by not linking the JIT blocks."), 
		wxITEM_CHECK);

	pCoreMenu->Append(IDM_JITNOBLOCKCACHE, _("&Disable JIT Cache"),
		_("Avoid any involuntary JIT cache clearing, this may prevent Zelda TP from crashing.\n[This option must be selected before a game is started.]"), 
		wxITEM_CHECK);
	pCoreMenu->Append(IDM_CLEARCODECACHE, _("&Clear JIT cache"));

	pCoreMenu->AppendSeparator();
	pCoreMenu->Append(IDM_LOGINSTRUCTIONS, _("&Log JIT instruction coverage"));
	pCoreMenu->Append(IDM_SEARCHINSTRUCTION, _("&Search for an op"));

	pCoreMenu->AppendSeparator();
	pCoreMenu->Append(IDM_JITOFF, _("&JIT off (JIT core)"),
		_("Turn off all JIT functions, but still use the JIT core from Jit.cpp"),
		wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSOFF, _("&JIT LoadStore off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSLBZXOFF, _("    &JIT LoadStore lbzx off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSLXZOFF, _("    &JIT LoadStore lXz off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSLWZOFF, _("&JIT LoadStore lwz off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSFOFF, _("&JIT LoadStore Floating off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITLSPOFF, _("&JIT LoadStore Paired off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITFPOFF, _("&JIT FloatingPoint off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITIOFF, _("&JIT Integer off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITPOFF, _("&JIT Paired off"),
			wxEmptyString, wxITEM_CHECK);
	pCoreMenu->Append(IDM_JITSROFF, _("&JIT SystemRegisters off"),
			wxEmptyString, wxITEM_CHECK);

	pMenuBar->Append(pCoreMenu, _("&JIT"));


	// Debug Menu
	wxMenu* pDebugMenu = new wxMenu;

	pDebugMenu->Append(IDM_STEP, _("Step &Into\tF11"));
	pDebugMenu->Append(IDM_STEPOVER, _("Step &Over\tF10"));
	pDebugMenu->Append(IDM_TOGGLE_BREAKPOINT, _("Toggle &Breakpoint\tF9"));

	pMenuBar->Append(pDebugMenu, _("&Debug"));

	CreateMenuSymbols(pMenuBar);
}

void CCodeWindow::CreateMenuOptions(wxMenu* pMenu)
{
	wxMenuItem* boottopause = pMenu->Append(IDM_BOOTTOPAUSE, _("Boot to pause"),
		_("Start the game directly instead of booting to pause"), 
		wxITEM_CHECK);
	boottopause->Check(bBootToPause);

	wxMenuItem* automaticstart = pMenu->Append(IDM_AUTOMATICSTART, _("&Automatic start"), 
		StrToWxStr(
		"Automatically load the Default ISO when Dolphin starts, or the last game you loaded,"
		" if you have not given it an elf file with the --elf command line. [This can be"
		" convenient if you are bug-testing with a certain game and want to rebuild"
		" and retry it several times, either with changes to Dolphin or if you are"
		" developing a homebrew game.]"), 
		wxITEM_CHECK);
	automaticstart->Check(bAutomaticStart);

	pMenu->Append(IDM_FONTPICKER, _("&Font..."), wxEmptyString, wxITEM_NORMAL);
}

// CPU Mode and JIT Menu
void CCodeWindow::OnCPUMode(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_INTERPRETER:
			PowerPC::SetMode(UseInterpreter() ? PowerPC::MODE_INTERPRETER : PowerPC::MODE_JIT);
			break;
		case IDM_BOOTTOPAUSE:
			bBootToPause = !bBootToPause;
			return;
		case IDM_AUTOMATICSTART:
			bAutomaticStart = !bAutomaticStart;
			return;
		case IDM_JITOFF:
			Core::g_CoreStartupParameter.bJITOff = event.IsChecked();
			break;
		case IDM_JITLSOFF:
			Core::g_CoreStartupParameter.bJITLoadStoreOff = event.IsChecked();
			break;
		case IDM_JITLSLXZOFF:
			Core::g_CoreStartupParameter.bJITLoadStorelXzOff = event.IsChecked();
			break;
		case IDM_JITLSLWZOFF:
			Core::g_CoreStartupParameter.bJITLoadStorelwzOff = event.IsChecked();
			break;
		case IDM_JITLSLBZXOFF:
			Core::g_CoreStartupParameter.bJITLoadStorelbzxOff = event.IsChecked();
			break;
		case IDM_JITLSFOFF:
			Core::g_CoreStartupParameter.bJITLoadStoreFloatingOff = event.IsChecked();
			break;
		case IDM_JITLSPOFF:
			Core::g_CoreStartupParameter.bJITLoadStorePairedOff = event.IsChecked();
			break;
		case IDM_JITFPOFF:
			Core::g_CoreStartupParameter.bJITFloatingPointOff = event.IsChecked();
			break;
		case IDM_JITIOFF:
			Core::g_CoreStartupParameter.bJITIntegerOff = event.IsChecked();
			break;
		case IDM_JITPOFF:
			Core::g_CoreStartupParameter.bJITPairedOff = event.IsChecked();
			break;
		case IDM_JITSROFF:
			Core::g_CoreStartupParameter.bJITSystemRegistersOff = event.IsChecked();
			break;
	}

	// Clear the JIT cache to enable these changes
	JitInterface::ClearCache();
	
	// Update
	UpdateButtonStates();
}

void CCodeWindow::OnJitMenu(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_LOGINSTRUCTIONS:
			PPCTables::LogCompiledInstructions();
			break;

		case IDM_CLEARCODECACHE:
			JitInterface::ClearCache();
			break;

		case IDM_SEARCHINSTRUCTION:
		{
			wxString str;
			str = wxGetTextFromUser(_T(""), wxT("Op?"), wxEmptyString, this);
			for (u32 addr = 0x80000000; addr < 0x80100000; addr += 4)
			{
				const char *name = PPCTables::GetInstructionName(Memory::ReadUnchecked_U32(addr));
				auto const wx_name = WxStrToStr(str);
				if (name && (wx_name == name))
				{
					NOTICE_LOG(POWERPC, "Found %s at %08x", wx_name.c_str(), addr);
				}
			}
			break;
		}
	}
}

// Shortcuts
bool CCodeWindow::UseInterpreter()
{
	return GetMenuBar()->IsChecked(IDM_INTERPRETER);
}

bool CCodeWindow::BootToPause()
{
	return GetMenuBar()->IsChecked(IDM_BOOTTOPAUSE);
}

bool CCodeWindow::AutomaticStart()
{
	return GetMenuBar()->IsChecked(IDM_AUTOMATICSTART);
}

bool CCodeWindow::JITNoBlockCache()
{
	return GetMenuBar()->IsChecked(IDM_JITNOBLOCKCACHE);
}

bool CCodeWindow::JITBlockLinking()
{
	return GetMenuBar()->IsChecked(IDM_JITBLOCKLINKING);
}

// Toolbar
void CCodeWindow::InitBitmaps()
{
	// load original size 48x48
	m_Bitmaps[Toolbar_Step] = wxGetBitmapFromMemory(toolbar_add_breakpoint_png);
	m_Bitmaps[Toolbar_StepOver] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_Skip] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_GotoPC] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);
	m_Bitmaps[Toolbar_SetPC] = wxGetBitmapFromMemory(toolbar_add_memcheck_png);

	// scale to 24x24 for toolbar
	for (size_t n = 0; n < ToolbarDebugBitmapMax; n++)
		m_Bitmaps[n] = wxBitmap(m_Bitmaps[n].ConvertToImage().Scale(24, 24));
}

void CCodeWindow::PopulateToolbar(wxAuiToolBar* toolBar)
{
	int w = m_Bitmaps[0].GetWidth(),
		h = m_Bitmaps[0].GetHeight();

	toolBar->SetToolBitmapSize(wxSize(w, h));
	toolBar->AddTool(IDM_STEP,		_("Step"),			m_Bitmaps[Toolbar_Step]);
	toolBar->AddTool(IDM_STEPOVER,	_("Step Over"),		m_Bitmaps[Toolbar_StepOver]);
	toolBar->AddTool(IDM_SKIP,		_("Skip"),			m_Bitmaps[Toolbar_Skip]);
	toolBar->AddSeparator();
	toolBar->AddTool(IDM_GOTOPC,		_("Show PC"),		m_Bitmaps[Toolbar_GotoPC]);
	toolBar->AddTool(IDM_SETPC,		_("Set PC"),		m_Bitmaps[Toolbar_SetPC]);
	toolBar->AddSeparator();
	toolBar->AddControl(new wxTextCtrl(toolBar, IDM_ADDRBOX, _T("")));

	toolBar->Realize();
}

// Update GUI
void CCodeWindow::Update()
{
	if (!codeview) return;

	codeview->Refresh();
	UpdateCallstack();
	UpdateButtonStates();

	// Do not automatically show the current PC position when a breakpoint is hit or
	// when we pause since this can be called at other times too.
	//codeview->Center(PC);
}

void CCodeWindow::UpdateButtonStates()
{
	bool Initialized = (Core::GetState() != Core::CORE_UNINITIALIZED);
	bool Pause = (Core::GetState() == Core::CORE_PAUSE);
	bool Stepping = CCPU::IsStepping();
	wxAuiToolBar* ToolBar = GetToolBar();

	// Toolbar
	if (!ToolBar) return;

	if (!Initialized)
	{
		ToolBar->EnableTool(IDM_STEPOVER, false);
		ToolBar->EnableTool(IDM_SKIP, false);
	}
	else
	{
		if (!Stepping)
		{
			ToolBar->EnableTool(IDM_STEPOVER, false);
			ToolBar->EnableTool(IDM_SKIP, false);
		}
		else
		{
			ToolBar->EnableTool(IDM_STEPOVER, true);
			ToolBar->EnableTool(IDM_SKIP, true);
		}
	}

	ToolBar->EnableTool(IDM_STEP, Initialized && Stepping);

	if (ToolBar) ToolBar->Realize();

	// Menu bar
	// ------------------
	GetMenuBar()->Enable(IDM_INTERPRETER, Pause); // CPU Mode

	GetMenuBar()->Enable(IDM_JITNOBLOCKCACHE, !Initialized);

	GetMenuBar()->Enable(IDM_JITOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSLXZOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSLWZOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSLBZXOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSFOFF, Pause);
	GetMenuBar()->Enable(IDM_JITLSPOFF, Pause);
	GetMenuBar()->Enable(IDM_JITFPOFF, Pause);
	GetMenuBar()->Enable(IDM_JITIOFF, Pause);
	GetMenuBar()->Enable(IDM_JITPOFF, Pause);
	GetMenuBar()->Enable(IDM_JITSROFF, Pause);

	GetMenuBar()->Enable(IDM_CLEARCODECACHE, Pause); // JIT Menu
	GetMenuBar()->Enable(IDM_SEARCHINSTRUCTION, Initialized);

	GetMenuBar()->Enable(IDM_CLEARSYMBOLS, Initialized); // Symbols menu
	GetMenuBar()->Enable(IDM_SCANFUNCTIONS, Initialized);
	GetMenuBar()->Enable(IDM_LOADMAPFILE, Initialized);
	GetMenuBar()->Enable(IDM_SAVEMAPFILE, Initialized);
	GetMenuBar()->Enable(IDM_SAVEMAPFILEWITHCODES, Initialized);
	GetMenuBar()->Enable(IDM_CREATESIGNATUREFILE, Initialized);
	GetMenuBar()->Enable(IDM_RENAME_SYMBOLS, Initialized);
	GetMenuBar()->Enable(IDM_USESIGNATUREFILE, Initialized);
	GetMenuBar()->Enable(IDM_PATCHHLEFUNCTIONS, Initialized);

	// Update Fonts
	callstack->SetFont(DebuggerFont);
	symbols->SetFont(DebuggerFont);
	callers->SetFont(DebuggerFont);
	calls->SetFont(DebuggerFont);
}
