// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <FileUtil.h>
#include <IniFile.h>

#include "WxUtils.h"
#include "NetPlayClient.h"
#include "NetPlayServer.h"
#include "NetWindow.h"
#include "Frame.h"
#include "Core.h"
#include "ConfigManager.h"

#include <sstream>
#include <string>

#define NETPLAY_TITLEBAR	"Dolphin NetPlay"
#define INITIAL_PAD_BUFFER_SIZE 20

BEGIN_EVENT_TABLE(NetPlayDiag, wxFrame)
	EVT_COMMAND(wxID_ANY, wxEVT_THREAD, NetPlayDiag::OnThread)
END_EVENT_TABLE()

static NetPlayServer* netplay_server = NULL;
static NetPlayClient* netplay_client = NULL;
extern CFrame* main_frame;
NetPlayDiag *NetPlayDiag::npd = NULL;

std::string BuildGameName(const GameListItem& game)
{
	// Lang needs to be consistent
	auto const lang = 0;
	
	std::string name(game.GetBannerName(lang));
	if (name.empty())
		name = game.GetVolumeName(lang);

	if (game.GetRevision() != 0)
		return name + " (" + game.GetUniqueID() + ", Revision " + std::to_string((long long)game.GetRevision()) + ")";
	else
		return name + " (" + game.GetUniqueID() + ")";
}

void FillWithGameNames(wxListBox* game_lbox, const CGameListCtrl& game_list)
{
	for (u32 i = 0 ; auto game = game_list.GetISO(i); ++i)
		game_lbox->Append(StrToWxStr(BuildGameName(*game)));
}

