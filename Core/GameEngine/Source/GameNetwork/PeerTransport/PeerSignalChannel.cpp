#include "GameNetwork/PeerTransport/PeerSignalChannel.h"
#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <utility>

void PeerSignalInbox::Push(int64_t senderUserID, std::vector<uint8_t> payload)
{
	std::scoped_lock<std::mutex> lock(m_mutex);

	if (m_signals.size() >= k_maxSignals)
	{
		PeerTransportLog(PeerLogLevel::Release, "[WebSocket] Pending signal queue full (%zu), discarding oldest signal", m_signals.size());
		m_signals.pop_front();
	}

	PeerSignal signal;
	signal.senderUserID = senderUserID;
	signal.payload = std::move(payload);
	m_signals.push_back(std::move(signal));
}

void PeerSignalInbox::Clear()
{
	std::scoped_lock<std::mutex> lock(m_mutex);
	m_signals.clear();
}

std::deque<PeerSignal> PeerSignalInbox::Drain()
{
	std::scoped_lock<std::mutex> lock(m_mutex);
	std::deque<PeerSignal> drained;
	drained.swap(m_signals);
	return drained;
}

// May be called from any thread; always queues, FlushTo() sends on the main thread.
void PeerSignalOutbox::Push(int64_t targetUserID, std::vector<uint8_t> payload)
{
	std::scoped_lock<std::mutex> lock(m_sendQueueMutex);

	// Best-effort delivery; drop oldest on backlog.
	while (m_queueSend.size() > k_maxBacklog)
	{
		PeerTransportLog(PeerLogLevel::Release, "Signaling send queue is backed up.  Discarding oldest signals\n");
		m_queueSend.pop_front();
	}

	QueuedSend newEntry = QueuedSend();
	newEntry.target_user_id = targetUserID;
	newEntry.vecPayload = std::move(payload);
	m_queueSend.push_back(std::move(newEntry));
}

void PeerSignalOutbox::Clear()
{
	std::scoped_lock<std::mutex> lock(m_sendQueueMutex);
	m_queueSend.clear();
}

void PeerSignalOutbox::FlushTo(IPeerSignalChannel& channel)
{
	std::deque<QueuedSend> sendBatch;
	{
		std::scoped_lock<std::mutex> lock(m_sendQueueMutex);
		sendBatch.swap(m_queueSend);
	}

	while (!sendBatch.empty())
	{
		const QueuedSend& sendData = sendBatch.front();
		channel.SendSignal(sendData.target_user_id, sendData.vecPayload);
		sendBatch.pop_front();
	}
}

size_t PeerSignalOutbox::Size()
{
	std::scoped_lock<std::mutex> lock(m_sendQueueMutex);
	return m_queueSend.size();
}
