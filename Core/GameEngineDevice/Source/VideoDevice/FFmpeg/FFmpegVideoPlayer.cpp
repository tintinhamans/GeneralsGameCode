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

//////// FFmpegVideoPlayer.cpp ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "Lib/BaseType.h"
#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Registry.h"
#include "Common/FileSystem.h"

#include "VideoDevice/FFmpeg/FFmpegFile.h"

extern "C" {
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
}

#include "VideoDevice/FFmpeg/FFmpegPcmConverter.h"
#ifdef RTS_HAS_OPENAL_FFMPEG
#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "OpenALAudioDevice/OpenALMovieVoice.h"
#endif
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>
#include <limits>
extern "C"
{
#include <libavutil/buffer.h>
}

//----------------------------------------------------------------------------
//         Externals
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------
#define VIDEO_LANG_PATH_FORMAT "Data/%s/Movies/%s.%s"
#define VIDEO_PATH	"Data\\Movies"
#define VIDEO_EXT		"bik"



//----------------------------------------------------------------------------
//         Private Types
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Prototypes
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions
//----------------------------------------------------------------------------

#ifdef RTS_HAS_OPENAL_FFMPEG
// Bink steps the speech volume to 1 to 81 percent of 32768, and a volume of zero mutes.
static float movieAudioGain(Real volume)
{
	if (!(volume > 0))
		return 0;
	const Int percent = static_cast<Int>((volume * 0.8f) * 100) + 1;
	const Int binkVolume = (32768 * percent) / 100;
	return OpenALAudioManager::directSoundGain(binkVolume / 32768.0f);
}
#endif


//----------------------------------------------------------------------------
//         Public Functions
//----------------------------------------------------------------------------


//============================================================================
// FFmpegVideoPlayer::FFmpegVideoPlayer
//============================================================================

FFmpegVideoPlayer::FFmpegVideoPlayer()
{

}

//============================================================================
// FFmpegVideoPlayer::~FFmpegVideoPlayer
//============================================================================

FFmpegVideoPlayer::~FFmpegVideoPlayer()
{
	deinit();
}

//============================================================================
// FFmpegVideoPlayer::init
//============================================================================

void	FFmpegVideoPlayer::init()
{
	// Need to load the stuff from the ini file.
	VideoPlayer::init();

	if (TheAudio != nullptr)
		m_volume = TheAudio->getVolume(AudioAffect_Speech);
}

//============================================================================
// FFmpegVideoPlayer::deinit
//============================================================================

void FFmpegVideoPlayer::deinit()
{
	closeAllStreams();
	VideoPlayer::deinit();
}

//============================================================================
// FFmpegVideoPlayer::reset
//============================================================================

void	FFmpegVideoPlayer::reset()
{
	VideoPlayer::reset();
}

//============================================================================
// FFmpegVideoPlayer::update
//============================================================================

void	FFmpegVideoPlayer::update()
{
	VideoPlayer::update();

}

//============================================================================
// FFmpegVideoPlayer::createAudioVoice
//============================================================================

std::shared_ptr<OpenALMovieVoice> FFmpegVideoPlayer::createAudioVoice()
{
#if defined(RTS_HAS_OPENAL_FFMPEG) && !RTS_GENERALS
	if (TheAudio != nullptr)
		return static_cast<OpenALAudioManager *>(TheAudio)->createMovieVoice();
#endif
	return nullptr;
}

//============================================================================
// FFmpegVideoPlayer::createStream
//============================================================================

VideoStreamInterface* FFmpegVideoPlayer::createStream(File *file)
{
	if (file == nullptr)
		return nullptr;
	auto *decoder = NEW FFmpegFile();
	if (!decoder->open(file) || decoder->getVideoStreamIndex() < 0 || decoder->getNumFrames() <= 0)
	{
		delete decoder;
		return nullptr;
	}
	std::shared_ptr<OpenALMovieVoice> audio;
	if (decoder->hasAudio())
		audio = createAudioVoice();
	auto *stream = NEW FFmpegVideoStream(decoder, std::move(audio), m_volume, videoQueueByteLimit());
	if (!stream->valid())
	{
		delete stream;
		return nullptr;
	}
	stream->m_next = m_firstStream;
	stream->m_player = this;
	m_firstStream = stream;
	return stream;
}

