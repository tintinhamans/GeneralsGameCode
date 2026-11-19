// Standalone checks of the client.json settings that were Options.ini keys, see GenOnlineClientOptions. Builds without the game, see CMakeLists.txt.
#include "GameNetwork/GenOnline/GenOnlineClientOptions.h"
#include <cstdio>

using namespace GenOnlineClientOptions;

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

static void TestDefaults()
{
	// The camera factors default to the neutral 50 of the scroll factor.
	CHECK(CameraFactorDefault == 50);
	CHECK(CameraFactorMin == 1);
	CHECK(CameraFactorToReal(CameraFactorDefault) == 0.5f);
	CHECK(CameraFactorToReal(145) == 1.45f);
	CHECK(CameraFactorToReal(1) == 0.01f);

	CHECK(ObserverStatsFontSizeDefault == 7);
	CHECK(ObserverNotificationFontSizeDefault == 10);
	CHECK(ObserverFontSizeMin == 0);
	CHECK(ObserverFontSizeMax == 15);
	CHECK(ObserverNotificationDefault);
	CHECK(!LegacyMediaDefault);
}

static void TestClientFileValues()
{
	// A client.json camera factor is rounded, a value below 1 becomes 1 and values above the slider range of 145 stay.
	CHECK(ClampCameraFactor(50.0) == 50);
	CHECK(ClampCameraFactor(72.6) == 73);
	CHECK(ClampCameraFactor(1.0) == 1);
	CHECK(ClampCameraFactor(0.0) == 1);
	CHECK(ClampCameraFactor(-5.0) == 1);
	CHECK(ClampCameraFactor(146.0) == 146);
	CHECK(ClampCameraFactor(500.0) == 500);
	CHECK(ClampCameraFactor(-1.0e9) == 1);
	CHECK(ClampCameraFactor(1.0e300) == CameraFactorMax);

	// A client.json observer font size drops its fraction and is clamped to 0 to 15.
	CHECK(ClampObserverFontSize(7.0) == 7);
	CHECK(ClampObserverFontSize(7.9) == 7);
	CHECK(ClampObserverFontSize(-1.0) == 0);
	CHECK(ClampObserverFontSize(16.0) == 15);
	CHECK(ClampObserverFontSize(1.0e9) == 15);
}

static void TestOptionsIniCameraMultiplier()
{
	int factor = 0;

	// The old multiplier 1.0 is the neutral factor 50.
	CHECK(ParseOptionsIniCameraMultiplier("1.0", factor));
	CHECK(factor == 50);

	CHECK(ParseOptionsIniCameraMultiplier("1.5", factor));
	CHECK(factor == 75);

	CHECK(ParseOptionsIniCameraMultiplier("0.25", factor));
	CHECK(factor == 13);

	// The old range of 0.1 to 10 becomes 5 to 500, a value below 1 becomes 1.
	CHECK(ParseOptionsIniCameraMultiplier("0.1", factor));
	CHECK(factor == 5);

	CHECK(ParseOptionsIniCameraMultiplier("10", factor));
	CHECK(factor == 500);

	CHECK(ParseOptionsIniCameraMultiplier("0.001", factor));
	CHECK(factor == 1);

	CHECK(ParseOptionsIniCameraMultiplier("-3", factor));
	CHECK(factor == 1);

	// A value that is not a number is not taken.
	factor = 99;
	CHECK(!ParseOptionsIniCameraMultiplier("fast", factor));
	CHECK(!ParseOptionsIniCameraMultiplier("", factor));
	CHECK(!ParseOptionsIniCameraMultiplier("1.5x", factor));
	CHECK(!ParseOptionsIniCameraMultiplier("nan", factor));
	CHECK(factor == 99);
}

static void TestOptionsIniFontSize()
{
	int size = 0;

	CHECK(ParseOptionsIniFontSize("12", size));
	CHECK(size == 12);

	CHECK(ParseOptionsIniFontSize("-4", size));
	CHECK(size == 0);

	CHECK(ParseOptionsIniFontSize("40", size));
	CHECK(size == 15);

	size = 99;
	CHECK(!ParseOptionsIniFontSize("big", size));
	CHECK(!ParseOptionsIniFontSize("12.5", size));
	CHECK(!ParseOptionsIniFontSize("", size));
	CHECK(size == 99);
}

static void TestOptionsIniYesNo()
{
	bool value = false;

	CHECK(ParseOptionsIniYesNo("yes", value));
	CHECK(value);

	CHECK(ParseOptionsIniYesNo("YES", value));
	CHECK(value);

	CHECK(ParseOptionsIniYesNo("no", value));
	CHECK(!value);

	CHECK(ParseOptionsIniYesNo("No", value));
	CHECK(!value);

	value = true;
	CHECK(!ParseOptionsIniYesNo("maybe", value));
	CHECK(!ParseOptionsIniYesNo("", value));
	CHECK(value);
}

int main()
{
	TestDefaults();
	TestClientFileValues();
	TestOptionsIniCameraMultiplier();
	TestOptionsIniFontSize();
	TestOptionsIniYesNo();

	if (s_failures != 0)
	{
		std::printf("%d checks failed\n", s_failures);
		return 1;
	}
	std::printf("All client option checks passed\n");
	return 0;
}
