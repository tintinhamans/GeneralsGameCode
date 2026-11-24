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

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Common/GameMemory.h"
#include "GameNetwork/NetCommandMsg.h"
#include "GameNetwork/NetCommandRef.h"
#include "GameNetwork/NetCommandWrapperList.h"

namespace
{

const UnsignedShort WRAPPED_ID = 7;

void processChunk(NetCommandWrapperList *list, UnsignedInt numChunks, UnsignedInt totalDataLength, const std::vector<UnsignedByte> &data)
{
	NetWrapperCommandMsg *msg = newInstance(NetWrapperCommandMsg);
	msg->setPlayerID(0);
	msg->setWrappedCommandID(WRAPPED_ID);
	msg->setNumChunks(numChunks);
	msg->setTotalDataLength(totalDataLength);
	msg->setChunkNumber(0);
	msg->setDataOffset(0);
	NetCommandDataChunk chunk((UnsignedInt)data.size());
	if (!data.empty())
	{
		memcpy(chunk.data(), &data[0], data.size());
	}
	msg->setData(chunk);

	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	msg->detach();
	list->processWrapper(ref);
	deleteInstance(ref);
}

Int readyCommandCount(NetCommandWrapperList *list)
{
	NetCommandList *ready = list->getReadyCommands();
	const Int count = ready->length();
	deleteInstance(ready);
	return count;
}

std::vector<UnsignedByte> keepAliveBytes()
{
	NetDisconnectKeepAliveCommandMsg *msg = newInstance(NetDisconnectKeepAliveCommandMsg);
	msg->setPlayerID(0);
	NetCommandRef *ref = NEW_NETCOMMANDREF(msg);
	msg->detach();
	std::vector<UnsignedByte> bytes(ref->getCommand()->getSizeForNetPacket());
	bytes.resize(ref->getCommand()->copyBytesForNetPacket(&bytes[0], *ref));
	deleteInstance(ref);
	return bytes;
}

void ignoreCrash(const char *)
{
}

// The parser reports an unknown command type as a debug crash, which these tests expect.
class ExpectedCrashes
{
public:
	ExpectedCrashes() : m_previous(DebugGetCrashHandler()) { DebugSetCrashHandler(ignoreCrash); }
	~ExpectedCrashes() { DebugSetCrashHandler(m_previous); }

private:
	DebugCrashHandler m_previous;
};

class NetCommandWrapperListTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_list = newInstance(NetCommandWrapperList);
		m_list->init();
	}

	void TearDown() override
	{
		deleteInstance(m_list);
	}

	NetCommandWrapperList *m_list = nullptr;
};

} // namespace

TEST_F(NetCommandWrapperListTest, DeliversAWrappedCommand)
{
	const std::vector<UnsignedByte> bytes = keepAliveBytes();
	processChunk(m_list, 1, (UnsignedInt)bytes.size(), bytes);
	EXPECT_EQ(readyCommandCount(m_list), 1);
}

TEST_F(NetCommandWrapperListTest, DropsAWrappedCommandOfUnknownType)
{
	ExpectedCrashes expected;
	processChunk(m_list, 1, 1, std::vector<UnsignedByte>(1, 'X'));
	EXPECT_EQ(readyCommandCount(m_list), 0);
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
}

TEST_F(NetCommandWrapperListTest, IgnoresAWrapperWithoutChunks)
{
	processChunk(m_list, 0, 16, std::vector<UnsignedByte>());
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
	EXPECT_EQ(readyCommandCount(m_list), 0);
}

TEST_F(NetCommandWrapperListTest, IgnoresAWrapperWithoutData)
{
	processChunk(m_list, 1, 0, std::vector<UnsignedByte>());
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
}

TEST_F(NetCommandWrapperListTest, IgnoresAWrapperLargerThanTheLimit)
{
	processChunk(m_list, 0xFFFFFFFF, 0xFFFFFFFF, std::vector<UnsignedByte>(1, 0));
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
}

TEST_F(NetCommandWrapperListTest, IgnoresAWrapperWithMoreChunksThanBytes)
{
	processChunk(m_list, 1000, 10, std::vector<UnsignedByte>(10, 0));
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
}

TEST_F(NetCommandWrapperListTest, IgnoresAWrapperWithTooFewChunksForItsData)
{
	processChunk(m_list, 1, MAX_PACKET_SIZE + 1, std::vector<UnsignedByte>(1, 0));
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 0);
}

TEST_F(NetCommandWrapperListTest, KeepsAnIncompleteWrapper)
{
	processChunk(m_list, 2, 2, std::vector<UnsignedByte>(1, 0));
	EXPECT_EQ(m_list->getPercentComplete(WRAPPED_ID), 50);
	EXPECT_EQ(readyCommandCount(m_list), 0);
}