void FFmpegVideoPlayer::setVolume(Real volume)
{
	if (!std::isfinite(volume) || volume < 0)
		volume = 0;
	m_volume = volume;
	for (auto *stream = m_firstStream; stream != nullptr; stream = static_cast<VideoStream *>(stream->next()))
		static_cast<FFmpegVideoStream *>(stream)->setVolume(volume);
}

//============================================================================
// FFmpegVideoPlayer::open
//============================================================================

VideoStreamInterface*	FFmpegVideoPlayer::open( AsciiString movieTitle )
{
	VideoStreamInterface*	stream = nullptr;

	const Video* pVideo = getVideo(movieTitle);
	if (pVideo) {
		DEBUG_LOG(("FFmpegVideoPlayer::createStream() - About to open bink file"));

		if (TheGlobalData->m_modDir.isNotEmpty())
		{
			char filePath[ _MAX_PATH ];
			snprintf( filePath, ARRAY_SIZE(filePath), "%s%s\\%s.%s", TheGlobalData->m_modDir.str(), VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
			File* file =  TheFileSystem->openFile(filePath);
			DEBUG_ASSERTLOG(!file, ("opened bink file %s", filePath));
			if (file)
			{
				return createStream( file );
			}
		}

		char localizedFilePath[ _MAX_PATH ];
		snprintf( localizedFilePath, ARRAY_SIZE(localizedFilePath), VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );
		File* file =  TheFileSystem->openFile(localizedFilePath);
		DEBUG_ASSERTLOG(!file, ("opened localized bink file %s", localizedFilePath));
		if (!file)
		{
			char filePath[ _MAX_PATH ];
			snprintf( filePath, ARRAY_SIZE(filePath), "%s\\%s.%s", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
			file = TheFileSystem->openFile(filePath);
			DEBUG_ASSERTLOG(!file, ("opened bink file %s", filePath));
		}

		DEBUG_LOG(("FFmpegVideoPlayer::createStream() - About to create stream"));
		stream = createStream( file );
	}

	return stream;
}

//============================================================================
// FFmpegVideoPlayer::load
//============================================================================

VideoStreamInterface*	FFmpegVideoPlayer::load( AsciiString movieTitle )
{
	return open(movieTitle); // load() used to have the same body as open(), so I'm combining them.  Munkee.
}

//============================================================================
//============================================================================
void FFmpegVideoPlayer::notifyVideoPlayerOfNewProvider(Bool)
{
}

struct FFmpegVideoStream::State
{
	using Clock = std::chrono::steady_clock;
	struct Frame
	{
		AVFrame *frame;
		Int64 timeUS;
		size_t bytes;
	};
	std::deque<Frame> frames;
	std::shared_ptr<OpenALMovieVoice> audio;
	FFmpegPcmConverter converter;
	std::vector<short> pending;
	size_t pendingOffset = 0;
	std::deque<short> history;
	uint64_t audioBase = 0;
	uint64_t voiceQueued = 0;
	bool audioLost = false;
	size_t queuedBytes = 0;
	size_t currentBytes = 0;
	size_t largestFrameBytes = 0;
	size_t queueByteLimit = 0;
	Int64 progressUS = 0;
	Int64 videoEndUS = 0;
	Int64 currentUS = 0;
	Int64 seekUS = 0;
	Int64 trimFrames = 0;
	uint64_t silenceFrames = 0;
	uint64_t submitted = 0;
	Int64 audioOrigin = 0;
	bool audioOriginSet = false;
	uint64_t audioRecoveryEpoch = 0;
	int rate = 0;
	int index = 0;
	int decodedIndex = 0;
	int totalFrames = 0;
	bool eof = false;
	bool failed = false;
	bool audioEnded = false;
	bool started = false;
	bool clockPrimed = false;
	bool paused = false;
	Real volume = 1;
	Clock::time_point start = Clock::now();
	Clock::time_point pauseStart = start;
	~State()
	{
		clearFrames();
	}
	void clearFrames()
	{
		for (auto &frame : frames)
			av_frame_free(&frame.frame);
		frames.clear();
		queuedBytes = 0;
	}
	void fail(const char *reason)
	{
		if (!failed)
			DEBUG_LOG(("FFmpeg movie playback failed: %s", reason));
		failed = true;
#ifdef RTS_HAS_OPENAL_FFMPEG
		if (audio != nullptr)
			audio->close();
#endif
	}
	void disableAudio()
	{
		if (clockPrimed)
		{
			playbackUS();
			const auto now = Clock::now();
			start = now - std::chrono::microseconds(progressUS);
			pauseStart = now;
		}
		DEBUG_LOG(("FFmpeg movie audio is unavailable, so the movie continues without audio"));
#ifdef RTS_HAS_OPENAL_FFMPEG
		if (audio != nullptr)
			audio->close();
#endif
		audio.reset();
		rate = 0;
		pending.clear();
		pendingOffset = 0;
		history.clear();
		silenceFrames = 0;
	}
	Int64 elapsedUS() const
	{
		if (!clockPrimed)
			return 0;
		auto now = pauseStart;
		if (!paused)
			now = Clock::now();
		return std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
	}
	Int64 playbackUS()
	{
		Int64 elapsed = elapsedUS();
#ifdef RTS_HAS_OPENAL_FFMPEG
		if (audio != nullptr && rate > 0)
		{
			if (audio->failed())
				return progressUS;
			// Callback progress caps starvation and is not a presentation clock.
			if (!audioLost && (!audioEnded || !audio->finished()))
				elapsed = std::min(elapsed, static_cast<Int64>(audioBase + audio->readFrames()) * 1000000 / rate);
		}
#endif
		progressUS = elapsed;
		return elapsed;
	}
#ifdef RTS_HAS_OPENAL_FFMPEG
	uint64_t historyFrames() const
	{
		return history.size() / 2;
	}
	uint64_t audioEndFrames() const
	{
		return submitted + silenceFrames + (pending.size() - pendingOffset) / 2;
	}
	uint64_t clockFrames() const
	{
		return static_cast<uint64_t>(std::max<Int64>(0, elapsedUS())) * rate / 1000000;
	}
	void trimHistory(uint64_t position)
	{
		const uint64_t first = submitted - historyFrames();
		if (position > first)
			history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(std::min(position - first, historyFrames()) * 2));
	}
	// Audio the lost device missed is skipped so it rejoins at the playback position.
	void skipAudioBefore(uint64_t position)
	{
		trimHistory(position);
		if (submitted < position && silenceFrames > 0)
		{
			const uint64_t frames = std::min(silenceFrames, position - submitted);
			silenceFrames -= frames;
			submitted += frames;
		}
		if (submitted < position && pendingOffset < pending.size())
		{
			const size_t frames = static_cast<size_t>(std::min<uint64_t>((pending.size() - pendingOffset) / 2, position - submitted));
			pendingOffset += frames * 2;
			submitted += frames;
			if (pendingOffset == pending.size())
			{
				pending.clear();
				pendingOffset = 0;
			}
		}
	}
	unsigned sinkAvailable() const
	{
		if (audioLost)
			return OpenALMovieVoice::CapacityFrames - static_cast<unsigned>(std::min<uint64_t>(historyFrames(), OpenALMovieVoice::CapacityFrames));
		return audio->availableFrames();
	}
	// History keeps queued audio the device has not read yet.
	bool sink(const short *pcm, unsigned frames)
	{
		if (!audioLost)
		{
			if (!audio->queue(pcm, frames))
				return false;
			voiceQueued += frames;
		}
		history.insert(history.end(), pcm, pcm + static_cast<size_t>(frames) * 2);
		submitted += frames;
		return true;
	}
	void rejoinAudio()
	{
		skipAudioBefore(clockFrames());
		if (!audio->reset())
		{
			disableAudio();
			return;
		}
		audioBase = submitted - historyFrames();
		voiceQueued = 0;
		started = false;
		audioEnded = false;
		if (history.empty())
			return;
		const std::vector<short> tail(history.begin(), history.end());
		if (!audio->queue(tail.data(), static_cast<uint32_t>(tail.size() / 2)))
		{
			disableAudio();
			return;
		}
		voiceQueued = tail.size() / 2;
	}
	// Video keeps playing silently while the device is lost, and audio rejoins on recovery.
	void followDevice()
	{
		const bool lost = !audio->connected();
		const uint64_t epoch = audio->recoveryEpoch();
		const bool recovered = !lost && (audioLost || epoch != audioRecoveryEpoch);
		if ((lost || recovered) && !audioLost && clockPrimed)
		{
			const auto now = Clock::now();
			start = now - std::chrono::microseconds(progressUS);
			pauseStart = now;
		}
		audioLost = lost;
		if (recovered)
		{
			audioRecoveryEpoch = epoch;
			rejoinAudio();
		}
	}
#endif
};

//============================================================================
// FFmpegVideoStream::FFmpegVideoStream
//============================================================================

FFmpegVideoStream::FFmpegVideoStream(FFmpegFile *file, std::shared_ptr<OpenALMovieVoice> audio, Real volume, size_t queueByteLimit)
	: m_state(new State), m_ffmpegFile(file)
{
	m_state->queueByteLimit = queueByteLimit;
	m_state->totalFrames = file->getNumFrames();
	m_state->audio = std::move(audio);
	m_state->volume = volume;
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr)
	{
		m_state->audioRecoveryEpoch = m_state->audio->recoveryEpoch();
		m_state->rate = m_state->audio->sampleRate();
		if (!m_state->converter.configure(m_state->rate, 2))
			m_state->disableAudio();
	}
#endif
	if (file->hasAudio() && m_state->audio == nullptr)
		m_state->disableAudio();
	m_state->pending.reserve(65536 * 2);
	m_state->converter.setPcmCallback([this](const short *pcm, Int frames)
	{
		const Int64 trim = std::min<Int64>(m_state->trimFrames, frames);
		m_state->trimFrames -= trim;
		pcm += trim * 2;
		frames -= static_cast<Int>(trim);
		const size_t count = static_cast<size_t>(frames) * 2;
		if (m_state->pendingOffset > 0)
		{
			m_state->pending.erase(m_state->pending.begin(), m_state->pending.begin() + m_state->pendingOffset);
			m_state->pendingOffset = 0;
		}
		if (count > 65536 * 2 - m_state->pending.size())
		{
			m_state->fail("audio packet exceeds pending PCM limit");
			return;
		}
		m_state->pending.insert(m_state->pending.end(), pcm, pcm + count);
	});
	m_ffmpegFile->setFrameCallback(onFrame);
	m_ffmpegFile->setUserData(this);
	pump();
	selectFrame();
	m_state->start = State::Clock::now();
	m_state->pauseStart = m_state->start;
}

