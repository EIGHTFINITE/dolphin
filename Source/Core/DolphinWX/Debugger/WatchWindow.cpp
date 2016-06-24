// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>

#include <wx/bitmap.h>
#include <wx/panel.h>
#include <wx/aui/auibar.h>

#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Core/ConfigManager.h"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinWX/WxUtils.h"
#include "DolphinWX/Debugger/WatchView.h"
#include "DolphinWX/Debugger/WatchWindow.h"

class CWatchToolbar : public wxAuiToolBar
{
public:
CWatchToolbar(CWatchWindow* parent, const wxWindowID id)
	: wxAuiToolBar(parent, id, wxDefaultPosition, wxDefaultSize,
			wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_TEXT)
{
	SetToolBitmapSize(wxSize(16, 16));

	m_Bitmaps[Toolbar_File] = WxUtils::LoadResourceBitmap("toolbar_debugger_delete");

	AddTool(ID_LOAD, _("Load"), m_Bitmaps[Toolbar_File]);
	Bind(wxEVT_TOOL, &CWatchWindow::Event_LoadAll, parent, ID_LOAD);

	AddTool(ID_SAVE, _("Save"), m_Bitmaps[Toolbar_File]);
	Bind(wxEVT_TOOL, &CWatchWindow::Event_SaveAll, parent, ID_SAVE);
}

private:

	enum
	{
		Toolbar_File,
		Num_Bitmaps
	};

	enum
	{
		ID_LOAD,
		ID_SAVE
	};

	wxBitmap m_Bitmaps[Num_Bitmaps];
};

CWatchWindow::CWatchWindow(wxWindow* parent, wxWindowID id,
		const wxPoint& position, const wxSize& size,
		long style, const wxString& name)
	: wxPanel(parent, id, position, size, style, name)
	, m_GPRGridView(nullptr)
{
	m_mgr.SetManagedWindow(this);
	m_mgr.SetFlags(wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE);

	m_GPRGridView = new CWatchView(this);

	m_mgr.AddPane(new CWatchToolbar(this, wxID_ANY), wxAuiPaneInfo().ToolbarPane().Top().
		LeftDockable(true).RightDockable(true).BottomDockable(false).Floatable(false));
	m_mgr.AddPane(m_GPRGridView, wxAuiPaneInfo().CenterPane());
	m_mgr.Update();
}

CWatchWindow::~CWatchWindow()
{
	m_mgr.UnInit();
}

void CWatchWindow::NotifyUpdate()
{
	if (m_GPRGridView != nullptr)
		m_GPRGridView->Update();
}

void CWatchWindow::Event_SaveAll(wxCommandEvent& WXUNUSED(event))
{
	SaveAll();
}

void CWatchWindow::SaveAll()
{
	IniFile ini;
	ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini", false);
	ini.SetLines("Watches", PowerPC::watches.GetStrings());
	ini.Save(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini");
}

void CWatchWindow::Event_LoadAll(wxCommandEvent& WXUNUSED(event))
{
	LoadAll();
}

void CWatchWindow::LoadAll()
{
	IniFile ini;
	Watches::TWatchesStr watches;

	if (!ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + SConfig::GetInstance().GetUniqueID() + ".ini", false))
	{
		return;
	}

	if (ini.GetLines("Watches", &watches, false))
	{
		PowerPC::watches.Clear();
		PowerPC::watches.AddFromStrings(watches);
	}

	NotifyUpdate();
}
