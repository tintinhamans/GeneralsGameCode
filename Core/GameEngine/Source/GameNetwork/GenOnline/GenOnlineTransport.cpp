#include "PreRTS.h" // This must go first in EVERY cpp file int the GameEngine

#include "Common/CRC.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GenOnline/GenOnlineTransport.h"
#include "GameNetwork/TransportFactory.h"

#include "GameNetwork/GenOnline/GenOnlineInclude.h"
#include "GameNetwork/GenOnline/GenOnlineInterfaces.h"
#include <steam/steamnetworkingtypes.h>
#include "GameNetwork/GenOnline/Provider/GenOnlinePluginHost.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

static Transport *createGenOnlineTransport()
{
    return new GenOnlineTransport;
}

void RegisterGenOnlineTransport()
{
    TransportFactory::registerTransport(GENONLINE_TRANSPORT_NAME, TRANSPORT_CAPABILITY_NONE, createGenOnlineTransport);
}

GenOnlineTransport::GenOnlineTransport()
{
    // Initialize statistics tracking
    m_statisticsSlot = 0;
    m_lastSecond = timeGetTime();
    m_useLatency = FALSE;
    m_usePacketLoss = FALSE;
}

GenOnlineTransport::~GenOnlineTransport()
{
    reset();
}

Bool GenOnlineTransport::init(AsciiString ip, UnsignedShort port)
{
    return TRUE;
}

Bool GenOnlineTransport::init(UnsignedInt ip, UnsignedShort port)
{
    return TRUE;
}

void GenOnlineTransport::reset(void)
{
    // Clear buffers and statistics to avoid stale state.
    std::memset(m_inBuffer, 0, sizeof(m_inBuffer));
    std::memset(m_outBuffer, 0, sizeof(m_outBuffer));
    std::memset(m_incomingPackets, 0, sizeof(m_incomingPackets));
    std::memset(m_incomingBytes, 0, sizeof(m_incomingBytes));
    std::memset(m_outgoingPackets, 0, sizeof(m_outgoingPackets));
    std::memset(m_outgoingBytes, 0, sizeof(m_outgoingBytes));
    std::memset(m_unknownPackets, 0, sizeof(m_unknownPackets));
    std::memset(m_unknownBytes, 0, sizeof(m_unknownBytes));
    
    // Clear retry state for all outgoing packets
    for (int i = 0; i < MAX_MESSAGES; ++i)
    {
        m_outPacketState[i].retryCount = 0;
        m_inBufferOccupied[i] = false;  // Mark all incoming slots as empty
    }
}

Bool GenOnlineTransport::update(void)
{
    Bool retval = TRUE;

    if (doRecv() == FALSE)
    {
        retval = FALSE;
    }
    if (doSend() == FALSE)
    {
        retval = FALSE;
    }

    return retval;
}

Bool GenOnlineTransport::doRecv(void)
{
    // The plugin transport moves packets only inside its tick, so it takes in what arrived since then before the receive.
    GenOnlinePluginHost::Transport::Pump();

    bool bRet = FALSE;
    int numRead = 0;

    // Statistics gathering - advance slot every second (same as UDPTransport)
    UnsignedInt now = timeGetTime();
    if (m_lastSecond + 1000 < now)
    {
        m_lastSecond = now;
        m_statisticsSlot = (m_statisticsSlot + 1) % MAX_TRANSPORT_STATISTICS_SECONDS;
        m_outgoingPackets[m_statisticsSlot] = 0;
        m_outgoingBytes[m_statisticsSlot] = 0;
        m_incomingPackets[m_statisticsSlot] = 0;
        m_incomingBytes[m_statisticsSlot] = 0;
        m_unknownPackets[m_statisticsSlot] = 0;
        m_unknownBytes[m_statisticsSlot] = 0;
    }

    auto* pLobbyInterface =
        GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
    if (!pLobbyInterface)
    {
        NetworkLog(ELogVerbosity::LOG_DEBUG, "Game Packet Recv: No lobby interface");
        return FALSE;
    }

    NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
    if (!pMesh)
    {
        NetworkLog(ELogVerbosity::LOG_DEBUG, "Game Packet Recv: No network mesh");
        return FALSE;
    }

    std::map<int64_t, PlayerConnection>& connections = pMesh->GetAllConnections();
    for (auto& kvPair : connections)
    {
        // Plugin connections share one plugin queue, the mesh receives their packets below.
        if (!kvPair.second.ReceivesPerConnection())
        {
            continue;
        }

        // Only take as many messages as there are free slots, the rest stay queued in GNS
        const int numFreeSlots = countFreeInSlots();

        if (numFreeSlots == 0)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Recv: m_inBuffer is full, leaving packets from user %lld queued",
                static_cast<long long>(kvPair.second.m_userID));
            continue;
        }

        PlayerConnection& connection = kvPair.second;
        connection.ReceiveFrames(numFreeSlots, [&](const uint8_t* data, uint32_t len, int64_t arrivalMs)
        {
            if (dispatchFrame(connection, data, len, arrivalMs))
            {
                ++numRead;
                bRet = TRUE;
            }
        });
    }

    // Only take as many plugin packets as there are free slots, the rest stay queued in the plugin.
    const int numFreePluginSlots = countFreeInSlots();
    if (numFreePluginSlots == 0)
    {
        if (pMesh->HasPluginPackets())
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "Game Packet Recv: m_inBuffer is full, leaving plugin packets queued");
        }
    }
    else
    {
        pMesh->ReceivePluginPackets(numFreePluginSlots, [&](PlayerConnection& connection, const uint8_t* data, uint32_t len, int64_t arrivalMs)
        {
            if (dispatchFrame(connection, data, len, arrivalMs))
            {
                ++numRead;
                bRet = TRUE;
            }
        });
    }


    NetworkLog(ELogVerbosity::LOG_DEBUG,
        "Game Packet Recv: Read %d packets this frame", numRead);

    return bRet;
}

