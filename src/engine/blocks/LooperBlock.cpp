// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/blocks/LooperBlock.hpp"

#include "engine/dsp/Smoother.hpp"
#include "engine/TapTempo.hpp"
#include "engine/metronome/Metronome.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xythos {

namespace {

constexpr uint32_t kFadeSamples = 256; // the loop's end crossfades this long into what came just before its start
constexpr float kClickGain = 0.8f;      // the clicks at Click 100%, as the metronome's default volume (up to 200%)
constexpr double kMaxLatencyMs = 200.0;

const char* const kModeChoices[] = { "Count In", "Free" };
const char* const kBeatChoices[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
const char* const kCountInChoices[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8",
                                        "9", "10", "11", "12", "13", "14", "15", "16" };
const char* const kMeasureChoices[] = { "1", "2", "3", "4", "5", "6", "7", "8",
                                        "9", "10", "11", "12", "13", "14", "15", "16" };
const char* const kLoopChoices[] = { "1", "2", "3", "4", "5", "6", "7", "8" };

class LooperBlock : public Block {
public:
    LooperBlock()
    {
        fLevel.snap(1.0f);
        fFeedback.snap(1.0f);
    }

    void prepare(double sampleRate, uint32_t /*maxFrames*/) override
    {
        fSampleRate = sampleRate;
        // the loop holds a fixed number of samples (no reallocation if prepared again at another rate)
        for (std::vector<float>& buffer : fBuffer)
            if (buffer.size() != kLooperMaxSamples)
                buffer.assign(kLooperMaxSamples, 0.0f);
        fLevel.prepare(sampleRate);
        fFeedback.prepare(sampleRate);
        fFade.prepare(sampleRate, 0.005);
        fLatency = latencySamples(fLatencyMs);
        fAccentClick = classicMetronomeClick(true, sampleRate);
        fBeatClick = classicMetronomeClick(false, sampleRate);
        fClickData = nullptr;
        clear();
    }

    bool configure(const nlohmann::json& config) override
    {
        fHasCountIn = config.is_object() && config.value(kLooperCountInKey, false);
        return true;
    }

    void setParam(int index, float value) override
    {
        const bool on = value >= 0.5f;
        switch (index)
        {
        case kLooperParamRecDub:
            if (on && !fRecDubHeld)
                recDub();
            fRecDubHeld = on;
            break;
        case kLooperParamPlayStop:
            if (on && !fPlayStopHeld)
                playStop();
            fPlayStopHeld = on;
            break;
        case kLooperParamClear:
            if (on && !fClearHeld)
                clear();
            fClearHeld = on;
            break;
        case kLooperParamLevel:    fLevel.setTarget(value / 100.0f); break;
        case kLooperParamFeedback: fFeedback.setTarget(value / 100.0f); break;
        case kLooperParamReverse:  fReverse = on; break;
        case kLooperParamHalf:     fHalf = on; break;
        // the count-in settings are taken when a count-in starts
        case kLooperParamMode:     fCountInMode = value < 0.5f; break;
        case kLooperParamBpm:      fBpm = std::clamp(std::round(static_cast<double>(value)), 40.0, 240.0); break;
        case kLooperParamBeats:    fBeats = std::clamp(static_cast<int>(std::lround(value)), 1, 12); break;
        case kLooperParamCountIn:  fCountIn = std::clamp(static_cast<int>(std::lround(value)), 0, 16); break;
        case kLooperParamMeasures: fMeasures = std::clamp(static_cast<int>(std::lround(value)), 1, 16); break;
        case kLooperParamLoops:    fLoops = std::clamp(static_cast<int>(std::lround(value)), 1, 8); break;
        case kLooperParamClick:    fClickLevel = std::clamp(value / 100.0f, 0.0f, 2.0f); break;
        case kLooperParamLatency:
            fLatencyMs = std::clamp(static_cast<double>(value), 0.0, kMaxLatencyMs);
            fLatency = latencySamples(fLatencyMs);
            break;
        case kLooperParamTap:
            if (on && !fTapHeld)
            {
                // the next count-in's tempo; the UI sets the BPM control to the same (it times the taps too)
                const double bpm = fTapTempo.tap(static_cast<double>(fSamples) / fSampleRate);
                if (bpm > 0.0)
                    fBpm = std::clamp(std::round(bpm), 40.0, 240.0);
            }
            fTapHeld = on;
            break;
        }
    }

    void setClickOutput(float* click) override { fClickOut = click; }

    void process(float* const* io, uint32_t numChannels, uint32_t frames) override
    {
        fSamples += frames;
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (fTimed)
                tick();
            processSample(io, numChannels, i);
            // the clicks go out, never into the loop: to the engine's click bus, past the blocks after this one (an
            // amp after the looper would distort them), or without an engine into the block's output
            if (fClickData != nullptr)
            {
                const float sample = (*fClickData)[fClickPos] * fClickGain;
                if (++fClickPos >= fClickData->size())
                    fClickData = nullptr;
                if (fClickOut != nullptr)
                    fClickOut[i] += sample;
                else
                    for (uint32_t ch = 0; ch < numChannels; ++ch)
                        io[ch][i] += sample;
            }
        }

        LooperStatus status;
        status.state = fState;
        status.lengthTenths = static_cast<int>(fLength / fSampleRate * 10.0);
        if (fState == LooperState::CountIn)
            status.progress = std::clamp(static_cast<int>(fClock / fBeatLength) + 1, 1, std::max(1, fCountInClicks));
        else if (fState == LooperState::Recording)
            status.progress = fTimed ? std::min(99, static_cast<int>(uint64_t { fLength } * 100 / fRecLength)) : 0;
        else
            status.progress = fLength > 0 ? std::min(99, static_cast<int>(fPosition / fLength * 100.0)) : 0;
        fMeter = status.encode();
    }

    float meter() const override { return fMeter; }

    // The loop survives chain changes (the engine reuses the block); reset() only rewinds
    void reset() override { fPosition = 0.0; }

private:
    // One sample of the count-in's timeline (Count In mode, from the press on): starts the recording when the
    // count-in is over, ends the overdub passes, and starts each click (process() plays it from fClickData)
    void tick()
    {
        if (fState == LooperState::CountIn && fClock >= fRecStart + fRecLatency)
        {
            // a round trip after the downbeat's click: when what was played to it arrives
            fLength = 0;
            fState = LooperState::Recording;
        }
        if (fState == LooperState::Recording && fClock >= fRecStart + fRecLength)
        {
            // the grid's next downbeat: the loop comes round now, from its first sample (its downbeat, attack and
            // all), while the bar's last round trip, still on its way in, goes on into the loop's end
            fTailPos = fLength;
            fLength = fRecLength;
            fPosition = 0.0;
            fState = fPasses > 1 ? LooperState::Overdubbing : LooperState::Playing;
            if (fPasses <= 1)
                fTimed = false;
            fFade.snap(1.0f);
            if (fTailPos >= fLength)
                finishTail();
        }
        if (fState == LooperState::Overdubbing && fClock >= fEnd)
        {
            // every pass is recorded: just the loop from now on
            fState = LooperState::Playing;
            fTimed = false;
            return;
        }
        if (fClock < fClickEnd && fClock == static_cast<uint64_t>(std::llround(fNextClick * fBeatLength)))
        {
            // the bar's first beat is accented: bars start where the recording does
            const int beat = ((fNextClick - fCountInClicks) % fBarBeats + fBarBeats) % fBarBeats;
            fClickData = beat == 0 ? &fAccentClick : &fBeatClick;
            fClickPos = 0;
            fClickGain = fClickLevel * kClickGain * (beat == 0 ? 1.0f : 0.8f);
            ++fNextClick;
        }
        ++fClock;
    }

    void processSample(float* const* io, uint32_t numChannels, uint32_t i)
    {
        // the input's last moments, kept for the seam: a recording starting now takes them as what led into it
        if (fState == LooperState::Recording && fLength == 0)
            for (uint32_t ch = 0; ch < 2; ++ch)
                for (uint32_t k = 0; k < kFadeSamples; ++k)
                    fPreroll[ch][k] = fRecent[ch][(fRecentPos + k) % kFadeSamples];
        for (uint32_t ch = 0; ch < 2; ++ch)
            fRecent[ch][fRecentPos] = io[numChannels > 1 ? ch : 0][i];
        fRecentPos = (fRecentPos + 1) % kFadeSamples;

        const float level = fLevel.next();
        const float feedback = fFeedback.next();
        // playback fades in and out on start and stop
        fFade.setTarget(fState == LooperState::Playing || fState == LooperState::Overdubbing ? 1.0f : 0.0f);
        const float fade = fFade.next();

        if (fState == LooperState::CountIn)
            return;
        if (fState == LooperState::Recording)
        {
            // Count In mode's recording ends on the grid (tick()); Free mode's when pressed, or when the buffer is full
            if (fLength < kLooperMaxSamples && !(fTimed && fLength >= fRecLength))
            {
                for (uint32_t ch = 0; ch < 2; ++ch)
                    fBuffer[ch][fLength] = io[numChannels > 1 ? ch : 0][i];
                ++fLength;
            }
            if (!fTimed && fLength >= kLooperMaxSamples)
                closeLoop();
            return;
        }
        // Count In mode, just after the loop came round: the last bar's last round trip arrives, into the loop's end
        const bool tailing = fTailPos < fLength;
        if (tailing)
        {
            for (uint32_t ch = 0; ch < 2; ++ch)
                fBuffer[ch][fTailPos] = io[numChannels > 1 ? ch : 0][i];
            if (++fTailPos >= fLength)
                finishTail();
        }
        if (fLength == 0 || (fState == LooperState::Stopped && fade <= 0.0f))
            return;

        const auto index = static_cast<uint32_t>(fPosition);
        const double frac = fPosition - index;
        const uint32_t next = index + 1 < fLength ? index + 1 : 0;
        const double step = (fHalf ? 0.5 : 1.0) * (fReverse ? -1.0 : 1.0);
        // overdubs go where the playback was a round trip ago: what the player heard and played along to
        uint32_t write = index;
        if (fState == LooperState::Overdubbing && fLatency > 0)
        {
            const double length = static_cast<double>(fLength);
            double at = std::fmod(fPosition - step * static_cast<double>(fLatency), length);
            if (at < 0.0)
                at += length;
            write = std::min(static_cast<uint32_t>(at), fLength - 1);
        }
        for (uint32_t ch = 0; ch < numChannels; ++ch)
        {
            std::vector<float>& buffer = fBuffer[ch];
            const float played = static_cast<float>(buffer[index] + (buffer[next] - buffer[index]) * frac);
            if (fState == LooperState::Overdubbing && !tailing) // the tail just wrote this input
                buffer[write] = buffer[write] * feedback + io[ch][i];
            io[ch][i] += played * level * fade;
        }
        if (fState == LooperState::Overdubbing && !tailing && numChannels == 1)
            fBuffer[1][write] = fBuffer[0][write];

        if (fState != LooperState::Stopped)
        {
            fPosition += step;
            if (fPosition >= fLength)
                fPosition -= fLength;
            if (fPosition < 0.0)
                fPosition += fLength;
        }
    }

    // Count In mode: the metronome starts, counts in, and times the recording, with the settings as they are now
    void startCountIn()
    {
        fBeatLength = fSampleRate * 60.0 / fBpm;
        fBarBeats = fBeats;
        fCountInClicks = fCountIn;
        fPasses = fLoops;
        fRecStart = static_cast<uint64_t>(std::llround(fCountIn * fBeatLength));
        fRecLength = static_cast<uint32_t>(
            std::clamp<long long>(std::llround(fMeasures * fBeats * fBeatLength), kFadeSamples * 4, kLooperMaxSamples));
        fRecLatency = std::min(fLatency, fRecLength / 2);
        // the clicks stop on the grid; the passes, which run a round trip late, that much after
        fClickEnd = fRecStart + uint64_t { fRecLength } * static_cast<uint64_t>(fPasses);
        fEnd = fClickEnd + fRecLatency;
        fClock = 0;
        fNextClick = 0;
        fTimed = true;
        fLength = 0;
        fState = LooperState::CountIn;
    }

    void recDub()
    {
        endTail();
        switch (fState)
        {
        case LooperState::Empty:
            if (fHasCountIn && fCountInMode)
            {
                startCountIn();
                break;
            }
            fLength = 0;
            fState = LooperState::Recording;
            break;
        case LooperState::CountIn:
            clear(); // a second press calls it off
            break;
        case LooperState::Recording:
            fTimed = false;
            closeLoop();
            break;
        case LooperState::Playing:
            fState = LooperState::Overdubbing;
            break;
        case LooperState::Overdubbing:
            fTimed = false;
            fState = LooperState::Playing;
            break;
        case LooperState::Stopped:
            fPosition = fReverse ? fLength - 1 : 0.0;
            fState = LooperState::Overdubbing;
            break;
        }
    }

    void playStop()
    {
        endTail();
        switch (fState)
        {
        case LooperState::CountIn:
            clear();
            break;
        case LooperState::Recording:
            fTimed = false;
            closeLoop();
            break;
        case LooperState::Playing:
        case LooperState::Overdubbing:
            fTimed = false;
            fState = LooperState::Stopped;
            break;
        case LooperState::Stopped:
            fPosition = fReverse ? fLength - 1 : 0.0;
            fState = LooperState::Playing;
            break;
        case LooperState::Empty:
            break;
        }
    }

    void closeLoop()
    {
        if (fLength < kFadeSamples * 4)
        {
            // too short to be a loop: treat as a mistake
            clear();
            return;
        }
        crossfadeSeam();
        fPosition = 0.0;
        fState = LooperState::Playing;
        fFade.snap(1.0f);
    }

    // The seam: the loop's last moments crossfade into what was played just before its start, so the end runs on
    // into the start without a click, and the start (a downbeat's attack, often) stays as it was played
    void crossfadeSeam()
    {
        for (uint32_t ch = 0; ch < 2; ++ch)
        {
            std::vector<float>& buffer = fBuffer[ch];
            for (uint32_t k = 0; k < kFadeSamples; ++k)
            {
                const float g = static_cast<float>(k + 1) / kFadeSamples;
                float& sample = buffer[fLength - kFadeSamples + k];
                sample += (fPreroll[ch][k] - sample) * g;
            }
        }
    }

    // Count In mode: the loop's end is all in
    void finishTail()
    {
        fTailPos = kNoTail;
        crossfadeSeam();
    }

    // A press during the tail: what hasn't arrived yet is left silent
    void endTail()
    {
        if (fTailPos >= fLength)
            return;
        for (uint32_t ch = 0; ch < 2; ++ch)
            std::fill(fBuffer[ch].begin() + fTailPos, fBuffer[ch].begin() + fLength, 0.0f);
        finishTail();
    }

    void clear()
    {
        fTimed = false;
        fTailPos = kNoTail;
        fState = LooperState::Empty;
        fLength = 0;
        fPosition = 0.0;
        fFade.snap(0.0f);
    }

    double fSampleRate = 48000.0;
    std::vector<float> fBuffer[2];
    LooperState fState = LooperState::Empty;
    uint32_t fLength = 0;
    double fPosition = 0.0;
    bool fReverse = false, fHalf = false;
    bool fRecDubHeld = false, fPlayStopHeld = false, fClearHeld = false;
    Smoother fLevel, fFeedback, fFade;
    float fMeter = 0.0f;

    // Count In mode: its settings
    bool fHasCountIn = false; // the config's "countIn": blocks saved before Count In stay in Free mode
    bool fCountInMode = true;
    double fBpm = 120.0;
    int fBeats = 4, fCountIn = 8, fMeasures = 4, fLoops = 1;
    float fClickLevel = 1.5f;
    // Tap: the footswitch's taps, timed by the samples processed
    TapTempo fTapTempo;
    uint64_t fSamples = 0;
    bool fTapHeld = false;
    // the input's last kFadeSamples (a ring), and what led into the recording, for the seam
    float fRecent[2][kFadeSamples] = {};
    uint32_t fRecentPos = 0;
    float fPreroll[2][kFadeSamples] = {};
    // Latency: the round trip, in ms and in samples
    double fLatencyMs = 0.0;
    uint32_t fLatency = 0;
    // Count In mode: the next sample of the loop's end to write once it's playing (the last round trip of its last
    // bar, still arriving); kNoTail when there's none
    static constexpr uint32_t kNoTail = 0xffffffffu;
    uint32_t fTailPos = kNoTail;

    uint32_t latencySamples(double ms) const { return static_cast<uint32_t>(std::lround(ms * fSampleRate / 1000.0)); }
    float* fClickOut = nullptr; // the engine's click bus (setClickOutput()), or none
    // the timeline, in samples from the press (fTimed while it runs: counting in, recording, overdub passes)
    bool fTimed = false;
    uint64_t fClock = 0;
    double fBeatLength = 24000.0;
    int fBarBeats = 4, fCountInClicks = 4, fPasses = 1;
    int fNextClick = 0;       // clicks so far
    uint64_t fRecStart = 0;   // when the recording starts
    uint32_t fRecLength = 0;  // the loop's length
    uint64_t fEnd = 0;        // when the last pass ends
    uint64_t fClickEnd = 0;   // when the clicks stop (the last pass's end on the grid)
    uint32_t fRecLatency = 0; // the round trip, as the count-in took it
    // the clicks (the metronome's Classic sound), one at a time
    std::vector<float> fAccentClick, fBeatClick;
    const std::vector<float>* fClickData = nullptr;
    size_t fClickPos = 0;
    float fClickGain = 0.0f;
};

} // namespace

