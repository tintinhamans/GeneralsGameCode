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

#pragma once

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <memory>
#include <chrono>
#include <cstdint>
#include <thread>

class OpenALPcmStream;

// Controls, queueing and destruction belong to the thread that opens the device.
class OpenALDevice
{
public:
	OpenALDevice() = default;
	~OpenALDevice();
	OpenALDevice(const OpenALDevice &) = delete;
	OpenALDevice &operator=(const OpenALDevice &) = delete;
	// Opens the default output and follows the system default when it changes.
	// A sampleRate of 0 mixes at the output endpoint's own rate.
	bool open(int sampleRate = 48000);
	bool close();
	bool isOpen() const;
	bool isStereoOutput() const;
	int getSampleRate() const;
	bool isConnected() const;
	// The owner thread services the device once per tick, before it updates the device's streams.
	bool update();
	uint64_t recoveryEpoch() const;
	int getLastError() const
	{
		return m_error;
	}

private:
	friend class OpenALPcmStream;
	struct Context
	{
		ALCdevice *device = nullptr;
		ALCcontext *context = nullptr;
		LPALBUFFERCALLBACKSOFT bufferCallback = nullptr;
		int sampleRate = 0;
		ALint linearResampler = -1;
		bool disconnectSupported = false;
		bool deviceConnected = true;
		bool systemEvents = false;
		unsigned availabilityGeneration = 0;
		unsigned removalGeneration = 0;
		unsigned defaultGeneration = 0;
		ALCint outputMode = 0;
		LPALCREOPENDEVICESOFT reopen = nullptr;
		std::chrono::steady_clock::time_point nextRetry;
		std::chrono::steady_clock::time_point retryDeadline;
		std::chrono::steady_clock::time_point nextCheck;
		uint64_t recoveryEpoch = 0;
		bool connected() const
		{
			return deviceConnected;
		}
		std::thread::id owner = std::this_thread::get_id();
		~Context();
		bool onOwnerThread() const
		{
			return owner == std::this_thread::get_id();
		}
	};
	bool initialize(ALCdevice *device, int sampleRate);
	bool reopenDefault();
	ALCint outputMode() const;
	std::shared_ptr<Context> m_context;
	int m_error = 0;
};
