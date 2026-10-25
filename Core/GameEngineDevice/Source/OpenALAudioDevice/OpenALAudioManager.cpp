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

#include "OpenALAudioDevice/OpenALAudioManager.h"
#include "OpenALAudioDevice/OpenALPcmStream.h"
#include "VideoDevice/FFmpeg/FFmpegAudioFile.h"
#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/AudioRequest.h"
#include "Common/AudioSettings.h"
#include "Common/FileSystem.h"
#include "Common/GameCommon.h"
#include "Common/GameSounds.h"
#include "Common/GlobalData.h"
#include "GameClient/DebugDisplay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>

namespace {
constexpr unsigned SOUND_BUFFER_FRAMES = 4096;
constexpr unsigned STREAM_BUFFER_FRAMES = 16384;
constexpr unsigned MAX_PENDING_FRAMES = 65536;
constexpr unsigned MAX_DECODE_PACKETS = 32;
constexpr size_t FORCED_PCM_BYTES = 16 * 1024 * 1024;
}

struct OpenALAudioManager::Voice
{
    RefCountPtr<DynamicAudioEventRTS> event;
    OpenALPcmStream stream;
    std::unique_ptr<FFmpegAudioFile> decoder;
    std::shared_ptr<const OpenALAudioClip> clip;
    size_t clipOffset = 0;
    std::vector<short> pending;
    size_t pendingOffset = 0;
    int channels = 2;
    int rate = 48000;
    bool removed = false;
    bool requestedStop = false;
    bool stopTailPublished = false;
    bool paused = false;
    bool forced = false;
    bool eof = false;
    bool decodeFailed = false;
    bool fading = false;
    unsigned fadeFrames = 0;
    Real delayMS = 0;
    uint64_t delayBoundary = 0;
    uint64_t submittedFrames = 0;
    uint64_t readFrames = 0;
    uint32_t previousRead = 0;
    std::deque<uint64_t> musicBoundaries;
    std::deque<std::pair<uint64_t, PortionToPlay>> portionBoundaries;
    PortionToPlay portion = PP_Done;
    unsigned completedRounds = 0;
};

OpenALAudioManager::OpenALAudioManager(Bool dummy) : m_dummy(dummy) {}

OpenALAudioManager::~OpenALAudioManager()
{
    closeDevice();
    removeAllAudioRequests();
}

void OpenALAudioManager::init()
{
    AudioManager::init();
    m_cache.setBudget(m_audioSettings->m_maxCacheSize);
    if (!m_dummy)
        openDevice();
}

void OpenALAudioManager::reset()
{
    stopAudio(AudioAffect_All);
    m_voices.clear();
    removeAllAudioRequests();
    removeLevelSpecificAudioEventInfos();
    m_cache.clear();
    m_metadata.clear();
    m_paused = 0;
    m_ambientPaused = false;
    m_lastMusic.clear();
    m_musicCompletions = 0;
    AudioManager::reset();
}

void OpenALAudioManager::update()
{
    if (!m_dummy)
        AudioManager::update();
    updatePlayback();
}

AudioHandle OpenALAudioManager::addAudioEvent(const AudioEventRTS *event)
{
    retireFinished();
    return AudioManager::addAudioEvent(event);
}

void OpenALAudioManager::retireFinished()
{
    for (auto &voice : m_voices) {
        if (voice->removed)
            continue;
        const auto state = voice->stream.state();
        if (state == OpenALPcmStream::State::Finished || state == OpenALPcmStream::State::Error)
            finishVoice(*voice);
    }
    m_voices.remove_if([](const std::unique_ptr<Voice> &voice) { return voice->removed; });
}

void OpenALAudioManager::openDevice()
{
    if (m_dummy || (TheGlobalData != nullptr && !TheGlobalData->m_audioOn))
        return;
    if (!m_audioSettings->m_useDigital) {
        setOn(false, AudioAffect_All);
        return;
    }
    if (!m_device.open(m_audioSettings->m_outputRate)) {
        std::fprintf(stderr, "OpenAL audio device initialization failed: 0x%x\n", m_device.getLastError());
        setOn(false, AudioAffect_All);
        m_provider = PROVIDER_ERROR;
        return;
    }
    m_provider = 0;
    m_speaker = m_audioSettings->m_defaultSpeakerType2D;
}

void OpenALAudioManager::closeDevice()
{
    stopAudio(AudioAffect_All);
    m_voices.clear();
    m_cache.clear();
    m_device.close();
    m_provider = PROVIDER_ERROR;
}

void *OpenALAudioManager::getDevice()
{
    if (!m_device.isOpen())
        return nullptr;
    return &m_device;
}

AudioAffect OpenALAudioManager::category(const AudioEventRTS &event) const
{
    const AudioEventInfo *info = event.getAudioEventInfo();
    if (info != nullptr && info->m_soundType == AT_Music)
        return AudioAffect_Music;
    if (info != nullptr && info->m_soundType == AT_Streaming)
        return AudioAffect_Speech;
    if (event.isPositionalAudio())
        return AudioAffect_Sound3D;
    return AudioAffect_Sound;
}

