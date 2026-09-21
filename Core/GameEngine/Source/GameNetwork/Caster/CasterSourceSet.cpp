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

#include "GameNetwork/Caster/CasterSourceSet.h"

namespace CasterTransportCore
{

using namespace CasterProtocol;

ClientSource::ClientSource()
{
	clear();
}

void ClientSource::clear()
{
	active = FALSE;
	id = INVALID_ID;
	helloAckSeen = FALSE;
	subscribeSent = FALSE;
	subscribeMatchRunning = FALSE;
	subAckSeen = FALSE;
	subAckState = CasterSubscribeTable::ACK_IN_PROGRESS;
	memset(gameName, 0, sizeof(gameName));
	gameNameLen = 0;
	advertisedMatchRunning = FALSE;
	commandAssembler.reset();
	commandAssemblyActive = FALSE;
	assemblingFrame = assemblingBytes = assemblingChecksum = 0;
	hasCommandProgress = FALSE;
	highestCommandFrame = 0;
	nativeEndSeen = FALSE;
	nativeEndHasFrames = FALSE;
	nativeEndFrame = 0;
}

CasterSourceSet::CasterSourceSet()
{
	UnsignedInt i;
	m_gameUid = 0;
	m_count = 0;
	m_nativeFrameCount = 0;
	m_nativeEndSeen = FALSE;
	m_nativeHasFrames = FALSE;
	m_nativeFinalFrame = 0;
	m_nextNativeFrame = 0;
	m_lastNativeHistoryRequest = INVALID_ID;
	m_nativeHistoryRequestAgeMs = 0;
	m_noSourcesMs = 0;
	m_nativeUnavailable = FALSE;
	m_protocolMismatchSeen = FALSE;
	m_compatibleSourceSeen = FALSE;
	m_disagreementSeen = FALSE;
	m_haveValidatedCommandFrame = FALSE;
	m_latestValidatedCommandFrame = 0;
	for (i = 0; i < NATIVE_FRAME_SLOTS; ++i)
	{
		m_nativeFrames[i].data = nullptr;
	}
	reset(0);
}

CasterSourceSet::~CasterSourceSet()
{
	clearNativeFrames();
}

void CasterSourceSet::deactivateSource(ClientSource* source)
{
	if (source == nullptr || !source->active)
	{
		return;
	}
	source->clear();
	--m_count;
}

void CasterSourceSet::reset(UnsignedInt gameUid)
{
	UnsignedInt i;

	m_gameUid = gameUid;
	m_haveBootstrap = FALSE;
	m_bootstrapBodyLen = 0;
	memset(m_bootstrapBody, 0, sizeof(m_bootstrapBody));
	CasterBootstrap::clear(m_bootstrap);
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		deactivateSource(&m_sources[i]);
		m_sources[i].id = INVALID_ID;
	}
	m_count = 0;
	clearNativeFrames();
	m_nativeEndSeen = FALSE;
	m_nativeHasFrames = FALSE;
	m_nativeFinalFrame = 0;
	m_nextNativeFrame = 0;
	m_lastNativeHistoryRequest = INVALID_ID;
	m_nativeHistoryRequestAgeMs = 0;
	m_noSourcesMs = 0;
	m_nativeUnavailable = FALSE;
	m_protocolMismatchSeen = FALSE;
	m_compatibleSourceSeen = FALSE;
	m_disagreementSeen = FALSE;
	m_haveValidatedCommandFrame = FALSE;
	m_latestValidatedCommandFrame = 0;
	m_haveProgressReport = FALSE;
	m_reportedFrame = 0;
	m_progressReceivedMs = 0;
}

UnsignedInt CasterSourceSet::gameUid() const
{
	return m_gameUid;
}

ClientSource* CasterSourceSet::findSource(UnsignedInt sourceId)
{
	UnsignedInt i;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_sources[i].active && m_sources[i].id == sourceId)
		{
			return &m_sources[i];
		}
	}
	return nullptr;
}

const ClientSource* CasterSourceSet::findSource(UnsignedInt sourceId) const
{
	UnsignedInt i;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_sources[i].active && m_sources[i].id == sourceId)
		{
			return &m_sources[i];
		}
	}
	return nullptr;
}

