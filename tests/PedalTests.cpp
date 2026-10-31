// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

// Behaviour tests for the pedal blocks: dynamics, EQ, modulation and wah

#include <doctest/doctest.h>

#include "TestSupport.hpp"
#include "engine/TempoSync.hpp"
#include "engine/blocks/AmbientBlocks.hpp"
#include "engine/blocks/BitcrusherBlock.hpp"
#include "engine/blocks/ChorusBlock.hpp"
#include "engine/blocks/DoublerBlock.hpp"
#include "engine/blocks/EchoBlocks.hpp"
#include "engine/blocks/EqBlocks.hpp"
#include "engine/blocks/FilterBlock.hpp"
#include "engine/blocks/FlangerBlock.hpp"
#include "engine/blocks/GateBlock.hpp"
#include "engine/TapTempo.hpp"
#include "engine/blocks/LooperBlock.hpp"
#include "engine/blocks/ModSweeperBlock.hpp"
#include "engine/blocks/OctaveBlock.hpp"
#include "engine/blocks/PhaserBlock.hpp"
#include "engine/blocks/PitchBlocks.hpp"
#include "engine/blocks/RingModBlock.hpp"
#include "engine/blocks/RotaryBlock.hpp"
#include "engine/blocks/SpringReverbBlock.hpp"
#include "engine/blocks/SynthBlock.hpp"
#include "engine/blocks/TremoloBlock.hpp"
#include "engine/blocks/VolumeBlocks.hpp"
#include "engine/blocks/WahBlocks.hpp"
#include "engine/dsp/PitchTracker.hpp"

#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace xythos;

namespace {

constexpr double kRate = 48000.0;

std::vector<std::vector<float>> sine(double hz, float amplitude, size_t length, int channels = 1)
{
    std::vector<std::vector<float>> x(channels, std::vector<float>(length));
    for (auto& ch : x)
        for (size_t i = 0; i < length; ++i)
            ch[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kRate));
    return x;
}

// Steady-state gain in dB of a block for a sine
double sineGainDb(Block& block, double hz, float amplitude = 0.1f, int channels = 1, int channel = 0)
{
    auto x = sine(hz, amplitude, 48000, channels);
    testing::runBlock(block, x);
    const double out = testing::rms(x[channel], 24000, 48000);
    return 20.0 * std::log10(out / (amplitude / std::sqrt(2.0)));
}

std::unique_ptr<Block> make(const BlockDescriptor& type)
{
    return testing::createWithDefaults(type, kRate);
}

// RMS level over windows of `window` samples
std::vector<double> envelope(const std::vector<float>& x, size_t window)
{
    std::vector<double> result;
    for (size_t t = 0; t + window <= x.size(); t += window)
        result.push_back(testing::rms(x, t, t + window));
    return result;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Noise gate

TEST_CASE("gate passes signals over the threshold and cuts those under it")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamReduction, 60.0f);
    CHECK(sineGainDb(*block, 200.0, 0.1f) == doctest::Approx(0.0).epsilon(0.01)); // -20 dB peak: open
    CHECK(block->meter() < 0.1f);
    CHECK(sineGainDb(*block, 200.0, 0.001f) < -55.0); // -60 dB peak: closed
    CHECK(block->meter() == doctest::Approx(60.0f).epsilon(0.01));
}

TEST_CASE("gate holds, then releases after the signal drops")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamHold, 100.0f);
    block->setParam(kGateParamRelease, 10.0f);
    auto x = sine(200.0, 0.1f, 48000);
    for (size_t i = 12000; i < x[0].size(); ++i)
        x[0][i] *= 0.003f; // -50 dB below the open level: under the threshold
    testing::runBlock(*block, x);
    const double open = testing::rms(x[0], 6000, 12000);
    // the detector takes a little while to fall, then 100 ms hold: still open 60 ms after the drop
    CHECK(testing::rms(x[0], 14400, 14880) / 0.003 == doctest::Approx(open / 1.0).epsilon(0.1));
    // well after hold + release: closed
    CHECK(testing::rms(x[0], 24000, 48000) < open * 0.003 * 0.01);
}

TEST_CASE("gate can listen to the rig input instead of its own")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamThreshold, -40.0f);
    block->setParam(kGateParamKey, static_cast<float>(GateKey::Rig));
    auto x = sine(200.0, 0.5f, 24000);
    std::vector<float> rig(24000, 0.0f); // the guitar is silent; the block's input is loud (e.g. amp hiss)
    for (size_t t = 0; t < 24000; t += 120)
    {
        float* io[1] = { x[0].data() + t };
        const float* key[1] = { rig.data() + t };
        block->setRigInput(key, 1);
        block->process(io, 1, 120);
    }
    CHECK(testing::rms(x[0], 12000, 24000) < 1e-4);
}

TEST_CASE("expander attenuates in proportion to how far under the threshold the signal is")
{
    auto block = make(kGateBlockDescriptor);
    block->setParam(kGateParamType, static_cast<float>(GateType::Expander));
    block->setParam(kGateParamThreshold, -30.0f);
    block->setParam(kGateParamHysteresis, 0.0f);
    // a square keeps the peak detector steady: 10 dB under the threshold -> 20 dB of attenuation (ratio 3)
    std::vector<std::vector<float>> x(1, std::vector<float>(48000));
    for (size_t i = 0; i < x[0].size(); ++i)
        x[0][i] = (i / 48) % 2 ? 0.01f : -0.01f; // -40 dB
    testing::runBlock(*block, x);
    CHECK(block->meter() == doctest::Approx(20.0f).epsilon(0.02));
}

// ---------------------------------------------------------------------------------------------------------------------
// EQ

TEST_CASE("graphic EQ boosts and cuts its bands")
{
    auto block = make(kGraphicEqBlockDescriptor);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(0.0).epsilon(0.01));
    block->setParam(3, 12.0f); // 800 Hz
    block->setParam(6, -12.0f); // 6.4 kHz
    CHECK(sineGainDb(*block, 800.0) == doctest::Approx(12.0).epsilon(0.03));
    CHECK(sineGainDb(*block, 6400.0) == doctest::Approx(-12.0).epsilon(0.03));
    CHECK(std::fabs(sineGainDb(*block, 100.0)) < 0.5);
    block->setParam(kGraphicEqParamLevel, -6.0f);
    CHECK(sineGainDb(*block, 100.0) == doctest::Approx(-6.0).epsilon(0.1));
}

