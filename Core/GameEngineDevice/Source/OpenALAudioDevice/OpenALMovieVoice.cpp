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

#include "OpenALAudioDevice/OpenALMovieVoice.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
struct OpenALMovieVoice::State
{
    OpenALDevice *device = nullptr;
    OpenALPcmStream stream;
    int rate = 0;
    uint32_t previousRead = 0;
    uint64_t read = 0;
};
OpenALMovieVoice::OpenALMovieVoice(OpenALDevice &device, int rate) : m_state(new State)
{
    m_state->device = &device;
    m_state->rate = rate;
    if (!reset())
        close();
}
OpenALMovieVoice::~OpenALMovieVoice() { close(); }
void OpenALMovieVoice::update()
{
    if (m_state->device != nullptr) {
        m_state->device->update();
        m_state->stream.update();
    }
}
bool OpenALMovieVoice::connected() const
{
    return m_state->device != nullptr && m_state->device->isConnected();
}
uint64_t OpenALMovieVoice::recoveryEpoch() const
{
    if (m_state->device == nullptr)
        return 0;
    return m_state->device->recoveryEpoch();
}
bool OpenALMovieVoice::queue(const short *pcm, uint32_t frames)
{
    return m_state->stream.queue(pcm, frames) == OpenALPcmStream::QueueResult::Accepted;
}
uint32_t OpenALMovieVoice::availableFrames() const { return m_state->stream.availableFrames(); }
uint64_t OpenALMovieVoice::readFrames()
{
    const uint32_t current = m_state->stream.getReadFrames();
    m_state->read += static_cast<uint32_t>(current - m_state->previousRead);
    m_state->previousRead = current;
    return m_state->read;
}
int OpenALMovieVoice::sampleRate() const { return m_state->rate; }
bool OpenALMovieVoice::play() { return m_state->stream.play(); }
bool OpenALMovieVoice::pause() { return m_state->stream.pause(); }
bool OpenALMovieVoice::end() { return m_state->stream.markEnd(); }
bool OpenALMovieVoice::finished() const { return m_state->stream.state() == OpenALPcmStream::State::Finished; }
bool OpenALMovieVoice::failed() const
{
    return m_state->device == nullptr || m_state->stream.state() == OpenALPcmStream::State::Error;
}
bool OpenALMovieVoice::setGain(float gain) { return m_state->stream.setGain(gain); }
bool OpenALMovieVoice::reset()
{
    if (m_state->device == nullptr)
        return false;
    m_state->stream.close();
    m_state->previousRead = 0;
    m_state->read = 0;
    return m_state->stream.open(*m_state->device, m_state->rate, 2, 16384);
}
void OpenALMovieVoice::close()
{
    m_state->stream.close();
    m_state->device = nullptr;
}