Int GenOnlineTransport::countFreeInSlots() const
{
    int numFreeSlots = 0;
    for (int iSlot = 0; iSlot < MAX_MESSAGES; ++iSlot)
    {
        if (!m_inBufferOccupied[iSlot] || m_inBuffer[iSlot].length == 0)
        {
            ++numFreeSlots;
        }
    }
    return numFreeSlots;
}

Bool GenOnlineTransport::dispatchFrame(PlayerConnection& connection, const uint8_t* data, uint32_t len, int64_t arrivalMs)
{
    const long long userID = static_cast<long long>(connection.m_userID);

    // all packets must have at least 1 byte to indicate channel
    if (data == nullptr || len < sizeof(ENetworkChannel))
    {
        // Malformed packet - too small for header
        NetworkLog(ELogVerbosity::LOG_RELEASE, "[NET PACKET] Dropping malformed packet - size %u is less than header size 1 from user %lld", len, userID);
        return FALSE;
    }

    const ENetworkChannel netChannel = static_cast<ENetworkChannel>(data[0]);
    const uint8_t* pPacket = data + sizeof(ENetworkChannel);
    size_t packetSize = len - sizeof(ENetworkChannel);

    if (connection.DispatchControlFrame(netChannel, pPacket, packetSize, arrivalMs))
    {
        return FALSE;
    }

    if (netChannel != ENetworkChannel::NETWORK_CHANNEL_GAME)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "Game Packet Recv: Dropping packet with unknown channel %u from user %lld", static_cast<unsigned>(data[0]), userID);
        return FALSE;
    }

    NetworkLog(ELogVerbosity::LOG_DEBUG,
        "[GAME PACKET] Received message of size %u from user %lld",
        static_cast<unsigned>(packetSize), userID);

    // the packet envelope: sequence number and receiver feedback
    const size_t envelopeSize = connection.ConsumeEnvelope(pPacket, packetSize);
    if (envelopeSize == 0)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE, "Game Packet Recv: Dropping packet without a valid envelope from user %lld, probably another game version", userID);
        return FALSE;
    }
    pPacket += envelopeSize;
    packetSize -= envelopeSize;

    // Must at least contain the header
    if (packetSize < sizeof(TransportMessageHeader))
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet Recv: Dropping packet smaller than header (%u < %zu)",
            static_cast<unsigned>(packetSize), sizeof(TransportMessageHeader));
        return FALSE;
    }

    // Max bytes we ever expect from the wire:
    // header + payload (no trailing length/addr/port)
    const uint32_t maxWireSize =
        static_cast<uint32_t>(sizeof(TransportMessageHeader) + MAX_MESSAGE_LEN);

    if (packetSize > maxWireSize)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet Recv: Dropping packet too large (%u > %u)",
            static_cast<unsigned>(packetSize), maxWireSize);
        return FALSE;
    }

    TransportMessage incomingMessage{};

    // Clear incomingMessage, then copy header + payload region only
    std::memset(&incomingMessage, 0, sizeof(incomingMessage));

    // Copy header safely
    std::memcpy(&incomingMessage.header,
        pPacket,
        sizeof(TransportMessageHeader));

    // Compute payload length
    const uint32_t payloadLen =
        static_cast<uint32_t>(packetSize) - static_cast<uint32_t>(sizeof(TransportMessageHeader));

    // Sanity check payloadLen against local buffer size
    if (payloadLen > sizeof(incomingMessage.data))
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet Recv: Dropping packet, payloadLen (%u) > incoming buffer (%zu)",
            payloadLen, sizeof(incomingMessage.data));
        return FALSE;
    }

    // Copy payload into data[]
    if (payloadLen > 0)
    {
        std::memcpy(incomingMessage.data,
            pPacket + sizeof(TransportMessageHeader),
            payloadLen);
    }

    // Length is bounded by sizeof(data), so cast is safe
    incomingMessage.length = static_cast<Int>(payloadLen);

