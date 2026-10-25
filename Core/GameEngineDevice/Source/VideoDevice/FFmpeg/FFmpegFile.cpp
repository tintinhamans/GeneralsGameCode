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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//////// FFmpegFile.cpp ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include <limits>
#include "Common/File.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}


static void logFFmpegError(const char *operation, Int error)
{
    char message[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(error, message, sizeof(message));
    DEBUG_LOG(("FFmpeg %s failed: %s\n", operation, message));
}

FFmpegFile::FFmpegFile() {}

FFmpegFile::FFmpegFile(File *file)
{
	open(file);
}

FFmpegFile::~FFmpegFile()
{
	close();
}

Bool FFmpegFile::open(File *file)
{
	if (file != nullptr && file == m_file)
		return false;
	close();
	m_file = file;
	if (m_file == nullptr)
		return false;

	m_fmtCtx = avformat_alloc_context();
	if (m_fmtCtx == nullptr) {
		close();
		return false;
	}
	const Int bufferSize = 0x10000;
	UnsignedByte *buffer = static_cast<UnsignedByte *>(av_malloc(bufferSize));
	if (buffer == nullptr) {
		close();
		return false;
	}
	m_avioCtx = avio_alloc_context(buffer, bufferSize, 0, file, &readPacket, nullptr, &seekPacket);
	if (m_avioCtx == nullptr) {
		av_free(buffer);
		close();
		return false;
	}
	m_fmtCtx->pb = m_avioCtx;
	m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
	Int result = avformat_open_input(&m_fmtCtx, nullptr, nullptr, nullptr);
	if (result < 0) {
		logFFmpegError("open input", result);
		close();
		return false;
	}
	result = avformat_find_stream_info(m_fmtCtx, nullptr);
	if (result < 0) {
		logFFmpegError("read stream metadata", result);
		close();
		return false;
	}

	m_streams.resize(m_fmtCtx->nb_streams);
	for (unsigned int i = 0; i < m_fmtCtx->nb_streams; ++i) {
		AVStream *input = m_fmtCtx->streams[i];
		FFmpegStream &stream = m_streams[i];
		stream.codec = avcodec_find_decoder(input->codecpar->codec_id);
		if (stream.codec == nullptr) {
			logFFmpegError("find decoder", AVERROR_DECODER_NOT_FOUND);
			close();
			return false;
		}
		stream.codec_ctx = avcodec_alloc_context3(stream.codec);
		stream.frame = av_frame_alloc();
		if (stream.codec_ctx == nullptr || stream.frame == nullptr) {
			logFFmpegError("allocate decoder", AVERROR(ENOMEM));
			close();
			return false;
		}
		result = avcodec_parameters_to_context(stream.codec_ctx, input->codecpar);
		if (result >= 0)
			result = avcodec_open2(stream.codec_ctx, stream.codec, nullptr);
		if (result < 0) {
			logFFmpegError("initialize decoder", result);
			close();
			return false;
		}
		stream.stream_type = stream.codec->type;
		stream.stream_idx = i;
	}
	m_packet = av_packet_alloc();
	if (m_packet == nullptr) {
		close();
		return false;
	}
	return true;
}

Int FFmpegFile::readPacket(void *opaque, UnsignedByte *buffer, Int size)
{
	const Int count = static_cast<File *>(opaque)->read(buffer, size);
	if (count < 0)
		return AVERROR(EIO);
	if (count == 0)
		return AVERROR_EOF;
	return count;
}

Int64 FFmpegFile::seekPacket(void *opaque, Int64 offset, Int whence)
{
	File *file = static_cast<File *>(opaque);
	const Int size = file->size();
	if (size < 0)
		return AVERROR(EIO);
	if (whence == AVSEEK_SIZE)
		return size;
	whence &= ~AVSEEK_FORCE;
	Int64 origin = 0;
	if (whence == SEEK_CUR) {
		origin = file->position();
		if (origin < 0)
			return AVERROR(EIO);
	} else if (whence == SEEK_END) {
		origin = size;
	} else if (whence != SEEK_SET) {
		return AVERROR(EINVAL);
	}
	// File uses signed 32-bit offsets; validate before narrowing or adding.
	if (offset < -origin || offset > static_cast<Int64>(size) - origin)
		return AVERROR(EINVAL);
	const Int position = static_cast<Int>(origin + offset);
	if (file->seek(position, File::START) != position)
		return AVERROR(EIO);
	return position;
}

void FFmpegFile::close()
{
	avformat_close_input(&m_fmtCtx);
	for (auto &stream : m_streams) {
		avcodec_free_context(&stream.codec_ctx);
		av_frame_free(&stream.frame);
	}
	m_streams.clear();
	if (m_avioCtx != nullptr) {
		av_freep(&m_avioCtx->buffer);
		avio_context_free(&m_avioCtx);
	}
	av_packet_free(&m_packet);
	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;
	}
	m_endOfInput = false;
	m_decodeFailed = false;
}