FFmpegVideoStream::~FFmpegVideoStream()
{
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr)
		m_state->audio->close();
#endif
	av_frame_free(&m_frame);
	sws_freeContext(m_swsContext);
	delete m_ffmpegFile;
}

bool FFmpegVideoStream::valid() const
{
	return !m_state->failed && m_state->totalFrames > 0;
}

void FFmpegVideoStream::onFrame(AVFrame *frame, int stream, int type, void *user)
{
	auto &video = *static_cast<FFmpegVideoStream *>(user);
	auto &state = *video.m_state;
	if (state.failed)
		return;
	if (type == AVMEDIA_TYPE_VIDEO && stream == video.m_ffmpegFile->getVideoStreamIndex())
	{
		if (frame == nullptr || frame->width <= 0 || frame->height <= 0 || frame->data[0] == nullptr)
		{
			state.fail("invalid video frame");
			return;
		}
		Int64 time = video.m_ffmpegFile->getFrameTimestampUS(frame, stream);
		if (time < 0)
			time = video.m_ffmpegFile->getVideoFrameTimeUS(state.decodedIndex);
		++state.decodedIndex;
		if (time < state.seekUS)
			return;
		const size_t limit = state.queueByteLimit;
		size_t bytes = 0;
		for (const auto *buffer : frame->buf)
		{
			if (buffer != nullptr)
			{
				if (buffer->size > limit - bytes)
				{
					state.fail("video frame exceeds memory limit");
					return;
				}
				bytes += buffer->size;
			}
		}
		for (int i = 0; i < frame->nb_extended_buf; ++i)
		{
			if (frame->extended_buf[i]->size > limit - bytes)
			{
				state.fail("video frame exceeds memory limit");
				return;
			}
			bytes += frame->extended_buf[i]->size;
		}
		if (bytes == 0 || state.frames.size() >= 8 ||
			bytes > limit - state.queuedBytes - state.currentBytes)
		{
			state.fail("video packet exceeds queued frame limit");
			return;
		}
		AVFrame *copy = av_frame_clone(frame);
		if (copy == nullptr)
		{
			state.fail("could not retain video frame");
			return;
		}
		state.largestFrameBytes = std::max(state.largestFrameBytes, bytes);
		state.frames.push_back({copy, time, bytes});
		state.queuedBytes += bytes;
		int retained = static_cast<int>(state.frames.size());
		if (video.m_frame != nullptr)
			++retained;
		if (state.index <= std::numeric_limits<int>::max() - retained)
			state.totalFrames = std::max(state.totalFrames, state.index + retained);
	}
	else if (type == AVMEDIA_TYPE_AUDIO && stream == video.m_ffmpegFile->getAudioStreamIndex() && state.rate > 0)
	{
		if (!state.audioOriginSet)
		{
			Int64 time = video.m_ffmpegFile->getFrameTimestampUS(frame, stream);
			if (time < 0)
				time = state.seekUS;
			state.audioOrigin = av_rescale(time - state.seekUS, state.rate, 1000000);
			if (state.audioOrigin < 0)
				state.trimFrames = -state.audioOrigin;
			else
				state.silenceFrames = static_cast<uint64_t>(state.audioOrigin);
			state.audioOriginSet = true;
		}
		if (!state.converter.convert(frame))
			state.fail("audio conversion failed");
	}
}

