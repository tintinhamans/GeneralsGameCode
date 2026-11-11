/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#include "OpenALAudioDevice/OpenALDevice.h"
#include <exception>
#include <atomic>
#include <mutex>
#include <cstring>
#include <vector>

namespace
{
static_assert(std::atomic<unsigned>::is_always_lock_free, "Audio notification counters must not block");

struct PlaybackEvents
{
	std::mutex mutex;
	unsigned users = 0;
	std::atomic<unsigned> added{0};
	std::atomic<unsigned> removed{0};
	std::atomic<unsigned> defaultChanged{0};
	LPALCEVENTCALLBACKSOFT setCallback = nullptr;
	LPALCEVENTCONTROLSOFT control = nullptr;
	std::vector<ALCenum> enabled;

	static void ALC_APIENTRY notify(ALCenum event, ALCenum type, ALCdevice *, ALCsizei,
		const ALCchar *, void *user) noexcept
	{
		if (type != ALC_PLAYBACK_DEVICE_SOFT)
			return;
		auto &events = *static_cast<PlaybackEvents *>(user);
		if (event == ALC_EVENT_TYPE_DEVICE_ADDED_SOFT)
			events.added.fetch_add(1, std::memory_order_relaxed);
		if (event == ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT)
			events.removed.fetch_add(1, std::memory_order_relaxed);
		if (event == ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT)
			events.defaultChanged.fetch_add(1, std::memory_order_relaxed);
	}

	bool retain()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (users != 0)
		{
			++users;
			return true;
		}
		if (!alcIsExtensionPresent(nullptr, "ALC_SOFT_system_events"))
			return false;
		auto supported = reinterpret_cast<LPALCEVENTISSUPPORTEDSOFT>(alcGetProcAddress(nullptr, "alcEventIsSupportedSOFT"));
		control = reinterpret_cast<LPALCEVENTCONTROLSOFT>(alcGetProcAddress(nullptr, "alcEventControlSOFT"));
		setCallback = reinterpret_cast<LPALCEVENTCALLBACKSOFT>(alcGetProcAddress(nullptr, "alcEventCallbackSOFT"));
		if (supported == nullptr || control == nullptr || setCallback == nullptr ||
			supported(ALC_EVENT_TYPE_DEVICE_ADDED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) != ALC_EVENT_SUPPORTED_SOFT)
		{
			alcGetError(nullptr);
			return false;
		}
		enabled = {ALC_EVENT_TYPE_DEVICE_ADDED_SOFT};
		if (supported(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) == ALC_EVENT_SUPPORTED_SOFT)
			enabled.push_back(ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT);
		if (supported(ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT, ALC_PLAYBACK_DEVICE_SOFT) == ALC_EVENT_SUPPORTED_SOFT)
			enabled.push_back(ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT);
		setCallback(notify, this);
		if (!control(static_cast<ALCsizei>(enabled.size()), enabled.data(), ALC_TRUE))
		{
			setCallback(nullptr, nullptr);
			enabled.clear();
			alcGetError(nullptr);
			return false;
		}
		users = 1;
		return true;
	}

	void release()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (--users != 0)
			return;
		// OpenAL Soft drains running callbacks before it replaces them.
		setCallback(nullptr, nullptr);
		control(static_cast<ALCsizei>(enabled.size()), enabled.data(), ALC_FALSE);
		enabled.clear();
	}
};

PlaybackEvents &playbackEvents()
{
	static PlaybackEvents events;
	return events;
}

std::vector<ALCint> outputAttributes(int sampleRate)
{
	// Basic stereo with HRTF off keeps a headphone endpoint from switching HRTF on behind the measured Miles mix.
	std::vector<ALCint> attributes = {ALC_OUTPUT_MODE_SOFT, ALC_STEREO_BASIC_SOFT, ALC_HRTF_SOFT, ALC_FALSE};
	// Without a requested rate the device mixes at the endpoint's rate and skips a whole-mix resample.
	if (sampleRate > 0)
		attributes.insert(attributes.end(), {ALC_FREQUENCY, sampleRate});
	attributes.push_back(0);
	return attributes;
}
}

