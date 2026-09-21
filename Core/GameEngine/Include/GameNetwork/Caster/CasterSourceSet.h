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

// The caster's source set: connection and subscription state per source,
// source health and age, canonical native frame selection, disagreement
// quarantine, reconnect policy, terminal consensus and the chat/lobby fan-in.
// CasterTransport serializes all calls under its client lock.

#pragma once

#include "Lib/BaseType.h"
#include "GameNetwork/Caster/CasterBootstrap.h"
#include "GameNetwork/Caster/CasterLobby.h"
#include "GameNetwork/Caster/CasterTransportCore.h"
#include "GameNetwork/Caster/CasterCommandFrame.h"

namespace CasterTransportCore
{

static const UnsignedInt MAX_CLIENT_SOURCES = 16;

static const UnsignedInt MAX_GAME_NAME_BYTES = 64;

/// Exponential reconnect backoff for one source link (250 ms doubling to 8 s).
struct ReconnectPolicy
{
	enum { INITIAL_DELAY_MS = 250, MAX_DELAY_MS = 8000 };

	UnsignedInt nextAt;		///< 0 = not armed
	UnsignedInt delayMs;

	void reset() { nextAt = 0; delayMs = INITIAL_DELAY_MS; }

	/// TRUE while the link must keep waiting. Arms the timer on the first call
	/// and disarms it when the wait is over.
	Bool mustWait(UnsignedInt nowMs)
	{
		if (nextAt == 0)
		{
			nextAt = nowMs + delayMs;
			if (delayMs < MAX_DELAY_MS) delayMs *= 2;
		}
		if ((Int)(nowMs - nextAt) < 0)
		{
			return TRUE;
		}
		nextAt = 0;
		return FALSE;
	}
};

/// One consistent read of the source set, taken under a single lock, so the UI
/// never combines values from different moments.
struct SourceSetSnapshot
{
	CasterLobby::LobbySubscription subscription;
	Bool nativeUnavailable;
	Bool hasProgressReport;
	UnsignedInt reportedFrame;
	UnsignedInt reportAgeMs;
	UnsignedInt connectedSources;
};

/**
 * One source link's client-side state. It embeds an assembler, which owns its
 * working buffers on the heap, so this stays small: it is default-constructed
 * and cleared field by field, never memset or copied wholesale.
 */
struct ClientSource
{
	ClientSource();

	/// Returns every field to its inactive value. The assembler is reset but
	/// keeps its buffers for reuse.
	void clear();

	Bool active;
	UnsignedInt id;
	Bool helloAckSeen;
	Bool subscribeSent;
	Bool subscribeMatchRunning;
	Bool subAckSeen;
	UnsignedByte subAckState;
	char gameName[MAX_GAME_NAME_BYTES + 1];
	UnsignedInt gameNameLen;
	Bool advertisedMatchRunning;
	CasterCommandFrame::Assembler commandAssembler;
	Bool commandAssemblyActive;
	UnsignedInt assemblingFrame;
	UnsignedInt assemblingBytes;
	UnsignedInt assemblingChecksum;
	Bool hasCommandProgress;
	UnsignedInt highestCommandFrame;
	Bool nativeEndSeen;
	Bool nativeEndHasFrames;
	UnsignedInt nativeEndFrame;

private:
	ClientSource(const ClientSource&);
	ClientSource& operator=(const ClientSource&);
};

class CasterSourceSet
{
public:
	CasterSourceSet();
	~CasterSourceSet();

	void reset(UnsignedInt gameUid);

	UnsignedInt gameUid() const;

	Bool addSource(UnsignedInt sourceId);
	Bool removeSource(UnsignedInt sourceId);
	Bool rearmSource(UnsignedInt sourceId);
	Bool getSource(UnsignedInt sourceId, ClientSource& out) const;
	Bool acceptsLobby(UnsignedInt sourceId, UnsignedInt gameUid, Bool isHost) const;

	CasterLobby::LobbySubscription subscriptionState() const;

	/// Records that a newer complete, validated command frame just arrived; it
	/// refreshes the live-head age reported by `progressReport`.
	void noteValidatedProgress(UnsignedInt nowMs);
	Bool progressReport(UnsignedInt nowMs, UnsignedInt& frame, UnsignedInt& ageMs) const;

