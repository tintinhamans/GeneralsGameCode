/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#include "PreRTS.h"

#include <string.h>

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameClient/Color.h"
#include "GameLogic/GameLogic.h"
#include "Common/OptionPreferences.h"
#include "Common/FileSystem.h"
#include "Common/Recorder.h"
#include "Common/version.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LanguageFilter.h"
#include "GameNetwork/Caster/Caster.h"
#include "GameNetwork/Caster/CasterChat.h"
#include "GameNetwork/Caster/CasterChatMessage.h"
#include "GameNetwork/Caster/CasterCommandFrame.h"
#include "GameNetwork/Caster/CasterFrameHistory.h"
#include "GameNetwork/Caster/CasterBootstrap.h"
#include "GameNetwork/Caster/CasterProtocol.h"
#include "GameNetwork/Caster/CasterTransport.h"

static const UnsignedInt CASTER_LOBBY_INTERVAL_MS = 1000;

/// Repeat period for the typed match-start bootstrap while casters watch.
static const UnsignedInt CASTER_BOOTSTRAP_INTERVAL_MS = 2000;

static Bool isCasterNetworkMessage(const GameMessage* message)
{
	return message != nullptr
		&& message->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES
		&& message->getType() < GameMessage::MSG_END_NETWORK_MESSAGES;
}

static GameMessage* copyCasterNetworkMessage(const GameMessage* source)
{
	GameMessage* copy;
	UnsignedByte index;

	if (!isCasterNetworkMessage(source)) return nullptr;
	copy = newInstance(GameMessage)(source->getType());
	if (copy == nullptr) return nullptr;
	copy->friend_setPlayerIndex(source->getPlayerIndex());
	for (index = 0; index < source->getArgumentCount(); ++index)
	{
		const GameMessageArgumentType* argument = source->getArgument(index);
		if (argument == nullptr) { deleteInstance(copy); return nullptr; }
		switch (source->getArgumentDataType(index))
		{
		case ARGUMENTDATATYPE_INTEGER: copy->appendIntegerArgument(argument->integer); break;
		case ARGUMENTDATATYPE_REAL: copy->appendRealArgument(argument->real); break;
		case ARGUMENTDATATYPE_BOOLEAN: copy->appendBooleanArgument(argument->boolean); break;
		case ARGUMENTDATATYPE_OBJECTID: copy->appendObjectIDArgument(argument->objectID); break;
		case ARGUMENTDATATYPE_DRAWABLEID: copy->appendDrawableIDArgument(argument->drawableID); break;
		case ARGUMENTDATATYPE_TEAMID: copy->appendTeamIDArgument(argument->teamID); break;
		case ARGUMENTDATATYPE_LOCATION: copy->appendLocationArgument(argument->location); break;
		case ARGUMENTDATATYPE_PIXEL: copy->appendPixelArgument(argument->pixel); break;
		case ARGUMENTDATATYPE_PIXELREGION: copy->appendPixelRegionArgument(argument->pixelRegion); break;
		case ARGUMENTDATATYPE_TIMESTAMP: copy->appendTimestampArgument(argument->timestamp); break;
		case ARGUMENTDATATYPE_WIDECHAR: copy->appendWideCharArgument(argument->wChar); break;
		default: deleteInstance(copy); return nullptr;
		}
	}
	return copy;
}

static void deleteCasterMessageList(GameMessage* first)
{
	while (first != nullptr)
	{
		GameMessage* next = first->next();
		deleteInstance(first);
		first = next;
	}
}

Caster* TheCaster = nullptr;

Caster::Caster()
	: m_server(nullptr),
		m_client(nullptr),
		m_haveClientUpdateTime(FALSE),
		m_lastClientUpdateMs(0),
		m_recordingFile(nullptr),
		m_commandHistory(nullptr),
		m_lastCommandFrame(0),
		m_hasCommandFrames(FALSE),
		m_passiveFrame(0),
		m_passiveFrameLength(0),
		m_passiveFramePrepared(FALSE),
		m_passiveFrameValid(FALSE),

		m_casterChatId(GetTickCount()),
		m_lobbyChatId(GetTickCount()),
		m_playbackEntered(FALSE),
		m_passivePlaybackEndPending(FALSE),
		m_postgameChat(FALSE),
		m_watchFaultReported(FALSE),
		m_lastLobbyLen(0),
		m_lastLobbyMs(0),
		m_haveLastLobby(FALSE)
{
	m_casterUid[0] = '\0';
	m_casterChatName[0] = '\0';
	m_watchHostIp[0] = '\0';
	m_lastDiscoveryRequestMs = 0;
	m_discoveryRequestAttempts = 0;
	m_queryHostIp[0] = '\0';
	m_queryIpCount = 0;
	m_lastPlayerQueryMs = 0;
	m_playerQueryAttempts = 0;
	clearWatchedSlotColors();
	m_lastLobbyText[0] = '\0';
	CasterBootstrap::clear(m_matchBootstrap);
	memset(m_matchBootstrapFrame, 0, sizeof(m_matchBootstrapFrame));
	m_matchBootstrapFrameLen = 0;
	m_lastMatchBootstrapMs = 0;
	m_haveMatchBootstrap = FALSE;
	InitializeCriticalSection(&m_queueLock);
}

Caster::~Caster()
{
	shutdown();
	DeleteCriticalSection(&m_queueLock);
}

Bool Caster::getTelemetry(Telemetry& telemetry) const
{
	if (!isCaster() || !playbackEntered() || m_client == nullptr || TheGameLogic == nullptr)
		return FALSE;
	CasterTransportCore::SourceSetSnapshot snapshot;
	m_client->snapshot(snapshot);
	const UnsignedInt sourceFrame = snapshot.reportedFrame;
	telemetry.reportAgeMs = snapshot.reportAgeMs;
	telemetry.reported = snapshot.hasProgressReport;
	telemetry.sources = snapshot.connectedSources;
	const UnsignedInt playbackFrame = TheGameLogic->getFrame();
	telemetry.available = telemetry.reported && sourceFrame >= playbackFrame;
	telemetry.delayFrames = telemetry.available ? sourceFrame - playbackFrame : 0;
	telemetry.stale = telemetry.reported && (telemetry.reportAgeMs > 1000 || telemetry.sources == 0);
	return TRUE;
}

Bool Caster::enablePlayer(UnsignedShort tcpPort)
{
	CasterTransport::Server* server;

	if (m_server != nullptr || m_client != nullptr)
	{
		return FALSE;
	}

	m_core.setRole(CASTER_ROLE_PLAYER);
	m_core.setBroadcaster(this);

	server = new CasterTransport::Server();
	if (server == nullptr || !server->start(&m_core, tcpPort))
	{
		delete server;
		m_core.setBroadcaster(nullptr);
		m_core.resetRole();
		return FALSE;
	}

	m_server = server;
	m_server->setChatSink(this);
	return TRUE;
}