NetPlaySetupDiag::NetPlaySetupDiag(wxWindow* const parent, const CGameListCtrl* const game_list)
	: wxFrame(parent, wxID_ANY, wxT(NETPLAY_TITLEBAR), wxDefaultPosition, wxDefaultSize)
	, m_game_list(game_list)
{
	IniFile inifile;
	inifile.Load(File::GetUserPath(D_CONFIG_IDX) + "Dolphin.ini");
	IniFile::Section& netplay_section = *inifile.GetOrCreateSection("NetPlay");

	wxPanel* const panel = new wxPanel(this);

	// top row
	wxStaticText* const nick_lbl = new wxStaticText(panel, wxID_ANY, _("Nickname :"),
			wxDefaultPosition, wxDefaultSize);
	
	std::string nickname;
	netplay_section.Get("Nickname", &nickname, "Player");
	m_nickname_text = new wxTextCtrl(panel, wxID_ANY, StrToWxStr(nickname));

	wxBoxSizer* const nick_szr = new wxBoxSizer(wxHORIZONTAL);
	nick_szr->Add(nick_lbl, 0, wxCENTER);
	nick_szr->Add(m_nickname_text, 0, wxALL, 5);


	// tabs
	wxNotebook* const notebook = new wxNotebook(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	wxPanel* const connect_tab = new wxPanel(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	notebook->AddPage(connect_tab, _("Connect"));
	wxPanel* const host_tab = new wxPanel(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	notebook->AddPage(host_tab, _("Host"));


	// connect tab
	{
	wxStaticText* const ip_lbl = new wxStaticText(connect_tab, wxID_ANY, _("Address :"),
			wxDefaultPosition, wxDefaultSize);
	
	std::string address;
	netplay_section.Get("Address", &address, "localhost");
	m_connect_ip_text = new wxTextCtrl(connect_tab, wxID_ANY, StrToWxStr(address));

	wxStaticText* const port_lbl = new wxStaticText(connect_tab, wxID_ANY, _("Port :"),
			wxDefaultPosition, wxDefaultSize);
	
	// string? w/e
	std::string port;
	netplay_section.Get("ConnectPort", &port, "2626");	
	m_connect_port_text = new wxTextCtrl(connect_tab, wxID_ANY, StrToWxStr(port));

	wxButton* const connect_btn = new wxButton(connect_tab, wxID_ANY, _("Connect"));
	connect_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlaySetupDiag::OnJoin, this);

	wxStaticText* const alert_lbl = new wxStaticText(connect_tab, wxID_ANY,
		_("ALERT:\n\n"
		"Netplay will only work with the following settings:\n"
		" - Enable Dual Core [OFF]\n"
		" - DSP Emulator Engine Must be the same on all computers!\n"
		" - DSP on Dedicated Thread [OFF]\n"
		" - Framelimit NOT set to [Audio]\n"
		" - Manually set the exact number of controllers to be used to [Standard Controller]\n"
		"\n"
		"All players should use the same Dolphin version and settings.\n"
		"All memory cards must be identical between players or disabled.\n"
		"Wiimote support has not been implemented!\n"
		"\n"
		"The host must have the chosen TCP port open/forwarded!\n"),
		wxDefaultPosition, wxDefaultSize);

	wxBoxSizer* const top_szr = new wxBoxSizer(wxHORIZONTAL);
	top_szr->Add(ip_lbl, 0, wxCENTER | wxRIGHT, 5);
	top_szr->Add(m_connect_ip_text, 3);
	top_szr->Add(port_lbl, 0, wxCENTER | wxRIGHT | wxLEFT, 5);
	top_szr->Add(m_connect_port_text, 1);

	wxBoxSizer* const con_szr = new wxBoxSizer(wxVERTICAL);
	con_szr->Add(top_szr, 0, wxALL | wxEXPAND, 5);
	con_szr->AddStretchSpacer(1);
	con_szr->Add(alert_lbl, 0, wxLEFT | wxRIGHT | wxEXPAND, 5);
	con_szr->AddStretchSpacer(1);
	con_szr->Add(connect_btn, 0, wxALL | wxALIGN_RIGHT, 5);

	connect_tab->SetSizerAndFit(con_szr);
	}

	// host tab
	{
	wxStaticText* const port_lbl = new wxStaticText(host_tab, wxID_ANY, _("Port :"),
			wxDefaultPosition, wxDefaultSize);
	
	// string? w/e
	std::string port;
	netplay_section.Get("HostPort", &port, "2626");	
	m_host_port_text = new wxTextCtrl(host_tab, wxID_ANY, StrToWxStr(port));

	wxButton* const host_btn = new wxButton(host_tab, wxID_ANY, _("Host"));
	host_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlaySetupDiag::OnHost, this);

	m_game_lbox = new wxListBox(host_tab, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SORT);
	m_game_lbox->Bind(wxEVT_COMMAND_LISTBOX_DOUBLECLICKED, &NetPlaySetupDiag::OnHost, this);
	
	FillWithGameNames(m_game_lbox, *game_list);

	wxBoxSizer* const top_szr = new wxBoxSizer(wxHORIZONTAL);
	top_szr->Add(port_lbl, 0, wxCENTER | wxRIGHT, 5);
	top_szr->Add(m_host_port_text, 0);
#ifdef USE_UPNP
	m_upnp_chk = new wxCheckBox(host_tab, wxID_ANY, _("Forward port (UPnP)"));
	top_szr->Add(m_upnp_chk, 0, wxALL | wxALIGN_RIGHT, 5);
#endif

	wxBoxSizer* const host_szr = new wxBoxSizer(wxVERTICAL);
	host_szr->Add(top_szr, 0, wxALL | wxEXPAND, 5);
	host_szr->Add(m_game_lbox, 1, wxLEFT | wxRIGHT | wxEXPAND, 5);
	host_szr->Add(host_btn, 0, wxALL | wxALIGN_RIGHT, 5);

	host_tab->SetSizerAndFit(host_szr);
	}

	// bottom row
	wxButton* const quit_btn = new wxButton(panel, wxID_ANY, _("Quit"));
	quit_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlaySetupDiag::OnQuit, this);

	// main sizer
	wxBoxSizer* const main_szr = new wxBoxSizer(wxVERTICAL);
	main_szr->Add(nick_szr, 0, wxALL | wxALIGN_RIGHT, 5);
	main_szr->Add(notebook, 1, wxLEFT | wxRIGHT | wxEXPAND, 5);
	main_szr->Add(quit_btn, 0, wxALL | wxALIGN_RIGHT, 5);

	panel->SetSizerAndFit(main_szr);

	//wxBoxSizer* const diag_szr = new wxBoxSizer(wxVERTICAL);
	//diag_szr->Add(panel, 1, wxEXPAND);
	//SetSizerAndFit(diag_szr);

	main_szr->SetSizeHints(this);

	Center();
	Show();
}

