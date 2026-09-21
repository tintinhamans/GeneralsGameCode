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

#pragma once

#include <windows.h>

#include "Lib/BaseType.h"
#include "Common/UnicodeString.h"
#include "GameNetwork/Caster/CommandFrameSource.h"
#include "GameNetwork/Caster/CasterBeacon.h"
#include "GameNetwork/Caster/CasterBootstrap.h"
#include "GameNetwork/Caster/CasterCore.h"
#include "GameNetwork/Caster/CasterReplayFile.h"
#include "GameNetwork/Caster/LiveCasterSession.h"
#include "GameNetwork/Caster/CasterLobby.h"

class CommandList;
struct ChatMessage;
class GameMessage;
class CasterFrameHistory;

namespace CasterTransport
{
	class Server;
	class Client;
}

// Main-thread facade; transport workers must not access engine or UI state.
class Caster : public CasterBroadcaster,
	public CasterTransportCore::ChatSink, public CommandFrameSource
{
public:
	Caster();
	virtual ~Caster();

	// --- lifecycle (main thread) -------------------------------------------

	Bool enablePlayer(UnsignedShort tcpPort);

	Bool enableCaster(UnsignedShort tcpPort);

	void shutdown();

	CasterRole role() const;
	Bool isPlayer() const;
	Bool isCaster() const;

	void update();
	struct Telemetry
	{
		Bool reported;
		Bool available;
		Bool stale;
		UnsignedInt delayFrames;
		UnsignedInt reportAgeMs;
		UnsignedInt sources;
	};
	Bool getTelemetry(Telemetry& telemetry) const;

	// --- recorder seams (main thread) --------------------------------------

	void onRecordingStarted(CasterReplayFile* recordingFile);
	/// Publishes the final, transformed command list for one player simulation
	/// frame. Empty frames are intentional and are sent as empty batches.
	void publishCommandFrame(UnsignedInt frame, GameMessage* first);
	/// Decodes a reconciled native batch into the recorder's command list during
	/// cast playback. FALSE means the target frame is not ready or the batch was
	/// malformed.
	Bool appendPassiveCommandFrame(UnsignedInt frame, CommandList* commands);

	// --- CommandFrameSource (main thread) ----------------------------------

	virtual Bool isFrameReady(UnsignedInt frame);
	virtual Bool appendFrame(UnsignedInt frame, CommandList* commands);
	virtual Bool hasEnded();
	virtual Bool needsCatchUp(UnsignedInt frame);

	/// Records the end of cast playback; update() performs the teardown safely.
	void onPassivePlaybackEnded();

	void onRecorderClosed();

	/// Tears down the chat-only postgame watch (score screen closed).
	void endPostgameChat();

	Bool announceCurrentGame();

	/// Publishes the typed match-start bootstrap for the announced game. Called
	/// once the recorder wrote its header, so the slots are final.
	Bool publishMatchBootstrap(Int difficulty, Int originalGameMode, Int rankPoints, Int maxFPS);

	Bool announceLobbyGame();

	// --- player surface (main thread) --------------------------------------

	/// `matchRunning` is FALSE for the lobby announcement and TRUE once the
	/// recorder started, which is the caster's streaming gate.
	Bool announceGame(UnsignedInt gameUid, const char* gameName, Bool matchRunning);
	void clearAnnouncedGame();
	Bool isGameAnnounced() const;

	UnsignedInt announcedGameUid() const;

	UnsignedShort serverPort() const;
	Bool isSubscribed(UnsignedInt gameUid);

	Bool sendLobbyChat(const char* utf8Text, UnsignedInt utf8Len, const char* senderName,
		UnsignedInt senderNameLen, UnsignedByte senderSlot, UnsignedByte senderTeam,
		UnsignedInt senderIdentity, Bool isEmote);

	Bool forwardLobbyState();

	/// Re-sends the cached match-start bootstrap to a subscriber that just
	/// became streaming-ready. Main thread.
	Bool forwardMatchBootstrap();

	// --- caster surface (main thread) ------------------------------------

	/// Abandons a watch that never opened (failed room open) and tears it down.
	/// Every real exit goes through finishWatch() instead.
	void unsubscribe();
	/// The single exit for a live watch. Tears the watch down when the policy
	/// (decideWatchExit) says so, notifies the UI through OnLiveCasterWatchExit
	/// and returns the chosen action. Main thread only.
	WatchExitAction finishWatch(WatchExitReason reason);
	Bool selectGame(const LiveCasterGameKey& key);
	Bool hasSelectedGame() const;
	Bool isSelectedGame(const LiveCasterGameKey& key) const;
	Bool isWatching() const;
	UnsignedInt watchedGameUid() const;

	Bool watchHost(const char* hostIp);

	Bool popLobbyLine(CasterLobby::LineQueue::Line& out);

	CasterLobby::LobbyStatus lobbyStatus() const;

	/// Main thread. The latest parsed lobby snapshot for the watched game. Only
	/// meaningful when `lobbyStatus()` is `LOBBY_STATUS_OK`; it is cleared on a
	/// new watch or an unsubscribe, so it never holds another game's state.
	const CasterLobby::LobbyState& lobbyState() const;

	/// Main thread. Caster only: resolves the watched room's colour for a
	/// player sender (chat.senderSlot) into chat.color / chat.hasColor from the
	/// match bootstrap or the lobby snapshot, remembering the slot colours so
	/// the score screen still resolves them after the watch ends. Leaves the
	/// message untouched for casters, non-caster instances or unknown slots.
	void applyCasterChatColor(ChatMessage& chat);

	Bool enterPlaybackIfReady();

	Bool playbackEntered() const;

	Bool onBeacon(UnsignedInt gameUid, const char* uid, const char* ip, UnsignedShort tcpPort,
		const char* gameName, Bool isHost);
	/// A details reply from the probed host carried another protocol version: the
	/// watch can never open, so leave through the protocol-incompatible exit.
	void onIncompatibleSource(const char* ip);

	const char* casterUid() const;
	void setCasterUid(const char* uid);
	void setCasterChatName(const char* utf8Name);

	Bool sendCasterChat(UnsignedByte direction, UnsignedByte senderSlot,
		UnsignedByte senderTeam, const char* senderName, UnsignedInt senderNameLen,
		const char* utf8Text, UnsignedInt utf8Len, Bool isEmote);

	Bool sendPlayerChat(UnsignedInt playerID, UnsignedInt commandID, UnsignedInt playerMask,
		UnsignedInt activePlayerMask, UnsignedByte senderTeam, const char* senderName,
		UnsignedInt senderNameLen, const char* utf8Text, UnsignedInt utf8Len);

	void setPrivacyFilter(Bool enabled);

	// --- inbound queue (any thread; guarded internally) --------------------

	/// Pushes one event for the main-thread drain. Never blocks; a full queue
	/// drops the event.
	Bool pushInbound(const CasterInbound& event);

	virtual void broadcastChat(const UnsignedByte* frame, UnsignedInt length);

	// --- CasterTransportCore::ChatSink (worker thread) ------------------

	/// A received link CHAT frame: queue it for the main-thread drain. Never
	/// touches the engine or the UI; the drain does the rendering and relay.
	virtual void onChat(UnsignedInt gameUid, UnsignedInt commandID, UnsignedByte direction,
		UnsignedByte senderSlot, UnsignedByte isSenderCaster, UnsignedByte senderTeam,
		UnsignedInt senderIdentity, const char* senderName, UnsignedInt senderNameLen,
		const char* text, UnsignedInt textLen, UnsignedInt senderLinkId, UnsignedByte isEmote);

	virtual void onLobbyState(UnsignedInt gameUid, const char* text, UnsignedInt textLen,
		UnsignedInt senderLinkId);

private:
	Bool pushInboundLocked(const CasterInbound& event);
	void drainInbound();

	void handleInboundChat(const CasterInbound& event);
	void displayChatLine(const CasterInbound& event);
	void relayChatLine(const CasterInbound& event);

	void beginWatch(UnsignedInt gameUid, const char* gameName);

	Bool linkDiscoveredSources();

	void updatePlayerQueries();
	/// TRUE for the probed host and every address in the cached player table.
	Bool isQueryTarget(const char* ip) const;

	CasterCore m_core;
	LiveCasterSession m_session;
	CasterTransport::Server* m_server;
	CasterTransport::Client* m_client;
	Bool m_haveClientUpdateTime;
	UnsignedInt m_lastClientUpdateMs;

	CasterReplayFile* m_recordingFile;
	CasterFrameHistory* m_commandHistory;
	UnsignedInt m_lastCommandFrame;
	Bool m_hasCommandFrames;
	UnsignedByte m_passiveFrameBytes[64 * 1024];
	/// Main-thread-only scratch for encoding one outbound command frame, so a
	/// 64 KiB buffer never lands on the caller's stack.
	UnsignedByte m_commandFrameBytes[64 * 1024];
	UnsignedInt m_passiveFrame;
	UnsignedInt m_passiveFrameLength;
	Bool m_passiveFramePrepared;
	Bool m_passiveFrameValid;
	UnsignedInt m_casterChatId;
	UnsignedInt m_lobbyChatId;
	Bool m_playbackEntered;
	Bool m_passivePlaybackEndPending;
	Bool m_postgameChat;
	Bool m_watchFaultReported;

	CasterBeacon::SourceTable m_beaconSources;

	Char m_watchHostIp[16];
	UnsignedInt m_lastDiscoveryRequestMs;
	UnsignedInt m_discoveryRequestAttempts;

	/// Occupied player addresses from the LAN slot table, cached for the session
	/// and queried directly until each has answered with its capabilities.
	Char m_queryHostIp[16];
	UnsignedInt m_queryIps[CasterBeacon::MAX_SOURCES];
	UnsignedInt m_queryIpCount;
	UnsignedInt m_lastPlayerQueryMs;
	UnsignedInt m_playerQueryAttempts;

	/// Lobby chat lines drained for the read-only lobby view. Main thread only.
	CasterLobby::LineQueue m_lobbyLines;

	Char m_casterUid[CasterBeacon::MAX_UID_CHARS + 1];
	Char m_casterChatName[65];

	/// Slot colour indices of the watched room (-1 unknown); caster only.
	Int m_watchedSlotColor[CasterLobby::LOBBY_MAX_SLOTS];
	Bool m_haveWatchedSlotColors;
	void clearWatchedSlotColors();

	/// The last lobby blob forwarded to subscribers and when it went, so an
	/// unchanged state is sent at most once per period while a changed one goes
	/// at once (CasterLobby::shouldForwardLobby). Main thread only.
	Char m_lastLobbyText[CasterProtocol::MAX_LOBBY_TEXT_BYTES + 1];
	UnsignedInt m_lastLobbyLen;
	UnsignedInt m_lastLobbyMs;
	Bool m_haveLastLobby;

	/// The typed match start this player publishes, and its encoded frame, kept
	/// so a late subscriber can be served without rebuilding it. Main thread only.
	CasterBootstrap::MatchStart m_matchBootstrap;
	UnsignedByte m_matchBootstrapFrame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt m_matchBootstrapFrameLen;
	UnsignedInt m_lastMatchBootstrapMs;
	Bool m_haveMatchBootstrap;

	/// Guards the core's bounded inbound queue only. Never held across a
	/// socket call, a sleep, or any engine call.
	CRITICAL_SECTION m_queueLock;

	Caster(const Caster&);
	Caster& operator=(const Caster&);
};