#if defined(RTS_DEBUG) || defined(RTS_INTERNAL)
    if (m_usePacketLoss)
    {
        // Drop packet if random value is below loss percentage
        // E.g., if m_packetLoss = 50, drop ~50% of packets
        if (TheGlobalData->m_packetLoss > GameClientRandomValue(0, 100))
        {
            // Simulated packet loss
            NetworkLog(ELogVerbosity::LOG_DEBUG,
                "Game Packet Recv: Simulated packet loss (loss%%=%d)",
                TheGlobalData->m_packetLoss);
            return FALSE;
        }
    }
#endif

    const bool isGenerals = isGeneralsPacket(&incomingMessage);

    if (!isGenerals)
    {
        // Check if it's a CRC failure or magic number failure to help diagnose corruption
        if (incomingMessage.header.magic != GENERALS_MAGIC_NUMBER)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Recv: BAD MAGIC NUMBER - Expected 0x%04X, got 0x%04X from user %lld. "
                "Packet is corrupted or from wrong game version.",
                GENERALS_MAGIC_NUMBER, incomingMessage.header.magic,
                userID);
        }
        else
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Recv: CRC MISMATCH - Expected 0x%08X, got 0x%08X from user %lld. "
                "Packet is corrupted during transmission or has invalid payload length (%u).",
                incomingMessage.header.crc, 0, // We'd need to compute the CRC to compare
                userID, incomingMessage.length);
        }
        m_unknownPackets[m_statisticsSlot]++;
        m_unknownBytes[m_statisticsSlot] += static_cast<UnsignedInt>(packetSize);
        return FALSE;
    }

    m_incomingPackets[m_statisticsSlot]++;
    m_incomingBytes[m_statisticsSlot] += static_cast<UnsignedInt>(packetSize);

    // Store into first free slot in m_inBuffer
    bool stored = false;
    int fullCount = 0;
    for (int i = 0; i < MAX_MESSAGES; ++i)
    {
        // Check if slot is occupied using flag, not length
        // (length could be 0 for legitimate empty packets)
        // However, if the packet has been consumed (length cleared to 0 by outside code),
        // clear the occupied flag too
        if (m_inBuffer[i].length == 0 && m_inBufferOccupied[i])
        {
            m_inBufferOccupied[i] = false;
        }

        if (m_inBufferOccupied[i])
        {
            fullCount++;
            continue;
        }

        // Clear slot
        std::memset(&m_inBuffer[i], 0, sizeof(m_inBuffer[i]));

        // the connection the packet arrived on says which user sent it
        m_inBuffer[i].senderKnown = true;
        m_inBuffer[i].senderUserID = static_cast<Int64>(connection.m_userID);

        // Copy header
        m_inBuffer[i].header = incomingMessage.header;

        // Copy payload with bounds check
        if (payloadLen > 0)
        {
            const size_t dstCap = sizeof(m_inBuffer[i].data);
            const size_t toCopy = (payloadLen <= dstCap) ? payloadLen : dstCap;

            if (payloadLen > dstCap)
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE,
                    "Game Packet Recv: WARNING - Truncating payload from %u to %zu bytes for inBuffer[%d] from user %lld. "
                    "This indicates the incoming packet exceeds the buffer capacity and data will be lost. "
                    "Consider increasing MAX_MESSAGE_LEN or MAX_PACKET_SIZE.",
                    payloadLen, dstCap, i, userID);
            }

            std::memcpy(m_inBuffer[i].data,
                incomingMessage.data,
                toCopy);

            m_inBuffer[i].length = static_cast<Int>(toCopy);
        }
        else
        {
            // Zero-length packet - store with length=0 but mark as occupied
            m_inBuffer[i].length = 0;
        }

        // Mark slot as occupied
        m_inBufferOccupied[i] = true;
        stored = true;
        break;
    }

    if (!stored)
    {
        // Buffer is full - log this as it indicates potential packet loss
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet Recv: ERROR - m_inBuffer is FULL (%d/%d slots occupied), dropping packet from user %lld. "
            "Incoming packets will be lost until buffer slots are freed. "
            "Consider increasing MAX_MESSAGES (%d) to handle higher packet rates.",
            fullCount, MAX_MESSAGES, userID, MAX_MESSAGES);
        return FALSE;
    }

    return TRUE;
}

