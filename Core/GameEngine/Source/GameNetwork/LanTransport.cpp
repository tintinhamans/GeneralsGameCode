/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#if defined(RTS_NETWORK_ENET)

#include "GameNetwork/LANAPI.h"
#include "GameNetwork/LobbyOptions.h"


namespace
{

	const UnsignedInt BROADCAST_ADDRESS = 0xFFFFFFFF;

	void textToWire(const WideChar *src, Int capacity, uint32_t *dest, pb_size_t *count)
	{
		Int n = 0;
		while (n < capacity && src[n] != 0)
		{
			dest[n] = (uint32_t)(UnsignedShort)src[n];
			++n;
		}
		*count = (pb_size_t)n;
	}

	void textFromWire(WideChar *dest, Int capacity, const uint32_t *src, pb_size_t count)
	{
		Int n = 0;
		while (n < capacity && n < (Int)count)
		{
			dest[n] = (WideChar)src[n];
			++n;
		}
		dest[n] = 0;
	}

	void toWire(const LANMessage &msg, netproto_LanMessage &wire)
	{
		memset(&wire, 0, sizeof(wire));
		wire.type = (uint32_t)msg.messageType;
		textToWire(msg.name, g_lanPlayerNameLength, wire.name, &wire.name_count);
		strlcpy(wire.user_name, msg.userName, ARRAY_SIZE(wire.user_name));
		strlcpy(wire.host_name, msg.hostName, ARRAY_SIZE(wire.host_name));

		switch (msg.messageType)
		{
		case LANMessage::MSG_GAME_START_TIMER:
			wire.which_body = netproto_LanMessage_start_timer_tag;
			wire.start_timer.seconds = msg.StartTimer.seconds;
			break;
		case LANMessage::MSG_REQUEST_GAME_LEAVE:
			wire.which_body = netproto_LanMessage_game_to_leave_tag;
			textToWire(msg.GameToLeave.gameName, g_lanGameNameLength, wire.game_to_leave.game_name, &wire.game_to_leave.game_name_count);
			break;
		case LANMessage::MSG_GAME_ANNOUNCE:
			wire.which_body = netproto_LanMessage_game_info_tag;
			textToWire(msg.GameInfo.gameName, g_lanGameNameLength, wire.game_info.game_name, &wire.game_info.game_name_count);
			wire.game_info.in_progress = msg.GameInfo.inProgress ? true : false;
			if (LobbyOptions::pack(msg.GameInfo.options, &wire.game_info.packed_options))
				wire.game_info.has_packed_options = true;
			else
			{
				strlcpy(wire.game_info.options, msg.GameInfo.options, ARRAY_SIZE(wire.game_info.options));
				DEBUG_LOG(("LanTransport - game options sent as text: [%s]", msg.GameInfo.options));
			}
			wire.game_info.is_direct_connect = msg.GameInfo.isDirectConnect ? true : false;
			wire.game_info.enet_port = 0;
			break;
		case LANMessage::MSG_REQUEST_GAME_INFO:
			wire.which_body = netproto_LanMessage_player_info_tag;
			wire.player_info.ip = msg.PlayerInfo.ip;
			textToWire(msg.PlayerInfo.playerName, g_lanPlayerNameLength, wire.player_info.player_name, &wire.player_info.player_name_count);
			break;
		case LANMessage::MSG_REQUEST_JOIN:
			wire.which_body = netproto_LanMessage_game_to_join_tag;
			wire.game_to_join.game_ip = msg.GameToJoin.gameIP;
			wire.game_to_join.exe_crc = msg.GameToJoin.exeCRC;
			wire.game_to_join.ini_crc = msg.GameToJoin.iniCRC;
			strlcpy(wire.game_to_join.serial, msg.GameToJoin.serial, ARRAY_SIZE(wire.game_to_join.serial));
			break;
		case LANMessage::MSG_JOIN_ACCEPT:
			wire.which_body = netproto_LanMessage_game_joined_tag;
			textToWire(msg.GameJoined.gameName, g_lanGameNameLength, wire.game_joined.game_name, &wire.game_joined.game_name_count);
			wire.game_joined.game_ip = msg.GameJoined.gameIP;
			wire.game_joined.player_ip = msg.GameJoined.playerIP;
			wire.game_joined.slot_position = msg.GameJoined.slotPosition;
			break;
		case LANMessage::MSG_JOIN_DENY:
			wire.which_body = netproto_LanMessage_game_not_joined_tag;
			textToWire(msg.GameNotJoined.gameName, g_lanGameNameLength, wire.game_not_joined.game_name, &wire.game_not_joined.game_name_count);
			wire.game_not_joined.game_ip = msg.GameNotJoined.gameIP;
			wire.game_not_joined.player_ip = msg.GameNotJoined.playerIP;
			wire.game_not_joined.reason = (int32_t)msg.GameNotJoined.reason;
			break;
		case LANMessage::MSG_SET_ACCEPT:
			wire.which_body = netproto_LanMessage_accept_tag;
			textToWire(msg.Accept.gameName, g_lanGameNameLength, wire.accept.game_name, &wire.accept.game_name_count);
			wire.accept.is_accepted = msg.Accept.isAccepted ? true : false;
			break;
		case LANMessage::MSG_MAP_AVAILABILITY:
			wire.which_body = netproto_LanMessage_map_status_tag;
			textToWire(msg.MapStatus.gameName, g_lanGameNameLength, wire.map_status.game_name, &wire.map_status.game_name_count);
			wire.map_status.map_crc = msg.MapStatus.mapCRC;
			wire.map_status.has_map = msg.MapStatus.hasMap ? true : false;
			break;
		case LANMessage::MSG_CHAT:
			wire.which_body = netproto_LanMessage_chat_tag;
			textToWire(msg.Chat.gameName, g_lanGameNameLength, wire.chat.game_name, &wire.chat.game_name_count);
			wire.chat.chat_type = (int32_t)msg.Chat.chatType;
			textToWire(msg.Chat.message, g_lanMaxChatLength, wire.chat.message, &wire.chat.message_count);
			break;
		case LANMessage::MSG_GAME_OPTIONS:
			wire.which_body = netproto_LanMessage_game_options_tag;
			if (LobbyOptions::pack(msg.GameOptions.options, &wire.game_options.packed_options))
				wire.game_options.has_packed_options = true;
			else
			{
				strlcpy(wire.game_options.options, msg.GameOptions.options, ARRAY_SIZE(wire.game_options.options));
				DEBUG_LOG(("LanTransport - game options sent as text: [%s]", msg.GameOptions.options));
			}
			break;
		default:
			break;
		}
	}

