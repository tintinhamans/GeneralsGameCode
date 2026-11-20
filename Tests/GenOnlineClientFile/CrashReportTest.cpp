// Standalone checks of the crash report rules in GenOnlineCrashReport. Builds without the game, see CMakeLists.txt.
#include "GameNetwork/GenOnline/GenOnlineCrashReport.h"
#include <cstdio>

using namespace GenOnlineCrashReport;

static int s_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			++s_failures; \
		} \
	} while (0)

static void TestWithoutPlayerNames()
{
	// The details as GameLogic builds them for a mismatch.
	const std::wstring details =
		L"GameLogic frame 1200, latest frame 1195, GetGameLogicRandomSeedCRC was 77\n"
		L"Had 3 CRCs from 3 players\n"
		L"Mismatched Players:\n"
		L"player 2 (Alice) = 1A2B [MISMATCH]\n"
		L"player 5 (Bob (x) = 7) = FF [MISMATCH]\n";
	const std::wstring expected =
		L"GameLogic frame 1200, latest frame 1195, GetGameLogicRandomSeedCRC was 77\n"
		L"Had 3 CRCs from 3 players\n"
		L"Mismatched Players:\n"
		L"player 2 = 1A2B [MISMATCH]\n"
		L"player 5 = FF [MISMATCH]\n";
	CHECK(WithoutPlayerNames(details) == expected);

	CHECK(WithoutPlayerNames(L"").empty());
	CHECK(WithoutPlayerNames(L"player 1 (<NONE>) = 0 [MISMATCH]") == L"player 1 = 0 [MISMATCH]");

	// A line in an unknown shape may hold a name and is dropped.
	CHECK(WithoutPlayerNames(L"player 3 Carol = 12 [MISMATCH]\n") == L"");
	CHECK(WithoutPlayerNames(L"Carol\nHad 2 CRCs from 2 players\n") == L"Had 2 CRCs from 2 players\n");

	// A name that spans lines leaves nothing of itself behind.
	const std::wstring split = WithoutPlayerNames(L"player 4 (Da\nve) = 3 [MISMATCH]\n");
	CHECK(split.find(L"Da") == std::wstring::npos);
	CHECK(split.find(L"ve") == std::wstring::npos);
}

int main()
{
	TestWithoutPlayerNames();
	if (s_failures != 0)
	{
		std::printf("%d check(s) failed\n", s_failures);
		return 1;
	}
	std::printf("All crash report checks passed\n");
	return 0;
}
