// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/progdlg.h>
#include <wx/statusbr.h>
#include <wx/thread.h>
#include <wx/toolbar.h>
#include <wx/toplevel.h>
#include <wx/aui/framemanager.h>

#ifdef __APPLE__
#include <AppKit/AppKit.h>
#endif

#include "Common/CDUtils.h"
#include "Common/CommonTypes.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/NandPaths.h"

#include "Core/BootManager.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/HotkeyManager.h"
#include "Core/Movie.h"
#include "Core/State.h"
#include "Core/HW/CPU.h"
#include "Core/HW/DVDInterface.h"
#include "Core/HW/GCKeyboard.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiiSaveCrypted.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "Core/IPC_HLE/WII_IPC_HLE_WiiMote.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCSymbolDB.h"

#include "DiscIO/NANDContentLoader.h"

#include "DolphinWX/AboutDolphin.h"
#include "DolphinWX/ControllerConfigDiag.h"
#include "DolphinWX/FifoPlayerDlg.h"
#include "DolphinWX/Frame.h"
#include "DolphinWX/GameListCtrl.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/InputConfigDiag.h"
#include "DolphinWX/ISOFile.h"
#include "DolphinWX/LogWindow.h"
#include "DolphinWX/MemcardManager.h"
#include "DolphinWX/TASInputDlg.h"
#include "DolphinWX/WXInputBase.h"
#include "DolphinWX/WxUtils.h"
#include "DolphinWX/Cheats/CheatsWindow.h"
#include "DolphinWX/Config/ConfigMain.h"
#include "DolphinWX/Debugger/BreakpointWindow.h"
#include "DolphinWX/Debugger/CodeWindow.h"
#include "DolphinWX/Debugger/WatchWindow.h"
#include "DolphinWX/NetPlay/NetPlaySetupFrame.h"
#include "DolphinWX/NetPlay/NetWindow.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

#ifdef _WIN32
#ifndef SM_XVIRTUALSCREEN
#define SM_XVIRTUALSCREEN 76
#endif
#ifndef SM_YVIRTUALSCREEN
#define SM_YVIRTUALSCREEN 77
#endif
#ifndef SM_CXVIRTUALSCREEN
#define SM_CXVIRTUALSCREEN 78
#endif
#ifndef SM_CYVIRTUALSCREEN
#define SM_CYVIRTUALSCREEN 79
#endif
#endif

class InputConfig;
class wxFrame;

// This override allows returning a fake menubar object while removing the real one from the screen
wxMenuBar* CFrame::GetMenuBar() const
{
	if (m_frameMenuBar)
	{
		return m_frameMenuBar;
	}
	else
	{
		return m_menubar_shadow;
	}
}