	void fromWire(const netproto_LanMessage &wire, LANMessage &msg)
	{
		memset(&msg, 0, sizeof(msg));
		msg.messageType = (LANMessage::Type)wire.type;
		textFromWire(msg.name, g_lanPlayerNameLength, wire.name, wire.name_count);
		strlcpy(msg.userName, wire.user_name, ARRAY_SIZE(msg.userName));
		strlcpy(msg.hostName, wire.host_name, ARRAY_SIZE(msg.hostName));

		switch (wire.which_body)
		{
		case netproto_LanMessage_start_timer_tag:
			msg.StartTimer.seconds = wire.start_timer.seconds;
			break;
		case netproto_LanMessage_game_to_leave_tag:
			textFromWire(msg.GameToLeave.gameName, g_lanGameNameLength, wire.game_to_leave.game_name, wire.game_to_leave.game_name_count);
			break;
		case netproto_LanMessage_game_info_tag:
			textFromWire(msg.GameInfo.gameName, g_lanGameNameLength, wire.game_info.game_name, wire.game_info.game_name_count);
			msg.GameInfo.inProgress = wire.game_info.in_progress;
			if (!wire.game_info.has_packed_options || !LobbyOptions::unpack(wire.game_info.packed_options, msg.GameInfo.options, ARRAY_SIZE(msg.GameInfo.options)))
				strlcpy(msg.GameInfo.options, wire.game_info.options, ARRAY_SIZE(msg.GameInfo.options));
			msg.GameInfo.isDirectConnect = wire.game_info.is_direct_connect;
			break;
		case netproto_LanMessage_player_info_tag:
			msg.PlayerInfo.ip = wire.player_info.ip;
			textFromWire(msg.PlayerInfo.playerName, g_lanPlayerNameLength, wire.player_info.player_name, wire.player_info.player_name_count);
			break;
		case netproto_LanMessage_game_to_join_tag:
			msg.GameToJoin.gameIP = wire.game_to_join.game_ip;
			msg.GameToJoin.exeCRC = wire.game_to_join.exe_crc;
			msg.GameToJoin.iniCRC = wire.game_to_join.ini_crc;
			strlcpy(msg.GameToJoin.serial, wire.game_to_join.serial, ARRAY_SIZE(msg.GameToJoin.serial));
			break;
		case netproto_LanMessage_game_joined_tag:
			textFromWire(msg.GameJoined.gameName, g_lanGameNameLength, wire.game_joined.game_name, wire.game_joined.game_name_count);
			msg.GameJoined.gameIP = wire.game_joined.game_ip;
			msg.GameJoined.playerIP = wire.game_joined.player_ip;
			msg.GameJoined.slotPosition = wire.game_joined.slot_position;
			break;
		case netproto_LanMessage_game_not_joined_tag:
			textFromWire(msg.GameNotJoined.gameName, g_lanGameNameLength, wire.game_not_joined.game_name, wire.game_not_joined.game_name_count);
			msg.GameNotJoined.gameIP = wire.game_not_joined.game_ip;
			msg.GameNotJoined.playerIP = wire.game_not_joined.player_ip;
			msg.GameNotJoined.reason = (LANAPIInterface::ReturnType)wire.game_not_joined.reason;
			break;
		case netproto_LanMessage_accept_tag:
			textFromWire(msg.Accept.gameName, g_lanGameNameLength, wire.accept.game_name, wire.accept.game_name_count);
			msg.Accept.isAccepted = wire.accept.is_accepted;
			break;
		case netproto_LanMessage_map_status_tag:
			textFromWire(msg.MapStatus.gameName, g_lanGameNameLength, wire.map_status.game_name, wire.map_status.game_name_count);
			msg.MapStatus.mapCRC = wire.map_status.map_crc;
			msg.MapStatus.hasMap = wire.map_status.has_map;
			break;
		case netproto_LanMessage_chat_tag:
			textFromWire(msg.Chat.gameName, g_lanGameNameLength, wire.chat.game_name, wire.chat.game_name_count);
			msg.Chat.chatType = (LANAPIInterface::ChatType)wire.chat.chat_type;
			textFromWire(msg.Chat.message, g_lanMaxChatLength, wire.chat.message, wire.chat.message_count);
			break;
		case netproto_LanMessage_game_options_tag:
			if (!wire.game_options.has_packed_options || !LobbyOptions::unpack(wire.game_options.packed_options, msg.GameOptions.options, ARRAY_SIZE(msg.GameOptions.options)))
				strlcpy(msg.GameOptions.options, wire.game_options.options, ARRAY_SIZE(msg.GameOptions.options));
			break;
		default:
			break;
		}
	}

