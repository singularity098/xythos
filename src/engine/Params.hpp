// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace xythos {

constexpr int kMaxBlocks = 32;
constexpr int kParamsPerSlot = 16;
constexpr int kSlotParamCount = 1 + kParamsPerSlot; // bypass + block parameters

// Host parameter indices. These are the DAW's automation IDs, so existing indices must never move:
// parameters added in later milestones go after the slots (see kFirstLaterParam).
enum GlobalParam : uint32_t {
    kParamInputMode,        // 0 = mono, 1 = stereo
    kParamInput,            // dB
    kParamBass,             // dB
    kParamMiddle,           // dB
    kParamTreble,           // dB
    kParamOutput,           // dB
    kParamBypass,           // global bypass
    kParamLimiter,          // output limiter on/off

    // outputs (DSP -> UI)
    kParamInPeakL,          // dBFS
    kParamInPeakR,
    kParamOutPeakL,
    kParamOutPeakR,
    kParamInClip,           // 1 while the input has recently clipped
    kParamOutClip,
    kParamLimiterReduction, // dB of gain reduction (positive)

    kGlobalParamCount
};

constexpr uint32_t kSlotParamBase = kGlobalParamCount;
constexpr uint32_t kFirstLaterParam = kSlotParamBase + kMaxBlocks * kSlotParamCount;

// Parameters added after the slot block. Append only.
enum LaterParam : uint32_t {
    // outputs: which slots have a block running in the audio engine, as two 16-bit masks (floats hold these exactly)
    kParamRunningSlotsLow = kFirstLaterParam,
    kParamRunningSlotsHigh,

    // chain exchange for hosts that don't deliver UI state (see state/ChainExchange.hpp)
    kParamInstanceId,       // output: this plugin instance's random ID
    kParamChainRevision,    // input, hidden: bumped by the UI after writing the exchange file

    // outputs: one meter per slot, for blocks that show a level (Block::meter(), e.g. compressor gain reduction)
    kParamSlotMeterBase,
    kParamSlotMeterEnd = kParamSlotMeterBase + kMaxBlocks,

    // MIDI learn: the UI asks for learning by setting kParamMidiLearn to 1; the DSP reports every CC it receives
    // as channel * 128 + cc in kParamMidiLastCc (value in kParamMidiLastValue) and bumps kParamMidiCcCount, so the
    // UI can map it, and can pass mapped values on to the host in formats where the DSP can't
    kParamMidiLearn = kParamSlotMeterEnd, // input, hidden
    kParamMidiLastCc,                     // output, hidden
    kParamMidiCcCount,                    // output, hidden
    kParamMidiLastValue,                  // output, hidden: 0..127

    // merge of the split (M6)
    kParamMergeBalance,     // -100 (only A) .. 100 (only B) %; both at full level in the middle
    kParamMergeLevelA,      // dB
    kParamMergeLevelB,
    kParamMergePanA,        // -100 (left) .. 100 (right) %; the chain is stereo after a merge, in Mono mode too
    kParamMergePanB,
    kParamMergeInvertA,     // phase invert
    kParamMergeInvertB,

    // tuner (M8)
    kParamTuner,            // on/off
    kParamTunerMute,        // mute the guitar while the tuner is on
    kParamTunerReference,   // A4 in Hz
    kParamTunerStrobe,      // hidden, UI only: strobe view (Hive, or Chevrons) instead of the needle (Vernier)
    kParamTunerFrequency,   // output: detected frequency in Hz, 0 when there's no clear note
    kParamTunerClarity,     // output: 0..1