OpenALAudioManager::ResourcePool OpenALAudioManager::pool(const AudioEventRTS &event) const
{
    const AudioAffect kind = category(event);
    if (kind == AudioAffect_Music || kind == AudioAffect_Speech)
        return ResourcePool::Streams;
    if (kind == AudioAffect_Sound3D)
        return ResourcePool::Samples3D;
    return ResourcePool::Samples2D;
}

bool OpenALAudioManager::active(const Voice &voice) const
{
    if (voice.removed)
        return false;
    const auto state = voice.stream.state();
    return state != OpenALPcmStream::State::Finished && state != OpenALPcmStream::State::Error;
}

bool OpenALAudioManager::matches(const Voice &voice, AudioAffect which) const
{
    return active(voice) && (category(*voice.event) & which) != 0;
}

unsigned OpenALAudioManager::capacity(ResourcePool which) const
{
    if (m_dummy)
        return 0;
    int value = m_audioSettings->m_streamCount;
    if (which == ResourcePool::Samples2D)
        value = m_audioSettings->m_sampleCount2D;
    else if (which == ResourcePool::Samples3D)
        value = m_audioSettings->m_sampleCount3D;
    if (value < 0)
        return 0;
    return static_cast<unsigned>(value);
}

unsigned OpenALAudioManager::count(ResourcePool which) const
{
    unsigned result = 0;
    for (const auto &voice : m_voices) {
        if (active(*voice) && pool(*voice->event) == which)
            ++result;
    }
    return result;
}

UnsignedInt OpenALAudioManager::getNum2DSamples() const { return capacity(ResourcePool::Samples2D); }
UnsignedInt OpenALAudioManager::getNum3DSamples() const { return capacity(ResourcePool::Samples3D); }
UnsignedInt OpenALAudioManager::getNumStreams() const { return capacity(ResourcePool::Streams); }
UnsignedInt OpenALAudioManager::getNumAvailable2DSamples() const
{
    const unsigned used = count(ResourcePool::Samples2D);
    if (used >= getNum2DSamples())
        return 0;
    return getNum2DSamples() - used;
}
UnsignedInt OpenALAudioManager::getNumAvailable3DSamples() const
{
    const unsigned used = count(ResourcePool::Samples3D);
    if (used >= getNum3DSamples())
        return 0;
    return getNum3DSamples() - used;
}

void OpenALAudioManager::stopAudio(AudioAffect which)
{
    for (auto &voice : m_voices) {
        if (matches(*voice, which))
            finishVoice(*voice);
    }
}

void OpenALAudioManager::pauseAudio(AudioAffect which)
{
    m_paused |= which;
    for (auto &voice : m_voices) {
        if (matches(*voice, which) && !voice->paused) {
            voice->stream.pause();
            voice->paused = true;
        }
    }
    for (auto it = m_audioRequests.begin(); it != m_audioRequests.end();) {
        AudioRequest *request = *it;
        if (request->m_request == AR_Play) {
            releaseAudioRequest(request);
            it = m_audioRequests.erase(it);
        } else {
            ++it;
        }
    }
}

void OpenALAudioManager::resumeAudio(AudioAffect which)
{
    m_paused &= ~which;
    for (auto &voice : m_voices) {
        if (matches(*voice, which) && voice->paused) {
            if (m_ambientPaused && voice->event->getAudioEventInfo()->isPermanentSound())
                continue;
            voice->stream.play();
            voice->paused = false;
        }
    }
}

void OpenALAudioManager::pauseAmbient(Bool pause)
{
    m_ambientPaused = pause;
    for (auto &voice : m_voices) {
        if (!active(*voice) || !voice->event->getAudioEventInfo()->isPermanentSound())
            continue;
        if (pause && !voice->paused) {
            voice->stream.pause();
            voice->paused = true;
        } else if (!pause && voice->paused && (m_paused & category(*voice->event)) == 0) {
            voice->stream.play();
            voice->paused = false;
        }
    }
}

OpenALAudioManager::Voice *OpenALAudioManager::find(AudioHandle handle)
{
    for (auto &voice : m_voices) {
        if (active(*voice) && voice->event->getPlayingHandle() == handle)
            return voice.get();
    }
    return nullptr;
}

void OpenALAudioManager::finishVoice(Voice &voice)
{
    if (voice.removed)
        return;
    voice.removed = true;
    voice.stream.stop();
    voice.stream.close();
    voice.decoder.reset();
    voice.clip.reset();
    voice.pending.clear();
    if (voice.event->getAudioEventInfo()->m_soundType == AT_Streaming && voice.event->getUninterruptible())
        setDisallowSpeech(false);
}