Bool CasterSourceSet::addSource(UnsignedInt sourceId)
{
	ClientSource* free = nullptr;
	UnsignedInt i;

	if (sourceId == INVALID_ID || findSource(sourceId) != nullptr)
	{
		return FALSE;
	}
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (!m_sources[i].active)
		{
			free = &m_sources[i];
			break;
		}
	}
	if (free == nullptr)
	{
		return FALSE;
	}

	free->active = TRUE;
	free->id = sourceId;
	free->helloAckSeen = FALSE;
	free->subscribeSent = FALSE;
	free->subscribeMatchRunning = FALSE;
	free->subAckSeen = FALSE;
	free->subAckState = CasterSubscribeTable::ACK_IN_PROGRESS;
	free->gameName[0] = 0;
	free->gameNameLen = 0;
	free->advertisedMatchRunning = FALSE;
	free->commandAssembler.reset();
	free->commandAssemblyActive = FALSE;
	free->assemblingFrame = free->assemblingBytes = free->assemblingChecksum = 0;
	free->hasCommandProgress = FALSE;
	free->highestCommandFrame = 0;
	free->nativeEndSeen = FALSE;
	free->nativeEndHasFrames = FALSE;
	free->nativeEndFrame = 0;
	++m_count;
	return TRUE;
}

Bool CasterSourceSet::removeSource(UnsignedInt sourceId)
{
	ClientSource* source = findSource(sourceId);

	if (source == nullptr)
	{
		return FALSE;
	}
	deactivateSource(source);
	recomputeNativeEnd();
	return TRUE;
}

void CasterSourceSet::recomputeNativeEnd()
{
	UnsignedInt i;
	Bool haveEligible = FALSE;
	Bool allEligibleEnded = TRUE;
	Bool hasFrames = FALSE;
	UnsignedInt finalFrame = 0;

	// A command-stream END only speaks for sources which completed the
	// subscription handshake.  Pending links may never carry native data and
	// must not hold a completed stream open.  Once all eligible sources report
	// END, retain that terminal evidence even if their sockets are reaped.
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (!m_sources[i].active || !m_sources[i].subAckSeen
			|| m_sources[i].subAckState != CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			continue;
		}
		haveEligible = TRUE;
		if (!m_sources[i].nativeEndSeen)
		{
			allEligibleEnded = FALSE;
			continue;
		}
		if (m_sources[i].nativeEndHasFrames)
		{
			hasFrames = TRUE;
			if (m_sources[i].nativeEndFrame > finalFrame) finalFrame = m_sources[i].nativeEndFrame;
		}
	}

	if (haveEligible)
	{
		// A newly eligible peer which has not ended is live evidence that a
		// previously retained terminal result is stale.  Only retain terminal
		// evidence while every current eligible peer agrees, or after all have
		// disconnected.
		m_nativeEndSeen = allEligibleEnded;
		if (allEligibleEnded)
		{
			m_nativeHasFrames = hasFrames;
			m_nativeFinalFrame = finalFrame;
		}
	}
}

Bool CasterSourceSet::acceptsLobby(UnsignedInt sourceId, UnsignedInt uid, Bool isHost) const
{
	const ClientSource* source = findSource(sourceId);
	return isHost && uid == gameUid() && source != nullptr && source->subAckSeen
		&& (source->subAckState == CasterSubscribeTable::ACK_IN_PROGRESS
			|| source->subAckState == CasterSubscribeTable::ACK_STREAMING_ELIGIBLE) ? TRUE : FALSE;
}

Bool CasterSourceSet::getSource(UnsignedInt sourceId, ClientSource& out) const
{
	const ClientSource* source = findSource(sourceId);

	if (source == nullptr)
	{
		return FALSE;
	}
	// `out` owns an assembler, so it is cleared field by field rather than
	// memset: a wholesale overwrite would leak and corrupt its buffers.
	out.clear();
	out.active = source->active;
	out.id = source->id;
	out.helloAckSeen = source->helloAckSeen;
	out.subscribeSent = source->subscribeSent;
	out.subscribeMatchRunning = source->subscribeMatchRunning;
	out.subAckSeen = source->subAckSeen;
	out.subAckState = source->subAckState;
	out.gameNameLen = source->gameNameLen;
	out.advertisedMatchRunning = source->advertisedMatchRunning;
	memcpy(out.gameName, source->gameName, sizeof(out.gameName));
	out.hasCommandProgress = source->hasCommandProgress;
	out.highestCommandFrame = source->highestCommandFrame;
	out.nativeEndSeen = source->nativeEndSeen;
	out.nativeEndHasFrames = source->nativeEndHasFrames;
	out.nativeEndFrame = source->nativeEndFrame;
	return TRUE;
}

