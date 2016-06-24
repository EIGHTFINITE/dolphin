// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/ENetUtil.h"
#include "Common/MsgHandler.h"
#include "Common/Timer.h"
#include "Core/ConfigManager.h"
#include "Core/Movie.h"
#include "Core/NetPlayClient.h"
#include "Core/HW/EXI_DeviceIPL.h"
#include "Core/HW/SI.h"
#include "Core/HW/SI_DeviceGCController.h"
#include "Core/HW/Sram.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb.h"
#include "InputCommon/GCAdapter.h"

static std::mutex crit_netplay_client;
static NetPlayClient * netplay_client = nullptr;
static std::array<int, 4> s_wiimote_sources_cache;
NetSettings g_NetPlaySettings;

// called from ---GUI--- thread
NetPlayClient::~NetPlayClient()
{
	// not perfect
	if (m_is_running.load())
		StopGame();

	if (m_is_connected)
	{
		m_do_loop.store(false);
		m_thread.join();
	}

	if (m_server)
	{
		Disconnect();
	}

	if (g_MainNetHost.get() == m_client)
	{
		g_MainNetHost.release();
	}
	if (m_client)
	{
		enet_host_destroy(m_client);
		m_client = nullptr;
	}

	if (m_traversal_client)
	{
		ReleaseTraversalClient();
	}
}

// called from ---GUI--- thread
NetPlayClient::NetPlayClient(const std::string& address, const u16 port, NetPlayUI* dialog, const std::string& name, bool traversal, const std::string& centralServer, u16 centralPort)
	: m_dialog(dialog)
	, m_player_name(name)
{
	ClearBuffers();

	if (!traversal)
	{
		//Direct Connection
		m_client = enet_host_create(nullptr, 1, 3, 0, 0);

		if (m_client == nullptr)
		{
			PanicAlertT("Couldn't Create Client");
		}

		ENetAddress addr;
		enet_address_set_host(&addr, address.c_str());
		addr.port = port;

		m_server = enet_host_connect(m_client, &addr, 3, 0);

		if (m_server == nullptr)
		{
			PanicAlertT("Couldn't create peer.");
		}

		ENetEvent netEvent;
		int net = enet_host_service(m_client, &netEvent, 5000);
		if (net > 0 && netEvent.type == ENET_EVENT_TYPE_CONNECT)
		{
			if (Connect())
			{
				m_client->intercept = ENetUtil::InterceptCallback;
				m_thread = std::thread(&NetPlayClient::ThreadFunc, this);
			}
		}
		else
		{
			PanicAlertT("Failed to Connect!");
		}

	}
	else
	{
		if (address.size() > NETPLAY_CODE_SIZE)
		{
			PanicAlertT("Host code size is to large.\nPlease recheck that you have the correct code");
			return;
		}

		if (!EnsureTraversalClient(centralServer, centralPort))
			return;
		m_client = g_MainNetHost.get();

		m_traversal_client = g_TraversalClient.get();

		// If we were disconnected in the background, reconnect.
		if (m_traversal_client->m_State == TraversalClient::Failure)
			m_traversal_client->ReconnectToServer();
		m_traversal_client->m_Client = this;
		m_host_spec = address;
		m_connection_state = ConnectionState::WaitingForTraversalClientConnection;
		OnTraversalStateChanged();
		m_connecting = true;

		Common::Timer connect_timer;
		connect_timer.Start();

		while (m_connecting)
		{
			ENetEvent netEvent;
			if (m_traversal_client)
				m_traversal_client->HandleResends();

			while (enet_host_service(m_client, &netEvent, 4) > 0)
			{
				sf::Packet rpac;
				switch (netEvent.type)
				{
				case ENET_EVENT_TYPE_CONNECT:
					m_server = netEvent.peer;
					if (Connect())
					{
						m_connection_state = ConnectionState::Connected;
						m_thread = std::thread(&NetPlayClient::ThreadFunc, this);
					}
					return;
				default:
					break;
				}
			}
			if (connect_timer.GetTimeElapsed() > 5000)
				break;
		}
		PanicAlertT("Failed To Connect!");
	}
}

