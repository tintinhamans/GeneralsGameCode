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

#include "GameNetwork/NetProtoSerializer.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetProto/netproto.pb.h"

#include <pb_encode.h>
#include <pb_decode.h>

namespace NetProto
{

namespace
{

	UnsignedInt s_session = 0;

	struct BytesSource
	{
		const UnsignedByte *data;
		UnsignedInt size;
	};

	struct CommandSource
	{
		NetCommandRef *const *refs;
		Int count;
	};

	struct DecodeState
	{
		DecodeState() : payload(nullptr), payloadSize(0) {}
		~DecodeState()
		{
			delete[] payload;
			for (size_t i = 0; i < bundled.size(); ++i)
				bundled[i]->detach();
		}

		std::vector<netproto_Arg> args;
		std::vector<NetGameCommandMsg *> bundled;
		UnsignedByte *payload;
		UnsignedInt payloadSize;
	};

	struct EnvelopeState
	{
		NetCommandList *list;
	};

	UnsignedInt varintSize(UnsignedInt value)
	{
		UnsignedInt size = 1;
		while (value >= 0x80)
		{
			value >>= 7;
			++size;
		}
		return size;
	}


	bool encodeBytes(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
	{
		const BytesSource *source = static_cast<const BytesSource *>(*arg);
		if (source->size == 0)
			return true;

		return pb_encode_tag_for_field(stream, field) && pb_encode_string(stream, source->data, source->size);
	}

	bool encodeArgs(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
	{
		const NetGameCommandMsg *msg = static_cast<const NetGameCommandMsg *>(*arg);
		const Int count = msg->getArgumentCount();

		for (Int i = 0; i < count; ++i)
		{
			const GameMessageArgument *source = msg->getArgument(i);
			netproto_Arg arg;
			memset(&arg, 0, sizeof(arg));

			switch (source->m_type)
			{
			case ARGUMENTDATATYPE_INTEGER:
				arg.which_value = netproto_Arg_integer_tag;
				arg.integer = source->m_data.integer;
				break;
			case ARGUMENTDATATYPE_REAL:
				arg.which_value = netproto_Arg_real_tag;
				arg.real = source->m_data.real;
				break;
			case ARGUMENTDATATYPE_BOOLEAN:
				arg.which_value = netproto_Arg_boolean_tag;
				arg.boolean = source->m_data.boolean ? true : false;
				break;
			case ARGUMENTDATATYPE_OBJECTID:
				arg.which_value = netproto_Arg_object_id_tag;
				arg.object_id = (uint32_t)source->m_data.objectID;
				break;
			case ARGUMENTDATATYPE_DRAWABLEID:
				arg.which_value = netproto_Arg_drawable_id_tag;
				arg.drawable_id = (uint32_t)source->m_data.drawableID;
				break;
			case ARGUMENTDATATYPE_TEAMID:
				arg.which_value = netproto_Arg_team_id_tag;
				arg.team_id = source->m_data.teamID;
				break;
			case ARGUMENTDATATYPE_LOCATION:
				arg.which_value = netproto_Arg_location_tag;
				arg.location.x = source->m_data.location.x;
				arg.location.y = source->m_data.location.y;
				arg.location.z = source->m_data.location.z;
				break;
			case ARGUMENTDATATYPE_PIXEL:
				arg.which_value = netproto_Arg_pixel_tag;
				arg.pixel.x = source->m_data.pixel.x;
				arg.pixel.y = source->m_data.pixel.y;
				break;
			case ARGUMENTDATATYPE_PIXELREGION:
				arg.which_value = netproto_Arg_pixel_region_tag;
				arg.pixel_region.has_lo = true;
				arg.pixel_region.lo.x = source->m_data.pixelRegion.lo.x;
				arg.pixel_region.lo.y = source->m_data.pixelRegion.lo.y;
				arg.pixel_region.has_hi = true;
				arg.pixel_region.hi.x = source->m_data.pixelRegion.hi.x;
				arg.pixel_region.hi.y = source->m_data.pixelRegion.hi.y;
				break;
			case ARGUMENTDATATYPE_TIMESTAMP:
				arg.which_value = netproto_Arg_timestamp_tag;
				arg.timestamp = source->m_data.timestamp;
				break;
			case ARGUMENTDATATYPE_WIDECHAR:
				arg.which_value = netproto_Arg_wide_char_tag;
				arg.wide_char = (uint32_t)(UnsignedShort)source->m_data.wChar;
				break;
			default:
				return false;
			}

			if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, netproto_Arg_fields, &arg))
				return false;
		}

		return true;
	}