	// Lobby wide traffic has no member to connect to; the rest is addressed to a member.
	bool isDatagramType(Int type)
	{
		switch (type)
		{
		case LANMessage::MSG_REQUEST_LOCATIONS:
		case LANMessage::MSG_GAME_ANNOUNCE:
		case LANMessage::MSG_LOBBY_ANNOUNCE:
		case LANMessage::MSG_REQUEST_LOBBY_LEAVE:
		case LANMessage::MSG_REQUEST_GAME_INFO:
			return true;
		default:
			return false;
		}
	}

}

LanTransport::LanTransport()
{
	m_port = 0;
	for (size_t i = 0; i < ARRAY_SIZE(m_inBuffer); ++i)
	{
		m_inBuffer[i].length = 0;
	}
}

LanTransport::~LanTransport()
{
	reset();
}

Bool LanTransport::init( UnsignedInt ip, UnsignedShort port )
{
	// A socket bound to a loopback address reaches the other loopback addresses with the loopback broadcast only.
	const UnsignedInt broadcast = (ip >> 24) == 127 ? 0x7FFFFFFF : BROADCAST_ADDRESS;
	if (!m_link.open(ip, port, (unsigned short)(port + 1), broadcast))
	{
		DEBUG_LOG(("LanTransport::init - Failure to bind %d.%d.%d.%d:%d", PRINTF_IP_AS_4_INTS(ip), port));
		return FALSE;
	}

	m_port = port;
	return TRUE;
}

void LanTransport::reset()
{
	m_link.close();
}

Bool LanTransport::update()
{
	if (!m_link.isOpen())
		return FALSE;

	m_link.service();

	UnsignedInt addr;
	bool reliable;
	netproto_LanMessage wire;
	size_t slot = 0;
	while (slot < ARRAY_SIZE(m_inBuffer))
	{
		while (slot < ARRAY_SIZE(m_inBuffer) && m_inBuffer[slot].length > 0)
		{
			++slot;
		}
		if (slot == ARRAY_SIZE(m_inBuffer) || !m_link.pop(&addr, &reliable, &wire))
			break;

		LANMessage msg;
		fromWire(wire, msg);
		memcpy(m_inBuffer[slot].data, &msg, sizeof(msg));
		m_inBuffer[slot].length = sizeof(msg);
		m_inBuffer[slot].addr = addr;
		m_inBuffer[slot].port = m_port;
	}

	return TRUE;
}

Bool LanTransport::queueSend( UnsignedInt addr, UnsignedShort port, const UnsignedByte *buf, Int len )
{
	if (len != sizeof(LANMessage))
		return FALSE;

	const LANMessage &msg = *reinterpret_cast<const LANMessage *>(buf);
	netproto_LanMessage wire;
	toWire(msg, wire);
	if (wire.which_body == netproto_LanMessage_game_info_tag)
		wire.game_info.enet_port = (uint32_t)(m_port + 1);

	if (addr == BROADCAST_ADDRESS || addr == 0x7FFFFFFF)
		return m_link.sendBroadcast(wire) ? TRUE : FALSE;

	if (isDatagramType(msg.messageType))
		return m_link.sendUnicast(addr, wire) ? TRUE : FALSE;

	return m_link.sendReliable(addr, wire) ? TRUE : FALSE;
}

#endif