Bool Caster::enableCaster(UnsignedShort tcpPort)
{
	if (m_server != nullptr || m_client != nullptr)
	{
		return FALSE;
	}

	m_core.setRole(CASTER_ROLE_CASTER);

	m_client = new CasterTransport::Client();
	if (m_client == nullptr)
	{
		m_core.resetRole();
		return FALSE;
	}

	m_haveClientUpdateTime = FALSE;
	m_client->setChatSink(this);
	return TRUE;
}

void Caster::shutdown()
{
	m_haveClientUpdateTime = FALSE;
	m_recordingFile = nullptr;
	if (TheGameLogic != nullptr)
	{
		TheGameLogic->setCommandFrameSource(nullptr);
	}

	if (m_client != nullptr)
	{
		m_client->stop();
		delete m_client;
		m_client = nullptr;
	}
	if (m_server != nullptr)
	{
		m_server->stop();
		m_server->setCommandHistory(nullptr);
		delete m_server;
		m_server = nullptr;
	}
	if (m_commandHistory != nullptr)
	{
		m_commandHistory->close();
		delete m_commandHistory;
		m_commandHistory = nullptr;
	}

	m_core.setBroadcaster(nullptr);
	m_core.resetRole();
	m_core.stopWatching();
	m_core.clearAnnouncedGame();
	m_core.clearInbound();
	m_session.reset();
	m_beaconSources.reset();
	m_playbackEntered = FALSE;
	m_watchHostIp[0] = '\0';
	m_queryHostIp[0] = '\0';
	m_lobbyLines.clear();

}

CasterRole Caster::role() const
{
	return m_core.role();
}

Bool Caster::isPlayer() const
{
	return m_core.isPlayer();
}

Bool Caster::isCaster() const
{
	return m_core.isCaster();
}

void Caster::update()
{
	if (m_passivePlaybackEndPending)
	{
		// Recorder callbacks can occur while the game is unwinding. Defer the
		// caster teardown until this main-thread update, outside that callback.
		m_passivePlaybackEndPending = FALSE;
		// Keep the transport and watch alive so the caster's score screen can
		// still chat; endPostgameChat() tears it down when that screen closes.
		if (TheGameLogic != nullptr)
		{
			TheGameLogic->setCommandFrameSource(nullptr);
		}
		m_postgameChat = TRUE;
	}

	if (m_client != nullptr)
	{
		const UnsignedInt now = timeGetTime();
		const UnsignedInt elapsedMs = m_haveClientUpdateTime ? now - m_lastClientUpdateMs : 0;
		m_lastClientUpdateMs = now;
		m_haveClientUpdateTime = TRUE;
		m_client->update(elapsedMs);
		if (m_core.isCaster() && m_core.isWatching() && !m_watchFaultReported && !m_postgameChat)
		{
			if (m_client->protocolIncompatible())
			{
				m_watchFaultReported = TRUE;
				finishWatch(WATCH_EXIT_PROTOCOL_INCOMPATIBLE);
			}
			else if (m_client->transportDisagreement())
			{
				m_watchFaultReported = TRUE;
				finishWatch(WATCH_EXIT_TRANSPORT_DISAGREEMENT);
			}
		}
	}

	if (CasterBeacon::shouldRequestDetails(m_core.isCaster() && !m_core.isWatching()
		&& m_watchHostIp[0] != '\0', timeGetTime(), m_lastDiscoveryRequestMs,
		m_discoveryRequestAttempts))
	{
		m_lastDiscoveryRequestMs = timeGetTime();
		++m_discoveryRequestAttempts;
		RequestCasterHostDetails(m_watchHostIp);
	}

	updatePlayerQueries();

	if (m_core.isCaster() && !m_playbackEntered && !m_postgameChat)
	{
		enterPlaybackIfReady();
	}

	if (m_core.isPlayer() && !m_core.isGameAnnounced())
	{
		announceLobbyGame();
	}

	if (m_recordingFile != nullptr)
	{
		if (TheRecorder == nullptr || TheRecorder->getCasterFile() != m_recordingFile)
		{
			m_recordingFile = nullptr;
		}
	}

	if (m_core.isPlayer())
	{
		forwardMatchBootstrap();
		forwardLobbyState();
	}

	drainInbound();
}

void Caster::drainInbound()
{
	CasterInbound event;

	for (;;)
	{
		Bool got;

		EnterCriticalSection(&m_queueLock);
		got = m_core.popInbound(event);
		LeaveCriticalSection(&m_queueLock);

		if (!got)
		{
			break;
		}
		handleInboundChat(event);
	}
}

void Caster::handleInboundChat(const CasterInbound& event)
{
	const Bool localCaster = m_core.isCaster();
	const UnsignedInt delivery = CasterChat::routeDelivery(event.direction,
		(event.isSenderCaster != 0), localCaster, m_core.privacyFilter());

	if ((delivery & CasterChat::DELIVER_LOBBY_LINE) != 0)
	{
		if (localCaster)
		{
			m_lobbyLines.push(event.text, event.textLen, event.senderName, event.senderNameLen,
				event.senderSlot, (event.isSenderCaster != 0), (event.isEmote != 0));
		}
		else
		{
			PostCasterLocalLine(event.text, event.senderName, event.senderSlot,
				(event.isSenderCaster != 0), (event.isEmote != 0));
		}
	}
	if ((delivery & CasterChat::DELIVER_IN_GAME) != 0)
	{
		displayChatLine(event);
	}
	if ((delivery & CasterChat::DELIVER_RELAY) != 0)
	{
		relayChatLine(event);
	}
}

void Caster::displayChatLine(const CasterInbound& event)
{
	ChatMessage chat;
	UnicodeString line;
	RGBColor rgb;
	Player* player;
	Color color = 0;

	if (TheInGameUI == nullptr || !MakeCasterChatMessage(chat, event))
	{
		return;
	}

	player = (ThePlayerList != nullptr) ? ThePlayerList->getPlayerFromSlotIndex(event.senderSlot) : nullptr;
	if (player != nullptr)
	{
		if (chat.displayName.isEmpty())
		{
			chat.displayName = player->getPlayerDisplayName();
		}
		chat.hasColor = TRUE;
		chat.color = (Color)player->getPlayerColor();
	}

	if (!chat.hasColor && !chat.isCaster && player == nullptr)
	{
		applyCasterChatColor(chat);
	}
	RenderChatMessage(chat, 0, line, color);
	if (!chat.isCaster && player == nullptr && !chat.hasColor)
	{
		TheInGameUI->message(L"%ls", line.str());
		return;
	}
	rgb.setFromInt(color);
	TheInGameUI->messageColor(&rgb, L"%ls", line.str());
}

void Caster::relayChatLine(const CasterInbound& event)
{
	UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt frameLen;

	if (m_server == nullptr)
	{
		return;
	}

	frameLen = CasterProtocol::encodeChat(frame, sizeof(frame), event.gameUid,
		event.commandID, event.direction, event.senderSlot, (UnsignedByte)1, event.senderTeam,
		event.senderIdentity, event.senderName, event.senderNameLen, event.text, event.textLen,
		(event.isEmote != 0));
	if (frameLen == 0)
	{
		return;
	}
	m_server->broadcastChatFrame(frame, frameLen, event.senderLinkId);
}

Bool Caster::pushInbound(const CasterInbound& event)
{
	return pushInboundLocked(event);
}