void OpenALAudioManager::stopVoice(Voice &voice, bool immediate)
{
    if (immediate || voice.event->getAudioEventInfo()->m_soundType != AT_SoundEffect) {
        finishVoice(voice);
        return;
    }
    voice.requestedStop = true;
    if (voice.stopTailPublished || (voice.event->getAudioEventInfo()->m_control & AC_LOOP) == 0)
        return;
    const uint32_t read = voice.stream.getReadFrames();
    voice.readFrames += static_cast<uint32_t>(read - voice.previousRead);
    voice.previousRead = read;
    while (!voice.portionBoundaries.empty() && voice.portionBoundaries.front().first <= voice.readFrames)
        voice.portionBoundaries.pop_front();
    if ((!voice.portionBoundaries.empty() && voice.portionBoundaries.front().second == PP_Decay) ||
        (voice.portionBoundaries.empty() && voice.portion == PP_Decay))
        return;
    if (voice.portionBoundaries.empty() && !(voice.delayMS > 0 && voice.readFrames >= voice.delayBoundary))
        return;
    uint64_t end = voice.readFrames;
    if (!voice.portionBoundaries.empty())
        end = voice.portionBoundaries.front().first;
    std::shared_ptr<const OpenALAudioClip> decay;
    const AsciiString filename = voice.event->getDecayFilename();
    if (!filename.isEmpty()) {
        decay = m_cache.load(filename, voice.rate, voice.channels);
        if (decay == nullptr)
            decay = OpenALAudioCache::loadUncached(filename, voice.rate, voice.channels, FORCED_PCM_BYTES);
        if (decay == nullptr)
            std::fprintf(stderr, "OpenAL loop decay could not be decoded within its 16 MiB tail budget: %s\n", filename.str());
    }
    const short *tail = nullptr;
    unsigned frames = 0;
    if (decay != nullptr) {
        if (decay->samples.size() > FORCED_PCM_BYTES / sizeof(short)) {
            std::fprintf(stderr, "OpenAL loop decay exceeds the 16 MiB stop-tail budget; retaining ordinary loop shutdown: %s\n", filename.str());
            return;
        }
        tail = decay->samples.data();
        frames = static_cast<unsigned>(decay->samples.size() / voice.channels);
    }
    if (!voice.stream.replaceQueuedAfter(static_cast<uint32_t>(end), tail, frames)) {
        // A finite source whose callback has latched EOF is already committed to mixer prefetch.
        if (voice.stream.getLastError() != AL_NO_ERROR)
            finishVoice(voice);
        return;
    }
    voice.stopTailPublished = true;
    voice.eof = true;
    voice.delayMS = 0;
    voice.portionBoundaries.clear();
    voice.decoder.reset();
    voice.clip.reset();
    voice.pending.clear();
    voice.pendingOffset = 0;
    voice.event->setNextPlayPortion(PP_Done);
}

void OpenALAudioManager::killAudioEventImmediately(AudioHandle handle)
{
    for (auto it = m_audioRequests.begin(); it != m_audioRequests.end();) {
        AudioRequest *request = *it;
        if (request->m_pendingEvent && request->m_pendingEvent->getPlayingHandle() == handle) {
            releaseAudioRequest(request);
            it = m_audioRequests.erase(it);
        } else {
            ++it;
        }
    }
    if (Voice *voice = find(handle))
        finishVoice(*voice);
}

Bool OpenALAudioManager::isCurrentlyPlaying(AudioHandle handle)
{
    for (AudioRequest *request : m_audioRequests) {
        if (request->m_pendingEvent && request->m_pendingEvent->getPlayingHandle() == handle)
            return true;
    }
    Voice *voice = find(handle);
    if (voice == nullptr)
        return false;
    const auto state = voice->stream.state();
    return state != OpenALPcmStream::State::Finished && state != OpenALPcmStream::State::Error;
}

void OpenALAudioManager::notifyOfAudioCompletion(UnsignedInt handle, UnsignedInt)
{
    if (Voice *voice = find(handle))
        finishVoice(*voice);
}