TEST_CASE("parametric EQ: mid band, cuts off at the ends of their ranges, and cutting")
{
    auto block = make(kParametricEqBlockDescriptor);
    CHECK(std::fabs(sineGainDb(*block, 30.0)) < 0.05);
    CHECK(std::fabs(sineGainDb(*block, 15000.0)) < 0.05);
    block->setParam(kParEqParamMidFreq, 1000.0f);
    block->setParam(kParEqParamMid, 9.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(9.0).epsilon(0.03));
    block->setParam(kParEqParamLowCut, 300.0f);
    CHECK(sineGainDb(*block, 50.0) < -25.0);
    block->setParam(kParEqParamHighCut, 3000.0f);
    CHECK(sineGainDb(*block, 15000.0) < -25.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Tremolo

TEST_CASE("tremolo modulates the level at its rate and depth")
{
    auto block = make(kTremoloBlockDescriptor);
    block->setParam(kTremParamRate, 4.0f);
    block->setParam(kTremParamDepth, 100.0f);
    auto x = sine(2000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    // 4 Hz: one cycle per 12000 samples; with 480-sample windows, 25 windows per cycle
    const auto env = envelope(x[0], 480);
    const double peak = *std::max_element(env.begin(), env.end());
    const double dip = *std::min_element(env.begin(), env.end());
    CHECK(peak == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.05));
    CHECK(dip < peak * 0.05);
    // the loudest windows repeat every 25 windows
    const size_t first = std::max_element(env.begin(), env.begin() + 25) - env.begin();
    const size_t second = std::max_element(env.begin() + 25, env.begin() + 50) - env.begin();
    CHECK(second - first == 25);
}

TEST_CASE("tremolo follows the host tempo when synced")
{
    auto block = make(kTremoloBlockDescriptor);
    const ParamSpec& rate = kTremoloBlockDescriptor.params[kTremParamRate];
    // pick 1/4 at 120 BPM: 2 Hz
    float n = 0.0f;
    while (std::string(rateNoteName(rateNoteIndex(n))) != "1/4")
        n += 0.01f;
    block->setParam(kTremParamRate, rate.toValue(n));
    block->setParam(kTremParamSync, 1.0f);
    block->setParam(kTremParamDepth, 100.0f);
    block->setTempo(120.0);
    auto x = sine(2000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    const auto env = envelope(x[0], 480); // 50 windows per cycle at 2 Hz
    const size_t first = std::max_element(env.begin(), env.begin() + 50) - env.begin();
    const size_t second = std::max_element(env.begin() + 50, env.begin() + 100) - env.begin();
    CHECK(second - first == 50);
}

TEST_CASE("harmonic tremolo at zero depth and pan's power are flat")
{
    auto block = make(kTremoloBlockDescriptor);
    block->setParam(kTremParamType, static_cast<float>(TremoloType::Harmonic));
    block->setParam(kTremParamDepth, 0.0f);
    CHECK(std::fabs(sineGainDb(*block, 300.0)) < 0.01);
    CHECK(std::fabs(sineGainDb(*block, 3000.0)) < 0.01);

    block->setParam(kTremParamType, static_cast<float>(TremoloType::Pan));
    block->setParam(kTremParamDepth, 100.0f);
    auto x = sine(1000.0, 0.5f, 48000, 2);
    testing::runBlock(*block, x);
    const auto left = envelope(x[0], 480), right = envelope(x[1], 480);
    for (size_t w = 0; w < left.size(); ++w)
        CHECK(left[w] * left[w] + right[w] * right[w] == doctest::Approx(2 * 0.125).epsilon(0.05));
}

// ---------------------------------------------------------------------------------------------------------------------
// Chorus, flanger, phaser, doubler

TEST_CASE("chorus at zero mix is dry, and vibrato keeps the level")
{
    auto block = make(kChorusBlockDescriptor);
    block->setParam(kChorusParamMix, 0.0f);
    auto x = sine(440.0, 0.5f, 24000);
    const auto dry = x;
    testing::runBlock(*block, x);
    for (size_t i = 0; i < x[0].size(); ++i)
        REQUIRE(x[0][i] == doctest::Approx(dry[0][i]).epsilon(1e-5));

    block->setParam(kChorusParamType, static_cast<float>(ChorusType::Vibrato));
    block->setParam(kChorusParamTone, 20000.0f);
    CHECK(std::fabs(sineGainDb(*block, 440.0)) < 0.2);
}

TEST_CASE("flanger without sweep is a comb filter at the Manual delay")
{
    auto block = make(kFlangerBlockDescriptor);
    block->setParam(kFlangerParamDepth, 0.0f);
    block->setParam(kFlangerParamManual, 1.0f);
    block->setParam(kFlangerParamFeedback, 0.0f);
    block->setParam(kFlangerParamMix, 50.0f);
    // 1 ms: notches at 500 Hz, 1500 Hz...; peaks at 1 kHz, 2 kHz... (3 dB up: the 50% mix's makeup gain)
    CHECK(sineGainDb(*block, 500.0) < -30.0);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(3.0).epsilon(0.02));
}

TEST_CASE("phaser without sweep notches where its stages reach 180 degrees")
{
    auto block = make(kPhaserBlockDescriptor);
    block->setParam(kPhaserParamDepth, 0.0f);
    block->setParam(kPhaserParamCenter, 1000.0f);
    block->setParam(kPhaserParamFeedback, 0.0f);
    block->setParam(kPhaserParamMix, 50.0f);
    // four first-order allpasses at 1 kHz: -180 degrees total at 1000 * tan(22.5 deg) and 1000 * tan(67.5 deg)
    CHECK(sineGainDb(*block, 1000.0 * std::tan(M_PI / 8.0)) < -30.0);
    CHECK(sineGainDb(*block, 1000.0 * std::tan(3.0 * M_PI / 8.0)) < -30.0);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(3.0).epsilon(0.02)); // with the 50% mix's makeup
}

TEST_CASE("doubler pans a delayed take out to one side")
{
    auto block = make(kDoublerBlockDescriptor);
    block->setParam(kDoublerParamDrift, 0.0f);
    block->setParam(kDoublerParamTone, 20000.0f);
    std::vector<std::vector<float>> x(2, std::vector<float>(4800, 0.0f));
    x[0][0] = x[1][0] = 1.0f;
    testing::runBlock(*block, x);
    // 18 ms = 864 samples: on the right only (width 100%)
    double leftLate = 0.0, rightLate = 0.0;
    for (size_t i = 800; i < 1000; ++i)
    {
        leftLate += std::fabs(x[0][i]);
        rightLate += std::fabs(x[1][i]);
    }
    CHECK(leftLate < 1e-3);
    CHECK(rightLate > 0.5);
    CHECK(x[0][0] == doctest::Approx(1.0f)); // dry stays at full level at 50% mix
}

// ---------------------------------------------------------------------------------------------------------------------
// Modulation sweeper

namespace {

std::vector<std::vector<float>> noise(size_t length, uint32_t seed = 7)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    std::vector<std::vector<float>> x(1, std::vector<float>(length));
    for (float& v : x[0])
        v = dist(rng);
    return x;
}

// The lag (0..400 samples) at which `out` best matches `in` over [from, to)
int bestLag(const std::vector<float>& out, const std::vector<float>& in, size_t from, size_t to)
{
    int best = -1;
    double bestCorr = 0.0;
    for (int lag = 0; lag <= 400; ++lag)
    {
        double corr = 0.0;
        for (size_t i = from; i < to; ++i)
            corr += static_cast<double>(out[i]) * in[i - lag];
        if (corr > bestCorr)
        {
            bestCorr = corr;
            best = lag;
        }
    }
    return best;
}

// A sweeper whose effects are plain delays, so each one can be told apart by its lag: with no depth, no feedback
// and full mix the vibrato delays by 5 ms (239 samples), the flanger by 1.5 ms (72) and the chorus by 7 ms (335)
std::unique_ptr<Block> sweeperOfDelays(SweeperEffect e1, SweeperEffect e2, int e3, SweeperPattern pattern)
{
    auto block = make(kModSweeperBlockDescriptor);
    block->setParam(kSweeperParamEffect1, static_cast<float>(e1));
    block->setParam(kSweeperParamEffect2, static_cast<float>(e2));
    block->setParam(kSweeperParamEffect3, static_cast<float>(e3));
    block->setParam(kSweeperParamPattern, static_cast<float>(pattern));
    block->setParam(kSweeperParamSweepRate, 1.0f);
    block->setParam(kSweeperParamGlide, 0.0f);
    block->setParam(kSweeperParamDepth, 0.0f);
    block->setParam(kSweeperParamMix, 100.0f);
    block->setParam(kSweeperParamFeedback, 0.0f);
    return block;
}

// The lag in the middle of each one-second step
std::vector<int> stepLags(Block& block, int steps)
{
    auto x = noise(static_cast<size_t>(steps) * 48000);
    const auto dry = x;
    testing::runBlock(block, x);
    std::vector<int> lags;
    for (int s = 0; s < steps; ++s)
        lags.push_back(bestLag(x[0], dry[0], s * 48000 + 12000, s * 48000 + 36000));
    return lags;
}

} // namespace

TEST_CASE("modulation sweeper at zero mix is dry all through its sweep, and doesn't allocate")
{
    auto block = make(kModSweeperBlockDescriptor);
    block->setParam(kSweeperParamEffect3, 3.0f); // phaser
    block->setParam(kSweeperParamSweepRate, 4.0f);
    block->setParam(kSweeperParamMix, 0.0f);
    auto x = sine(440.0, 0.5f, 48000);
    const auto dry = x;
    int allocations = 0;
    testing::runBlock(*block, x, 128, &allocations);
    CHECK(allocations == 0);
    for (size_t i = 0; i < x[0].size(); ++i)
        REQUIRE(x[0][i] == doctest::Approx(dry[0][i]).epsilon(1e-4));
}