	bool encodeBundled(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
	{
		const NetFrameCommandMsg *frame = static_cast<const NetFrameCommandMsg *>(*arg);

		for (Int i = 0; i < frame->getBundledCount(); ++i)
		{
			const NetGameCommandMsg *game = frame->getBundled(i);
			netproto_BundledGameCommand bundled;
			memset(&bundled, 0, sizeof(bundled));
			bundled.message_type = (netproto_GameMessageType)(Int)game->getGameMessageType();
			bundled.args.funcs.encode = encodeArgs;
			bundled.args.arg = const_cast<NetGameCommandMsg *>(game);

			if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, netproto_BundledGameCommand_fields, &bundled))
				return false;
		}

		return true;
	}

	// Fills the command from the reference. The callbacks in the command borrow the message and bytes.
	bool fillCommand(netproto_Command &c, const NetCommandRef &ref, BytesSource &bytes)
	{
		const NetCommandMsg *msg = ref.getCommand();

		memset(&c, 0, sizeof(c));
		bytes.data = nullptr;
		bytes.size = 0;

		c.type = (uint32_t)msg->getNetCommandType();
		c.frame = msg->getExecutionFrame();
		c.player = msg->getPlayerID();
		c.id = msg->getID();

		switch (msg->getNetCommandType())
		{
		case NETCOMMANDTYPE_FRAMEINFO:
		{
			// The id only keeps frame infos apart in the send queue; the receiver derives its own.
			c.id = 0;
			c.bundled.funcs.encode = encodeBundled;
			c.bundled.arg = const_cast<NetCommandMsg *>(msg);
			if (static_cast<const NetFrameCommandMsg *>(msg)->hasMetrics())
			{
				c.which_body = netproto_Command_frame_info_tag;
				c.frame_info.latency_ms = static_cast<const NetFrameCommandMsg *>(msg)->getLatencyMs();
				c.frame_info.fps = static_cast<const NetFrameCommandMsg *>(msg)->getFps();
			}
			break;
		}
		case NETCOMMANDTYPE_PLAYERLEAVE:
			c.which_body = netproto_Command_player_leave_tag;
			c.player_leave.leaving_player = static_cast<const NetPlayerLeaveCommandMsg *>(msg)->getLeavingPlayerID();
			break;
		case NETCOMMANDTYPE_RUNAHEADMETRICS:
		{
			const NetRunAheadMetricsCommandMsg *m = static_cast<const NetRunAheadMetricsCommandMsg *>(msg);
			c.which_body = netproto_Command_run_ahead_metrics_tag;
			c.run_ahead_metrics.average_latency = m->getAverageLatency();
			c.run_ahead_metrics.average_fps = m->getAverageFps();
			break;
		}
		case NETCOMMANDTYPE_RUNAHEAD:
		{
			const NetRunAheadCommandMsg *m = static_cast<const NetRunAheadCommandMsg *>(msg);
			c.which_body = netproto_Command_run_ahead_tag;
			c.run_ahead.run_ahead = m->getRunAhead();
			c.run_ahead.frame_rate = m->getFrameRate();
			break;
		}
		case NETCOMMANDTYPE_DESTROYPLAYER:
			c.which_body = netproto_Command_destroy_player_tag;
			c.destroy_player.player_index = static_cast<const NetDestroyPlayerCommandMsg *>(msg)->getPlayerIndex();
			break;
		case NETCOMMANDTYPE_DISCONNECTPLAYER:
		{
			const NetDisconnectPlayerCommandMsg *m = static_cast<const NetDisconnectPlayerCommandMsg *>(msg);
			c.which_body = netproto_Command_disconnect_player_tag;
			c.disconnect_player.slot = m->getDisconnectSlot();
			c.disconnect_player.disconnect_frame = m->getDisconnectFrame();
			break;
		}
		case NETCOMMANDTYPE_CHAT:
		case NETCOMMANDTYPE_DISCONNECTCHAT:
		{
			UnicodeString text;
			if (msg->getNetCommandType() == NETCOMMANDTYPE_CHAT)
			{
				const NetChatCommandMsg *m = static_cast<const NetChatCommandMsg *>(msg);
				text = m->getText();
				c.chat.player_mask = m->getPlayerMask();
			}
			else
			{
				text = static_cast<const NetDisconnectChatCommandMsg *>(msg)->getText();
			}

			const Int length = min<Int>(text.getLength(), 255);
			c.which_body = netproto_Command_chat_tag;
			c.chat.text_count = (pb_size_t)length;
			for (Int i = 0; i < length; ++i)
				c.chat.text[i] = (uint32_t)(UnsignedShort)text.getCharAt(i);
			break;
		}
		case NETCOMMANDTYPE_DISCONNECTVOTE:
		{
			const NetDisconnectVoteCommandMsg *m = static_cast<const NetDisconnectVoteCommandMsg *>(msg);
			c.which_body = netproto_Command_disconnect_vote_tag;
			c.disconnect_vote.slot = m->getSlot();
			c.disconnect_vote.vote_frame = m->getVoteFrame();
			break;
		}
		case NETCOMMANDTYPE_PROGRESS:
			c.which_body = netproto_Command_progress_tag;
			c.progress.percentage = static_cast<const NetProgressCommandMsg *>(msg)->getPercentage();
			break;
		case NETCOMMANDTYPE_FILE:
		{
			const NetFileCommandMsg *m = static_cast<const NetFileCommandMsg *>(msg);
			c.which_body = netproto_Command_file_tag;
			strlcpy(c.file.filename, m->getPortableFilename().str(), ARRAY_SIZE(c.file.filename));
			bytes.data = m->getFileData();
			bytes.size = m->getFileLength();
			break;
		}
		case NETCOMMANDTYPE_FILEANNOUNCE:
		{
			const NetFileAnnounceCommandMsg *m = static_cast<const NetFileAnnounceCommandMsg *>(msg);
			c.which_body = netproto_Command_file_announce_tag;
			strlcpy(c.file_announce.filename, m->getPortableFilename().str(), ARRAY_SIZE(c.file_announce.filename));
			c.file_announce.file_id = m->getFileID();
			c.file_announce.player_mask = m->getPlayerMask();
			break;
		}
		case NETCOMMANDTYPE_FILEPROGRESS:
		{
			const NetFileProgressCommandMsg *m = static_cast<const NetFileProgressCommandMsg *>(msg);
			c.which_body = netproto_Command_file_progress_tag;
			c.file_progress.file_id = m->getFileID();
			c.file_progress.progress = m->getProgress();
			break;
		}
		case NETCOMMANDTYPE_DISCONNECTFRAME:
			c.which_body = netproto_Command_disconnect_frame_tag;
			c.disconnect_frame.disconnect_frame = static_cast<const NetDisconnectFrameCommandMsg *>(msg)->getDisconnectFrame();
			break;
		case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
			c.which_body = netproto_Command_screen_off_tag;
			c.screen_off.new_frame = static_cast<const NetDisconnectScreenOffCommandMsg *>(msg)->getNewFrame();
			break;
		case NETCOMMANDTYPE_KEEPALIVE:
		case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:
		case NETCOMMANDTYPE_PACKETROUTERQUERY:
		case NETCOMMANDTYPE_PACKETROUTERACK:
		case NETCOMMANDTYPE_LOADCOMPLETE:
		case NETCOMMANDTYPE_TIMEOUTSTART:
			break;

		default:
			return false;
		}

		return true;
	}