bool NetPlayClient::Connect()
{
	// send connect message
	sf::Packet spac;
	spac << scm_rev_git_str;
	spac << netplay_dolphin_ver;
	spac << m_player_name;
	Send(spac);
	enet_host_flush(m_client);
	sf::Packet rpac;
	// TODO: make this not hang
	ENetEvent netEvent;
	if (enet_host_service(m_client, &netEvent, 5000) > 0 && netEvent.type == ENET_EVENT_TYPE_RECEIVE)
	{
		rpac.append(netEvent.packet->data, netEvent.packet->dataLength);
		enet_packet_destroy(netEvent.packet);
	}
	else
	{
		return false;
	}

	MessageId error;
	rpac >> error;

	// got error message
	if (error)
	{
		switch (error)
		{
		case CON_ERR_SERVER_FULL:
			PanicAlertT("The server is full!");
			break;
		case CON_ERR_VERSION_MISMATCH:
			PanicAlertT("The server and client's NetPlay versions are incompatible!");
			break;
		case CON_ERR_GAME_RUNNING:
			PanicAlertT("The server responded: the game is currently running!");
			break;
		default:
			PanicAlertT("The server sent an unknown error message!");
			break;
		}

		Disconnect();
		return false;
	}
	else
	{
		rpac >> m_pid;

		Player player;
		player.name = m_player_name;
		player.pid = m_pid;
		player.revision = netplay_dolphin_ver;

		// add self to player list
		m_players[m_pid] = player;
		m_local_player = &m_players[m_pid];

		m_dialog->Update();

		m_is_connected = true;

		return true;
	}
}