TEST_CASE("modulation sweeper steps between two effects")
{
    auto block = sweeperOfDelays(SweeperEffect::Vibrato, SweeperEffect::Flanger, 0, SweeperPattern::BackAndForth);
    CHECK(stepLags(*block, 4) == std::vector<int> { 239, 72, 239, 72 });
}

TEST_CASE("modulation sweeper patterns with three effects")
{
    const int chorus = static_cast<int>(SweeperEffect::Chorus) + 1;
    auto backAndForth =
        sweeperOfDelays(SweeperEffect::Vibrato, SweeperEffect::Flanger, chorus, SweeperPattern::BackAndForth);
    CHECK(stepLags(*backAndForth, 6) == std::vector<int> { 239, 72, 335, 72, 239, 72 });
    auto cycle = sweeperOfDelays(SweeperEffect::Vibrato, SweeperEffect::Flanger, chorus, SweeperPattern::Cycle);
    CHECK(stepLags(*cycle, 6) == std::vector<int> { 239, 72, 335, 239, 72, 335 });
}

TEST_CASE("modulation sweeper glides between effects")
{
    // at 100% glide, halfway through a step the two effects are mixed equally
    auto block = sweeperOfDelays(SweeperEffect::Vibrato, SweeperEffect::Flanger, 0, SweeperPattern::BackAndForth);
    block->setParam(kSweeperParamGlide, 100.0f);
    auto x = noise(48000);
    const auto dry = x;
    testing::runBlock(*block, x);
    double a = 0.0, b = 0.0, energy = 0.0;
    for (size_t i = 23000; i < 25000; ++i)
    {
        a += static_cast<double>(x[0][i]) * dry[0][i - 239];
        b += static_cast<double>(x[0][i]) * dry[0][i - 72];
        energy += static_cast<double>(dry[0][i]) * dry[0][i];
    }
    CHECK(a / energy == doctest::Approx(0.5).epsilon(0.1));
    CHECK(b / energy == doctest::Approx(0.5).epsilon(0.1));
}

TEST_CASE("synced modulation sweeper moves on the beat")
{
    auto block = sweeperOfDelays(SweeperEffect::Vibrato, SweeperEffect::Flanger, 0, SweeperPattern::BackAndForth);
    const ParamSpec& rate = kModSweeperBlockDescriptor.params[kSweeperParamSweepRate];
    float n = 0.0f;
    while (std::string(rateNoteName(rateNoteIndex(n))) != "1/4")
        n += 0.01f;
    block->setParam(kSweeperParamSweepRate, rate.toValue(n));
    block->setParam(kSweeperParamSync, 1.0f);
    block->setTempo(120.0);
    // the song starts 0.3 beats in: beat 1 (the flanger's step) is at 0.7 beats = 0.35 s = sample 16800
    auto x = noise(48000);
    const auto dry = x;
    const double beatsPerSample = 120.0 / 60.0 / kRate;
    for (size_t t = 0; t < x[0].size(); t += 128)
    {
        block->setSongPosition(0.3 + static_cast<double>(t) * beatsPerSample);
        float* io[1] = { x[0].data() + t };
        block->process(io, 1, 128);
    }
    CHECK(bestLag(x[0], dry[0], 12000, 16000) == 239);
    CHECK(bestLag(x[0], dry[0], 17600, 28000) == 72);
    CHECK(bestLag(x[0], dry[0], 41600, 48000) == 239); // beat 2
}

// ---------------------------------------------------------------------------------------------------------------------
// Wah

TEST_CASE("wah: toe down brings the highs up")
{
    auto block = make(kWahBlockDescriptor);
    block->setParam(kWahParamPosition, 0.0f);
    const double heel = sineGainDb(*block, 2000.0);
    block->setParam(kWahParamPosition, 100.0f);
    const double toe = sineGainDb(*block, 2000.0);
    CHECK(toe - heel > 15.0);
}

TEST_CASE("auto wah opens up with louder playing")
{
    auto block = make(kAutoWahBlockDescriptor);
    block->setParam(kAutoWahParamSensitivity, 20.0f);
    const double quiet = sineGainDb(*block, 2000.0, 0.002f);
    const double loud = sineGainDb(*block, 2000.0, 0.2f);
    CHECK(loud - quiet > 10.0);
    // Down: the other way round
    block->setParam(kAutoWahParamDirection, 1.0f);
    CHECK(sineGainDb(*block, 2000.0, 0.002f) - sineGainDb(*block, 2000.0, 0.2f) > 10.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Octave, volume, swell, ring mod, bitcrusher, rotary

namespace {

// Amplitude of one frequency in x[from, to) (a single-bin DFT with a Hann window)
double toneAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    double re = 0.0, im = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i - from) / n);
        const double angle = 2.0 * M_PI * hz * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(angle);
        im -= w * x[i] * std::sin(angle);
    }
    return 4.0 * std::sqrt(re * re + im * im) / n;
}

// Level in a band around `hz` (+-6%): delay-line pitch shifting spreads a tone into sidebands at the grain rate,
// so a single DFT bin misses most of it
double bandAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    double power = 0.0;
    for (double f = hz * 0.94; f <= hz * 1.06; f += 2.0)
    {
        const double a = toneAt(x, f, from, to);
        power += a * a;
    }
    return std::sqrt(power);
}

} // namespace

TEST_CASE("octave divides by two and four, and rectifies up")
{
    struct Case {
        int param;
        double hz;
    };
    for (const Case c : { Case { kOctParamSub1, 110.0 }, Case { kOctParamSub2, 55.0 }, Case { kOctParamUp, 440.0 } })
    {
        CAPTURE(c.param);
        auto block = make(kOctaveBlockDescriptor);
        block->setParam(kOctParamSub1, 0.0f);
        block->setParam(kOctParamDry, 0.0f);
        block->setParam(c.param, 100.0f);
        auto x = sine(220.0, 0.3f, 48000);
        testing::runBlock(*block, x);
        // the new octave is the strongest component
        CHECK(toneAt(x[0], c.hz, 24000, 48000) > 2.0 * toneAt(x[0], 220.0, 24000, 48000));
    }
}

TEST_CASE("volume pedal tapers")
{
    auto block = make(kVolumeBlockDescriptor);
    block->setParam(kVolumeParamVolume, 50.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(-30.0).epsilon(0.01));
    block->setParam(kVolumeParamTaper, 1.0f);
    CHECK(sineGainDb(*block, 1000.0) == doctest::Approx(-6.02).epsilon(0.01));
    block->setParam(kVolumeParamVolume, 0.0f);
    auto x = sine(1000.0, 0.5f, 24000);
    testing::runBlock(*block, x);
    CHECK(testing::rms(x[0], 12000, 24000) == 0.0);
}

TEST_CASE("auto swell fades each new note in")
{
    auto block = make(kSwellBlockDescriptor);
    block->setParam(kSwellParamAttack, 300.0f);
    // a note, a pause, then another note
    auto x = sine(220.0, 0.3f, 72000);
    for (size_t i = 24000; i < 36000; ++i)
        x[0][i] = 0.0f;
    testing::runBlock(*block, x);
    const double full = 0.3 / std::sqrt(2.0);
    CHECK(testing::rms(x[0], 0, 2400) < 0.1 * full);         // the first 50 ms: barely started
    CHECK(testing::rms(x[0], 16800, 24000) > 0.95 * full);   // after the swell: full level
    CHECK(testing::rms(x[0], 36000, 38400) < 0.1 * full);    // the next note swells again
    CHECK(testing::rms(x[0], 52800, 60000) > 0.95 * full);
}

TEST_CASE("ring mod makes sum and difference tones")
{
    auto block = make(kRingModBlockDescriptor);
    block->setParam(kRingParamFrequency, 300.0f);
    auto x = sine(1000.0, 0.5f, 48000);
    testing::runBlock(*block, x);
    CHECK(toneAt(x[0], 700.0, 24000, 48000) == doctest::Approx(0.25).epsilon(0.02));
    CHECK(toneAt(x[0], 1300.0, 24000, 48000) == doctest::Approx(0.25).epsilon(0.02));
    CHECK(toneAt(x[0], 1000.0, 24000, 48000) < 0.005);
}