Bool Caster::pushInboundLocked(const CasterInbound& event)
{
	Bool ok;

	EnterCriticalSection(&m_queueLock);
	ok = m_core.pushInbound(event);
	LeaveCriticalSection(&m_queueLock);
	return ok;
}

//-----------------------------------------------------------------------------
// Recorder seams (main thread)
//-----------------------------------------------------------------------------

void Caster::onRecordingStarted(CasterReplayFile* recordingFile)
{
	if (recordingFile == nullptr)
	{
		return;
	}
	if (m_server != nullptr)
	{
		m_server->setCommandHistory(nullptr);
		if (m_commandHistory == nullptr)
		{
			try
			{
				m_commandHistory = new CasterFrameHistory();
			}
			catch (...)
			{
				m_commandHistory = nullptr;
			}
		}
		// open() closes first, so it both starts a fresh history and recycles an
		// existing one.
		if (m_commandHistory != nullptr)
		{
			if (m_commandHistory->open())
			{
				m_server->setCommandHistory(m_commandHistory);
			}
			else
			{
				delete m_commandHistory;
				m_commandHistory = nullptr;
			}
		}
		m_hasCommandFrames = FALSE;
		m_lastCommandFrame = 0;
	}

	m_recordingFile = recordingFile;
}

void Caster::publishCommandFrame(UnsignedInt frame, GameMessage* first)
{
	UnsignedInt length;
	GameMessage* copiedFirst = nullptr;
	GameMessage* copiedLast = nullptr;

	if (m_server == nullptr || !m_core.isPlayer() || !m_core.isGameAnnounced())
	{
		return;
	}
	if (!m_hasCommandFrames && frame == 1)
	{
		// The scratch buffer is free at this point and reused below for the real
		// batch, so the empty baseline costs no extra memory.
		UnsignedInt emptyLength = CasterCommandFrame::encode(0, nullptr,
			m_commandFrameBytes, sizeof(m_commandFrameBytes));

		// Network deliberately discards pregame frame zero.  Persist its explicit
		// empty batch so history and late subscribers share the same baseline.
		if (emptyLength == 0 || !m_server->broadcastCommandFrame(
			m_core.currentGameUid(), 0, m_commandFrameBytes, emptyLength))
		{
			return;
		}
	}

	// RelayCommandsToCommandList has already applied its ordering and
	// frame-synchronized transformations. This is the sole live/history write;
	// no caster acknowledgement barrier participates in player lockstep.
	for (GameMessage* message = first; message != nullptr; message = message->next())
	{
		GameMessage* copy;
		if (!isCasterNetworkMessage(message)) continue;
		copy = copyCasterNetworkMessage(message);
		if (copy == nullptr) { deleteCasterMessageList(copiedFirst); return; }
		if (copiedLast != nullptr) copiedLast->friend_setNext(copy); else copiedFirst = copy;
		copiedLast = copy;
	}

	length = CasterCommandFrame::encode(frame, copiedFirst, m_commandFrameBytes,
		sizeof(m_commandFrameBytes));
	deleteCasterMessageList(copiedFirst);
	if (length != 0 && m_server->broadcastCommandFrame(m_core.currentGameUid(), frame,
		m_commandFrameBytes, length))
	{
		m_lastCommandFrame = frame;
		m_hasCommandFrames = TRUE;
	}
}

Bool Caster::appendPassiveCommandFrame(UnsignedInt frame, CommandList* commands)
{
	if (commands == nullptr || !isFrameReady(frame))
	{
		return FALSE;
	}

	if (!CasterCommandFrame::appendDecoded(m_passiveFrameBytes, m_passiveFrameLength,
		frame, commands))
	{
		m_passiveFrameValid = FALSE;
		return FALSE;
	}

	// Advancing the transport watermark only after the command list accepted the
	// fully validated batch prevents retries from publishing a partial frame.
	m_client->consumeNativeFrame(frame);
	m_session.beginPlaying();
	m_passiveFramePrepared = FALSE;
	m_passiveFrameLength = 0;
	return TRUE;
}

Bool Caster::isFrameReady(UnsignedInt frame)
{
	// Readiness includes source-progress reconciliation and the terminal drain
	// exception in the client.
	UnsignedInt length;


	if (m_passiveFramePrepared && m_passiveFrame == frame)
	{
		return m_passiveFrameValid;
	}
	if (m_client == nullptr || m_client->nextNativeFrame() != frame
		|| !m_client->nativeFrameAvailable(frame))
	{
		return FALSE;
	}
	{
		Bool hasFrames;
		UnsignedInt finalFrame;
		Bool terminalDrain = m_client->nativeEnd(hasFrames, finalFrame)
			&& hasFrames && frame == finalFrame;
		if (!terminalDrain && !m_client->nativeSourcesProgressThrough(frame + 1))
		{
			return FALSE;
		}
	}

	length = m_client->copyNativeFrame(frame, m_passiveFrameBytes,
		sizeof(m_passiveFrameBytes));
	if (length == 0)
	{
		return FALSE;
	}

	m_passiveFrame = frame;
	m_passiveFrameLength = length;
	m_passiveFramePrepared = TRUE;
	// Validate without touching the engine list. A malformed reconciled batch is
	// quarantined locally and stalls only this caster.
	m_passiveFrameValid = CasterCommandFrame::validate(m_passiveFrameBytes,
		m_passiveFrameLength, frame);
	return m_passiveFrameValid;
}

Bool Caster::needsCatchUp(UnsignedInt frame)
{
	UnsignedInt firstFrame;
	UnsignedInt lastFrame;
	UnsignedInt frameCount;

	return m_client != nullptr && m_client->nativeProgress(firstFrame, lastFrame, frameCount)
		&& frameCount != 0 && lastFrame > frame + 2;
}

Bool Caster::appendFrame(UnsignedInt frame, CommandList* commands)
{
	return appendPassiveCommandFrame(frame, commands);
}

Bool Caster::hasEnded()
{
	Bool hasFrames;
	UnsignedInt finalFrame;

	if (m_client == nullptr)
	{
		return FALSE;
	}
	// A transport timeout means every source is unavailable without a terminal
	// declaration.  End local playback cleanly; never manufacture a final frame.
	if (m_client->nativeUnavailable())
	{
		return TRUE;
	}
	// The playback frame and the next unconsumed transport frame advance in
	// lockstep, so the next native frame is the frame being asked about.
	return m_client->nativeEnd(hasFrames, finalFrame)
		&& (!hasFrames || m_client->nextNativeFrame() > finalFrame);
}

void Caster::onPassivePlaybackEnded()
{
	if (m_core.isCaster() && m_playbackEntered)
	{
		finishWatch((m_client != nullptr && m_client->nativeUnavailable())
			? WATCH_EXIT_ALL_SOURCES_LOST : WATCH_EXIT_MATCH_COMPLETED);
	}
}

void Caster::endPostgameChat()
{
	if (m_postgameChat)
	{
		unsubscribe();
	}
}