Bool CasterSourceSet::rearmSource(UnsignedInt sourceId)
{
	ClientSource* source = findSource(sourceId);
	if (source == nullptr) return FALSE;
	source->helloAckSeen = FALSE;
	source->subscribeSent = FALSE;
	source->subscribeMatchRunning = FALSE;
	source->subAckSeen = FALSE;
	source->subAckState = CasterSubscribeTable::ACK_IN_PROGRESS;
	source->commandAssembler.reset();
	source->commandAssemblyActive = FALSE;
	return TRUE;
}

CasterLobby::LobbySubscription CasterSourceSet::subscriptionState() const
{
	UnsignedInt i;
	Bool anyLinked = FALSE;
	Bool anySent = FALSE;
	Bool anyAcked = FALSE;

	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (!m_sources[i].active)
		{
			continue;
		}
		anyLinked = TRUE;
		if (m_sources[i].subAckSeen)
		{
			anyAcked = TRUE;
		}
		if (m_sources[i].subscribeSent)
		{
			anySent = TRUE;
		}
	}

	if (anyAcked)
	{
		return CasterLobby::LOBBY_SUB_ACTIVE;
	}
	if (anySent)
	{
		return CasterLobby::LOBBY_SUB_PENDING;
	}
	if (anyLinked)
	{
		return CasterLobby::LOBBY_SUB_UNSENT;
	}
	return CasterLobby::LOBBY_SUB_NONE;
}

void CasterSourceSet::noteValidatedProgress(UnsignedInt nowMs)
{
	UnsignedInt validatedFrame = 0;

	if (latestValidatedCommandFrame(validatedFrame)
		&& (!m_haveProgressReport || validatedFrame > m_reportedFrame))
	{
		m_haveProgressReport = TRUE;
		m_reportedFrame = validatedFrame;
		m_progressReceivedMs = nowMs;
	}
}

Bool CasterSourceSet::progressReport(UnsignedInt nowMs, UnsignedInt& frame, UnsignedInt& ageMs) const
{
	frame = m_reportedFrame;
	ageMs = m_haveProgressReport ? nowMs - m_progressReceivedMs : 0;
	return m_haveProgressReport;
}

void CasterSourceSet::snapshot(UnsignedInt nowMs, SourceSetSnapshot& out) const
{
	out.subscription = subscriptionState();
	out.nativeUnavailable = m_nativeUnavailable;
	out.hasProgressReport = progressReport(nowMs, out.reportedFrame, out.reportAgeMs);
	out.connectedSources = 0;
}

Bool CasterSourceSet::relaySideband(UnsignedInt sourceId, const Frame& frame,
	Bool isHost, ChatSink* sink) const
{
	if (sink == nullptr)
	{
		return FALSE;
	}
	if (frame.type == FRAME_CHAT)
	{
		sink->onChat(frame.gameUid, frame.commandID, frame.direction, frame.senderSlot,
			frame.isSenderCaster, frame.senderTeam, frame.senderIdentity,
			frame.senderName, frame.senderNameLen, frame.text, frame.textLen, sourceId,
			frame.isEmote);
		return TRUE;
	}
	if (frame.type == FRAME_LOBBY)
	{
		if (acceptsLobby(sourceId, frame.gameUid, isHost))
		{
			sink->onLobbyState(frame.gameUid, frame.text, frame.textLen, sourceId);
		}
		return TRUE;
	}
	return FALSE;
}

UnsignedInt CasterSourceSet::buildHello(UnsignedByte* out, UnsignedInt cap) const
{
	return encodeHello(out, cap);
}