extern Caster* TheCaster;

Bool CasterEnable(CasterRole role);

void CasterDisable();

/// Implemented by the LAN lobby menu: performs the UI part of a watch exit
/// (return to the LAN lobby, popup). Called after the watch was torn down.
void OnLiveCasterWatchExit(WatchExitReason reason, WatchExitAction action);

Bool GetCasterGameName(char* out, UnsignedInt cap);

Bool GetCasterGameSeed(UnsignedInt& hostIP, UnsignedInt& seed);

Bool GetCasterLobbyState(char* out, UnsignedInt cap, UnsignedInt& lenOut);

/// The current LAN game's GameInfo options string, for any player in the game
/// (the host-only mirror is GetCasterLobbyState).
Bool GetCasterMatchOptions(char* out, UnsignedInt cap, UnsignedInt& lenOut);

Bool RequestCasterHostDetails(const char* hostIp);
Bool GetCasterGamePlayerIPs(const char* hostIp, UnsignedInt* out, UnsignedInt cap, UnsignedInt& count);

/// The LAN browser's slot colours for the game identified by `hostIP`/`seed`.
/// The caster's last-resort colour source when neither the match bootstrap nor
/// a lobby snapshot is available. Unoccupied or unknown slots are written as -1.
Bool GetCasterGameSlotColors(UnsignedInt hostIP, UnsignedInt seed, Int* out, UnsignedInt cap,
	UnsignedInt& count);

CasterLobby::ChatSurface PostCasterLocalLine(const char* ascii, const char* senderName,
	UnsignedByte senderSlot, Bool senderIsCaster, Bool isEmote);

/// The one caster-side chat send used by every LAN caster surface: strips a
/// leading stock "/me " emote prefix, converts the line and sender name to
/// UTF-8, remembers the sender name and sends one CHAT_LOBBY frame. When
/// `echoLocally` is TRUE and no watch is live the line is also posted locally,
/// because the sender's own feed is not running then. FALSE when nothing was
/// sent. Main thread; the caller owns the surface guards.
Bool SendCasterLobbyChatLine(UnicodeString input, UnicodeString senderName, Bool echoLocally);