// Create menu items
// ---------------------
wxMenuBar* CFrame::CreateMenu()
{
	wxMenuBar* menubar = new wxMenuBar();

	// file menu
	wxMenu* fileMenu = new wxMenu;
	fileMenu->Append(wxID_OPEN, GetMenuLabel(HK_OPEN));
	fileMenu->Append(IDM_CHANGE_DISC, GetMenuLabel(HK_CHANGE_DISC));

	wxMenu *externalDrive = new wxMenu;
	fileMenu->Append(IDM_DRIVES, _("&Boot from DVD Backup..."), externalDrive);

	drives = cdio_get_devices();
	// Windows Limitation of 24 character drives
	for (unsigned int i = 0; i < drives.size() && i < 24; i++)
	{
		externalDrive->Append(IDM_DRIVE1 + i, StrToWxStr(drives[i]));
	}

	fileMenu->AppendSeparator();
	fileMenu->Append(wxID_REFRESH, GetMenuLabel(HK_REFRESH_LIST));
	fileMenu->AppendSeparator();
	fileMenu->Append(wxID_EXIT, _("E&xit") + wxString("\tAlt+F4"));
	menubar->Append(fileMenu, _("&File"));

	// Emulation menu
	wxMenu* emulationMenu = new wxMenu;
	emulationMenu->Append(IDM_PLAY, GetMenuLabel(HK_PLAY_PAUSE));
	emulationMenu->Append(IDM_STOP, GetMenuLabel(HK_STOP));
	emulationMenu->Append(IDM_RESET, GetMenuLabel(HK_RESET));
	emulationMenu->AppendSeparator();
	emulationMenu->Append(IDM_TOGGLE_FULLSCREEN, GetMenuLabel(HK_FULLSCREEN));
	emulationMenu->Append(IDM_FRAMESTEP, GetMenuLabel(HK_FRAME_ADVANCE), wxEmptyString);

	wxMenu *skippingMenu = new wxMenu;
	emulationMenu->AppendSubMenu(skippingMenu, _("Frame S&kipping"));
	for (int i = 0; i < 10; i++)
		skippingMenu->AppendRadioItem(IDM_FRAME_SKIP_0 + i, wxString::Format("%i", i));
	skippingMenu->Check(IDM_FRAME_SKIP_0 + SConfig::GetInstance().m_FrameSkip, true);
	Movie::SetFrameSkipping(SConfig::GetInstance().m_FrameSkip);

	emulationMenu->AppendSeparator();
	emulationMenu->Append(IDM_SCREENSHOT, GetMenuLabel(HK_SCREENSHOT));

	emulationMenu->AppendSeparator();
	wxMenu *saveMenu = new wxMenu;
	wxMenu *loadMenu = new wxMenu;
	wxMenu *slotSelectMenu = new wxMenu;
	emulationMenu->Append(IDM_LOAD_STATE, _("&Load State"), loadMenu);
	emulationMenu->Append(IDM_SAVE_STATE, _("Sa&ve State"), saveMenu);
	emulationMenu->Append(IDM_SELECT_SLOT, _("Select State Slot"), slotSelectMenu);

	saveMenu->Append(IDM_SAVE_STATE_FILE, GetMenuLabel(HK_SAVE_STATE_FILE));
	saveMenu->Append(IDM_SAVE_SELECTED_SLOT, GetMenuLabel(HK_SAVE_STATE_SLOT_SELECTED));
	saveMenu->Append(IDM_SAVE_FIRST_STATE, GetMenuLabel(HK_SAVE_FIRST_STATE));
	saveMenu->Append(IDM_UNDO_SAVE_STATE, GetMenuLabel(HK_UNDO_SAVE_STATE));
	saveMenu->AppendSeparator();

	loadMenu->Append(IDM_LOAD_STATE_FILE, GetMenuLabel(HK_LOAD_STATE_FILE));
	loadMenu->Append(IDM_LOAD_SELECTED_SLOT, GetMenuLabel(HK_LOAD_STATE_SLOT_SELECTED));
	loadMenu->Append(IDM_UNDO_LOAD_STATE, GetMenuLabel(HK_UNDO_LOAD_STATE));
	loadMenu->AppendSeparator();

	for (unsigned int i = 0; i < State::NUM_STATES; i++)
	{
		loadMenu->Append(IDM_LOAD_SLOT_1 + i, GetMenuLabel(HK_LOAD_STATE_SLOT_1 + i));
		saveMenu->Append(IDM_SAVE_SLOT_1 + i, GetMenuLabel(HK_SAVE_STATE_SLOT_1 + i));
		slotSelectMenu->Append(IDM_SELECT_SLOT_1 + i, GetMenuLabel(HK_SELECT_STATE_SLOT_1 + i));
	}

	loadMenu->AppendSeparator();
	for (unsigned int i = 0; i < State::NUM_STATES; i++)
		loadMenu->Append(IDM_LOAD_LAST_1 + i, GetMenuLabel(HK_LOAD_LAST_STATE_1 + i));

	menubar->Append(emulationMenu, _("&Emulation"));

	// Movie menu
	wxMenu* movieMenu = new wxMenu;
	movieMenu->Append(IDM_RECORD, GetMenuLabel(HK_START_RECORDING));
	movieMenu->Append(IDM_PLAY_RECORD, GetMenuLabel(HK_PLAY_RECORDING));
	movieMenu->Append(IDM_RECORD_EXPORT, GetMenuLabel(HK_EXPORT_RECORDING));
	movieMenu->Append(IDM_RECORD_READ_ONLY, GetMenuLabel(HK_READ_ONLY_MODE), wxEmptyString, wxITEM_CHECK);
	movieMenu->Append(IDM_TAS_INPUT, _("TAS Input"));
	movieMenu->AppendSeparator();
	movieMenu->AppendCheckItem(IDM_TOGGLE_PAUSE_MOVIE, _("Pause at End of Movie"));
	movieMenu->Check(IDM_TOGGLE_PAUSE_MOVIE, SConfig::GetInstance().m_PauseMovie);
	movieMenu->AppendCheckItem(IDM_SHOW_LAG, _("Show Lag Counter"));
	movieMenu->Check(IDM_SHOW_LAG, SConfig::GetInstance().m_ShowLag);
	movieMenu->AppendCheckItem(IDM_SHOW_FRAME_COUNT, _("Show Frame Counter"));
	movieMenu->Check(IDM_SHOW_FRAME_COUNT, SConfig::GetInstance().m_ShowFrameCount);
	movieMenu->Check(IDM_RECORD_READ_ONLY, true);
	movieMenu->AppendCheckItem(IDM_SHOW_INPUT_DISPLAY, _("Show Input Display"));
	movieMenu->Check(IDM_SHOW_INPUT_DISPLAY, SConfig::GetInstance().m_ShowInputDisplay);
	movieMenu->AppendSeparator();
	movieMenu->AppendCheckItem(IDM_TOGGLE_DUMP_FRAMES, _("Dump Frames"));
	movieMenu->Check(IDM_TOGGLE_DUMP_FRAMES, SConfig::GetInstance().m_DumpFrames);
	movieMenu->AppendCheckItem(IDM_TOGGLE_DUMP_AUDIO, _("Dump Audio"));
	movieMenu->Check(IDM_TOGGLE_DUMP_AUDIO, SConfig::GetInstance().m_DumpAudio);
	menubar->Append(movieMenu, _("&Movie"));

	// Options menu
	wxMenu* pOptionsMenu = new wxMenu;
	pOptionsMenu->Append(wxID_PREFERENCES, _("Co&nfigure..."));
	pOptionsMenu->AppendSeparator();
	pOptionsMenu->Append(IDM_CONFIG_GFX_BACKEND, _("&Graphics Settings"));
	pOptionsMenu->Append(IDM_CONFIG_AUDIO, _("&Audio Settings"));
	pOptionsMenu->Append(IDM_CONFIG_CONTROLLERS, _("&Controller Settings"));
	pOptionsMenu->Append(IDM_CONFIG_HOTKEYS, _("&Hotkey Settings"));
	if (g_pCodeWindow)
	{
		pOptionsMenu->AppendSeparator();
		g_pCodeWindow->CreateMenuOptions(pOptionsMenu);
	}
	menubar->Append(pOptionsMenu, _("&Options"));

	// Tools menu
	wxMenu* toolsMenu = new wxMenu;
	toolsMenu->Append(IDM_MEMCARD, _("&Memcard Manager (GC)"));
	toolsMenu->Append(IDM_IMPORT_SAVE, _("Import Wii Save"));
	toolsMenu->Append(IDM_EXPORT_ALL_SAVE, _("Export All Wii Saves"));
	toolsMenu->Append(IDM_CHEATS, _("&Cheat Manager"));

	toolsMenu->Append(IDM_NETPLAY, _("Start &NetPlay"));

	toolsMenu->Append(IDM_MENU_INSTALL_WAD, _("Install WAD"));
	UpdateWiiMenuChoice(toolsMenu->Append(IDM_LOAD_WII_MENU, "Dummy string to keep wxw happy"));

	toolsMenu->Append(IDM_FIFOPLAYER, _("FIFO Player"));

	toolsMenu->AppendSeparator();
	wxMenu* wiimoteMenu = new wxMenu;
	toolsMenu->AppendSubMenu(wiimoteMenu, _("Connect Wiimotes"));
	wiimoteMenu->AppendCheckItem(IDM_CONNECT_WIIMOTE1, GetMenuLabel(HK_WIIMOTE1_CONNECT));
	wiimoteMenu->AppendCheckItem(IDM_CONNECT_WIIMOTE2, GetMenuLabel(HK_WIIMOTE2_CONNECT));
	wiimoteMenu->AppendCheckItem(IDM_CONNECT_WIIMOTE3, GetMenuLabel(HK_WIIMOTE3_CONNECT));
	wiimoteMenu->AppendCheckItem(IDM_CONNECT_WIIMOTE4, GetMenuLabel(HK_WIIMOTE4_CONNECT));
	wiimoteMenu->AppendSeparator();
	wiimoteMenu->AppendCheckItem(IDM_CONNECT_BALANCEBOARD, GetMenuLabel(HK_BALANCEBOARD_CONNECT));

	menubar->Append(toolsMenu, _("&Tools"));

	wxMenu* viewMenu = new wxMenu;
	viewMenu->AppendCheckItem(IDM_TOGGLE_TOOLBAR, _("Show &Toolbar"));
	viewMenu->Check(IDM_TOGGLE_TOOLBAR, SConfig::GetInstance().m_InterfaceToolbar);
	viewMenu->AppendCheckItem(IDM_TOGGLE_STATUSBAR, _("Show &Status Bar"));
	viewMenu->Check(IDM_TOGGLE_STATUSBAR, SConfig::GetInstance().m_InterfaceStatusbar);
	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(IDM_LOG_WINDOW, _("Show &Log"));
	viewMenu->AppendCheckItem(IDM_LOG_CONFIG_WINDOW, _("Show Log &Configuration"));
	viewMenu->AppendSeparator();

	if (g_pCodeWindow)
	{
		viewMenu->Check(IDM_LOG_WINDOW, g_pCodeWindow->bShowOnStart[0]);

		static const wxString menu_text[] = {
			_("&Registers"),
			_("&Watch"),
			_("&Breakpoints"),
			_("&Memory"),
			_("&JIT"),
			_("&Sound"),
			_("&Video")
		};

		for (int i = IDM_REGISTER_WINDOW; i <= IDM_VIDEO_WINDOW; i++)
		{
			viewMenu->AppendCheckItem(i, menu_text[i - IDM_REGISTER_WINDOW]);
			viewMenu->Check(i, g_pCodeWindow->bShowOnStart[i - IDM_LOG_WINDOW]);
		}

		viewMenu->AppendSeparator();
	}
	else
	{
		viewMenu->Check(IDM_LOG_WINDOW, SConfig::GetInstance().m_InterfaceLogWindow);
		viewMenu->Check(IDM_LOG_CONFIG_WINDOW, SConfig::GetInstance().m_InterfaceLogConfigWindow);
	}

	wxMenu *platformMenu = new wxMenu;
	viewMenu->AppendSubMenu(platformMenu, _("Show Platforms"));
	platformMenu->AppendCheckItem(IDM_LIST_WII, _("Show Wii"));
	platformMenu->Check(IDM_LIST_WII, SConfig::GetInstance().m_ListWii);
	platformMenu->AppendCheckItem(IDM_LIST_GC, _("Show GameCube"));
	platformMenu->Check(IDM_LIST_GC, SConfig::GetInstance().m_ListGC);
	platformMenu->AppendCheckItem(IDM_LIST_WAD, _("Show WAD"));
	platformMenu->Check(IDM_LIST_WAD, SConfig::GetInstance().m_ListWad);
	platformMenu->AppendCheckItem(IDM_LIST_ELFDOL, _("Show ELF/DOL"));
	platformMenu->Check(IDM_LIST_ELFDOL, SConfig::GetInstance().m_ListElfDol);

	wxMenu *regionMenu = new wxMenu;
	viewMenu->AppendSubMenu(regionMenu, _("Show Regions"));
	regionMenu->AppendCheckItem(IDM_LIST_JAP, _("Show JAP"));
	regionMenu->Check(IDM_LIST_JAP, SConfig::GetInstance().m_ListJap);
	regionMenu->AppendCheckItem(IDM_LIST_PAL, _("Show PAL"));
	regionMenu->Check(IDM_LIST_PAL, SConfig::GetInstance().m_ListPal);
	regionMenu->AppendCheckItem(IDM_LIST_USA, _("Show USA"));
	regionMenu->Check(IDM_LIST_USA, SConfig::GetInstance().m_ListUsa);
	regionMenu->AppendSeparator();
	regionMenu->AppendCheckItem(IDM_LIST_AUSTRALIA, _("Show Australia"));
	regionMenu->Check(IDM_LIST_AUSTRALIA, SConfig::GetInstance().m_ListAustralia);
	regionMenu->AppendCheckItem(IDM_LIST_FRANCE, _("Show France"));
	regionMenu->Check(IDM_LIST_FRANCE, SConfig::GetInstance().m_ListFrance);
	regionMenu->AppendCheckItem(IDM_LIST_GERMANY, _("Show Germany"));
	regionMenu->Check(IDM_LIST_GERMANY, SConfig::GetInstance().m_ListGermany);
	regionMenu->AppendCheckItem(IDM_LIST_ITALY, _("Show Italy"));
	regionMenu->Check(IDM_LIST_ITALY, SConfig::GetInstance().m_ListItaly);
	regionMenu->AppendCheckItem(IDM_LIST_KOREA, _("Show Korea"));
	regionMenu->Check(IDM_LIST_KOREA, SConfig::GetInstance().m_ListKorea);
	regionMenu->AppendCheckItem(IDM_LIST_NETHERLANDS, _("Show Netherlands"));
	regionMenu->Check(IDM_LIST_NETHERLANDS, SConfig::GetInstance().m_ListNetherlands);
	regionMenu->AppendCheckItem(IDM_LIST_RUSSIA, _("Show Russia"));
	regionMenu->Check(IDM_LIST_RUSSIA, SConfig::GetInstance().m_ListRussia);
	regionMenu->AppendCheckItem(IDM_LIST_SPAIN, _("Show Spain"));
	regionMenu->Check(IDM_LIST_SPAIN, SConfig::GetInstance().m_ListSpain);
	regionMenu->AppendCheckItem(IDM_LIST_TAIWAN, _("Show Taiwan"));
	regionMenu->Check(IDM_LIST_TAIWAN, SConfig::GetInstance().m_ListTaiwan);
	regionMenu->AppendCheckItem(IDM_LIST_WORLD, _("Show World"));
	regionMenu->Check(IDM_LIST_WORLD, SConfig::GetInstance().m_ListWorld);
	regionMenu->AppendCheckItem(IDM_LIST_UNKNOWN, _("Show Unknown"));
	regionMenu->Check(IDM_LIST_UNKNOWN, SConfig::GetInstance().m_ListUnknown);

	viewMenu->AppendCheckItem(IDM_LIST_DRIVES, _("Show Drives"));
	viewMenu->Check(IDM_LIST_DRIVES, SConfig::GetInstance().m_ListDrives);
	viewMenu->Append(IDM_PURGE_GAME_LIST_CACHE, _("Purge Game List Cache"));

	wxMenu *columnsMenu = new wxMenu;
	viewMenu->AppendSubMenu(columnsMenu, _("Select Columns"));
	columnsMenu->AppendCheckItem(IDM_SHOW_SYSTEM, _("Platform"));
	columnsMenu->Check(IDM_SHOW_SYSTEM, SConfig::GetInstance().m_showSystemColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_BANNER, _("Banner"));
	columnsMenu->Check(IDM_SHOW_BANNER, SConfig::GetInstance().m_showBannerColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_MAKER, _("Maker"));
	columnsMenu->Check(IDM_SHOW_MAKER, SConfig::GetInstance().m_showMakerColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_FILENAME, _("File Name"));
	columnsMenu->Check(IDM_SHOW_FILENAME, SConfig::GetInstance().m_showFileNameColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_ID, _("Game ID"));
	columnsMenu->Check(IDM_SHOW_ID, SConfig::GetInstance().m_showIDColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_REGION, _("Region"));
	columnsMenu->Check(IDM_SHOW_REGION, SConfig::GetInstance().m_showRegionColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_SIZE, _("File Size"));
	columnsMenu->Check(IDM_SHOW_SIZE, SConfig::GetInstance().m_showSizeColumn);
	columnsMenu->AppendCheckItem(IDM_SHOW_STATE, _("State"));
	columnsMenu->Check(IDM_SHOW_STATE, SConfig::GetInstance().m_showStateColumn);



	menubar->Append(viewMenu, _("&View"));

	if (g_pCodeWindow)
	{
		g_pCodeWindow->CreateMenu(SConfig::GetInstance(), menubar);
	}

	// Help menu
	wxMenu* helpMenu = new wxMenu;
	// Re-enable when there's something useful to display */
	// helpMenu->Append(wxID_HELP, _("&Help"));
	helpMenu->Append(IDM_HELP_WEBSITE, _("&Website"));
	helpMenu->Append(IDM_HELP_ONLINE_DOCS, _("Online &Documentation"));
	helpMenu->Append(IDM_HELP_GITHUB, _("&GitHub Repository"));
	helpMenu->AppendSeparator();
	helpMenu->Append(wxID_ABOUT, _("&About..."));
	menubar->Append(helpMenu, _("&Help"));

	return menubar;
}

