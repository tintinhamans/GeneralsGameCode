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

#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include <algorithm>
extern "C"
{
#include <libavutil/avutil.h>
}
struct FFmpegAudioFile::State
{
	FFmpegFile file;
	FFmpegPcmConverter converter;
	bool finished = false;
};
FFmpegAudioFile::FFmpegAudioFile() : m_state(new State)
{
	m_state->file.setFrameCallback([this](AVFrame *frame, Int stream, Int type, void *)
	{
		if (type == AVMEDIA_TYPE_AUDIO && stream == m_state->file.getAudioStreamIndex())
			m_state->converter.convert(frame);
	});
}
FFmpegAudioFile::~FFmpegAudioFile()
{
	close();
}
Bool FFmpegAudioFile::open(File *file, Int rate, Int channels, float normalizedCutoff)
{
	close();
	if (!m_state->file.open(file))
		return false;
	if (rate == 0)
		rate = getSourceSampleRate();
	if (channels == 0)
		channels = getSourceNumChannels();
	if (!m_state->file.hasAudio() || !m_state->converter.configure(rate, channels, FFmpegPcmConverter::MonoUpmix::Duplicate, normalizedCutoff))
	{
		close();
		return false;
	}
	return true;
}
void FFmpegAudioFile::setErrorLogging(Bool enabled)
{
	m_state->file.setErrorLogging(enabled);
}
void FFmpegAudioFile::close()
{
	m_state->file.close();
	m_state->converter.reset();
	m_state->finished = false;
}
Bool FFmpegAudioFile::decodePacket()
{
	if (m_state->finished || hasError() || getSampleRate() == 0)
		return false;
	if (m_state->file.decodePacket())
		return !hasError();
	m_state->finished = true;
	if (!m_state->file.hasError())
		m_state->converter.drain();
	return false;
}
Bool FFmpegAudioFile::hasError() const
{
	return m_state->converter.hasError() || m_state->file.hasError();
}
void FFmpegAudioFile::setPcmCallback(FFmpegPcmCallback callback)
{
	m_state->converter.setPcmCallback(std::move(callback));
}
Int FFmpegAudioFile::getSampleRate() const
{
	return m_state->converter.getSampleRate();
}
Int FFmpegAudioFile::getNumChannels() const
{
	return m_state->converter.getNumChannels();
}
Int FFmpegAudioFile::getSourceSampleRate() const
{
	return m_state->file.getSampleRate();
}
Int FFmpegAudioFile::getSourceNumChannels() const
{
	return std::min(m_state->file.getNumChannels(), 2);
}
