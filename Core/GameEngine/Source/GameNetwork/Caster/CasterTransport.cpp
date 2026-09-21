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

#include "GameNetwork/Caster/CasterTransport.h"
#include "GameNetwork/Caster/CasterCommandFrame.h"

namespace CasterTransport
{

using CasterProtocol::Frame;
using CasterProtocol::MAX_FRAME_BYTES;
using CasterTransportCore::FrameSink;
using CasterTransportCore::INVALID_ID;
using CasterTransportCore::MAX_CLIENT_SOURCES;
using CasterTransportCore::MAX_CONNECTIONS;
using CasterTransportCore::OutboundQueue;
using CasterTransportCore::QueueSink;

// The thread entry points are befriended by Server and Client, but a friend
// declaration alone does not make the name visible to ordinary lookup. Declare
// them here so taking their address below does not rely on friend injection.
unsigned __stdcall casterServerAcceptProc(void* param);
unsigned __stdcall casterServerWorkerProc(void* param);
unsigned __stdcall casterClientWorkerProc(void* param);

static const long WORKER_TICK_USEC = 2000;

static void setNonBlockingSocket(SOCKET sock)
{
	// Send small per-tick command/progress frames without Nagle delays.
	BOOL noDelay = TRUE;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);
}

static Bool peekFrame(const UnsignedByte* inbox, UnsignedInt size, Frame& out,
	UnsignedInt& total, Bool& malformed)
{
	UnsignedInt declared;

	malformed = FALSE;
	total = 0;
	if (size < 4)
	{
		return FALSE;
	}
	declared = (UnsignedInt)inbox[0]
		| ((UnsignedInt)inbox[1] << 8)
		| ((UnsignedInt)inbox[2] << 16)
		| ((UnsignedInt)inbox[3] << 24);
	total = declared + 4;
	if (total < CasterProtocol::FRAME_HEADER_BYTES || total > MAX_FRAME_BYTES)
	{
		malformed = TRUE;
		return FALSE;
	}
	if (size < total)
	{
		return FALSE;
	}
	if (!CasterProtocol::decodeFrame(inbox[4],
		inbox + CasterProtocol::FRAME_HEADER_BYTES, declared - 1, out))
	{
		malformed = TRUE;
		return FALSE;
	}
	return TRUE;
}

/// Drops the frame `peekFrame` just returned. Only safe once the caller is done
/// with the decoded byte view.
static void removeFrame(UnsignedByte* inbox, UnsignedInt& size, UnsignedInt total)
{
	if (size > total)
	{
		memmove(inbox, inbox + total, size - total);
	}
	size -= total;
}

Server::Server()
{
	UnsignedInt i;

	m_host = nullptr;
	m_chatSink = nullptr;
	m_commandHistory = nullptr;
	m_lobbySnapshotPending = FALSE;
	m_bootstrapPending = FALSE;
	m_commandEndPending = FALSE;
	m_commandEndGameUid = 0;
	m_commandEndHasFrames = FALSE;
	m_commandEndFinalFrame = 0;
	m_listen = INVALID_SOCKET;
	m_port = 0;
	m_acceptThread = NULL;
	m_running = 0;
	m_wsaStarted = FALSE;
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		m_threads[i] = NULL;
		m_connections[i].state = LINK_FREE;
		m_connections[i].socket = INVALID_SOCKET;
	}
	InitializeCriticalSection(&m_lock);
	InitializeCriticalSection(&m_historyLock);
}

Server::~Server()
{
	stop();
	if (m_host != nullptr)
	{

		m_host = nullptr;
	}
	DeleteCriticalSection(&m_lock);
	DeleteCriticalSection(&m_historyLock);
}

Bool Server::createListenSocket(UnsignedShort port)
{
	WSADATA wsaData;
	sockaddr_in address;
	int reuse = 1;
	int length;
	SOCKET sock;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		return FALSE;
	}
	m_wsaStarted = TRUE;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		return FALSE;
	}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);

	if (bind(sock, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
	{
		closesocket(sock);
		return FALSE;
	}
	if (listen(sock, 8) == SOCKET_ERROR)
	{
		closesocket(sock);
		return FALSE;
	}

	length = sizeof(address);
	if (getsockname(sock, (sockaddr*)&address, &length) != SOCKET_ERROR)
	{
		m_port = ntohs(address.sin_port);
	}
	else
	{
		m_port = port;
	}

	m_listen = sock;
	setNonBlockingSocket(m_listen);
	return TRUE;
}

Bool Server::start(HostServices* host, UnsignedShort port)
{
	if (m_running || host == nullptr)
	{
		return FALSE;
	}
	if (!createListenSocket(port))
	{
		if (m_wsaStarted)
		{
			WSACleanup();
			m_wsaStarted = FALSE;
		}
		return FALSE;
	}

	m_lobbySnapshotPending = FALSE;
	m_bootstrapPending = FALSE;
	m_host = host;
	m_running = 1;
	m_acceptThread = (HANDLE)_beginthreadex(NULL, 0, &casterServerAcceptProc, this, 0, NULL);
	if (m_acceptThread == NULL)
	{
		m_running = 0;
		closesocket(m_listen);
		m_listen = INVALID_SOCKET;
		WSACleanup();
		m_wsaStarted = FALSE;
		return FALSE;
	}
	return TRUE;
}

void Server::freeSlot(UnsignedInt index)
{
	Connection& connection = m_connections[index];
	HANDLE thread = m_threads[index];
	SOCKET sock = INVALID_SOCKET;

	if (thread != NULL)
	{
		WaitForSingleObject(thread, 5000);
		CloseHandle(thread);
		m_threads[index] = NULL;
	}

	// The lock is held for the table/session bookkeeping only; the socket close
	// happens after it is released.
	EnterCriticalSection(&m_lock);
	sock = connection.socket;
	connection.socket = INVALID_SOCKET;
	connection.session.onTeardown(m_table);
	connection.queue.clear();
	connection.inboxSize = 0;
	connection.state = LINK_FREE;
	LeaveCriticalSection(&m_lock);

	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}
}