wxString CFrame::GetMenuLabel(int Id)
{
	wxString Label;

	switch (Id)
	{
		case HK_OPEN:
			Label = _("&Open...");
			break;
		case HK_CHANGE_DISC:
			Label = _("Change &Disc...");
			break;
		case HK_REFRESH_LIST:
			Label = _("&Refresh List");
			break;

		case HK_PLAY_PAUSE:
			if (Core::GetState() == Core::CORE_RUN)
				Label = _("&Pause");
			else
				Label = _("&Play");
			break;
		case HK_STOP:
			Label = _("&Stop");
			break;
		case HK_RESET:
			Label = _("&Reset");
			break;
		case HK_FRAME_ADVANCE:
			Label = _("&Frame Advance");
			break;

		case HK_START_RECORDING:
			Label = _("Start Re&cording Input");
			break;
		case HK_PLAY_RECORDING:
			Label = _("P&lay Input Recording...");
			break;
		case HK_EXPORT_RECORDING:
			Label = _("Export Recording...");
			break;
		case HK_READ_ONLY_MODE:
			Label = _("&Read-Only Mode");
			break;

		case HK_FULLSCREEN:
			Label = _("&Fullscreen");
			break;
		case HK_SCREENSHOT:
			Label = _("Take Screenshot");
			break;
		case HK_EXIT:
			Label = _("Exit");
			break;

		case HK_WIIMOTE1_CONNECT:
		case HK_WIIMOTE2_CONNECT:
		case HK_WIIMOTE3_CONNECT:
		case HK_WIIMOTE4_CONNECT:
			Label = wxString::Format(_("Connect Wiimote %i"),
					Id - HK_WIIMOTE1_CONNECT + 1);
			break;
		case HK_BALANCEBOARD_CONNECT:
			Label = _("Connect Balance Board");
			break;
		case HK_LOAD_STATE_SLOT_1:
		case HK_LOAD_STATE_SLOT_2:
		case HK_LOAD_STATE_SLOT_3:
		case HK_LOAD_STATE_SLOT_4:
		case HK_LOAD_STATE_SLOT_5:
		case HK_LOAD_STATE_SLOT_6:
		case HK_LOAD_STATE_SLOT_7:
		case HK_LOAD_STATE_SLOT_8:
		case HK_LOAD_STATE_SLOT_9:
		case HK_LOAD_STATE_SLOT_10:
			Label = wxString::Format(_("Slot %i - %s"),
			        Id - HK_LOAD_STATE_SLOT_1 + 1,
			        StrToWxStr(State::GetInfoStringOfSlot(Id - HK_LOAD_STATE_SLOT_1 + 1)));
			break;

		case HK_SAVE_STATE_SLOT_1:
		case HK_SAVE_STATE_SLOT_2:
		case HK_SAVE_STATE_SLOT_3:
		case HK_SAVE_STATE_SLOT_4:
		case HK_SAVE_STATE_SLOT_5:
		case HK_SAVE_STATE_SLOT_6:
		case HK_SAVE_STATE_SLOT_7:
		case HK_SAVE_STATE_SLOT_8:
		case HK_SAVE_STATE_SLOT_9:
		case HK_SAVE_STATE_SLOT_10:
			Label = wxString::Format(_("Slot %i - %s"),
			        Id - HK_SAVE_STATE_SLOT_1 + 1,
			        StrToWxStr(State::GetInfoStringOfSlot(Id - HK_SAVE_STATE_SLOT_1 + 1)));
			break;
		case HK_SAVE_STATE_FILE:
			Label = _("Save State...");
			break;

		case HK_LOAD_LAST_STATE_1:
		case HK_LOAD_LAST_STATE_2:
		case HK_LOAD_LAST_STATE_3:
		case HK_LOAD_LAST_STATE_4:
		case HK_LOAD_LAST_STATE_5:
		case HK_LOAD_LAST_STATE_6:
		case HK_LOAD_LAST_STATE_7:
		case HK_LOAD_LAST_STATE_8:
		case HK_LOAD_LAST_STATE_9:
		case HK_LOAD_LAST_STATE_10:
			Label = wxString::Format(_("Last %i"),
				Id - HK_LOAD_LAST_STATE_1 + 1);
			break;
		case HK_LOAD_STATE_FILE:
			Label = _("Load State...");
			break;

		case HK_SAVE_FIRST_STATE: Label = _("Save Oldest State"); break;
		case HK_UNDO_LOAD_STATE:  Label = _("Undo Load State");   break;
		case HK_UNDO_SAVE_STATE:  Label = _("Undo Save State");   break;

		case HK_SAVE_STATE_SLOT_SELECTED:
			Label = _("Save state to selected slot");
			break;

		case HK_LOAD_STATE_SLOT_SELECTED:
			Label = _("Load state from selected slot");
			break;

		case HK_SELECT_STATE_SLOT_1:
		case HK_SELECT_STATE_SLOT_2:
		case HK_SELECT_STATE_SLOT_3:
		case HK_SELECT_STATE_SLOT_4:
		case HK_SELECT_STATE_SLOT_5:
		case HK_SELECT_STATE_SLOT_6:
		case HK_SELECT_STATE_SLOT_7:
		case HK_SELECT_STATE_SLOT_8:
		case HK_SELECT_STATE_SLOT_9:
		case HK_SELECT_STATE_SLOT_10:
			Label = wxString::Format(_("Select Slot %i - %s"),
			        Id - HK_SELECT_STATE_SLOT_1 + 1,
			        StrToWxStr(State::GetInfoStringOfSlot(Id - HK_SELECT_STATE_SLOT_1 + 1)));
			break;


		default:
			Label = wxString::Format(_("Undefined %i"), Id);
	}

	return Label;
}


// Create toolbar items
// ---------------------
void CFrame::PopulateToolbar(wxToolBar* ToolBar)
{
	int w = m_Bitmaps[Toolbar_FileOpen].GetWidth(),
		h = m_Bitmaps[Toolbar_FileOpen].GetHeight();
	ToolBar->SetToolBitmapSize(wxSize(w, h));


	WxUtils::AddToolbarButton(ToolBar, wxID_OPEN,               _("Open"),        m_Bitmaps[Toolbar_FileOpen],    _("Open file..."));
	WxUtils::AddToolbarButton(ToolBar, wxID_REFRESH,            _("Refresh"),     m_Bitmaps[Toolbar_Refresh],     _("Refresh game list"));
	ToolBar->AddSeparator();
	WxUtils::AddToolbarButton(ToolBar, IDM_PLAY,                _("Play"),        m_Bitmaps[Toolbar_Play],        _("Play"));
	WxUtils::AddToolbarButton(ToolBar, IDM_STOP,                _("Stop"),        m_Bitmaps[Toolbar_Stop],        _("Stop"));
	WxUtils::AddToolbarButton(ToolBar, IDM_TOGGLE_FULLSCREEN,   _("FullScr"),     m_Bitmaps[Toolbar_FullScreen],  _("Toggle fullscreen"));
	WxUtils::AddToolbarButton(ToolBar, IDM_SCREENSHOT,          _("ScrShot"),     m_Bitmaps[Toolbar_Screenshot],  _("Take screenshot"));
	ToolBar->AddSeparator();
	WxUtils::AddToolbarButton(ToolBar, wxID_PREFERENCES,        _("Config"),      m_Bitmaps[Toolbar_ConfigMain],  _("Configure..."));
	WxUtils::AddToolbarButton(ToolBar, IDM_CONFIG_GFX_BACKEND,  _("Graphics"),    m_Bitmaps[Toolbar_ConfigGFX],   _("Graphics settings"));
	WxUtils::AddToolbarButton(ToolBar, IDM_CONFIG_CONTROLLERS,  _("Controllers"), m_Bitmaps[Toolbar_Controller],  _("Controller settings"));
}


// Delete and recreate the toolbar
void CFrame::RecreateToolbar()
{
	static const long TOOLBAR_STYLE = wxTB_DEFAULT_STYLE | wxTB_TEXT;

	if (m_ToolBar != nullptr)
	{
		m_ToolBar->Destroy();
		m_ToolBar = nullptr;
	}

	m_ToolBar = CreateToolBar(TOOLBAR_STYLE, wxID_ANY);

	if (g_pCodeWindow)
	{
		g_pCodeWindow->PopulateToolbar(m_ToolBar);
		m_ToolBar->AddSeparator();
	}

	PopulateToolbar(m_ToolBar);
	// after adding the buttons to the toolbar, must call Realize() to reflect
	// the changes
	m_ToolBar->Realize();

	UpdateGUI();
}

