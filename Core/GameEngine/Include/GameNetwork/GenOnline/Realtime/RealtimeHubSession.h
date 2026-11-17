/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameNetwork/GenOnline/Realtime/RealtimeMessages.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeProtocol.h"
#include "GameNetwork/GenOnline/Realtime/RealtimeRest.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

enum class DisconnectAction
{
	Reconnect,
	ReconnectAfterKick,
	// Reconnect once after SwitchReconnectDelayMs, without backoff and without a reconnect screen.
	ReconnectAfterSwitch,
	// Refresh the access token once, register the session again and reconnect, otherwise return to login.
	RefreshAndReconnect,
	StopSignedInElsewhere,
	ReturnToLogin,
};

constexpr int64_t KICK_RECONNECT_DELAY_MS = 10000;

inline DisconnectAction ActionForCloseCode(const std::string& code, bool allowReconnect)
{
	if (code == "session.superseded")
		return DisconnectAction::StopSignedInElsewhere;
	if (code == "auth.revoked")
		return DisconnectAction::RefreshAndReconnect;
	if (code == "moderation.kicked")
		return DisconnectAction::ReconnectAfterKick;
	if (code == "server.switching" && allowReconnect)
	{
		return DisconnectAction::ReconnectAfterSwitch;
	}
	if (code == "moderation.banned" || code == "protocol.error")
		return DisconnectAction::ReturnToLogin;
	return allowReconnect ? DisconnectAction::Reconnect : DisconnectAction::ReturnToLogin;
}

// Reconnect delays: a random 0.5 to 3 seconds first, then 2 seconds doubling up to 30 seconds.
// Reconnects continue for as long as the user stays logged in.
class ReconnectPolicy
{
public:
	static constexpr int64_t FIRST_MIN_MS = 500;
	static constexpr int64_t FIRST_MAX_MS = 3000;
	static constexpr int64_t STEP_BASE_MS = 2000;
	static constexpr int64_t STEP_CAP_MS = 30000;

	// random returns a value in [0, 1).
	explicit ReconnectPolicy(std::function<double()> random) : m_random(random) {}

	int64_t NextDelayMs()
	{
		int attempt = m_attempt++;
		if (attempt == 0)
			return FIRST_MIN_MS + static_cast<int64_t>(m_random() * static_cast<double>(FIRST_MAX_MS - FIRST_MIN_MS));
		int64_t delay = STEP_BASE_MS;
		for (int i = 1; i < attempt && delay < STEP_CAP_MS; ++i)
			delay *= 2;
		return delay < STEP_CAP_MS ? delay : STEP_CAP_MS;
	}

	// A connection that reached Ready starts the delays over.
	void Reset()
	{
		m_attempt = 0;
	}

	int Attempt() const
	{
		return m_attempt;
	}

private:
	std::function<double()> m_random;
	int m_attempt = 0;
};

// A handover reconnect stays hidden for this long after the server.switching close.
constexpr int64_t SWITCH_HIDDEN_RECONNECT_MS = 3000;
// The longest delay after a server.switching close that had no ServerSwitching before it.
constexpr int64_t SWITCH_DEFAULT_DELAY_MAX_MS = 1000;

// The delay of the reconnect after a server.switching close. It is the retry_after_ms of the
// ServerSwitching before the close, kept within 0 and the backoff cap. Without a ServerSwitching
// it is a random 0 to 1 second. random is a value in [0, 1).
inline int64_t SwitchReconnectDelayMs(const std::optional<int64_t>& retryAfterMs, double random)
{
	if (!retryAfterMs.has_value())
	{
		return static_cast<int64_t>(random * static_cast<double>(SWITCH_DEFAULT_DELAY_MAX_MS));
	}
	return (std::min)((std::max)(*retryAfterMs, static_cast<int64_t>(0)), ReconnectPolicy::STEP_CAP_MS);
}

// A handover reconnect that has not connected within 3 seconds of the close continues as a normal reconnect.
inline bool SwitchReconnectHidden(int64_t closedAtMs, int64_t nowMs)
{
	return nowMs - closedAtMs < SWITCH_HIDDEN_RECONNECT_MS;
}

// For this long after a server.switching close the new copy may still be starting.
constexpr int64_t SWITCH_FAST_RETRY_WINDOW_MS = 30000;
// The longest reconnect delay in that window, so the client is back soon after the new copy is.
constexpr int64_t SWITCH_FAST_RETRY_MAX_MS = 2000;