OpenALDevice::Context::~Context()
{
	if (!onOwnerThread())
		std::terminate();
	if (systemEvents)
		playbackEvents().release();
	if (context != nullptr)
	{
		if (alcGetCurrentContext() == context)
			alcMakeContextCurrent(nullptr);
		alcDestroyContext(context);
	}
	if (device != nullptr)
		alcCloseDevice(device);
}

OpenALDevice::~OpenALDevice()
{
	if (!close())
		std::terminate();
}

bool OpenALDevice::close()
{
	if (m_context != nullptr && !m_context->onOwnerThread())
		return false;
	m_context.reset();
	return true;
}

bool OpenALDevice::isOpen() const
{
	return m_context != nullptr;
}

bool OpenALDevice::isConnected() const
{
	return isOpen() && m_context->connected();
}

uint64_t OpenALDevice::recoveryEpoch() const
{
	if (!isOpen())
		return 0;
	return m_context->recoveryEpoch;
}

bool OpenALDevice::update()
{
	if (!isOpen())
		return false;
	auto &context = *m_context;
	const auto now = std::chrono::steady_clock::now();
	bool available = false;
	bool removed = false;
	bool defaultChanged = false;
	if (context.systemEvents)
	{
		auto &events = playbackEvents();
		const auto addedGeneration = events.added.load(std::memory_order_relaxed);
		const auto removedGeneration = events.removed.load(std::memory_order_relaxed);
		const auto defaultGeneration = events.defaultChanged.load(std::memory_order_relaxed);
		available = addedGeneration != context.availabilityGeneration;
		removed = removedGeneration != context.removalGeneration;
		defaultChanged = defaultGeneration != context.defaultGeneration;
		context.availabilityGeneration = addedGeneration;
		context.removalGeneration = removedGeneration;
		context.defaultGeneration = defaultGeneration;
	}
	if (context.disconnectSupported && (removed || available || defaultChanged || now >= context.nextCheck))
	{
		context.nextCheck = now + std::chrono::milliseconds(100);
		ALCint connected = ALC_FALSE;
		alcGetIntegerv(context.device, ALC_CONNECTED, 1, &connected);
		if (connected != ALC_TRUE && context.deviceConnected)
		{
			context.deviceConnected = false;
			context.retryDeadline = now + std::chrono::seconds(8);
			context.nextRetry = now;
		}
	}
	if (context.connected())
	{
		// Sources and queued PCM survive a reopen, so following a new default needs no recovery.
		if (defaultChanged)
			reopenDefault();
		return true;
	}
	if (context.systemEvents && (available || defaultChanged))
	{
		if (now >= context.retryDeadline)
			context.retryDeadline = now + std::chrono::seconds(8);
		context.nextRetry = now;
	}
	if (context.systemEvents && now >= context.retryDeadline)
		return false;
	if (context.reopen == nullptr || now < context.nextRetry)
		return false;
	const bool reopened = reopenDefault();
	context.nextRetry = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
	if (!context.systemEvents)
		context.nextRetry = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	if (!reopened)
		return false;
	context.deviceConnected = true;
	context.nextRetry = {};
	++context.recoveryEpoch;
	return true;
}

bool OpenALDevice::reopenDefault()
{
	auto &context = *m_context;
	alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
	const auto attributes = outputAttributes(context.sampleRate);
	alcGetError(context.device);
	const bool reopened = context.reopen(context.device, nullptr, attributes.data()) == ALC_TRUE;
	m_error = alcGetError(context.device);
	ALCint connected = ALC_FALSE;
	alcGetIntegerv(context.device, ALC_CONNECTED, 1, &connected);
	// A successful reopen does not guarantee that its requested attributes were honored.
	if (!reopened || connected != ALC_TRUE || outputMode() != context.outputMode)
	{
		if (m_error == ALC_NO_ERROR)
			m_error = ALC_INVALID_DEVICE;
		return false;
	}
	m_error = ALC_NO_ERROR;
	return true;
}