void CFrame::InitBitmaps()
{
	auto const dir = StrToWxStr(File::GetThemeDir(SConfig::GetInstance().theme_name));

	m_Bitmaps[Toolbar_FileOpen   ].LoadFile(dir + "open.png",       wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Refresh    ].LoadFile(dir + "refresh.png",    wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Play       ].LoadFile(dir + "play.png",       wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Stop       ].LoadFile(dir + "stop.png",       wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Pause      ].LoadFile(dir + "pause.png",      wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_ConfigMain ].LoadFile(dir + "config.png",     wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_ConfigGFX  ].LoadFile(dir + "graphics.png",   wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Controller ].LoadFile(dir + "classic.png",    wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_Screenshot ].LoadFile(dir + "screenshot.png", wxBITMAP_TYPE_PNG);
	m_Bitmaps[Toolbar_FullScreen ].LoadFile(dir + "fullscreen.png", wxBITMAP_TYPE_PNG);

	// Update in case the bitmap has been updated
	if (m_ToolBar != nullptr)
		RecreateToolbar();
}

// Menu items

// Start the game or change the disc.
// Boot priority:
// 1. Show the game list and boot the selected game.
// 2. Default ISO
// 3. Boot last selected game
void CFrame::BootGame(const std::string& filename)
{
	std::string bootfile = filename;
	SConfig& StartUp = SConfig::GetInstance();

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
		return;

	// Start filename if non empty.
	// Start the selected ISO, or try one of the saved paths.
	// If all that fails, ask to add a dir and don't boot
	if (bootfile.empty())
	{
		if (m_GameListCtrl->GetSelectedISO() != nullptr)
		{
			if (m_GameListCtrl->GetSelectedISO()->IsValid())
				bootfile = m_GameListCtrl->GetSelectedISO()->GetFileName();
		}
		else if (!StartUp.m_strDefaultISO.empty() &&
		         File::Exists(StartUp.m_strDefaultISO))
		{
			bootfile = StartUp.m_strDefaultISO;
		}
		else
		{
			if (!SConfig::GetInstance().m_LastFilename.empty() &&
			    File::Exists(SConfig::GetInstance().m_LastFilename))
			{
				bootfile = SConfig::GetInstance().m_LastFilename;
			}
			else
			{
				m_GameListCtrl->BrowseForDirectory();
				return;
			}
		}
	}
	if (!bootfile.empty())
	{
		StartGame(bootfile);
		if (UseDebugger && g_pCodeWindow)
		{
			if (g_pCodeWindow->m_WatchWindow)
				g_pCodeWindow->m_WatchWindow->LoadAll();
			if (g_pCodeWindow->m_BreakpointWindow)
				g_pCodeWindow->m_BreakpointWindow->LoadAll();
		}
	}
}

// Open file to boot
void CFrame::OnOpen(wxCommandEvent& WXUNUSED (event))
{
	if (Core::GetState() == Core::CORE_UNINITIALIZED)
		DoOpen(true);
}

void CFrame::DoOpen(bool Boot)
{
	std::string currentDir = File::GetCurrentDir();

	wxString path = wxFileSelector(
			_("Select the file to load"),
			wxEmptyString, wxEmptyString, wxEmptyString,
			_("All GC/Wii files (elf, dol, gcm, iso, wbfs, ciso, gcz, wad)") +
			wxString::Format("|*.elf;*.dol;*.gcm;*.iso;*.wbfs;*.ciso;*.gcz;*.wad;*.dff;*.tmd|%s",
				wxGetTranslation(wxALL_FILES)),
			wxFD_OPEN | wxFD_FILE_MUST_EXIST,
			this);

	if (path.IsEmpty())
		return;

	std::string currentDir2 = File::GetCurrentDir();

	if (currentDir != currentDir2)
	{
		PanicAlertT("Current directory changed from %s to %s after wxFileSelector!",
				currentDir.c_str(), currentDir2.c_str());
		File::SetCurrentDir(currentDir);
	}

	// Should we boot a new game or just change the disc?
	if (Boot && !path.IsEmpty())
	{
		BootGame(WxStrToStr(path));
	}
	else
	{
		DVDInterface::ChangeDisc(WxStrToStr(path));
	}
}

void CFrame::OnRecordReadOnly(wxCommandEvent& event)
{
	Movie::SetReadOnly(event.IsChecked());
}

void CFrame::OnTASInput(wxCommandEvent& event)
{
	for (int i = 0; i < 4; ++i)
	{
		if (SConfig::GetInstance().m_SIDevice[i] != SIDEVICE_NONE && SConfig::GetInstance().m_SIDevice[i] != SIDEVICE_GC_GBA)
		{
			g_TASInputDlg[i]->CreateGCLayout();
			g_TASInputDlg[i]->Show();
			g_TASInputDlg[i]->SetTitle(wxString::Format(_("TAS Input - Controller %d"), i + 1));
		}

		if (g_wiimote_sources[i] == WIIMOTE_SRC_EMU && !(Core::IsRunning() && !SConfig::GetInstance().bWii))
		{
			g_TASInputDlg[i+4]->CreateWiiLayout(i);
			g_TASInputDlg[i+4]->Show();
			g_TASInputDlg[i+4]->SetTitle(wxString::Format(_("TAS Input - Wiimote %d"), i + 1));
		}
	}
}

void CFrame::OnTogglePauseMovie(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_PauseMovie = !SConfig::GetInstance().m_PauseMovie;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnToggleDumpFrames(wxCommandEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_DumpFrames = !SConfig::GetInstance().m_DumpFrames;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnToggleDumpAudio(wxCommandEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_DumpAudio = !SConfig::GetInstance().m_DumpAudio;
}

void CFrame::OnShowLag(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_ShowLag = !SConfig::GetInstance().m_ShowLag;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnShowFrameCount(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().m_ShowFrameCount = !SConfig::GetInstance().m_ShowFrameCount;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnShowInputDisplay(wxCommandEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_ShowInputDisplay = !SConfig::GetInstance().m_ShowInputDisplay;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnFrameStep(wxCommandEvent& event)
{
	bool wasPaused = (Core::GetState() == Core::CORE_PAUSE);

	Movie::DoFrameStep();

	bool isPaused = (Core::GetState() == Core::CORE_PAUSE);
	if (isPaused && !wasPaused) // don't update on unpause, otherwise the status would be wrong when pausing next frame
		UpdateGUI();
}

void CFrame::OnChangeDisc(wxCommandEvent& WXUNUSED (event))
{
	DoOpen(false);
}

void CFrame::OnRecord(wxCommandEvent& WXUNUSED (event))
{
	if ((!Core::IsRunningAndStarted() && Core::IsRunning()) || Movie::IsRecordingInput() || Movie::IsPlayingInput())
		return;

	int controllers = 0;

	if (Movie::IsReadOnly())
	{
		// The user just chose to record a movie, so that should take precedence
		Movie::SetReadOnly(false);
		GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Check(false);
	}

	for (int i = 0; i < 4; i++)
	{
		if (SIDevice_IsGCController(SConfig::GetInstance().m_SIDevice[i]))
			controllers |= (1 << i);

		if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
			controllers |= (1 << (i + 4));
	}

	if (Movie::BeginRecordingInput(controllers))
		BootGame("");
}

void CFrame::OnPlayRecording(wxCommandEvent& WXUNUSED (event))
{
	wxString path = wxFileSelector(
			_("Select The Recording File"),
			wxEmptyString, wxEmptyString, wxEmptyString,
			_("Dolphin TAS Movies (*.dtm)") +
				wxString::Format("|*.dtm|%s", wxGetTranslation(wxALL_FILES)),
			wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST,
			this);

	if (path.IsEmpty())
		return;

	if (!Movie::IsReadOnly())
	{
		// let's make the read-only flag consistent at the start of a movie.
		Movie::SetReadOnly(true);
		GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Check();
	}

	if (Movie::PlayInput(WxStrToStr(path)))
		BootGame("");
}

void CFrame::OnRecordExport(wxCommandEvent& WXUNUSED (event))
{
	DoRecordingSave();
}

void CFrame::OnPlay(wxCommandEvent& WXUNUSED (event))
{
	if (Core::IsRunning())
	{
		// Core is initialized and emulator is running
		if (UseDebugger)
		{
			CPU::EnableStepping(!CPU::IsStepping());

			wxThread::Sleep(20);
			g_pCodeWindow->JumpToAddress(PC);
			g_pCodeWindow->Update();
			// Update toolbar with Play/Pause status
			UpdateGUI();
		}
		else
		{
			DoPause();
		}
	}
	else
	{
		// Core is uninitialized, start the game
		BootGame("");
	}
}

void CFrame::OnRenderParentClose(wxCloseEvent& event)
{
	// Before closing the window we need to shut down the emulation core.
	// We'll try to close this window again once that is done.
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		DoStop();
		if (event.CanVeto())
		{
			event.Veto();
		}
		return;
	}

	event.Skip();
}

void CFrame::OnRenderParentMove(wxMoveEvent& event)
{
	if (Core::GetState() != Core::CORE_UNINITIALIZED &&
		!RendererIsFullscreen() && !m_RenderFrame->IsMaximized() && !m_RenderFrame->IsIconized())
	{
		SConfig::GetInstance().iRenderWindowXPos = m_RenderFrame->GetPosition().x;
		SConfig::GetInstance().iRenderWindowYPos = m_RenderFrame->GetPosition().y;
	}
	event.Skip();
}

void CFrame::OnRenderParentResize(wxSizeEvent& event)
{
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		int width, height;
		if (!SConfig::GetInstance().bRenderToMain &&
			!RendererIsFullscreen() && !m_RenderFrame->IsMaximized() && !m_RenderFrame->IsIconized())
		{
			m_RenderFrame->GetClientSize(&width, &height);
			SConfig::GetInstance().iRenderWindowWidth = width;
			SConfig::GetInstance().iRenderWindowHeight = height;
		}
		m_LogWindow->Refresh();
		m_LogWindow->Update();
	}
	event.Skip();
}

void CFrame::ToggleDisplayMode(bool bFullscreen)
{
#ifdef _WIN32
	if (bFullscreen && SConfig::GetInstance().strFullscreenResolution != "Auto")
	{
		DEVMODE dmScreenSettings;
		memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
		dmScreenSettings.dmSize = sizeof(dmScreenSettings);
		sscanf(SConfig::GetInstance().strFullscreenResolution.c_str(),
				"%dx%d", &dmScreenSettings.dmPelsWidth, &dmScreenSettings.dmPelsHeight);
		dmScreenSettings.dmBitsPerPel = 32;
		dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

		// Try To Set Selected Mode And Get Results.  NOTE: CDS_FULLSCREEN Gets Rid Of Start Bar.
		ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN);
	}
	else
	{
		// Change to default resolution
		ChangeDisplaySettings(nullptr, CDS_FULLSCREEN);
	}
#elif defined(HAVE_XRANDR) && HAVE_XRANDR
	if (SConfig::GetInstance().strFullscreenResolution != "Auto")
		m_XRRConfig->ToggleDisplayMode(bFullscreen);
#endif
}

// Prepare the GUI to start the game.
void CFrame::StartGame(const std::string& filename)
{
	if (m_bGameLoading)
		return;
	m_bGameLoading = true;

	if (m_ToolBar)
		m_ToolBar->EnableTool(IDM_PLAY, false);
	GetMenuBar()->FindItem(IDM_PLAY)->Enable(false);

	if (SConfig::GetInstance().bRenderToMain)
	{
		// Game has been started, hide the game list
		m_GameListCtrl->Disable();
		m_GameListCtrl->Hide();

		m_RenderParent = m_Panel;
		m_RenderFrame = this;
		if (SConfig::GetInstance().bKeepWindowOnTop)
			m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() | wxSTAY_ON_TOP);
		else
			m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);

		// No, I really don't want TAB_TRAVERSAL being set behind my back,
		// thanks.  (Note that calling DisableSelfFocus would prevent this flag
		// from being set for new children, but wouldn't reset the existing
		// flag.)
		m_RenderParent->SetWindowStyle(m_RenderParent->GetWindowStyle() & ~wxTAB_TRAVERSAL);
	}
	else
	{
		wxPoint position(SConfig::GetInstance().iRenderWindowXPos,
				SConfig::GetInstance().iRenderWindowYPos);
#ifdef __APPLE__
		// On OS X, the render window's title bar is not visible,
		// and the window therefore not easily moved, when the
		// position is 0,0. Weed out the 0's from existing configs.
		if (position == wxPoint(0, 0))
			position = wxDefaultPosition;
#endif

		wxSize size(SConfig::GetInstance().iRenderWindowWidth,
				SConfig::GetInstance().iRenderWindowHeight);
#ifdef _WIN32
		// Out of desktop check
		int leftPos = GetSystemMetrics(SM_XVIRTUALSCREEN);
		int topPos = GetSystemMetrics(SM_YVIRTUALSCREEN);
		int width =  GetSystemMetrics(SM_CXVIRTUALSCREEN);
		int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if ((leftPos + width) < (position.x + size.GetWidth()) || leftPos > position.x || (topPos + height) < (position.y + size.GetHeight()) || topPos > position.y)
			position.x = position.y = wxDefaultCoord;
#endif
		m_RenderFrame = new CRenderFrame((wxFrame*)this, wxID_ANY, _("Dolphin"), position);
		if (SConfig::GetInstance().bKeepWindowOnTop)
			m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() | wxSTAY_ON_TOP);
		else
			m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);

		m_RenderFrame->SetBackgroundColour(*wxBLACK);
		m_RenderFrame->SetClientSize(size.GetWidth(), size.GetHeight());
		m_RenderFrame->Bind(wxEVT_CLOSE_WINDOW, &CFrame::OnRenderParentClose, this);
		m_RenderFrame->Bind(wxEVT_ACTIVATE, &CFrame::OnActive, this);
		m_RenderFrame->Bind(wxEVT_MOVE, &CFrame::OnRenderParentMove, this);