HandleResult CasterSourceSet::onFrame(UnsignedInt sourceId, const Frame& frame, FrameSink& out)
{
	ClientSource* source = findSource(sourceId);

	if (source == nullptr)
	{
		return HANDLE_IGNORED;
	}

	switch (frame.type)
	{
	case FRAME_HELLO_ACK:
	{
		UnsignedInt copyLen;
		UnsignedByte subscribe[MAX_FRAME_BYTES];
		UnsignedInt subscribeLen;
		Bool refreshSubscribe;
		Bool advertisedMatchRunning;

		if (frame.protoVer != PROTOCOL_VERSION)
		{
			m_protocolMismatchSeen = TRUE;
			return HANDLE_REJECTED;
		}
		m_compatibleSourceSeen = TRUE;

		copyLen = frame.gameNameLen;
		if (copyLen > MAX_GAME_NAME_BYTES)
		{
			copyLen = MAX_GAME_NAME_BYTES;
		}
		if (copyLen != 0 && frame.gameName != nullptr)
		{
			memcpy(source->gameName, frame.gameName, copyLen);
		}
		source->gameName[copyLen] = 0;
		source->gameNameLen = copyLen;
		source->helloAckSeen = TRUE;

		// A host announces its lobby game before the match starts.  If that
		// subscription was acknowledged in progress, re-send once when the host
		// starts reporting the match running, so the server promotes this link.
		advertisedMatchRunning = (frame.matchRunning != 0) ? TRUE : FALSE;
		refreshSubscribe = source->subscribeSent && source->subAckSeen
			&& source->subAckState == CasterSubscribeTable::ACK_IN_PROGRESS
			&& source->subscribeMatchRunning != advertisedMatchRunning;
		source->advertisedMatchRunning = advertisedMatchRunning;

		if (!source->subscribeSent || refreshSubscribe)
		{
			subscribeLen = encodeSubscribe(subscribe, sizeof(subscribe), m_gameUid);
			if (subscribeLen != 0 && out.emit(sourceId, subscribe, subscribeLen))
			{
				source->subscribeSent = TRUE;
				source->subscribeMatchRunning = advertisedMatchRunning;
				if (refreshSubscribe)
				{
					source->subAckSeen = FALSE;
					source->subAckState = CasterSubscribeTable::ACK_IN_PROGRESS;
				}
			}
		}
		return HANDLE_REPLIED;
	}

	case FRAME_SUB_ACK:
		if (frame.gameUid != m_gameUid)
		{
			return HANDLE_IGNORED;
		}
		source->subAckSeen = TRUE;
		source->subAckState = frame.modeOrState;
		if (source->subAckState == CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			recomputeNativeEnd();
			m_lastNativeHistoryRequest = INVALID_ID;
			requestNativeCatchup(out);
		}
		return HANDLE_ACCEPTED;

	case FRAME_BOOTSTRAP:
	{
		CasterBootstrap::MatchStart start;

		if (frame.gameUid != m_gameUid || !source->subAckSeen
			|| source->subAckState != CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			return HANDLE_IGNORED;
		}
		if (frame.bytes == nullptr || frame.length == 0
			|| frame.length > sizeof(m_bootstrapBody))
		{
			return HANDLE_REJECTED;
		}
		if (m_haveBootstrap)
		{
			// Every player publishes the same match start. A source that
			// contradicts the accepted one is quarantined, exactly like a source
			// that contradicts an accepted command frame.
			if (m_bootstrapBodyLen != frame.length
				|| memcmp(m_bootstrapBody, frame.bytes, frame.length) != 0)
			{
				m_disagreementSeen = TRUE;
				return HANDLE_REJECTED;
			}
			return HANDLE_ACCEPTED;
		}
		if (!decodeBootstrapBody(frame.gameUid, frame.bytes, frame.length, start))
		{
			return HANDLE_REJECTED;
		}
		memcpy(m_bootstrapBody, frame.bytes, frame.length);
		m_bootstrapBodyLen = frame.length;
		m_bootstrap = start;
		m_haveBootstrap = TRUE;
		return HANDLE_ACCEPTED;
	}

	case FRAME_COMMAND_FRAGMENT:
		if (frame.gameUid != m_gameUid || !source->subAckSeen
			|| source->subAckState != CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			return HANDLE_IGNORED;
		}
		if (!source->commandAssemblyActive)
		{
			if (!source->commandAssembler.begin(frame.simulationFrame, frame.totalBytes, frame.checksum))
			{
				return HANDLE_REJECTED;
			}
			source->commandAssemblyActive = TRUE;
			source->assemblingFrame = frame.simulationFrame;
			source->assemblingBytes = frame.totalBytes;
			source->assemblingChecksum = frame.checksum;
		}
		else if (source->assemblingFrame != frame.simulationFrame || source->assemblingBytes != frame.totalBytes || source->assemblingChecksum != frame.checksum)
		{
			return HANDLE_REJECTED;
		}
		if (!source->commandAssembler.append(frame.fragmentOffset, frame.bytes, frame.fragmentLength))
		{
			source->commandAssembler.reset();
			source->commandAssemblyActive = FALSE;
			return HANDLE_REJECTED;
		}
		if (source->commandAssembler.isComplete())
		{
			Bool inWindow = frame.simulationFrame >= m_nextNativeFrame
				&& frame.simulationFrame - m_nextNativeFrame < NATIVE_FRAME_SLOTS;
			Bool valid = CasterCommandFrame::validate(source->commandAssembler.data(),
				source->commandAssembler.length(), frame.simulationFrame);
			Bool accepted = valid && acceptCompleteCommandFrame(sourceId, frame.simulationFrame,
				source->commandAssembler.data(), source->commandAssembler.length());
			source->commandAssembler.reset();
			source->commandAssemblyActive = FALSE;
			if (valid && (!m_haveValidatedCommandFrame || frame.simulationFrame > m_latestValidatedCommandFrame))
			{
				m_haveValidatedCommandFrame = TRUE;
				m_latestValidatedCommandFrame = frame.simulationFrame;
			}
			if (accepted && (!source->hasCommandProgress || frame.simulationFrame > source->highestCommandFrame))
			{
				source->hasCommandProgress = TRUE;
				source->highestCommandFrame = frame.simulationFrame;
			}
			if (!accepted)
			{
				if (!valid) return HANDLE_REJECTED;
				if (inWindow) return HANDLE_REJECTED;
				requestNativeCatchup(out);
				return HANDLE_ACCEPTED;
			}
			return HANDLE_ACCEPTED;
		}
		return HANDLE_ACCEPTED;

	case FRAME_COMMAND_END:
		if (frame.gameUid != m_gameUid || !source->subAckSeen
			|| source->subAckState != CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			return HANDLE_IGNORED;
		}
		if (source->nativeEndSeen && (source->nativeEndHasFrames != (frame.hasFrames != 0)
			|| source->nativeEndFrame != frame.simulationFrame))
		{
			m_disagreementSeen = TRUE;
			return HANDLE_REJECTED;
		}
		source->nativeEndSeen = TRUE;
		source->nativeEndHasFrames = (frame.hasFrames != 0) ? TRUE : FALSE;
		source->nativeEndFrame = frame.simulationFrame;
		recomputeNativeEnd();
		return HANDLE_ACCEPTED;

	default:
		return HANDLE_IGNORED;
	}
}