NetPlaySetupDiag::~NetPlaySetupDiag()
{
	IniFile inifile;
	const std::string dolphin_ini = File::GetUserPath(D_CONFIG_IDX) + "Dolphin.ini";
	inifile.Load(dolphin_ini);
	IniFile::Section& netplay_section = *inifile.GetOrCreateSection("NetPlay");

	netplay_section.Set("Nickname", WxStrToStr(m_nickname_text->GetValue()));
	netplay_section.Set("Address", WxStrToStr(m_connect_ip_text->GetValue()));
	netplay_section.Set("ConnectPort", WxStrToStr(m_connect_port_text->GetValue()));
	netplay_section.Set("HostPort", WxStrToStr(m_host_port_text->GetValue()));

	inifile.Save(dolphin_ini);
	main_frame->g_NetPlaySetupDiag = NULL;
}

void NetPlaySetupDiag::MakeNetPlayDiag(int port, const std::string &game, bool is_hosting)
{
	NetPlayDiag *&npd = NetPlayDiag::GetInstance();
	std::string ip;
	npd = new NetPlayDiag(m_parent, m_game_list, game, is_hosting);
	if (is_hosting)
		ip = "127.0.0.1";
	else
		ip = WxStrToStr(m_connect_ip_text->GetValue());

	netplay_client = new NetPlayClient(ip, (u16)port, npd, WxStrToStr(m_nickname_text->GetValue()));
	if (netplay_client->is_connected)
	{
		npd->Show();
		Destroy();
	}
	else
	{
		npd->Destroy();
	}
}

void NetPlaySetupDiag::OnHost(wxCommandEvent&)
{
	NetPlayDiag *&npd = NetPlayDiag::GetInstance();
	if (npd)
	{
		PanicAlertT("A NetPlay window is already open!!");
		return;
	}

	if (-1 == m_game_lbox->GetSelection())
	{
		PanicAlertT("You must choose a game!!");
		return;
	}

	std::string game(WxStrToStr(m_game_lbox->GetStringSelection()));

	unsigned long port = 0;
	m_host_port_text->GetValue().ToULong(&port);
	netplay_server = new NetPlayServer(u16(port));
	netplay_server->ChangeGame(game);
	netplay_server->AdjustPadBufferSize(INITIAL_PAD_BUFFER_SIZE);
	if (netplay_server->is_connected)
	{
#ifdef USE_UPNP
		if(m_upnp_chk->GetValue())
			netplay_server->TryPortmapping(port);
#endif
	}

	MakeNetPlayDiag(port, game, true);
}

void NetPlaySetupDiag::OnJoin(wxCommandEvent&)
{
	NetPlayDiag *&npd = NetPlayDiag::GetInstance();
	if (npd)
	{
		PanicAlertT("A NetPlay window is already open!!");
		return;
	}

	unsigned long port = 0;
	m_connect_port_text->GetValue().ToULong(&port);
	MakeNetPlayDiag(port, "", false);
}

void NetPlaySetupDiag::OnQuit(wxCommandEvent&)
{
	Destroy();
}

