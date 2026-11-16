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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/Connection.h"
#include "GameNetwork/networkutil.h"
#include "GameLogic/GameLogic.h"

#if defined(GENERALS_ONLINE)
enum { MaxQuitFlushTime = 5000 }; // wait this many milliseconds at most to retry things before quitting
#else
enum { MaxQuitFlushTime = 30000 }; // wait this many milliseconds at most to retry things before quitting
#endif

/**
 * The constructor.
 */
static const time_t RETRY_TIME_MIN = 50;
static const time_t RETRY_TIME_MAX = 2000;

// The ceiling leaves half of the receiver's packet slots free for other traffic
static const Int WRAPPER_WINDOW_MIN = 16;
static const Int WRAPPER_WINDOW_MAX = MAX_MESSAGES / 2;

Connection::Connection() {
	m_transport = nullptr;
	m_user = nullptr;
	m_netCommandList = nullptr;
	m_retryTime = RETRY_TIME_MAX; // until a round trip has been measured
	m_lastTimeSent = 0;
	m_frameGrouping = 1;
	m_isQuitting = false;
	m_quitTime = 0;
	m_averageLatency = 0.0f;
	m_latencySampleCount = 0;
	m_wrapperWindow = WRAPPER_WINDOW_MIN;
	m_lastWrapperWindowCut = 0;
	Int i;
	for(i = 0; i < CONNECTION_LATENCY_HISTORY_LENGTH; i++)
	{
		m_latencies[i] = 0.0f;
	}
}

/**
 * The destructor.
 */
Connection::~Connection() {
	deleteInstance(m_user);
	m_user = nullptr;

	deleteInstance(m_netCommandList);
	m_netCommandList = nullptr;
}

/**
 * Initialize the connection and any subsystems.
 */
void Connection::init() {
	m_transport = nullptr;

	deleteInstance(m_user);
	m_user = nullptr;

	if (m_netCommandList == nullptr) {
		m_netCommandList = newInstance(NetCommandList);
		m_netCommandList->init();
	}
	m_netCommandList->reset();

	m_lastTimeSent = 0;
	m_frameGrouping = 1;
	m_numRetries = 0;
	m_retryMetricsTime = 0;

	for (Int i = 0; i < CONNECTION_LATENCY_HISTORY_LENGTH; ++i) {
		m_latencies[i] = 0;
	}
	m_averageLatency = 0;
	m_latencySampleCount = 0;
	m_isQuitting = FALSE;
	m_quitTime = 0;
	m_wrapperWindow = WRAPPER_WINDOW_MIN;
	m_lastWrapperWindowCut = 0;
}

/**
 * Take the connection back to the initial state.
 */
void Connection::reset() {
	init();
}

/**
 * Doesn't really do anything.
 */
void Connection::update() {
}

/**
 * Attach the transport object that this connection should use.
 */
void Connection::attachTransport(Transport *transport) {
	m_transport = transport;
}

/**
 * Assign this connection a user.  This is the user to whome we send all our packetized goodies.
 */
void Connection::setUser(User *user) {
	deleteInstance(m_user);
	m_user = user;
}

/**
 * Return the user object.
 */
User * Connection::getUser() {
	return m_user;
}

/**
 * Add this network command to the send queue for this connection.
 * The relay is the mask specifying the people the person we are sending to should send to.
 * The relay mostly has to do with the packet router.
 */