void Caster::onRecorderClosed()
{
	if (m_server != nullptr && m_core.isPlayer() && m_core.isGameAnnounced())
	{
		m_server->broadcastCommandEnd(m_core.currentGameUid(), m_hasCommandFrames,
			m_lastCommandFrame);
	}
	m_recordingFile = nullptr;
	// The match is over: stop claiming it's running so late queries and
	// reconnects don't get served the frozen last frame as if live.
	m_core.clearAnnouncedGame();
}

Bool Caster::announceCurrentGame()
{
	char gameName[CASTER_GAME_NAME_BYTES + 1];
	UnsignedInt hostIP;
	UnsignedInt seed;
	UnsignedInt gameUid;

	if (m_recordingFile == nullptr || m_casterUid[0] == '\0')
	{
		return FALSE;
	}

	if (!GetCasterGameName(gameName, sizeof(gameName)))
	{
		return FALSE;
	}
	if (!GetCasterGameSeed(hostIP, seed))
	{
		return FALSE;
	}

	gameUid = CasterBeacon::gameUid(hostIP, seed);

	// The recorder is writing, so the match is running: promote subscribers.
	return announceGame(gameUid, gameName, TRUE);
}

/// Copies one engine string into a bounded UTF-8 field; an over-long or
/// unconvertible string becomes empty rather than failing the match start.
static void copyVersionText(const UnicodeString& text, Char* out, UnsignedInt cap)
{
	UnsignedInt length;

	out[0] = '\0';
	if (cap < 2 || text.isEmpty())
	{
		return;
	}
	length = CasterProtocol::wideToUtf8(text.str(), (UnsignedInt)text.getLength(),
		out, cap - 1);
	if (length >= cap)
	{
		length = 0;
	}
	out[length] = '\0';
}