// The backoff delay of a reconnect, held at SWITCH_FAST_RETRY_MAX_MS within the window after a server switch.
// switchClosedAtMs is -1 when the reconnect did not start with a server switch.
inline int64_t SwitchWindowDelayMs(int64_t backoffMs, int64_t switchClosedAtMs, int64_t nowMs)
{
	if (switchClosedAtMs < 0 || nowMs - switchClosedAtMs >= SWITCH_FAST_RETRY_WINDOW_MS)
	{
		return backoffMs;
	}
	return (std::min)(backoffMs, SWITCH_FAST_RETRY_MAX_MS);
}

// The outcome of a hub method that expects a Result. transportError is set when no Result
// arrived, for example because the connection dropped.
struct InvokeOutcome
{
	bool delivered = false;
	Result result;
	std::string transportError;

	bool Succeeded() const
	{
		return delivered && result.ok;
	}

	std::string Code() const
	{
		return result.code.has_value() ? *result.code : std::string();
	}

	template <class T>
	bool Data(T& out) const
	{
		if (!Succeeded())
			return false;
		try
		{
			nlohmann::json data = result.data.has_value() ? *result.data : nlohmann::json::object();
			out = data.get<T>();
			return true;
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
};

class HubSession
{
public:
	enum class State
	{
		Idle,
		Handshaking,
		WaitingForReady,
		Ready,
		Closed,
	};

	struct Callbacks
	{
		std::function<bool(const std::string& text)> sendText;
		// Closes the websocket. The session already decided what happens next.
		std::function<void()> closeTransport;
		// Runs once per connection after Ready arrived.
		std::function<void(const ReadyEvent& ready)> onReady;
		// Runs once per connection when it ended.
		std::function<void(DisconnectAction action, const std::string& reason)> onDisconnected;
		std::function<void(const std::string& line)> log;
	};

	explicit HubSession(const Callbacks& callbacks) : m_callbacks(callbacks) {}

	// Registers the handler of event E. A later registration of the same event replaces it.
	template <class E>
	void On(std::function<void(const E&)> handler)
	{
		m_handlers[E::EVENT] = [this, handler](const nlohmann::json& argument)
		{
			E event;
			try
			{
				event = argument.get<E>();
			}
			catch (const std::exception& e)
			{
				Log(std::string("realtime: dropped malformed ") + E::EVENT + " event: " + e.what());
				return;
			}
			handler(event);
		};
	}

	void OnTransportOpen(int64_t nowMs)
	{
		m_splitter.Reset();
		m_state = State::Handshaking;
		m_lastReceiveMs = nowMs;
		m_lastSendMs = nowMs;
		m_disconnectReported = false;
		m_serverSwitchingRetryAfterMs.reset();
		SendRaw(BuildHandshakeRequest(), nowMs);
	}

	void OnTransportText(const char* data, size_t length, int64_t nowMs)
	{
		if (m_state == State::Idle || m_state == State::Closed)
			return;
		m_lastReceiveMs = nowMs;
		if (!m_splitter.Append(data, length))
		{
			Fail(DisconnectAction::ReturnToLogin, "record larger than the 64 KB limit");
			return;
		}
		std::string record;
		while (m_state != State::Closed && m_splitter.Next(record))
		{
			if (record.size() > MAX_RECORD_BYTES)
			{
				Fail(DisconnectAction::ReturnToLogin, "record larger than the 64 KB limit");
				return;
			}
			HandleRecord(record);
		}
	}

	// The websocket closed or failed without a close record.
	void OnTransportLost(const std::string& reason)
	{
		if (m_state == State::Idle || m_state == State::Closed)
			return;
		End(DisconnectAction::Reconnect, reason, false);
	}

	// Sends keep-alive pings and detects a silent server.
	void Tick(int64_t nowMs)
	{
		if (m_state == State::Idle || m_state == State::Closed)
			return;
		if (nowMs - m_lastReceiveMs >= SERVER_TIMEOUT_MS)
		{
			End(DisconnectAction::Reconnect, "no record from the server for 30 seconds", true);
			return;
		}
		if (m_state != State::Handshaking && nowMs - m_lastSendMs >= PING_INTERVAL_MS)
			SendRaw(SerializePing(), nowMs);
	}

	// Calls a hub method that answers with a Result. The callback runs exactly once.
	template <class R>
	void Invoke(const R& request, std::function<void(const InvokeOutcome&)> callback, int64_t nowMs)
	{
		static_assert(R::EXPECTS_RESULT, "use Fire for methods without a Result");
		InvokeMethod(R::METHOD, nlohmann::json(request), callback, nowMs);
	}

	// Calls a hub method by name with its serialized request. The callback runs exactly once.
	void InvokeMethod(const std::string& method, const nlohmann::json& request, std::function<void(const InvokeOutcome&)> callback, int64_t nowMs)
	{
		if (m_state != State::Ready)
		{
			if (callback)
			{
				InvokeOutcome outcome;
				outcome.transportError = "not connected";
				callback(outcome);
			}
			return;
		}
		std::string id = m_tracker.Add([callback](bool succeeded, const nlohmann::json& result, const std::string& error)
		{
			InvokeOutcome outcome;
			if (succeeded)
			{
				try
				{
					outcome.result = result.get<Result>();
					outcome.delivered = true;
				}
				catch (const std::exception& e)
				{
					outcome.transportError = std::string("malformed Result: ") + e.what();
				}
			}
			else
			{
				outcome.transportError = error;
			}
			if (callback)
				callback(outcome);
		});
		SendRaw(SerializeInvocation(method, request, id), nowMs);
	}

	// Sends a fire-and-forget hub method. Returns false when the session is not ready.
	template <class R>
	bool Fire(const R& request, int64_t nowMs)
	{
		static_assert(!R::EXPECTS_RESULT, "use Invoke for methods with a Result");
		if (m_state != State::Ready)
			return false;
		return SendRaw(SerializeInvocation(R::METHOD, nlohmann::json(request), std::string()), nowMs);
	}

	State GetState() const
	{
		return m_state;
	}

	size_t PendingInvocations() const
	{
		return m_tracker.PendingCount();
	}

	// The retry_after_ms of the last ServerSwitching of this connection, for the server.switching close that follows.
	std::optional<int64_t> ServerSwitchingRetryAfterMs() const
	{
		return m_serverSwitchingRetryAfterMs;
	}

private:
	void HandleRecord(const std::string& text)
	{
		if (m_state == State::Handshaking)
		{
			std::string error;
			if (!ParseHandshakeResponse(text, error))
			{
				Fail(DisconnectAction::ReturnToLogin, "handshake refused: " + error);
				return;
			}
			m_state = State::WaitingForReady;
			return;
		}

		Record record;
		std::string error;
		if (!ParseRecord(text, record, error))
		{
			Fail(DisconnectAction::ReturnToLogin, "malformed record: " + error);
			return;
		}

		switch (static_cast<RecordType>(record.type))
		{
		case RecordType::Invocation:
			HandleInvocation(record);
			break;
		case RecordType::Completion:
			if (!m_tracker.Complete(record))
				Log("realtime: completion for unknown invocation " + record.invocationId);
			break;
		case RecordType::Close:
			End(ActionForCloseCode(record.error, record.allowReconnect), record.error.empty() ? std::string("server closed the connection") : record.error, true);
			break;
		case RecordType::Ping:
		default:
			break;
		}
	}

	void HandleInvocation(const Record& record)
	{
		if (m_state == State::WaitingForReady)
		{
			if (record.target != ReadyEvent::EVENT)
			{
				Fail(DisconnectAction::ReturnToLogin, "first event was " + record.target + " instead of Ready");
				return;
			}
			ReadyEvent ready;
			try
			{
				ready = record.argument.get<ReadyEvent>();
			}
			catch (const std::exception& e)
			{
				Fail(DisconnectAction::ReturnToLogin, std::string("malformed Ready: ") + e.what());
				return;
			}
			if (ready.protocol_version != PROTOCOL_VERSION)
			{
				Fail(DisconnectAction::ReturnToLogin, "unsupported protocol version " + std::to_string(ready.protocol_version));
				return;
			}
			m_state = State::Ready;
			if (m_callbacks.onReady)
				m_callbacks.onReady(ready);
			return;
		}

		if (record.target == ServerSwitchingEvent::EVENT)
		{
			RememberServerSwitching(record.argument);
		}

		auto it = m_handlers.find(record.target);
		if (it == m_handlers.end())
			return;
		it->second(record.argument);
	}

	void RememberServerSwitching(const nlohmann::json& argument)
	{
		try
		{
			m_serverSwitchingRetryAfterMs = argument.get<ServerSwitchingEvent>().retry_after_ms;
		}
		catch (const std::exception& e)
		{
			Log(std::string("realtime: dropped malformed ServerSwitching event: ") + e.what());
		}
	}

	bool SendRaw(const std::string& text, int64_t nowMs)
	{
		if (!m_callbacks.sendText || !m_callbacks.sendText(text))
			return false;
		m_lastSendMs = nowMs;
		return true;
	}

	void Fail(DisconnectAction action, const std::string& reason)
	{
		End(action, reason, true);
	}

	void End(DisconnectAction action, const std::string& reason, bool closeTransport)
	{
		if (m_state == State::Closed)
			return;
		m_state = State::Closed;
		m_splitter.Reset();
		Log("realtime: connection ended: " + reason);
		m_tracker.FailAll("connection lost");
		if (closeTransport && m_callbacks.closeTransport)
			m_callbacks.closeTransport();
		if (!m_disconnectReported)
		{
			m_disconnectReported = true;
			if (m_callbacks.onDisconnected)
				m_callbacks.onDisconnected(action, reason);
		}
	}

	void Log(const std::string& line)
	{
		if (m_callbacks.log)
			m_callbacks.log(line);
	}

	Callbacks m_callbacks;
	State m_state = State::Idle;
	RecordSplitter m_splitter;
	InvocationTracker m_tracker;
	std::map<std::string, std::function<void(const nlohmann::json&)> > m_handlers;
	int64_t m_lastReceiveMs = 0;
	int64_t m_lastSendMs = 0;
	bool m_disconnectReported = false;
	std::optional<int64_t> m_serverSwitchingRetryAfterMs;
};

// True when a Result failed with service.unavailable and what standby, the answer of a copy that hands over.
inline bool IsStandbyResult(const InvokeOutcome& outcome)
{
	if (!outcome.delivered || outcome.result.ok || outcome.Code() != "service.unavailable" || !outcome.result.params.has_value())
	{
		return false;
	}
	const nlohmann::json& params = *outcome.result.params;
	if (!params.is_object())
	{
		return false;
	}
	auto what = params.find("what");
	return what != params.end() && what->is_string() && what->get<std::string>() == "standby";
}

struct ParkedHubCall
{
	std::string method;
	nlohmann::json request;
	std::function<void(const InvokeOutcome&)> callback;
	// Runs before each pass, and false drops the call without its callback.
	std::function<bool()> fnWanted;
	StandbyRetry standby;
	int64_t sendAtMs = 0;
	// The time of the last standby answer, where the wait for a ready session starts.
	int64_t parkedAtMs = 0;
	// The last standby answer, which the callback gets when the session stays away past the window.
	InvokeOutcome lastOutcome;
};

// Holds hub calls that got a standby Result until the session is ready and their wait passed.
// Transport errors and other failures go to the callback unchanged.
class StandbyHubCalls
{
public:
	// Takes the outcome of a sent call. Returns true when the call is parked, otherwise its callback ran.
	// Standby answers in a row for STANDBY_RETRY_WINDOW_MS run the callback with the last answer.
	bool Complete(ParkedHubCall call, const InvokeOutcome& outcome, int64_t nowMs)
	{
		const int64_t delayMs = call.standby.NextDelayMs(IsStandbyResult(outcome), nowMs);
		if (delayMs >= 0)
		{
			call.sendAtMs = nowMs + delayMs;
			call.parkedAtMs = nowMs;
			call.lastOutcome = outcome;
			m_calls.push_back(call);
			return true;
		}
		if (call.callback)
		{
			call.callback(outcome);
		}
		return false;
	}

	// Drops the calls that are no longer wanted and returns the due calls in the order they were parked.
	// A call is due once the session is ready and its wait passed. A call whose session stays away for the standby window after its last standby answer fails.
	std::vector<ParkedHubCall> TakeDue(int64_t nowMs, bool bReady, std::function<void(const ParkedHubCall&)> onDropped = nullptr)
	{
		std::vector<ParkedHubCall> due;
		std::vector<ParkedHubCall> calls;
		calls.swap(m_calls);
		for (ParkedHubCall& call : calls)
		{
			if (call.fnWanted && !call.fnWanted())
			{
				if (onDropped)
				{
					onDropped(call);
				}
				continue;
			}
			if (bReady && nowMs >= call.sendAtMs)
			{
				due.push_back(call);
			}
			else if (!bReady && nowMs - call.parkedAtMs >= STANDBY_RETRY_WINDOW_MS)
			{
				if (call.callback)
				{
					call.callback(call.lastOutcome);
				}
			}
			else
			{
				m_calls.push_back(call);
			}
		}
		return due;
	}

	// Drops every parked call without its callback.
	void Clear()
	{
		m_calls.clear();
	}

	size_t Size() const
	{
		return m_calls.size();
	}

private:
	std::vector<ParkedHubCall> m_calls;
};

} // namespace GenOnlineRealtime
