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

#include "VideoDevice/FFmpeg/FFmpegPcmConverter.h"
#include <limits>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

struct FFmpegPcmConverter::State
{
    FFmpegPcmCallback callback;
    SwrContext *resampler = nullptr;
    AVChannelLayout inputLayout = {};
    AVSampleFormat inputFormat = AV_SAMPLE_FMT_NONE;
    Int inputRate = 0;
    Int outputRate = 0;
    Int channels = 0;
    Int64 samples = 0;
    Bool failed = false;
    std::vector<short> pcm;

    void resetConversion()
    {
        swr_free(&resampler);
        av_channel_layout_uninit(&inputLayout);
        inputFormat = AV_SAMPLE_FMT_NONE;
        inputRate = 0;
        samples = 0;
        failed = false;
        pcm.clear();
    }

    Bool convert(const uint8_t **input, Int count)
    {
        const Int capacity = swr_get_out_samples(resampler, count);
        if (capacity < 0 || capacity > 65536 || capacity > std::numeric_limits<Int>::max() / channels)
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

    void onFrame(AVFrame *frame)
    {
        if (failed)
            return;
        if (frame == nullptr || outputRate <= 0 || frame->sample_rate <= 0 || frame->nb_samples < 0 || frame->nb_samples > 65536 ||
            !av_channel_layout_check(&frame->ch_layout)) {
            failed = true;
            return;
        }
        const AVSampleFormat format = static_cast<AVSampleFormat>(frame->format);
        if (av_get_bytes_per_sample(format) <= 0 || (frame->nb_samples > 0 && frame->extended_data == nullptr)) {
            failed = true;
            return;
        }
        int planes = 1;
        if (av_sample_fmt_is_planar(format))
            planes = frame->ch_layout.nb_channels;
        for (int plane = 0; frame->nb_samples > 0 && plane < planes; ++plane) {
            if (frame->extended_data[plane] == nullptr) {
                failed = true;
                return;
            }
        }
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

FFmpegPcmConverter::FFmpegPcmConverter() : m_state(new State) {}
FFmpegPcmConverter::~FFmpegPcmConverter() { reset(); }
bool FFmpegPcmConverter::configure(Int rate, Int channels)
{
    reset();
    if (rate <= 0 || (channels != 1 && channels != 2))
        return false;
    m_state->outputRate = rate;
    m_state->channels = channels;
    return true;
}
void FFmpegPcmConverter::reset()
{
    m_state->resetConversion();
    m_state->outputRate = 0;
    m_state->channels = 0;
}
bool FFmpegPcmConverter::convert(AVFrame *frame)
{
    m_state->onFrame(frame);
    return !m_state->failed;
}
bool FFmpegPcmConverter::drain()
{
    if (m_state->failed)
        return false;
    m_state->failed = !m_state->drain();
    return !m_state->failed;
}
void FFmpegPcmConverter::setPcmCallback(FFmpegPcmCallback callback) { m_state->callback = std::move(callback); }
Int FFmpegPcmConverter::getSampleRate() const { return m_state->outputRate; }
Int FFmpegPcmConverter::getNumChannels() const { return m_state->channels; }
Int64 FFmpegPcmConverter::getDecodedSampleCount() const { return m_state->samples; }
bool FFmpegPcmConverter::hasError() const { return m_state->failed; }