TEST_CASE("bitcrusher quantizes and holds")
{
    auto block = make(kBitcrusherBlockDescriptor);
    block->setParam(kCrushParamBits, 2.0f); // steps of 0.5
    block->setParam(kCrushParamRate, 6000.0f); // hold each value for 8 samples
    block->setParam(kCrushParamTone, 20000.0f);
    auto x = sine(100.0, 0.9f, 4800);
    testing::runBlock(*block, x);
    size_t lastChange = 0;
    int changes = 0;
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        REQUIRE(std::fabs(x[0][i] * 2.0f - std::round(x[0][i] * 2.0f)) < 1e-6f);
        if (i > 0 && x[0][i] != x[0][i - 1])
        {
            if (changes++ > 0)
                REQUIRE(i - lastChange >= 8);
            lastChange = i;
        }
    }
    CHECK(changes > 10);
}

TEST_CASE("rotary speeds up gradually and its mix at zero is dry")
{
    auto block = make(kRotaryBlockDescriptor);
    block->setParam(kRotaryParamWidth, 0.0f);
    auto x = sine(3000.0, 0.3f, 48000 * 6);
    // a little settle at slow, then switch to fast
    std::vector<std::vector<float>> first(1, std::vector<float>(x[0].begin(), x[0].begin() + 4800));
    testing::runBlock(*block, first);
    block->setParam(kRotaryParamSpeed, 1.0f);
    testing::runBlock(*block, x);

    // count the horn's level swings per second, early and late
    auto swings = [&](size_t from) {
        const auto env = envelope(std::vector<float>(x[0].begin() + from, x[0].begin() + from + 48000), 240);
        double mean = 0.0;
        for (double e : env)
            mean += e;
        mean /= static_cast<double>(env.size());
        int crossings = 0;
        for (size_t w = 1; w < env.size(); ++w)
            if (env[w - 1] < mean && env[w] >= mean)
                ++crossings;
        return crossings;
    };
    const int early = swings(0);
    const int late = swings(48000 * 5);
    CHECK(early < late);
    CHECK(late == doctest::Approx(6.7).epsilon(0.16));

    block->setParam(kRotaryParamMix, 0.0f);
    auto dry = sine(500.0, 0.3f, 24000);
    const auto reference = dry;
    testing::runBlock(*block, dry);
    CHECK(dry[0][23999] == doctest::Approx(reference[0][23999]).epsilon(1e-4));
}

// ---------------------------------------------------------------------------------------------------------------------
// Pitch, reverbs and echoes

TEST_CASE("pitch shifter moves a tone by its interval")
{
    for (const float shift : { 12.0f, -12.0f, 7.0f })
    {
        CAPTURE(shift);
        auto block = make(kPitchShifterBlockDescriptor);
        block->setParam(kPitchParamShift1, shift);
        block->setParam(kPitchParamDry, 0.0f);
        auto x = sine(440.0, 0.3f, 48000);
        testing::runBlock(*block, x);
        const double target = 440.0 * std::exp2(shift / 12.0);
        CHECK(bandAt(x[0], target, 24000, 48000) > 4.0 * bandAt(x[0], 440.0, 24000, 48000));
        CHECK(testing::rms(x[0], 24000, 48000) == doctest::Approx(0.3 / std::sqrt(2.0)).epsilon(0.1));
    }
}