void FFmpegVideoStream::selectFrame()
{
	if (m_frame != nullptr || m_state->frames.empty())
		return;
	auto frame = m_state->frames.front();
	m_state->frames.pop_front();
	m_state->queuedBytes -= frame.bytes;
	m_state->currentBytes = frame.bytes;
	m_state->currentUS = frame.timeUS;
	m_frame = frame.frame;
	if (!m_state->clockPrimed)
	{
		m_state->clockPrimed = true;
		m_state->start = State::Clock::now();
		m_state->pauseStart = m_state->start;
	}
}

void FFmpegVideoStream::pump()
{
	if (m_state->failed)
		return;
	for (unsigned packets = 0; packets < 32; ++packets)
	{
		if (m_state->failed)
			return;
#ifdef RTS_HAS_OPENAL_FFMPEG
		if (m_state->audio != nullptr)
		{
			if (m_state->audio->failed())
			{
				m_state->disableAudio();
			}
			else
			{
				if (m_state->audioLost)
					m_state->skipAudioBefore(m_state->clockFrames());
				else
					m_state->trimHistory(m_state->audioBase + m_state->audio->readFrames());
				unsigned available = m_state->sinkAvailable();
				if (m_state->silenceFrames > 0 && available > 0)
				{
					short silence[2048] = {};
					const unsigned count = static_cast<unsigned>(std::min<uint64_t>(1024,
						std::min<uint64_t>(available, m_state->silenceFrames)));
					if (!m_state->sink(silence, count))
					{
						m_state->disableAudio();
						continue;
					}
					m_state->silenceFrames -= count;
				}
				else if (m_state->pendingOffset < m_state->pending.size() && available > 0)
				{
					const unsigned count = static_cast<unsigned>(std::min<size_t>(available,
						(m_state->pending.size() - m_state->pendingOffset) / 2));
					if (!m_state->sink(m_state->pending.data() + m_state->pendingOffset, count))
					{
						m_state->disableAudio();
						continue;
					}
					m_state->pendingOffset += count * 2;
					if (m_state->pendingOffset == m_state->pending.size())
					{
						m_state->pending.clear();
						m_state->pendingOffset = 0;
					}
				}
				if (!m_state->audioLost && !m_state->paused && !m_state->started && m_state->voiceQueued > 0 &&
					(m_state->clockPrimed || m_frame != nullptr || !m_state->frames.empty()))
				{
					if (!m_state->audio->setGain(movieAudioGain(m_state->volume)) || !m_state->audio->play())
					{
						m_state->disableAudio();
						continue;
					}
					m_state->started = true;
				}
				// An audio-first packet must not block decoding the initial image.
				if ((m_state->silenceFrames > 0 || !m_state->pending.empty()) &&
					(m_state->clockPrimed || m_frame != nullptr || !m_state->frames.empty()))
				{
					if (m_state->sinkAvailable() == 0)
						return;
					continue;
				}
				if (!m_state->audioLost && m_state->eof && m_state->silenceFrames == 0 && m_state->pending.empty() &&
					!m_state->audioEnded)
				{
					if (m_state->voiceQueued == 0)
					{
						m_state->disableAudio();
						continue;
					}
					if (!m_state->audio->end())
					{
						m_state->disableAudio();
						continue;
					}
					m_state->audioEnded = true;
				}
			}
		}
#endif
		if (m_state->eof || m_state->frames.size() >= 8 ||
			m_state->largestFrameBytes > m_state->queueByteLimit - m_state->queuedBytes - m_state->currentBytes)
			return;
		if (!m_ffmpegFile->decodePacket())
		{
			if (m_ffmpegFile->hasError())
			{
				m_state->fail("demux or decode failed");
				return;
			}
			m_state->eof = true;
			m_state->totalFrames = m_state->index + static_cast<int>(m_state->frames.size());
			if (m_frame != nullptr)
				++m_state->totalFrames;
			if (m_state->rate > 0 && !m_state->converter.drain())
			{
				m_state->fail("audio drain failed");
				return;
			}
		}
	}
}

