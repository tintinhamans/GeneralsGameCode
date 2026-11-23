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
#if defined(GENONLINE)
#include <limits>
#endif
#include "Common/File.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}


#if defined(GENONLINE)
void FFmpegFile::logError(const char *operation, Int error)
{
    if (!m_errorLogging)
        return;
    char message[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(error, message, sizeof(message));
    DEBUG_LOG(("FFmpeg %s failed: %s\n", operation, message));
}

#endif
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
#if defined(GENONLINE)
	if (file != nullptr && file == m_file)
		return false;
	close();
#else
	DEBUG_ASSERTCRASH(m_file == nullptr, ("already open"));
	DEBUG_ASSERTCRASH(file != nullptr, ("null file pointer"));
#if LOGGING_LEVEL != LOGLEVEL_NONE
	av_log_set_level(AV_LOG_INFO);
#endif

// This is required for FFmpeg older than 4.0 -> deprecated afterwards though
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
#endif

#endif
	m_file = file;
#if defined(GENONLINE)
	if (m_file == nullptr)
		return false;
#endif

	// FFmpeg setup
	m_fmtCtx = avformat_alloc_context();
#if defined(GENONLINE)
	if (m_fmtCtx == nullptr) {
#else
	if (!m_fmtCtx) {
		DEBUG_LOG(("Failed to alloc AVFormatContext"));
#endif
		close();
		return false;
	}
#if defined(GENONLINE)
	const Int bufferSize = 0x10000;
	UnsignedByte *buffer = static_cast<UnsignedByte *>(av_malloc(bufferSize));
#else

	constexpr size_t avio_ctx_buffer_size = 0x10000;
	uint8_t *buffer = static_cast<uint8_t *>(av_malloc(avio_ctx_buffer_size));
#endif
	if (buffer == nullptr) {
#if !defined(GENONLINE)
		DEBUG_LOG(("Failed to alloc AVIOContextBuffer"));
#endif
		close();
		return false;
	}
#if defined(GENONLINE)
	m_avioCtx = avio_alloc_context(buffer, bufferSize, 0, file, &readPacket, nullptr, &seekPacket);
#else

	m_avioCtx = avio_alloc_context(buffer, avio_ctx_buffer_size, 0, file, &readPacket, nullptr, nullptr);
#endif
	if (m_avioCtx == nullptr) {
#if defined(GENONLINE)
		av_free(buffer);
#else
		DEBUG_LOG(("Failed to alloc AVIOContext"));
#endif
		close();
		return false;
	}

	m_fmtCtx->pb = m_avioCtx;
	m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
#if defined(GENONLINE)
	Int result = avformat_open_input(&m_fmtCtx, nullptr, nullptr, nullptr);
#else

	int result = avformat_open_input(&m_fmtCtx, nullptr, nullptr, nullptr);
#endif
	if (result < 0) {
#if defined(GENONLINE)
		logError("open input", result);
#else
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avformat_open_input': %s", error_buffer));
#endif
		close();
		return false;
	}

	result = avformat_find_stream_info(m_fmtCtx, nullptr);
	if (result < 0) {
#if defined(GENONLINE)
		logError("read stream metadata", result);
#else
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avformat_find_stream_info': %s", error_buffer));
#endif
		close();
		return false;
	}

	m_streams.resize(m_fmtCtx->nb_streams);
#if defined(GENONLINE)
	for (unsigned int i = 0; i < m_fmtCtx->nb_streams; ++i) {
		AVStream *input = m_fmtCtx->streams[i];
		FFmpegStream &stream = m_streams[i];
		stream.codec = avcodec_find_decoder(input->codecpar->codec_id);
		if (stream.codec == nullptr) {
			// Streams that are never selected, like cover art, may lack a decoder.
			const AVMediaType type = input->codecpar->codec_type;
			if ((type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_VIDEO) &&
				(input->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0 && findMatch(type) == nullptr) {
				logError("find decoder", AVERROR_DECODER_NOT_FOUND);
				close();
				return false;
			}
			input->discard = AVDISCARD_ALL;
			continue;
#else
	for (unsigned int stream_idx = 0; stream_idx < m_fmtCtx->nb_streams; stream_idx++) {
		AVStream *av_stream = m_fmtCtx->streams[stream_idx];
		const AVCodec *input_codec = avcodec_find_decoder(av_stream->codecpar->codec_id);
		if (input_codec == nullptr) {
			DEBUG_LOG(("Codec not supported: '%s'", avcodec_get_name(av_stream->codecpar->codec_id)));
			close();
			return false;
#endif
		}
#if defined(GENONLINE)
		stream.codec_ctx = avcodec_alloc_context3(stream.codec);
		stream.frame = av_frame_alloc();
		if (stream.codec_ctx == nullptr || stream.frame == nullptr) {
			logError("allocate decoder", AVERROR(ENOMEM));
#else

		AVCodecContext *codec_ctx = avcodec_alloc_context3(input_codec);
		if (codec_ctx == nullptr) {
			DEBUG_LOG(("Could not allocate codec context"));
#endif
			close();
			return false;
		}
#if defined(GENONLINE)
		result = avcodec_parameters_to_context(stream.codec_ctx, input->codecpar);
		if (result >= 0)
			result = avcodec_open2(stream.codec_ctx, stream.codec, nullptr);
#else

		result = avcodec_parameters_to_context(codec_ctx, av_stream->codecpar);
#endif
		if (result < 0) {
#if defined(GENONLINE)
			logError("initialize decoder", result);
#else
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_parameters_to_context': %s", error_buffer));
#endif
			close();
			return false;
		}
#if defined(GENONLINE)
		stream.stream_type = stream.codec->type;
		stream.stream_idx = i;
#else

		result = avcodec_open2(codec_ctx, input_codec, nullptr);
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_open2': %s", error_buffer));
			close();
			return false;
		}

		FFmpegStream &output_stream = m_streams[stream_idx];
		output_stream.codec_ctx = codec_ctx;
		output_stream.codec = input_codec;
		output_stream.stream_type = input_codec->type;
		output_stream.stream_idx = stream_idx;
		output_stream.frame = av_frame_alloc();
#endif
	}

	m_packet = av_packet_alloc();
#if defined(GENONLINE)
	if (m_packet == nullptr) {
		close();
		return false;
	}
#else

#endif
	return true;
}

#if defined(GENONLINE)
Int FFmpegFile::readPacket(void *opaque, UnsignedByte *buffer, Int size)
#else
/**
 * Read an FFmpeg packet from file
 */
int FFmpegFile::readPacket(void *opaque, uint8_t *buf, int buf_size)
#endif
{
#if defined(GENONLINE)
	const Int count = static_cast<File *>(opaque)->read(buffer, size);
	if (count < 0)
		return AVERROR(EIO);
	if (count == 0)
#else
	File *file = static_cast<File *>(opaque);
	const int read = file->read(buf, buf_size);

	// Streaming protocol requires us to return real errors - when we read less equal 0 we're at EOF
	if (read <= 0)
#endif
		return AVERROR_EOF;
#if defined(GENONLINE)
	return count;
}
#endif

#if defined(GENONLINE)
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
	// File uses signed 32-bit offsets, so the offset is validated before narrowing or adding.
	if (offset < -origin || offset > static_cast<Int64>(size) - origin)
		return AVERROR(EINVAL);
	const Int position = static_cast<Int>(origin + offset);
	if (file->seek(position, File::START) != position)
		return AVERROR(EIO);
	return position;
#else
	return read;
#endif
}

/**
 * close all the open FFmpeg handles for an open file.
 */
void FFmpegFile::close()
{
#if defined(GENONLINE)
	avformat_close_input(&m_fmtCtx);
#else
	if (m_fmtCtx != nullptr) {
		avformat_close_input(&m_fmtCtx);
	}

#endif
	for (auto &stream : m_streams) {
#if defined(GENONLINE)
		avcodec_free_context(&stream.codec_ctx);
		av_frame_free(&stream.frame);
#else
		if (stream.codec_ctx != nullptr) {
			avcodec_free_context(&stream.codec_ctx);
			av_frame_free(&stream.frame);
		}
#endif
	}
	m_streams.clear();

	if (m_avioCtx != nullptr) {
		av_freep(&m_avioCtx->buffer);
		avio_context_free(&m_avioCtx);
	}
#if defined(GENONLINE)
	av_packet_free(&m_packet);
#else

	if (m_packet != nullptr) {
		av_packet_free(&m_packet);
	}

#endif
	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;
	}
#if defined(GENONLINE)
	m_endOfInput = false;
	m_decodeFailed = false;
#endif
}

#if defined(GENONLINE)
Bool FFmpegFile::receiveFrames(FFmpegStream &stream, Bool &produced)
#else
Bool FFmpegFile::decodePacket()
#endif
{
#if defined(GENONLINE)
	for (;;) {
		const Int result = avcodec_receive_frame(stream.codec_ctx, stream.frame);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
			return true;
		// Handle any other errors
		if (result < 0) {
			logError("receive frame", result);
			return false;
		}
		produced = true;
		if (m_frameCallback != nullptr)
			m_frameCallback(stream.frame, stream.stream_idx, stream.stream_type, m_userData);
		av_frame_unref(stream.frame);
	}
}
#else
	DEBUG_ASSERTCRASH(m_fmtCtx != nullptr, ("null format context"));
	DEBUG_ASSERTCRASH(m_packet != nullptr, ("null packet pointer"));
#endif

#if defined(GENONLINE)
Bool FFmpegFile::submitPacket(FFmpegStream &stream, const AVPacket *packet, Bool &produced)
{
	if (stream.codec_ctx == nullptr)
		return true;
	Int result = avcodec_send_packet(stream.codec_ctx, packet);
	if (result == AVERROR(EAGAIN)) {
		if (!receiveFrames(stream, produced))
			return false;
		result = avcodec_send_packet(stream.codec_ctx, packet);
	}
	if (result < 0) {
		logError("submit packet or drain", result);
#else
	int result = av_read_frame(m_fmtCtx, m_packet);
	if (result == AVERROR_EOF)
#endif
		return false;
#if defined(GENONLINE)
	}
	return receiveFrames(stream, produced);
}
#endif

#if defined(GENONLINE)
Bool FFmpegFile::decodePacket()
{
	if (m_fmtCtx == nullptr || m_packet == nullptr || m_endOfInput || m_decodeFailed)
		return false;
	Bool produced = false;
	Int result = av_read_frame(m_fmtCtx, m_packet);
	if (result == AVERROR_EOF && m_avioCtx->error < 0 && m_avioCtx->error != AVERROR_EOF) {
		logError("read input", m_avioCtx->error);
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
		logError("read packet", result);
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
#else
	const int stream_idx = m_packet->stream_index;
	DEBUG_ASSERTCRASH(m_streams.size() > stream_idx, ("stream index out of bounds"));

	auto &stream = m_streams[stream_idx];
	AVCodecContext *codec_ctx = stream.codec_ctx;
	result = avcodec_send_packet(codec_ctx, m_packet);
	// Check if we need more data
	if (result == AVERROR(EAGAIN))
		return true;
#endif

#if defined(GENONLINE)
Bool FFmpegFile::seek(Int streamIndex, Int64 timestamp)
{
	if (m_fmtCtx == nullptr)
		return false;
	const Int result = av_seek_frame(m_fmtCtx, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
#else
	// Handle any other errors
#endif
	if (result < 0) {
#if defined(GENONLINE)
		logError("seek", result);
#else
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avcodec_send_packet': %s", error_buffer));
#endif
		return false;
	}
#if defined(GENONLINE)
	m_avioCtx->error = 0;
#endif
	av_packet_unref(m_packet);
#if defined(GENONLINE)
	for (auto &stream : m_streams) {
		if (stream.codec_ctx == nullptr)
			continue;
		avcodec_flush_buffers(stream.codec_ctx);
		av_frame_unref(stream.frame);
	}
	m_endOfInput = false;
	m_decodeFailed = false;
	return true;
}
#endif

#if defined(GENONLINE)
Bool FFmpegFile::rewind()
{
	return seek(-1, 0);
}
#else
	// Get all frames in this packet
	while (result >= 0) {
		result = avcodec_receive_frame(codec_ctx, stream.frame);
#endif

#if defined(GENONLINE)
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
#else
		// Check if we need more data
		if (result == AVERROR(EAGAIN))
			return true;
#endif

#if defined(GENONLINE)
Int FFmpegFile::getAudioStreamIndex() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return -1;
	return stream->stream_idx;
}
#else
		// Handle any other errors
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_receive_frame': %s", error_buffer));
			return false;
		}
#endif

#if defined(GENONLINE)
Int FFmpegFile::getVideoStreamIndex() const
{
    const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
    if (stream == nullptr)
        return -1;
    return stream->stream_idx;
}
#else
		if (m_frameCallback != nullptr) {
			m_frameCallback(stream.frame, stream_idx, stream.stream_type, m_userData);
		}
	}
#endif

#if defined(GENONLINE)
Int64 FFmpegFile::getFrameTimestampUS(const AVFrame *frame, Int stream) const
{
    if (m_fmtCtx == nullptr || frame == nullptr || stream < 0 ||
        static_cast<unsigned>(stream) >= m_fmtCtx->nb_streams || frame->best_effort_timestamp == AV_NOPTS_VALUE)
        return -1;
    Int64 origin = m_fmtCtx->start_time;
    if (origin == AV_NOPTS_VALUE)
        origin = 0;
    return av_rescale_q(frame->best_effort_timestamp, m_fmtCtx->streams[stream]->time_base, AV_TIME_BASE_Q) - origin;
#else
	return true;
#endif
}

#if defined(GENONLINE)
Int64 FFmpegFile::getVideoFrameTimeUS(Int index) const
#else
void FFmpegFile::seekFrame(int frame_idx)
#endif
{
#if defined(GENONLINE)
    const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
    if (stream == nullptr || index < 0)
        return 0;
    const AVRational rate = m_fmtCtx->streams[stream->stream_idx]->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        return 0;
    return av_rescale_q(index, av_inv_q(rate), AV_TIME_BASE_Q);
#else
	// Note: not tested, since not used ingame
	for (const auto &stream : m_streams) {
		Int64 timestamp = av_q2d(m_fmtCtx->streams[stream.stream_idx]->time_base) * frame_idx
			* av_q2d(m_fmtCtx->streams[stream.stream_idx]->avg_frame_rate);
		int result = av_seek_frame(m_fmtCtx, stream.stream_idx, timestamp, AVSEEK_FLAG_ANY);
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'av_seek_frame': %s", error_buffer));
		}
	}
#endif
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

#if defined(GENONLINE)
	const AVStream *video = m_fmtCtx->streams[stream->stream_idx];
    if (video->nb_frames > 0 && video->nb_frames <= std::numeric_limits<Int>::max())
        return static_cast<Int>(video->nb_frames);
    if (m_fmtCtx->duration <= 0 || video->avg_frame_rate.num <= 0 || video->avg_frame_rate.den <= 0)
        return 0;
    const Int64 count = av_rescale_q(m_fmtCtx->duration, AV_TIME_BASE_Q, av_inv_q(video->avg_frame_rate));
    if (count <= 0 || count > std::numeric_limits<Int>::max())
        return 0;
    return static_cast<Int>(count);
#else
	return (m_fmtCtx->duration / (double)AV_TIME_BASE) * av_q2d(m_fmtCtx->streams[stream->stream_idx]->avg_frame_rate);
#endif
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