TEST_CASE("whammy bends from no shift to its interval")
{
    auto block = make(kWhammyBlockDescriptor); // +1 Oct
    auto x = sine(440.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    CHECK(toneAt(x[0], 440.0, 24000, 48000) > 0.25);
    block->setParam(kWhammyParamPosition, 100.0f);
    auto y = sine(440.0, 0.3f, 48000);
    testing::runBlock(*block, y);
    CHECK(bandAt(y[0], 880.0, 24000, 48000) > 4.0 * bandAt(y[0], 440.0, 24000, 48000));
}

TEST_CASE("poly octave adds an octave down")
{
    auto block = make(kPolyOctaveBlockDescriptor);
    block->setParam(kPolyOctParamDry, 0.0f);
    block->setParam(kPolyOctParamSub1, 100.0f);
    auto x = sine(440.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    CHECK(bandAt(x[0], 220.0, 24000, 48000) > 4.0 * bandAt(x[0], 440.0, 24000, 48000));
}

TEST_CASE("spring reverb decays at about its set time")
{
    auto block = make(kSpringReverbBlockDescriptor);
    block->setParam(kSpringParamMix, 100.0f);
    block->setParam(kSpringParamDecay, 2.0f);
    std::vector<std::vector<float>> x(1, std::vector<float>(48000 * 3, 0.0f));
    for (size_t i = 0; i < 480; ++i)
        x[0][i] = 0.5f * static_cast<float>(std::sin(i * 0.3));
    testing::runBlock(*block, x);
    const double early = testing::rms(x[0], 24000, 36000);
    const double late = testing::rms(x[0], 72000, 84000);
    // 2 s RT60: 30 dB per second, one second apart; the damping takes the highs down faster
    const double drop = 20.0 * std::log10(early / late);
    CHECK(drop > 20.0);
    CHECK(drop < 50.0);
}

TEST_CASE("shimmer's tail climbs an octave")
{
    auto run = [](float shimmer) {
        auto block = make(kShimmerBlockDescriptor);
        block->setParam(kShimmerParamShimmer, shimmer);
        block->setParam(kShimmerParamMix, 100.0f);
        block->setParam(kShimmerParamTone, 16000.0f);
        auto x = sine(440.0, 0.3f, 48000 * 4);
        std::fill(x[0].begin() + 48000, x[0].end(), 0.0f);
        testing::runBlock(*block, x);
        return bandAt(x[0], 880.0, 48000 * 3, 48000 * 4) / bandAt(x[0], 440.0, 48000 * 3, 48000 * 4);
    };
    CHECK(run(100.0f) > 10.0 * run(0.0f));
}

TEST_CASE("freeze holds the sound until it's turned off")
{
    auto block = make(kFreezeBlockDescriptor);
    block->setParam(kFreezeParamFadeOut, 500.0f);
    auto x = sine(440.0, 0.3f, 48000);
    testing::runBlock(*block, x); // the tank listens
    block->setParam(kFreezeParamFreeze, 1.0f);
    std::vector<std::vector<float>> held(1, std::vector<float>(48000 * 6, 0.0f));
    testing::runBlock(*block, held);
    const double start = testing::rms(held[0], 48000, 96000);
    const double end = testing::rms(held[0], 48000 * 5, 48000 * 6);
    CHECK(start > 0.01);
    CHECK(end > 0.8 * start);

    block->setParam(kFreezeParamFreeze, 0.0f);
    std::vector<std::vector<float>> after(1, std::vector<float>(48000 * 2, 0.0f));
    testing::runBlock(*block, after);
    CHECK(testing::rms(after[0], 48000, 96000) < 0.001 * start);
}

TEST_CASE("tape echo plays its heads at 1x, 2x and 3x the time")
{
    auto block = make(kTapeEchoBlockDescriptor);
    block->setParam(kTapeParamTime, 100.0f);
    block->setParam(kTapeParamWow, 0.0f);
    block->setParam(kTapeParamFeedback, 0.0f);
    block->setParam(kTapeParamTone, 20000.0f);
    block->setParam(kTapeParamHeads, 6.0f); // 1+2+3
    std::vector<std::vector<float>> x(1, std::vector<float>(19200, 0.0f));
    x[0][0] = 0.5f;
    testing::runBlock(*block, x);
    for (const size_t at : { 4800u, 9600u, 14400u })
    {
        CAPTURE(at);
        double around = 0.0;
        for (size_t i = at - 10; i < at + 10; ++i)
            around += std::fabs(x[0][i]);
        CHECK(around > 0.1);
    }
    double between = 0.0;
    for (size_t i = 6000; i < 8400; ++i)
        between += std::fabs(x[0][i]);
    CHECK(between < 0.05);
}

TEST_CASE("ducking delay turns the repeats down while playing")
{
    auto block = make(kDuckingDelayBlockDescriptor);
    block->setParam(kDuckParamTime, 400.0f);
    block->setParam(kDuckParamMix, 100.0f); // wet only
    block->setParam(kDuckParamFeedback, 0.0f);
    block->setParam(kDuckParamThreshold, -40.0f);
    block->setParam(kDuckParamDuck, 20.0f);
    block->setParam(kDuckParamRelease, 50.0f);
    auto x = sine(440.0, 0.3f, 48000);
    std::fill(x[0].begin() + 24000, x[0].end(), 0.0f);
    testing::runBlock(*block, x, 128);
    // the echo starts at 400 ms while the note still plays: 20 dB down
    const double ducked = testing::rms(x[0], 20000, 24000);
    CHECK(block->meter() < 0.1f);
    // from 250 ms after the note stops, the echo is free
    const double free = testing::rms(x[0], 24000 + 12000, 24000 + 19000);
    CHECK(20.0 * std::log10(free / ducked) == doctest::Approx(20.0).epsilon(0.1));
}

TEST_CASE("synth follows the played note")
{
    for (const double hz : { 82.4, 196.0, 440.0 })
    {
        CAPTURE(hz);
        auto block = make(kSynthBlockDescriptor);
        block->setParam(kSynthParamWave, static_cast<float>(SynthWave::Square));
        block->setParam(kSynthParamCutoff, 8000.0f);
        block->setParam(kSynthParamResonance, 0.7f);
        // a guitar-like tone: fundamental plus strong harmonics
        std::vector<std::vector<float>> x(1, std::vector<float>(48000));
        for (size_t i = 0; i < x[0].size(); ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            x[0][i] = static_cast<float>(0.2 * std::sin(2 * M_PI * hz * t) + 0.15 * std::sin(2 * M_PI * 2 * hz * t + 1.0)
                                         + 0.1 * std::sin(2 * M_PI * 3 * hz * t + 2.0));
        }
        testing::runBlock(*block, x);
        // a square at the note: strong fundamental and third harmonic, no second
        const double fundamental = toneAt(x[0], hz, 24000, 48000);
        CHECK(fundamental > 0.05);
        CHECK(toneAt(x[0], 3 * hz, 24000, 48000) > 0.2 * fundamental);
        CHECK(toneAt(x[0], 2 * hz, 24000, 48000) < 0.05 * fundamental);
    }
}

TEST_CASE("synth octave and silence")
{
    auto block = make(kSynthBlockDescriptor);
    block->setParam(kSynthParamOctave, 1.0f); // -1
    block->setParam(kSynthParamCutoff, 8000.0f);
    auto x = sine(220.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    CHECK(toneAt(x[0], 110.0, 24000, 48000) > 0.05);

    std::vector<std::vector<float>> quiet(1, std::vector<float>(48000, 0.0f));
    testing::runBlock(*block, quiet);
    CHECK(testing::rms(quiet[0], 24000, 48000) < 1e-4);
}

// ---------------------------------------------------------------------------------------------------------------------
// Looper

namespace {

void press(Block& block, int param)
{
    block.setParam(param, 1.0f);
    block.setParam(param, 0.0f);
}

LooperStatus looperStatus(const Block& block)
{
    return LooperStatus::decode(block.meter());
}

} // namespace

TEST_CASE("looper status survives the trip through a float")
{
    LooperStatus s;
    s.state = LooperState::Overdubbing;
    s.lengthTenths = 600;
    s.progress = 99;
    const LooperStatus back = LooperStatus::decode(s.encode());
    CHECK(back.state == s.state);
    CHECK(back.lengthTenths == 600);
    CHECK(back.progress == 99);
}

TEST_CASE("looper records, plays back, overdubs, stops and clears")
{
    auto block = make(kLooperBlockDescriptor);
    block->setParam(kLooperParamLatency, 0.0f); // no round trip to make up for (the latency tests below)
    // record 0.5 s of a tone
    press(*block, kLooperParamRecDub);
    auto take = sine(440.0, 0.3f, 24000);
    int allocations = 0;
    testing::runBlock(*block, take, 128, &allocations);
    CHECK(looperStatus(*block).state == LooperState::Recording);
    press(*block, kLooperParamRecDub);

    // silence in: the loop plays the tone back, over and over
    std::vector<std::vector<float>> out(1, std::vector<float>(48000, 0.0f));
    testing::runBlock(*block, out, 128, &allocations);
    CHECK(allocations == 0);
    CHECK(looperStatus(*block).state == LooperState::Playing);
    CHECK(looperStatus(*block).lengthTenths == 5);
    CHECK(testing::rms(out[0], 1000, 23000) == doctest::Approx(0.3 / std::sqrt(2.0)).epsilon(0.02));
    CHECK(testing::rms(out[0], 25000, 47000) == doctest::Approx(0.3 / std::sqrt(2.0)).epsilon(0.02));
    // the loop is sample-exact: the second pass matches the first
    CHECK(out[0][30000] == doctest::Approx(out[0][6000]).epsilon(1e-5));

    // overdub the same tone in phase: twice as loud afterwards
    press(*block, kLooperParamRecDub);
    auto dub = sine(440.0, 0.3f, 24000);
    testing::runBlock(*block, dub);
    CHECK(looperStatus(*block).state == LooperState::Overdubbing);
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> twice(1, std::vector<float>(24000, 0.0f));
    testing::runBlock(*block, twice);
    CHECK(testing::rms(twice[0], 1000, 23000) == doctest::Approx(0.6 / std::sqrt(2.0)).epsilon(0.03));

    // stop: silent (after the fade); play again
    press(*block, kLooperParamPlayStop);
    std::vector<std::vector<float>> stopped(1, std::vector<float>(4800, 0.0f));
    testing::runBlock(*block, stopped);
    CHECK(looperStatus(*block).state == LooperState::Stopped);
    CHECK(testing::rms(stopped[0], 2400, 4800) < 1e-4);
    press(*block, kLooperParamPlayStop);
    std::vector<std::vector<float>> again(1, std::vector<float>(4800, 0.0f));
    testing::runBlock(*block, again);
    CHECK(testing::rms(again[0], 2400, 4800) > 0.3);

    // clear
    press(*block, kLooperParamClear);
    std::vector<std::vector<float>> cleared(1, std::vector<float>(4800, 0.0f));
    testing::runBlock(*block, cleared);
    CHECK(looperStatus(*block).state == LooperState::Empty);
    CHECK(testing::rms(cleared[0], 0, 4800) == 0.0);
}

TEST_CASE("looper buttons act on presses, not releases")
{
    auto block = make(kLooperBlockDescriptor);
    block->setParam(kLooperParamRecDub, 1.0f);
    block->setParam(kLooperParamRecDub, 1.0f); // held: still one press
    std::vector<std::vector<float>> x(1, std::vector<float>(4800, 0.1f));
    testing::runBlock(*block, x);
    CHECK(looperStatus(*block).state == LooperState::Recording);
    block->setParam(kLooperParamRecDub, 0.0f); // release: nothing
    testing::runBlock(*block, x);
    CHECK(looperStatus(*block).state == LooperState::Recording);
}

TEST_CASE("looper blocks saved before Count In stay in Free mode")
{
    // no "countIn" in the config (createWithDefaults() configures an empty one): recording starts at once
    auto block = make(kLooperBlockDescriptor);
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> x(1, std::vector<float>(4800, 0.1f));
    testing::runBlock(*block, x);
    CHECK(looperStatus(*block).state == LooperState::Recording);
}

namespace {

// A looper made since Count In existed, in Count In mode at 120 BPM (0.5 s a beat) in 4/4
std::unique_ptr<Block> countInLooper(int countIn, int measures, int loops)
{
    auto block = make(kLooperBlockDescriptor);
    block->configure({ { kLooperCountInKey, true } });
    block->setParam(kLooperParamMode, 0.0f);
    block->setParam(kLooperParamBpm, 120.0f);
    block->setParam(kLooperParamBeats, 4.0f);
    block->setParam(kLooperParamCountIn, static_cast<float>(countIn));
    block->setParam(kLooperParamMeasures, static_cast<float>(measures));
    block->setParam(kLooperParamLoops, static_cast<float>(loops));
    block->setParam(kLooperParamClick, 100.0f);
    block->setParam(kLooperParamLatency, 0.0f);
    return block;
}

// Where the clicks start: the first sample of each burst after silence
std::vector<size_t> clickStarts(const std::vector<float>& x)
{
    std::vector<size_t> starts;
    size_t quiet = 1000000;
    for (size_t i = 0; i < x.size(); ++i)
    {
        if (std::abs(x[i]) > 1e-4f)
        {
            if (quiet > 4800)
                starts.push_back(i);
            quiet = 0;
        }
        else
            ++quiet;
    }
    return starts;
}

} // namespace

TEST_CASE("looper count in: clicks four, records the bars with the click, then plays without it")
{
    CHECK(kLooperBlockDescriptor.params[kLooperParamMode].def == 0.0f); // Count In is the default (owner)
    auto block = countInLooper(4, 1, 1);
    press(*block, kLooperParamRecDub);

    // silence in: only the clicks come out, on every beat (24000 samples) from the press, 4 counting in + 4 recorded
    std::vector<std::vector<float>> first(1, std::vector<float>(24000 * 4 - 100, 0.0f));
    int allocations = 0;
    testing::runBlock(*block, first, 128, &allocations);
    CHECK(looperStatus(*block).state == LooperState::CountIn);
    CHECK(looperStatus(*block).progress == 4);
    std::vector<std::vector<float>> second(1, std::vector<float>(24000 * 4 - 200, 0.0f));
    testing::runBlock(*block, second, 128, &allocations);
    CHECK(looperStatus(*block).state == LooperState::Recording);
    std::vector<std::vector<float>> third(1, std::vector<float>(24000 * 4 + 300, 0.0f));
    testing::runBlock(*block, third, 128, &allocations);
    CHECK(allocations == 0);
    // the loop closed itself after one bar: playing, and the clicks never went into it
    CHECK(looperStatus(*block).state == LooperState::Playing);
    CHECK(looperStatus(*block).lengthTenths == 20);

    std::vector<float> all = first[0];
    all.insert(all.end(), second[0].begin(), second[0].end());
    all.insert(all.end(), third[0].begin(), third[0].end());
    const std::vector<size_t> starts = clickStarts(all);
    REQUIRE(starts.size() == 8);
    for (size_t k = 0; k < starts.size(); ++k)
    {
        // (a click's first sample is 0: it fades in)
        CHECK(starts[k] >= k * 24000);
        CHECK(starts[k] <= k * 24000 + 2);
    }
    // the accents: the first count-in click and the recording's first beat
    const auto peak = [&all](size_t from) {
        float p = 0.0f;
        for (size_t i = from; i < from + 2400; ++i)
            p = std::max(p, std::abs(all[i]));
        return p;
    };
    CHECK(peak(0) > peak(24000) * 1.1f);
    CHECK(peak(4 * 24000) > peak(5 * 24000) * 1.1f);
    CHECK(testing::rms(all, 8 * 24000, all.size()) == 0.0);
}

TEST_CASE("looper count in records only during the bars, and overdubs the extra loops")
{
    auto block = countInLooper(2, 1, 2);
    block->setParam(kLooperParamClick, 0.0f);
    press(*block, kLooperParamRecDub);
    // count-in (1 s): the tone isn't recorded; then the bar (2 s) is, and a second pass (2 s) overdubs it
    auto tone = sine(440.0, 0.2f, 48000 * 5);
    testing::runBlock(*block, tone);
    CHECK(looperStatus(*block).state == LooperState::Overdubbing);
    CHECK(looperStatus(*block).lengthTenths == 20);

    // played back: the tone twice over (in phase: a whole number of cycles a beat)
    std::vector<std::vector<float>> out(1, std::vector<float>(96000, 0.0f));
    testing::runBlock(*block, out);
    CHECK(looperStatus(*block).state == LooperState::Playing);
    CHECK(testing::rms(out[0], 2000, 90000) == doctest::Approx(0.4 / std::sqrt(2.0)).epsilon(0.03));
}

TEST_CASE("looper count in: a second press calls it off, and Free mode records at once")
{
    auto block = countInLooper(4, 1, 1);
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> x(1, std::vector<float>(4800, 0.0f));
    testing::runBlock(*block, x);
    CHECK(looperStatus(*block).state == LooperState::CountIn);
    press(*block, kLooperParamRecDub);
    testing::runBlock(*block, x);
    CHECK(looperStatus(*block).state == LooperState::Empty);

    block->setParam(kLooperParamMode, 1.0f);
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> quiet(1, std::vector<float>(48000, 0.0f));
    testing::runBlock(*block, quiet);
    CHECK(looperStatus(*block).state == LooperState::Recording);
    CHECK(testing::rms(quiet[0], 0, 48000) == 0.0); // no click
}

TEST_CASE("looper defaults: two bars of count-in, a loud click")
{
    // owner, 2026-10-31: 8 clicks (two bars of 4/4), and a click loud enough to hear while playing
    CHECK(kLooperBlockDescriptor.params[kLooperParamCountIn].def == 8.0f);
    CHECK(kLooperBlockDescriptor.params[kLooperParamClick].def >= 100.0f);
    CHECK(kLooperBlockDescriptor.params[kLooperParamClick].max == 200.0f);
}

TEST_CASE("looper Tap sets the count-in tempo")
{
    // taps every 0.4 s: 150 BPM, so one bar of 4 is 1.6 s
    auto block = countInLooper(0, 1, 1);
    block->setParam(kLooperParamClick, 0.0f);
    std::vector<std::vector<float>> gap(1, std::vector<float>(19200, 0.0f));
    for (int t = 0; t < 4; ++t)
    {
        press(*block, kLooperParamTap);
        testing::runBlock(*block, gap);
    }
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> bar(1, std::vector<float>(48000 * 2, 0.0f));
    testing::runBlock(*block, bar);
    CHECK(looperStatus(*block).state == LooperState::Playing);
    CHECK(looperStatus(*block).lengthTenths == 16);
}

TEST_CASE("looper latency: a count-in loop played a round trip late comes round on the clicks")
{
    // 10 ms (480 samples) round trip: the player hears each click and their note arrives 480 samples after it
    CHECK(kLooperBlockDescriptor.params[kLooperParamLatency].def == 10.0f);
    auto block = countInLooper(4, 1, 1);
    block->setParam(kLooperParamClick, 0.0f);
    block->setParam(kLooperParamLatency, 10.0f);
    press(*block, kLooperParamRecDub);

    // a note on each beat of the recorded bar (beats 4..7 from the press), and on the next downbeat (8), late by the
    // round trip; a held tone under them, so the seam has something to cut
    constexpr size_t beat = 24000, late = 480;
    std::vector<std::vector<float>> in(1, std::vector<float>(beat * 8 + late, 0.0f));
    for (size_t t = beat * 4; t < in[0].size(); ++t)
        in[0][t] = 0.1f;
    for (size_t k = 4; k < 8; ++k)
        in[0][k * beat + late] = 0.6f;
    int allocations = 0;
    testing::runBlock(*block, in, 128, &allocations);
    CHECK(looperStatus(*block).state == LooperState::Playing);
    CHECK(looperStatus(*block).lengthTenths == 20);

    // from here on silence in: the notes come back exactly on the grid (beats 9, 10, 11 and 12), at full strength
    std::vector<std::vector<float>> out(1, std::vector<float>(beat * 4 + 3000, 0.0f));
    testing::runBlock(*block, out, 128, &allocations);
    CHECK(allocations == 0);
    const size_t start = beat * 8 + late; // where `out` starts
    for (size_t k = 9; k <= 12; ++k)
    {
        const size_t at = k * beat - start;
        CHECK(out[0][at] == doctest::Approx(0.6f).epsilon(0.01));
        CHECK(std::abs(out[0][at - 1]) < 0.2f);
        CHECK(std::abs(out[0][at + 1]) < 0.2f);
    }
    // the held tone has no gap or click at the seam (beat 12, where the loop starts again)
    float worst = 0.0f;
    for (size_t t = 12 * beat - start - 2000; t < 12 * beat - start + 2000; ++t)
        if (t != 12 * beat - start)
            worst = std::max(worst, std::abs(out[0][t] - 0.1f));
    CHECK(worst < 0.02f);
}

TEST_CASE("looper latency: the first repeat comes round on the downbeat, attack and all")
{
    // owner, 2026-10-31: a pause before the first repeat. The loop waited for the round trip after the bar line and
    // then played on from that far in, so the first repeat's downbeat lost its attack.
    auto block = countInLooper(4, 1, 1);
    block->setParam(kLooperParamClick, 0.0f);
    block->setParam(kLooperParamLatency, 10.0f); // 480 samples
    press(*block, kLooperParamRecDub);
    constexpr size_t beat = 24000, late = 480;
    // a sharp note on the recorded bar's downbeat (beat 4 from the press), a round trip late; a held tone to the end
    // of the bar, its last round trip arriving after the next downbeat
    std::vector<std::vector<float>> in(1, std::vector<float>(beat * 8 + late, 0.0f));
    for (size_t t = beat * 4; t < in[0].size(); ++t)
        in[0][t] = 0.1f;
    in[0][beat * 4 + late] = 0.6f;
    // up to the next downbeat (beat 8)
    std::vector<std::vector<float>> before(1, std::vector<float>(in[0].begin(), in[0].begin() + beat * 8));
    testing::runBlock(*block, before, 128);
    CHECK(looperStatus(*block).state == LooperState::Recording);
    std::vector<std::vector<float>> after(1, std::vector<float>(in[0].begin() + beat * 8, in[0].end()));
    after[0].resize(beat * 4 + 1000, 0.0f);
    int allocations = 0;
    testing::runBlock(*block, after, 128, &allocations);
    CHECK(allocations == 0);
    CHECK(looperStatus(*block).state == LooperState::Playing);
    // the loop came round exactly on beat 8 with the downbeat (0.6) and played the held tone after it
    CHECK(after[0][0] == doctest::Approx(0.6f + 0.1f).epsilon(0.01)); // and the live tone's last moment
    CHECK(after[0][100] == doctest::Approx(0.1f + 0.1f).epsilon(0.01)); // the loop's tone plus the live one
    // the bar's last round trip, which arrived after the loop came round, is in the loop's end: the tone runs on into
    // the seam at beat 12 with no gap, and the downbeat comes round again on it
    float worst = 0.0f;
    for (size_t t = beat * 4 - 2000; t < beat * 4; ++t)
        worst = std::max(worst, std::abs(after[0][t] - 0.1f));
    CHECK(worst < 0.02f);
    CHECK(after[0][beat * 4] == doctest::Approx(0.6f).epsilon(0.01));
}

TEST_CASE("looper latency: overdubs land where the playback was a round trip earlier")
{
    auto block = make(kLooperBlockDescriptor); // Free mode
    block->setParam(kLooperParamLatency, 10.0f); // 480 samples
    press(*block, kLooperParamRecDub);
    std::vector<std::vector<float>> silence(1, std::vector<float>(24000, 0.0f));
    testing::runBlock(*block, silence);
    press(*block, kLooperParamRecDub); // closed: playing from the start
    press(*block, kLooperParamRecDub); // overdub
    std::vector<std::vector<float>> dub(1, std::vector<float>(24000, 0.0f));
    dub[0][5000] = 0.5f;
    testing::runBlock(*block, dub);
    press(*block, kLooperParamRecDub); // back to playing, at the loop's start again
    std::vector<std::vector<float>> out(1, std::vector<float>(24000, 0.0f));
    testing::runBlock(*block, out);
    CHECK(out[0][4520] == doctest::Approx(0.5f));
    CHECK(std::abs(out[0][5000]) < 1e-6f);
}

TEST_CASE("TapTempo averages the taps and starts over after a pause")
{
    TapTempo taps;
    CHECK(taps.tap(10.0) == 0.0);
    CHECK(taps.tap(10.5) == doctest::Approx(120.0));
    CHECK(taps.tap(11.0) == doctest::Approx(120.0));
    CHECK(taps.tap(11.6) == doctest::Approx(60.0 / (1.6 / 3.0)));
    CHECK(taps.tap(20.0) == 0.0); // a pause: starts over
    CHECK(taps.tap(21.0) == doctest::Approx(60.0));
}

TEST_CASE("looper clicks go to the engine's click bus when it gives one")
{
    auto block = countInLooper(4, 1, 1);
    std::vector<float> click(128, 0.0f);
    std::vector<float> io(128, 0.0f);
    float* channels[1] = { io.data() };
    press(*block, kLooperParamRecDub);
    block->setClickOutput(click.data());
    block->process(channels, 1, 128);
    float peak = 0.0f;
    for (float v : click)
        peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.01f);
    CHECK(testing::rms(io, 0, io.size()) == 0.0);
}

TEST_CASE("harmonizer intervals follow the scale")
{
    // C major (key 0, scale 0): a third above C (60) is E (+4), above D (62) is F (+3), above B (71) is D (+3)
    CHECK(harmonySemitones(60, 0, 0, 2) == 4);
    CHECK(harmonySemitones(62, 0, 0, 2) == 3);
    CHECK(harmonySemitones(71, 0, 0, 2) == 3);
    // a fifth above B in C major is F (a diminished fifth: +6); an octave is 12; a third below E is C (-4)
    CHECK(harmonySemitones(71, 0, 0, 4) == 6);
    CHECK(harmonySemitones(64, 0, 0, 7) == 12);
    CHECK(harmonySemitones(64, 0, 0, -2) == -4);
    CHECK(harmonySemitones(60, 0, 0, -7) == -12);
    // A minor (key 9, scale 1): a third above A is C (+3)
    CHECK(harmonySemitones(69, 9, 1, 2) == 3);
    // a note outside the scale (C# in C major) takes C's interval
    CHECK(harmonySemitones(61, 0, 0, 2) == 4);
}

TEST_CASE("harmonizer adds a diatonic third")
{
    auto block = make(kHarmonizerBlockDescriptor); // C major, a third above
    block->setParam(kHarmParamDry, 0.0f);
    // D (293.66 Hz): a minor third above is F (349.23 Hz)
    auto x = sine(293.66, 0.3f, 48000);
    testing::runBlock(*block, x);
    CHECK(bandAt(x[0], 349.23, 24000, 48000) > 4.0 * bandAt(x[0], 293.66, 24000, 48000));
    // E (329.63 Hz): a minor third above is G (392 Hz)
    auto e = sine(329.63, 0.3f, 48000);
    testing::runBlock(*block, e);
    CHECK(bandAt(e[0], 392.0, 24000, 48000) > 4.0 * bandAt(e[0], 329.63, 24000, 48000));
}

TEST_CASE("filter types, and its sweep")
{
    auto block = make(kFilterBlockDescriptor);
    block->setParam(kFilterParamDepth, 0.0f);
    block->setParam(kFilterParamCutoff, 1000.0f);
    block->setParam(kFilterParamResonance, 0.7071f);
    CHECK(sineGainDb(*block, 100.0) == doctest::Approx(0.0).epsilon(0.05));
    CHECK(sineGainDb(*block, 8000.0) < -30.0);
    block->setParam(kFilterParamType, 2.0f); // high-pass
    CHECK(sineGainDb(*block, 100.0) < -30.0);
    block->setParam(kFilterParamType, 3.0f); // notch
    CHECK(sineGainDb(*block, 1000.0) < -30.0);
    CHECK(std::fabs(sineGainDb(*block, 100.0)) < 0.5);

    // swept low-pass: a high tone comes and goes
    block->setParam(kFilterParamType, 0.0f);
    block->setParam(kFilterParamDepth, 100.0f);
    block->setParam(kFilterParamRate, 2.0f);
    auto x = sine(3000.0, 0.3f, 48000);
    testing::runBlock(*block, x);
    const auto env = envelope(x[0], 480);
    CHECK(*std::max_element(env.begin(), env.end()) > 10.0 * *std::min_element(env.begin(), env.end()));
}

TEST_CASE("pitch shifting a single note is clean (pitch-synchronous splices)")
{
    // a guitar-like note with six harmonics, a fifth up: nearly all the output energy is at the shifted harmonics
    for (const double hz : { 82.4, 196.0, 329.6 })
    {
        CAPTURE(hz);
        auto block = make(kPitchShifterBlockDescriptor);
        block->setParam(kPitchParamShift1, 7.0f);
        block->setParam(kPitchParamDry, 0.0f);
        std::vector<std::vector<float>> x(1, std::vector<float>(96000));
        for (size_t i = 0; i < x[0].size(); ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 6; ++h)
                v += 0.15 / h * std::sin(2.0 * M_PI * hz * h * static_cast<double>(i) / kRate + h);
            x[0][i] = static_cast<float>(v);
        }
        testing::runBlock(*block, x);
        const double total = testing::rms(x[0], 48000, 96000);
        double shifted = 0.0;
        for (int h = 1; h <= 6; ++h)
        {
            const double a = toneAt(x[0], hz * std::exp2(7.0 / 12.0) * h, 48000, 96000);
            shifted += a * a / 2.0;
        }
        CHECK(shifted / (total * total) > 0.95);
    }
}

TEST_CASE("poly octave shifts chords cleanly too")
{
    // an open E chord, an octave down: nearly all the energy within 25 cents of the shifted partials, at the
    // original level
    const double notes[] = { 82.41, 123.47, 164.81, 207.65, 246.94, 329.63 };
    auto block = make(kPolyOctaveBlockDescriptor);
    block->setParam(kPolyOctParamDry, 0.0f);
    block->setParam(kPolyOctParamSub1, 100.0f);
    std::vector<std::vector<float>> x(1, std::vector<float>(96000));
    for (size_t i = 0; i < x[0].size(); ++i)
    {
        double v = 0.0;
        for (const double hz : notes)
            for (int h = 1; h <= 4; ++h)
                v += 0.1 / h * std::sin(2.0 * M_PI * hz * h * static_cast<double>(i) / kRate + h);
        x[0][i] = static_cast<float>(v);
    }
    const double inputRms = testing::rms(x[0], 48000, 96000);
    testing::runBlock(*block, x);
    const double total = testing::rms(x[0], 48000, 96000);
    CHECK(total == doctest::Approx(inputRms).epsilon(0.1));
    // energy near the shifted partials: 1 Hz steps over a Hann-windowed DFT overcount a tone by 1.5
    std::vector<bool> taken(2000, false);
    double near = 0.0;
    for (const double hz : notes)
    {
        for (int h = 1; h <= 4; ++h)
        {
            const double g = hz * h * 0.5;
            for (int f = static_cast<int>(g * 0.985); f <= static_cast<int>(g * 1.015) + 1; ++f)
            {
                if (taken[static_cast<size_t>(f)])
                    continue;
                taken[static_cast<size_t>(f)] = true;
                const double a = toneAt(x[0], f, 48000, 96000);
                near += a * a / 2.0 / 1.5;
            }
        }
    }
    CHECK(near / (total * total) > 0.9);
}

// ---------------------------------------------------------------------------------------------------------------------
// Pitch tracking on plucked strings

namespace {

// A Karplus-Strong pluck (picked at a fifth of the string). Its true pitch is rate / (length + 0.5).
std::vector<float> pluck(double nominalHz, size_t length, uint32_t seed, double* trueHz)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const auto period = static_cast<size_t>(std::lround(kRate / nominalHz));
    *trueHz = kRate / (static_cast<double>(period) + 0.5);
    std::vector<float> string(period);
    for (float& v : string)
        v = noise(rng);
    const size_t pick = period / 5;
    std::vector<float> picked(period);
    for (size_t i = 0; i < period; ++i)
        picked[i] = string[i] - string[(i + period - pick) % period];
    std::vector<float> out(length);
    size_t p = 0;
    float previous = 0.0f;
    for (size_t i = 0; i < length; ++i)
    {
        const float v = picked[p];
        picked[p] = 0.996f * 0.5f * (v + previous);
        previous = v;
        p = (p + 1) % period;
        out[i] = 0.3f * v;
    }
    return out;
}

} // namespace

