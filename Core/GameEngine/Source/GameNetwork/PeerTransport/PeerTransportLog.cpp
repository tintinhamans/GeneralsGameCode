#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

// The size of one line, the same as the Generals Online network log takes.
static constexpr size_t k_maxLogLine = 8192;

// Constant initialized, so a provider may set it from a static initializer of another file.
static std::atomic<PeerTransportLogSink> s_logSink{ nullptr };

void SetPeerTransportLogSink(PeerTransportLogSink sink)
{
	s_logSink.store(sink);
}

void PeerTransportLog(PeerLogLevel level, const char* fmt, ...)
{
	const PeerTransportLogSink sink = s_logSink.load();
	if (sink == nullptr)
	{
		return;
	}

	static thread_local char szLine[k_maxLogLine];
	va_list args;
	va_start(args, fmt);
	vsnprintf(szLine, sizeof(szLine), fmt, args);
	va_end(args);
	szLine[sizeof(szLine) - 1] = 0;
	sink(level, szLine);
}