void Connection::sendNetCommandMsg(NetCommandMsg *msg, UnsignedByte relay) {
	if (m_isQuitting)
		return;

	if (m_netCommandList != nullptr) {
		NetPacket packet;

		// check to see if this command will fit in a packet.  If not, we need to split it up.
		// we are splitting up the command here so that the retry logic will not try to
		// resend the ENTIRE command (i.e. multiple packets work of data) and only do the retry
		// one wrapper command at a time.

		NetCommandRef *tempref = NEW_NETCOMMANDREF(msg);
		if (packet.addCommand(tempref)) {
			deleteInstance(tempref);
			tempref = nullptr;
		} else {
			tempref->setRelay(relay);

			// the message doesn't fit in a single packet, need to split it up.
			if (NetCommandList* list = NetPacket::ConstructBigCommandList(tempref)) {
				for (NetCommandRef* ref1 = list->getFirstMessage(); ref1 != nullptr; ref1 = ref1->getNext()) {
					if (NetCommandRef* ref2 = m_netCommandList->addMessage(ref1->getCommand())) {
						ref2->setRelay(relay);
					}
				}

				deleteInstance(list);
				list = nullptr;
			}

			deleteInstance(tempref);
			tempref = nullptr;

			return;
		}

		// the message fits in a packet, add to the command list normally.
		NetCommandRef *ref = m_netCommandList->addMessage(msg);

		if (ref != nullptr) {

/*
#if defined(RTS_DEBUG)
			if (msg->getNetCommandType() == NETCOMMANDTYPE_GAMECOMMAND) {
				DEBUG_LOG(("Connection::sendNetCommandMsg - added game command %d to net command list for frame %d.",
					msg->getID(), msg->getExecutionFrame()));
			} else if (msg->getNetCommandType() == NETCOMMANDTYPE_FRAMEINFO) {
				DEBUG_LOG(("Connection::sendNetCommandMsg - added frame info for frame %d", msg->getExecutionFrame()));
			}
#endif // RTS_DEBUG
*/

			ref->setRelay(relay);
		}
	}
}

void Connection::clearCommandsExceptFrom( Int playerIndex )
{
	NetCommandRef *tmp = m_netCommandList->getFirstMessage();
	while (tmp)
	{
		NetCommandRef *next = tmp->getNext();
		NetCommandMsg *msg = tmp->getCommand();

		if (msg->getPlayerID() != playerIndex)
		{
			DEBUG_LOG(("Connection::clearCommandsExceptFrom(%d) - clearing a command from player %d for frame %d",
				playerIndex, tmp->getCommand()->getPlayerID(), tmp->getCommand()->getExecutionFrame()));

			m_netCommandList->removeMessage(tmp);
			deleteInstance(tmp);
		}

		tmp = next;
	}
}

Bool Connection::isQueueEmpty() {
	if (m_netCommandList->getFirstMessage() == nullptr) {
		return TRUE;
	}
	return FALSE;
}

void Connection::setQuitting()
{
	m_isQuitting = TRUE;
	m_quitTime = timeGetTime();
	DEBUG_LOG(("Connection::setQuitting() at time %d", m_quitTime));
}

/**
 * This is the good part. We take all the network commands queued up for this connection,
 * packetize them and put them on the transport's send queue for actual sending.
 */