TEST_CASE("pitch tracker follows plucked strings to a fraction of a cent")
{
    for (const double nominal : { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26 })
    {
        double hz = 0.0;
        const auto x = pluck(nominal, 48000, static_cast<uint32_t>(nominal), &hz);
        CAPTURE(hz);
        PitchTracker tracker;
        tracker.prepare(kRate);
        int found = -1;
        double worst = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            tracker.process(x[i]);
            const double cents = tracker.hz() > 0.0 ? 1200.0 * std::log2(tracker.hz() / hz) : 1e9;
            if (found < 0 && std::fabs(cents) < 10.0)
                found = static_cast<int>(i);
            if (i > 4800 && tracker.sounding())
                worst = std::max(worst, std::fabs(cents));
        }
        CHECK(found >= 0);
        CHECK(found < 1500); // within about 30 ms
        CHECK(worst < 1.0);
    }
}

TEST_CASE("synth plays the plucked note")
{
    double hz = 0.0;
    auto x = std::vector<std::vector<float>>(1, pluck(110.0, 24000, 3, &hz));
    auto block = make(kSynthBlockDescriptor);
    block->setParam(kSynthParamWave, static_cast<float>(SynthWave::Square));
    block->setParam(kSynthParamCutoff, 8000.0f);
    block->setParam(kSynthParamResonance, 0.7f);
    testing::runBlock(*block, x);
    const double fundamental = toneAt(x[0], hz, 4800, 14400);
    CHECK(fundamental > 3.0 * toneAt(x[0], 2.0 * hz, 4800, 14400));
    CHECK(toneAt(x[0], 3.0 * hz, 4800, 14400) > 0.2 * fundamental); // a square's third harmonic
}