#ifdef _WIN32
		// The renderer should use a top-level window for exclusive fullscreen support.
		m_RenderParent = m_RenderFrame;
#else
		// To capture key events on Linux and Mac OS X the frame needs at least one child.
		m_RenderParent = new wxPanel(m_RenderFrame, IDM_MPANEL, wxDefaultPosition, wxDefaultSize, 0);
#endif

		m_RenderFrame->Show();
	}

#if defined(__APPLE__)
	NSView *view = (NSView *) m_RenderFrame->GetHandle();
	NSWindow *window = [view window];

	[window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
#endif

	wxBeginBusyCursor();

	DoFullscreen(SConfig::GetInstance().bFullscreen);

	if (!BootManager::BootCore(filename))
	{
		DoFullscreen(false);
		// Destroy the renderer frame when not rendering to main
		if (!SConfig::GetInstance().bRenderToMain)
			m_RenderFrame->Destroy();
		m_RenderParent = nullptr;
		m_bGameLoading = false;
		UpdateGUI();
	}
	else
	{
#if defined(HAVE_X11) && HAVE_X11
	if (SConfig::GetInstance().bDisableScreenSaver)
		X11Utils::InhibitScreensaver(X11Utils::XDisplayFromHandle(GetHandle()),
				X11Utils::XWindowFromHandle(GetHandle()), true);
#endif

		m_RenderParent->SetFocus();

		wxTheApp->Bind(wxEVT_KEY_DOWN,     &CFrame::OnKeyDown,     this);
		wxTheApp->Bind(wxEVT_RIGHT_DOWN,   &CFrame::OnMouse,       this);
		wxTheApp->Bind(wxEVT_RIGHT_UP,     &CFrame::OnMouse,       this);
		wxTheApp->Bind(wxEVT_MIDDLE_DOWN,  &CFrame::OnMouse,       this);
		wxTheApp->Bind(wxEVT_MIDDLE_UP,    &CFrame::OnMouse,       this);
		wxTheApp->Bind(wxEVT_MOTION,       &CFrame::OnMouse,       this);
		wxTheApp->Bind(wxEVT_SET_FOCUS,    &CFrame::OnFocusChange, this);
		wxTheApp->Bind(wxEVT_KILL_FOCUS,   &CFrame::OnFocusChange, this);
		m_RenderParent->Bind(wxEVT_SIZE, &CFrame::OnRenderParentResize, this);
	}

	wxEndBusyCursor();
}

void CFrame::OnBootDrive(wxCommandEvent& event)
{
	BootGame(drives[event.GetId()-IDM_DRIVE1]);
}

// Refresh the file list and browse for a favorites directory
void CFrame::OnRefresh(wxCommandEvent& WXUNUSED (event))
{
	if (m_GameListCtrl)
	{
		m_GameListCtrl->Update();
	}
}

// Create screenshot
void CFrame::OnScreenshot(wxCommandEvent& WXUNUSED (event))
{
	Core::SaveScreenShot();
}

// Pause the emulation
void CFrame::DoPause()
{
	if (Core::GetState() == Core::CORE_RUN)
	{
		Core::SetState(Core::CORE_PAUSE);
		if (SConfig::GetInstance().bHideCursor)
			m_RenderParent->SetCursor(wxNullCursor);
		Core::UpdateTitle();
	}
	else
	{
		Core::SetState(Core::CORE_RUN);
		if (SConfig::GetInstance().bHideCursor &&
				RendererHasFocus())
			m_RenderParent->SetCursor(wxCURSOR_BLANK);
	}
	UpdateGUI();
}

// Stop the emulation
void CFrame::DoStop()
{
	if (!Core::IsRunningAndStarted())
		return;
	if (m_confirmStop)
		return;

	// don't let this function run again until it finishes, or is aborted.
	m_confirmStop = true;

	m_bGameLoading = false;
	if (Core::GetState() != Core::CORE_UNINITIALIZED ||
			m_RenderParent != nullptr)
	{
#if defined __WXGTK__
		wxMutexGuiLeave();
		std::lock_guard<std::recursive_mutex> lk(keystate_lock);
		wxMutexGuiEnter();
#endif
		// Ask for confirmation in case the user accidentally clicked Stop / Escape
		if (SConfig::GetInstance().bConfirmStop)
		{
			// Exit fullscreen to ensure it does not cover the stop dialog.
			DoFullscreen(false);

			// Pause the state during confirmation and restore it afterwards
			Core::EState state = Core::GetState();

			// If exclusive fullscreen is not enabled then we can pause the emulation
			// before we've exited fullscreen. If not then we need to exit fullscreen first.
			if (!RendererIsFullscreen() || !g_Config.ExclusiveFullscreenEnabled() ||
				SConfig::GetInstance().bRenderToMain)
			{
				Core::SetState(Core::CORE_PAUSE);
			}

			wxMessageDialog m_StopDlg(
				this,
				_("Do you want to stop the current emulation?"),
				_("Please confirm..."),
				wxYES_NO | wxSTAY_ON_TOP | wxICON_EXCLAMATION,
				wxDefaultPosition);

			HotkeyManagerEmu::Enable(false);
			int Ret = m_StopDlg.ShowModal();
			HotkeyManagerEmu::Enable(true);
			if (Ret != wxID_YES)
			{
				Core::SetState(state);
				m_confirmStop = false;
				return;
			}
		}

		if (UseDebugger && g_pCodeWindow)
		{
			if (g_pCodeWindow->m_WatchWindow)
			{
				g_pCodeWindow->m_WatchWindow->SaveAll();
				PowerPC::watches.Clear();
			}
			if (g_pCodeWindow->m_BreakpointWindow)
			{
				g_pCodeWindow->m_BreakpointWindow->SaveAll();
				PowerPC::breakpoints.Clear();
				PowerPC::memchecks.Clear();
				g_pCodeWindow->m_BreakpointWindow->NotifyUpdate();
			}
			g_symbolDB.Clear();
			Host_NotifyMapLoaded();
		}

		// TODO: Show the author/description dialog here
		if (Movie::IsRecordingInput())
			DoRecordingSave();
		if (Movie::IsMovieActive())
			Movie::EndPlayInput(false);

		if (NetPlayDialog::GetNetPlayClient())
			NetPlayDialog::GetNetPlayClient()->Stop();

		BootManager::Stop();
		UpdateGUI();
	}
}