Bool GenOnlineTransport::sendFailureLogDue()
{
	const UnsignedInt nowMs = timeGetTime();
	if (static_cast<Int>(nowMs - m_nextSendFailLogMs) < 0)
	{
		++m_skippedSendFailLogs;
		return FALSE;
	}
	m_nextSendFailLogMs = nowMs + 1000;
	return TRUE;
}

Bool GenOnlineTransport::doSend(void)
{
    const Bool bSent = sendQueued();
    // The plugin transport sends the packets of this frame now instead of at its next main loop tick.
    GenOnlinePluginHost::Transport::Pump();
    return bSent;
}

Bool GenOnlineTransport::sendQueued()
{
    Bool retval = TRUE;
    int numSent = 0;

    for (int i = 0; i < MAX_MESSAGES; ++i)
    {
        if (m_outBuffer[i].length == 0)
        {
            m_outPacketState[i].retryCount = 0;  // Reset retry counter when packet slot is cleared
            continue;
        }

        GenOnlineServicesManager* pOnlineServicesManager = GenOnlineServicesManager::GetInstance();
        if (pOnlineServicesManager == nullptr)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Send: No OnlineServicesManager");
            return FALSE;
        }

        GenOnlineLobbyInterface* pLobbyInterface =
            GenOnlineServicesManager::GetInterface<GenOnlineLobbyInterface>();
        if (pLobbyInterface == nullptr)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Send: No LobbyInterface");
            return FALSE;
        }

        if (TheGenOnlineGame == nullptr)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Send: TheNGMPGame is null");
            return FALSE;
        }

        GenOnlineGameSlot* pSlot =
            static_cast<GenOnlineGameSlot*>(TheGenOnlineGame->getSlot(m_outBuffer[i].addr));

        if (pSlot != nullptr)
        {
            const uint32_t totalLen =
                static_cast<uint32_t>(m_outBuffer[i].length) + sizeof(TransportMessageHeader);

            // Sanity check against some reasonable upper bound
            if (totalLen > (sizeof(TransportMessageHeader) + MAX_PACKET_SIZE))
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE,
                    "Game Packet Send: totalLen (%u) exceeds allowed max (%zu)",
                    totalLen,
                    sizeof(TransportMessageHeader) + static_cast<size_t>(MAX_PACKET_SIZE));
                m_outBuffer[i].length = 0; // drop this entry
                m_outPacketState[i].retryCount = 0;
                retval = FALSE;
                continue;
            }

            NetworkMesh* pMesh = GenOnlineServicesManager::GetNetworkMesh();
            if (pMesh == nullptr)
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE,
                    "Game Packet Send: No network mesh");
                // Don't clear the packet - retry next frame
                retval = FALSE;
                continue;
            }

            // CRITICAL FIX: Create a temporary buffer with ONLY header + data
            // Do NOT send the entire TransportMessage struct which contains metadata
            // (length, addr, port fields that corrupt the packet on the wire)
            std::vector<byte> packetData;
            packetData.resize(totalLen);
            
            // Copy header
            std::memcpy(packetData.data(), 
                       &m_outBuffer[i].header, 
                       sizeof(TransportMessageHeader));
            
            // Copy payload data
            std::memcpy(packetData.data() + sizeof(TransportMessageHeader),
                       m_outBuffer[i].data,
                       m_outBuffer[i].length);

            int sendResult =
                pMesh->SendGamePacket(
                    packetData.data(),  // Send only header + data, NOT entire struct
                    totalLen,
                    pSlot->m_userID);

            if (sendResult >= 0)
            {
                // Send successful
                ++numSent;
                m_outgoingPackets[m_statisticsSlot]++;
                m_outgoingBytes[m_statisticsSlot] +=
                    m_outBuffer[i].length + sizeof(TransportMessageHeader);
                m_outBuffer[i].length = 0; // Remove from queue
                m_outPacketState[i].retryCount = 0;
                retval = TRUE;
            }
            else
            {
                // Send failed - implement retry logic for transient errors
                m_outPacketState[i].retryCount++;
                
                if (m_outPacketState[i].retryCount < OutgoingPacketState::MAX_RETRIES)
                {
                    if (sendFailureLogDue())
                    {
                        NetworkLog(ELogVerbosity::LOG_RELEASE,
                            "Game Packet Send: SendGamePacket failed (err=%d), retry %d/%d for packet to user %lld, %d lines skipped",
                            sendResult, m_outPacketState[i].retryCount,
                            OutgoingPacketState::MAX_RETRIES, pSlot->m_userID, m_skippedSendFailLogs);
                        m_skippedSendFailLogs = 0;
                    }
                    // Keep packet in queue for retry
                    retval = FALSE;
                }
                else
                {
                    // Max retries exceeded - drop packet
                    if (sendFailureLogDue())
                    {
                        NetworkLog(ELogVerbosity::LOG_RELEASE,
                            "Game Packet Send: Dropping packet after %d failed retries to user %lld, %d lines skipped",
                            m_outPacketState[i].retryCount, pSlot->m_userID, m_skippedSendFailLogs);
                        m_skippedSendFailLogs = 0;
                    }
                    m_outBuffer[i].length = 0;
                    m_outPacketState[i].retryCount = 0;
                    retval = FALSE;
                }
            }
        }
        else
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Send: No slot for addr %u, dropping packet", m_outBuffer[i].addr);
            m_outBuffer[i].length = 0;
            m_outPacketState[i].retryCount = 0;
            retval = FALSE;
        }
    }

    NetworkLog(ELogVerbosity::LOG_DEBUG,
        "Game Packet Send: Sent %d packets this frame", numSent);

    return retval;
}