const BlockDescriptor kLooperBlockDescriptor = {
    "looper",
    "Looper",
    Category::Delay,
    kLooperParamCount,
    {
        ParamSpec { "Rec/Dub", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Play/Stop", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Clear", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Level", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Feedback", "%", 0.0f, 100.0f, 100.0f, ParamScale::Linear },
        ParamSpec { "Reverse", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Half Speed", "", 0.0f, 1.0f, 0.0f, ParamScale::Toggle },
        ParamSpec { "Mode", "", 0.0f, 1.0f, 0.0f, ParamScale::Choice, kModeChoices },
        ParamSpec { "BPM", "BPM", 40.0f, 240.0f, 120.0f, ParamScale::Linear },
        ParamSpec { "Beats", "", 1.0f, 12.0f, 4.0f, ParamScale::Choice, kBeatChoices },
        ParamSpec { "Count-In", "", 0.0f, 16.0f, 8.0f, ParamScale::Choice, kCountInChoices },
        ParamSpec { "Measures", "", 1.0f, 16.0f, 4.0f, ParamScale::Choice, kMeasureChoices },
        ParamSpec { "Loops", "", 1.0f, 8.0f, 1.0f, ParamScale::Choice, kLoopChoices },
        ParamSpec { "Click", "%", 0.0f, 200.0f, 150.0f, ParamScale::Linear },
        ParamSpec { "Tap", "", 0.0f, 1.0f, 0.0f, ParamScale::Momentary },
        ParamSpec { "Latency", "ms", 0.0f, 200.0f, 10.0f, ParamScale::Linear },
    },
    [] { return std::unique_ptr<Block>(new LooperBlock()); },
    -1,
    -1,
    false,
    "Loop",
    0.0f,
    MeterStyle::Looper,
};

} // namespace xythos
