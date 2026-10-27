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
#include <cmath>
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
    FFmpegFloatPcmCallback floatCallback;
    SwrContext *resampler = nullptr;
    SwrContext *inputConverter = nullptr;
    float normalizedCutoff = 1.0f;
    struct FilterHistory
    {
        double x1 = 0;
        double x2 = 0;
        double y1 = 0;
        double y2 = 0;
    };
    std::vector<FilterHistory> history;
    std::vector<float> filtered;
    double b0 = 1;
    double a1 = 0;
    double a2 = 0;
    AVChannelLayout inputLayout = {};
    AVSampleFormat inputFormat = AV_SAMPLE_FMT_NONE;
    Int inputRate = 0;
    Int outputRate = 0;
    Int channels = 0;
    MonoUpmix monoUpmix = MonoUpmix::EqualPower;
    Int64 samples = 0;
    Bool failed = false;
    std::vector<short> pcm;
    std::vector<float> floatPcm;

    void resetConversion()
    {
        swr_free(&resampler);
        swr_free(&inputConverter);
        history.clear();
        filtered.clear();
        av_channel_layout_uninit(&inputLayout);
        inputFormat = AV_SAMPLE_FMT_NONE;
        inputRate = 0;
        samples = 0;
        failed = false;
        pcm.clear();
        floatPcm.clear();
    }

    Bool convert(const uint8_t **input, Int count)
    {
        const Int capacity = swr_get_out_samples(resampler, count);
        if (capacity < 0 || capacity > 65536 || capacity > std::numeric_limits<Int>::max() / channels)
            return false;
        uint8_t *output = nullptr;
        if (floatCallback != nullptr) {
            floatPcm.resize(static_cast<size_t>(capacity) * channels);
            output = reinterpret_cast<uint8_t *>(floatPcm.data());
        } else {
            pcm.resize(static_cast<size_t>(capacity) * channels);
            output = reinterpret_cast<uint8_t *>(pcm.data());
        }
        const Int converted = swr_convert(resampler, &output, capacity, input, count);
        if (converted < 0)
            return false;
        samples += converted;
        if (converted > 0) {
            if (floatCallback != nullptr)
                floatCallback(floatPcm.data(), converted);
            else if (callback != nullptr)
                callback(pcm.data(), converted);
        }
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

    bool filterEnabled() const
    {
        return normalizedCutoff > 0.0f && normalizedCutoff < 0.999f;
    }

    bool prepareFilter(AVFrame *frame, bool resetHistory)
    {
        swr_free(&inputConverter);
        if (!filterEnabled())
            return true;
        const int count = frame->ch_layout.nb_channels;
        if (count <= 0 || frame->nb_samples > std::numeric_limits<Int>::max() / count)
            return false;
        if (resetHistory)
            history.assign(count, FilterHistory{});
        const double c = 1.0 / std::tan(1.57079632679489661923 * normalizedCutoff);
        const double rootTwo = std::sqrt(2.0);
        b0 = 1.0 / (1.0 + rootTwo * c + c * c);
        // Miles stores feedback coefficients with 12-bit fractional precision.
        a1 = std::round(2.0 * (1.0 - c * c) * b0 * 4096.0) / 4096.0;
        a2 = std::round((1.0 - rootTwo * c + c * c) * b0 * 4096.0) / 4096.0;
        if (swr_alloc_set_opts2(&inputConverter, &frame->ch_layout, AV_SAMPLE_FMT_FLT,
                frame->sample_rate, &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
                frame->sample_rate, 0, nullptr) < 0 || inputConverter == nullptr)
            return false;
        return swr_init(inputConverter) >= 0;
    }

    bool filterFrame(AVFrame *frame)
    {
        const int count = frame->ch_layout.nb_channels;
        if (frame->nb_samples > std::numeric_limits<Int>::max() / count)
            return false;
        filtered.resize(static_cast<size_t>(frame->nb_samples) * count);
        uint8_t *output = reinterpret_cast<uint8_t *>(filtered.data());
        if (swr_convert(inputConverter, &output, frame->nb_samples,
                const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples) != frame->nb_samples)
            return false;
        for (Int sample = 0; sample < frame->nb_samples; ++sample) {
            for (Int channel = 0; channel < count; ++channel) {
                auto &h = history[channel];
                const size_t index = static_cast<size_t>(sample) * count + channel;
                const double input = filtered[index];
                const double outputSample = b0 * (input + 2.0 * h.x1 + h.x2) - a1 * h.y1 - a2 * h.y2;
                h.x2 = h.x1;
                h.x1 = input;
                h.y2 = h.y1;
                h.y1 = outputSample;
                filtered[index] = static_cast<float>(outputSample);
            }
        }
        const uint8_t *input = reinterpret_cast<const uint8_t *>(filtered.data());
        return convert(&input, frame->nb_samples);
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
            const bool resetHistory = inputRate != frame->sample_rate ||
                av_channel_layout_compare(&inputLayout, &frame->ch_layout) != 0;
            swr_free(&resampler);
            if (!prepareFilter(frame, resetHistory)) {
                failed = true;
                return;
            }
            av_channel_layout_uninit(&inputLayout);
            AVChannelLayout outputLayout = {};
            av_channel_layout_default(&outputLayout, channels);
            AVSampleFormat resamplerFormat = format;
            if (filterEnabled())
                resamplerFormat = AV_SAMPLE_FMT_FLT;
            AVSampleFormat outputFormat = AV_SAMPLE_FMT_S16;
            if (floatCallback != nullptr)
                outputFormat = AV_SAMPLE_FMT_FLT;
            Int result = swr_alloc_set_opts2(&resampler, &outputLayout, outputFormat, outputRate,
                &frame->ch_layout, resamplerFormat, frame->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&outputLayout);
            if (result >= 0 && resampler != nullptr && monoUpmix == MonoUpmix::Duplicate &&
                frame->ch_layout.nb_channels == 1 && channels == 2) {
                const double matrix[] = {1.0, 1.0};
                result = swr_set_matrix(resampler, matrix, 1);
            }
            if (result < 0 || resampler == nullptr || swr_init(resampler) < 0 ||
                av_channel_layout_copy(&inputLayout, &frame->ch_layout) < 0) {
                failed = true;
                return;
            }
            inputRate = frame->sample_rate;
            inputFormat = format;
        }
        if (frame->nb_samples == 0)
            return;
        if (filterEnabled())
            failed = !filterFrame(frame);
        else
            failed = !convert(const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    }
};