Bool Caster::publishMatchBootstrap(Int difficulty, Int originalGameMode, Int rankPoints,
	Int maxFPS)
{
	char options[CasterProtocol::MAX_LOBBY_TEXT_BYTES + 1];
	UnsignedInt optionsLen = 0;
	CasterLobby::LobbyState state;
	UnsignedInt gameUid;
	UnsignedInt i;
	UnsignedInt frameLen;
	Int localIndex = -1;

	m_haveMatchBootstrap = FALSE;
	m_matchBootstrapFrameLen = 0;
	m_lastMatchBootstrapMs = 0;
	CasterBootstrap::clear(m_matchBootstrap);

	if (m_server == nullptr || !m_core.isPlayer() || !m_core.isGameAnnounced())
	{
		return FALSE;
	}
	gameUid = m_core.currentGameUid();
	if (gameUid == 0)
	{
		return FALSE;
	}
	if (!GetCasterMatchOptions(options, sizeof(options), optionsLen) || optionsLen == 0)
	{
		return FALSE;
	}
	if (!CasterLobby::parseLobbyState(options, optionsLen, state))
	{
		return FALSE;
	}

	// The recorder writes the recording player's slot; in a LAN game that is the
	// host, which is the first human slot for every player's copy of the table.
	for (i = 0; i < state.slotCount; ++i)
	{
		if (state.slots[i].kind == CasterLobby::LOBBY_SLOT_HUMAN)
		{
			localIndex = (Int)i;
			break;
		}
	}
	if (localIndex < 0)
	{
		return FALSE;
	}

	m_matchBootstrap.gameUid = gameUid;
	m_matchBootstrap.frameCount = 0;
	m_matchBootstrap.localPlayerIndex = localIndex;
	m_matchBootstrap.difficulty = difficulty;
	m_matchBootstrap.originalGameMode = originalGameMode;
	m_matchBootstrap.rankPoints = rankPoints;
	m_matchBootstrap.maxFPS = maxFPS;
	if (TheVersion != nullptr)
	{
		m_matchBootstrap.versionNumber = TheVersion->getVersionNumber();
		copyVersionText(TheVersion->getUnicodeVersion(), m_matchBootstrap.versionString,
			sizeof(m_matchBootstrap.versionString));
		copyVersionText(TheVersion->getUnicodeBuildTime(), m_matchBootstrap.versionTimeString,
			sizeof(m_matchBootstrap.versionTimeString));
	}
	if (TheGlobalData != nullptr)
	{
		m_matchBootstrap.exeCRC = TheGlobalData->m_exeCRC;
		m_matchBootstrap.iniCRC = TheGlobalData->m_iniCRC;
	}

	// Lobby churn (countdown and its revision) is normalized away so every
	// player's bootstrap is byte-identical and the source set can reconcile it.
	m_matchBootstrap.room.gameUid = gameUid;
	m_matchBootstrap.room.revision = 0;
	m_matchBootstrap.room.countdownSeconds = 0;
	m_matchBootstrap.room.zeroHour = state.zeroHour;
	m_matchBootstrap.room.useStats = state.useStats;
	m_matchBootstrap.room.mapContentsMask = state.mapContentsMask;
	m_matchBootstrap.room.mapCRC = state.mapCRC;
	m_matchBootstrap.room.mapSize = state.mapSize;
	m_matchBootstrap.room.seed = state.seed;
	m_matchBootstrap.room.crcInterval = state.crcInterval;
	m_matchBootstrap.room.superweaponRestriction = state.superweaponRestriction;
	m_matchBootstrap.room.startingCash = state.startingCash;
	m_matchBootstrap.room.oldFactionsOnly = state.oldFactionsOnly;
	memcpy(m_matchBootstrap.room.mapName, state.mapName, sizeof(m_matchBootstrap.room.mapName));
	m_matchBootstrap.room.slotCount = state.slotCount;
	for (i = 0; i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
	{
		m_matchBootstrap.room.slots[i] = state.slots[i];
	}

	frameLen = CasterProtocol::encodeBootstrap(m_matchBootstrapFrame,
		sizeof(m_matchBootstrapFrame), m_matchBootstrap);
	if (frameLen == 0)
	{
		CasterBootstrap::clear(m_matchBootstrap);
		return FALSE;
	}
	m_matchBootstrapFrameLen = frameLen;
	m_haveMatchBootstrap = TRUE;
	m_lastMatchBootstrapMs = timeGetTime();
	m_server->broadcastBootstrapFrame(gameUid, m_matchBootstrapFrame, m_matchBootstrapFrameLen);
	return TRUE;
}

Bool Caster::forwardMatchBootstrap()
{
	UnsignedInt gameUid;
	UnsignedInt now;

	if (m_server == nullptr || !m_haveMatchBootstrap || m_matchBootstrapFrameLen == 0)
	{
		return FALSE;
	}
	gameUid = m_core.currentGameUid();
	if (gameUid == 0 || gameUid != m_matchBootstrap.gameUid
		|| !m_server->isSubscribed(gameUid))
	{
		return FALSE;
	}
	// A new subscriber needs it at once; the periodic repeat covers a bounded
	// queue that dropped it. The caster accepts identical bytes idempotently
	// and only sends it to subscribers, so an unwatched match costs nothing.
	now = timeGetTime();
	if (!m_server->takeBootstrapRequest(gameUid)
		&& (now - m_lastMatchBootstrapMs) < CASTER_BOOTSTRAP_INTERVAL_MS)
	{
		return FALSE;
	}
	m_lastMatchBootstrapMs = now;
	m_server->broadcastBootstrapFrame(gameUid, m_matchBootstrapFrame, m_matchBootstrapFrameLen);
	return TRUE;
}

Bool Caster::announceLobbyGame()
{
	char gameName[CASTER_GAME_NAME_BYTES + 1];
	UnsignedInt hostIP;
	UnsignedInt seed;
	UnsignedInt gameUid;

	if (!m_core.isPlayer() || m_casterUid[0] == '\0')
	{
		return FALSE;
	}

	if (!GetCasterGameName(gameName, sizeof(gameName)))
	{
		return FALSE;
	}
	if (!GetCasterGameSeed(hostIP, seed))
	{
		return FALSE;
	}

	gameUid = CasterBeacon::gameUid(hostIP, seed);
	return announceGame(gameUid, gameName, FALSE);
}

//-----------------------------------------------------------------------------
// Player surface (main thread)
//-----------------------------------------------------------------------------

Bool Caster::announceGame(UnsignedInt gameUid, const char* gameName, Bool matchRunning)
{
	if (!m_core.announceGame(gameUid, gameName, matchRunning))
	{
		return FALSE;
	}

	m_haveLastLobby = FALSE;
	m_lastLobbyLen = 0;
	m_lastLobbyMs = 0;
	// A newly announced game invalidates the previous match start; the recorder
	// path republishes one through publishMatchBootstrap().
	m_haveMatchBootstrap = FALSE;
	m_matchBootstrapFrameLen = 0;
	m_lastMatchBootstrapMs = 0;
	CasterBootstrap::clear(m_matchBootstrap);

	// A now-running match must reach every already-connected caster: the
	// refreshed HELLO_ACK makes each source re-subscribe and be promoted.
	if (m_server != nullptr && matchRunning)
	{
		m_server->refreshGameMetadata(gameUid);
	}
	return TRUE;
}

void Caster::clearAnnouncedGame()
{
	m_core.clearAnnouncedGame();
	m_haveLastLobby = FALSE;
	m_lastLobbyLen = 0;
	m_lastLobbyMs = 0;
	m_haveMatchBootstrap = FALSE;
	m_matchBootstrapFrameLen = 0;
	m_lastMatchBootstrapMs = 0;
	CasterBootstrap::clear(m_matchBootstrap);
}

Bool Caster::isGameAnnounced() const
{
	return m_core.isGameAnnounced();
}

UnsignedInt Caster::announcedGameUid() const
{
	return m_core.currentGameUid();
}

UnsignedShort Caster::serverPort() const
{
	return (m_server != nullptr) ? m_server->port() : 0;
}

Bool Caster::isSubscribed(UnsignedInt gameUid)
{
	return (m_server != nullptr) ? m_server->isSubscribed(gameUid) : FALSE;
}

Bool Caster::sendLobbyChat(const char* utf8Text, UnsignedInt utf8Len, const char* senderName,
	UnsignedInt senderNameLen, UnsignedByte senderSlot, UnsignedByte senderTeam,
	UnsignedInt senderIdentity, Bool isEmote)
{
	UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt frameLen;
	UnsignedInt gameUid;

	if (!m_core.isPlayer() || !m_core.isGameAnnounced() || m_server == nullptr)
	{
		return FALSE;
	}
	gameUid = m_core.currentGameUid();
	if (gameUid == 0 || !m_server->isSubscribed(gameUid))
	{

		return FALSE;
	}

	frameLen = CasterProtocol::encodeChat(frame, sizeof(frame), gameUid, ++m_lobbyChatId,
		(UnsignedByte)CasterProtocol::CHAT_LOBBY, senderSlot, (UnsignedByte)0, senderTeam,
		senderIdentity, senderName, senderNameLen,
		utf8Text, utf8Len, isEmote);
	if (frameLen == 0)
	{
		return FALSE;
	}
	m_server->broadcastChatFrame(frame, frameLen, CasterTransport::INVALID_INDEX);
	return TRUE;
}

Bool Caster::forwardLobbyState()
{
	char text[CasterProtocol::MAX_LOBBY_TEXT_BYTES + 1];
	UnsignedInt textLen = 0;
	UnsignedInt gameUid;
	UnsignedInt now;
	Bool changed;

	if (m_server == nullptr || !m_core.isGameAnnounced())
	{
		return FALSE;
	}
	gameUid = m_core.currentGameUid();
	if (gameUid == 0)
	{
		return FALSE;
	}

	if (!m_server->isSubscribed(gameUid))
	{
		return FALSE;
	}

	if (!GetCasterLobbyState(text, sizeof(text), textLen) || textLen == 0)
	{
		return FALSE;
	}

	changed = (m_server->takeLobbySnapshotRequest(gameUid) || !m_haveLastLobby || m_lastLobbyLen != textLen
		|| memcmp(m_lastLobbyText, text, textLen) != 0) ? TRUE : FALSE;
	now = timeGetTime();
	if (!CasterLobby::shouldForwardLobby(TRUE, m_haveLastLobby, changed, now,
			m_lastLobbyMs, CASTER_LOBBY_INTERVAL_MS))
	{
		return FALSE;
	}

	{
		UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
		UnsignedInt frameLen = CasterProtocol::encodeLobby(frame, sizeof(frame), gameUid,
			text, textLen);
		if (frameLen == 0)
		{
			return FALSE;
		}
		m_server->broadcastLobbyFrame(gameUid, frame, frameLen);
	}

	memcpy(m_lastLobbyText, text, textLen);
	m_lastLobbyText[textLen] = '\0';
	m_lastLobbyLen = textLen;
	m_lastLobbyMs = now;
	m_haveLastLobby = TRUE;
	return TRUE;
}

static void formatIp(UnsignedInt ip, char (&out)[16])
{
	sprintf(out, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

void Caster::updatePlayerQueries()
{
	const UnsignedInt now = timeGetTime();
	LiveCasterSessionState state = m_session.state();
	UnsignedInt ips[CasterBeacon::MAX_SOURCES];
	UnsignedInt count = 0;
	UnsignedInt pending = 0;
	UnsignedInt i;
	char ipText[16];

	if (m_queryHostIp[0] == '\0' || !m_core.isCaster())
	{
		return;
	}
	if (state != LIVE_CASTER_IDLE && state != LIVE_CASTER_DISCOVERING
		&& state != LIVE_CASTER_ROOM)
	{
		// The game started or the session ended: stop querying.
		m_queryHostIp[0] = '\0';
		return;
	}
	if (!CasterBeacon::shouldQueryPlayers(TRUE, now, m_lastPlayerQueryMs, m_playerQueryAttempts))
	{
		return;
	}

	// Slots may fill while we wait, so refresh the cached table while the browser entry lives.
	if (GetCasterGamePlayerIPs(m_queryHostIp, ips, CasterBeacon::MAX_SOURCES, count))
	{
		memcpy(m_queryIps, ips, count * sizeof(ips[0]));
		m_queryIpCount = count;
	}

	for (i = 0; i < m_queryIpCount; ++i)
	{
		formatIp(m_queryIps[i], ipText);
		if (!m_beaconSources.hasIp(ipText))
		{
			++pending;
		}
	}
	if (m_queryIpCount != 0 && pending == 0)
	{
		m_queryHostIp[0] = '\0';
		return;
	}

	m_lastPlayerQueryMs = now;
	++m_playerQueryAttempts;
	for (i = 0; i < m_queryIpCount; ++i)
	{
		formatIp(m_queryIps[i], ipText);
		if (!m_beaconSources.hasIp(ipText))
		{
			RequestCasterHostDetails(ipText);
		}
	}
}

//-----------------------------------------------------------------------------
// Caster surface (main thread)
//-----------------------------------------------------------------------------

Bool Caster::selectGame(const LiveCasterGameKey& key)
{
	return m_session.select(key);
}

Bool Caster::hasSelectedGame() const
{
	return m_session.hasGame();
}

Bool Caster::isSelectedGame(const LiveCasterGameKey& key) const
{
	return m_session.isGame(key);
}

void Caster::beginWatch(UnsignedInt gameUid, const char* gameName)
{
	m_haveClientUpdateTime = FALSE;
	m_client->reset(gameUid);
	m_core.beginWatching(gameUid, gameName);
	if (m_session.hasGame() && m_session.gameKey().uid == gameUid)
	{
		m_session.enterRoom();
	}
	m_core.clearInbound();
	m_playbackEntered = FALSE;
	m_passivePlaybackEndPending = FALSE;
	m_postgameChat = FALSE;
	m_watchFaultReported = FALSE;
	m_passiveFramePrepared = FALSE;
	m_passiveFrameValid = FALSE;
	m_passiveFrameLength = 0;
	m_watchHostIp[0] = '\0';
}

Bool Caster::linkDiscoveredSources()
{
	CasterBeacon::Source source;
	CasterTransportCore::ClientSource existing;
	UnsignedInt index = 0;
	Bool linked = FALSE;

	if (m_client == nullptr || !m_core.isWatching())
	{
		return FALSE;
	}
	while (m_beaconSources.getSourceAt(index, source))
	{
		++index;
		if (source.gameUid != m_core.watchedGameUid())
		{
			continue;
		}
		if (m_client->sourceState(source.sourceId, existing))
		{
			linked = TRUE;
			continue;
		}
		if (m_client->addSource(source.sourceId, source.ip, source.tcpPort, source.isHost))
		{
			linked = TRUE;
		}
	}
	return linked;
}

void Caster::unsubscribe()
{
	m_session.beginEnding();
	m_haveClientUpdateTime = FALSE;
	if (m_client != nullptr)
	{
		m_client->stop();
	}

	if (TheGameLogic != nullptr)
	{
		TheGameLogic->setCommandFrameSource(nullptr);
	}
	m_core.stopWatching();
	m_beaconSources.reset();
	m_playbackEntered = FALSE;
	m_passivePlaybackEndPending = FALSE;
	m_postgameChat = FALSE;
	m_watchFaultReported = FALSE;
	m_passiveFramePrepared = FALSE;
	m_passiveFrameValid = FALSE;
	m_passiveFrameLength = 0;
	m_watchHostIp[0] = '\0';
	m_queryHostIp[0] = '\0';
	m_lobbyLines.clear();
	m_session.reset();
}

WatchExitAction Caster::finishWatch(WatchExitReason reason)
{
	const WatchExitAction action = decideWatchExit(reason, m_playbackEntered);

	switch (action)
	{
	case WATCH_ACTION_LOBBY:
	case WATCH_ACTION_POPUP_LOBBY:
		unsubscribe();
		break;
	case WATCH_ACTION_POSTGAME:
		m_session.beginEnding();
		// Make the caster inactive to menu/UI callers immediately. update()
		// performs the transport and core teardown after the recorder callback
		// that ended the match has unwound.
		m_playbackEntered = FALSE;
		m_passivePlaybackEndPending = TRUE;
		break;
	case WATCH_ACTION_STAY:
	default:
		if (reason == WATCH_EXIT_MAP_UNAVAILABLE)
		{
			m_session.returnToRoom();
		}
		break;
	}

	// A user Back already runs inside the room's own shutdown; the UI is leaving.
	if (reason != WATCH_EXIT_USER_BACKED_OUT)
	{
		OnLiveCasterWatchExit(reason, action);
	}
	return action;
}

Bool Caster::watchHost(const char* hostIp)
{
	if (m_client == nullptr || !m_core.isCaster() || hostIp == NULL || hostIp[0] == '\0')
	{
		return FALSE;
	}
	if (strlen(hostIp) >= sizeof(m_watchHostIp))
	{
		return FALSE;
	}

	unsubscribe();
	clearWatchedSlotColors();
	strcpy(m_watchHostIp, hostIp);
	strcpy(m_queryHostIp, hostIp);
	m_queryIpCount = 0;
	m_lastPlayerQueryMs = 0;
	m_playerQueryAttempts = 0;
	m_lastDiscoveryRequestMs = timeGetTime();
	m_discoveryRequestAttempts = 1;
	RequestCasterHostDetails(m_watchHostIp);
	return TRUE;
}

Bool Caster::popLobbyLine(CasterLobby::LineQueue::Line& out)
{
	return m_lobbyLines.pop(out);
}

static const UnsignedInt CASTER_LOBBY_STALE_MS = 3500;

CasterLobby::LobbyStatus Caster::lobbyStatus() const
{
	CasterLobby::LobbySubscription subscription = CasterLobby::LOBBY_SUB_NONE;

	if (m_client != nullptr)
	{
		CasterTransportCore::SourceSetSnapshot snapshot;
		m_client->snapshot(snapshot);
		subscription = snapshot.subscription;
	}
	return CasterLobby::classifyLobbyFeed(m_core.isWatching(), subscription,
		m_core.lobbyStatus(timeGetTime(), CASTER_LOBBY_STALE_MS));
}

const CasterLobby::LobbyState& Caster::lobbyState() const
{
	return m_core.lobbyState();
}

void Caster::clearWatchedSlotColors()
{
	UnsignedInt i;

	for (i = 0; i < CasterLobby::LOBBY_MAX_SLOTS; ++i)
	{
		m_watchedSlotColor[i] = -1;
	}
	m_haveWatchedSlotColors = FALSE;
}

void Caster::applyCasterChatColor(ChatMessage& chat)
{
	CasterBootstrap::MatchStart start;
	UnsignedInt i;
	Color color = 0;

	if (!m_core.isCaster() || chat.isCaster
		|| chat.senderSlot >= CasterLobby::LOBBY_MAX_SLOTS)
	{
		return;
	}

	CasterBootstrap::clear(start);
	if (m_client != nullptr && m_client->bootstrap(start)
		&& start.room.slotCount <= CasterLobby::LOBBY_MAX_SLOTS
		&& (!m_core.isWatching() || start.gameUid == m_core.watchedGameUid()))
	{
		for (i = 0; i < start.room.slotCount; ++i)
		{
			m_watchedSlotColor[i] = start.room.slots[i].color;
		}
		m_haveWatchedSlotColors = TRUE;
	}
	else if (m_core.hasLobbyState() && m_core.lobbyState().valid
		&& m_core.lobbyState().slotCount <= CasterLobby::LOBBY_MAX_SLOTS)
	{
		const CasterLobby::LobbyState& state = m_core.lobbyState();
		for (i = 0; i < state.slotCount; ++i)
		{
			m_watchedSlotColor[i] = state.slots[i].color;
		}
		m_haveWatchedSlotColors = TRUE;
	}
	else if (m_session.hasGame()
		&& (!m_core.isWatching() || m_session.gameKey().uid == m_core.watchedGameUid()))
	{
		// Last resort: the LAN browser still lists the watched game, so its slot
		// colours are authoritative even after the bootstrap and the lobby
		// snapshot are gone.
		Int browserColor[CasterLobby::LOBBY_MAX_SLOTS];
		UnsignedInt browserCount = 0;

		if (GetCasterGameSlotColors(m_session.gameKey().hostIP, m_session.gameKey().seed,
			browserColor, CasterLobby::LOBBY_MAX_SLOTS, browserCount))
		{
			if (browserCount > CasterLobby::LOBBY_MAX_SLOTS)
			{
				browserCount = CasterLobby::LOBBY_MAX_SLOTS;
			}
			for (i = 0; i < browserCount; ++i)
			{
				m_watchedSlotColor[i] = browserColor[i];
			}
			m_haveWatchedSlotColors = TRUE;
		}
	}

	if (m_haveWatchedSlotColors && m_watchedSlotColor[chat.senderSlot] >= 0
		&& ChatColorFromIndex(m_watchedSlotColor[chat.senderSlot], color))
	{
		chat.hasColor = TRUE;
		chat.color = color;
	}
}

Bool Caster::isWatching() const
{
	return m_core.isWatching();
}

UnsignedInt Caster::watchedGameUid() const
{
	return m_core.watchedGameUid();
}

Bool Caster::playbackEntered() const
{
	return m_playbackEntered;
}

Bool Caster::enterPlaybackIfReady()
{
	CasterBootstrap::MatchStart start;
	ReplayStartData startData;

	CasterBootstrap::clear(start);
	if (m_playbackEntered)
	{
		return TRUE;
	}
	if (m_client == nullptr || !m_core.isWatching() || TheRecorder == nullptr)
	{
		return FALSE;
	}
	if (!m_client->bootstrap(start) || start.gameUid != m_core.watchedGameUid())
	{
		return FALSE;
	}

	// The playback itself starts from typed state, with no file behind it.
	if (!CasterBootstrap::fillReplayStart(start, startData))
	{
		return FALSE;
	}

	// Header metadata follows the established replay initialization path; command
	// bodies are injected as validated native command batches. The optional local
	// copy of the watched match is an ordinary replay file, and the recorder writes
	// its header from this same start data, through the code that writes the header
	// of a recorded game.
	m_session.beginStarting();
	m_session.beginLoading();
	TheGameLogic->setCommandFrameSource(this);
	if (!TheRecorder->startCasterPlayback(startData))
	{
		TheGameLogic->setCommandFrameSource(nullptr);
		finishWatch(WATCH_EXIT_MAP_UNAVAILABLE);
		return FALSE;
	}
	m_playbackEntered = TRUE;
	return TRUE;
}


Bool Caster::isQueryTarget(const char* ip) const
{
	UnsignedInt i;
	char ipText[16];

	if (ip == nullptr)
	{
		return FALSE;
	}
	if (m_watchHostIp[0] != '\0' && strcmp(ip, m_watchHostIp) == 0)
	{
		return TRUE;
	}
	for (i = 0; i < m_queryIpCount; ++i)
	{
		formatIp(m_queryIps[i], ipText);
		if (strcmp(ip, ipText) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void Caster::onIncompatibleSource(const char* ip)
{
	// Only the probed host decides whether a room can open at all. An incompatible
	// peer is ignored; a live watch reports it through the transport instead.
	if (!m_core.isCaster() || m_core.isWatching() || m_watchHostIp[0] == '\0'
		|| ip == nullptr || strcmp(ip, m_watchHostIp) != 0)
	{
		return;
	}
	m_watchHostIp[0] = '\0';
	m_queryHostIp[0] = '\0';
	finishWatch(WATCH_EXIT_PROTOCOL_INCOMPATIBLE);
}

Bool Caster::onBeacon(UnsignedInt gameUid, const char* uid, const char* ip, UnsignedShort tcpPort,
	const char* gameName, Bool isHost)
{
	UnsignedInt sourceId = 0;
	CasterBeacon::CastResult result;

	if (m_client == nullptr || gameUid == 0 || uid == nullptr || ip == nullptr)
	{
		return FALSE;
	}
	if (!m_core.isWatching() && !isQueryTarget(ip))
	{
		// Before a watch exists only answers to our own direct queries count;
		// nothing else may start a watch.
		return FALSE;
	}

	result = m_beaconSources.cast(gameUid, uid, ip, tcpPort, isHost, sourceId);
	if (result == CasterBeacon::SOURCE_REJECTED || result == CasterBeacon::SOURCE_TABLE_FULL)
	{
		return FALSE;
	}

	if (!m_core.isWatching())
	{
		// LAN peer detail replies may beat the selected host reply. cast()
		// deliberately retained this UID/IP membership; when the host establishes the watched UID,
		// linkDiscoveredSources() attaches every retained matching peer.
		if (m_watchHostIp[0] == '\0' || strcmp(ip, m_watchHostIp) != 0)
		{
			return TRUE;
		}
		beginWatch(gameUid, gameName);
		linkDiscoveredSources();
		return TRUE;
	}

	if (gameUid != m_core.watchedGameUid())
	{
		return FALSE;
	}

	if (result == CasterBeacon::SOURCE_ADDED)
	{
		return m_client->addSource(sourceId, ip, tcpPort, isHost);
	}
	return (result == CasterBeacon::SOURCE_REFRESHED) ? TRUE : FALSE;
}

const char* Caster::casterUid() const
{
	return m_casterUid;
}

void Caster::setCasterUid(const char* uid)
{

	m_casterUid[0] = '\0';
	CasterBeacon::normalizeUid(uid, m_casterUid, sizeof(m_casterUid));
}

void Caster::setCasterChatName(const char* utf8Name)
{
	m_casterChatName[0] = '\0';
	if (utf8Name != NULL)
		strlcpy(m_casterChatName, utf8Name, sizeof(m_casterChatName));
}

Bool Caster::sendCasterChat(UnsignedByte direction, UnsignedByte senderSlot,
	UnsignedByte senderTeam, const char* senderName, UnsignedInt senderNameLen,
	const char* utf8Text, UnsignedInt utf8Len, Bool isEmote)
{
	UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt frameLen;
	UnsignedInt senderIdentity = CasterChat::identityFromUid(m_casterUid);
	const char* effectiveName;
	UnsignedInt effectiveNameLen;

	effectiveName = (senderName != NULL && senderNameLen != 0) ? senderName : m_casterChatName;
	effectiveNameLen = (senderName != NULL && senderNameLen != 0) ? senderNameLen : (UnsignedInt)strlen(m_casterChatName);

	if (m_client == nullptr)
	{
		return FALSE;
	}

	frameLen = m_core.encodeCasterChat(frame, sizeof(frame), ++m_casterChatId, direction,
		senderSlot, senderTeam, senderIdentity, effectiveName, effectiveNameLen, utf8Text, utf8Len,
		isEmote);
	if (frameLen == 0)
	{
		return FALSE;
	}

	m_client->broadcast(frame, frameLen);

	if (m_core.isWatching())
	{
		CasterInbound event;
		CasterCore::makeChatEvent(event, m_core.watchedGameUid(), m_casterChatId,
			direction, senderSlot, (UnsignedByte)1, senderTeam, senderIdentity, effectiveName, effectiveNameLen,
			utf8Text, utf8Len, isEmote);
		pushInbound(event);
	}
	return TRUE;
}

Bool Caster::sendPlayerChat(UnsignedInt playerID, UnsignedInt commandID, UnsignedInt playerMask,
	UnsignedInt activePlayerMask, UnsignedByte senderTeam, const char* senderName,
	UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len)
{
	return m_core.capturePlayerChat(playerID, commandID, playerMask, activePlayerMask,
		senderTeam, senderName, senderNameLen, utf8Text, utf8Len);
}

void Caster::setPrivacyFilter(Bool enabled)
{
	m_core.setPrivacyFilter(enabled);
}

void Caster::broadcastChat(const UnsignedByte* frame, UnsignedInt length)
{

	if (m_server != nullptr)
	{
		m_server->broadcastChatFrame(frame, length, CasterTransport::INVALID_INDEX);
	}
}

//-----------------------------------------------------------------------------
// CasterTransportCore::ChatSink (worker thread)
//-----------------------------------------------------------------------------

void Caster::onChat(UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction,
	UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam,
	UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen,
	const char* text, UnsignedInt textLen, UnsignedInt senderLinkId, UnsignedByte isEmote)
{

	EnterCriticalSection(&m_queueLock);
	m_core.receiveChat(gameUid, commandID, direction, senderSlot, isSenderCaster, senderTeam,
		senderIdentity, senderName, senderNameLen, text, textLen, senderLinkId, isEmote);
	LeaveCriticalSection(&m_queueLock);
}

void Caster::onLobbyState(UnsignedInt gameUid, const char* text, UnsignedInt textLen,
	UnsignedInt senderLinkId)
{
	(void)senderLinkId;

	// Same rule as onChat: the worker never parses or touches engine state. The
	// parse happens here, under the queue lock, because the mirrored state is
	// read by the main thread; `receiveLobbyState` is pure and bounded.
	EnterCriticalSection(&m_queueLock);
	m_core.receiveLobbyState(gameUid, text, textLen, timeGetTime());
	LeaveCriticalSection(&m_queueLock);
}

// The `CasterUid` key defaults to the machine's short hostname. The detail-reply
// token is ASCII with no separator, so keep the alphanumeric prefix and stop at
// the first byte that cannot be encoded.
static void deriveDefaultCasterUid(char* out)
{
	char computerName[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD size = sizeof(computerName);
	UnsignedInt i = 0;

	out[0] = '\0';
	if (!GetComputerNameA(computerName, &size))
	{
		return;
	}
	while (computerName[i] != '\0' && i < CasterBeacon::MAX_UID_CHARS)
	{
		char c = computerName[i];
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
		{
			out[i] = c;
		}
		else
		{
			break;
		}
		++i;
	}
	out[i] = '\0';
}

Bool CasterEnable(CasterRole role)
{
	OptionPreferences prefs;
	Caster* caster;
	AsciiString uid;
	char defaultUid[CasterBeacon::MAX_UID_CHARS + 1];
	UnsignedShort tcpPort;

	if (!prefs.getLiveCastingEnabled() || role == CASTER_ROLE_NONE)
	{

		CasterDisable();
		return FALSE;
	}
	if (TheCaster != nullptr)
	{

		if (TheCaster->role() == role)
		{
			return TRUE;
		}
		CasterDisable();
	}

	caster = new Caster();
	if (caster == nullptr)
	{
		return FALSE;
	}

	caster->setPrivacyFilter(prefs.getCasterPrivacyFilter());

	uid = prefs.getCasterUid();
	if (uid.isEmpty())
	{
		deriveDefaultCasterUid(defaultUid);
		uid = defaultUid;
	}
	caster->setCasterUid(uid.str());
	if (caster->casterUid()[0] == '\0')
	{

		DEBUG_LOG(("CasterEnable - no encodable caster uid; capability replies are disabled"));
	}

	tcpPort = prefs.getCasterTCPPort();
	if (role == CASTER_ROLE_CASTER)
	{
		if (!caster->enableCaster(tcpPort))
		{
			delete caster;
			return FALSE;
		}
	}
	else if (!caster->enablePlayer(tcpPort))
	{
		delete caster;
		return FALSE;
	}

	TheCaster = caster;
	return TRUE;
}

void CasterDisable()
{
	if (TheCaster == nullptr)
	{
		return;
	}
	delete TheCaster;
	TheCaster = nullptr;
}

Bool SendCasterLobbyChatLine(UnicodeString input, UnicodeString senderName, Bool echoLocally)
{
	char utf8[CasterProtocol::MAX_FRAME_BYTES];
	char senderUtf8[65];
	UnsignedInt utf8Len;
	UnsignedInt senderUtf8Len;
	Bool isEmote = FALSE;

	if (TheCaster == nullptr)
	{
		return FALSE;
	}

	// TheSuperHackers @feature arcticdolphin 08/08/2026 Recognizes the stock
	// "/me " emote convention the same way LANAPI::RequestPlayerChat does.
	if (input.startsWithNoCase(L"/me "))
	{
		input = UnicodeString(input.str() + 4);
		isEmote = TRUE;
	}

	// wideToUtf8 never terminates its output, and both setCasterChatName and
	// PostCasterLocalLine read C strings: reserve the terminator and write it.
	utf8Len = CasterProtocol::wideToUtf8(input.str(), (UnsignedInt)input.getLength(),
		utf8, sizeof(utf8) - 1);
	senderUtf8Len = CasterProtocol::wideToUtf8(senderName.str(),
		(UnsignedInt)senderName.getLength(), senderUtf8, sizeof(senderUtf8) - 1);
	utf8[utf8Len] = '\0';
	senderUtf8[senderUtf8Len] = '\0';

	TheCaster->setCasterChatName(senderUtf8);
	if (utf8Len == 0)
	{
		return FALSE;
	}
	if (!TheCaster->sendCasterChat((UnsignedByte)CasterProtocol::CHAT_LOBBY, 0xFF, 0,
		senderUtf8, senderUtf8Len, utf8, utf8Len, isEmote))
	{
		return FALSE;
	}

	// While a watch is live the send echoes back through the lobby line queue;
	// otherwise the caller's surface needs the local echo to see its own line.
	if (echoLocally && !TheCaster->isWatching())
	{
		PostCasterLocalLine(utf8, senderUtf8, 0xFF, TRUE, isEmote);
	}
	return TRUE;
}