Bool FFmpegFile::receiveFrames(FFmpegStream &stream, Bool &produced)
{
	for (;;) {
		const Int result = avcodec_receive_frame(stream.codec_ctx, stream.frame);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
			return true;
		if (result < 0) {
			logFFmpegError("receive frame", result);
			return false;
		}
		produced = true;
		if (m_frameCallback != nullptr)
			m_frameCallback(stream.frame, stream.stream_idx, stream.stream_type, m_userData);
		av_frame_unref(stream.frame);
	}
}

Bool FFmpegFile::submitPacket(FFmpegStream &stream, const AVPacket *packet, Bool &produced)
{
	Int result = avcodec_send_packet(stream.codec_ctx, packet);
	if (result == AVERROR(EAGAIN)) {
		if (!receiveFrames(stream, produced))
			return false;
		result = avcodec_send_packet(stream.codec_ctx, packet);
	}
	if (result < 0) {
		logFFmpegError("submit packet or drain", result);
		return false;
	}
	return receiveFrames(stream, produced);
}

Bool FFmpegFile::decodePacket()
{
	if (m_fmtCtx == nullptr || m_packet == nullptr || m_endOfInput || m_decodeFailed)
		return false;
	Bool produced = false;
	Int result = av_read_frame(m_fmtCtx, m_packet);
	if (result == AVERROR_EOF && m_avioCtx->error < 0 && m_avioCtx->error != AVERROR_EOF) {
		logFFmpegError("read input", m_avioCtx->error);
		m_decodeFailed = true;
		return false;
	}
	if (result == AVERROR_EOF) {
		m_endOfInput = true;
		for (auto &stream : m_streams) {
			if (!submitPacket(stream, nullptr, produced)) {
				m_decodeFailed = true;
				return false;
			}
		}
		return produced;
	}
	if (result < 0 || m_packet->stream_index < 0 ||
		static_cast<size_t>(m_packet->stream_index) >= m_streams.size()) {
		if (result >= 0)
			result = AVERROR_INVALIDDATA;
		logFFmpegError("read packet", result);
		av_packet_unref(m_packet);
		m_decodeFailed = true;
		return false;
	}
	FFmpegStream &stream = m_streams[m_packet->stream_index];
	m_decodeFailed = !submitPacket(stream, m_packet, produced);
	av_packet_unref(m_packet);
	if (m_decodeFailed)
		return false;
	return true;
}

Bool FFmpegFile::seek(Int streamIndex, Int64 timestamp)
{
	if (m_fmtCtx == nullptr)
		return false;
	const Int result = av_seek_frame(m_fmtCtx, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
	if (result < 0) {
		logFFmpegError("seek", result);
		return false;
	}
	m_avioCtx->error = 0;
	av_packet_unref(m_packet);
	for (auto &stream : m_streams) {
		avcodec_flush_buffers(stream.codec_ctx);
		av_frame_unref(stream.frame);
	}
	m_endOfInput = false;
	m_decodeFailed = false;
	return true;
}

Bool FFmpegFile::rewind()
{
	return seek(-1, 0);
}

Bool FFmpegFile::seekFrame(Int frameIndex)
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr || frameIndex < 0)
		return false;
	AVStream *video = m_fmtCtx->streams[stream->stream_idx];
	if (video->avg_frame_rate.num <= 0 || video->avg_frame_rate.den <= 0)
		return false;
	return seek(stream->stream_idx, av_rescale_q(frameIndex, av_inv_q(video->avg_frame_rate), video->time_base));
}