bool OpenALAudioManager::startPortion(Voice &voice)
{
    auto &event = *voice.event;
    AsciiString filename;
    switch (event.getNextPlayPortion()) {
    case PP_Attack: filename = event.getAttackFilename(); break;
    case PP_Sound: filename = event.getFilename(); break;
    case PP_Decay: filename = event.getDecayFilename(); break;
    default: return false;
    }
    voice.clip.reset();
    voice.decoder.reset();
    voice.clipOffset = 0;
    voice.portion = event.getNextPlayPortion();
    if (filename.isEmpty())
        return false;
    if (voice.forced)
        voice.clip = OpenALAudioCache::loadUncached(filename, voice.rate, voice.channels, FORCED_PCM_BYTES);
    else if (event.getAudioEventInfo()->m_soundType == AT_SoundEffect)
        voice.clip = m_cache.load(filename, voice.rate, voice.channels);
    if (voice.clip != nullptr)
        return true;
    if (voice.forced) {
        std::fprintf(stderr, "OpenAL loading audio could not be preloaded within the 16 MiB briefing budget: %s\n", filename.str());
        return false;
    }
    voice.pending.reserve(MAX_PENDING_FRAMES * static_cast<size_t>(voice.channels));
    voice.decoder = std::make_unique<FFmpegAudioFile>();
    if (TheFileSystem == nullptr || !voice.decoder->open(TheFileSystem->openFile(filename.str()), voice.rate, voice.channels))
        return false;
    voice.decoder->setPcmCallback([&voice](const short *pcm, int frames) {
        const size_t count = static_cast<size_t>(frames) * voice.channels;
        if (voice.decodeFailed || count > MAX_PENDING_FRAMES * static_cast<size_t>(voice.channels) - voice.pending.size()) {
            voice.decodeFailed = true;
            return;
        }
        voice.pending.insert(voice.pending.end(), pcm, pcm + count);
    });
    return true;
}

bool OpenALAudioManager::nextPortion(Voice &voice)
{
    if (voice.forced)
        return false;
    auto &event = *voice.event;
    const AudioEventInfo *info = event.getAudioEventInfo();
    if (info->m_soundType == AT_Music) {
        voice.musicBoundaries.push_back(voice.submittedFrames);
        event.generateFilename();
        return startPortion(voice);
    }
    if ((info->m_control & AC_LOOP) != 0 && event.getNextPlayPortion() == PP_Attack)
        event.setNextPlayPortion(PP_Sound);
    else if ((info->m_control & AC_LOOP) != 0 && event.getNextPlayPortion() == PP_Sound) {
        event.decreaseLoopCount();
        if (!voice.requestedStop && event.hasMoreLoops()) {
            event.generateFilename();
            if (event.getDelay() > MSEC_PER_LOGICFRAME_REAL) {
                voice.delayMS = event.getDelay();
                voice.delayBoundary = voice.submittedFrames;
            }
            return startPortion(voice);
        }
        event.advanceNextPlayPortion();
    } else {
        event.advanceNextPlayPortion();
    }
    if (event.getNextPlayPortion() == PP_Done)
        return false;
    return startPortion(voice);
}

bool OpenALAudioManager::pump(Voice &voice)
{
    if (voice.eof || voice.delayMS > 0)
        return true;
    unsigned decodedPackets = 0;
    for (unsigned transfer = 0; transfer < SOUND_BUFFER_FRAMES * 2 + MAX_DECODE_PACKETS; ++transfer) {
        if (voice.pendingOffset < voice.pending.size()) {
            const unsigned available = voice.stream.availableFrames();
            if (available == 0)
                return true;
            const size_t remaining = (voice.pending.size() - voice.pendingOffset) / voice.channels;
            const unsigned frames = static_cast<unsigned>(std::min<size_t>(available, remaining));
            const auto result = voice.stream.queue(voice.pending.data() + voice.pendingOffset, frames);
            if (result == OpenALPcmStream::QueueResult::Full)
                return true;
            if (result != OpenALPcmStream::QueueResult::Accepted)
                return false;
            voice.pendingOffset += static_cast<size_t>(frames) * voice.channels;
            voice.submittedFrames += frames;
            if (voice.pendingOffset != voice.pending.size())
                continue;
            voice.pending.clear();
            voice.pendingOffset = 0;
        }
        if (voice.stream.availableFrames() == 0)
            return true;
        if (voice.clip != nullptr && voice.clipOffset < voice.clip->samples.size()) {
            const size_t available = static_cast<size_t>(voice.stream.availableFrames()) * voice.channels;
            const size_t count = std::min(available, voice.clip->samples.size() - voice.clipOffset);
            const unsigned frames = static_cast<unsigned>(count / voice.channels);
            const auto result = voice.stream.queue(voice.clip->samples.data() + voice.clipOffset, frames);
            if (result == OpenALPcmStream::QueueResult::Full)
                return true;
            if (result != OpenALPcmStream::QueueResult::Accepted)
                return false;
            voice.clipOffset += count;
            voice.submittedFrames += frames;
            if (voice.clipOffset < voice.clip->samples.size())
                continue;
            if (voice.forced) {
                voice.clip.reset();
                voice.eof = true;
                return voice.stream.markEnd();
            }
        } else if (voice.decoder != nullptr) {
            if (decodedPackets++ >= MAX_DECODE_PACKETS)
                return true;
            const bool decoded = voice.decoder->decodePacket();
            if (voice.decodeFailed || voice.decoder->hasError())
                return false;
            if (decoded || !voice.pending.empty())
                continue;
        }
        if (voice.event->getAudioEventInfo()->m_soundType == AT_SoundEffect)
            voice.portionBoundaries.emplace_back(voice.submittedFrames, voice.portion);
        if (!nextPortion(voice)) {
            voice.eof = true;
            return voice.stream.markEnd();
        }
        if (voice.delayMS > 0)
            return true;
    }
    return true;
}

