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

// Socket and worker-thread ownership for caster connections.
// Workers never access engine or UI state. Main-thread sends enqueue bounded frames;
// socket I/O happens outside the bookkeeping lock.

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Caster/CasterTransportCore.h"
#include "GameNetwork/Caster/CasterFrameHistory.h"
#include "GameNetwork/Caster/CasterSourceSet.h"

#include <winsock2.h>
#include <windows.h>
#include <process.h>

namespace CasterTransport
{

using CasterTransportCore::HostServices;

/// Connection / source link states. `volatile LONG` so the two threads can
/// observe each other's transitions without a lock for the fast checks.
enum LinkState
{
	LINK_FREE    = 0,
	LINK_ACTIVE  = 1,
	LINK_CLOSING = 2,	///< the worker is exiting; the reaper owns the cleanup
};

static const UnsignedInt INVALID_INDEX = 0xFFFFFFFF;

static const UnsignedInt INBOX_BYTES = 4096;

class Server
{
public:
	Server();
	~Server();

	Bool start(HostServices* host, UnsignedShort port);
	void stop();
	Bool isRunning() const;
	UnsignedShort port() const;

	/// Stores and broadcasts one complete native command frame without replay parsing.
	Bool broadcastCommandFrame(UnsignedInt gameUid, UnsignedInt simulationFrame,
		const UnsignedByte* data, UnsignedInt length);
	void broadcastCommandEnd(UnsignedInt gameUid, Bool hasFrames, UnsignedInt finalSimulationFrame);
	void setCommandHistory(CasterFrameHistory* history);

	void broadcastChatFrame(const UnsignedByte* frame, UnsignedInt length, UnsignedInt excludeLinkId);

	void broadcastLobbyFrame(UnsignedInt gameUid, const UnsignedByte* frame, UnsignedInt length);
	Bool takeLobbySnapshotRequest(UnsignedInt gameUid);

	/// Sends the typed match-start bootstrap to every streaming-ready subscriber.
	void broadcastBootstrapFrame(UnsignedInt gameUid, const UnsignedByte* frame, UnsignedInt length);
	/// TRUE once when a subscriber became streaming-ready and still needs it.
	Bool takeBootstrapRequest(UnsignedInt gameUid);

	void refreshGameMetadata(UnsignedInt gameUid);

	void setChatSink(CasterTransportCore::ChatSink* sink);

	/// Main thread. O(1) gate probe; the lock is held for the lookup only.
	Bool isSubscribed(UnsignedInt gameUid);

private:
	friend unsigned __stdcall casterServerAcceptProc(void* param);
	friend unsigned __stdcall casterServerWorkerProc(void* param);

	struct Connection
	{
		volatile LONG state;
		SOCKET socket;
		CasterTransportCore::OutboundQueue queue;
		CasterTransportCore::ServerConnection session;
		UnsignedInt deliveredCommandEndGameUid;
		UnsignedByte inbox[INBOX_BYTES];
		UnsignedInt inboxSize;
	};

	struct WorkerParam
	{
		Server* server;
		UnsignedInt index;
	};

	Bool createListenSocket(UnsignedShort port);
	Bool spawnWorker(UnsignedInt index);
	void acceptLoop();
	void workerLoop(UnsignedInt index);
	void serveCommandHistory(UnsignedInt index, const CasterProtocol::Frame& frame);
	void retryCommandEnd(UnsignedInt index);
	void reapFinished();
	void freeSlot(UnsignedInt index);

	HostServices* m_host;
	CasterTransportCore::ChatSink* m_chatSink;
	CasterFrameHistory* m_commandHistory;
	CasterSubscribeTable::Table m_table;
	Bool m_lobbySnapshotPending;
	Bool m_bootstrapPending;
	Bool m_commandEndPending;
	UnsignedInt m_commandEndGameUid;
	Bool m_commandEndHasFrames;
	UnsignedInt m_commandEndFinalFrame;
	CRITICAL_SECTION m_lock;
	CRITICAL_SECTION m_historyLock;
	SOCKET m_listen;
	UnsignedShort m_port;
	HANDLE m_acceptThread;
	HANDLE m_threads[CasterTransportCore::MAX_CONNECTIONS];
	volatile LONG m_running;
	Bool m_wsaStarted;
	Connection m_connections[CasterTransportCore::MAX_CONNECTIONS];