Bool CasterSourceSet::tick(UnsignedInt elapsedMs, FrameSink& out)
{
	Bool sent = FALSE;
	Bool anyEligible = FALSE;
	UnsignedInt sourceIndex;
	for (sourceIndex = 0; sourceIndex < MAX_CLIENT_SOURCES; ++sourceIndex)
	{
		if (m_sources[sourceIndex].active && m_sources[sourceIndex].subAckSeen
			&& m_sources[sourceIndex].subAckState == CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			anyEligible = TRUE;
			break;
		}
	}
	if (!anyEligible && !m_nativeEndSeen)
	{
		m_noSourcesMs += elapsedMs;
		if (m_noSourcesMs >= 5000) m_nativeUnavailable = TRUE;
	}
	else
	{
		m_noSourcesMs = 0;
		m_nativeUnavailable = FALSE;
	}
	if (m_lastNativeHistoryRequest != INVALID_ID)
	{
		m_nativeHistoryRequestAgeMs += elapsedMs;
		if (m_nativeHistoryRequestAgeMs >= 1000)
		{
			m_lastNativeHistoryRequest = INVALID_ID;
			m_nativeHistoryRequestAgeMs = 0;
			if (requestNativeCatchup(out)) sent = TRUE;
		}
	}

	return sent;
}