void CFrame::OnStopped()
{
	m_confirmStop = false;

#if defined(HAVE_X11) && HAVE_X11
	if (SConfig::GetInstance().bDisableScreenSaver)
		X11Utils::InhibitScreensaver(X11Utils::XDisplayFromHandle(GetHandle()),
				X11Utils::XWindowFromHandle(GetHandle()), false);
#endif
	m_RenderFrame->SetTitle(StrToWxStr(scm_rev_str));

	// Destroy the renderer frame when not rendering to main
	m_RenderParent->Unbind(wxEVT_SIZE, &CFrame::OnRenderParentResize, this);

	// Mouse
	wxTheApp->Unbind(wxEVT_RIGHT_DOWN,  &CFrame::OnMouse, this);
	wxTheApp->Unbind(wxEVT_RIGHT_UP,    &CFrame::OnMouse, this);
	wxTheApp->Unbind(wxEVT_MIDDLE_DOWN, &CFrame::OnMouse, this);
	wxTheApp->Unbind(wxEVT_MIDDLE_UP,   &CFrame::OnMouse, this);
	wxTheApp->Unbind(wxEVT_MOTION,      &CFrame::OnMouse, this);
	if (SConfig::GetInstance().bHideCursor)
		m_RenderParent->SetCursor(wxNullCursor);
	DoFullscreen(false);
	if (!SConfig::GetInstance().bRenderToMain)
	{
		m_RenderFrame->Destroy();
	}
	else
	{
#if defined(__APPLE__)
		// Disable the full screen button when not in a game.
		NSView *view = (NSView *)m_RenderFrame->GetHandle();
		NSWindow *window = [view window];

		[window setCollectionBehavior : NSWindowCollectionBehaviorDefault];
#endif

		// Make sure the window is not longer set to stay on top
		m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);
	}
	m_RenderParent = nullptr;

	// Clean framerate indications from the status bar.
	GetStatusBar()->SetStatusText(" ", 0);

	// Clear wiimote connection status from the status bar.
	GetStatusBar()->SetStatusText(" ", 1);

	// If batch mode was specified on the command-line or we were already closing, exit now.
	if (m_bBatchMode || m_bClosing)
		Close(true);

	// If using auto size with render to main, reset the application size.
	if (SConfig::GetInstance().bRenderToMain &&
		SConfig::GetInstance().bRenderWindowAutoSize)
		SetSize(SConfig::GetInstance().iWidth,
		SConfig::GetInstance().iHeight);

	m_GameListCtrl->Enable();
	m_GameListCtrl->Show();
	m_GameListCtrl->SetFocus();
	UpdateGUI();
}

void CFrame::DoRecordingSave()
{
	bool paused = (Core::GetState() == Core::CORE_PAUSE);

	if (!paused)
		DoPause();

	wxString path = wxFileSelector(
			_("Select The Recording File"),
			wxEmptyString, wxEmptyString, wxEmptyString,
			_("Dolphin TAS Movies (*.dtm)") +
				wxString::Format("|*.dtm|%s", wxGetTranslation(wxALL_FILES)),
			wxFD_SAVE | wxFD_PREVIEW | wxFD_OVERWRITE_PROMPT,
			this);

	if (path.IsEmpty())
		return;

	Movie::SaveRecording(WxStrToStr(path));

	if (!paused)
		DoPause();
}

void CFrame::OnStop(wxCommandEvent& WXUNUSED (event))
{
	DoStop();
}

void CFrame::OnReset(wxCommandEvent& WXUNUSED (event))
{
	if (Movie::IsRecordingInput())
		Movie::g_bReset = true;
	ProcessorInterface::ResetButton_Tap();
}

void CFrame::OnConfigMain(wxCommandEvent& WXUNUSED (event))
{
	CConfigMain ConfigMain(this);
	HotkeyManagerEmu::Enable(false);
	if (ConfigMain.ShowModal() == wxID_OK)
		m_GameListCtrl->Update();
	HotkeyManagerEmu::Enable(true);
	UpdateGUI();
}

void CFrame::OnConfigGFX(wxCommandEvent& WXUNUSED (event))
{
	HotkeyManagerEmu::Enable(false);
	if (g_video_backend)
		g_video_backend->ShowConfig(this);
	HotkeyManagerEmu::Enable(true);
}

void CFrame::OnConfigAudio(wxCommandEvent& WXUNUSED (event))
{
	CConfigMain ConfigMain(this);
	ConfigMain.SetSelectedTab(CConfigMain::ID_AUDIOPAGE);
	HotkeyManagerEmu::Enable(false);
	if (ConfigMain.ShowModal() == wxID_OK)
		m_GameListCtrl->Update();
	HotkeyManagerEmu::Enable(true);
}

void CFrame::OnConfigControllers(wxCommandEvent& WXUNUSED (event))
{
	ControllerConfigDiag config_dlg(this);
	HotkeyManagerEmu::Enable(false);
	config_dlg.ShowModal();
	HotkeyManagerEmu::Enable(true);
}

void CFrame::OnConfigHotkey(wxCommandEvent& WXUNUSED (event))
{
	InputConfig* const hotkey_plugin = HotkeyManagerEmu::GetConfig();

	// check if game is running
	bool game_running = false;
	if (Core::GetState() == Core::CORE_RUN)
	{
		Core::SetState(Core::CORE_PAUSE);
		game_running = true;
	}

	HotkeyManagerEmu::Enable(false);

	InputConfigDialog m_ConfigFrame(this, *hotkey_plugin, _("Dolphin Hotkeys"));
	m_ConfigFrame.ShowModal();

	// Update references in case controllers were refreshed
	Wiimote::LoadConfig();
	Keyboard::LoadConfig();
	Pad::LoadConfig();
	HotkeyManagerEmu::LoadConfig();

	HotkeyManagerEmu::Enable(true);

	// if game isn't running
	if (game_running)
	{
		Core::SetState(Core::CORE_RUN);
	}

	// Update the GUI in case menu accelerators were changed
	UpdateGUI();
}

void CFrame::OnHelp(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case wxID_ABOUT:
		{
			AboutDolphin frame(this);
			HotkeyManagerEmu::Enable(false);
			frame.ShowModal();
			HotkeyManagerEmu::Enable(true);
		}
		break;
	case IDM_HELP_WEBSITE:
		WxUtils::Launch("https://dolphin-emu.org/");
		break;
	case IDM_HELP_ONLINE_DOCS:
		WxUtils::Launch("https://dolphin-emu.org/docs/guides/");
		break;
	case IDM_HELP_GITHUB:
		WxUtils::Launch("https://github.com/dolphin-emu/dolphin");
		break;
	}
}

void CFrame::ClearStatusBar()
{
	if (this->GetStatusBar()->IsEnabled())
	{
		this->GetStatusBar()->SetStatusText("", 0);
	}
}

void CFrame::StatusBarMessage(const char * Text, ...)
{
	const int MAX_BYTES = 1024 * 10;
	char Str[MAX_BYTES];
	va_list ArgPtr;
	va_start(ArgPtr, Text);
	vsnprintf(Str, MAX_BYTES, Text, ArgPtr);
	va_end(ArgPtr);

	if (this->GetStatusBar()->IsEnabled())
	{
		this->GetStatusBar()->SetStatusText(StrToWxStr(Str), 0);
	}
}


// Miscellaneous menus
// ---------------------
// NetPlay stuff
void CFrame::OnNetPlay(wxCommandEvent& WXUNUSED (event))
{
	if (!g_NetPlaySetupDiag)
	{
		if (NetPlayDialog::GetInstance() != nullptr)
			NetPlayDialog::GetInstance()->Raise();
		else
			g_NetPlaySetupDiag = new NetPlaySetupFrame(this, m_GameListCtrl);
	}
	else
	{
		g_NetPlaySetupDiag->Raise();
	}
}

void CFrame::OnMemcard(wxCommandEvent& WXUNUSED (event))
{
	CMemcardManager MemcardManager(this);
	HotkeyManagerEmu::Enable(false);
	MemcardManager.ShowModal();
	HotkeyManagerEmu::Enable(true);
}

void CFrame::OnExportAllSaves(wxCommandEvent& WXUNUSED (event))
{
	CWiiSaveCrypted::ExportAllSaves();
}

void CFrame::OnImportSave(wxCommandEvent& WXUNUSED (event))
{
	wxString path = wxFileSelector(_("Select the save file"),
			wxEmptyString, wxEmptyString, wxEmptyString,
			_("Wii save files (*.bin)") + "|*.bin|" + wxGetTranslation(wxALL_FILES),
			wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST,
			this);

	if (!path.IsEmpty())
		CWiiSaveCrypted::ImportWiiSave(WxStrToStr(path));
}

void CFrame::OnShowCheatsWindow(wxCommandEvent& WXUNUSED (event))
{
	if (!g_CheatsWindow)
		g_CheatsWindow = new wxCheatsWindow(this);
	else
		g_CheatsWindow->Raise();
}

void CFrame::OnLoadWiiMenu(wxCommandEvent& WXUNUSED(event))
{
	BootGame(Common::GetTitleContentPath(TITLEID_SYSMENU, Common::FROM_CONFIGURED_ROOT));
}

void CFrame::OnInstallWAD(wxCommandEvent& event)
{
	std::string fileName;

	switch (event.GetId())
	{
	case IDM_LIST_INSTALL_WAD:
	{
		const GameListItem *iso = m_GameListCtrl->GetSelectedISO();
		if (!iso)
			return;
		fileName = iso->GetFileName();
		break;
	}
	case IDM_MENU_INSTALL_WAD:
	{
		wxString path = wxFileSelector(
			_("Select a Wii WAD file to install"),
			wxEmptyString, wxEmptyString, wxEmptyString,
			_("Wii WAD files (*.wad)") + "|*.wad|" + wxGetTranslation(wxALL_FILES),
			wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST,
			this);
		fileName = WxStrToStr(path);
		break;
	}
	default:
		return;
	}

	wxProgressDialog dialog(_("Installing WAD..."),
		_("Working..."),
		1000,
		this,
		wxPD_APP_MODAL |
		wxPD_ELAPSED_TIME | wxPD_ESTIMATED_TIME | wxPD_REMAINING_TIME |
		wxPD_SMOOTH
		);

	u64 titleID = DiscIO::CNANDContentManager::Access().Install_WiiWAD(fileName);
	if (titleID == TITLEID_SYSMENU)
	{
		UpdateWiiMenuChoice();
	}
}