NetPlayDiag::NetPlayDiag(wxWindow* const parent, const CGameListCtrl* const game_list,
		const std::string& game, const bool is_hosting)
	: wxFrame(parent, wxID_ANY, wxT(NETPLAY_TITLEBAR), wxDefaultPosition, wxDefaultSize)
	, m_selected_game(game)
	, m_start_btn(NULL)
	, m_game_list(game_list)
{
	wxPanel* const panel = new wxPanel(this);

	// top crap
	m_game_btn = new wxButton(panel, wxID_ANY,
			StrToWxStr(m_selected_game).Prepend(_(" Game : ")),
			wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
	
	if (is_hosting)
		m_game_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnChangeGame, this);
	else
		m_game_btn->Disable();

	// middle crap

	// chat
	m_chat_text = new wxTextCtrl(panel, wxID_ANY, wxEmptyString
		, wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE);

	m_chat_msg_text = new wxTextCtrl(panel, wxID_ANY, wxEmptyString
		, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_chat_msg_text->Bind(wxEVT_COMMAND_TEXT_ENTER, &NetPlayDiag::OnChat, this);

	wxButton* const chat_msg_btn = new wxButton(panel, wxID_ANY, _("Send"));
	chat_msg_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnChat, this);

	wxBoxSizer* const chat_msg_szr = new wxBoxSizer(wxHORIZONTAL);
	chat_msg_szr->Add(m_chat_msg_text, 1);
	chat_msg_szr->Add(chat_msg_btn, 0);

	wxStaticBoxSizer* const chat_szr = new wxStaticBoxSizer(wxVERTICAL, panel, _("Chat"));
	chat_szr->Add(m_chat_text, 1, wxEXPAND);
	chat_szr->Add(chat_msg_szr, 0, wxEXPAND | wxTOP, 5);

	m_player_lbox = new wxListBox(panel, wxID_ANY, wxDefaultPosition, wxSize(256, -1));

	wxStaticBoxSizer* const player_szr = new wxStaticBoxSizer(wxVERTICAL, panel, _("Players"));
	player_szr->Add(m_player_lbox, 1, wxEXPAND);
	// player list
	if (is_hosting)
	{
		wxButton* const player_config_btn = new wxButton(panel, wxID_ANY, _("Configure Pads"));
		player_config_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnConfigPads, this);
		player_szr->Add(player_config_btn, 0, wxEXPAND | wxTOP, 5);
	}

	wxBoxSizer* const mid_szr = new wxBoxSizer(wxHORIZONTAL);
	mid_szr->Add(chat_szr, 1, wxEXPAND | wxRIGHT, 5);
	mid_szr->Add(player_szr, 0, wxEXPAND);

	// bottom crap
	wxButton* const quit_btn = new wxButton(panel, wxID_ANY, _("Quit"));
	quit_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnQuit, this);

	wxBoxSizer* const bottom_szr = new wxBoxSizer(wxHORIZONTAL);
	if (is_hosting)
	{
		m_start_btn = new wxButton(panel, wxID_ANY, _("Start"));
		m_start_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NetPlayDiag::OnStart, this);
		bottom_szr->Add(m_start_btn);

		bottom_szr->Add(new wxStaticText(panel, wxID_ANY, _("Buffer:")), 0, wxLEFT | wxCENTER, 5 );
		wxSpinCtrl* const padbuf_spin = new wxSpinCtrl(panel, wxID_ANY, wxT("20")
			, wxDefaultPosition, wxSize(64, -1), wxSP_ARROW_KEYS, 0, 200, INITIAL_PAD_BUFFER_SIZE);
		padbuf_spin->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, &NetPlayDiag::OnAdjustBuffer, this);
		bottom_szr->Add(padbuf_spin, 0, wxCENTER);

		m_memcard_write = new wxCheckBox(panel, wxID_ANY, _("Write memcards (GC)"));
		bottom_szr->Add(m_memcard_write, 0, wxCENTER);
	}

	bottom_szr->AddStretchSpacer(1);
	bottom_szr->Add(quit_btn);

	// main sizer
	wxBoxSizer* const main_szr = new wxBoxSizer(wxVERTICAL);
	main_szr->Add(m_game_btn, 0, wxEXPAND | wxALL, 5);
	main_szr->Add(mid_szr, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);
	main_szr->Add(bottom_szr, 0, wxEXPAND | wxALL, 5);

	panel->SetSizerAndFit(main_szr);

	main_szr->SetSizeHints(this);
	SetSize(512, 512-128);

	Center();
}

NetPlayDiag::~NetPlayDiag()
{
	if (netplay_client)
	{
		delete netplay_client;
		netplay_client = NULL;
	}
	if (netplay_server)
	{
		delete netplay_server;
		netplay_server = NULL;
	}
	npd = NULL;
}

void NetPlayDiag::OnChat(wxCommandEvent&)
{
	wxString s = m_chat_msg_text->GetValue();

	if (s.Length())
	{
		netplay_client->SendChatMessage(WxStrToStr(s));
		m_chat_text->AppendText(s.Prepend(wxT(" >> ")).Append(wxT('\n')));
		m_chat_msg_text->Clear();
	}
}

void NetPlayDiag::GetNetSettings(NetSettings &settings)
{
	SConfig &instance = SConfig::GetInstance();
	settings.m_DSPHLE = instance.m_LocalCoreStartupParameter.bDSPHLE;
	settings.m_DSPEnableJIT = instance.m_EnableJIT;
	settings.m_WriteToMemcard = m_memcard_write->GetValue();

	for (unsigned int i = 0; i < 4; ++i)
		settings.m_Controllers[i] = SConfig::GetInstance().m_SIDevice[i];
}