Int FFmpegFile::getAudioStreamIndex() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return -1;
	return stream->stream_idx;
}

Int FFmpegFile::getVideoStreamIndex() const
{
    const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
    if (stream == nullptr)
        return -1;
    return stream->stream_idx;
}

Int64 FFmpegFile::getFrameTimestampUS(const AVFrame *frame, Int stream) const
{
    if (m_fmtCtx == nullptr || frame == nullptr || stream < 0 ||
        static_cast<unsigned>(stream) >= m_fmtCtx->nb_streams || frame->best_effort_timestamp == AV_NOPTS_VALUE)
        return -1;
    Int64 origin = m_fmtCtx->start_time;
    if (origin == AV_NOPTS_VALUE)
        origin = 0;
    return av_rescale_q(frame->best_effort_timestamp, m_fmtCtx->streams[stream]->time_base, AV_TIME_BASE_Q) - origin;
}

Int64 FFmpegFile::getVideoFrameTimeUS(Int index) const
{
    const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
    if (stream == nullptr || index < 0)
        return 0;
    const AVRational rate = m_fmtCtx->streams[stream->stream_idx]->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        return 0;
    return av_rescale_q(index, av_inv_q(rate), AV_TIME_BASE_Q);
}

Bool FFmpegFile::hasAudio() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	return stream != nullptr;
}

const FFmpegFile::FFmpegStream *FFmpegFile::findMatch(Int type) const
{
	for (auto &stream : m_streams) {
		if (stream.stream_type == type)
			return &stream;
	}

	return nullptr;
}

Int FFmpegFile::getNumChannels() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->ch_layout.nb_channels;
}

Int FFmpegFile::getSampleRate() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->sample_rate;
}

Int FFmpegFile::getBytesPerSample() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return av_get_bytes_per_sample(stream->codec_ctx->sample_fmt);
}

Int FFmpegFile::getSizeForSamples(Int numSamples) const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return av_samples_get_buffer_size(nullptr, stream->codec_ctx->ch_layout.nb_channels, numSamples, stream->codec_ctx->sample_fmt, 1);
}

Int FFmpegFile::getHeight() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->height;
}

Int FFmpegFile::getWidth() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->width;
}

Int FFmpegFile::getNumFrames() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (m_fmtCtx == nullptr || stream == nullptr || m_fmtCtx->streams[stream->stream_idx] == nullptr)
		return 0;

	const AVStream *video = m_fmtCtx->streams[stream->stream_idx];
    if (video->nb_frames > 0 && video->nb_frames <= std::numeric_limits<Int>::max())
        return static_cast<Int>(video->nb_frames);
    if (m_fmtCtx->duration <= 0 || video->avg_frame_rate.num <= 0 || video->avg_frame_rate.den <= 0)
        return 0;
    const Int64 count = av_rescale_q(m_fmtCtx->duration, AV_TIME_BASE_Q, av_inv_q(video->avg_frame_rate));
    if (count <= 0 || count > std::numeric_limits<Int>::max())
        return 0;
    return static_cast<Int>(count);
}

Int FFmpegFile::getCurrentFrame() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;
	return stream->codec_ctx->frame_num;
}

Int FFmpegFile::getPixelFormat() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return AV_PIX_FMT_NONE;

	return stream->codec_ctx->pix_fmt;
}

UnsignedInt FFmpegFile::getFrameTime() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0u;
	return 1000u / av_q2d(m_fmtCtx->streams[stream->stream_idx]->avg_frame_rate);
}