void OpenALAudioManager::playRequest(AudioRequest &request, bool forced)
{
    if (!request.m_pendingEvent || m_dummy || !m_device.isOpen())
        return;
    auto &event = *request.m_pendingEvent;
    const AudioEventInfo *info = event.getAudioEventInfo();
    if (info == nullptr || !isOn(category(event)))
        return;
    retireFinished();
    std::vector<Voice *> victims;
    for (auto &previous : m_voices) {
        if (!active(*previous))
            continue;
        if ((forced && previous->forced) ||
            (info->m_soundType == AT_Streaming && event.getUninterruptible() &&
                category(*previous->event) == AudioAffect_Speech))
            victims.push_back(previous.get());
    }
    const AudioHandle replace = event.getHandleToKill();
    if (replace != 0) {
        Voice *old = find(replace);
        if (old == nullptr)
            return;
        if (std::find(victims.begin(), victims.end(), old) == victims.end())
            victims.push_back(old);
    }
    const AudioAffect kind = category(event);
    const ResourcePool resource = pool(event);
    unsigned used = count(resource);
    for (Voice *victim : victims) {
        if (pool(*victim->event) == resource && used > 0)
            --used;
    }
    if (used >= capacity(resource)) {
        Voice *replacement = nullptr;
        if (info->m_soundType == AT_Music) {
            for (auto &previous : m_voices) {
                if (matches(*previous, AudioAffect_Music) &&
                    std::find(victims.begin(), victims.end(), previous.get()) == victims.end()) {
                    replacement = previous.get();
                    break;
                }
            }
        }
        if (replacement == nullptr)
            replacement = lowestPriority(event);
        if (replacement == nullptr || std::find(victims.begin(), victims.end(), replacement) != victims.end())
            return;
        victims.push_back(replacement);
    }
    auto voice = std::make_unique<Voice>();
    voice->event = request.m_pendingEvent;
    voice->forced = forced;
    voice->requestedStop = request.m_requestStop;
    voice->rate = m_audioSettings->m_outputRate;
    if (kind == AudioAffect_Sound3D)
        voice->channels = 1;
    unsigned bufferFrames = STREAM_BUFFER_FRAMES;
    if (info->m_soundType == AT_SoundEffect)
        bufferFrames = SOUND_BUFFER_FRAMES;
    if (!startPortion(*voice))
        return;
    if (forced)
        bufferFrames = static_cast<unsigned>(voice->clip->samples.size() / voice->channels);
    if (!voice->stream.open(m_device, voice->rate, voice->channels, bufferFrames)) {
        std::fprintf(stderr, "OpenAL could not prepare a source for %s; existing voices retained\n", event.getEventName().str());
        return;
    }
    const Coord3D *position = event.getCurrentPosition();
    if (kind == AudioAffect_Sound3D && position != nullptr &&
        !voice->stream.setPosition(position->x, position->y, position->z, true))
        return;
    Real pitch = event.getPitchShift();
    if (pitch <= 0.0f)
        pitch = 1.0f;
    Real gain = effectiveVolume(event);
    if (forced)
        gain = event.getVolume() * m_speechVolume;
    if (!voice->stream.setPitch(pitch) || !voice->stream.setGain(gain) || !pump(*voice) || voice->submittedFrames == 0)
        return;
    voice->paused = (m_paused & kind) != 0;
    if (m_ambientPaused && info->isPermanentSound())
        voice->paused = true;
    if (!voice->paused && !voice->stream.play())
        return;
    for (Voice *victim : victims)
        finishVoice(*victim);
    if (info->m_soundType == AT_Music) {
        for (auto &other : m_voices) {
            if (matches(*other, AudioAffect_Music))
                other->fading = true;
        }
        m_lastMusic = event.getEventName();
        m_musicCompletions = 0;
    }
    if (info->m_soundType == AT_Streaming && event.getUninterruptible())
        setDisallowSpeech(true);
    m_voices.push_back(std::move(voice));
}

