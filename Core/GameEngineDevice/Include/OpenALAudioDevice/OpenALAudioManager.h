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

#pragma once

#include "Common/GameAudio.h"
#include "Common/AsciiString.h"
#include "OpenALAudioDevice/OpenALDevice.h"
#include "OpenALAudioDevice/OpenALAudioCache.h"
#include "VideoDevice/FFmpeg/FFmpegAudioMetadata.h"
#include <memory>

class OpenALAudioManager : public AudioManager
{
public:
    explicit OpenALAudioManager(Bool dummy = false);
    ~OpenALAudioManager() override;
    void init() override;
    void reset() override;
    void update() override;
    AudioHandle addAudioEvent(const AudioEventRTS *event) override;
    void openDevice() override;
    void closeDevice() override;
    void *getDevice() override;
    void stopAudio(AudioAffect which) override;
    void pauseAudio(AudioAffect which) override;
    void resumeAudio(AudioAffect which) override;
    void pauseAmbient(Bool pause) override;
    void killAudioEventImmediately(AudioHandle handle) override;
    Bool isCurrentlyPlaying(AudioHandle handle) override;
    void notifyOfAudioCompletion(UnsignedInt handle, UnsignedInt flags) override;
    AsciiString nextMusicTrack() override;
    AsciiString prevMusicTrack() override;
    Bool isMusicPlaying() const override;
    Bool hasMusicTrackCompleted(const AsciiString &track, Int count) const override;
    UnsignedInt getProviderCount() const override;
    AsciiString getProviderName(UnsignedInt index) const override;
    UnsignedInt getProviderIndex(AsciiString name) const override;
    void selectProvider(UnsignedInt index) override;
    void unselectProvider() override;
    UnsignedInt getSelectedProvider() const override;
    void setSpeakerType(UnsignedInt type) override;
    UnsignedInt getSpeakerType() override;
    void setPreferredProvider(AsciiString provider) override;
    void setPreferredSpeaker(AsciiString speaker) override;
    void setHardwareAccelerated(Bool enabled) override;
    void setSpeakerSurround(Bool enabled) override;
    UnsignedInt getNum2DSamples() const override;
    UnsignedInt getNum3DSamples() const override;
    UnsignedInt getNumStreams() const override;
    UnsignedInt getNumAvailable2DSamples() const override;
    UnsignedInt getNumAvailable3DSamples() const override;
    Bool doesViolateLimit(AudioEventRTS *event) const override;
    Bool isPlayingLowerPriority(AudioEventRTS *event) const override;
    Bool isPlayingAlready(AudioEventRTS *event) const override;
    Bool isObjectPlayingVoice(UnsignedInt object) const override;
    void adjustVolumeOfPlayingAudio(AsciiString event, Real volume) override;
    void removePlayingAudio(AsciiString event) override;
    void removeAllDisabledAudio() override;
    void processRequestList() override;
    Real getFileLengthMS(AsciiString filename) const override;
    void closeAnySamplesUsingFile(const void *file) override;
    void *getHandleForBink() override;
    void releaseHandleForBink() override;
    void friend_forcePlayAudioEventRTS(const AudioEventRTS *event) override;
#if defined(RTS_DEBUG)
    void audioDebugDisplay(DebugDisplayInterface *, void *, FILE *file = nullptr) override;
#endif
protected:
    void setDeviceListenerPosition() override;
    void updatePlayback();
    OpenALDevice m_device;
private:
    struct Voice;
    enum class ResourcePool { Samples2D, Samples3D, Streams };
    ResourcePool pool(const AudioEventRTS &event) const;
    AudioAffect category(const AudioEventRTS &event) const;
    bool active(const Voice &voice) const;
    bool matches(const Voice &voice, AudioAffect which) const;
    void playRequest(AudioRequest &request, bool forced = false);
    bool startPortion(Voice &voice);
    bool nextPortion(Voice &voice);
    bool pump(Voice &voice);
    void stopVoice(Voice &voice, bool immediate);
    void finishVoice(Voice &voice);
    void updateVoice(Voice &voice);
    void retireFinished();
    Real effectiveVolume(AudioEventRTS &event) const;
    Voice *find(AudioHandle handle);
    const Voice *findMusic(const AsciiString *track = nullptr) const;
    Voice *lowestPriority(AudioEventRTS &event) const;
    unsigned count(ResourcePool which) const;
    unsigned capacity(ResourcePool which) const;
    AsciiString selectMusic(int direction);
    Bool m_dummy = false;
    Bool m_ambientPaused = false;
    UnsignedInt m_provider = PROVIDER_ERROR;
    UnsignedInt m_speaker = 0;
    AsciiString m_preferredProvider;
    AsciiString m_preferredSpeaker;
    OpenALAudioCache m_cache;
    mutable FFmpegAudioMetadata m_metadata;
    std::list<std::unique_ptr<Voice>> m_voices;
    unsigned m_paused = 0;
    AsciiString m_lastMusic;
    Int m_musicCompletions = 0;
};
