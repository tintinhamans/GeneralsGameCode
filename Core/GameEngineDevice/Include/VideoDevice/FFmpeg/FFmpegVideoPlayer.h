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

//////// FFmpegVideoPlayer.h ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

#pragma once

//----------------------------------------------------------------------------
//           Includes
//----------------------------------------------------------------------------

#include "GameClient/VideoPlayer.h"
#include <memory>
#include <cstddef>
class OpenALMovieVoice;

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------

class FFmpegFile;
struct AVFrame;
struct SwsContext;

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

//===============================
// FFmpegVideoStream
//===============================

class FFmpegVideoStream : public VideoStream
{
	friend class FFmpegVideoPlayer;

	protected:
		struct State;
		std::unique_ptr<State> m_state;
		AVFrame *m_frame = nullptr;
		SwsContext *m_swsContext = nullptr;
		FFmpegFile *m_ffmpegFile;
		FFmpegVideoStream(FFmpegFile *file, std::shared_ptr<OpenALMovieVoice> audio, Real volume, size_t queueByteLimit);
		virtual ~FFmpegVideoStream();
		static void onFrame(AVFrame *frame, int stream, int type, void *user);
		void pump();
		void selectFrame();
		void setVolume(Real volume);
		bool valid() const;
	public:

		virtual void update();											///< Update bink stream

		void setPaused(Bool paused) override;
		Int64 audioOffsetUS(); ///< Audio read position minus the presentation clock
		virtual Bool isPlaybackComplete() override;
		virtual Bool	isFrameReady();								///< Is the frame ready to be displayed
		virtual void	frameDecompress();						///< Render current frame in to buffer
		virtual void	frameRender( VideoBuffer *buffer ); ///< Render current frame in to buffer
		virtual void	frameNext();									///< Advance to next frame
		virtual Int		frameIndex();									///< Returns zero based index of current frame
		virtual Int		frameCount();									///< Returns the total number of frames in the stream
		virtual void	frameGoto( Int index );							///< Go to the spcified frame index
		virtual Int		height();											///< Return the height of the video
		virtual Int		width();											///< Return the width of the video


};

//===============================
// FFmpegVideoPlayer
//===============================
/**
  *	FFmpeg video playback code.
	*/
//===============================

class FFmpegVideoPlayer : public VideoPlayer
{

	protected:

		VideoStreamInterface* createStream(File* file);
		virtual std::shared_ptr<OpenALMovieVoice> createAudioVoice();
		virtual size_t videoQueueByteLimit() const
		{
			return 64 * 1024 * 1024;
		}
		Real m_volume = 1.0f;

	public:

		// subsytem requirements
		virtual void	init();														///< Initialize video playback code
		virtual void	reset();													///< Reset video playback
		virtual void	update();													///< Services all audio tasks. Should be called frequently

		virtual void	deinit();													///< Close down player


		FFmpegVideoPlayer();
		~FFmpegVideoPlayer();

		// service

		virtual VideoStreamInterface*	open( AsciiString movieTitle );	///< Open video file for playback
		virtual VideoStreamInterface*	load( AsciiString movieTitle );	///< Load video file in to memory for playback

		virtual void notifyVideoPlayerOfNewProvider( Bool nowHasValid );
		virtual void setVolume(Real volume) override;
};


//----------------------------------------------------------------------------
//           Inlining
//----------------------------------------------------------------------------
