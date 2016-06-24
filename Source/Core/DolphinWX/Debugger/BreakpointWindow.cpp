// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/listbase.h>
#include <wx/panel.h>
#include <wx/aui/auibar.h>
#include <wx/aui/framemanager.h>

#include "Common/BreakPoints.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Core/ConfigManager.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinWX/WxUtils.h"
#include "DolphinWX/Debugger/BreakpointDlg.h"
#include "DolphinWX/Debugger/BreakpointView.h"
#include "DolphinWX/Debugger/BreakpointWindow.h"
#include "DolphinWX/Debugger/CodeWindow.h"
#include "DolphinWX/Debugger/MemoryCheckDlg.h"

class CBreakPointBar : public wxAuiToolBar
{
public:
	CBreakPointBar(CBreakPointWindow* parent, const wxWindowID id)
		: wxAuiToolBar(parent, id, wxDefaultPosition, wxDefaultSize,
				wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_TEXT)
	{
		SetToolBitmapSize(wxSize(24, 24));

		m_Bitmaps[Toolbar_Delete] = WxUtils::LoadResourceBitmap("toolbar_debugger_delete");
		m_Bitmaps[Toolbar_Add_BP] = WxUtils::LoadResourceBitmap("toolbar_add_breakpoint");
		m_Bitmaps[Toolbar_Add_MC] = WxUtils::LoadResourceBitmap("toolbar_add_memorycheck");

		AddTool(ID_DELETE, _("Delete"), m_Bitmaps[Toolbar_Delete]);
		Bind(wxEVT_TOOL, &CBreakPointWindow::OnDelete, parent, ID_DELETE);

		AddTool(ID_CLEAR, _("Clear"), m_Bitmaps[Toolbar_Delete]);
		Bind(wxEVT_TOOL, &CBreakPointWindow::OnClear, parent, ID_CLEAR);

		AddTool(ID_ADDBP, "+BP", m_Bitmaps[Toolbar_Add_BP]);
		Bind(wxEVT_TOOL, &CBreakPointWindow::OnAddBreakPoint, parent, ID_ADDBP);

		// Add memory breakpoints if you can use them
		if (Memory::AreMemoryBreakpointsActivated())
		{
			AddTool(ID_ADDMC, "+MC", m_Bitmaps[Toolbar_Add_MC]);
			Bind(wxEVT_TOOL, &CBreakPointWindow::OnAddMemoryCheck, parent, ID_ADDMC);
		}

		AddTool(ID_LOAD, _("Load"), m_Bitmaps[Toolbar_Delete]);
		Bind(wxEVT_TOOL, &CBreakPointWindow::Event_LoadAll, parent, ID_LOAD);

		AddTool(ID_SAVE, _("Save"), m_Bitmaps[Toolbar_Delete]);
		Bind(wxEVT_TOOL, &CBreakPointWindow::Event_SaveAll, parent, ID_SAVE);
	}

private:

	enum
	{
		Toolbar_Delete,
		Toolbar_Add_BP,
		Toolbar_Add_MC,
		Num_Bitmaps
	};

	enum
	{
		ID_DELETE = 2000,
		ID_CLEAR,
		ID_ADDBP,
		ID_ADDMC,
		ID_LOAD,
		ID_SAVE
	};

	wxBitmap m_Bitmaps[Num_Bitmaps];
};

CBreakPointWindow::CBreakPointWindow(CCodeWindow* _pCodeWindow, wxWindow* parent,
	    wxWindowID id, const wxString& title, const wxPoint& position,
	    const wxSize& size, long style)
	: wxPanel(parent, id, position, size, style, title)
	, m_pCodeWindow(_pCodeWindow)
{
	Bind(wxEVT_CLOSE_WINDOW, &CBreakPointWindow::OnClose, this);

	m_mgr.SetManagedWindow(this);
	m_mgr.SetFlags(wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE);

	m_BreakPointListView = new CBreakPointView(this, wxID_ANY);
	m_BreakPointListView->Bind(wxEVT_LIST_ITEM_SELECTED, &CBreakPointWindow::OnSelectBP, this);

	m_mgr.AddPane(new CBreakPointBar(this, wxID_ANY), wxAuiPaneInfo().ToolbarPane().Top().
	              LeftDockable(true).RightDockable(true).BottomDockable(false).Floatable(false));
	m_mgr.AddPane(m_BreakPointListView, wxAuiPaneInfo().CenterPane());
	m_mgr.Update();
}

CBreakPointWindow::~CBreakPointWindow()
{
	m_mgr.UnInit();
}

void CBreakPointWindow::OnClose(wxCloseEvent& event)
{
	SaveAll();
	event.Skip();
}

void CBreakPointWindow::NotifyUpdate()
{
	m_BreakPointListView->Update();
}

void CBreakPointWindow::OnDelete(wxCommandEvent& WXUNUSED(event))
{
	m_BreakPointListView->DeleteCurrentSelection();
}

// jump to begin addr
void CBreakPointWindow::OnSelectBP(wxListEvent& event)
{
	long Index = event.GetIndex();
	if (Index >= 0)
	{
		u32 Address = (u32)m_BreakPointListView->GetItemData(Index);
		if (m_pCodeWindow)
			m_pCodeWindow->JumpToAddress(Address);
	}
}

// Clear all breakpoints and memchecks
void CBreakPointWindow::OnClear(wxCommandEvent& WXUNUSED(event))
{
	PowerPC::debug_interface.ClearAllBreakpoints();
	PowerPC::debug_interface.ClearAllMemChecks();

	NotifyUpdate();
}

void CBreakPointWindow::OnAddBreakPoint(wxCommandEvent& WXUNUSED(event))
{
	BreakPointDlg bpDlg(this);
	bpDlg.ShowModal();
}

void CBreakPointWindow::OnAddMemoryCheck(wxCommandEvent& WXUNUSED(event))
{
	MemoryCheckDlg memDlg(this);
	memDlg.ShowModal();
}

void CBreakPointWindow::Event_SaveAll(wxCommandEvent& WXUNUSED(event))
{
	SaveAll();
}

void CBreakPointWindow::SaveAll()
{
	// simply dump all to bp/mc files in a way we can read again
	IniFile ini;
	ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini", false);
	ini.SetLines("BreakPoints", PowerPC::breakpoints.GetStrings());
	ini.SetLines("MemoryChecks", PowerPC::memchecks.GetStrings());
	ini.Save(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini");
}

void CBreakPointWindow::Event_LoadAll(wxCommandEvent& WXUNUSED(event))
{
	LoadAll();
	return;
}

void CBreakPointWindow::LoadAll()
{
	IniFile ini;
	BreakPoints::TBreakPointsStr newbps;
	MemChecks::TMemChecksStr newmcs;

	if (!ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini", false))
	{
		return;
	}

	if (ini.GetLines("BreakPoints", &newbps, false))
	{
		PowerPC::breakpoints.Clear();
		PowerPC::breakpoints.AddFromStrings(newbps);
	}

	if (ini.GetLines("MemoryChecks", &newmcs, false))
	{
		PowerPC::memchecks.Clear();
		PowerPC::memchecks.AddFromStrings(newmcs);
	}

	NotifyUpdate();
}
