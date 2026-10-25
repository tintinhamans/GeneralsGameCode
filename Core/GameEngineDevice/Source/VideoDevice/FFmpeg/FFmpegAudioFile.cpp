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
#include <limits>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

struct FFmpegAudioFile::State
{
    FFmpegFile file;
    FFmpegPcmCallback callback;
    SwrContext *resampler = nullptr;
    AVChannelLayout inputLayout = {};
    AVSampleFormat inputFormat = AV_SAMPLE_FMT_NONE;
    Int inputRate = 0;
    Int outputRate = 0;
    Int channels = 0;
    Int64 samples = 0;
    Bool failed = false;
    Bool finished = false;
    std::vector<short> pcm;

    void resetConversion()
    {
        swr_free(&resampler);
        av_channel_layout_uninit(&inputLayout);
        inputFormat = AV_SAMPLE_FMT_NONE;
        inputRate = 0;
        samples = 0;
        failed = false;
        finished = false;
        pcm.clear();
    }

    Bool convert(const uint8_t **input, Int count)
    {
        const Int capacity = swr_get_out_samples(resampler, count);
        if (capacity < 0 || capacity > std::numeric_limits<Int>::max() / channels)
            return false;
        pcm.resize(static_cast<size_t>(capacity) * channels);
        uint8_t *output = reinterpret_cast<uint8_t *>(pcm.data());
        const Int converted = swr_convert(resampler, &output, capacity, input, count);
        if (converted < 0)
            return false;
        samples += converted;
        if (converted > 0 && callback != nullptr)
            callback(pcm.data(), converted);
        return true;
    }

    Bool drain()
    {
        if (resampler == nullptr)
            return true;
        for (;;) {
            const Int64 previous = samples;
            if (!convert(nullptr, 0))
                return false;
            if (samples == previous)
                return true;
        }
    }

    void onFrame(AVFrame *frame, Int stream, Int type)
    {
        if (failed || type != AVMEDIA_TYPE_AUDIO || stream != file.getAudioStreamIndex())
            return;
        if (frame->sample_rate <= 0 || frame->nb_samples < 0 ||
            !av_channel_layout_check(&frame->ch_layout)) {
            failed = true;
            return;
        }
        const AVSampleFormat format = static_cast<AVSampleFormat>(frame->format);
        if (resampler == nullptr || inputRate != frame->sample_rate || inputFormat != format ||
            av_channel_layout_compare(&inputLayout, &frame->ch_layout) != 0) {
            if (!drain()) {
                failed = true;
                return;
            }
            swr_free(&resampler);
            av_channel_layout_uninit(&inputLayout);
            AVChannelLayout outputLayout = {};
            av_channel_layout_default(&outputLayout, channels);
            Int result = swr_alloc_set_opts2(&resampler, &outputLayout, AV_SAMPLE_FMT_S16, outputRate,
                &frame->ch_layout, format, frame->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&outputLayout);
            if (result < 0 || resampler == nullptr || swr_init(resampler) < 0 ||
                av_channel_layout_copy(&inputLayout, &frame->ch_layout) < 0) {
                failed = true;
                return;
            }
            inputRate = frame->sample_rate;
            inputFormat = format;
        }
        failed = !convert(const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    }
};

FFmpegAudioFile::FFmpegAudioFile() : m_state(new State)
{
    m_state->file.setFrameCallback([this](AVFrame *frame, Int stream, Int type, void *) {
        m_state->onFrame(frame, stream, type);
    });
}

FFmpegAudioFile::~FFmpegAudioFile()
{
    close();
}

Bool FFmpegAudioFile::open(File *file, Int sampleRate, Int channels)
{
    close();
    if (!m_state->file.open(file))
        return false;
    if (sampleRate <= 0 || (channels != 1 && channels != 2) || !m_state->file.hasAudio()) {
        close();
        return false;
    }
    m_state->outputRate = sampleRate;
    m_state->channels = channels;
    return true;
}

void FFmpegAudioFile::close()
{
    m_state->file.close();
    m_state->resetConversion();
    m_state->outputRate = 0;
    m_state->channels = 0;
}

Bool FFmpegAudioFile::decodePacket()
{
    if (m_state->finished || hasError() || m_state->outputRate == 0)
        return false;
    if (m_state->file.decodePacket())
        return !hasError();
    m_state->finished = true;
    if (!m_state->file.hasError() && !m_state->failed)
        m_state->failed = !m_state->drain();
    return false;
}

Bool FFmpegAudioFile::rewind()
{
    if (!m_state->file.rewind())
        return false;
    m_state->resetConversion();
    return true;
}

Bool FFmpegAudioFile::hasError() const
{
    return m_state->failed || m_state->file.hasError();
}

void FFmpegAudioFile::setPcmCallback(FFmpegPcmCallback callback)
{
    m_state->callback = callback;
}

Int FFmpegAudioFile::getSampleRate() const { return m_state->outputRate; }
Int FFmpegAudioFile::getNumChannels() const { return m_state->channels; }
Int64 FFmpegAudioFile::getDecodedSampleCount() const { return m_state->samples; }

double FFmpegAudioFile::getDecodedDurationMS() const
{
    if (m_state->outputRate == 0)
        return 0;
    return 1000.0 * m_state->samples / m_state->outputRate;
}