	/// Fills every field except `connectedSources`, which needs link state.
	void snapshot(UnsignedInt nowMs, SourceSetSnapshot& out) const;

	/// Routes a chat or lobby frame to `sink`. TRUE when consumed; anything else
	/// (or a missing sink) still belongs to `onFrame`.
	Bool relaySideband(UnsignedInt sourceId, const CasterProtocol::Frame& frame,
		Bool isHost, ChatSink* sink) const;

	UnsignedInt buildHello(UnsignedByte* out, UnsignedInt cap) const;

	HandleResult onFrame(UnsignedInt sourceId, const CasterProtocol::Frame& frame,
		FrameSink& out);

	Bool tick(UnsignedInt elapsedMs, FrameSink& out);

	/// Copies the reconciled bootstrap. FALSE while none has arrived.
	Bool bootstrap(CasterBootstrap::MatchStart& out) const;

	Bool nativeFrameAvailable(UnsignedInt simulationFrame) const;
	Bool nativeSourcesProgressThrough(UnsignedInt simulationFrame) const;
	UnsignedInt copyNativeFrame(UnsignedInt simulationFrame, UnsignedByte* dest, UnsignedInt capacity) const;
	Bool consumeNativeFrame(UnsignedInt simulationFrame, FrameSink& out);
	UnsignedInt nextNativeFrame() const;
	Bool nativeProgress(UnsignedInt& firstSimulationFrame, UnsignedInt& lastSimulationFrame, UnsignedInt& frameCount) const;
	Bool nativeEnd(Bool& hasFrames, UnsignedInt& finalSimulationFrame) const;
	Bool nativeUnavailable() const;

	/// TRUE once every source that answered was rejected for a protocol version
	/// mismatch and none speaks ours.
	Bool protocolIncompatible() const;

	/// TRUE once a source was quarantined for contradicting another source's
	/// frames and no source remains.
	Bool transportDisagreement() const;
	Bool latestValidatedCommandFrame(UnsignedInt& frame) const;
	Bool requestCommandHistory(UnsignedInt firstSimulationFrame, UnsignedInt frameCount, FrameSink& out);

private:
	ClientSource* findSource(UnsignedInt sourceId);
	const ClientSource* findSource(UnsignedInt sourceId) const;

	void deactivateSource(ClientSource* source);

	Bool acceptCompleteCommandFrame(UnsignedInt sourceId, UnsignedInt simulationFrame, const UnsignedByte* data, UnsignedInt length);
	Bool requestNativeCatchup(FrameSink& out);
	void recomputeNativeEnd();
	void clearNativeFrames();

	struct NativeFrame
	{
		UnsignedInt simulationFrame;
		UnsignedInt length;
		UnsignedInt checksum;
		UnsignedByte* data;
	};
	enum { NATIVE_FRAME_SLOTS = 64 };

	UnsignedInt m_gameUid;
	/// The first accepted match-start bootstrap and its exact wire body; every
	/// later source must publish the same bytes or it is quarantined.
	Bool m_haveBootstrap;
	CasterBootstrap::MatchStart m_bootstrap;
	UnsignedByte m_bootstrapBody[CasterProtocol::MAX_FRAME_BYTES];
	UnsignedInt m_bootstrapBodyLen;
	ClientSource m_sources[MAX_CLIENT_SOURCES];
	UnsignedInt m_count;
	NativeFrame m_nativeFrames[NATIVE_FRAME_SLOTS];
	UnsignedInt m_nativeFrameCount;
	Bool m_nativeEndSeen;
	Bool m_nativeHasFrames;
	UnsignedInt m_nativeFinalFrame;
	UnsignedInt m_nextNativeFrame;
	UnsignedInt m_lastNativeHistoryRequest;
	UnsignedInt m_nativeHistoryRequestAgeMs;
	UnsignedInt m_noSourcesMs;
	Bool m_nativeUnavailable;
	Bool m_protocolMismatchSeen;
	Bool m_compatibleSourceSeen;
	Bool m_disagreementSeen;
	Bool m_haveValidatedCommandFrame;
	UnsignedInt m_latestValidatedCommandFrame;
	Bool m_haveProgressReport;
	UnsignedInt m_reportedFrame;
	UnsignedInt m_progressReceivedMs;

	CasterSourceSet(const CasterSourceSet&);
	CasterSourceSet& operator=(const CasterSourceSet&);
};

}