UnsignedInt Connection::doSend() {
	Int numpackets = 0;
	time_t curtime = timeGetTime();
	Bool couldQueue = TRUE;

	// Do this check first, since it's an important fail-safe
	if (m_isQuitting && curtime > m_quitTime + MaxQuitFlushTime)
	{
		DEBUG_LOG(("Timed out a quitting connection.  Deleting all %d messages", m_netCommandList->length()));
		m_netCommandList->reset();
		return 0;
	}

	if ((curtime - m_lastTimeSent) < m_frameGrouping) {
//		DEBUG_LOG(("not sending packet, time = %d, m_lastFrameSent = %d, m_frameGrouping = %d", curtime, m_lastTimeSent, m_frameGrouping));
		return 0;
	}

	// A file transfer queues thousands of wrapper commands, far more than the receiver can buffer in one update
	Int wrappersInFlight = 0;
	for (NetCommandRef *ref = m_netCommandList->getFirstMessage(); ref != nullptr; ref = ref->getNext())
	{
		if (ref->getTimeLastSent() != -1 && ref->getCommand()->getNetCommandType() == NETCOMMANDTYPE_WRAPPER)
		{
			++wrappersInFlight;
		}
	}

	std::vector<NetCommandRef *> packed;

	// iterate through all the messages and put them into a packet(s).
	NetCommandRef *msg = m_netCommandList->getFirstMessage();

	while ((msg != nullptr) && couldQueue) {
		NetPacket packet;
		packet.setAddress(m_user->GetIPAddr(), m_user->GetPort());

		Bool notDone = TRUE;

		// add the command messages until either we run out of messages or the packet is full.
		while ((msg != nullptr) && notDone) {
			NetCommandRef *next = msg->getNext();

			time_t timeLastSent = msg->getTimeLastSent();

			if (((curtime - timeLastSent) > m_retryTime) || (timeLastSent == -1)) {
				const Bool isNewWrapper = (timeLastSent == -1) && (msg->getCommand()->getNetCommandType() == NETCOMMANDTYPE_WRAPPER);
				if (isNewWrapper && wrappersInFlight >= m_wrapperWindow)
				{
					msg = next;
					continue;
				}

				notDone = packet.addCommand(msg);
				if (notDone) {
					packed.push_back(msg);
					if (isNewWrapper)
					{
						++wrappersInFlight;
					}
				}
			}
			msg = next;
		}

		if (msg != nullptr) {
			DEBUG_LOG(("didn't finish sending all commands in connection"));
		}

		++numpackets;

		/// @todo Make the act of giving the transport object a packet to send more efficient.  Make the transport take a NetPacket object rather than the raw data, thus avoiding an extra memcpy.
		if (packet.getNumCommands() > 0) {
			// If the packet actually has any information to give, give it to the transport object
			// for transmission.
			couldQueue = m_transport->queueSend(packet.getAddr(), packet.getPort(), packet.getData(), packet.getLength());

			// A refused packet leaves its commands as they were, so they go out on the next update
			if (couldQueue)
			{
				m_lastTimeSent = curtime;
				markCommandsSent(packed, curtime);
			}
		}

		packed.clear();
	}

	return numpackets;
}

void Connection::markCommandsSent(const std::vector<NetCommandRef *> &commands, time_t curtime)
{
	for (size_t i = 0; i < commands.size(); ++i)
	{
		NetCommandRef *msg = commands[i];

		if (!CommandRequiresAck(msg->getCommand()))
		{
			m_netCommandList->removeMessage(msg);
			deleteInstance(msg);
			continue;
		}

		if (msg->getTimeLastSent() != -1)
		{
			++m_numRetries;
			msg->setResent();

			const Bool isWrapper = msg->getCommand()->getNetCommandType() == NETCOMMANDTYPE_WRAPPER;
			if (isWrapper && (curtime - m_lastWrapperWindowCut) > m_retryTime)
			{
				m_wrapperWindow = max(WRAPPER_WINDOW_MIN, m_wrapperWindow / 2);
				m_lastWrapperWindowCut = curtime;
			}
		}

		doRetryMetrics();
		msg->setTimeLastSent(curtime);
	}
}

NetCommandRef * Connection::processAck(NetAckStage1CommandMsg *msg) {
	return processAck(msg->getCommandID(), msg->getOriginalPlayerID());
}

NetCommandRef * Connection::processAck(NetAckBothCommandMsg *msg) {
	return processAck(msg->getCommandID(), msg->getOriginalPlayerID());
}

NetCommandRef * Connection::processAck(NetCommandMsg *msg) {
	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKSTAGE1) {
		NetAckStage1CommandMsg *ackmsg = (NetAckStage1CommandMsg *)msg;
		return processAck(ackmsg);
	}

	if (msg->getNetCommandType() == NETCOMMANDTYPE_ACKBOTH) {
		NetAckBothCommandMsg *ackmsg = (NetAckBothCommandMsg *)msg;
		return processAck(ackmsg);
	}

	return nullptr;
}