void CFrame::UpdateWiiMenuChoice(wxMenuItem *WiiMenuItem)
{
	if (!WiiMenuItem)
	{
		WiiMenuItem = GetMenuBar()->FindItem(IDM_LOAD_WII_MENU);
	}

	const DiscIO::CNANDContentLoader & SysMenu_Loader = DiscIO::CNANDContentManager::Access().GetNANDLoader(TITLEID_SYSMENU, Common::FROM_CONFIGURED_ROOT);
	if (SysMenu_Loader.IsValid())
	{
		int sysmenuVersion = SysMenu_Loader.GetTitleVersion();
		char sysmenuRegion = SysMenu_Loader.GetCountryChar();
		WiiMenuItem->Enable();
		WiiMenuItem->SetItemLabel(wxString::Format(_("Load Wii System Menu %d%c"), sysmenuVersion, sysmenuRegion));
	}
	else
	{
		WiiMenuItem->Enable(false);
		WiiMenuItem->SetItemLabel(_("Load Wii System Menu"));
	}
}

void CFrame::OnFifoPlayer(wxCommandEvent& WXUNUSED (event))
{
	if (m_FifoPlayerDlg)
	{
		m_FifoPlayerDlg->Show();
		m_FifoPlayerDlg->SetFocus();
	}
	else
	{
		m_FifoPlayerDlg = new FifoPlayerDlg(this);
	}
}

void CFrame::ConnectWiimote(int wm_idx, bool connect)
{
	if (Core::IsRunning() && SConfig::GetInstance().bWii)
	{
		bool was_unpaused = Core::PauseAndLock(true);
		GetUsbPointer()->AccessWiiMote(wm_idx | 0x100)->Activate(connect);
		wxString msg(wxString::Format(_("Wiimote %i %s"), wm_idx + 1,
					connect ? _("Connected") : _("Disconnected")));
		Core::DisplayMessage(WxStrToStr(msg), 3000);
		Host_UpdateMainFrame();
		Core::PauseAndLock(false, was_unpaused);
	}
}

void CFrame::OnConnectWiimote(wxCommandEvent& event)
{
	bool was_unpaused = Core::PauseAndLock(true);
	ConnectWiimote(event.GetId() - IDM_CONNECT_WIIMOTE1, !GetUsbPointer()->AccessWiiMote((event.GetId() - IDM_CONNECT_WIIMOTE1) | 0x100)->IsConnected());
	Core::PauseAndLock(false, was_unpaused);
}

// Toggle fullscreen. In Windows the fullscreen mode is accomplished by expanding the m_Panel to cover
// the entire screen (when we render to the main window).
void CFrame::OnToggleFullscreen(wxCommandEvent& WXUNUSED (event))
{
	DoFullscreen(!RendererIsFullscreen());
}

void CFrame::OnToggleDualCore(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().bCPUThread = !SConfig::GetInstance().bCPUThread;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnToggleSkipIdle(wxCommandEvent& WXUNUSED (event))
{
	SConfig::GetInstance().bSkipIdle = !SConfig::GetInstance().bSkipIdle;
	SConfig::GetInstance().SaveSettings();
}

void CFrame::OnLoadStateFromFile(wxCommandEvent& WXUNUSED (event))
{
	wxString path = wxFileSelector(
		_("Select the state to load"),
		wxEmptyString, wxEmptyString, wxEmptyString,
		_("All Save States (sav, s##)") +
			wxString::Format("|*.sav;*.s??|%s", wxGetTranslation(wxALL_FILES)),
		wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST,
		this);

	if (!path.IsEmpty())
		State::LoadAs(WxStrToStr(path));
}

void CFrame::OnSaveStateToFile(wxCommandEvent& WXUNUSED (event))
{
	wxString path = wxFileSelector(
		_("Select the state to save"),
		wxEmptyString, wxEmptyString, wxEmptyString,
		_("All Save States (sav, s##)") +
			wxString::Format("|*.sav;*.s??|%s", wxGetTranslation(wxALL_FILES)),
		wxFD_SAVE,
		this);

	if (!path.IsEmpty())
		State::SaveAs(WxStrToStr(path));
}

void CFrame::OnLoadLastState(wxCommandEvent& event)
{
	if (Core::IsRunningAndStarted())
	{
		int id = event.GetId();
		int slot = id - IDM_LOAD_LAST_1 + 1;
		State::LoadLastSaved(slot);
	}
}

void CFrame::OnSaveFirstState(wxCommandEvent& WXUNUSED(event))
{
	if (Core::IsRunningAndStarted())
		State::SaveFirstSaved();
}

void CFrame::OnUndoLoadState(wxCommandEvent& WXUNUSED (event))
{
	if (Core::IsRunningAndStarted())
		State::UndoLoadState();
}

void CFrame::OnUndoSaveState(wxCommandEvent& WXUNUSED (event))
{
	if (Core::IsRunningAndStarted())
		State::UndoSaveState();
}


void CFrame::OnLoadState(wxCommandEvent& event)
{
	if (Core::IsRunningAndStarted())
	{
		int id = event.GetId();
		int slot = id - IDM_LOAD_SLOT_1 + 1;
		State::Load(slot);
	}
}

void CFrame::OnSaveState(wxCommandEvent& event)
{
	if (Core::IsRunningAndStarted())
	{
		int id = event.GetId();
		int slot = id - IDM_SAVE_SLOT_1 + 1;
		State::Save(slot);
	}
}

void CFrame::OnFrameSkip(wxCommandEvent& event)
{
	int amount = event.GetId() - IDM_FRAME_SKIP_0;

	Movie::SetFrameSkipping((unsigned int)amount);
	SConfig::GetInstance().m_FrameSkip = amount;
}

void CFrame::OnSelectSlot(wxCommandEvent& event)
{
	g_saveSlot = event.GetId() - IDM_SELECT_SLOT_1 + 1;
	Core::DisplayMessage(StringFromFormat("Selected slot %d - %s", g_saveSlot, State::GetInfoStringOfSlot(g_saveSlot).c_str()), 2500);
}

void CFrame::OnLoadCurrentSlot(wxCommandEvent& event)
{
	if (Core::IsRunningAndStarted())
	{
		State::Load(g_saveSlot);
	}
}

void CFrame::OnSaveCurrentSlot(wxCommandEvent& event)
{
	if (Core::IsRunningAndStarted())
	{
		State::Save(g_saveSlot);
	}
}



// GUI
// ---------------------

// Update the enabled/disabled status
void CFrame::UpdateGUI()
{
	// Save status
	bool Initialized     = Core::IsRunning();
	bool Running         = Core::GetState() == Core::CORE_RUN;
	bool Paused          = Core::GetState() == Core::CORE_PAUSE;
	bool Stopping        = Core::GetState() == Core::CORE_STOPPING;
	bool RunningWii      = Initialized && SConfig::GetInstance().bWii;

	// Make sure that we have a toolbar
	if (m_ToolBar)
	{
		// Enable/disable the Config and Stop buttons
		m_ToolBar->EnableTool(wxID_OPEN, !Initialized);
		// Don't allow refresh when we don't show the list
		m_ToolBar->EnableTool(wxID_REFRESH, !Initialized);
		m_ToolBar->EnableTool(IDM_STOP, Running || Paused);
		m_ToolBar->EnableTool(IDM_TOGGLE_FULLSCREEN, Running || Paused);
		m_ToolBar->EnableTool(IDM_SCREENSHOT, Running || Paused);
	}

	// File
	GetMenuBar()->FindItem(wxID_OPEN)->Enable(!Initialized);
	GetMenuBar()->FindItem(IDM_DRIVES)->Enable(!Initialized);
	GetMenuBar()->FindItem(wxID_REFRESH)->Enable(!Initialized);

	// Emulation
	GetMenuBar()->FindItem(IDM_STOP)->Enable(Running || Paused);
	GetMenuBar()->FindItem(IDM_RESET)->Enable(Running || Paused);
	GetMenuBar()->FindItem(IDM_RECORD)->Enable(!Movie::IsRecordingInput());
	GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable(!Initialized);
	GetMenuBar()->FindItem(IDM_RECORD_EXPORT)->Enable(Movie::IsMovieActive());
	GetMenuBar()->FindItem(IDM_FRAMESTEP)->Enable(Running || Paused);
	GetMenuBar()->FindItem(IDM_SCREENSHOT)->Enable(Running || Paused);
	GetMenuBar()->FindItem(IDM_TOGGLE_FULLSCREEN)->Enable(Running || Paused);

	// Update Key Shortcuts
	for (unsigned int i = 0; i < NUM_HOTKEYS; i++)
	{
		if (GetCmdForHotkey(i) == -1)
			continue;
		if (GetMenuBar()->FindItem(GetCmdForHotkey(i)))
			GetMenuBar()->FindItem(GetCmdForHotkey(i))->SetItemLabel(GetMenuLabel(i));
	}

	GetMenuBar()->FindItem(IDM_LOAD_STATE)->Enable(Initialized);
	GetMenuBar()->FindItem(IDM_SAVE_STATE)->Enable(Initialized);
	// Misc
	GetMenuBar()->FindItem(IDM_CHANGE_DISC)->Enable(Initialized);
	if (DiscIO::CNANDContentManager::Access().GetNANDLoader(TITLEID_SYSMENU, Common::FROM_CONFIGURED_ROOT).IsValid())
		GetMenuBar()->FindItem(IDM_LOAD_WII_MENU)->Enable(!Initialized);

	// Tools
	GetMenuBar()->FindItem(IDM_CHEATS)->Enable(SConfig::GetInstance().bEnableCheats);

	GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE1)->Enable(RunningWii);
	GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE2)->Enable(RunningWii);
	GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE3)->Enable(RunningWii);
	GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE4)->Enable(RunningWii);
	GetMenuBar()->FindItem(IDM_CONNECT_BALANCEBOARD)->Enable(RunningWii);
	if (RunningWii)
	{
		bool was_unpaused = Core::PauseAndLock(true);
		GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE1)->Check(GetUsbPointer()->
				AccessWiiMote(0x0100)->IsConnected());
		GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE2)->Check(GetUsbPointer()->
				AccessWiiMote(0x0101)->IsConnected());
		GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE3)->Check(GetUsbPointer()->
				AccessWiiMote(0x0102)->IsConnected());
		GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE4)->Check(GetUsbPointer()->
				AccessWiiMote(0x0103)->IsConnected());
		GetMenuBar()->FindItem(IDM_CONNECT_BALANCEBOARD)->Check(GetUsbPointer()->
				AccessWiiMote(0x0104)->IsConnected());
		Core::PauseAndLock(false, was_unpaused);
	}

	if (m_ToolBar)
	{
		// Get the tool that controls pausing/playing
		wxToolBarToolBase * PlayTool = m_ToolBar->FindById(IDM_PLAY);

		if (PlayTool)
		{
			int position = m_ToolBar->GetToolPos(IDM_PLAY);

			if (Running)
			{
				m_ToolBar->DeleteTool(IDM_PLAY);
				m_ToolBar->InsertTool(position, IDM_PLAY, _("Pause"), m_Bitmaps[Toolbar_Pause],
				                      WxUtils::CreateDisabledButtonBitmap(m_Bitmaps[Toolbar_Pause]),
				                      wxITEM_NORMAL, _("Pause"));
			}
			else
			{
				m_ToolBar->DeleteTool(IDM_PLAY);
				m_ToolBar->InsertTool(position, IDM_PLAY, _("Play"), m_Bitmaps[Toolbar_Play],
				                      WxUtils::CreateDisabledButtonBitmap(m_Bitmaps[Toolbar_Play]),
				                      wxITEM_NORMAL, _("Play"));
			}
			m_ToolBar->Realize();
		}
	}

	GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Enable(Running || Paused);

	if (!Initialized && !m_bGameLoading)
	{
		if (m_GameListCtrl->IsEnabled())
		{
			// Prepare to load Default ISO, enable play button
			if (!SConfig::GetInstance().m_strDefaultISO.empty())
			{
				if (m_ToolBar)
					m_ToolBar->EnableTool(IDM_PLAY, true);
				GetMenuBar()->FindItem(IDM_PLAY)->Enable();
				GetMenuBar()->FindItem(IDM_RECORD)->Enable();
				GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
			}
			// Prepare to load last selected file, enable play button
			else if (!SConfig::GetInstance().m_LastFilename.empty() &&
			         File::Exists(SConfig::GetInstance().m_LastFilename))
			{
				if (m_ToolBar)
					m_ToolBar->EnableTool(IDM_PLAY, true);
				GetMenuBar()->FindItem(IDM_PLAY)->Enable();
				GetMenuBar()->FindItem(IDM_RECORD)->Enable();
				GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
			}
			else
			{
				// No game has been selected yet, disable play button
				if (m_ToolBar)
					m_ToolBar->EnableTool(IDM_PLAY, false);
				GetMenuBar()->FindItem(IDM_PLAY)->Enable(false);
				GetMenuBar()->FindItem(IDM_RECORD)->Enable(false);
				GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable(false);
			}
		}

		// Game has not started, show game list
		if (!m_GameListCtrl->IsShown())
		{
			m_GameListCtrl->Enable();
			m_GameListCtrl->Show();
		}
		// Game has been selected but not started, enable play button
		if (m_GameListCtrl->GetSelectedISO() != nullptr && m_GameListCtrl->IsEnabled())
		{
			if (m_ToolBar)
				m_ToolBar->EnableTool(IDM_PLAY, true);
			GetMenuBar()->FindItem(IDM_PLAY)->Enable();
			GetMenuBar()->FindItem(IDM_RECORD)->Enable();
			GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
		}
	}
	else if (Initialized)
	{
		// Game has been loaded, enable the pause button
		if (m_ToolBar)
			m_ToolBar->EnableTool(IDM_PLAY, !Stopping);
		GetMenuBar()->FindItem(IDM_PLAY)->Enable(!Stopping);

		// Reset game loading flag
		m_bGameLoading = false;
	}

	// Refresh toolbar
	if (m_ToolBar)
	{
		m_ToolBar->Refresh();
	}

	// Commit changes to manager
	m_Mgr->Update();

	// Update non-modal windows
	if (g_CheatsWindow)
	{
		if (SConfig::GetInstance().bEnableCheats)
			g_CheatsWindow->UpdateGUI();
		else
			g_CheatsWindow->Close();
	}
}

