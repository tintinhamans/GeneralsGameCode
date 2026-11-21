#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include "GameNetwork/GenOnline/json.hpp"

// The defaults and ranges of the settings.client.json settings of the camera and the observer overlay and of the legacy media setting.
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
}