// called from ---NETPLAY--- thread
unsigned int NetPlayClient::OnData(sf::Packet& packet)
{
	MessageId mid;
	packet >> mid;

	switch (mid)
	{
	case NP_MSG_PLAYER_JOIN:
	{
		Player player;
		packet >> player.pid;
		packet >> player.name;
		packet >> player.revision;

		{
			std::lock_guard<std::recursive_mutex> lkp(m_crit.players);
			m_players[player.pid] = player;
		}

		m_dialog->Update();
	}
	break;

	case NP_MSG_PLAYER_LEAVE:
	{
		PlayerId pid;
		packet >> pid;

		{
			std::lock_guard<std::recursive_mutex> lkp(m_crit.players);
			m_players.erase(m_players.find(pid));
		}

		m_dialog->Update();
	}
	break;

	case NP_MSG_CHAT_MESSAGE:
	{
		PlayerId pid;
		packet >> pid;
		std::string msg;
		packet >> msg;

		// don't need lock to read in this thread
		const Player& player = m_players[pid];

		// add to gui
		std::ostringstream ss;
		ss << player.name << '[' << (char)(pid + '0') << "]: " << msg;

		m_dialog->AppendChat(ss.str());
	}
	break;

	case NP_MSG_PAD_MAPPING:
	{
		for (PadMapping& mapping : m_pad_map)
		{
			packet >> mapping;
		}

		UpdateDevices();

		m_dialog->Update();
	}
	break;

	case NP_MSG_WIIMOTE_MAPPING:
	{
		for (PadMapping& mapping : m_wiimote_map)
		{
			packet >> mapping;
		}

		m_dialog->Update();
	}
	break;

	case NP_MSG_PAD_DATA:
	{
		PadMapping map = 0;
		GCPadStatus pad;
		packet >> map
		       >> pad.button
		       >> pad.analogA
		       >> pad.analogB
		       >> pad.stickX
		       >> pad.stickY
		       >> pad.substickX
		       >> pad.substickY
		       >> pad.triggerLeft
		       >> pad.triggerRight;

		// Trusting server for good map value (>=0 && <4)
		// add to pad buffer
		m_pad_buffer.at(map).Push(pad);
	}
	break;

	case NP_MSG_WIIMOTE_DATA:
	{
		PadMapping map = 0;
		NetWiimote nw;
		u8 size;
		packet >> map >> size;

		nw.resize(size);

		for (unsigned int i = 0; i < size; ++i)
			packet >> nw[i];

		// Trusting server for good map value (>=0 && <4)
		// add to Wiimote buffer
		m_wiimote_buffer.at(map).Push(nw);
	}
	break;


	case NP_MSG_PAD_BUFFER:
	{
		u32 size = 0;
		packet >> size;

		m_target_buffer_size = size;
	}
	break;

	case NP_MSG_CHANGE_GAME:
	{
		{
			std::lock_guard<std::recursive_mutex> lkg(m_crit.game);
			packet >> m_selected_game;
		}

		// update gui
		m_dialog->OnMsgChangeGame(m_selected_game);
	}
	break;

	case NP_MSG_START_GAME:
	{
		{
			std::lock_guard<std::recursive_mutex> lkg(m_crit.game);
			packet >> m_current_game;
			packet >> g_NetPlaySettings.m_CPUthread;
			packet >> g_NetPlaySettings.m_CPUcore;
			packet >> g_NetPlaySettings.m_SelectedLanguage;
			packet >> g_NetPlaySettings.m_OverrideGCLanguage;
			packet >> g_NetPlaySettings.m_ProgressiveScan;
			packet >> g_NetPlaySettings.m_PAL60;
			packet >> g_NetPlaySettings.m_DSPEnableJIT;
			packet >> g_NetPlaySettings.m_DSPHLE;
			packet >> g_NetPlaySettings.m_WriteToMemcard;
			packet >> g_NetPlaySettings.m_OCEnable;
			packet >> g_NetPlaySettings.m_OCFactor;

			int tmp;
			packet >> tmp;
			g_NetPlaySettings.m_EXIDevice[0] = (TEXIDevices)tmp;
			packet >> tmp;
			g_NetPlaySettings.m_EXIDevice[1] = (TEXIDevices)tmp;

			u32 time_low, time_high;
			packet >> time_low;
			packet >> time_high;
			g_netplay_initial_gctime = time_low | ((u64)time_high << 32);
		}

		m_dialog->OnMsgStartGame();
	}
	break;

	case NP_MSG_STOP_GAME:
	{
		m_dialog->OnMsgStopGame();
	}
	break;

	case NP_MSG_DISABLE_GAME:
	{
		PanicAlertT("Other client disconnected while game is running!! NetPlay is disabled. You must manually stop the game.");
		m_is_running.store(false);
		NetPlay_Disable();
	}
	break;

	case NP_MSG_PING:
	{
		u32 ping_key = 0;
		packet >> ping_key;

		sf::Packet spac;
		spac << (MessageId)NP_MSG_PONG;
		spac << ping_key;

		Send(spac);
	}
	break;

	case NP_MSG_PLAYER_PING_DATA:
	{
		PlayerId pid;
		packet >> pid;

		{
			std::lock_guard<std::recursive_mutex> lkp(m_crit.players);
			Player& player = m_players[pid];
			packet >> player.ping;
		}

		m_dialog->Update();
	}
	break;

	case NP_MSG_DESYNC_DETECTED:
	{
		int pid_to_blame;
		u32 frame;
		packet >> pid_to_blame;
		packet >> frame;
		const char* blame_str = "";
		const char* blame_name = "";
		std::lock_guard<std::recursive_mutex> lkp(m_crit.players);
		if (pid_to_blame != -1)
		{
			auto it = m_players.find(pid_to_blame);
			blame_str = " from player ";
			blame_name = it != m_players.end() ? it->second.name.c_str() : "??";
		}

		m_dialog->AppendChat(StringFromFormat("/!\\ Possible desync detected%s%s on frame %u", blame_str, blame_name, frame));
	}
	break;

	case NP_MSG_SYNC_GC_SRAM:
	{
		u8 sram[sizeof(g_SRAM.p_SRAM)];
		for (size_t i = 0; i < sizeof(g_SRAM.p_SRAM); ++i)
		{
			packet >> sram[i];
		}

		{
			std::lock_guard<std::recursive_mutex> lkg(m_crit.game);
			memcpy(g_SRAM.p_SRAM, sram, sizeof(g_SRAM.p_SRAM));
			g_SRAM_netplay_initialized = true;
		}
	}
	break;

	default:
		PanicAlertT("Unknown message received with id : %d", mid);
		break;

	}

	return 0;
}