void OpenALAudioManager::processRequestList()
{
    for (auto it = m_audioRequests.begin(); it != m_audioRequests.end();) {
        AudioRequest *request = *it;
        if (request->m_pendingEvent && request->m_pendingEvent->getDelay() >= MSEC_PER_LOGICFRAME_REAL) {
            request->m_pendingEvent->decrementDelay(MSEC_PER_LOGICFRAME_REAL);
            request->m_requiresCheckForSample = true;
            ++it;
            continue;
        }
        it = m_audioRequests.erase(it);
        switch (request->m_request) {
        case AR_Play:
            if (!request->m_requiresCheckForSample || !request->m_pendingEvent ||
                request->m_pendingEvent->getAudioEventInfo()->m_soundType != AT_SoundEffect ||
                m_sound->canPlayNow(request->m_pendingEvent.Peek()))
                playRequest(*request);
            break;
        case AR_Pause:
            if (Voice *voice = find(request->m_handleToInteractOn)) {
                voice->paused = true;
                voice->stream.pause();
            }
            break;
        case AR_Stop:
            if (request->m_handleToInteractOn == AHSV_StopTheMusic || request->m_handleToInteractOn == AHSV_StopTheMusicFade) {
                for (auto &voice : m_voices) {
                    if (!matches(*voice, AudioAffect_Music))
                        continue;
                    if (request->m_handleToInteractOn == AHSV_StopTheMusicFade)
                        voice->fading = true;
                    else
                        finishVoice(*voice);
                }
            } else {
                for (auto pending = m_audioRequests.begin(); pending != m_audioRequests.end();) {
                    AudioRequest *queued = *pending;
                    if (!queued->m_pendingEvent || queued->m_pendingEvent->getPlayingHandle() != request->m_handleToInteractOn) {
                        ++pending;
                        continue;
                    }
                    if (queued->m_pendingEvent->getAudioEventInfo()->m_soundType == AT_SoundEffect) {
                        queued->m_requestStop = true;
                        ++pending;
                    } else {
                        if (pending == it)
                            ++it;
                        releaseAudioRequest(queued);
                        pending = m_audioRequests.erase(pending);
                    }
                }
                if (Voice *voice = find(request->m_handleToInteractOn))
                    stopVoice(*voice, false);
            }
            break;
        }
        releaseAudioRequest(request);
    }
}

void OpenALAudioManager::updateVoice(Voice &voice)
{
    if (voice.removed)
        return;
    const uint32_t read = voice.stream.getReadFrames();
    voice.readFrames += static_cast<uint32_t>(read - voice.previousRead);
    voice.previousRead = read;
    while (!voice.portionBoundaries.empty() && voice.portionBoundaries.front().first <= voice.readFrames)
        voice.portionBoundaries.pop_front();
    while (!voice.musicBoundaries.empty() && voice.readFrames >= voice.musicBoundaries.front()) {
        voice.musicBoundaries.pop_front();
        ++voice.completedRounds;
        if (!voice.fading && voice.event->getEventName() == m_lastMusic)
            m_musicCompletions = static_cast<Int>(voice.completedRounds);
    }
    if (voice.paused)
        return;
    if (voice.delayMS > 0 && voice.readFrames >= voice.delayBoundary)
        voice.delayMS -= MSEC_PER_LOGICFRAME_REAL;
    auto &event = *voice.event;
    if (event.isPositionalAudio()) {
        const Coord3D *position = event.getCurrentPosition();
        if (position == nullptr || event.isDead()) {
            finishVoice(voice);
            return;
        }
        if (!voice.stream.setPosition(position->x, position->y, position->z, true)) {
            finishVoice(voice);
            return;
        }
        const AudioEventInfo *info = event.getAudioEventInfo();
        if ((info->m_type & ST_GLOBAL) == 0 && info->m_priority != AP_CRITICAL) {
            Coord3D distance = m_listenerPosition;
            distance.sub(*position);
            if (distance.length() >= info->m_maxDistance) {
                finishVoice(voice);
                return;
            }
        }
    }
    Real gain = effectiveVolume(event);
    if (voice.forced)
        gain = event.getVolume() * m_speechVolume;
    if (voice.fading) {
        ++voice.fadeFrames;
        const int length = m_audioSettings->m_fadeAudioFrames;
        if (length <= 0 || voice.fadeFrames >= static_cast<unsigned>(length)) {
            finishVoice(voice);
            return;
        }
        gain *= 1.0f - static_cast<Real>(voice.fadeFrames) / length;
    }
    if (!voice.stream.setGain(gain) || !pump(voice) || voice.stream.state() == OpenALPcmStream::State::Error ||
        voice.stream.state() == OpenALPcmStream::State::Finished)
        finishVoice(voice);
}

void OpenALAudioManager::updatePlayback()
{
    if (m_dummy) {
        removeAllAudioRequests();
        return;
    }
    setDeviceListenerPosition();
    retireFinished();
    processRequestList();
    for (auto &voice : m_voices)
        updateVoice(*voice);
    m_voices.remove_if([](const std::unique_ptr<Voice> &voice) { return voice->removed; });
    m_volumeHasChanged = false;
}

