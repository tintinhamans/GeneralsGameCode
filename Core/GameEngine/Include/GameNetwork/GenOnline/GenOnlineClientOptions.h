#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>
#include "GameNetwork/GenOnline/json.hpp"

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

	// Reads an old CameraRotateSpeedMultiplier or CameraPitchSpeedMultiplier the way the old reader did, with atof and a clamp to 0.1 to 10,
	// and returns it as a factor where 1.0 is 50. A value that is not a number at all reads as the default factor.
	inline int ReadOptionsIniCameraMultiplier(const char* text)
	{
		float multiplier = static_cast<float>(std::atof(text != nullptr ? text : ""));
		if (std::isnan(multiplier))
		{
			return CameraFactorDefault;
		}
		if (multiplier < 0.1f)
		{
			multiplier = 0.1f;
		}
		else if (multiplier > 10.0f)
		{
			multiplier = 10.0f;
		}
		return ClampCameraFactor(static_cast<double>(multiplier) * CameraFactorDefault);
	}

	// Reads an old observer font size the way the old reader did, with atoi and a clamp to 0 to 15.
	inline int ReadOptionsIniFontSize(const char* text)
	{
		const long size = std::strtol(text != nullptr ? text : "", nullptr, 10);
		return ClampObserverFontSize(static_cast<double>(size));
	}

	// Reads an old observer notification value the way the old reader did. Yes in any case is true and any other text is false.
	inline bool ReadOptionsIniObserverFlag(const char* text)
	{
		if (text == nullptr)
		{
			return false;
		}
		const char* word = "yes";
		while (*text != '\0' && *word != '\0')
		{
			if (std::tolower(static_cast<unsigned char>(*text)) != *word)
			{
				return false;
			}
			++text;
			++word;
		}
		return *text == '\0' && *word == '\0';
	}

	// Removes the settings that still wait for their Options.ini value from a document client.json is written from.
	// The paths are a section and a key joined by a dot.
	inline void DropPendingSettings(nlohmann::json& root, const std::set<std::string>& pendingPaths)
	{
		for (const std::string& path : pendingPaths)
		{
			const std::string::size_type dot = path.find('.');
			if (dot == std::string::npos)
			{
				continue;
			}
			const std::string section = path.substr(0, dot);
			if (root.contains(section) && root[section].is_object())
			{
				root[section].erase(path.substr(dot + 1));
			}
		}
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