void Server::reapFinished()
{
	UnsignedInt i;

	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		if (m_threads[i] == NULL || m_connections[i].state == LINK_FREE)
		{
			continue;
		}
		if (WaitForSingleObject(m_threads[i], 0) != WAIT_OBJECT_0)
		{
			continue;
		}
		freeSlot(i);
	}
}

void Server::stop()
{
	UnsignedInt i;

	if (m_running)
	{
		m_running = 0;
	}

	if (m_acceptThread != NULL)
	{
		WaitForSingleObject(m_acceptThread, 5000);
		CloseHandle(m_acceptThread);
		m_acceptThread = NULL;
	}

	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		SOCKET sock = INVALID_SOCKET;
		EnterCriticalSection(&m_lock);
		if (m_connections[i].state != LINK_FREE)
		{
			sock = m_connections[i].socket;
		}
		LeaveCriticalSection(&m_lock);
		if (sock != INVALID_SOCKET)
		{
			shutdown(sock, SD_BOTH);
		}
	}

	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		freeSlot(i);
	}

	EnterCriticalSection(&m_lock);
	m_table.reset();
	LeaveCriticalSection(&m_lock);

	if (m_listen != INVALID_SOCKET)
	{
		closesocket(m_listen);
		m_listen = INVALID_SOCKET;
	}
	if (m_wsaStarted)
	{
		WSACleanup();
		m_wsaStarted = FALSE;
	}
	m_host = nullptr;
	m_chatSink = nullptr;
}

Bool Server::isRunning() const
{
	return (m_running) ? TRUE : FALSE;
}

UnsignedShort Server::port() const
{
	return m_port;
}

Bool Server::spawnWorker(UnsignedInt index)
{
	WorkerParam* param = new WorkerParam;
	HANDLE thread;

	if (param == nullptr)
	{
		return FALSE;
	}
	param->server = this;
	param->index = index;

	thread = (HANDLE)_beginthreadex(NULL, 0, &casterServerWorkerProc, param, 0, NULL);
	if (thread == NULL)
	{
		delete param;
		return FALSE;
	}
	m_threads[index] = thread;
	return TRUE;
}

void Server::acceptLoop()
{
	while (m_running)
	{
		fd_set readSet;
		timeval tv;
		int ready;
		SOCKET client;
		UnsignedInt slot;
		UnsignedInt i;

		reapFinished();

		FD_ZERO(&readSet);
		FD_SET(m_listen, &readSet);
		tv.tv_sec = 0;
		tv.tv_usec = 50000;
		ready = select(0, &readSet, NULL, NULL, &tv);
		if (ready == SOCKET_ERROR)
		{
			break;
		}
		if (ready == 0)
		{
			continue;
		}

		client = accept(m_listen, NULL, NULL);
		if (client == INVALID_SOCKET)
		{
			continue;
		}

		slot = INVALID_INDEX;
		EnterCriticalSection(&m_lock);
		for (i = 0; i < MAX_CONNECTIONS; ++i)
		{
			if (m_connections[i].state == LINK_FREE && m_threads[i] == NULL)
			{
				slot = i;
				m_connections[i].state = LINK_ACTIVE;
				m_connections[i].socket = client;
				m_connections[i].queue.clear();
				m_connections[i].inboxSize = 0;
				m_connections[i].session.reset();
				m_connections[i].deliveredCommandEndGameUid = 0;
				break;
			}
		}
		LeaveCriticalSection(&m_lock);

		if (slot == INVALID_INDEX)
		{

			closesocket(client);
			continue;
		}

		setNonBlockingSocket(client);
		if (!spawnWorker(slot))
		{
			EnterCriticalSection(&m_lock);
			m_connections[slot].socket = INVALID_SOCKET;
			m_connections[slot].state = LINK_FREE;
			LeaveCriticalSection(&m_lock);
			closesocket(client);
		}
	}

	reapFinished();
}