    // metronome (M9). Tempo, volume, on/off and sync are automatable; the rest are hidden settings.
    kParamMetronome,        // on/off (start/stop, or armed to follow the DAW's transport)
    kParamMetroTempo,       // BPM
    kParamMetroVolume,      // %
    kParamMetroSync,        // follow the DAW's tempo, meter and transport
    kParamMetroBeats,       // beats per bar, 1..16
    kParamMetroNoteValue,   // 0..3: the beat is a half, quarter, eighth or sixteenth note
    kParamMetroGrouping,    // UI only: bit i set = a group starts at beat i (0 = no grouping)
    kParamMetroSubdivision, // MetroSubdivision
    kParamMetroSound,       // 0 Classic, 1 Digital, 2 Beep
    kParamMetroSubVolume,   // %, relative to the volume
    kParamMetroTrainer,     // 0 off, 1 step, 2 gradual
    kParamMetroTrainerStart, // BPM
    kParamMetroTrainerEnd,   // BPM
    kParamMetroTrainerStep,  // BPM per step
    kParamMetroTrainerEvery, // bars per step
    kParamMetroTrainerBars,  // bars for the gradual ramp
    kParamMetroGap,         // gap trainer on/off
    kParamMetroGapPlay,     // bars
    kParamMetroGapMute,     // bars
    kParamMetroAccent1,     // 16 beats: 0 mute, 1 soft, 2 normal, 3 accent
    kParamMetroAccent16 = kParamMetroAccent1 + 15,
    kParamMetroBeat,        // output: beat being heard (0-based), -1 when stopped
    kParamMetroBeatCount,   // output: counts beats, so the UI can pulse on every one
    kParamMetroBar,         // output: bar (0-based)
    kParamMetroLiveTempo,   // output: tempo being played (the trainer changes it; the DAW's in sync mode)
    kParamMetroSilent,      // output: 1 while the gap trainer mutes the click

    kParamMergeAlign,       // ms: > 0 delays branch B, < 0 delays branch A, to line up two amps' timing

    // Outputs, hidden: each slot's latched footswitches (Block::latched(), bit i = parameter i), so the UI can show
    // them and save them in presets
    kParamSlotLatchBase,
    kParamSlotLatchEnd = kParamSlotLatchBase + kMaxBlocks,

    // added after the latches (each new one goes last: the indices are DAW automation IDs)
    kParamTunerRunes = kParamSlotLatchEnd, // hidden, UI only: the Runes view (owner, 2026-10-31); over Tuner Strobe
    kParamTunerChevrons,    // hidden, UI only: with Tuner Strobe on, the Chevrons strobe instead of Hive (owner, 2026-10-31)

    kParamCount
};

constexpr uint32_t slotBypassParam(int slot)
{
    return kSlotParamBase + static_cast<uint32_t>(slot * kSlotParamCount);
}

constexpr uint32_t slotParam(int slot, int param)
{
    return slotBypassParam(slot) + 1 + static_cast<uint32_t>(param);
}

constexpr uint32_t slotMeterParam(int slot)
{
    return kParamSlotMeterBase + static_cast<uint32_t>(slot);
}

constexpr uint32_t slotLatchParam(int slot)
{
    return kParamSlotLatchBase + static_cast<uint32_t>(slot);
}

constexpr bool isSlotParam(uint32_t index)
{
    return index >= kSlotParamBase && index < kFirstLaterParam;
}

// The value (as the host sees it) a CC value 0..127 sets a mapped parameter to
float midiCcToValue(uint32_t index, int ccValue);

// Parameters a MIDI controller can be mapped to: the user-facing inputs (globals and slots)
constexpr bool isMidiMappable(uint32_t index)
{
    return index < kParamInPeakL || isSlotParam(index);
}

enum ParamFlags : uint32_t {
    kParamFlagNone    = 0,
    kParamFlagOutput  = 1 << 0,
    kParamFlagToggle  = 1 << 1,
    kParamFlagInteger = 1 << 2,
    kParamFlagHidden  = 1 << 3,
};

struct GlobalParamSpec {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    uint32_t flags;
};

// Spec for any parameter that isn't a slot parameter (globals and later parameters)
const GlobalParamSpec& globalParamSpec(uint32_t index);

} // namespace xythos