void CFrame::UpdateGameList()
{
	m_GameListCtrl->Update();
}

void CFrame::GameListChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case IDM_LIST_WII:
		SConfig::GetInstance().m_ListWii = event.IsChecked();
		break;
	case IDM_LIST_GC:
		SConfig::GetInstance().m_ListGC = event.IsChecked();
		break;
	case IDM_LIST_WAD:
		SConfig::GetInstance().m_ListWad = event.IsChecked();
		break;
	case IDM_LIST_ELFDOL:
		SConfig::GetInstance().m_ListElfDol = event.IsChecked();
		break;
	case IDM_LIST_JAP:
		SConfig::GetInstance().m_ListJap = event.IsChecked();
		break;
	case IDM_LIST_PAL:
		SConfig::GetInstance().m_ListPal = event.IsChecked();
		break;
	case IDM_LIST_USA:
		SConfig::GetInstance().m_ListUsa = event.IsChecked();
		break;
	case IDM_LIST_AUSTRALIA:
		SConfig::GetInstance().m_ListAustralia = event.IsChecked();
		break;
	case IDM_LIST_FRANCE:
		SConfig::GetInstance().m_ListFrance = event.IsChecked();
		break;
	case IDM_LIST_GERMANY:
		SConfig::GetInstance().m_ListGermany = event.IsChecked();
		break;
	case IDM_LIST_ITALY:
		SConfig::GetInstance().m_ListItaly = event.IsChecked();
		break;
	case IDM_LIST_KOREA:
		SConfig::GetInstance().m_ListKorea = event.IsChecked();
		break;
	case IDM_LIST_NETHERLANDS:
		SConfig::GetInstance().m_ListNetherlands = event.IsChecked();
		break;
	case IDM_LIST_RUSSIA:
		SConfig::GetInstance().m_ListRussia = event.IsChecked();
		break;
	case IDM_LIST_SPAIN:
		SConfig::GetInstance().m_ListSpain = event.IsChecked();
		break;
	case IDM_LIST_TAIWAN:
		SConfig::GetInstance().m_ListTaiwan = event.IsChecked();
		break;
	case IDM_LIST_WORLD:
		SConfig::GetInstance().m_ListWorld = event.IsChecked();
		break;
	case IDM_LIST_UNKNOWN:
		SConfig::GetInstance().m_ListUnknown = event.IsChecked();
		break;
	case IDM_LIST_DRIVES:
		SConfig::GetInstance().m_ListDrives = event.IsChecked();
		break;
	case IDM_PURGE_GAME_LIST_CACHE:
		std::vector<std::string> rFilenames = DoFileSearch({".cache"}, {File::GetUserPath(D_CACHE_IDX)});

		for (const std::string& rFilename : rFilenames)
		{
			File::Delete(rFilename);
		}
		break;
	}

	// Update gamelist
	if (m_GameListCtrl)
	{
		m_GameListCtrl->Update();
	}
}

// Enable and disable the toolbar
void CFrame::OnToggleToolbar(wxCommandEvent& event)
{
	SConfig::GetInstance().m_InterfaceToolbar = event.IsChecked();
	DoToggleToolbar(event.IsChecked());
}
void CFrame::DoToggleToolbar(bool _show)
{
	GetToolBar()->Show(_show);
	m_Mgr->Update();
}

// Enable and disable the status bar
void CFrame::OnToggleStatusbar(wxCommandEvent& event)
{
	SConfig::GetInstance().m_InterfaceStatusbar = event.IsChecked();

	GetStatusBar()->Show(event.IsChecked());

	SendSizeEvent();
}

void CFrame::OnChangeColumnsVisible(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case IDM_SHOW_SYSTEM:
		SConfig::GetInstance().m_showSystemColumn = !SConfig::GetInstance().m_showSystemColumn;
		break;
	case IDM_SHOW_BANNER:
		SConfig::GetInstance().m_showBannerColumn = !SConfig::GetInstance().m_showBannerColumn;
		break;
	case IDM_SHOW_MAKER:
		SConfig::GetInstance().m_showMakerColumn = !SConfig::GetInstance().m_showMakerColumn;
		break;
	case IDM_SHOW_FILENAME:
		SConfig::GetInstance().m_showFileNameColumn = !SConfig::GetInstance().m_showFileNameColumn;
		break;
	case IDM_SHOW_ID:
		SConfig::GetInstance().m_showIDColumn = !SConfig::GetInstance().m_showIDColumn;
		break;
	case IDM_SHOW_REGION:
		SConfig::GetInstance().m_showRegionColumn = !SConfig::GetInstance().m_showRegionColumn;
		break;
	case IDM_SHOW_SIZE:
		SConfig::GetInstance().m_showSizeColumn = !SConfig::GetInstance().m_showSizeColumn;
		break;
	case IDM_SHOW_STATE:
		SConfig::GetInstance().m_showStateColumn = !SConfig::GetInstance().m_showStateColumn;
		break;
	default: return;
	}
	m_GameListCtrl->Update();
	SConfig::GetInstance().SaveSettings();
}