void Server::workerLoop(UnsignedInt index)
{
	Connection& connection = m_connections[index];
	UnsignedByte sendBuffer[2048];
	QueueSink sink(&connection.queue);
	Bool alive = TRUE;
	SOCKET sock;

	for (;;)
	{
		if (!m_running || !alive)
		{
			break;
		}

		// 1. Drain the bounded outbound queue: peek under the lock, send
		//    outside it, consume under it.
		for (;;)
		{
			UnsignedInt available;
			int sent;

			EnterCriticalSection(&m_lock);
			available = connection.queue.peek(sendBuffer, sizeof(sendBuffer));
			LeaveCriticalSection(&m_lock);
			if (available == 0)
			{
				break;
			}

			sent = send(connection.socket, (const char*)sendBuffer, (int)available, 0);
			if (sent == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					break;
				}
				alive = FALSE;
				break;
			}
			if (sent <= 0)
			{
				break;
			}

			EnterCriticalSection(&m_lock);
			connection.queue.consume((UnsignedInt)sent);
			// A previously full queue may now have room for the retained terminal END.
			retryCommandEnd(index);
			LeaveCriticalSection(&m_lock);
			if ((UnsignedInt)sent < available)
			{
				break;
			}
		}
		if (!alive)
		{
			break;
		}

		{
			fd_set readSet;
			timeval tv;
			int ready;

			FD_ZERO(&readSet);
			FD_SET(connection.socket, &readSet);
			tv.tv_sec = 0;
			tv.tv_usec = WORKER_TICK_USEC;
			ready = select(0, &readSet, NULL, NULL, &tv);
			if (ready == SOCKET_ERROR)
			{
				alive = FALSE;
				break;
			}
			if (ready == 0)
			{
				continue;
			}
		}

		// 3. Receive into the link inbox and dispatch every complete frame.
		{
			UnsignedInt space = INBOX_BYTES - connection.inboxSize;
			int got;

			if (space == 0)
			{
				// A full inbox with no complete frame is a malformed peer.
				alive = FALSE;
				break;
			}
			got = recv(connection.socket, (char*)(connection.inbox + connection.inboxSize),
				(int)space, 0);
			if (got == 0)
			{
				alive = FALSE;
				break;
			}
			if (got == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					continue;
				}
				alive = FALSE;
				break;
			}

			connection.inboxSize += (UnsignedInt)got;
			EnterCriticalSection(&m_lock);
			for (;;)
			{
				Frame decoded;
				UnsignedInt total = 0;
				Bool malformed = FALSE;
				if (!peekFrame(connection.inbox, connection.inboxSize, decoded, total, malformed))
				{
					if (malformed)
					{
						alive = FALSE;
					}
					break;
				}
				if (decoded.type == CasterProtocol::FRAME_CHAT && m_chatSink != nullptr
					&& connection.session.isSubscribed()
					&& connection.session.subscribedGameUid() == decoded.gameUid
					&& decoded.isSenderCaster != 0)
				{
					m_chatSink->onChat(decoded.gameUid, decoded.commandID, decoded.direction, decoded.senderSlot,
						decoded.isSenderCaster, decoded.senderTeam, decoded.senderIdentity,
						decoded.senderName, decoded.senderNameLen, decoded.text, decoded.textLen, index,
						decoded.isEmote);
				}
				else if (decoded.type == CasterProtocol::FRAME_LOBBY)
				{
					alive = FALSE;
					break;
				}
				else
				{
					if (decoded.type == CasterProtocol::FRAME_COMMAND_HISTORY_REQ)
					{
						removeFrame(connection.inbox, connection.inboxSize, total);
						LeaveCriticalSection(&m_lock);
						serveCommandHistory(index, decoded);
						EnterCriticalSection(&m_lock);
						retryCommandEnd(index);
						continue;
					}
					Bool wasSubscribed = connection.session.isSubscribed();
					connection.session.onFrame(decoded, *m_host, m_table, sink);
					if (!wasSubscribed && connection.session.isSubscribed())
					{
						m_lobbySnapshotPending = TRUE;
						retryCommandEnd(index);
					}
					// A subscribe that reached streaming-ready needs the typed
					// match-start bootstrap, including the re-subscribe a running
					// match triggers on an already subscribed link.
					if (decoded.type == CasterProtocol::FRAME_SUBSCRIBE
						&& connection.session.isStreamingReady())
					{
						m_bootstrapPending = TRUE;
					}
				}
				removeFrame(connection.inbox, connection.inboxSize, total);
			}
			LeaveCriticalSection(&m_lock);
			if (!alive)
			{
				break;
			}
		}
	}

	// Mark the slot for the reaper. The reaper owns the socket close, the
	// teardown and the slot reset, so there is exactly one cleanup path.
	EnterCriticalSection(&m_lock);
	sock = connection.socket;
	connection.state = LINK_CLOSING;
	LeaveCriticalSection(&m_lock);
	if (sock != INVALID_SOCKET)
	{
		shutdown(sock, SD_BOTH);
	}
}

unsigned __stdcall casterServerAcceptProc(void* param)
{
	Server* server = (Server*)param;
	server->acceptLoop();
	return 0;
}

unsigned __stdcall casterServerWorkerProc(void* param)
{
	Server::WorkerParam* worker = (Server::WorkerParam*)param;
	Server* server = worker->server;
	UnsignedInt index = worker->index;
	delete worker;
	server->workerLoop(index);
	return 0;
}

void Server::serveCommandHistory(UnsignedInt index, const CasterProtocol::Frame& request)
{
	UnsignedInt requested;
	Connection& connection = m_connections[index];
	EnterCriticalSection(&m_lock);
	Bool allowed = connection.state == LINK_ACTIVE && connection.session.subscribedGameUid() == request.gameUid
		&& connection.session.isStreamingReady();
	LeaveCriticalSection(&m_lock);
	if (!allowed) return;
	for (requested = 0; requested < request.frameCount; ++requested)
	{
		UnsignedByte commandBytes[CasterProtocol::MAX_COMMAND_FRAME_BYTES];
		UnsignedInt length = 0;
		UnsignedInt commandFrame = request.firstSimulationFrame + requested;
		UnsignedInt checksum;
		UnsignedInt offset = 0;
		UnsignedInt fragments;
		UnsignedInt wireTotal;
		CasterFrameHistory* history;
		EnterCriticalSection(&m_historyLock);
		history = m_commandHistory;
		if (history != nullptr) history->read(commandFrame, commandBytes, sizeof(commandBytes), length);
		LeaveCriticalSection(&m_historyLock);
		if (length == 0) continue;
		checksum = CasterCommandFrame::checksum(commandBytes, length);
		fragments = (length + CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES - 1)
			/ CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES;
		wireTotal = length + fragments * 25;
		EnterCriticalSection(&m_lock);
		if (connection.state != LINK_ACTIVE || connection.session.subscribedGameUid() != request.gameUid
			|| !connection.session.isStreamingReady() || !connection.queue.canPushBytes(wireTotal))
		{
			LeaveCriticalSection(&m_lock);
			return;
		}
		while (offset < length)
		{
			UnsignedByte wire[MAX_FRAME_BYTES];
			UnsignedInt fragmentLength = length - offset;
			UnsignedInt wireLength;
			if (fragmentLength > CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES) fragmentLength = CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES;
			wireLength = CasterProtocol::encodeCommandFragment(wire, sizeof(wire), request.gameUid,
				commandFrame, length, checksum, offset, commandBytes + offset, fragmentLength);
			if (wireLength == 0)
			{
				LeaveCriticalSection(&m_lock);
				return;
			}
			if (!connection.queue.push(wire, wireLength))
			{
				LeaveCriticalSection(&m_lock);
				return;
			}
			offset += fragmentLength;
		}
		LeaveCriticalSection(&m_lock);
	}
}

