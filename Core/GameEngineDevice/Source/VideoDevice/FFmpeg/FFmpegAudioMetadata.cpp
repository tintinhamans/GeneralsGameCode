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

#include "VideoDevice/FFmpeg/FFmpegAudioMetadata.h"
#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "Common/AsciiString.h"
#include "Common/FileSystem.h"
#include <limits>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/avutil.h>
}

Real FFmpegAudioMetadata::getLengthMS(const AsciiString &filename) const
{
    if (filename.isEmpty())
        return 0.0f;
    auto found = m_lengths.find(filename.str());
    if (found != m_lengths.end())
        return found->second;
    FFmpegFile file;
    if (TheFileSystem == nullptr || !file.open(TheFileSystem->openFile(filename.str())) || !file.hasAudio())
        return 0.0f;
    Int rate = 0;
    Int64 samples = 0;
    Bool valid = true;
    file.setFrameCallback([&](AVFrame *frame, Int stream, Int type, void *) {
        if (type != AVMEDIA_TYPE_AUDIO || stream != file.getAudioStreamIndex())
            return;
        if (frame->sample_rate <= 0 || frame->nb_samples < 0 ||
            (rate != 0 && rate != frame->sample_rate) ||
            samples > std::numeric_limits<Int64>::max() / 1000 - frame->nb_samples) {
            valid = false;
            return;
        }
        rate = frame->sample_rate;
        samples += frame->nb_samples;
    });
    while (file.decodePacket()) {}
    if (!valid || file.hasError() || rate == 0)
        return 0.0f;
    const Int64 milliseconds = samples * 1000 / rate;
    const Real result = static_cast<Real>(milliseconds);
    if (m_lengths.size() >= 4096)
        m_lengths.clear();
    m_lengths.emplace(filename.str(), result);
    return result;
}