void NetPlayClient::Send(sf::Packet& packet)
{
	ENetPacket* epac = enet_packet_create(packet.getData(), packet.getDataSize(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_server, 0, epac);
}

void NetPlayClient::Disconnect()
{
	ENetEvent netEvent;
	m_connecting = false;
	m_connection_state = ConnectionState::Failure;
	if (m_server)
		enet_peer_disconnect(m_server, 0);
	else
		return;

	while (enet_host_service(m_client, &netEvent, 3000) > 0)
	{
		switch (netEvent.type)
		{
		case ENET_EVENT_TYPE_RECEIVE:
			enet_packet_destroy(netEvent.packet);
			break;
		case ENET_EVENT_TYPE_DISCONNECT:
			m_server = nullptr;
			return;
		default:
			break;
		}
	}
	//didn't disconnect gracefully force disconnect
	enet_peer_reset(m_server);
	m_server = nullptr;
}

void NetPlayClient::SendAsync(std::unique_ptr<sf::Packet> packet)
{
	{
		std::lock_guard<std::recursive_mutex> lkq(m_crit.async_queue_write);
		m_async_queue.Push(std::move(packet));
	}
	ENetUtil::WakeupThread(m_client);
}

// called from ---NETPLAY--- thread
void NetPlayClient::ThreadFunc()
{
	while (m_do_loop.load())
	{
		ENetEvent netEvent;
		int net;
		if (m_traversal_client)
			m_traversal_client->HandleResends();
		net = enet_host_service(m_client, &netEvent, 250);
		while (!m_async_queue.Empty())
		{
			Send(*(m_async_queue.Front().get()));
			m_async_queue.Pop();
		}
		if (net > 0)
		{
			sf::Packet rpac;
			switch (netEvent.type)
			{
			case ENET_EVENT_TYPE_RECEIVE:
				rpac.append(netEvent.packet->data, netEvent.packet->dataLength);
				OnData(rpac);

				enet_packet_destroy(netEvent.packet);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				m_is_running.store(false);
				NetPlay_Disable();
				m_dialog->AppendChat("< LOST CONNECTION TO SERVER >");
				PanicAlertT("Lost connection to server!");
				m_do_loop.store(false);

				netEvent.peer->data = nullptr;
				break;
			default:
				break;
			}
		}
	}

	Disconnect();
	return;
}

// called from ---GUI--- thread
void NetPlayClient::GetPlayerList(std::string& list, std::vector<int>& pid_list)
{
	std::lock_guard<std::recursive_mutex> lkp(m_crit.players);

	std::ostringstream ss;

	const auto enumerate_player_controller_mappings = [&ss](const PadMappingArray& mappings, const Player& player) {
		for (size_t i = 0; i < mappings.size(); i++)
		{
			if (mappings[i] == player.pid)
				ss << i + 1;
			else
				ss << '-';
		}
	};

	for (const auto& entry : m_players)
	{
		const Player& player = entry.second;
		ss << player.name << "[" << static_cast<int>(player.pid) << "] : " << player.revision << " | ";

		enumerate_player_controller_mappings(m_pad_map, player);
		enumerate_player_controller_mappings(m_wiimote_map, player);

		ss << " |\nPing: " << player.ping << "ms\n\n";
		pid_list.push_back(player.pid);
	}

	list = ss.str();
}

// called from ---GUI--- thread
std::vector<const Player*> NetPlayClient::GetPlayers()
{
	std::lock_guard<std::recursive_mutex> lkp(m_crit.players);
	std::vector<const Player*> players;

	for (const auto& pair : m_players)
		players.push_back(&pair.second);

	return players;
}

// called from ---GUI--- thread
void NetPlayClient::SendChatMessage(const std::string& msg)
{
	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_CHAT_MESSAGE);
	*spac << msg;

	SendAsync(std::move(spac));
}