bool OpenALDevice::open(int sampleRate)
{
	if (!close())
		return false;
	if (sampleRate < 0)
	{
		m_error = ALC_INVALID_VALUE;
		return false;
	}
	return initialize(alcOpenDevice(nullptr), sampleRate);
}

bool OpenALDevice::initialize(ALCdevice *device, int sampleRate)
{
	m_error = ALC_NO_ERROR;
	if (device == nullptr)
	{
		m_error = ALC_INVALID_DEVICE;
		return false;
	}
	auto candidate = std::make_shared<Context>();
	candidate->device = device;
	if (!alcIsExtensionPresent(device, "ALC_SOFT_output_mode") || !alcIsExtensionPresent(device, "ALC_SOFT_HRTF"))
	{
		m_error = ALC_INVALID_VALUE;
		return false;
	}
	candidate->sampleRate = sampleRate;
	candidate->disconnectSupported = alcIsExtensionPresent(device, "ALC_EXT_disconnect") == ALC_TRUE;
	if (alcIsExtensionPresent(device, "ALC_SOFT_reopen_device"))
		candidate->reopen = reinterpret_cast<LPALCREOPENDEVICESOFT>(alcGetProcAddress(device, "alcReopenDeviceSOFT"));
	const auto attributes = outputAttributes(sampleRate);
	candidate->context = alcCreateContext(device, attributes.data());
	if (candidate->context == nullptr)
	{
		m_error = alcGetError(device);
		return false;
	}
	if (alcMakeContextCurrent(candidate->context) != ALC_TRUE || !alIsExtensionPresent("AL_SOFT_callback_buffer"))
	{
		m_error = ALC_INVALID_CONTEXT;
		return false;
	}
	candidate->bufferCallback = reinterpret_cast<LPALBUFFERCALLBACKSOFT>(alGetProcAddress("alBufferCallbackSOFT"));
	if (candidate->bufferCallback == nullptr)
	{
		m_error = ALC_INVALID_CONTEXT;
		return false;
	}
	if (alIsExtensionPresent("AL_SOFT_source_resampler"))
	{
		auto resamplerName = reinterpret_cast<LPALGETSTRINGISOFT>(alGetProcAddress("alGetStringiSOFT"));
		const ALint resamplers = alGetInteger(AL_NUM_RESAMPLERS_SOFT);
		for (ALint i = 0; resamplerName != nullptr && i < resamplers; ++i)
		{
			const ALchar *name = resamplerName(AL_RESAMPLER_NAME_SOFT, i);
			if (name != nullptr && std::strcmp(name, "Linear") == 0)
				candidate->linearResampler = i;
		}
		alGetError();
	}
	m_context = candidate;
	m_context->outputMode = outputMode();
	if (candidate->disconnectSupported && candidate->reopen != nullptr)
	{
		candidate->systemEvents = playbackEvents().retain();
		candidate->availabilityGeneration = playbackEvents().added.load(std::memory_order_relaxed);
		candidate->removalGeneration = playbackEvents().removed.load(std::memory_order_relaxed);
		candidate->defaultGeneration = playbackEvents().defaultChanged.load(std::memory_order_relaxed);
	}
	alDopplerFactor(0.0f);
	return true;
}

int OpenALDevice::getSampleRate() const
{
	if (!isOpen())
		return 0;
	ALCint value = 0;
	alcGetIntegerv(m_context->device, ALC_FREQUENCY, 1, &value);
	return value;
}

ALCint OpenALDevice::outputMode() const
{
	ALCint value = ALC_ANY_SOFT;
	if (isOpen())
		alcGetIntegerv(m_context->device, ALC_OUTPUT_MODE_SOFT, 1, &value);
	return value;
}

bool OpenALDevice::isStereoOutput() const
{
	return outputMode() == ALC_STEREO_BASIC_SOFT;
}
