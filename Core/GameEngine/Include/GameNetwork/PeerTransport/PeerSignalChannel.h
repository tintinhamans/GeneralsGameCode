#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// A peer to peer signal and the user it came from or goes to.
struct PeerSignal
{
	int64_t senderUserID = -1;	///< The user the provider says the signal came from.
	std::vector<uint8_t> payload;	///< Opaque bytes, whatever the peer wrote.
};

// Carries the opaque signal blobs of the peer connections between users. The online provider implements it.
// The transport calls it on the main thread only.
class IPeerSignalChannel
{
public:
	virtual ~IPeerSignalChannel() {}

	/// False while the channel can neither send nor receive. The signals to send wait then.
	virtual bool IsOpen() const = 0;
	virtual void SendSignal(int64_t targetUserID, const std::vector<uint8_t>& payload) = 0;
	/// Appends the received signals to out in arrival order. The sender is the user the provider stamped on the signal.
	virtual void DrainSignals(std::deque<PeerSignal>& out) = 0;
};

// The received signals that wait for the main thread. Any thread may push.
class PeerSignalInbox
{
public:
	static constexpr size_t k_maxSignals = 256;

	/// Queues a signal, a full inbox discards its oldest signal first.
	void Push(int64_t senderUserID, std::vector<uint8_t> payload);
	/// Drops the queued signals so they never reach the next lobby.
	void Clear();
	std::deque<PeerSignal> Drain();

private:
	std::mutex m_mutex;
	std::deque<PeerSignal> m_signals;
};

// The signals to send that wait for the main thread. Any thread may push.
class PeerSignalOutbox
{
public:
	/// The backlog that a push trims before it queues its signal.
	static constexpr size_t k_maxBacklog = 128;

	/// Queues a signal for the user, a backed up queue discards its oldest signals first.
	void Push(int64_t targetUserID, std::vector<uint8_t> payload);
	void Clear();
	/// Sends the queued signals in order. Call it on the main thread.
	void FlushTo(IPeerSignalChannel& channel);
	size_t Size();

private:
	struct QueuedSend
	{
		int64_t target_user_id;
		std::vector<uint8_t> vecPayload;
	};

	// Guards m_queueSend, Push() may run on any thread, FlushTo() drains on the main thread.
	std::mutex m_sendQueueMutex;
	std::deque<QueuedSend> m_queueSend;
};