// called from ---CPU--- thread
void NetPlayClient::SendPadState(const PadMapping in_game_pad, const GCPadStatus& pad)
{
	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_PAD_DATA);
	*spac << in_game_pad;
	*spac << pad.button
	      << pad.analogA
	      << pad.analogB
	      << pad.stickX
	      << pad.stickY
	      << pad.substickX
	      << pad.substickY
	      << pad.triggerLeft
	      << pad.triggerRight;

	SendAsync(std::move(spac));
}

// called from ---CPU--- thread
void NetPlayClient::SendWiimoteState(const PadMapping in_game_pad, const NetWiimote& nw)
{
	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_WIIMOTE_DATA);
	*spac << in_game_pad;
	*spac << static_cast<u8>(nw.size());
	for (auto it : nw)
	{
		*spac << it;
	}

	SendAsync(std::move(spac));
}

// called from ---GUI--- thread
void NetPlayClient::SendStartGamePacket()
{
	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_START_GAME);
	*spac << m_current_game;

	SendAsync(std::move(spac));
}

// called from ---GUI--- thread
void NetPlayClient::SendStopGamePacket()
{
	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_STOP_GAME);

	SendAsync(std::move(spac));
}

// called from ---GUI--- thread
bool NetPlayClient::StartGame(const std::string &path)
{
	std::lock_guard<std::recursive_mutex> lkg(m_crit.game);
	SendStartGamePacket();

	if (m_is_running.load())
	{
		PanicAlertT("Game is already running!");
		return false;
	}

	m_dialog->AppendChat(" -- STARTING GAME -- ");

	m_timebase_frame = 0;

	m_is_running.store(true);
	NetPlay_Enable(this);

	ClearBuffers();

	if (m_dialog->IsRecording())
	{

		if (Movie::IsReadOnly())
			Movie::SetReadOnly(false);

		u8 controllers_mask = 0;
		for (unsigned int i = 0; i < 4; ++i)
		{
			if (m_pad_map[i] > 0)
				controllers_mask |= (1 << i);
			if (m_wiimote_map[i] > 0)
				controllers_mask |= (1 << (i + 4));
		}
		Movie::BeginRecordingInput(controllers_mask);
	}

	// boot game

	m_dialog->BootGame(path);

	// Disable wiimotes on game start
	// TODO: remove this when re-implementing wiimote netplay
	if (SConfig::GetInstance().bWii)
	{
		for (unsigned int i = 0; i < 4; ++i)
		{
			s_wiimote_sources_cache[i] = g_wiimote_sources[i];
			WiimoteReal::ChangeWiimoteSource(i, WIIMOTE_SRC_NONE);
		}
	}

	UpdateDevices();

	return true;
}

// called from ---GUI--- thread
bool NetPlayClient::ChangeGame(const std::string&)
{
	return true;
}

