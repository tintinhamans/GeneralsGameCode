#pragma once
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

// The defaults and ranges of the client.json settings of the camera and the observer overlay and of the legacy media setting, and the
// reading of the old Options.ini values of those that Options.ini held before. Options.ini keeps only the keys of the upstream game.
// A value is taken from Options.ini once, when client.json does not have it yet.
namespace GenOnlineClientOptions
{
	// The camera rotate, pitch and zoom factors work like the ScrollFactor of the Options menu. The camera reads the whole number as
	// value / 100 and multiplies it by SCROLL_MULTIPLIER, so the default 50 is a neutral 1x multiplier. A value below 1 becomes 1.
	// The upper bound only keeps the whole number and its float exact, it is far above any usable speed.
	constexpr int CameraFactorDefault = 50;
	constexpr int CameraFactorMin = 1;
	constexpr int CameraFactorMax = 1000000;

	constexpr int ObserverFontSizeMin = 0;
	constexpr int ObserverFontSizeMax = 15;
	constexpr int ObserverStatsFontSizeDefault = 7;
	constexpr int ObserverNotificationFontSizeDefault = 10;
	constexpr bool ObserverNotificationDefault = true;

	constexpr bool LegacyMediaDefault = false;

	// The Options.ini keys these settings had.
	constexpr const char* OptionsIniCameraRotate = "CameraRotateSpeedMultiplier";
	constexpr const char* OptionsIniCameraPitch = "CameraPitchSpeedMultiplier";
	constexpr const char* OptionsIniLegacyMedia = "LegacyMedia";
	constexpr const char* OptionsIniObserverStatsFontSize = "ObserverStatsFontSize";
	constexpr const char* OptionsIniObserverNotificationFontSize = "ObserverNotificationFontSize";
	constexpr const char* OptionsIniObserverNotificationSpecialPowerUsage = "ObserverNotificationSpecialPowerUsage";
	constexpr const char* OptionsIniObserverNotificationSpecialPowerPurchase = "ObserverNotificationSpecialPowerPurchase";
	constexpr const char* OptionsIniObserverNotificationMilestone = "ObserverNotificationMilestone";

	inline float CameraFactorToReal(int factor)
	{
		return factor / 100.0f;
	}

	// Rounds a camera factor to a whole number of at least 1.
	inline int ClampCameraFactor(double factor)
	{
		if (!std::isfinite(factor))
		{
			return CameraFactorDefault;
		}
		const double clamped = std::clamp(factor, static_cast<double>(CameraFactorMin), static_cast<double>(CameraFactorMax));
		return static_cast<int>(std::lround(clamped));
	}

	// Drops the fraction of an observer font size and clamps it to 0 to 15.
	inline int ClampObserverFontSize(double size)
	{
		if (!std::isfinite(size))
		{
			return ObserverFontSizeMin;
		}
		const double clamped = std::clamp(size, static_cast<double>(ObserverFontSizeMin), static_cast<double>(ObserverFontSizeMax));
		return static_cast<int>(clamped);
	}

	// Takes the old CameraRotateSpeedMultiplier or CameraPitchSpeedMultiplier, where 1.0 was neutral, as the factor of the multiplier times 50, rounded and at least 1.
	// Returns false and leaves factor alone when the text is not a number.
	inline bool ParseOptionsIniCameraMultiplier(const char* text, int& factor)
	{
		if (text == nullptr || *text == '\0')
		{
			return false;
		}
		char* end = nullptr;
		errno = 0;
		const double multiplier = std::strtod(text, &end);
		if (end == text || *end != '\0' || errno == ERANGE || !std::isfinite(multiplier))
		{
			return false;
		}
		factor = ClampCameraFactor(multiplier * CameraFactorDefault);
		return true;
	}

	// Takes an old observer font size, a whole number. Returns false and leaves size alone when the text is not a whole number.
	inline bool ParseOptionsIniFontSize(const char* text, int& size)
	{
		if (text == nullptr || *text == '\0')
		{
			return false;
		}
		char* end = nullptr;
		errno = 0;
		const long value = std::strtol(text, &end, 10);
		if (end == text || *end != '\0' || errno == ERANGE)
		{
			return false;
		}
		size = ClampObserverFontSize(static_cast<double>(value));
		return true;
	}

	// Takes an old yes or no value in any case. Returns false and leaves value alone for any other text.
	inline bool ParseOptionsIniYesNo(const char* text, bool& value)
	{
		if (text == nullptr)
		{
			return false;
		}
		auto equals = [text](const char* word)
		{
			const char* a = text;
			const char* b = word;
			while (*a != '\0' && *b != '\0')
			{
				if (std::tolower(static_cast<unsigned char>(*a)) != *b)
				{
					return false;
				}
				++a;
				++b;
			}
			return *a == '\0' && *b == '\0';
		};
		if (equals("yes"))
		{
			value = true;
			return true;
		}
		if (equals("no"))
		{
			value = false;
			return true;
		}
		return false;
	}
}
