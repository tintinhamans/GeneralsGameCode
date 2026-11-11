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
#include <Lib/BaseType.h>
#include <functional>
#include <memory>
struct AVFrame;
using FFmpegPcmCallback = std::function<void(const short *, Int)>;

class FFmpegPcmConverter
{
public:
	enum class MonoUpmix { EqualPower, Duplicate };
	FFmpegPcmConverter();
	~FFmpegPcmConverter();
	bool configure(Int sampleRate, Int channels, MonoUpmix monoUpmix = MonoUpmix::EqualPower, float normalizedCutoff = 1.0f);
	void reset();
	bool convert(AVFrame *frame);
	bool drain();
	void setPcmCallback(FFmpegPcmCallback callback);
	Int getSampleRate() const;
	Int getNumChannels() const;
	bool hasError() const;
private:
	struct State;
	std::unique_ptr<State> m_state;
};