Bool Server::broadcastCommandFrame(UnsignedInt gameUid, UnsignedInt simulationFrame,
	const UnsignedByte* data, UnsignedInt length)
{
	UnsignedInt offset = 0;
	UnsignedInt checksum;
	UnsignedInt i;
	UnsignedInt fragments;
	UnsignedInt wireTotal;
	Bool targets[MAX_CONNECTIONS];
	Bool historyAccepted;
	if (!m_running || gameUid == 0 || data == nullptr || length == 0
		|| length > CasterProtocol::MAX_COMMAND_FRAME_BYTES)
	{
		return FALSE;
	}
	checksum = CasterCommandFrame::checksum(data, length);
	fragments = (length + CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES - 1)
		/ CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES;
	wireTotal = length + fragments * 25;
	EnterCriticalSection(&m_historyLock);
	historyAccepted = (m_commandHistory == nullptr || m_commandHistory->append(simulationFrame, data, length)) ? TRUE : FALSE;
	LeaveCriticalSection(&m_historyLock);
	if (!historyAccepted) return FALSE;
	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		targets[i] = (m_connections[i].state == LINK_ACTIVE
			&& m_connections[i].session.subscribedGameUid() == gameUid
			&& m_connections[i].session.isStreamingReady()
			&& m_connections[i].queue.canPushBytes(wireTotal)) ? TRUE : FALSE;
	}
	while (offset < length)
	{
		UnsignedByte frame[MAX_FRAME_BYTES];
		UnsignedInt fragmentLength = length - offset;
		UnsignedInt frameLength;
		if (fragmentLength > CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES)
		{
			fragmentLength = CasterProtocol::MAX_COMMAND_FRAGMENT_BYTES;
		}
		frameLength = CasterProtocol::encodeCommandFragment(frame, sizeof(frame), gameUid,
			simulationFrame, length, checksum, offset, data + offset, fragmentLength);
		if (frameLength == 0)
		{
			LeaveCriticalSection(&m_lock);
			return FALSE;
		}
		for (i = 0; i < MAX_CONNECTIONS; ++i)
		{
			if (targets[i])
			{
				m_connections[i].queue.push(frame, frameLength);
			}
		}
		offset += fragmentLength;
	}
	LeaveCriticalSection(&m_lock);
	return TRUE;
}

void Server::broadcastCommandEnd(UnsignedInt gameUid, Bool hasFrames, UnsignedInt finalSimulationFrame)
{
	UnsignedInt i;
	if (!m_running || gameUid == 0) return;
	EnterCriticalSection(&m_lock);
	// Retain terminal state for retries after a full queue, reconnect, or late
	// subscription. A new announced game replaces the previous terminal state.
	m_commandEndPending = TRUE;
	m_commandEndGameUid = gameUid;
	m_commandEndHasFrames = hasFrames;
	m_commandEndFinalFrame = finalSimulationFrame;
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		retryCommandEnd(i);
	}
	LeaveCriticalSection(&m_lock);
}

void Server::retryCommandEnd(UnsignedInt index)
{
	UnsignedByte frame[MAX_FRAME_BYTES];
	UnsignedInt length;
	Connection& connection = m_connections[index];
	if (!m_commandEndPending || connection.state != LINK_ACTIVE
		|| connection.session.subscribedGameUid() != m_commandEndGameUid
		|| !connection.session.isStreamingReady()
		|| connection.deliveredCommandEndGameUid == m_commandEndGameUid)
		return;
	length = CasterProtocol::encodeCommandEnd(frame, sizeof(frame), m_commandEndGameUid,
		m_commandEndHasFrames, m_commandEndFinalFrame);
	if (length != 0 && connection.queue.canPushBytes(length))
	{
		connection.queue.push(frame, length);
		connection.deliveredCommandEndGameUid = m_commandEndGameUid;
	}
}

void Server::setCommandHistory(CasterFrameHistory* history)
{
	EnterCriticalSection(&m_historyLock);
	m_commandHistory = history;
	LeaveCriticalSection(&m_historyLock);
	// A new recorder/history marks a new native session even if an upstream UID
	// was reused. Do not replay the preceding session's terminal marker.
	EnterCriticalSection(&m_lock);
	m_commandEndPending = FALSE;
	m_commandEndGameUid = 0;
	for (UnsignedInt i = 0; i < MAX_CONNECTIONS; ++i)
		m_connections[i].deliveredCommandEndGameUid = 0;
	LeaveCriticalSection(&m_lock);
}

void Server::broadcastChatFrame(const UnsignedByte* frame, UnsignedInt length, UnsignedInt excludeLinkId)
{
	UnsignedInt i;

	if (!m_running || frame == nullptr || length == 0 || length > CasterProtocol::MAX_FRAME_BYTES)
	{
		return;
	}

	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		if (i == excludeLinkId || m_connections[i].state != LINK_ACTIVE)
		{
			continue;
		}
		m_connections[i].queue.push(frame, length);
	}
	LeaveCriticalSection(&m_lock);
}

Bool Server::takeLobbySnapshotRequest(UnsignedInt gameUid)
{
	// Worker records subscription only; the main thread serializes game state.
	EnterCriticalSection(&m_lock);
	Bool pending = m_lobbySnapshotPending && m_table.isSubscribed(gameUid);
	if (pending)
	{
		m_lobbySnapshotPending = FALSE;
	}
	LeaveCriticalSection(&m_lock);
	return pending;
}