// called from ---NETPLAY--- thread
void NetPlayClient::UpdateDevices()
{
	u8 local_pad = 0;
	u8 pad = 0;

	for (auto player_id : m_pad_map)
	{
		// Use local controller types for local controllers if they are compatible
		// Only GCController-like controllers are supported, GBA and similar
		// exotic devices are not supported on netplay.
		if (player_id == m_local_player->pid)
		{
			if (SIDevice_IsGCController(SConfig::GetInstance().m_SIDevice[local_pad]))
			{
				SerialInterface::AddDevice(SConfig::GetInstance().m_SIDevice[local_pad], pad);
			}
			else
			{
				SerialInterface::AddDevice(SIDEVICE_GC_CONTROLLER, pad);
			}
			local_pad++;
		}
		else if (player_id > 0)
		{
			SerialInterface::AddDevice(SIDEVICE_GC_CONTROLLER, pad);
		}
		else
		{
			SerialInterface::AddDevice(SIDEVICE_NONE, pad);
		}
		pad++;
	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::ClearBuffers()
{
	// clear pad buffers, Clear method isn't thread safe
	for (unsigned int i = 0; i<4; ++i)
	{
		while (m_pad_buffer[i].Size())
			m_pad_buffer[i].Pop();

		while (m_wiimote_buffer[i].Size())
			m_wiimote_buffer[i].Pop();
	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::OnTraversalStateChanged()
{
	if (m_connection_state == ConnectionState::WaitingForTraversalClientConnection &&
		m_traversal_client->m_State == TraversalClient::Connected)
	{
		m_connection_state = ConnectionState::WaitingForTraversalClientConnectReady;
		m_traversal_client->ConnectToClient(m_host_spec);
	}
	else if (m_connection_state != ConnectionState::Failure &&
		m_traversal_client->m_State == TraversalClient::Failure)
	{
		Disconnect();
	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::OnConnectReady(ENetAddress addr)
{
	if (m_connection_state == ConnectionState::WaitingForTraversalClientConnectReady)
	{
		m_connection_state = ConnectionState::Connecting;
		enet_host_connect(m_client, &addr, 0, 0);
	}
}

// called from ---NETPLAY--- thread
void NetPlayClient::OnConnectFailed(u8 reason)
{
	m_connecting = false;
	m_connection_state = ConnectionState::Failure;
	switch (reason)
	{
	case TraversalConnectFailedClientDidntRespond:
		PanicAlertT("Traversal server timed out connecting to the host");
		break;
	case TraversalConnectFailedClientFailure:
		PanicAlertT("Server rejected traversal attempt");
		break;
	case TraversalConnectFailedNoSuchClient:
		PanicAlertT("Invalid host");
		break;
	default:
		PanicAlertT("Unknown error %x", reason);
		break;
	}
}

// called from ---CPU--- thread
bool NetPlayClient::GetNetPads(const u8 pad_nb, GCPadStatus* pad_status)
{
	// The interface for this is extremely silly.
	//
	// Imagine a physical device that links three GameCubes together
	// and emulates NetPlay that way. Which GameCube controls which
	// in-game controllers can be configured on the device (m_pad_map)
	// but which sockets on each individual GameCube should be used
	// to control which players? The solution that Dolphin uses is
	// that we hardcode the knowledge that they go in order, so if
	// you have a 3P game with three GameCubes, then every single
	// controller should be plugged into slot 1.
	//
	// If you have a 4P game, then one of the GameCubes will have
	// a controller plugged into slot 1, and another in slot 2.
	//
	// The slot number is the "local" pad number, and what player
	// it actually means is the "in-game" pad number.

	// When the 1st in-game pad is polled, we assume the others will
	// will be polled as well. To reduce latency, we poll all local
	// controllers at once and then send the status to the other
	// clients.
	if (IsFirstInGamePad(pad_nb))
	{
		const u8 num_local_pads = NumLocalPads();
		for (u8 local_pad = 0; local_pad < num_local_pads; local_pad++)
		{
			switch (SConfig::GetInstance().m_SIDevice[local_pad])
			{
			case SIDEVICE_WIIU_ADAPTER:
				GCAdapter::Input(local_pad, pad_status);
				break;
			case SIDEVICE_GC_CONTROLLER:
			default:
				Pad::GetStatus(local_pad, pad_status);
				break;
			}

			u8 ingame_pad = LocalPadToInGamePad(local_pad);

			// adjust the buffer either up or down
			// inserting multiple padstates or dropping states
			while (m_pad_buffer[ingame_pad].Size() <= m_target_buffer_size)
			{
				// add to buffer
				m_pad_buffer[ingame_pad].Push(*pad_status);

				// send
				SendPadState(ingame_pad, *pad_status);
			}
		}
	}

	// Now, we either use the data pushed earlier, or wait for the
	// other clients to send it to us
	while (!m_pad_buffer[pad_nb].Pop(*pad_status))
	{
		if (!m_is_running.load())
			return false;

		// TODO: use a condition instead of sleeping
		Common::SleepCurrentThread(1);
	}

	if (Movie::IsRecordingInput())
	{
		Movie::RecordInput(pad_status, pad_nb);
		Movie::InputUpdate();
	}
	else
	{
		Movie::CheckPadStatus(pad_status, pad_nb);
	}

	return true;
}


// called from ---CPU--- thread
bool NetPlayClient::WiimoteUpdate(int _number, u8* data, const u8 size)
{
	NetWiimote nw;
	static u8 previousSize[4] = { 4, 4, 4, 4 };
	{
		std::lock_guard<std::recursive_mutex> lkp(m_crit.players);

		// in game mapping for this local Wiimote
		unsigned int in_game_num = LocalWiimoteToInGameWiimote(_number);
		// does this local Wiimote map in game?
		if (in_game_num < 4)
		{
			if (previousSize[in_game_num] == size)
			{
				nw.assign(data, data + size);
				do
				{
					// add to buffer
					m_wiimote_buffer[in_game_num].Push(nw);

					SendWiimoteState(in_game_num, nw);
				} while (m_wiimote_buffer[in_game_num].Size() <= m_target_buffer_size * 200 / 120); // TODO: add a seperate setting for wiimote buffer?
			}
			else
			{
				while (m_wiimote_buffer[in_game_num].Size() > 0)
				{
					// Reporting mode changed, so previous buffer is no good.
					m_wiimote_buffer[in_game_num].Pop();
				}
				nw.resize(size, 0);

				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				m_wiimote_buffer[in_game_num].Push(nw);
				previousSize[in_game_num] = size;
			}
		}

	} // unlock players

	while (previousSize[_number] == size && !m_wiimote_buffer[_number].Pop(nw))
	{
		// wait for receiving thread to push some data
		Common::SleepCurrentThread(1);
		if (!m_is_running.load())
			return false;
	}

	// Use a blank input, since we may not have any valid input.
	if (previousSize[_number] != size)
	{
		nw.resize(size, 0);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
		m_wiimote_buffer[_number].Push(nw);
	}

	// We should have used a blank input last time, so now we just need to pop through the old buffer, until we reach a good input
	if (nw.size() != size)
	{
		u8 tries = 0;
		// Clear the buffer and wait for new input, since we probably just changed reporting mode.
		while (nw.size() != size)
		{
			while (!m_wiimote_buffer[_number].Pop(nw))
			{
				Common::SleepCurrentThread(1);
				if (!m_is_running.load())
					return false;
			}
			++tries;
			if (tries > m_target_buffer_size * 200 / 120)
				break;
		}

		// If it still mismatches, it surely desynced
		if (size != nw.size())
		{
			PanicAlertT("Netplay has desynced. There is no way to recover from this.");
			return false;
		}
	}

	previousSize[_number] = size;
	memcpy(data, nw.data(), size);
	return true;
}

// called from ---GUI--- thread and ---NETPLAY--- thread (client side)
bool NetPlayClient::StopGame()
{
	if (!m_is_running.load())
	{
		PanicAlertT("Game isn't running!");
		return false;
	}

	m_dialog->AppendChat(" -- STOPPING GAME -- ");

	m_is_running.store(false);
	NetPlay_Disable();

	// stop game
	m_dialog->StopGame();

	// Restore wiimote settings on game stop
	// TODO: remove this when re-implementing wiimote netplay
	if (SConfig::GetInstance().bWii)
	{
		for (unsigned int i = 0; i < 4; ++i)
		{
			g_wiimote_sources[i] = s_wiimote_sources_cache[i];
			WiimoteReal::ChangeWiimoteSource(i, s_wiimote_sources_cache[i]);
		}
	}

	return true;
}

// called from ---GUI--- thread
void NetPlayClient::Stop()
{
	if (!m_is_running.load())
		return;

	// Tell the server to stop if we have a pad mapped in game.
	if (LocalPlayerHasControllerMapped())
		SendStopGamePacket();
}

// called from ---GUI--- thread
bool NetPlayClient::LocalPlayerHasControllerMapped() const
{
	const auto mapping_matches_player_id = [this](const PadMapping& mapping) {
		return mapping == m_local_player->pid;
	};

	return std::any_of(m_pad_map.begin(),     m_pad_map.end(),     mapping_matches_player_id) ||
	       std::any_of(m_wiimote_map.begin(), m_wiimote_map.end(), mapping_matches_player_id);
}

bool NetPlayClient::IsFirstInGamePad(u8 ingame_pad) const
{
	return std::none_of(m_pad_map.begin(), m_pad_map.begin() + ingame_pad, [](auto mapping) {
		return mapping > 0;
	});
}

u8 NetPlayClient::NumLocalPads() const
{
	return static_cast<u8>(std::count_if(m_pad_map.begin(), m_pad_map.end(), [this](auto mapping) {
		return mapping == m_local_player->pid;
	}));
}

u8 NetPlayClient::InGamePadToLocalPad(u8 ingame_pad)
{
	// not our pad
	if (m_pad_map[ingame_pad] != m_local_player->pid)
		return 4;

	int local_pad = 0;
	int pad = 0;

	for (; pad < ingame_pad; pad++)
	{
		if (m_pad_map[pad] == m_local_player->pid)
			local_pad++;
	}

	return local_pad;
}

u8 NetPlayClient::LocalPadToInGamePad(u8 local_pad)
{
	// Figure out which in-game pad maps to which local pad.
	// The logic we have here is that the local slots always
	// go in order.
	int local_pad_count = -1;
	int ingame_pad = 0;
	for (; ingame_pad < 4; ingame_pad++)
	{
		if (m_pad_map[ingame_pad] == m_local_player->pid)
			local_pad_count++;

		if (local_pad_count == local_pad)
			break;
	}

	return ingame_pad;
}

u8 NetPlayClient::LocalWiimoteToInGameWiimote(u8 local_pad)
{
	// Figure out which in-game pad maps to which local pad.
	// The logic we have here is that the local slots always
	// go in order.
	int local_pad_count = -1;
	int ingame_pad = 0;
	for (; ingame_pad < 4; ingame_pad++)
	{
		if (m_wiimote_map[ingame_pad] == m_local_player->pid)
			local_pad_count++;

		if (local_pad_count == local_pad)
			break;
	}

	return ingame_pad;
}

void NetPlayClient::SendTimeBase()
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	u64 timebase = SystemTimers::GetFakeTimeBase();

	auto spac = std::make_unique<sf::Packet>();
	*spac << static_cast<MessageId>(NP_MSG_TIMEBASE);
	*spac << static_cast<u32>(timebase);
	*spac << static_cast<u32>(timebase << 32);
	*spac << netplay_client->m_timebase_frame++;

	netplay_client->SendAsync(std::move(spac));
}

// stuff hacked into dolphin

// called from ---CPU--- thread
// Actual Core function which is called on every frame
bool CSIDevice_GCController::NetPlay_GetInput(u8 numPAD, GCPadStatus* PadStatus)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->GetNetPads(numPAD, PadStatus);
	else
		return false;
}

bool WiimoteEmu::Wiimote::NetPlay_GetWiimoteData(int wiimote, u8* data, u8 size)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->WiimoteUpdate(wiimote, data, size);
	else
		return false;
}

// called from ---CPU--- thread
// so all players' games get the same time
u64 CEXIIPL::NetPlay_GetGCTime()
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return g_netplay_initial_gctime;
	else
		return 0;
}

// called from ---CPU--- thread
// return the local pad num that should rumble given a ingame pad num
u8 CSIDevice_GCController::NetPlay_InGamePadToLocalPad(u8 numPAD)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);

	if (netplay_client)
		return netplay_client->InGamePadToLocalPad(numPAD);
	else
		return numPAD;
}

bool NetPlay::IsNetPlayRunning()
{
	return netplay_client != nullptr;
}

void NetPlay_Enable(NetPlayClient* const np)
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);
	netplay_client = np;
}

void NetPlay_Disable()
{
	std::lock_guard<std::mutex> lk(crit_netplay_client);
	netplay_client = nullptr;
}
