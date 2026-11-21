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

#include "Common/StatsExporterPipe.h"

#include "Common/GlobalData.h"
#include "StatsExporter.pb.h"
#include "GameNetwork/GenOnline/GenOnlineInclude.h"	// NetworkLog
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"

#include <GenOnlinePluginApi.h>

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

	int32_t frameClassOf(UnsignedByte frameType)
	{
		switch (frameType)
		{
			case STATS_EXPORTER_FRAME_HELLO:
				return GENONLINE_PLUGIN_TOOL_FRAME_HELLO;
			case STATS_EXPORTER_FRAME_STATS_SNAPSHOT:
			case STATS_EXPORTER_FRAME_EVENTS:
			case STATS_EXPORTER_FRAME_CAMERA:
			case STATS_EXPORTER_FRAME_VIEW_STATS:
			case STATS_EXPORTER_FRAME_INTEREST_MAP:
				return GENONLINE_PLUGIN_TOOL_FRAME_PERIODIC;
			default:
				return GENONLINE_PLUGIN_TOOL_FRAME_LIFECYCLE;
		}
	}

	/// StatsExporterSink over the tool table of the caster plugin. The plugin owns the readers and their queues, and this class
	/// keeps the slot table of the readers, the feed configs and the decoding of inbound frames. The slot table is guarded by a
	/// critical section because isConnected, getReaders and enqueueFrame run on any thread. Reader events and inbound frames are
	/// taken on the main thread only.
	class PluginToolSink : public StatsExporterSink
	{
	public:
		static PluginToolSink& TheInstance()
		{
			static PluginToolSink s_instance;
			return s_instance;
		}

		Bool start()
		{
			if (m_stopped)
			{
				return FALSE;
			}
			if (m_api == NULL)
			{
				// The plugin may load after the first call, so a missing plugin is not remembered.
				const GenOnlinePluginToolApi* api = GenOnlinePluginHost::GetToolApi();
				if (api == NULL)
				{
					return FALSE;
				}
				m_api = api;
				m_listening = m_api->StartListener((uint32_t)STATS_EXPORTER_MAX_READERS) != GENONLINE_PLUGIN_FALSE;
				if (!m_listening)
				{
					NetworkLog(ELogVerbosity::LOG_RELEASE, "[Stats] ERROR: The caster plugin could not start its listener, the live stats pipe stays off");
				}
			}
			return m_listening;
		}

		void shutdown()
		{
			m_stopped = TRUE;
			m_listening = FALSE;
			m_api = NULL;
			m_chats.clear();
			m_requests.clear();
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
			{
				m_slots[i] = Slot();
				m_feedEpoch[i] = 0;
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual Bool isConnected() const
		{
			return m_listening && m_api->IsConnected() != GENONLINE_PLUGIN_FALSE;
		}

		virtual void getReaders(StatsExporterReaders& out) const
		{
			collectReaders(TRUE, out);
		}

		virtual void getUnwelcomedReaders(StatsExporterReaders& out) const
		{
			pumpReaderEvents();
			collectReaders(FALSE, out);
		}

		virtual void markWelcomed(const StatsExporterReaders& readers)
		{
			if (!m_listening)
			{
				return;
			}
			pumpReaderEvents();
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
			{
				if (m_slots[i].readerId != 0 && readers.epoch(i) == m_slots[i].epoch)
				{
					m_slots[i].welcomed = TRUE;
				}
			}
			LeaveCriticalSection(&m_lock);
		}

		virtual Bool enqueueFrame(UnsignedByte frameType, const UnsignedByte* body, UnsignedInt bodyLength, const StatsExporterReaders& to)
		{
			if (!m_listening || body == NULL || bodyLength == 0 || bodyLength > STATS_EXPORTER_MAX_BODY_BYTES)
			{
				return FALSE;
			}
			uint32_t ids[STATS_EXPORTER_MAX_READERS];
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
			{
				const Bool current = m_slots[i].readerId != 0 && to.epoch(i) == m_slots[i].epoch;
				ids[i] = current ? m_slots[i].readerId : 0;
			}
			LeaveCriticalSection(&m_lock);

			Bool queued = FALSE;
			const UnsignedInt64 now = StatsExporterNowMicros();
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
			{
				if (ids[i] != 0 && m_api->EnqueueFrame(frameType, frameClassOf(frameType), body, bodyLength, now, ids[i]) != GENONLINE_PLUGIN_FALSE)
				{
					queued = TRUE;
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
			if (!m_listening || slot < 0 || slot >= STATS_EXPORTER_MAX_READERS)
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

		virtual void getFeedConfig(Int slot, StatsExporterFeedConfig& out) const
		{
			out.setDefaults();
			if (!m_listening || slot < 0 || slot >= STATS_EXPORTER_MAX_READERS)
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

		virtual void setFeedConfig(Int slot, UnsignedInt epoch, const StatsExporterFeedConfig& config)
		{
			if (!m_listening || slot < 0 || slot >= STATS_EXPORTER_MAX_READERS || epoch == 0)
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

		virtual Bool takeChatSend(StatsExporterChatSend& out)
		{
			pumpReaderEvents();
			pumpInbound();
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

		virtual Bool takeRequest(StatsExporterQueuedRequest& out)
		{
			pumpReaderEvents();
			pumpInbound();
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
			CHAT_QUEUE_SLOTS = 8 * STATS_EXPORTER_MAX_READERS,
			REQUEST_QUEUE_SLOTS = STATS_EXPORTER_REQUEST_QUEUE_SLOTS * STATS_EXPORTER_MAX_READERS
		};

		struct QueuedChat
		{
			Int slot;
			StatsExporterChatSend chat;
		};

		struct QueuedRequest
		{
			Int slot;
			std::string body;	///< the Request message, still encoded
		};

		/// A reader id of 0 marks a free slot, because the plugin reserves 0 for all readers.
		struct Slot
		{
			Slot() : readerId(0), epoch(0), welcomed(FALSE) {}
			uint32_t readerId;
			UnsignedInt epoch;
			Bool welcomed;
		};

		PluginToolSink() : m_api(NULL), m_listening(FALSE), m_stopped(FALSE), m_nextEpoch(1)
		{
			InitializeCriticalSection(&m_lock);
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
			{
				m_feedEpoch[i] = 0;
			}
		}
		PluginToolSink(const PluginToolSink&);
		PluginToolSink& operator=(const PluginToolSink&);

		UnsignedInt newEpoch() const
		{
			if (m_nextEpoch == 0)
			{
				m_nextEpoch = 1;
			}
			return m_nextEpoch++;
		}

		/// Welcomed readers when welcomed is TRUE, the others otherwise. Any thread.
		void collectReaders(Bool welcomed, StatsExporterReaders& out) const
		{
			out.clear();
			EnterCriticalSection(&m_lock);
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
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
			for (Int i = 0; i < STATS_EXPORTER_MAX_READERS; ++i)
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
			int32_t event = 0;
			while (m_api->TakeReaderEvent(&readerId, &event) != GENONLINE_PLUGIN_FALSE)
			{
				if (readerId == 0)
				{
					continue;
				}
				EnterCriticalSection(&m_lock);
				const Int known = findSlot(readerId);
				if (event == GENONLINE_PLUGIN_TOOL_READER_HELLO_NEEDED)
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
				else if (event == GENONLINE_PLUGIN_TOOL_READER_GONE && known >= 0)
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
			uint32_t readerId = 0;
			uint8_t frameType = 0;
			uint32_t flags = 0;
			const uint8_t* body = NULL;
			uint32_t bodyLength = 0;
			while (m_api->TakeInbound(&readerId, &frameType, &flags, &body, &bodyLength) != GENONLINE_PLUGIN_FALSE)
			{
				EnterCriticalSection(&m_lock);
				const Int slot = findSlot(readerId);
				const UnsignedInt epoch = slot >= 0 ? m_slots[slot].epoch : 0;
				LeaveCriticalSection(&m_lock);
				if (slot >= 0 && epoch != 0 && (body != NULL || bodyLength == 0))
				{
					handleInbound(frameType, slot, epoch, flags, body, bodyLength);
				}
				m_api->FreeInbound(body);
				body = NULL;
				bodyLength = 0;
			}
		}

		void handleInbound(UnsignedByte frameType, Int slot, UnsignedInt epoch, UnsignedInt flags, const UnsignedByte* body, UnsignedInt bodyLength)
		{
			if (frameType == STATS_EXPORTER_INBOUND_REQUEST)
			{
				if ((flags & GENONLINE_PLUGIN_TOOL_INBOUND_RATE_LIMITED) != 0 || m_requests.size() >= (size_t)REQUEST_QUEUE_SLOTS)
				{
					queueRefusal(slot, epoch, extractRequestId(body, bodyLength), statsexporter::RESPONSE_STATUS_RATE_LIMITED, "too many requests");
					return;
				}
				m_requests.push_back(QueuedRequest());
				m_requests.back().slot = slot;
				m_requests.back().body.assign((const char*)body, bodyLength);
				return;
			}
			if (frameType != STATS_EXPORTER_INBOUND_CHAT_SEND)
			{
				return;	// unknown type: skipped, not an error
			}
			statsexporter::ChatSend chat;
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
			statsexporter::Response response;
			std::string body;

			response.set_id(requestId);
			response.set_status((statsexporter::ResponseStatus)status);
			response.set_message(message);
			if (!response.SerializeToString(&body) || body.empty())
			{
				return;
			}

			StatsExporterReaders to;
			to.add(slot, epoch);
			enqueueFrame(STATS_EXPORTER_FRAME_RESPONSE, (const UnsignedByte*)body.data(), (UnsignedInt)body.size(), to);
		}

		const GenOnlinePluginToolApi* m_api;
		Bool m_listening;
		Bool m_stopped;	///< set by shutdown() and never cleared, so the sink does not restart during engine teardown
		mutable CRITICAL_SECTION m_lock;	///< guards m_slots, m_feed, m_feedEpoch and m_nextEpoch
		mutable Slot m_slots[STATS_EXPORTER_MAX_READERS];
		mutable UnsignedInt m_nextEpoch;
		mutable StatsExporterFeedConfig m_feed[STATS_EXPORTER_MAX_READERS];
		mutable UnsignedInt m_feedEpoch[STATS_EXPORTER_MAX_READERS];	///< 0 = defaults
		std::deque<QueuedChat> m_chats;
		std::deque<QueuedRequest> m_requests;
	};
}

UnsignedInt64 StatsExporterNowMicros()
{
	ExporterClock& clock = exporterClock();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	const UnsignedInt64 ticks = (UnsignedInt64)(now.QuadPart - clock.start.QuadPart);
	const UnsignedInt64 frequency = (UnsignedInt64)clock.frequency.QuadPart;
	return (ticks / frequency) * 1000000 + (ticks % frequency) * 1000000 / frequency;
}

UnsignedInt64 StatsExporterEpochUnixMs()
{
	return exporterClock().epochUnixMs;
}

StatsExporterSink* StatsExporterPipeGetSink()
{
	// A batch of replay simulations (headless) must not open pipes.
	if (TheGlobalData != NULL && (TheGlobalData->m_headless || !TheGlobalData->m_simulateReplays.empty()))
	{
		return NULL;
	}
	PluginToolSink& sink = PluginToolSink::TheInstance();
	return sink.start() ? &sink : NULL;
}

void StatsExporterPipeStart()
{
	StatsExporterPipeGetSink();
}

void StatsExporterPipeShutdown()
{
	PluginToolSink::TheInstance().shutdown();
}