void Server::broadcastLobbyFrame(UnsignedInt gameUid, const UnsignedByte* frame, UnsignedInt length)
{
	UnsignedInt i;

	if (!m_running || gameUid == 0 || frame == nullptr || length == 0
		|| length > CasterProtocol::MAX_FRAME_BYTES)
	{
		return;
	}

	// Subscribed connections only: the same per-gameUid subscribe-table gate the
	// command-frame broadcast uses, so a caster that never subscribed to this
	// game receives no lobby state. Enqueue only; no socket call under the lock.
	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		if (m_connections[i].state != LINK_ACTIVE)
		{
			continue;
		}
		if (m_connections[i].session.subscribedGameUid() != gameUid)
		{
			continue;
		}
		m_connections[i].queue.push(frame, length);
	}
	LeaveCriticalSection(&m_lock);
}

Bool Server::takeBootstrapRequest(UnsignedInt gameUid)
{
	Bool pending;

	// Worker records the readiness only; the main thread owns the game state.
	EnterCriticalSection(&m_lock);
	pending = (m_bootstrapPending && m_table.isSubscribed(gameUid)) ? TRUE : FALSE;
	if (pending)
	{
		m_bootstrapPending = FALSE;
	}
	LeaveCriticalSection(&m_lock);
	return pending;
}

void Server::broadcastBootstrapFrame(UnsignedInt gameUid, const UnsignedByte* frame,
	UnsignedInt length)
{
	UnsignedInt i;

	if (!m_running || gameUid == 0 || frame == nullptr || length == 0
		|| length > CasterProtocol::MAX_FRAME_BYTES)
	{
		return;
	}

	// Streaming-ready subscribers only: the caster's source set discards a
	// bootstrap that arrives before its own SUB_ACK promoted the link.
	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		if (m_connections[i].state != LINK_ACTIVE)
		{
			continue;
		}
		if (m_connections[i].session.subscribedGameUid() != gameUid
			|| !m_connections[i].session.isStreamingReady())
		{
			continue;
		}
		m_connections[i].queue.push(frame, length);
	}
	LeaveCriticalSection(&m_lock);
}

void Server::refreshGameMetadata(UnsignedInt gameUid)
{
	UnsignedByte frame[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt frameLen;
	UnsignedInt i;

	if (!m_running || m_host == nullptr || gameUid == 0 || m_host->currentGameUid() != gameUid)
	{
		return;
	}

	frameLen = CasterProtocol::encodeHelloAck(frame, sizeof(frame), m_host->matchRunning(gameUid),
		m_host->gameName(gameUid));
	if (frameLen == 0)
	{
		return;
	}

	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CONNECTIONS; ++i)
	{
		if (m_connections[i].state != LINK_ACTIVE || !m_connections[i].session.helloDone()
			|| m_connections[i].session.subscribedGameUid() != gameUid)
		{
			continue;
		}
		m_connections[i].queue.push(frame, frameLen);
	}
	LeaveCriticalSection(&m_lock);
}

void Server::setChatSink(CasterTransportCore::ChatSink* sink)
{
	m_chatSink = sink;
}

Bool Server::isSubscribed(UnsignedInt gameUid)
{
	Bool subscribed;

	EnterCriticalSection(&m_lock);
	subscribed = m_table.isSubscribed(gameUid);
	LeaveCriticalSection(&m_lock);
	return subscribed;
}

Client::Client()
{
	UnsignedInt i;

	m_running = 0;
	m_wsaStarted = FALSE;
	m_chatSink = nullptr;
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		m_threads[i] = NULL;
		m_links[i].state = LINK_FREE;
		m_links[i].id = INVALID_ID;
		m_links[i].socket = INVALID_SOCKET;
	}
	InitializeCriticalSection(&m_lock);
	m_router.bind(this);
	m_core.reset(0);
}

Client::~Client()
{
	stop();
	DeleteCriticalSection(&m_lock);
}

Client::Router::Router()
{
	m_client = nullptr;
}

void Client::Router::bind(Client* client)
{
	m_client = client;
}

Bool Client::Router::emit(UnsignedInt targetId, const UnsignedByte* frame, UnsignedInt length)
{
	SourceLink* link;

	if (m_client == nullptr)
	{
		return FALSE;
	}

	link = m_client->findLink(targetId);
	if (link == nullptr)
	{
		return FALSE;
	}
	return link->queue.push(frame, length);
}

Client::SourceLink* Client::findLink(UnsignedInt sourceId)
{
	UnsignedInt i;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_links[i].state != LINK_FREE && m_links[i].id == sourceId)
		{
			return &m_links[i];
		}
	}
	return nullptr;
}

Client::SourceLink* Client::findFreeLink()
{
	UnsignedInt i;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_links[i].state == LINK_FREE && m_threads[i] == NULL)
		{
			return &m_links[i];
		}
	}
	return nullptr;
}

void Client::reset(UnsignedInt gameUid)
{
	UnsignedInt i;

	stop();
	EnterCriticalSection(&m_lock);
	m_core.reset(gameUid);
	LeaveCriticalSection(&m_lock);

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		m_links[i].state = LINK_FREE;
		m_links[i].id = INVALID_ID;
		m_links[i].socket = INVALID_SOCKET;
	}
}

