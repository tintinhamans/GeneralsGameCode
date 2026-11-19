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
	// The old multiplier 1.0 is the neutral factor 50.
	CHECK(ReadOptionsIniCameraMultiplier("1.0") == 50);
	CHECK(ReadOptionsIniCameraMultiplier("1.5") == 75);
	CHECK(ReadOptionsIniCameraMultiplier("0.25") == 13);

	// The old reader clamped the multiplier to 0.1 to 10 before it was used, so 20 acted as 10.
	CHECK(ReadOptionsIniCameraMultiplier("0.1") == 5);
	CHECK(ReadOptionsIniCameraMultiplier("10") == 500);
	CHECK(ReadOptionsIniCameraMultiplier("20") == 500);
	CHECK(ReadOptionsIniCameraMultiplier("0.001") == 5);
	CHECK(ReadOptionsIniCameraMultiplier("-3") == 5);

	// The old reader used atof, so text reads as its leading number or as 0, which the clamp made 0.1.
	CHECK(ReadOptionsIniCameraMultiplier("1.5x") == 75);
	CHECK(ReadOptionsIniCameraMultiplier("fast") == 5);
	CHECK(ReadOptionsIniCameraMultiplier("") == 5);
	CHECK(ReadOptionsIniCameraMultiplier("1e9") == 500);
}

static void TestOptionsIniFontSize()
{
	CHECK(ReadOptionsIniFontSize("12") == 12);
	CHECK(ReadOptionsIniFontSize("-4") == 0);
	CHECK(ReadOptionsIniFontSize("40") == 15);

	// The old reader used atoi, so text reads as its leading whole number or as 0.
	CHECK(ReadOptionsIniFontSize("12.5") == 12);
	CHECK(ReadOptionsIniFontSize("big") == 0);
	CHECK(ReadOptionsIniFontSize("") == 0);
	CHECK(ReadOptionsIniFontSize("99999999999999999999") == 15);
}

static void TestOptionsIniObserverFlag()
{
	// The old reader took yes in any case as true and any other text as false.
	CHECK(ReadOptionsIniObserverFlag("yes"));
	CHECK(ReadOptionsIniObserverFlag("YES"));
	CHECK(ReadOptionsIniObserverFlag("Yes"));
	CHECK(!ReadOptionsIniObserverFlag("no"));
	CHECK(!ReadOptionsIniObserverFlag("maybe"));
	CHECK(!ReadOptionsIniObserverFlag("1"));
	CHECK(!ReadOptionsIniObserverFlag("true"));
	CHECK(!ReadOptionsIniObserverFlag("yess"));
	CHECK(!ReadOptionsIniObserverFlag(""));
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

static void TestDropPendingSettings()
{
	// A setting that still waits for its Options.ini value is not written to client.json.
	nlohmann::json root = {
		{ "camera", { { "min_height", 100 }, { "rotate_factor", 50 }, { "pitch_factor", 50 } } },
		{ "observer", { { "stats_font_size", 7 } } },
		{ "chat", { { "duration_seconds_until_fade_out", 30 } } },
	};
	DropPendingSettings(root, { "camera.rotate_factor", "observer.stats_font_size", "media.legacy" });

	CHECK(root["camera"].contains("min_height"));
	CHECK(!root["camera"].contains("rotate_factor"));
	CHECK(root["camera"].contains("pitch_factor"));
	CHECK(!root["observer"].contains("stats_font_size"));
	CHECK(root["chat"].contains("duration_seconds_until_fade_out"));
	CHECK(!root.contains("media"));

	// Without pending settings the document stays the same.
	const nlohmann::json before = root;
	DropPendingSettings(root, {});
	CHECK(root == before);
}

int main()
{
	TestDefaults();
	TestClientFileValues();
	TestOptionsIniCameraMultiplier();
	TestOptionsIniFontSize();
	TestOptionsIniObserverFlag();
	TestOptionsIniYesNo();
	TestDropPendingSettings();

	if (s_failures != 0)
	{
		std::printf("%d checks failed\n", s_failures);
		return 1;
	}
	std::printf("All client option checks passed\n");
	return 0;
}
