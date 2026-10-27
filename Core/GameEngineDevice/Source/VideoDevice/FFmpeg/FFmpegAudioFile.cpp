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
extern "C" {
#include <libavutil/avutil.h>
}
struct FFmpegAudioFile::State
{
    FFmpegFile file;
    FFmpegPcmConverter converter;
    bool finished = false;
    float normalizedCutoff = 1.0f;
};
FFmpegAudioFile::FFmpegAudioFile() : m_state(new State)
{
    m_state->file.setFrameCallback([this](AVFrame *frame, Int stream, Int type, void *) {
        if (type == AVMEDIA_TYPE_AUDIO && stream == m_state->file.getAudioStreamIndex())
            m_state->converter.convert(frame);
    });
}
FFmpegAudioFile::~FFmpegAudioFile() { close(); }
Bool FFmpegAudioFile::open(File *file, Int rate, Int channels, float normalizedCutoff)
{
    close();
    if (!m_state->file.open(file))
        return false;
    if (!m_state->file.hasAudio() || !m_state->converter.configure(rate, channels, FFmpegPcmConverter::MonoUpmix::Duplicate, normalizedCutoff)) {
        close();
        return false;
    }
    m_state->normalizedCutoff = normalizedCutoff;
    return true;
}
void FFmpegAudioFile::close()
{
    m_state->file.close();
    m_state->converter.reset();
    m_state->finished = false;
    m_state->normalizedCutoff = 1.0f;
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
Bool FFmpegAudioFile::rewind()
{
    const int rate = getSampleRate();
    const int channels = getNumChannels();
    if (!m_state->file.rewind())
        return false;
    m_state->finished = false;
    return m_state->converter.configure(rate, channels, FFmpegPcmConverter::MonoUpmix::Duplicate, m_state->normalizedCutoff);
}
Bool FFmpegAudioFile::hasError() const { return m_state->converter.hasError() || m_state->file.hasError(); }
void FFmpegAudioFile::setPcmCallback(FFmpegPcmCallback callback) { m_state->converter.setPcmCallback(std::move(callback)); }
Bool FFmpegAudioFile::setFloatPcmCallback(FFmpegFloatPcmCallback callback)
{
    return m_state->converter.setFloatPcmCallback(std::move(callback));
}
Int FFmpegAudioFile::getSampleRate() const { return m_state->converter.getSampleRate(); }
Int FFmpegAudioFile::getNumChannels() const { return m_state->converter.getNumChannels(); }
Int64 FFmpegAudioFile::getDecodedSampleCount() const { return m_state->converter.getDecodedSampleCount(); }
double FFmpegAudioFile::getDecodedDurationMS() const
{
    if (getSampleRate() == 0)
        return 0;
    return 1000.0 * getDecodedSampleCount() / getSampleRate();
}