void FFmpegVideoStream::update()
{
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr)
	{
		m_state->audio->update();
		m_state->followDevice();
	}
#endif
	pump();
	selectFrame();
	m_state->playbackUS();
}

Bool FFmpegVideoStream::isFrameReady()
{
	update();
	return !m_state->failed && !m_state->paused && m_frame != nullptr &&
		m_state->seekUS + m_state->playbackUS() >= m_state->currentUS;
}

Bool FFmpegVideoStream::isPlaybackComplete()
{
	update();
	if (m_state->failed)
		return true;
	if (m_state->paused || !m_state->eof || m_frame != nullptr || !m_state->frames.empty())
		return false;
	const Int64 elapsed = m_state->playbackUS();
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr)
	{
		if (elapsed < static_cast<Int64>(m_state->audioEndFrames()) * 1000000 / m_state->rate)
			return false;
		if (!m_state->audioLost && (!m_state->audioEnded || !m_state->audio->finished()))
			return false;
	}
#endif
	return m_state->seekUS + elapsed >= m_state->videoEndUS;
}

void FFmpegVideoStream::setVolume(Real volume)
{
	m_state->volume = volume;
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr && !m_state->audio->setGain(movieAudioGain(volume)))
		m_state->disableAudio();
#endif
}

Int64 FFmpegVideoStream::audioOffsetUS()
{
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr && m_state->rate > 0 && !m_state->audioLost)
		return static_cast<Int64>(m_state->audioBase + m_state->audio->readFrames()) * 1000000 / m_state->rate -
			m_state->elapsedUS();