/**
 * The person we are sending to has ack'd one of the messages we sent him.
 * Take that message off the list of commands to send.
 */
NetCommandRef * Connection::processAck(UnsignedShort commandID, UnsignedByte originalPlayerID) {
	NetCommandRef *temp = m_netCommandList->getFirstMessage();
	while ((temp != nullptr) && ((temp->getCommand()->getID() != commandID) || (temp->getCommand()->getPlayerID() != originalPlayerID))) {

		// cycle through the commands till we find the one we need to remove.
		// Need to check for both the command ID and the player ID.
		temp = temp->getNext();
	}
	if (temp == nullptr) {
		return nullptr;
	}

#if defined(RTS_DEBUG)
	Bool doDebug = FALSE;
	if (temp->getCommand()->getNetCommandType() == NETCOMMANDTYPE_DISCONNECTFRAME) {
		doDebug = TRUE;
	}
#endif

	if (!temp->wasResent())
	{
		Int index = temp->getCommand()->getID() % CONNECTION_LATENCY_HISTORY_LENGTH;
		// Relayed commands from different players share IDs, so count a slot only the first time it fills
		if (m_latencies[index] == 0 && m_latencySampleCount < CONNECTION_LATENCY_HISTORY_LENGTH)
		{
			++m_latencySampleCount;
		}
		m_averageLatency -= ((Real)(m_latencies[index])) / CONNECTION_LATENCY_HISTORY_LENGTH;
		Real lat = timeGetTime() - temp->getTimeLastSent();
		m_averageLatency += lat / CONNECTION_LATENCY_HISTORY_LENGTH;
		m_latencies[index] = lat;
		updateRetryTime();

		if (temp->getCommand()->getNetCommandType() == NETCOMMANDTYPE_WRAPPER)
		{
			m_wrapperWindow = min(WRAPPER_WINDOW_MAX, m_wrapperWindow + 1);
		}
	}

#if defined(RTS_DEBUG)
	if (doDebug == TRUE) {
		DEBUG_LOG(("Connection::processAck - disconnect frame command %d found, removing from command list.", commandID));
	}
#endif
	m_netCommandList->removeMessage(temp);
	return temp;
}

void Connection::setFrameGrouping(time_t frameGrouping) {
	m_frameGrouping = frameGrouping;
}

// Resend after 1.5 round trips instead of a fixed two seconds, so a lost packet on a slow link costs a fraction of a second
void Connection::updateRetryTime()
{
	// m_averageLatency divides by the full history, so scale it back up while the history is still filling
	Real meanLatency = m_averageLatency * CONNECTION_LATENCY_HISTORY_LENGTH / m_latencySampleCount;
	time_t minRetryTime = max(RETRY_TIME_MIN, 2 * m_frameGrouping);
	m_retryTime = clamp<time_t>(minRetryTime, (time_t)(meanLatency * 1.5f), RETRY_TIME_MAX);
}

void Connection::doRetryMetrics() {
	static Int numSeconds = 0;
	time_t curTime = timeGetTime();

	if ((curTime - m_retryMetricsTime) > 10000) {
		m_retryMetricsTime = curTime;
		++numSeconds;
//		DEBUG_LOG(("Retries in the last 10 seconds = %d, average latency = %fms", m_numRetries, m_averageLatency));
		m_numRetries = 0;
	}
}

#if defined(RTS_DEBUG)
void Connection::debugPrintCommands() {
	NetCommandRef *ref = m_netCommandList->getFirstMessage();
	while (ref != nullptr) {
		DEBUG_LOG(("Connection::debugPrintCommands - ID: %d\tType: %s\tRelay: 0x%X for frame %d",
			ref->getCommand()->getID(), GetNetCommandTypeAsString(ref->getCommand()->getNetCommandType()),
			ref->getRelay(), ref->getCommand()->getExecutionFrame()));
		ref = ref->getNext();
	}
}
#endif
