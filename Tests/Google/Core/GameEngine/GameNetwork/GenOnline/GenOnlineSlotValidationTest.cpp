#include <gtest/gtest.h>

#include "GameNetwork/GenOnline/GenOnlineSlotValidation.h"

TEST(GenOnlineSlotValidation, KeepsValuesInRange)
{
	EXPECT_EQ(GenOnlineSlotValueOrUnset(0, 8), 0);
	EXPECT_EQ(GenOnlineSlotValueOrUnset(7, 8), 7);
	EXPECT_EQ(GenOnlineSlotValueOrUnset(-1, 8), -1);
}

TEST(GenOnlineSlotValidation, UnsetsValuesOutOfRange)
{
	EXPECT_EQ(GenOnlineSlotValueOrUnset(8, 8), -1);
	EXPECT_EQ(GenOnlineSlotValueOrUnset(40, 8), -1);
	EXPECT_EQ(GenOnlineSlotValueOrUnset(-5, 8), -1);
	EXPECT_EQ(GenOnlineSlotValueOrUnset(0, 0), -1);
}