#endif
	return 0;
}

void FFmpegVideoStream::setPaused(Bool paused)
{
	if (m_state->paused == paused)
		return;
	const auto now = State::Clock::now();
	if (paused)
		m_state->pauseStart = now;
	else
		m_state->start += now - m_state->pauseStart;
	m_state->paused = paused;
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr && m_state->started)
	{
		if (paused)
		{
			if (!m_state->audio->pause())
				m_state->disableAudio();
		}
		else if (!m_state->audio->finished() && !m_state->audio->play())
		{
			m_state->disableAudio();
		}
	}
#endif
}


//============================================================================
// FFmpegVideoStream::frameDecompress
//============================================================================

void FFmpegVideoStream::frameDecompress()
{

}

//============================================================================
// FFmpegVideoStream::frameRender
//============================================================================

void FFmpegVideoStream::frameRender( VideoBuffer *buffer )
{
	if (buffer == nullptr) {
		return;
	}

	if (m_frame == nullptr) {
		return;
	}

	if (m_frame->data == nullptr || m_frame->data[0] == nullptr)
	{
		return;
	}

	int frame_width = width();
	int frame_height = height();

	if (frame_width <= 0 || frame_height <= 0)
	{
		DEBUG_LOG(("Invalid frame dimensions: %dx%d", frame_width, frame_height));
		return;
	}

	AVPixelFormat src_pix_fmt = static_cast<AVPixelFormat>(m_frame->format);
	if (src_pix_fmt == AV_PIX_FMT_NONE)
	{
		DEBUG_LOG(("Invalid source pixel format"));
		return;
	}

	AVPixelFormat dst_pix_fmt;

	switch (buffer->format()) {
		case VideoBuffer::TYPE_R8G8B8:
			dst_pix_fmt = AV_PIX_FMT_RGB24;
			break;
		case VideoBuffer::TYPE_X8R8G8B8:
			dst_pix_fmt = AV_PIX_FMT_BGR0;
			break;
		case VideoBuffer::TYPE_R5G6B5:
			dst_pix_fmt = AV_PIX_FMT_RGB565;
			break;
		case VideoBuffer::TYPE_X1R5G5B5:
			dst_pix_fmt = AV_PIX_FMT_RGB555;
			break;
		default:
			return;
	}

	m_swsContext = sws_getCachedContext(m_swsContext,
		frame_width,
		frame_height,
		src_pix_fmt,
		buffer->width(),
		buffer->height(),
		dst_pix_fmt,
		SWS_BICUBIC,
		nullptr,
		nullptr,
		nullptr);

	if (m_swsContext == nullptr)
	{
		DEBUG_LOG(("Failed to create SwsContext for frame conversion (%dx%d %s -> %s)",
			frame_width, frame_height, av_get_pix_fmt_name(src_pix_fmt), av_get_pix_fmt_name(dst_pix_fmt)));
		return;
	}

	uint8_t *buffer_data = static_cast<uint8_t *>(buffer->lock());
	if (buffer_data == nullptr) {
		DEBUG_LOG(("Failed to lock videobuffer"));
		return;
	}

	int dst_strides[] = { (int)buffer->pitch() };
	uint8_t *dst_data[] = { buffer_data };
	int result = sws_scale(m_swsContext, m_frame->data, m_frame->linesize, 0, frame_height, dst_data, dst_strides);
	if (result < 0)
	{
		DEBUG_LOG(("Failed to scale frame: sws_scale returned %d", result));
	}
	buffer->unlock();
}

