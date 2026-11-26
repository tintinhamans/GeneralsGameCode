/*
**	Command & Conquer Generals Zero Hour(tm)
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

#include "Common/LocalApiReaders.h"

#include "Common/GlobalData.h"
#include "LocalApi.pb.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// NetworkLog
#include "GameNetwork/GenOnline/GenOnlineLocalApiPipe.h"
#include "GameNetwork/GenOnline/GenOnlineRealtimeBridge.h"
#include "GameNetwork/GenOnline/GenOnlineLobbyInterface.h"
#include "GameNetwork/GenOnline/GenOnlineInit.h"
#include "GameNetwork/GenOnline/GenOnlineAuth.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameLogic/GameLogic.h"
#include "Common/LocalApiControl.h"

#include <deque>

namespace
{
	struct ExporterClock
	{
		LARGE_INTEGER frequency;
		LARGE_INTEGER start;
		UnsignedInt64 epochUnixMs;

		ExporterClock()
		{
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&start);
			if (frequency.QuadPart <= 0)
			{
				frequency.QuadPart = 1000000;
			}

			FILETIME fileTime;
			GetSystemTimeAsFileTime(&fileTime);
			ULARGE_INTEGER hundredNanos;
			hundredNanos.LowPart = fileTime.dwLowDateTime;
			hundredNanos.HighPart = fileTime.dwHighDateTime;
			epochUnixMs = ((UnsignedInt64)hundredNanos.QuadPart - (UnsignedInt64)116444736000000000) / 10000;
		}
	};

	ExporterClock& exporterClock()
	{
		static ExporterClock s_clock;
		return s_clock;
	}


	Bool readVarint(const UnsignedByte*& cursor, const UnsignedByte* end, UnsignedInt64& value)
	{
		value = 0;
		for (Int shift = 0; shift < 64 && cursor < end; shift += 7)
		{
			const UnsignedByte b = *cursor++;
			value |= (UnsignedInt64)(b & 0x7F) << shift;
			if ((b & 0x80) == 0)
			{
				return TRUE;
			}
		}
		return FALSE;
	}

	// Lets a refusal name its request without a full decode.
	UnsignedInt extractRequestId(const UnsignedByte* body, UnsignedInt length)
	{
		const UnsignedByte* cursor = body;
		const UnsignedByte* end = body + length;

		while (cursor < end)
		{
			UnsignedInt64 tag;
			UnsignedInt64 value;

			if (!readVarint(cursor, end, tag))
			{
				return 0;
			}
			const UnsignedInt field = (UnsignedInt)(tag >> 3);
			const UnsignedInt wire = (UnsignedInt)(tag & 7);
			if (wire == 0)
			{
				if (!readVarint(cursor, end, value))
				{
					return 0;
				}
				if (field == 1)
				{
					return (UnsignedInt)value;
				}
			}
			else if (wire == 1)
			{
				cursor += 8;
			}
			else if (wire == 2)
			{
				if (!readVarint(cursor, end, value) || value > (UnsignedInt64)(end - cursor))
				{
					return 0;
				}
				cursor += (size_t)value;
			}
			else if (wire == 5)
			{
				cursor += 4;
			}
			else
			{
				return 0;
			}
		}
		return 0;
	}

	GenOnlineLocalApi::FrameClass frameClassOf(UnsignedByte frameType)
	{
		switch (frameType)
		{
			case LOCAL_API_FRAME_HELLO:
				return GenOnlineLocalApi::FrameClass::Hello;
			case LOCAL_API_FRAME_STATS_SNAPSHOT:
			case LOCAL_API_FRAME_EVENTS:
			case LOCAL_API_FRAME_CAMERA:
			case LOCAL_API_FRAME_VIEW_STATS:
			case LOCAL_API_FRAME_INTEREST_MAP:
				return GenOnlineLocalApi::FrameClass::Periodic;
			default:
				return GenOnlineLocalApi::FrameClass::Lifecycle;
		}
	}

	const char* const s_permissionNames[LOCAL_API_PERMISSION_COUNT] =
	{
		"local_api.match",
		"local_api.stats",
		"local_api.events",
		"local_api.interest",
		"local_api.camera.read",
		"local_api.chat.read",
		"local_api.chat.send",
		"local_api.query",
		"local_api.camera.control",
		"local_api.replay.control",
		"local_api.observe",
		"local_api.lobby.read",
		"local_api.lobby.control",
	};

	const UnsignedInt ALL_PERMISSIONS = (1u << LOCAL_API_PERMISSION_COUNT) - 1;

	/// The permission an outbound frame type needs, or -1 for Hello, Response and AccessState, which always pass.
	Int framePermission(UnsignedByte frameType)
	{
		switch (frameType)
		{
			case LOCAL_API_FRAME_MATCH_INFO:
			case LOCAL_API_FRAME_MATCH_SUMMARY:
			case LOCAL_API_FRAME_UI_STATE:
				return LOCAL_API_PERMISSION_MATCH;
			case LOCAL_API_FRAME_STATS_SNAPSHOT:
			case LOCAL_API_FRAME_VIEW_STATS:
				return LOCAL_API_PERMISSION_STATS;
			case LOCAL_API_FRAME_EVENTS:
				return LOCAL_API_PERMISSION_EVENTS;
			case LOCAL_API_FRAME_INTEREST_MAP:
				return LOCAL_API_PERMISSION_INTEREST;
			case LOCAL_API_FRAME_CAMERA:
				return LOCAL_API_PERMISSION_CAMERA_READ;
			case LOCAL_API_FRAME_CHAT_MESSAGE:
				return LOCAL_API_PERMISSION_CHAT_READ;
			case LOCAL_API_FRAME_LOBBY_STATE:
				return LOCAL_API_PERMISSION_LOBBY_READ;
			default:
				return -1;
		}
	}

	/// LocalApiSink over the local API pipe. The pipe owns the readers and their queues, and this class keeps the slot table of
	/// the readers, the feed configs, the access rules and the decoding of inbound frames. The slot table is guarded by a
	/// critical section because isConnected, getReaders and enqueueFrame run on any thread. Reader events and inbound frames are
	/// taken on the main thread only.
	class PipeLocalApiSink : public LocalApiSink
	{
	public:
		static PipeLocalApiSink& TheInstance()
		{
			static PipeLocalApiSink s_instance;
			return s_instance;
		}

		Bool start()
		{
			if (m_stopped || m_startFailed)
			{
				return FALSE;
			}
			if (!m_listening)
			{
				if (!GenOnlineServicesManager::Settings.GetLocalApiEnabled())
				{
					return FALSE;
				}
				m_listening = m_server.Start((uint32_t)LOCAL_API_MAX_READERS);
				if (!m_listening)
				{
					m_startFailed = TRUE;
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[LocalApi] ERROR: The pipe could not start, the live stats readers stay off");
					return FALSE;
				}
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[LocalApi] Listening on %s", GenOnlineLocalApi::PipeName().c_str());
			}
			return m_listening;
		}

		Bool permitted(LocalApiPermission permission) const
		{
			return (m_permitted & (1u << permission)) != 0;
		}

		void shutdown()
		{
			m_stopped = TRUE;
			m_listening = FALSE;
			m_server.Stop();
			m_chats.clear();
			m_requests.clear();
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				m_slots[i] = Slot();
				m_feedEpoch[i] = 0;
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual Bool isConnected() const
		{
			return m_listening && m_allowed && m_table.IsConnected();
		}

		virtual void getReaders(LocalApiReaders& out) const
		{
			collectReaders(TRUE, out);
		}

		virtual void getUnwelcomedReaders(LocalApiReaders& out) const
		{
			refreshAccess();
			pumpReaderEvents();
			collectReaders(FALSE, out);
		}

		virtual void markWelcomed(const LocalApiReaders& readers)
		{
			if (!m_listening)
			{
				return;
			}
			pumpReaderEvents();
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				if (m_slots[i].readerId != 0 && readers.epoch(i) == m_slots[i].epoch)
				{
					m_slots[i].welcomed = TRUE;
				}
			}
			LeaveCriticalSection(&m_lock);
			sendAccessState(readers);
		}

		virtual Bool enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const LocalApiReaders& to)
		{
			if (!m_listening || body == NULL || bodyLength == 0 || bodyLength > LOCAL_API_MAX_BODY_BYTES)
			{
				return FALSE;
			}
			// A closed pipe still greets a reader and tells it why nothing else arrives.
			if (!m_allowed && frameType != LOCAL_API_FRAME_HELLO && frameType != LOCAL_API_FRAME_ACCESS_STATE)
			{
				return FALSE;
			}
			const Int permission = framePermission(frameType);
			if (permission >= 0 && !permitted((LocalApiPermission)permission))
			{
				return FALSE;
			}
			uint32_t ids[LOCAL_API_MAX_READERS];
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				const Bool current = m_slots[i].readerId != 0 && to.epoch(i) == m_slots[i].epoch;
				ids[i] = current ? m_slots[i].readerId : 0;
			}
			LeaveCriticalSection(&m_lock);

			Bool queued = FALSE;
			const UnsignedInt64 now = LocalApiNowMicros();
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				if (ids[i] != 0 && m_table.EnqueueFrame(frameType, frameClassOf(frameType), body, bodyLength, now, ids[i]))
				{
					queued = TRUE;
					m_server.Wake();
				}
			}
			return queued;
		}

		virtual void takeResyncRequests()
		{
			pumpReaderEvents();
		}

		virtual void bumpEpoch(Int slot, UnsignedInt epoch)
		{
			if (!m_listening || slot < 0 || slot >= LOCAL_API_MAX_READERS)
			{
				return;
			}
			pumpReaderEvents();
			EnterCriticalSection(&m_lock);
			if (m_slots[slot].readerId != 0 && m_slots[slot].epoch == epoch)
			{
				resyncSlot(slot);
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual void getFeedConfig(Int slot, LocalApiFeedConfig& out) const
		{
			out.setDefaults();
			if (!m_listening || slot < 0 || slot >= LOCAL_API_MAX_READERS)
			{
				return;
			}
			pumpReaderEvents();
			EnterCriticalSection(&m_lock);
			if (m_feedEpoch[slot] != 0 && m_slots[slot].readerId != 0 && m_slots[slot].epoch == m_feedEpoch[slot])
			{
				out = m_feed[slot];
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual void setFeedConfig(Int slot, UnsignedInt epoch, const LocalApiFeedConfig& config)
		{
			if (!m_listening || slot < 0 || slot >= LOCAL_API_MAX_READERS || epoch == 0)
			{
				return;
			}
			pumpReaderEvents();
			EnterCriticalSection(&m_lock);
			if (m_slots[slot].readerId != 0 && m_slots[slot].epoch == epoch)
			{
				m_feed[slot] = config;
				m_feedEpoch[slot] = epoch;
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual Bool takeChatSend(LocalApiChatSend& out)
		{
			refreshAccess();
			pumpReaderEvents();
			pumpInbound();
			if (!m_allowed || !permitted(LOCAL_API_PERMISSION_CHAT_SEND))
			{
				m_chats.clear();
				return FALSE;
			}
			while (!m_chats.empty())
			{
				const Int slot = m_chats.front().slot;
				EnterCriticalSection(&m_lock);
				const Bool connected = m_slots[slot].readerId != 0;
				LeaveCriticalSection(&m_lock);
				if (connected)
				{
					out = m_chats.front().chat;
					m_chats.pop_front();
					return TRUE;
				}
				m_chats.pop_front();
			}
			return FALSE;
		}

		virtual Bool takeRequest(LocalApiQueuedRequest& out)
		{
			refreshAccess();
			pumpReaderEvents();
			pumpInbound();
			if (!m_allowed)
			{
				m_requests.clear();
				return FALSE;
			}
			while (!m_requests.empty())
			{
				QueuedRequest& entry = m_requests.front();
				EnterCriticalSection(&m_lock);
				const UnsignedInt epoch = (m_slots[entry.slot].readerId != 0 && m_slots[entry.slot].welcomed) ? m_slots[entry.slot].epoch : 0;
				LeaveCriticalSection(&m_lock);
				// A request is answered at the current epoch of its reader while that reader is still welcomed.
				if (epoch != 0)
				{
					out.from.clear();
					out.from.add(entry.slot, epoch);
					out.body.swap(entry.body);
					m_requests.pop_front();
					return TRUE;
				}
				m_requests.pop_front();
			}
			return FALSE;
		}

	private:
		enum
		{
			CHAT_QUEUE_SLOTS = 8 * LOCAL_API_MAX_READERS,
			REQUEST_QUEUE_SLOTS = LOCAL_API_REQUEST_QUEUE_SLOTS * LOCAL_API_MAX_READERS
		};

		struct QueuedChat
		{
			Int slot;
			LocalApiChatSend chat;
		};

		struct QueuedRequest
		{
			Int slot;
			std::string body;	///< the Request message, still encoded
		};

		/// A reader id of 0 marks a free slot, because the pipe reserves 0 for all readers.
		struct Slot
		{
			Slot() : readerId(0), epoch(0), welcomed(FALSE) {}
			uint32_t readerId;
			UnsignedInt epoch;
			Bool welcomed;
		};

		PipeLocalApiSink() : m_server(m_table), m_listening(FALSE), m_startFailed(FALSE), m_allowed(TRUE), m_permitted(ALL_PERMISSIONS), m_reason(localapi::ACCESS_REASON_NONE), m_sentMask(ALL_PERMISSIONS), m_stopped(FALSE), m_nextEpoch(1)
		{
			InitializeCriticalSection(&m_lock);
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				m_feedEpoch[i] = 0;
			}
		}
		PipeLocalApiSink(const PipeLocalApiSink&);
		PipeLocalApiSink& operator=(const PipeLocalApiSink&);

		UnsignedInt newEpoch() const
		{
			if (m_nextEpoch == 0)
			{
				m_nextEpoch = 1;
			}
			return m_nextEpoch++;
		}

		/// Main thread only, the results are cached in m_allowed and m_permitted for the other threads.
		/// The pipe is closed while the local player is an active player in a LAN or online match. LAN and online games and their
		/// lobbies also need a signed in account with the local_api client feature and a local_api permission per capability.
		/// Skirmish, single player, challenge and replays need no account.
		void refreshAccess() const
		{
			const GameMode mode = TheGameLogic != NULL ? TheGameLogic->getGameMode() : GAME_NONE;
			Bool needsAccount = mode == GAME_INTERNET || mode == GAME_LAN || TheLAN != NULL;
			if (!needsAccount)
			{
				GenOnlineLobbyInterface* lobby = GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
				needsAccount = lobby != NULL && lobby->IsInLobby();
			}
			GenOnlineAuthInterface* auth = GenOnlineServicesManager::GetInterface<GenOnlineAuthInterface>();
			const Bool signedIn = auth != NULL && auth->IsLoggedIn();

			UnsignedInt mask = ALL_PERMISSIONS;
			if (needsAccount)
			{
				mask = 0;
				for (Int i = 0; signedIn && i < LOCAL_API_PERMISSION_COUNT; ++i)
				{
					if (GenOnlineRealtimeBridge::HasPermission(s_permissionNames[i]))
					{
						mask |= 1u << i;
					}
				}
			}
			const Bool gainedPermission = (mask & ~m_permitted) != 0;
			m_permitted = mask;

			Int reason = localapi::ACCESS_REASON_NONE;
			if (LocalApiGatedPlayerIndex() >= 0)
			{
				reason = localapi::ACCESS_REASON_ACTIVE_PLAYER;
			}
			else if (needsAccount && !signedIn)
			{
				reason = localapi::ACCESS_REASON_SIGNED_OUT;
			}
			else if (needsAccount && !GenOnlineRealtimeBridge::HasClientFeature("local_api"))
			{
				reason = localapi::ACCESS_REASON_FEATURE_OFF;
			}
			const Bool changed = reason != m_reason || mask != m_sentMask;
			m_reason = reason;
			m_sentMask = mask;

			const Bool allowed = reason == localapi::ACCESS_REASON_NONE;
			if (allowed && (!m_allowed || gainedPermission) && m_listening)
			{
				// A reader missed what was held back, so it starts over with a new Hello.
				EnterCriticalSection(&m_lock);
				for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
				{
					if (m_slots[i].readerId != 0)
					{
						resyncSlot(i);
					}
				}
				LeaveCriticalSection(&m_lock);
			}
			m_allowed = allowed;

			if (changed && m_listening)
			{
				LocalApiReaders welcomed;
				EnterCriticalSection(&m_lock);
				for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
				{
					if (m_slots[i].readerId != 0 && m_slots[i].welcomed)
					{
						welcomed.add(i, m_slots[i].epoch);
					}
				}
				LeaveCriticalSection(&m_lock);
				sendAccessState(welcomed);
			}
		}

		void sendAccessState(const LocalApiReaders& to) const
		{
			if (to.empty())
			{
				return;
			}
			localapi::AccessState state;
			state.set_open(m_reason == localapi::ACCESS_REASON_NONE);
			state.set_reason((localapi::AccessReason)m_reason);
			for (Int i = 0; i < LOCAL_API_PERMISSION_COUNT; ++i)
			{
				if (permitted((LocalApiPermission)i))
				{
					state.add_permissions(s_permissionNames[i]);
				}
			}
			std::string body;
			if (!state.SerializeToString(&body) || body.empty())
			{
				return;
			}
			// The sink is a single mutable instance, the const only marks the main thread accessors.
			const_cast<PipeLocalApiSink*>(this)->enqueueFrame(LOCAL_API_FRAME_ACCESS_STATE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), to);
		}

		/// Welcomed readers when welcomed is TRUE, the others otherwise. Any thread.
		void collectReaders(Bool welcomed, LocalApiReaders& out) const
		{
			out.clear();
			// Unwelcomed readers are listed also while the pipe is closed, so each gets its Hello and AccessState.
			if (welcomed && !m_allowed)
			{
				return;
			}
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				if (m_slots[i].readerId != 0 && m_slots[i].welcomed == welcomed)
				{
					out.add(i, m_slots[i].epoch);
				}
			}
			LeaveCriticalSection(&m_lock);
		}

		/// A resync gives the same reader a new epoch, so its feed config moves to that epoch instead of falling back to the defaults. The lock is held.
		void resyncSlot(Int slot) const
		{
			const UnsignedInt epoch = newEpoch();
			if (m_feedEpoch[slot] != 0 && m_feedEpoch[slot] == m_slots[slot].epoch)
			{
				m_feedEpoch[slot] = epoch;
			}
			m_slots[slot].epoch = epoch;
			m_slots[slot].welcomed = FALSE;
		}

		Int findSlot(uint32_t readerId) const
		{
			for (Int i = 0; i < LOCAL_API_MAX_READERS; ++i)
			{
				if (m_slots[i].readerId == readerId)
				{
					return i;
				}
			}
			return -1;
		}

		/// Main thread only. A reader that finds no free slot is ignored, so it never gets a Hello and receives nothing.
		void pumpReaderEvents() const
		{
			if (!m_listening)
			{
				return;
			}
			uint32_t readerId = 0;
			GenOnlineLocalApi::ReaderEvent event = GenOnlineLocalApi::ReaderEvent::HelloNeeded;
			while (m_table.TakeReaderEvent(&readerId, &event))
			{
				if (readerId == 0)
				{
					continue;
				}
				EnterCriticalSection(&m_lock);
				const Int known = findSlot(readerId);
				if (event == GenOnlineLocalApi::ReaderEvent::HelloNeeded)
				{
					if (known >= 0)
					{
						resyncSlot(known);
					}
					else
					{
						const Int freeSlot = findSlot(0);
						if (freeSlot >= 0)
						{
							m_slots[freeSlot].readerId = readerId;
							m_slots[freeSlot].epoch = newEpoch();
							m_slots[freeSlot].welcomed = FALSE;
							m_feedEpoch[freeSlot] = 0;
						}
					}
				}
				else if (event == GenOnlineLocalApi::ReaderEvent::Gone && known >= 0)
				{
					m_slots[known] = Slot();
					m_feedEpoch[known] = 0;
				}
				LeaveCriticalSection(&m_lock);
			}
		}

		void pumpInbound()
		{
			if (!m_listening)
			{
				return;
			}
			GenOnlineLocalApi::TakenInbound taken;
			while (m_table.TakeInbound(&taken))
			{
				EnterCriticalSection(&m_lock);
				const Int slot = findSlot(taken.readerId);
				const UnsignedInt epoch = slot >= 0 ? m_slots[slot].epoch : 0;
				LeaveCriticalSection(&m_lock);
				// A frame that arrives while the pipe is closed is dropped, so it never runs once the pipe opens again.
				if (m_allowed && slot >= 0 && epoch != 0 && (taken.body || taken.bodyLength == 0))
				{
					handleInbound(taken.frameType, slot, epoch, taken.flags, taken.body.get(), taken.bodyLength);
				}
				taken = GenOnlineLocalApi::TakenInbound();
			}
		}

		void handleInbound(UnsignedByte frameType, Int slot, UnsignedInt epoch, UnsignedInt flags, const UnsignedByte* body, UnsignedInt bodyLength)
		{
			if (frameType == LOCAL_API_INBOUND_REQUEST)
			{
				if ((flags & GenOnlineLocalApi::kInboundRateLimited) != 0 || m_requests.size() >= (size_t)REQUEST_QUEUE_SLOTS)
				{
					queueRefusal(slot, epoch, extractRequestId(body, bodyLength), localapi::RESPONSE_STATUS_RATE_LIMITED, "too many requests");
					return;
				}
				m_requests.push_back(QueuedRequest());
				m_requests.back().slot = slot;
				m_requests.back().body.assign((const char*)body, bodyLength);
				return;
			}
			if (frameType != LOCAL_API_INBOUND_CHAT_SEND)
			{
				return;	// unknown type: skipped, not an error
			}
			localapi::ChatSend chat;
			if (!chat.ParseFromArray(body, (int)bodyLength))
			{
				return;	// decode failure: skipped
			}
			if (m_chats.size() >= (size_t)CHAT_QUEUE_SLOTS)
			{
				return;
			}
			m_chats.push_back(QueuedChat());
			QueuedChat& entry = m_chats.back();
			entry.slot = slot;
			entry.chat.channel = (int)chat.channel();
			entry.chat.audience = (int)chat.audience();
			entry.chat.text = chat.text();
		}

		/// Queues a Response without a result (a refusal) for the one reader the request came from.
		void queueRefusal(Int slot, UnsignedInt epoch, UnsignedInt requestId, int status, const char* message)
		{
			localapi::Response response;
			std::string body;

			response.set_id(requestId);
			response.set_status((localapi::ResponseStatus)status);
			response.set_message(message);
			if (!response.SerializeToString(&body) || body.empty())
			{
				return;
			}

			LocalApiReaders to;
			to.add(slot, epoch);
			enqueueFrame(LOCAL_API_FRAME_RESPONSE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), to);
		}

		mutable GenOnlineLocalApi::ReaderTable m_table;	///< thread safe on its own, so const members may take events and frames from it
		GenOnlineLocalApi::PipeServer m_server;
		Bool m_listening;
		Bool m_startFailed;	///< set when the pipe could not start, so it is not retried every frame
		mutable volatile Bool m_allowed;	///< FALSE while the local player plays a LAN or online match or an online game or lobby lacks the local_api client feature
		mutable volatile UnsignedInt m_permitted;	///< bit per LocalApiPermission
		mutable Int m_reason;	///< the localapi::AccessReason of the last refreshAccess, main thread only
		mutable UnsignedInt m_sentMask;	///< the permissions the last AccessState reported, main thread only
		Bool m_stopped;	///< set by shutdown() and never cleared, so the sink does not restart during engine teardown
		mutable CRITICAL_SECTION m_lock;	///< guards m_slots, m_feed, m_feedEpoch and m_nextEpoch
		mutable Slot m_slots[LOCAL_API_MAX_READERS];
		mutable UnsignedInt m_nextEpoch;
		mutable LocalApiFeedConfig m_feed[LOCAL_API_MAX_READERS];
		mutable UnsignedInt m_feedEpoch[LOCAL_API_MAX_READERS];	///< 0 = defaults
		std::deque<QueuedChat> m_chats;
		std::deque<QueuedRequest> m_requests;
	};
}

UnsignedInt64 LocalApiNowMicros()
{
	ExporterClock& clock = exporterClock();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	const UnsignedInt64 ticks = (UnsignedInt64)(now.QuadPart - clock.start.QuadPart);
	const UnsignedInt64 frequency = (UnsignedInt64)clock.frequency.QuadPart;
	return (ticks / frequency) * 1000000 + (ticks % frequency) * 1000000 / frequency;
}

UnsignedInt64 LocalApiEpochUnixMs()
{
	return exporterClock().epochUnixMs;
}

LocalApiSink* LocalApiReadersGetSink()
{
	// A batch of replay simulations (headless) must not start the reader feed.
	if (TheGlobalData != NULL && (TheGlobalData->m_headless || !TheGlobalData->m_simulateReplays.empty()))
	{
		return NULL;
	}
	PipeLocalApiSink& sink = PipeLocalApiSink::TheInstance();
	return sink.start() ? &sink : NULL;
}

void LocalApiReadersStart()
{
	LocalApiReadersGetSink();
}

void LocalApiReadersShutdown()
{
	PipeLocalApiSink::TheInstance().shutdown();
}

const char* LocalApiPermissionName(LocalApiPermission permission)
{
	return (permission >= 0 && permission < LOCAL_API_PERMISSION_COUNT) ? s_permissionNames[permission] : "";
}

Bool LocalApiPermitted(LocalApiPermission permission)
{
	return permission >= 0 && permission < LOCAL_API_PERMISSION_COUNT && PipeLocalApiSink::TheInstance().permitted(permission);
}
