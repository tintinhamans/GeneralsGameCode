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

#include "GameNetwork/GenOnline/json.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GenOnlineRealtime
{

constexpr char RECORD_SEPARATOR = '\x1E';

// The hub rejects larger records, so the client applies the same limit to records it receives.
constexpr size_t MAX_RECORD_BYTES = 64 * 1024;

constexpr int PING_INTERVAL_MS = 15000;
constexpr int SERVER_TIMEOUT_MS = 30000;

enum class RecordType : int
{
	Invocation = 1,
	Completion = 3,
	Ping = 6,
	Close = 7,
};

// One parsed hub record. Fields that a record type does not use stay empty.
struct Record
{
	int type = 0;
	std::string invocationId;
	std::string target;
	nlohmann::json argument;
	bool hasResult = false;
	nlohmann::json result;
	std::string error;
	bool allowReconnect = false;
};

inline std::string BuildHandshakeRequest()
{
	std::string out = "{\"protocol\":\"json\",\"version\":1}";
	out += RECORD_SEPARATOR;
	return out;
}

// The handshake response is "{}" on success and carries "error" on failure.
inline bool ParseHandshakeResponse(const std::string& record, std::string& error)
{
	error.clear();
	nlohmann::json j = nlohmann::json::parse(record, nullptr, false);
	if (j.is_discarded() || !j.is_object())
	{
		error = "malformed handshake response";
		return false;
	}
	auto it = j.find("error");
	if (it != j.end())
	{
		error = it->is_string() ? it->get<std::string>() : it->dump();
		return false;
	}
	return true;
}

// Every hub method and event carries exactly one object argument.
inline bool ParseRecord(const std::string& text, Record& record, std::string& error)
{
	record = Record();
	error.clear();
	nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
	if (j.is_discarded() || !j.is_object())
	{
		error = "record is not a JSON object";
		return false;
	}
	auto type = j.find("type");
	if (type == j.end() || !type->is_number_integer())
	{
		error = "record has no integer type";
		return false;
	}
	record.type = type->get<int>();

	auto invocationId = j.find("invocationId");
	if (invocationId != j.end() && invocationId->is_string())
		record.invocationId = invocationId->get<std::string>();

	switch (static_cast<RecordType>(record.type))
	{
	case RecordType::Invocation:
	{
		auto target = j.find("target");
		auto arguments = j.find("arguments");
		if (target == j.end() || !target->is_string())
		{
			error = "invocation has no target";
			return false;
		}
		record.target = target->get<std::string>();
		if (arguments == j.end() || !arguments->is_array() || arguments->size() != 1 || !(*arguments)[0].is_object())
		{
			error = "invocation " + record.target + " does not carry exactly one object argument";
			return false;
		}
		record.argument = (*arguments)[0];
		return true;
	}
	case RecordType::Completion:
	{
		if (record.invocationId.empty())
		{
			error = "completion has no invocationId";
			return false;
		}
		auto result = j.find("result");
		if (result != j.end())
		{
			record.hasResult = true;
			record.result = *result;
		}
		auto err = j.find("error");
		if (err != j.end() && err->is_string())
			record.error = err->get<std::string>();
		return true;
	}
	case RecordType::Close:
	{
		auto err = j.find("error");
		if (err != j.end() && err->is_string())
			record.error = err->get<std::string>();
		auto allowReconnect = j.find("allowReconnect");
		if (allowReconnect != j.end() && allowReconnect->is_boolean())
			record.allowReconnect = allowReconnect->get<bool>();
		return true;
	}
	default:
		// Pings and record types the client does not use parse without further fields.
		return true;
	}
}

// An empty invocationId sends a fire-and-forget invocation.
inline std::string SerializeInvocation(const std::string& target, const nlohmann::json& argument, const std::string& invocationId)
{
	nlohmann::json j;
	j["type"] = static_cast<int>(RecordType::Invocation);
	if (!invocationId.empty())
		j["invocationId"] = invocationId;
	j["target"] = target;
	j["arguments"] = nlohmann::json::array({ argument });
	std::string out = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	out += RECORD_SEPARATOR;
	return out;
}

inline std::string SerializePing()
{
	std::string out = "{\"type\":6}";
	out += RECORD_SEPARATOR;
	return out;
}

// Splits received websocket text into records. Text frames can carry several records, and
// a record can span several frames.
class RecordSplitter
{
public:
	// Returns false when the pending record grows past MAX_RECORD_BYTES.
	bool Append(const char* data, size_t length)
	{
		m_buffer.append(data, length);
		size_t lastSeparator = m_buffer.rfind(RECORD_SEPARATOR);
		size_t tailStart = lastSeparator == std::string::npos ? 0 : lastSeparator + 1;
		return m_buffer.size() - tailStart <= MAX_RECORD_BYTES;
	}

	// Returns the next complete record without its separator. The caller treats a record larger
	// than MAX_RECORD_BYTES as a protocol error.
	bool Next(std::string& record)
	{
		size_t separator = m_buffer.find(RECORD_SEPARATOR);
		if (separator == std::string::npos)
			return false;
		record.assign(m_buffer, 0, separator);
		m_buffer.erase(0, separator + 1);
		return true;
	}

	void Reset()
	{
		m_buffer.clear();
	}

private:
	std::string m_buffer;
};

class InvocationTracker
{
public:
	// The callback receives the completion result, or an error text when the hub reports a protocol
	// error or the connection closes before the completion arrives.
	typedef std::function<void(bool succeeded, const nlohmann::json& result, const std::string& error)> CompletionFn;

	std::string Add(CompletionFn fn)
	{
		std::string id = std::to_string(++m_lastId);
		m_pending[id] = fn;
		return id;
	}

	// Returns false for a completion that no pending invocation waits for.
	bool Complete(const Record& completion)
	{
		auto it = m_pending.find(completion.invocationId);
		if (it == m_pending.end())
			return false;
		CompletionFn fn = it->second;
		m_pending.erase(it);
		if (fn)
		{
			if (!completion.error.empty() || !completion.hasResult)
				fn(false, nlohmann::json(), completion.error.empty() ? std::string("completion has no result") : completion.error);
			else
				fn(true, completion.result, std::string());
		}
		return true;
	}

	void FailAll(const std::string& error)
	{
		std::map<std::string, CompletionFn> pending;
		pending.swap(m_pending);
		for (auto& entry : pending)
		{
			if (entry.second)
				entry.second(false, nlohmann::json(), error);
		}
	}

	size_t PendingCount() const
	{
		return m_pending.size();
	}

private:
	uint64_t m_lastId = 0;
	std::map<std::string, CompletionFn> m_pending;
};

} // namespace GenOnlineRealtime