Bool Client::addSource(UnsignedInt sourceId, const char* ip, UnsignedShort port, Bool isHost)
{
	WSADATA wsaData;
	SourceLink* link;
	UnsignedInt index;

	if (sourceId == INVALID_ID || ip == nullptr)
	{
		return FALSE;
	}

	EnterCriticalSection(&m_lock);
	if (findLink(sourceId) != nullptr)
	{
		LeaveCriticalSection(&m_lock);
		return FALSE;
	}
	link = findFreeLink();
	if (link == nullptr)
	{
		LeaveCriticalSection(&m_lock);
		return FALSE;
	}
	index = (UnsignedInt)(link - m_links);

	link->state = LINK_ACTIVE;
	link->id = sourceId;
	link->isHost = isHost;
	link->ip = (UnsignedInt)inet_addr(ip);
	link->port = port;
	link->socket = INVALID_SOCKET;
	link->queue.clear();
	link->chatQueue.clear();
	link->inboxSize = 0;
	link->reconnect.reset();
	link->rejected = FALSE;
	link->corePresent = TRUE;

	if (!m_core.addSource(sourceId))
	{
		link->state = LINK_FREE;
		link->id = INVALID_ID;
		LeaveCriticalSection(&m_lock);
		return FALSE;
	}
	LeaveCriticalSection(&m_lock);

	if (!m_wsaStarted)
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			EnterCriticalSection(&m_lock);
			m_core.removeSource(sourceId);
			link->state = LINK_FREE;
			link->id = INVALID_ID;
			LeaveCriticalSection(&m_lock);
			return FALSE;
		}
		m_wsaStarted = TRUE;
	}
	m_running = 1;

	if (!spawnWorker(index))
	{
		EnterCriticalSection(&m_lock);
		m_core.removeSource(sourceId);
		link->state = LINK_FREE;
		link->id = INVALID_ID;
		LeaveCriticalSection(&m_lock);
		return FALSE;
	}
	return TRUE;
}

Bool Client::spawnWorker(UnsignedInt index)
{
	WorkerParam* param = new WorkerParam;
	HANDLE thread;

	if (param == nullptr)
	{
		return FALSE;
	}
	param->client = this;
	param->index = index;

	thread = (HANDLE)_beginthreadex(NULL, 0, &casterClientWorkerProc, param, 0, NULL);
	if (thread == NULL)
	{
		delete param;
		return FALSE;
	}
	m_threads[index] = thread;
	return TRUE;
}

void Client::freeLink(UnsignedInt index)
{
	SourceLink& link = m_links[index];
	HANDLE thread = m_threads[index];
	SOCKET sock = INVALID_SOCKET;

	if (thread != NULL)
	{
		WaitForSingleObject(thread, 5000);
		CloseHandle(thread);
		m_threads[index] = NULL;
	}

	EnterCriticalSection(&m_lock);
	sock = link.socket;
	link.socket = INVALID_SOCKET;
	if (link.id != INVALID_ID)
	{
		m_core.removeSource(link.id);
	}
	link.queue.clear();
	link.chatQueue.clear();
	link.inboxSize = 0;
	link.state = LINK_FREE;	link.id = INVALID_ID;
	LeaveCriticalSection(&m_lock);

	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}
}

void Client::reapFinished()
{
	UnsignedInt i;
	UnsignedInt now = GetTickCount();
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		SourceLink& link = m_links[i];
		if (link.state != LINK_CLOSING)
		{
			continue;
		}
		if (m_threads[i] != NULL && WaitForSingleObject(m_threads[i], 0) != WAIT_OBJECT_0)
		{
			continue;
		}
		if (link.rejected)
		{
			freeLink(i);
			continue;
		}
		EnterCriticalSection(&m_lock);
		if (link.corePresent)
		{
			m_core.removeSource(link.id);
			link.corePresent = FALSE;
		}
		LeaveCriticalSection(&m_lock);
		if (m_threads[i] != NULL)
		{
			CloseHandle(m_threads[i]);
			m_threads[i] = NULL;
		}
		if (link.socket != INVALID_SOCKET)
		{
			closesocket(link.socket);
			link.socket = INVALID_SOCKET;
		}
		link.queue.clear();
		link.chatQueue.clear();
		link.inboxSize = 0;
		if (!m_running)
		{
			continue;
		}
		if (link.reconnect.mustWait(now))
		{
			continue;
		}
		link.state = LINK_ACTIVE;
		if (!spawnWorker(i))
		{
			link.state = LINK_CLOSING;
		}
	}
}

Bool Client::removeSource(UnsignedInt sourceId)
{
	SourceLink* link;
	UnsignedInt index;
	SOCKET sock = INVALID_SOCKET;

	EnterCriticalSection(&m_lock);
	link = findLink(sourceId);
	if (link == nullptr)
	{
		LeaveCriticalSection(&m_lock);
		return FALSE;
	}
	index = (UnsignedInt)(link - m_links);
	link->state = LINK_CLOSING;
	sock = link->socket;
	LeaveCriticalSection(&m_lock);

	if (sock != INVALID_SOCKET)
	{
		shutdown(sock, SD_BOTH);
	}
	freeLink(index);
	return TRUE;
}

void Client::stop()
{
	UnsignedInt i;

	if (!m_running && !m_wsaStarted)
	{
		return;
	}
	m_running = 0;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		SOCKET sock = INVALID_SOCKET;
		EnterCriticalSection(&m_lock);
		if (m_links[i].state != LINK_FREE)
		{
			sock = m_links[i].socket;
		}
		LeaveCriticalSection(&m_lock);
		if (sock != INVALID_SOCKET)
		{
			shutdown(sock, SD_BOTH);
		}
	}

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		freeLink(i);
	}

	if (m_wsaStarted)
	{
		WSACleanup();
		m_wsaStarted = FALSE;
	}
}