Real OpenALAudioManager::effectiveVolume(AudioEventRTS &event) const
{
    Real volume = event.getVolume() * event.getVolumeShift();
    switch (category(event)) {
    case AudioAffect_Music: volume *= m_musicVolume; break;
    case AudioAffect_Speech: volume *= m_speechVolume; break;
    case AudioAffect_Sound: volume *= m_soundVolume; break;
    case AudioAffect_Sound3D: {
        volume *= m_sound3DVolume;
        const Coord3D *position = event.getCurrentPosition();
        if (position == nullptr)
            return 0;
        const AudioEventInfo *info = event.getAudioEventInfo();
        Real minimum = info->m_minDistance;
        Real maximum = info->m_maxDistance;
        if ((info->m_type & ST_GLOBAL) != 0) {
            minimum = m_audioSettings->m_globalMinRange;
            maximum = m_audioSettings->m_globalMaxRange;
        }
        Coord3D delta = m_listenerPosition;
        delta.sub(*position);
        const Real distance = delta.length();
        if (distance >= maximum)
            return 0;
        if (m_audioSettings->m_use3DSoundRangeVolumeFade && distance > minimum && maximum > minimum)
            volume *= 1.0f - std::pow((distance - minimum) / (maximum - minimum), m_audioSettings->m_3DSoundRangeVolumeFadeExponent);
        break;
    }
    default: break;
    }
    if (!std::isfinite(volume) || volume < 0)
        return 0;
    return volume;
}

void OpenALAudioManager::setDeviceListenerPosition()
{
    if (!m_device.isOpen())
        return;
    const float forward[3] = {m_listenerOrientation.x, m_listenerOrientation.y, m_listenerOrientation.z};
    const float up[3] = {0, 0, 1};
    m_device.setListener(m_listenerPosition.x, m_listenerPosition.y, m_listenerPosition.z, forward, up);
}

Bool OpenALAudioManager::doesViolateLimit(AudioEventRTS *event) const
{
    const int limit = event->getAudioEventInfo()->m_limit;
    if (limit <= 0)
        return false;
    int playing = 0;
    int requested = 0;
    event->setHandleToKill(0);
    for (const auto &voice : m_voices) {
        if (!active(*voice) || category(*voice->event) != category(*event) || voice->event->getEventName() != event->getEventName())
            continue;
        if (playing == 0)
            event->setHandleToKill(voice->event->getPlayingHandle());
        ++playing;
    }
    for (AudioRequest *request : m_audioRequests) {
        if (request->m_pendingEvent && request->m_pendingEvent->getEventName() == event->getEventName())
            ++requested;
    }
    if ((event->getAudioEventInfo()->m_control & AC_INTERRUPT) != 0) {
        if (requested >= limit)
            return true;
        if (playing + requested < limit)
            event->setHandleToKill(0);
        return false;
    }
    event->setHandleToKill(0);
    return playing + requested >= limit;
}

Bool OpenALAudioManager::isPlayingAlready(AudioEventRTS *event) const
{
    for (const auto &voice : m_voices) {
        if (active(*voice) && category(*voice->event) == category(*event) && voice->event->getEventName() == event->getEventName())
            return true;
    }
    return false;
}

Bool OpenALAudioManager::isObjectPlayingVoice(UnsignedInt object) const
{
    if (object == 0)
        return false;
    for (const auto &voice : m_voices) {
        if (active(*voice) && voice->event->getObjectID() == object && (voice->event->getAudioEventInfo()->m_type & ST_VOICE) != 0)
            return true;
    }
    return false;
}

OpenALAudioManager::Voice *OpenALAudioManager::lowestPriority(AudioEventRTS &event) const
{
    Voice *result = nullptr;
    AudioPriority lowest = event.getAudioEventInfo()->m_priority;
    for (const auto &voice : m_voices) {
        if (!active(*voice) || pool(*voice->event) != pool(event))
            continue;
        if (category(*voice->event) == AudioAffect_Speech && voice->event->getUninterruptible())
            continue;
        const AudioPriority priority = voice->event->getAudioEventInfo()->m_priority;
        if (priority < lowest) {
            lowest = priority;
            result = voice.get();
        }
    }
    return result;
}

Bool OpenALAudioManager::isPlayingLowerPriority(AudioEventRTS *event) const
{
    return lowestPriority(*event) != nullptr;
}

void OpenALAudioManager::adjustVolumeOfPlayingAudio(AsciiString event, Real volume)
{
    for (auto &voice : m_voices) {
        if (active(*voice) && voice->event->getEventName() == event) {
            voice->event->setVolume(volume);
            voice->stream.setGain(effectiveVolume(*voice->event));
        }
    }
}

void OpenALAudioManager::removePlayingAudio(AsciiString event)
{
    for (auto &voice : m_voices) {
        if (active(*voice) && voice->event->getEventName() == event)
            finishVoice(*voice);
    }
}

void OpenALAudioManager::removeAllDisabledAudio()
{
    for (const auto &override : m_adjustedVolumes) {
        if (override.second == 0.0f)
            removePlayingAudio(override.first);
    }
}

void OpenALAudioManager::closeAnySamplesUsingFile(const void *file)
{
    for (auto &voice : m_voices) {
        if (active(*voice) && voice->clip.get() == file)
            finishVoice(*voice);
    }
}

Real OpenALAudioManager::getFileLengthMS(AsciiString filename) const
{
    return m_metadata.getLengthMS(filename);
}

