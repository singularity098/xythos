// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/Params.hpp"

#include <cstdio>

namespace xythos {

namespace {

constexpr GlobalParamSpec kGlobalParams[kGlobalParamCount] = {
    { "Input Mode",        "input_mode",  "",   0.0f,   1.0f,   0.0f, kParamFlagToggle },
    { "Input",             "input",       "dB", -24.0f, 24.0f,  0.0f, kParamFlagNone },
    { "Bass",              "bass",        "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Middle",            "middle",      "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Treble",            "treble",      "dB", -12.0f, 12.0f,  0.0f, kParamFlagNone },
    { "Output",            "output",      "dB", -24.0f, 24.0f,  0.0f, kParamFlagNone },
    { "Bypass",            "bypass",      "",   0.0f,   1.0f,   0.0f, kParamFlagToggle },
    { "Limiter",           "limiter",     "",   0.0f,   1.0f,   1.0f, kParamFlagToggle },
    { "Input Peak L",      "in_peak_l",   "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Input Peak R",      "in_peak_r",   "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Output Peak L",     "out_peak_l",  "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Output Peak R",     "out_peak_r",  "dB", -60.0f, 6.0f, -60.0f, kParamFlagOutput },
    { "Input Clip",        "in_clip",     "",   0.0f,   1.0f,   0.0f, kParamFlagOutput | kParamFlagToggle },
    { "Output Clip",       "out_clip",    "",   0.0f,   1.0f,   0.0f, kParamFlagOutput | kParamFlagToggle },
    { "Limiter Reduction", "limiter_gr",  "dB", 0.0f,   24.0f,  0.0f, kParamFlagOutput },
};

// Fixed part of the later parameters; the slot meters are generated in globalParamSpec()
constexpr GlobalParamSpec kLaterParams[kParamSlotMeterBase - kFirstLaterParam] = {
    { "Running Slots 1-16",  "running_lo", "", 0.0f, 65535.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Running Slots 17-32", "running_hi", "", 0.0f, 65535.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Instance ID",         "instance_id", "", 0.0f, 16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "Chain Revision",      "chain_rev",   "", 0.0f, 16777215.0f, 0.0f, kParamFlagInteger | kParamFlagHidden },
};

constexpr GlobalParamSpec kMidiParams[kParamMergeBalance - kParamMidiLearn] = {
    { "MIDI Learn",    "midi_learn",    "", 0.0f, 1.0f, 0.0f, kParamFlagToggle | kParamFlagHidden },
    { "MIDI Last CC",  "midi_last_cc",  "", 0.0f, 2047.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "MIDI CC Count", "midi_cc_count", "", 0.0f, 16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
    { "MIDI Last Value", "midi_last_value", "", 0.0f, 127.0f, 0.0f, kParamFlagOutput | kParamFlagInteger | kParamFlagHidden },
};

// Split merge, tuner and metronome
constexpr GlobalParamSpec kToolParams[kParamSlotLatchBase - kParamMergeBalance] = {
    { "Merge Balance",       "merge_balance",  "%",  -100.0f, 100.0f, 0.0f, kParamFlagNone },
    { "Merge Level A",       "merge_level_a",  "dB", -24.0f,  24.0f,  0.0f, kParamFlagNone },
    { "Merge Level B",       "merge_level_b",  "dB", -24.0f,  24.0f,  0.0f, kParamFlagNone },
    { "Merge Pan A",         "merge_pan_a",    "%",  -100.0f, 100.0f, 0.0f, kParamFlagNone },
    { "Merge Pan B",         "merge_pan_b",    "%",  -100.0f, 100.0f, 0.0f, kParamFlagNone },
    { "Merge Invert A",      "merge_invert_a", "",   0.0f,    1.0f,   0.0f, kParamFlagToggle },
    { "Merge Invert B",      "merge_invert_b", "",   0.0f,    1.0f,   0.0f, kParamFlagToggle },
    { "Tuner",               "tuner",          "",   0.0f,    1.0f,   0.0f, kParamFlagToggle },
    { "Tuner Mute",          "tuner_mute",     "",   0.0f,    1.0f,   1.0f, kParamFlagToggle },
    { "Tuner Reference",     "tuner_ref",      "Hz", 415.0f,  466.0f, 440.0f, kParamFlagNone },
    { "Tuner Strobe",        "tuner_strobe",   "",   0.0f,    1.0f,   0.0f, kParamFlagToggle | kParamFlagHidden },
    { "Tuner Frequency",     "tuner_freq",     "Hz", 0.0f,    2000.0f, 0.0f, kParamFlagOutput },
    { "Tuner Clarity",       "tuner_clarity",  "",   0.0f,    1.0f,   0.0f, kParamFlagOutput },
    { "Metronome",           "metro",          "",    0.0f,   1.0f,   0.0f,   kParamFlagToggle },
    { "Metronome Tempo",     "metro_tempo",    "BPM", 20.0f,  300.0f, 100.0f, kParamFlagNone },
    { "Metronome Volume",    "metro_volume",   "%",   0.0f,   100.0f, 80.0f,  kParamFlagNone },
    { "Metronome DAW Sync",  "metro_sync",     "",    0.0f,   1.0f,   0.0f,   kParamFlagToggle },
    { "Metronome Beats",     "metro_beats",    "",    1.0f,   16.0f,  4.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Metronome Note Value", "metro_note",    "",    0.0f,   3.0f,   1.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Metronome Grouping",  "metro_grouping", "",    0.0f,   65535.0f, 0.0f, kParamFlagInteger | kParamFlagHidden },
    { "Metronome Subdivision", "metro_sub",    "",    0.0f,   10.0f,  0.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Metronome Sound",     "metro_sound",    "",    0.0f,   2.0f,   0.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Metronome Sub Volume", "metro_sub_volume", "%", 0.0f,  100.0f, 50.0f,  kParamFlagHidden },
    { "Metronome Trainer",   "metro_trainer",  "",    0.0f,   2.0f,   0.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Trainer Start",       "metro_tr_start", "BPM", 20.0f,  300.0f, 80.0f,  kParamFlagInteger | kParamFlagHidden },
    { "Trainer End",         "metro_tr_end",   "BPM", 20.0f,  300.0f, 120.0f, kParamFlagInteger | kParamFlagHidden },
    { "Trainer Step",        "metro_tr_step",  "BPM", 1.0f,   50.0f,  5.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Trainer Every",       "metro_tr_every", "",    1.0f,   64.0f,  4.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Trainer Bars",        "metro_tr_bars",  "",    1.0f,   512.0f, 32.0f,  kParamFlagInteger | kParamFlagHidden },
    { "Gap Trainer",         "metro_gap",      "",    0.0f,   1.0f,   0.0f,   kParamFlagToggle | kParamFlagHidden },
    { "Gap Play",            "metro_gap_play", "",    1.0f,   32.0f,  2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Gap Mute",            "metro_gap_mute", "",    0.0f,   32.0f,  2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 1",            "metro_accent1",        "",    0.0f,   3.0f,   3.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 2",            "metro_accent2",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 3",            "metro_accent3",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 4",            "metro_accent4",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 5",            "metro_accent5",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 6",            "metro_accent6",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 7",            "metro_accent7",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 8",            "metro_accent8",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 9",            "metro_accent9",        "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 10",           "metro_accent10",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 11",           "metro_accent11",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 12",           "metro_accent12",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 13",           "metro_accent13",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 14",           "metro_accent14",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 15",           "metro_accent15",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Accent 16",           "metro_accent16",       "",    0.0f,   3.0f,   2.0f,   kParamFlagInteger | kParamFlagHidden },
    { "Metronome Beat",      "metro_beat",     "",    -1.0f,  15.0f,  -1.0f,  kParamFlagOutput | kParamFlagInteger },
    { "Metronome Beat Count", "metro_count",   "",    0.0f,   16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Metronome Bar",       "metro_bar",      "",    0.0f,   16777215.0f, 0.0f, kParamFlagOutput | kParamFlagInteger },
    { "Metronome Live Tempo", "metro_live_tempo", "BPM", 0.0f, 300.0f, 0.0f,  kParamFlagOutput },
    { "Metronome Silent",    "metro_silent",   "",    0.0f,   1.0f,   0.0f,   kParamFlagOutput | kParamFlagToggle },
    { "Merge Align",         "merge_align",    "ms",  -5.0f,  5.0f,   0.0f,   kParamFlagNone },
};

// Added after the latches, in index order
const GlobalParamSpec kNewestParams[] = {
    // the tuner's Runes view (owner, 2026-10-31: the default; Needle and Strobe stay, on Tuner Strobe)
    { "Tuner Runes",         "tuner_runes",    "",    0.0f,   1.0f,   1.0f,   kParamFlagToggle | kParamFlagHidden },
};
static_assert(sizeof(kNewestParams) / sizeof(kNewestParams[0]) == kParamCount - kParamSlotLatchEnd);

// The per-slot outputs: the meters, and the latched footswitches
struct SlotOutputSpecs {
    char names[kMaxBlocks][24];
    char symbols[kMaxBlocks][16];
    GlobalParamSpec specs[kMaxBlocks];

    SlotOutputSpecs(const char* name, const char* symbol, float min, float max, uint32_t flags)
    {
        for (int slot = 0; slot < kMaxBlocks; ++slot)
        {
            std::snprintf(names[slot], sizeof(names[slot]), "Slot %02d %s", slot + 1, name);
            std::snprintf(symbols[slot], sizeof(symbols[slot]), "s%02d_%s", slot + 1, symbol);
            specs[slot] = { names[slot], symbols[slot], "", min, max, 0.0f, flags };
        }
    }
};

} // namespace

const GlobalParamSpec& globalParamSpec(uint32_t index)
{
    if (index >= kParamSlotMeterBase)
    {
        static const SlotOutputSpecs meters("Meter", "meter", -60.0f, 60.0f, kParamFlagOutput | kParamFlagHidden);
        if (index < kParamSlotMeterEnd)
            return meters.specs[index - kParamSlotMeterBase];
        if (index >= kParamSlotLatchEnd)
            return kNewestParams[index - kParamSlotLatchEnd];
        if (index >= kParamSlotLatchBase)
        {
            static const SlotOutputSpecs latches("Latched", "latched", 0.0f, 65535.0f,
                                                 kParamFlagOutput | kParamFlagInteger | kParamFlagHidden);
            return latches.specs[index - kParamSlotLatchBase];
        }
        return index < kParamMergeBalance ? kMidiParams[index - kParamMidiLearn] : kToolParams[index - kParamMergeBalance];
    }
    return index >= kFirstLaterParam ? kLaterParams[index - kFirstLaterParam] : kGlobalParams[index];
}

float midiCcToValue(uint32_t index, int ccValue)
{
    const int clamped = ccValue < 0 ? 0 : (ccValue > 127 ? 127 : ccValue);
    const float n = clamped / 127.0f;
    if (isSlotParam(index))
    {
        // slot bypass is a switch; block parameters are normalized
        return (index - kSlotParamBase) % kSlotParamCount == 0 ? (clamped >= 64 ? 1.0f : 0.0f) : n;
    }
    const GlobalParamSpec& spec = globalParamSpec(index);
    if (spec.flags & kParamFlagToggle)
        return clamped >= 64 ? spec.max : spec.min;
    return spec.min + n * (spec.max - spec.min);
}

} // namespace xythos