	bool encodeCommands(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
	{
		const CommandSource *source = static_cast<const CommandSource *>(*arg);

		for (Int i = 0; i < source->count; ++i)
		{
			netproto_Command c;
			BytesSource bytes;
			if (!fillCommand(c, *source->refs[i], bytes))
				return false;

			c.payload.funcs.encode = encodeBytes;
			c.payload.arg = &bytes;

			if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, netproto_Command_fields, &c))
				return false;
		}

		return true;
	}

	bool measureCommand(const NetCommandRef *ref, size_t &size)
	{
		netproto_Command c;
		BytesSource bytes;
		if (!fillCommand(c, *ref, bytes))
			return false;

		c.payload.funcs.encode = encodeBytes;
		c.payload.arg = &bytes;
		return pb_get_encoded_size(&size, netproto_Command_fields, &c);
	}


	bool decodeArg(pb_istream_t *stream, const pb_field_t *, void **arg)
	{
		DecodeState *state = static_cast<DecodeState *>(*arg);
		if (state->args.size() >= MAX_ARGUMENTS)
			return false;

		netproto_Arg decoded;
		memset(&decoded, 0, sizeof(decoded));
		if (!pb_decode(stream, netproto_Arg_fields, &decoded))
			return false;

		state->args.push_back(decoded);
		return true;
	}

	bool decodePayload(pb_istream_t *stream, const pb_field_t *, void **arg)
	{
		DecodeState *state = static_cast<DecodeState *>(*arg);
		if (state->payload != nullptr || stream->bytes_left > MAX_PAYLOAD_BYTES)
			return false;

		state->payloadSize = (UnsignedInt)stream->bytes_left;
		state->payload = NEW UnsignedByte[state->payloadSize];
		return pb_read(stream, state->payload, state->payloadSize);
	}

	bool convertArg(NetGameCommandMsg *msg, const netproto_Arg &arg);

	bool decodeBundled(pb_istream_t *stream, const pb_field_t *, void **arg)
	{
		DecodeState *state = static_cast<DecodeState *>(*arg);
		if (state->bundled.size() >= MAX_ARGUMENTS)
			return false;

		DecodeState inner;
		netproto_BundledGameCommand decoded;
		memset(&decoded, 0, sizeof(decoded));
		decoded.args.funcs.decode = decodeArg;
		decoded.args.arg = &inner;
		if (!pb_decode(stream, netproto_BundledGameCommand_fields, &decoded))
			return false;

		NetGameCommandMsg *game = newInstance(NetGameCommandMsg);
		game->setGameMessageType((GameMessage::Type)(Int)decoded.message_type);
		for (size_t i = 0; i < inner.args.size(); ++i)
		{
			if (!convertArg(game, inner.args[i]))
			{
				game->detach();
				return false;
			}
		}

		state->bundled.push_back(game);
		return true;
	}

	bool convertArg(NetGameCommandMsg *msg, const netproto_Arg &arg)
	{
		GameMessageArgumentType data;
		memset(&data, 0, sizeof(data));
		GameMessageArgumentDataType type;

		switch (arg.which_value)
		{
		case netproto_Arg_integer_tag:
			type = ARGUMENTDATATYPE_INTEGER;
			data.integer = arg.integer;
			break;
		case netproto_Arg_real_tag:
			type = ARGUMENTDATATYPE_REAL;
			data.real = arg.real;
			break;
		case netproto_Arg_boolean_tag:
			type = ARGUMENTDATATYPE_BOOLEAN;
			data.boolean = arg.boolean;
			break;
		case netproto_Arg_object_id_tag:
			type = ARGUMENTDATATYPE_OBJECTID;
			data.objectID = (ObjectID)arg.object_id;
			break;
		case netproto_Arg_drawable_id_tag:
			type = ARGUMENTDATATYPE_DRAWABLEID;
			data.drawableID = (DrawableID)arg.drawable_id;
			break;
		case netproto_Arg_team_id_tag:
			type = ARGUMENTDATATYPE_TEAMID;
			data.teamID = arg.team_id;
			break;
		case netproto_Arg_location_tag:
			type = ARGUMENTDATATYPE_LOCATION;
			data.location.x = arg.location.x;
			data.location.y = arg.location.y;
			data.location.z = arg.location.z;
			break;
		case netproto_Arg_pixel_tag:
			type = ARGUMENTDATATYPE_PIXEL;
			data.pixel.x = arg.pixel.x;
			data.pixel.y = arg.pixel.y;
			break;
		case netproto_Arg_pixel_region_tag:
			type = ARGUMENTDATATYPE_PIXELREGION;
			data.pixelRegion.lo.x = arg.pixel_region.lo.x;
			data.pixelRegion.lo.y = arg.pixel_region.lo.y;
			data.pixelRegion.hi.x = arg.pixel_region.hi.x;
			data.pixelRegion.hi.y = arg.pixel_region.hi.y;
			break;
		case netproto_Arg_timestamp_tag:
			type = ARGUMENTDATATYPE_TIMESTAMP;
			data.timestamp = arg.timestamp;
			break;
		case netproto_Arg_wide_char_tag:
			type = ARGUMENTDATATYPE_WIDECHAR;
			data.wChar = (WideChar)arg.wide_char;
			break;
		default:
			return false;
		}

		msg->addArgument(type, data);
		return true;
	}

	NetCommandMsg *createMessage(NetCommandType type)
	{
		switch (type)
		{
		case NETCOMMANDTYPE_FRAMEINFO:				return newInstance(NetFrameCommandMsg);
		case NETCOMMANDTYPE_PLAYERLEAVE:			return newInstance(NetPlayerLeaveCommandMsg);
		case NETCOMMANDTYPE_RUNAHEADMETRICS:		return newInstance(NetRunAheadMetricsCommandMsg);
		case NETCOMMANDTYPE_RUNAHEAD:				return newInstance(NetRunAheadCommandMsg);
		case NETCOMMANDTYPE_DESTROYPLAYER:			return newInstance(NetDestroyPlayerCommandMsg);
		case NETCOMMANDTYPE_KEEPALIVE:				return newInstance(NetKeepAliveCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTCHAT:			return newInstance(NetDisconnectChatCommandMsg);
		case NETCOMMANDTYPE_CHAT:					return newInstance(NetChatCommandMsg);
		case NETCOMMANDTYPE_PROGRESS:				return newInstance(NetProgressCommandMsg);
		case NETCOMMANDTYPE_LOADCOMPLETE:			return newInstance(NetLoadCompleteCommandMsg);
		case NETCOMMANDTYPE_TIMEOUTSTART:			return newInstance(NetTimeOutGameStartCommandMsg);
		case NETCOMMANDTYPE_FILE:					return newInstance(NetFileCommandMsg);
		case NETCOMMANDTYPE_FILEANNOUNCE:			return newInstance(NetFileAnnounceCommandMsg);
		case NETCOMMANDTYPE_FILEPROGRESS:			return newInstance(NetFileProgressCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTKEEPALIVE:	return newInstance(NetDisconnectKeepAliveCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTPLAYER:		return newInstance(NetDisconnectPlayerCommandMsg);
		case NETCOMMANDTYPE_PACKETROUTERQUERY:		return newInstance(NetPacketRouterQueryCommandMsg);
		case NETCOMMANDTYPE_PACKETROUTERACK:		return newInstance(NetPacketRouterAckCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTVOTE:			return newInstance(NetDisconnectVoteCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTFRAME:		return newInstance(NetDisconnectFrameCommandMsg);
		case NETCOMMANDTYPE_DISCONNECTSCREENOFF:	return newInstance(NetDisconnectScreenOffCommandMsg);
		default:									return nullptr;
		}
	}

	NetCommandMsg *buildMessage(const netproto_Command &c, DecodeState &state)
	{
		if (c.id > 0xFFFF)
			return nullptr;

		NetCommandMsg *msg = createMessage((NetCommandType)c.type);
		if (msg == nullptr)
			return nullptr;

		msg->setExecutionFrame(c.frame);
		msg->setPlayerID(c.player);
		msg->setID((UnsignedShort)c.id);

		switch (msg->getNetCommandType())
		{
		case NETCOMMANDTYPE_FRAMEINFO:
		{
			NetFrameCommandMsg *m = static_cast<NetFrameCommandMsg *>(msg);
			m->setID((UnsignedShort)(c.frame & 0xFFFF));
			if (c.which_body == netproto_Command_frame_info_tag)
				m->setMetrics(c.frame_info.latency_ms, c.frame_info.fps);
			// Position in the bundle is the order of the commands within the frame.
			for (size_t i = 0; i < state.bundled.size(); ++i)
			{
				NetGameCommandMsg *game = state.bundled[i];
				game->setExecutionFrame(c.frame);
				game->setPlayerID(c.player);
				game->setID((UnsignedShort)(i + 1));
				m->addBundled(game);
			}
			break;
		}
		case NETCOMMANDTYPE_PLAYERLEAVE:
			static_cast<NetPlayerLeaveCommandMsg *>(msg)->setLeavingPlayerID((UnsignedByte)c.player_leave.leaving_player);
			break;
		case NETCOMMANDTYPE_RUNAHEADMETRICS:
		{
			NetRunAheadMetricsCommandMsg *m = static_cast<NetRunAheadMetricsCommandMsg *>(msg);
			m->setAverageLatency(c.run_ahead_metrics.average_latency);
			m->setAverageFps(c.run_ahead_metrics.average_fps);
			break;
		}
		case NETCOMMANDTYPE_RUNAHEAD:
		{
			NetRunAheadCommandMsg *m = static_cast<NetRunAheadCommandMsg *>(msg);
			m->setRunAhead((UnsignedShort)c.run_ahead.run_ahead);
			m->setFrameRate((UnsignedByte)c.run_ahead.frame_rate);
			break;
		}
		case NETCOMMANDTYPE_DESTROYPLAYER:
			static_cast<NetDestroyPlayerCommandMsg *>(msg)->setPlayerIndex(c.destroy_player.player_index);
			break;
		case NETCOMMANDTYPE_DISCONNECTPLAYER:
		{
			NetDisconnectPlayerCommandMsg *m = static_cast<NetDisconnectPlayerCommandMsg *>(msg);
			m->setDisconnectSlot((UnsignedByte)c.disconnect_player.slot);
			m->setDisconnectFrame(c.disconnect_player.disconnect_frame);
			break;
		}
		case NETCOMMANDTYPE_CHAT:
		case NETCOMMANDTYPE_DISCONNECTCHAT:
		{
			WideChar text[256];
			const Int length = c.chat.text_count < 255 ? c.chat.text_count : 255;
			for (Int i = 0; i < length; ++i)
				text[i] = (WideChar)c.chat.text[i];
			text[length] = 0;

			if (msg->getNetCommandType() == NETCOMMANDTYPE_CHAT)
			{
				NetChatCommandMsg *m = static_cast<NetChatCommandMsg *>(msg);
				m->setText(UnicodeString(text));
				m->setPlayerMask(c.chat.player_mask);
			}
			else
			{
				static_cast<NetDisconnectChatCommandMsg *>(msg)->setText(UnicodeString(text));
			}
			break;
		}
		case NETCOMMANDTYPE_DISCONNECTVOTE:
		{
			NetDisconnectVoteCommandMsg *m = static_cast<NetDisconnectVoteCommandMsg *>(msg);
			m->setSlot((UnsignedByte)c.disconnect_vote.slot);
			m->setVoteFrame(c.disconnect_vote.vote_frame);
			break;
		}
		case NETCOMMANDTYPE_PROGRESS:
			static_cast<NetProgressCommandMsg *>(msg)->setPercentage((UnsignedByte)c.progress.percentage);
			break;
		case NETCOMMANDTYPE_FILE:
		{
			NetFileCommandMsg *m = static_cast<NetFileCommandMsg *>(msg);
			m->setPortableFilename(AsciiString(c.file.filename));

			NetCommandDataChunk chunk(state.payload ? state.payload : NEW UnsignedByte[0], state.payloadSize);
			state.payload = nullptr;
			m->setFileData(chunk);
			break;
		}
		case NETCOMMANDTYPE_FILEANNOUNCE:
		{
			NetFileAnnounceCommandMsg *m = static_cast<NetFileAnnounceCommandMsg *>(msg);
			m->setPortableFilename(AsciiString(c.file_announce.filename));
			m->setFileID((UnsignedShort)c.file_announce.file_id);
			m->setPlayerMask((UnsignedByte)c.file_announce.player_mask);
			break;
		}
		case NETCOMMANDTYPE_FILEPROGRESS:
		{
			NetFileProgressCommandMsg *m = static_cast<NetFileProgressCommandMsg *>(msg);
			m->setFileID((UnsignedShort)c.file_progress.file_id);
			m->setProgress(c.file_progress.progress);
			break;
		}
		case NETCOMMANDTYPE_DISCONNECTFRAME:
			static_cast<NetDisconnectFrameCommandMsg *>(msg)->setDisconnectFrame(c.disconnect_frame.disconnect_frame);
			break;
		case NETCOMMANDTYPE_DISCONNECTSCREENOFF:
			static_cast<NetDisconnectScreenOffCommandMsg *>(msg)->setNewFrame(c.screen_off.new_frame);
			break;
		default:
			break;
		}

		return msg;
	}

	bool decodeCommandStream(pb_istream_t *stream, NetCommandRef *&ref)
	{
		ref = nullptr;

		DecodeState state;
		netproto_Command c;
		memset(&c, 0, sizeof(c));
		c.bundled.funcs.decode = decodeBundled;
		c.bundled.arg = &state;
		c.payload.funcs.decode = decodePayload;
		c.payload.arg = &state;

		if (!pb_decode(stream, netproto_Command_fields, &c))
			return false;

		NetCommandMsg *msg = buildMessage(c, state);
		if (msg == nullptr)
			return false;

		ref = NEW_NETCOMMANDREF(msg);
		msg->detach();
		return true;
	}

	bool decodeCommands(pb_istream_t *stream, const pb_field_t *, void **arg)
	{
		EnvelopeState *state = static_cast<EnvelopeState *>(*arg);

		NetCommandRef *ref = nullptr;
		if (!decodeCommandStream(stream, ref))
			return false;

		state->list->addMessage(ref);
		return true;
	}

}

void setSession(UnsignedInt session)
{
	s_session = session;
}

Channel getChannel(NetCommandType type)
{
	switch (type)
	{
	case NETCOMMANDTYPE_FILE:
	case NETCOMMANDTYPE_FILEANNOUNCE:
	case NETCOMMANDTYPE_FILEPROGRESS:
		return CHANNEL_FILES;
	case NETCOMMANDTYPE_GAMECOMMAND:
	case NETCOMMANDTYPE_FRAMEINFO:
	case NETCOMMANDTYPE_PLAYERLEAVE:
	case NETCOMMANDTYPE_RUNAHEAD:
	case NETCOMMANDTYPE_RUNAHEADMETRICS:
	case NETCOMMANDTYPE_DESTROYPLAYER:
	case NETCOMMANDTYPE_FRAMERESENDREQUEST:
		return CHANNEL_GAMEPLAY;
	default:
		return CHANNEL_MESSAGES;
	}
}

UnsignedInt getCommandSize(const NetCommandRef *ref)
{
	size_t size = 0;
	if (!measureCommand(ref, size))
		return 0;

	// tag of Envelope.commands, length prefix, message
	return 1 + varintSize((UnsignedInt)size) + (UnsignedInt)size;
}

UnsignedInt getSession()
{
	return s_session;
}

UnsignedInt encodeEnvelope(const Hello *hello, const Timing *timing, NetCommandRef *const *refs, Int count, UnsignedByte *buf, UnsignedInt bufSize)
{
	CommandSource source;
	source.refs = refs;
	source.count = count;

	netproto_Envelope env;
	memset(&env, 0, sizeof(env));
	if (hello != nullptr)
	{
		env.has_hello = true;
		env.hello.version = hello->version;
		env.hello.session = hello->session;
		env.hello.slot = hello->slot;
	}
	if (timing != nullptr)
	{
		env.stamp = timing->stamp;
		env.echo_stamp = timing->echoStamp;
		env.echo_hold = timing->echoHold;
	}
	env.commands.funcs.encode = encodeCommands;
	env.commands.arg = &source;

	pb_ostream_t stream = pb_ostream_from_buffer(buf, bufSize);
	if (!pb_encode(&stream, netproto_Envelope_fields, &env))
		return 0;

	return (UnsignedInt)stream.bytes_written;
}

NetCommandList *decodeEnvelope(const UnsignedByte *data, UnsignedInt size, Hello *hello, Bool *hasHello, Timing *timing)
{
	NetCommandList *list = newInstance(NetCommandList);
	list->init();

	EnvelopeState state;
	state.list = list;

	netproto_Envelope env;
	memset(&env, 0, sizeof(env));
	env.commands.funcs.decode = decodeCommands;
	env.commands.arg = &state;

	pb_istream_t stream = pb_istream_from_buffer(data, size);
	if (!pb_decode(&stream, netproto_Envelope_fields, &env))
	{
		deleteInstance(list);
		return nullptr;
	}

	if (timing != nullptr)
	{
		timing->stamp = env.stamp;
		timing->echoStamp = env.echo_stamp;
		timing->echoHold = env.echo_hold;
	}
	if (hasHello != nullptr)
		*hasHello = env.has_hello ? TRUE : FALSE;
	if (hello != nullptr && env.has_hello)
	{
		hello->version = env.hello.version;
		hello->session = env.hello.session;
		hello->slot = env.hello.slot;
	}

	return list;
}

}

#endif