std::string NetPlayDiag::FindGame()
{
	// find path for selected game, sloppy..
	for (u32 i = 0 ; auto game = m_game_list->GetISO(i); ++i)
		if (m_selected_game == BuildGameName(*game))
			return game->GetFileName();
	
	PanicAlertT("Game not found!");
	return "";
}

void NetPlayDiag::OnStart(wxCommandEvent&)
{
	NetSettings settings;
	GetNetSettings(settings);
	netplay_server->SetNetSettings(settings);
	netplay_server->StartGame(FindGame());
}

void NetPlayDiag::BootGame(const std::string& filename)
{
	main_frame->BootGame(filename);
}

void NetPlayDiag::StopGame()
{
	main_frame->DoStop();
}

// NetPlayUI methods called from ---NETPLAY--- thread
void NetPlayDiag::Update()
{
	wxCommandEvent evt(wxEVT_THREAD, 1);
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::AppendChat(const std::string& msg)
{
	chat_msgs.Push(msg);
	// silly
	Update();
}

void NetPlayDiag::OnMsgChangeGame(const std::string& filename)
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_CHANGE_GAME);
	// TODO: using a wxString in AddPendingEvent from another thread is unsafe i guess?
	evt.SetString(StrToWxStr(filename));
	GetEventHandler()->AddPendingEvent(evt);
}

void NetPlayDiag::OnMsgStartGame()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_START_GAME);
	GetEventHandler()->AddPendingEvent(evt);
	if (m_start_btn)
		m_start_btn->Disable();
}

void NetPlayDiag::OnMsgStopGame()
{
	wxCommandEvent evt(wxEVT_THREAD, NP_GUI_EVT_STOP_GAME);
	GetEventHandler()->AddPendingEvent(evt);
	if (m_start_btn)
		m_start_btn->Enable();
}

void NetPlayDiag::OnAdjustBuffer(wxCommandEvent& event)
{
	const int val = ((wxSpinCtrl*)event.GetEventObject())->GetValue();
	netplay_server->AdjustPadBufferSize(val);

	std::ostringstream ss;
	ss << "< Pad Buffer: " << val << " >";
	netplay_client->SendChatMessage(ss.str());
	m_chat_text->AppendText(StrToWxStr(ss.str()).Append(wxT('\n')));
}

void NetPlayDiag::OnQuit(wxCommandEvent&)
{
	Destroy();
}

// update gui
void NetPlayDiag::OnThread(wxCommandEvent& event)
{
	// player list
	m_playerids.clear();
	std::string tmps;
	netplay_client->GetPlayerList(tmps, m_playerids);

	const int selection = m_player_lbox->GetSelection();

	m_player_lbox->Clear();
	std::istringstream ss(tmps);
	while (std::getline(ss, tmps))
		m_player_lbox->Append(StrToWxStr(tmps));

	m_player_lbox->SetSelection(selection);

	switch (event.GetId())
	{
	case NP_GUI_EVT_CHANGE_GAME :
		// update selected game :/
		{
		m_selected_game.assign(WxStrToStr(event.GetString()));
		m_game_btn->SetLabel(event.GetString().Prepend(_(" Game : ")));
		}
		break;
	case NP_GUI_EVT_START_GAME :
		// client start game :/
		{
		netplay_client->StartGame(FindGame());
		}
		break;
	case NP_GUI_EVT_STOP_GAME :
		// client stop game
		{
		netplay_client->StopGame();
		}
		break;
	}

	// chat messages
	while (chat_msgs.Size())
	{
		std::string s;
		chat_msgs.Pop(s);
		//PanicAlert("message: %s", s.c_str());
		m_chat_text->AppendText(StrToWxStr(s).Append(wxT('\n')));
	}
}

void NetPlayDiag::OnChangeGame(wxCommandEvent&)
{
	wxString game_name;
	ChangeGameDiag* const cgd = new ChangeGameDiag(this, m_game_list, game_name);
	cgd->ShowModal();

	if (game_name.length())
	{
		m_selected_game = WxStrToStr(game_name);
		netplay_server->ChangeGame(m_selected_game);
		m_game_btn->SetLabel(game_name.Prepend(_(" Game : ")));
	}
}