//============================================================================
// FFmpegVideoStream::frameNext
//============================================================================

void FFmpegVideoStream::frameNext()
{
	if (m_state->failed || m_frame == nullptr)
		return;
	m_state->videoEndUS = m_state->currentUS + m_ffmpegFile->getVideoFrameTimeUS(1);
	av_frame_free(&m_frame);
	m_state->currentBytes = 0;
	++m_state->index;
	pump();
	selectFrame();
}

Int FFmpegVideoStream::frameIndex()
{
	return std::min(m_state->index, std::max(0, frameCount() - 1));
}
Int FFmpegVideoStream::frameCount()
{
	return m_state->totalFrames;
}

void FFmpegVideoStream::frameGoto(Int index)
{
	index = std::max(0, std::min(index, frameCount() - 1));
	m_state->clearFrames();
	av_frame_free(&m_frame);
	m_state->currentBytes = 0;
	m_state->index = index;
	m_state->seekUS = m_ffmpegFile->getVideoFrameTimeUS(index);
	m_state->submitted = 0;
	m_state->history.clear();
	m_state->audioBase = 0;
	m_state->voiceQueued = 0;
	m_state->audioOriginSet = false;
	m_state->audioOrigin = 0;
	m_state->trimFrames = 0;
	m_state->silenceFrames = 0;
	m_state->decodedIndex = 0;
	m_state->pending.clear();
	m_state->pendingOffset = 0;
	m_state->eof = false;
	m_state->audioEnded = false;
	m_state->started = false;
	m_state->clockPrimed = false;
	m_state->progressUS = 0;
	m_state->videoEndUS = m_state->seekUS;
	// Codec overlap and resampler history are rebuilt before trimming.
	m_state->failed = !m_ffmpegFile->rewind();
#ifdef RTS_HAS_OPENAL_FFMPEG
	if (m_state->audio != nullptr)
	{
		if (!m_state->audio->reset())
			m_state->disableAudio();
		else
			m_state->audioRecoveryEpoch = m_state->audio->recoveryEpoch();
	}
#endif
	if (m_state->rate > 0)
		m_state->converter.configure(m_state->rate, 2);
	m_state->start = State::Clock::now();
	m_state->pauseStart = m_state->start;
	pump();
	selectFrame();
}

//============================================================================
// VideoStream::height
//============================================================================

Int		FFmpegVideoStream::height()
{
	return m_ffmpegFile->getHeight();
}

//============================================================================
// VideoStream::width
//============================================================================

Int		FFmpegVideoStream::width()
{
	return m_ffmpegFile->getWidth();
}