void Client::workerLoop(UnsignedInt index)
{
	SourceLink& link = m_links[index];
	UnsignedByte sendBuffer[2048];
	SOCKET sock;
	Bool alive = TRUE;
	EnterCriticalSection(&m_lock);
	if (!link.corePresent)
	{
		m_core.addSource(link.id);
		link.corePresent = TRUE;
	}
	m_core.rearmSource(link.id);
	LeaveCriticalSection(&m_lock);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		EnterCriticalSection(&m_lock);
		link.state = LINK_CLOSING;
		LeaveCriticalSection(&m_lock);
		return;
	}
	setNonBlockingSocket(sock);
	EnterCriticalSection(&m_lock);
	link.socket = sock;
	LeaveCriticalSection(&m_lock);

	// Bounded, cancelable connect on the worker thread.
	{
		sockaddr_in address;
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = link.ip;
		address.sin_port = htons(link.port);

		if (connect(sock, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
		{
			if (WSAGetLastError() != WSAEWOULDBLOCK)
			{
				alive = FALSE;
			}
			else
			{
				for (;;)
				{
					fd_set writeSet;
					timeval tv;
					int ready;

					if (!m_running)
					{
						alive = FALSE;
						break;
					}
					FD_ZERO(&writeSet);
					FD_SET(sock, &writeSet);
					tv.tv_sec = 0;
					tv.tv_usec = 100000;
					ready = select(0, NULL, &writeSet, NULL, &tv);
					if (ready == SOCKET_ERROR)
					{
						alive = FALSE;
						break;
					}
					if (ready == 0)
					{
						continue;
					}
					{
						int error = 0;
						int errorLen = sizeof(error);
						getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &errorLen);
						if (error != 0)
						{
							alive = FALSE;
						}
					}
					break;
				}
			}
		}
	}

	// HELLO goes through the same bounded queue as every other frame.
	if (alive)
	{
		UnsignedByte hello[MAX_FRAME_BYTES];
		UnsignedInt helloLen;

		EnterCriticalSection(&m_lock);
		helloLen = m_core.buildHello(hello, sizeof(hello));
		LeaveCriticalSection(&m_lock);
		if (helloLen != 0)
		{
			EnterCriticalSection(&m_lock);
			link.queue.push(hello, helloLen);
			LeaveCriticalSection(&m_lock);
		}
	}

	while (m_running && alive)
	{
		// 1. Drain this link's bounded queue.
		for (;;)
		{
			UnsignedInt available;
			int sent;

			EnterCriticalSection(&m_lock);
			available = link.queue.peek(sendBuffer, sizeof(sendBuffer));
			LeaveCriticalSection(&m_lock);
			if (available == 0)
			{
				break;
			}

			sent = send(sock, (const char*)sendBuffer, (int)available, 0);
			if (sent == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					break;
				}
				alive = FALSE;
				break;
			}
			if (sent <= 0)
			{
				break;
			}

			EnterCriticalSection(&m_lock);
			link.queue.consume((UnsignedInt)sent);
			LeaveCriticalSection(&m_lock);
			if ((UnsignedInt)sent < available)
			{
				break;
			}
		}
		if (!alive)
		{
			break;
		}

		// A server accepts caster chat only after it has processed this
		// source's SUBSCRIBE. Keep chat behind the handshake rather than sending
		// it after HELLO but before SUB_ACK.
		{
			CasterTransportCore::ClientSource source;
			Bool chatReady = FALSE;

			EnterCriticalSection(&m_lock);
			chatReady = m_core.getSource(link.id, source) && source.subAckSeen;
			LeaveCriticalSection(&m_lock);
			if (chatReady)
			{
				for (;;)
				{
					UnsignedInt available;
					int sent;

					EnterCriticalSection(&m_lock);
					available = link.chatQueue.peek(sendBuffer, sizeof(sendBuffer));
					LeaveCriticalSection(&m_lock);
					if (available == 0)
					{
						break;
					}
					sent = send(sock, (const char*)sendBuffer, (int)available, 0);
					if (sent == SOCKET_ERROR)
					{
						if (WSAGetLastError() == WSAEWOULDBLOCK)
						{
							break;
						}
						alive = FALSE;
						break;
					}
					if (sent <= 0)
					{
						break;
					}
					EnterCriticalSection(&m_lock);
					link.chatQueue.consume((UnsignedInt)sent);
					LeaveCriticalSection(&m_lock);
					if ((UnsignedInt)sent < available)
					{
						break;
					}
				}
			}
		}
		if (!alive)
		{
			break;
		}

		{
			fd_set readSet;
			timeval tv;
			int ready;

			FD_ZERO(&readSet);
			FD_SET(sock, &readSet);
			tv.tv_sec = 0;
			tv.tv_usec = WORKER_TICK_USEC;
			ready = select(0, &readSet, NULL, NULL, &tv);
			if (ready == SOCKET_ERROR)
			{
				alive = FALSE;
				break;
			}
			if (ready == 0)
			{
				continue;
			}
		}

		// 3. Receive into the link inbox and hand complete frames to the core.
		{
			UnsignedInt space = INBOX_BYTES - link.inboxSize;
			int got;

			if (space == 0)
			{
				alive = FALSE;
				break;
			}
			got = recv(sock, (char*)(link.inbox + link.inboxSize), (int)space, 0);
			if (got == 0)
			{
				alive = FALSE;
				break;
			}
			if (got == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					continue;
				}
				alive = FALSE;
				break;
			}

			link.inboxSize += (UnsignedInt)got;
			EnterCriticalSection(&m_lock);
			for (;;)
			{
				Frame decoded;
				UnsignedInt total = 0;
				Bool malformed = FALSE;
				if (!peekFrame(link.inbox, link.inboxSize, decoded, total, malformed))
				{
					if (malformed)
					{
						alive = FALSE;
					}
					break;
				}
				if (!m_core.relaySideband(link.id, decoded, link.isHost, m_chatSink))
				{
					CasterTransportCore::HandleResult result = m_core.onFrame(link.id, decoded, m_router);
					if (result == CasterTransportCore::HANDLE_REJECTED)
					{
						link.rejected = TRUE;
						alive = FALSE;
					}
					if (decoded.type == CasterProtocol::FRAME_COMMAND_FRAGMENT
						&& result == CasterTransportCore::HANDLE_ACCEPTED)
					{
						// Only a newer complete, validated frame refreshes the live-head age.
						m_core.noteValidatedProgress(GetTickCount());
					}
				}
				removeFrame(link.inbox, link.inboxSize, total);
			}
			LeaveCriticalSection(&m_lock);
			if (!alive)
			{
				break;
			}
		}
	}

	EnterCriticalSection(&m_lock);
	link.state = LINK_CLOSING;
	LeaveCriticalSection(&m_lock);
	shutdown(sock, SD_BOTH);
}