TEST_CASE("rate sync goes down to four bars")
{
    CHECK(std::string(rateNoteName(rateNoteIndex(0.0f))) == "4 bars");
    CHECK(rateNoteHz(rateNoteIndex(0.0f), 120.0) == doctest::Approx(0.125));
    CHECK(std::string(rateNoteName(rateNoteIndex(1.0f))) == "1/32");
    CHECK(rateNoteHz(rateNoteIndex(1.0f), 120.0) == doctest::Approx(16.0));
}

TEST_CASE("synced tremolo locks to the song position")
{
    auto block = make(kTremoloBlockDescriptor);
    const ParamSpec& rate = kTremoloBlockDescriptor.params[kTremParamRate];
    float n = 0.0f;
    while (std::string(rateNoteName(rateNoteIndex(n))) != "1/4")
        n += 0.01f;
    block->setParam(kTremParamRate, rate.toValue(n));
    block->setParam(kTremParamSync, 1.0f);
    block->setParam(kTremParamDepth, 100.0f);
    block->setTempo(120.0);
    // the song starts 0.3 beats in; feed the position before each buffer, as the engine does
    auto x = sine(3000.0, 0.5f, 48000);
    const double beatsPerSample = 120.0 / 60.0 / kRate;
    for (size_t t = 0; t < x[0].size(); t += 128)
    {
        block->setSongPosition(0.3 + static_cast<double>(t) * beatsPerSample);
        float* io[1] = { x[0].data() + t };
        block->process(io, 1, 128);
    }
    // loudest on the beats: beat 1 is at (1 - 0.3) beats = 0.35 s = sample 16800, then every 24000
    const auto env = envelope(x[0], 240);
    const size_t peak = std::max_element(env.begin() + 40, env.begin() + 140) - env.begin(); // 0.2..0.7 s
    CHECK(std::abs(static_cast<int>(peak * 240 + 120) - 16800) < 480);
}