const OpenALAudioManager::Voice *OpenALAudioManager::findMusic(const AsciiString *track) const
{
    for (const auto &voice : m_voices) {
        if (matches(*voice, AudioAffect_Music) && !voice->fading &&
            (track == nullptr || *track == voice->event->getEventName()))
            return voice.get();
    }
    return nullptr;
}

AsciiString OpenALAudioManager::selectMusic(int direction)
{
    AsciiString current;
    if (const Voice *music = findMusic())
        current = music->event->getEventName();
    AsciiString track;
    if (direction > 0)
        track = nextTrackName(current);
    else
        track = prevTrackName(current);
    AudioEventRTS event(track);
    addAudioEvent(&event);
    return track;
}

AsciiString OpenALAudioManager::nextMusicTrack() { return selectMusic(1); }
AsciiString OpenALAudioManager::prevMusicTrack() { return selectMusic(-1); }
Bool OpenALAudioManager::isMusicPlaying() const { return findMusic() != nullptr; }
Bool OpenALAudioManager::hasMusicTrackCompleted(const AsciiString &track, Int count) const
{
    return findMusic(&track) != nullptr && m_lastMusic == track && m_musicCompletions >= count;
}

UnsignedInt OpenALAudioManager::getProviderCount() const
{
    if (m_dummy)
        return 0;
    return 1;
}
AsciiString OpenALAudioManager::getProviderName(UnsignedInt index) const
{
    if (index == 0 && !m_dummy)
        return AsciiString("OpenAL Soft");
    return AsciiString::TheEmptyString;
}
UnsignedInt OpenALAudioManager::getProviderIndex(AsciiString name) const
{
    if (!m_dummy && (name.isEmpty() || name == "OpenAL Soft"))
        return 0;
    return PROVIDER_ERROR;
}
void OpenALAudioManager::selectProvider(UnsignedInt index)
{
    if (index != 0 || m_dummy)
        return;
    if (!m_device.isOpen())
        openDevice();
    if (m_device.isOpen())
        m_provider = 0;
}
void OpenALAudioManager::unselectProvider() { closeDevice(); }
UnsignedInt OpenALAudioManager::getSelectedProvider() const { return m_provider; }
void OpenALAudioManager::setSpeakerType(UnsignedInt type)
{
    m_speaker = type;
    if (m_device.isOpen())
        std::fprintf(stderr, "OpenAL uses the operating system speaker layout (requested legacy type %u)\n", type);
}
UnsignedInt OpenALAudioManager::getSpeakerType() { return m_speaker; }
void OpenALAudioManager::setPreferredProvider(AsciiString provider) { m_preferredProvider = provider; }
void OpenALAudioManager::setPreferredSpeaker(AsciiString speaker)
{
    m_preferredSpeaker = speaker;
    setSpeakerType(translateSpeakerTypeToUnsignedInt(speaker));
}
void OpenALAudioManager::setHardwareAccelerated(Bool)
{
    AudioManager::setHardwareAccelerated(false);
}
void OpenALAudioManager::setSpeakerSurround(Bool enabled)
{
    AudioManager::setSpeakerSurround(enabled);
    UnsignedInt speaker = m_audioSettings->m_defaultSpeakerType2D;
    if (enabled)
        speaker = m_audioSettings->m_defaultSpeakerType3D;
    setSpeakerType(speaker);
}

void *OpenALAudioManager::getHandleForBink() { return nullptr; }
void OpenALAudioManager::releaseHandleForBink() {}

void OpenALAudioManager::friend_forcePlayAudioEventRTS(const AudioEventRTS *input)
{
    if (input == nullptr)
        return;
    if (input->getAudioEventInfo() == nullptr)
        getInfoForAudioEvent(input);
    if (input->getAudioEventInfo() == nullptr || !isOn(category(*input)))
        return;
    AudioRequest *request = allocateAudioRequest();
    request->m_pendingEvent.Assign_No_Add_Ref(newInstance(DynamicAudioEventRTS)(*input));
    request->m_pendingEvent->generateFilename();
    request->m_pendingEvent->generatePlayInfo();
    request->m_pendingEvent->setNextPlayPortion(PP_Sound);
    playRequest(*request, true);
    releaseAudioRequest(request);
}

#if defined(RTS_DEBUG)
void OpenALAudioManager::audioDebugDisplay(DebugDisplayInterface *display, void *, FILE *file)
{
    const unsigned sounds = count(ResourcePool::Samples2D);
    const unsigned positional = count(ResourcePool::Samples3D);
    const unsigned streams = count(ResourcePool::Streams);
    if (display != nullptr)
        display->printf("OpenAL: %u 2D, %u 3D, %u streams, %zu cached PCM bytes\n", sounds, positional, streams, m_cache.usedBytes());
    if (file != nullptr)
        std::fprintf(file, "OpenAL: %u 2D, %u 3D, %u streams, %zu cached PCM bytes\n", sounds, positional, streams, m_cache.usedBytes());
}
#endif