	Server(const Server&);
	Server& operator=(const Server&);
};

class Client
{
public:
	Client();
	~Client();

	/// Clears all state for a new watched game. Main thread, no sources live.
	void reset(UnsignedInt gameUid);

	Bool addSource(UnsignedInt sourceId, const char* ip, UnsignedShort port, Bool isHost);

	/// Main thread. Stops and removes one source link.
	Bool removeSource(UnsignedInt sourceId);

	/// Main thread. Stops every worker and releases the critical section.
	void stop();

	void broadcast(const UnsignedByte* frame, UnsignedInt length);

	void setChatSink(CasterTransportCore::ChatSink* sink);

	Bool isRunning() const;

	CasterLobby::LobbySubscription subscriptionState();

	/// Main thread. Advances the single-flight REQ timer (milliseconds).
	/// Returns TRUE when a REQ frame was emitted.
	Bool update(UnsignedInt elapsedMs);

	// --- snapshot accessors (main thread; the lock is held for the read) -----

	Bool sourceState(UnsignedInt sourceId, CasterTransportCore::ClientSource& out);
	Bool nativeFrameAvailable(UnsignedInt simulationFrame);
	Bool nativeSourcesProgressThrough(UnsignedInt simulationFrame);
	UnsignedInt copyNativeFrame(UnsignedInt simulationFrame, UnsignedByte* dest, UnsignedInt capacity);
	/// Marks one main-thread decoded frame consumed and requests the next bounded history window.
	Bool consumeNativeFrame(UnsignedInt simulationFrame);
	UnsignedInt nextNativeFrame();
	Bool nativeProgress(UnsignedInt& firstSimulationFrame, UnsignedInt& lastSimulationFrame, UnsignedInt& frameCount);
	Bool nativeEnd(Bool& hasFrames, UnsignedInt& finalSimulationFrame);
	Bool nativeUnavailable();
	/// Every reachable source rejected our protocol version.
	Bool protocolIncompatible();
	/// Every source was quarantined for contradicting another one.
	Bool transportDisagreement();

	/// Copies the reconciled typed match-start bootstrap; FALSE until one arrived.
	Bool bootstrap(CasterBootstrap::MatchStart& out);
	/// One consistent read of the source set for the UI (single lock hold).
	void snapshot(CasterTransportCore::SourceSetSnapshot& out);

private:
	friend unsigned __stdcall casterClientWorkerProc(void* param);

	struct SourceLink
	{
		volatile LONG state;
		UnsignedInt id;
		Bool isHost;
		UnsignedInt ip;
		UnsignedShort port;
		SOCKET socket;
		CasterTransportCore::OutboundQueue queue;
		CasterTransportCore::OutboundQueue chatQueue;
		UnsignedByte inbox[INBOX_BYTES];
		UnsignedInt inboxSize;
		CasterTransportCore::ReconnectPolicy reconnect;
		Bool rejected;
		Bool corePresent;
	};

	struct WorkerParam
	{
		Client* client;
		UnsignedInt index;
	};

	/// Routes a core-emitted frame to a source's bounded queue. The caller
	/// already holds `m_lock` (it is called from `CasterSourceSet`).
	class Router : public CasterTransportCore::FrameSink
	{
	public:
		Router();
		void bind(Client* client);
		virtual Bool emit(UnsignedInt targetId, const UnsignedByte* frame, UnsignedInt length);
	private:
		Client* m_client;
	};

	friend class Router;

	SourceLink* findLink(UnsignedInt sourceId);
	SourceLink* findFreeLink();
	Bool spawnWorker(UnsignedInt index);
	void workerLoop(UnsignedInt index);
	void freeLink(UnsignedInt index);
	void reapFinished();

	CRITICAL_SECTION m_lock;
	CasterTransportCore::CasterSourceSet m_core;
	CasterTransportCore::ChatSink* m_chatSink;
	Router m_router;
	HANDLE m_threads[CasterTransportCore::MAX_CLIENT_SOURCES];
	volatile LONG m_running;
	Bool m_wsaStarted;
	SourceLink m_links[CasterTransportCore::MAX_CLIENT_SOURCES];

	Client(const Client&);
	Client& operator=(const Client&);
};

}