void NetPlayDiag::OnConfigPads(wxCommandEvent&)
{
	int mapping[4];

	// get selected player id
	int pid = m_player_lbox->GetSelection();
	if (pid < 0)
		return;
	pid = m_playerids.at(pid);

	if (false == netplay_server->GetPadMapping(pid, mapping))
		return;

	PadMapDiag pmd(this, mapping);
	pmd.ShowModal();

	if (false == netplay_server->SetPadMapping(pid, mapping))
		PanicAlertT("Could not set pads. The player left or the game is currently running!\n"
				"(setting pads while the game is running is not yet supported)");
}

ChangeGameDiag::ChangeGameDiag(wxWindow* const parent, const CGameListCtrl* const game_list, wxString& game_name)
	: wxDialog(parent, wxID_ANY, _("Change Game"), wxDefaultPosition, wxDefaultSize)
	, m_game_name(game_name)
{
	m_game_lbox = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SORT);
	m_game_lbox->Bind(wxEVT_COMMAND_LISTBOX_DOUBLECLICKED, &ChangeGameDiag::OnPick, this);

	FillWithGameNames(m_game_lbox, *game_list);

	wxButton* const ok_btn = new wxButton(this, wxID_OK, _("Change"));
	ok_btn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &ChangeGameDiag::OnPick, this);

	wxBoxSizer* const szr = new wxBoxSizer(wxVERTICAL);
	szr->Add(m_game_lbox, 1, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 5);
	szr->Add(ok_btn, 0, wxALL | wxALIGN_RIGHT, 5);

	SetSizerAndFit(szr);
	SetFocus();
}

void ChangeGameDiag::OnPick(wxCommandEvent& event)
{
	// return the selected game name
	m_game_name = m_game_lbox->GetStringSelection();
	EndModal(wxID_OK);
}

PadMapDiag::PadMapDiag(wxWindow* const parent, int map[])
	: wxDialog(parent, wxID_ANY, _("Configure Pads"), wxDefaultPosition, wxDefaultSize)
	, m_mapping(map)
{
	wxBoxSizer* const h_szr = new wxBoxSizer(wxHORIZONTAL);

	h_szr->AddSpacer(20);

	// labels
	wxBoxSizer* const label_szr = new wxBoxSizer(wxVERTICAL);
	label_szr->Add(new wxStaticText(this, wxID_ANY, _("Local")), 0, wxALIGN_TOP);
	label_szr->AddStretchSpacer(1);
	label_szr->Add(new wxStaticText(this, wxID_ANY, _("In-Game")), 0, wxALIGN_BOTTOM);

	h_szr->Add(label_szr, 1, wxTOP | wxEXPAND, 20);

	// set up choices
	wxString pad_names[5];
	pad_names[0] = _("None");
	for (unsigned int i=1; i<5; ++i)
		pad_names[i] = wxString(_("Pad ")) + (wxChar)(wxT('0')+i);

	for (unsigned int i=0; i<4; ++i)
	{
		wxChoice* const pad_cbox = m_map_cbox[i]
			= new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 5, pad_names);
		pad_cbox->Select(m_mapping[i] + 1);

		pad_cbox->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &PadMapDiag::OnAdjust, this);

		wxBoxSizer* const v_szr = new wxBoxSizer(wxVERTICAL);
		v_szr->Add(new wxStaticText(this,wxID_ANY, pad_names[i + 1]), 1, wxALIGN_CENTER_HORIZONTAL);
		v_szr->Add(pad_cbox, 1);

		h_szr->Add(v_szr, 1, wxTOP | wxEXPAND, 20);
	}

	h_szr->AddSpacer(20);

	wxBoxSizer* const main_szr = new wxBoxSizer(wxVERTICAL);
	main_szr->Add(h_szr);
	main_szr->AddSpacer(5);
	main_szr->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT, 20);
	main_szr->AddSpacer(5);
	SetSizerAndFit(main_szr);
	SetFocus();
}

void PadMapDiag::OnAdjust(wxCommandEvent& event)
{
	(void)event;
	for (unsigned int i=0; i<4; ++i)
		m_mapping[i] = m_map_cbox[i]->GetSelection() - 1;
}

void NetPlay::StopGame()
{
	if (netplay_server != NULL)
		netplay_server->StopGame();

	// TODO: allow non-hosting clients to close the window
}