unsigned __stdcall casterClientWorkerProc(void* param)
{
	Client::WorkerParam* worker = (Client::WorkerParam*)param;
	Client* client = worker->client;
	UnsignedInt index = worker->index;
	delete worker;
	client->workerLoop(index);
	return 0;
}

void Client::broadcast(const UnsignedByte* frame, UnsignedInt length)
{
	UnsignedInt i;

	if (!m_running || frame == nullptr || length == 0 || length > MAX_FRAME_BYTES)
	{
		return;
	}

	// Enqueue only; the workers drain their own bounded queues and own the
	// socket calls. A full queue drops the whole frame in that link.
	EnterCriticalSection(&m_lock);
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_links[i].state != LINK_ACTIVE)
		{
			continue;
		}
		m_links[i].chatQueue.push(frame, length);
	}
	LeaveCriticalSection(&m_lock);
}

void Client::setChatSink(CasterTransportCore::ChatSink* sink)
{
	m_chatSink = sink;
}

Bool Client::isRunning() const
{
	return (m_running) ? TRUE : FALSE;
}

CasterLobby::LobbySubscription Client::subscriptionState()
{
	CasterTransportCore::SourceSetSnapshot state;

	snapshot(state);
	return state.subscription;
}

Bool Client::update(UnsignedInt elapsedMs)
{
	Bool sent = FALSE;

	if (!m_running)
	{
		return FALSE;
	}
	reapFinished();
	EnterCriticalSection(&m_lock);
	sent = m_core.tick(elapsedMs, m_router);
	LeaveCriticalSection(&m_lock);
	return sent;
}

Bool Client::sourceState(UnsignedInt sourceId, CasterTransportCore::ClientSource& out)
{
	Bool found;

	EnterCriticalSection(&m_lock);
	found = m_core.getSource(sourceId, out);
	LeaveCriticalSection(&m_lock);
	return found;
}

Bool Client::nativeFrameAvailable(UnsignedInt simulationFrame)
{
	Bool available;
	EnterCriticalSection(&m_lock);
	available = m_core.nativeFrameAvailable(simulationFrame);
	LeaveCriticalSection(&m_lock);
	return available;
}

Bool Client::nativeSourcesProgressThrough(UnsignedInt simulationFrame)
{
	Bool available;
	EnterCriticalSection(&m_lock);
	available = m_core.nativeSourcesProgressThrough(simulationFrame);
	LeaveCriticalSection(&m_lock);
	return available;
}

UnsignedInt Client::copyNativeFrame(UnsignedInt simulationFrame, UnsignedByte* dest, UnsignedInt capacity)
{
	UnsignedInt copied;
	EnterCriticalSection(&m_lock);
	copied = m_core.copyNativeFrame(simulationFrame, dest, capacity);
	LeaveCriticalSection(&m_lock);
	return copied;
}

Bool Client::consumeNativeFrame(UnsignedInt simulationFrame)
{
	Bool consumed;
	EnterCriticalSection(&m_lock);
	consumed = m_core.consumeNativeFrame(simulationFrame, m_router);
	LeaveCriticalSection(&m_lock);
	return consumed;
}

UnsignedInt Client::nextNativeFrame()
{
	UnsignedInt next;
	EnterCriticalSection(&m_lock);
	next = m_core.nextNativeFrame();
	LeaveCriticalSection(&m_lock);
	return next;
}

Bool Client::nativeProgress(UnsignedInt& firstSimulationFrame, UnsignedInt& lastSimulationFrame, UnsignedInt& frameCount)
{
	Bool available;
	EnterCriticalSection(&m_lock);
	available = m_core.nativeProgress(firstSimulationFrame, lastSimulationFrame, frameCount);
	LeaveCriticalSection(&m_lock);
	return available;
}

Bool Client::nativeEnd(Bool& hasFrames, UnsignedInt& finalSimulationFrame)
{
	Bool available;
	EnterCriticalSection(&m_lock);
	available = m_core.nativeEnd(hasFrames, finalSimulationFrame);
	LeaveCriticalSection(&m_lock);
	return available;
}

Bool Client::nativeUnavailable()
{
	Bool unavailable;
	EnterCriticalSection(&m_lock);
	unavailable = m_core.nativeUnavailable();
	LeaveCriticalSection(&m_lock);
	return unavailable;
}

Bool Client::protocolIncompatible()
{
	Bool incompatible;
	EnterCriticalSection(&m_lock);
	incompatible = m_core.protocolIncompatible();
	LeaveCriticalSection(&m_lock);
	return incompatible;
}

Bool Client::transportDisagreement()
{
	Bool disagreement;
	EnterCriticalSection(&m_lock);
	disagreement = m_core.transportDisagreement();
	LeaveCriticalSection(&m_lock);
	return disagreement;
}

void Client::snapshot(CasterTransportCore::SourceSetSnapshot& out)
{
	UnsignedInt i;
	UnsignedInt count = 0;

	EnterCriticalSection(&m_lock);
	m_core.snapshot(GetTickCount(), out);
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		CasterTransportCore::ClientSource info;
		if (m_links[i].state == LINK_ACTIVE && m_core.getSource(m_links[i].id, info) && info.helloAckSeen)
		{
			++count;
		}
	}
	out.connectedSources = count;
	LeaveCriticalSection(&m_lock);
}

Bool Client::bootstrap(CasterBootstrap::MatchStart& out)
{
	Bool ready;

	EnterCriticalSection(&m_lock);
	ready = m_core.bootstrap(out);
	LeaveCriticalSection(&m_lock);
	return ready;
}

}