Bool GenOnlineTransport::queueSend(UnsignedInt addr,
    UnsignedShort port,
    const UnsignedByte* buf,
    Int len /*, NetMessageFlags flags, Int id */)
{
    if (buf == nullptr)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet QueueSend: null buffer");
        return FALSE;
    }

    if (len < 1 || len > MAX_PACKET_SIZE)
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet QueueSend: invalid length %d (max %d)",
            len, MAX_PACKET_SIZE);
        return FALSE;
    }

    for (int i = 0; i < MAX_MESSAGES; ++i)
    {
        if (m_outBuffer[i].length != 0)
            continue;

        const size_t dstCap = sizeof(m_outBuffer[i].data);
        if (static_cast<size_t>(len) > dstCap)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet QueueSend: len (%d) > outBuffer[%d].data capacity (%zu)",
                len, i, dstCap);
            return FALSE;
        }

        // Insert data here
        std::memset(&m_outBuffer[i], 0, sizeof(m_outBuffer[i]));

        m_outBuffer[i].length = len;
        std::memcpy(m_outBuffer[i].data, buf, static_cast<size_t>(len));
        m_outBuffer[i].addr = addr;
        m_outBuffer[i].port = port;

        m_outBuffer[i].header.magic = GENERALS_MAGIC_NUMBER;

        CRC crc;
        // CRC over header.magic through end of payload
        const size_t crcLen =
            static_cast<size_t>(m_outBuffer[i].length) +
            sizeof(TransportMessageHeader) - sizeof(UnsignedInt);

        if (crcLen > sizeof(m_outBuffer[i]) - offsetof(TransportMessage, header.magic))
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet QueueSend: CRC length overflow, crcLen=%zu", crcLen);
            m_outBuffer[i].length = 0;
            return FALSE;
        }

        crc.computeCRC(
            reinterpret_cast<unsigned char*>(&(m_outBuffer[i].header.magic)),
            static_cast<unsigned int>(crcLen));

        m_outBuffer[i].header.crc = crc.get();

        if (!isGeneralsPacket(&m_outBuffer[i]))
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE,
                "Game Packet Queue Sending: Is NOT a generals packet");
        }

        return TRUE;
    }

    if (sendFailureLogDue())
    {
        NetworkLog(ELogVerbosity::LOG_RELEASE,
            "Game Packet QueueSend: m_outBuffer full, dropping packet, %d lines skipped", m_skippedSendFailLogs);
        m_skippedSendFailLogs = 0;
    }
    return FALSE;
}