Bool CasterSourceSet::bootstrap(CasterBootstrap::MatchStart& out) const
{
	if (!m_haveBootstrap)
	{
		return FALSE;
	}
	out = m_bootstrap;
	return TRUE;
}

void CasterSourceSet::clearNativeFrames()
{
	UnsignedInt i;
	for (i = 0; i < NATIVE_FRAME_SLOTS; ++i)
	{
		delete[] m_nativeFrames[i].data;
		m_nativeFrames[i].data = nullptr;
		m_nativeFrames[i].simulationFrame = 0;
		m_nativeFrames[i].length = 0;
		m_nativeFrames[i].checksum = 0;
	}
	m_nativeFrameCount = 0;
}

Bool CasterSourceSet::acceptCompleteCommandFrame(UnsignedInt sourceId, UnsignedInt simulationFrame, const UnsignedByte* data, UnsignedInt length)
{
	UnsignedInt i;
	NativeFrame* slot = nullptr;
	(void)sourceId;
	if (data == nullptr || length == 0 || length > CasterProtocol::MAX_COMMAND_FRAME_BYTES)
	{
		return FALSE;
	}
	if (simulationFrame < m_nextNativeFrame || simulationFrame - m_nextNativeFrame >= NATIVE_FRAME_SLOTS)
	{
		return FALSE;
	}
	for (i = 0; i < m_nativeFrameCount; ++i)
	{
		if (m_nativeFrames[i].simulationFrame == simulationFrame)
		{
			if (m_nativeFrames[i].length == length && memcmp(m_nativeFrames[i].data, data, length) == 0)
			{
				return TRUE;
			}
			// Two sources sent different bytes for the same frame; the rejected
			// link is quarantined by the transport.
			m_disagreementSeen = TRUE;
			return FALSE;
		}
	}
	if (m_nativeFrameCount == NATIVE_FRAME_SLOTS) return FALSE;
	slot = &m_nativeFrames[m_nativeFrameCount];
	slot->data = new UnsignedByte[length];
	if (slot->data == nullptr)
	{
		return FALSE;
	}
	memcpy(slot->data, data, length);
	slot->simulationFrame = simulationFrame;
	slot->length = length;
	slot->checksum = CasterCommandFrame::checksum(data, length);
	++m_nativeFrameCount;
	return TRUE;
}

Bool CasterSourceSet::nativeFrameAvailable(UnsignedInt simulationFrame) const
{
	UnsignedInt i;
	for (i = 0; i < m_nativeFrameCount; ++i)
	{
		if (m_nativeFrames[i].simulationFrame == simulationFrame) return TRUE;
	}
	return FALSE;
}

Bool CasterSourceSet::nativeSourcesProgressThrough(UnsignedInt simulationFrame) const
{
	UnsignedInt i;
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_sources[i].active && m_sources[i].hasCommandProgress
			&& m_sources[i].highestCommandFrame >= simulationFrame) return TRUE;
	}
	return FALSE;
}

UnsignedInt CasterSourceSet::copyNativeFrame(UnsignedInt simulationFrame, UnsignedByte* dest, UnsignedInt capacity) const
{
	UnsignedInt i;
	if (dest == nullptr) return 0;
	for (i = 0; i < m_nativeFrameCount; ++i)
	{
		if (m_nativeFrames[i].simulationFrame == simulationFrame && capacity >= m_nativeFrames[i].length)
		{
			memcpy(dest, m_nativeFrames[i].data, m_nativeFrames[i].length);
			return m_nativeFrames[i].length;
		}
	}
	return 0;
}