FFmpegPcmConverter::FFmpegPcmConverter() : m_state(new State) {}
FFmpegPcmConverter::~FFmpegPcmConverter() { reset(); }
bool FFmpegPcmConverter::configure(Int rate, Int channels, MonoUpmix monoUpmix, float normalizedCutoff)
{
    reset();
    if (rate <= 0 || (channels != 1 && channels != 2) || !std::isfinite(normalizedCutoff))
        return false;
    m_state->outputRate = rate;
    m_state->channels = channels;
    m_state->monoUpmix = monoUpmix;
    m_state->normalizedCutoff = normalizedCutoff;
    return true;
}
void FFmpegPcmConverter::reset()
{
    m_state->resetConversion();
    m_state->outputRate = 0;
    m_state->channels = 0;
    m_state->monoUpmix = MonoUpmix::EqualPower;
    m_state->normalizedCutoff = 1.0f;
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
void FFmpegPcmConverter::setPcmCallback(FFmpegPcmCallback callback)
{
    if (m_state->resampler != nullptr && m_state->floatCallback != nullptr) {
        m_state->failed = true;
        return;
    }
    m_state->floatCallback = nullptr;
    m_state->callback = std::move(callback);
}
bool FFmpegPcmConverter::setFloatPcmCallback(FFmpegFloatPcmCallback callback)
{
    if (m_state->resampler != nullptr)
        return false;
    m_state->callback = nullptr;
    m_state->floatCallback = std::move(callback);
    return true;
}
Int FFmpegPcmConverter::getSampleRate() const { return m_state->outputRate; }
Int FFmpegPcmConverter::getNumChannels() const { return m_state->channels; }
Int64 FFmpegPcmConverter::getDecodedSampleCount() const { return m_state->samples; }
bool FFmpegPcmConverter::hasError() const { return m_state->failed; }