Bool CasterSourceSet::consumeNativeFrame(UnsignedInt simulationFrame, FrameSink& out)
{
	UnsignedInt i;
	if (simulationFrame != m_nextNativeFrame) return FALSE;
	for (i = 0; i < m_nativeFrameCount; ++i)
	{
		if (m_nativeFrames[i].simulationFrame == simulationFrame)
		{
			delete[] m_nativeFrames[i].data;
			if (i + 1 < m_nativeFrameCount)
			{
				memmove(m_nativeFrames + i, m_nativeFrames + i + 1, sizeof(NativeFrame) * (m_nativeFrameCount - i - 1));
			}
			--m_nativeFrameCount;
			m_nativeFrames[m_nativeFrameCount].data = nullptr;
			m_nativeFrames[m_nativeFrameCount].simulationFrame = 0;
			m_nativeFrames[m_nativeFrameCount].length = 0;
			m_nativeFrames[m_nativeFrameCount].checksum = 0;
			++m_nextNativeFrame;
			if (!nativeFrameAvailable(m_nextNativeFrame))
			{
				m_lastNativeHistoryRequest = INVALID_ID;
				m_nativeHistoryRequestAgeMs = 0;
				requestNativeCatchup(out);
			}
			return TRUE;
		}
	}
	return FALSE;
}

UnsignedInt CasterSourceSet::nextNativeFrame() const
{
	return m_nextNativeFrame;
}

Bool CasterSourceSet::requestNativeCatchup(FrameSink& out)
{
	if (m_lastNativeHistoryRequest == m_nextNativeFrame) return FALSE;
	m_lastNativeHistoryRequest = m_nextNativeFrame;
	if (requestCommandHistory(m_nextNativeFrame, CasterProtocol::MAX_COMMAND_HISTORY_FRAMES, out))
	{
		m_nativeHistoryRequestAgeMs = 0;
		return TRUE;
	}
	return FALSE;
}

Bool CasterSourceSet::nativeProgress(UnsignedInt& firstSimulationFrame, UnsignedInt& lastSimulationFrame, UnsignedInt& frameCount) const
{
	UnsignedInt i;
	if (m_nativeFrameCount == 0) return FALSE;
	firstSimulationFrame = m_nativeFrames[0].simulationFrame;
	lastSimulationFrame = firstSimulationFrame;
	for (i = 1; i < m_nativeFrameCount; ++i)
	{
		if (m_nativeFrames[i].simulationFrame < firstSimulationFrame) firstSimulationFrame = m_nativeFrames[i].simulationFrame;
		if (m_nativeFrames[i].simulationFrame > lastSimulationFrame) lastSimulationFrame = m_nativeFrames[i].simulationFrame;
	}
	frameCount = m_nativeFrameCount;
	return TRUE;
}

Bool CasterSourceSet::nativeEnd(Bool& hasFrames, UnsignedInt& finalSimulationFrame) const
{
	if (!m_nativeEndSeen) return FALSE;
	hasFrames = m_nativeHasFrames;
	finalSimulationFrame = m_nativeFinalFrame;
	return TRUE;
}

Bool CasterSourceSet::nativeUnavailable() const
{
	return m_nativeUnavailable;
}

Bool CasterSourceSet::protocolIncompatible() const
{
	return m_protocolMismatchSeen && !m_compatibleSourceSeen && m_count == 0;
}

Bool CasterSourceSet::transportDisagreement() const
{
	return m_disagreementSeen && m_count == 0;
}

Bool CasterSourceSet::latestValidatedCommandFrame(UnsignedInt& frame) const
{
	if (!m_haveValidatedCommandFrame) return FALSE;
	frame = m_latestValidatedCommandFrame;
	return TRUE;
}

Bool CasterSourceSet::requestCommandHistory(UnsignedInt firstSimulationFrame, UnsignedInt frameCount, FrameSink& out)
{
	UnsignedByte request[MAX_FRAME_BYTES];
	UnsignedInt requestLength;
	UnsignedInt i;
	Bool emitted = FALSE;
	requestLength = encodeCommandHistoryReq(request, sizeof(request), m_gameUid, firstSimulationFrame, frameCount);
	if (requestLength == 0) return FALSE;
	for (i = 0; i < MAX_CLIENT_SOURCES; ++i)
	{
		if (m_sources[i].active && m_sources[i].subAckSeen
			&& m_sources[i].subAckState == CasterSubscribeTable::ACK_STREAMING_ELIGIBLE)
		{
			if (out.emit(m_sources[i].id, request, requestLength)) emitted = TRUE;
		}
	}
	return emitted;
}

}
