// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "ParamHelp.hpp"

#include "engine/Params.hpp"

#include <cstring>

namespace xythos {

namespace {

struct BlockHelp {
    const char* typeId;
    const char* param;
    const char* help;
};

// Shared wording for controls many blocks have
#define MIX_HELP(what)                                                                                                \
    "Blends the " what " with your dry signal. 0% is dry only and 100% is the effect only; around 50% the two are "   \
    "equal. Lower it for a subtle touch, raise it for a bigger, more obvious effect."
#define SYNC_HELP(knob)                                                                                               \
    "Locks the " knob " to your DAW's tempo. When it's on, the " knob " knob picks a note value (like 1/4 or a "      \
    "dotted 1/8) instead of a free value, and follows tempo changes. Without a DAW tempo it assumes 120 BPM."
#define WINDOW_HELP                                                                                                   \
    "The length of the slices the pitch shifter works with. Shorter windows react faster and trail your playing "     \
    "less, but can sound grainy on low notes. Longer windows sound smoother on low notes and chords, but the "        \
    "shifted sound lags a little behind. Start around 40-50 ms."
#define WIDTH_HELP(what)                                                                                              \
    "How wide the " what " spreads across the stereo field. 0% keeps both sides the same (centred); 100% makes "      \
    "left and right move differently for a wide, swirling sound. Only works when the plugin is in Stereo mode."

// Shared by the synths
#define SYNTH_ATTACK_HELP                                                                                             \
    "How quickly each note fades in. Short attacks follow your pick; long ones swell in slowly, like a bowed or "     \
    "pad sound."
#define SYNTH_RELEASE_HELP                                                                                            \
    "How long the synth keeps sounding after your note stops. Short releases follow the guitar closely; long ones "   \
    "let notes ring and blend into each other, like a pad."
#define SYNTH_OCTAVE_HELP                                                                                             \
    "Moves the synth up or down by octaves from the note you play. -1 and -2 give bass lines from guitar notes; "     \
    "+1 makes a higher lead."
#define SYNTH_GLIDE_HELP                                                                                              \
    "How long the synth slides from one note to the next, in milliseconds. Short times jump straight to each note; "  \
    "long ones give a smooth, portamento-like slide."
#define SYNTH_PICK_FILTER_HELP                                                                                        \
    "Keeps the click of your pick from sounding as a short, wrong note. A new note has to hold steady this long "     \
    "before the synth plays it; until then it stays silent, or keeps playing the last note. Raise it if you hear "    \
    "chirps or jumps at the start of notes; lower it for a faster response. Around 10-20 ms suits most playing; 0 "   \
    "is off."

#define TRACK_HELP                                                                                                    \
    "What the pedal listens to for your notes. Rig In (the default): your guitar as it enters the plugin, before "   \
    "any amp or effect; the clean guitar is much easier to follow, so notes come out cleaner. Block In: the signal "  \
    "arriving at this pedal; pick it to follow what an earlier pedal made, like an octave down. Either way the "     \
    "pedal plays at the level of the signal arriving at it, so a volume pedal or a gate before it still works."

#define PATTERN_TAP_HELP(when)                                                                                        \
    "How loud the echo " when " after your note is. 0% leaves that step silent; together the eight taps make the "   \
    "rhythm the echoes play."

#define SYNTH_MIX_HELP                                                                                                \
    "Blends the synth with your guitar. 100% is the synth only; lower values layer it with your dry playing."

constexpr BlockHelp kBlockHelp[] = {
    // NAM
    { "nam", "Input",
      "How hard you hit the amp model. Turning it up drives the model harder, so you get more gain, compression and "
      "distortion, just like turning up the guitar's volume into a real amp; turning it down cleans it up. Set it so "
      "your playing gets the amount of grit you want, then match the loudness with Output." },
    { "nam", "Output",
      "The volume coming out of the model, without changing its tone. Use it to balance this block against the rest "
      "of the chain, or to match the loudness when you change Input. 0 dB leaves the level as it is." },
    { "nam", "Normalize",
      "Brings every model to about the same loudness, using the loudness value stored in the .nam file. Keep it on "
      "so switching between models doesn't make big jumps in volume. Models without a stored loudness are left as "
      "they are." },
    { "nam", "Bass",
      "Boosts or cuts the lows coming out of the amp model (a shelf at 120 Hz). Handy for shaping each side of a split "
      "so the two amps don't pile up in the low end: trim the bass on one and let the other carry it. 0 dB leaves it "
      "as the model sounds." },
    { "nam", "Middle",
      "Boosts or cuts the mids coming out of the amp model (around 800 Hz). Boost to push the guitar forward in a mix "
      "or to make one side of a split cut through; cut for a scooped sound. 0 dB leaves it as the model sounds." },
    { "nam", "Treble",
      "Boosts or cuts the highs coming out of the amp model (a shelf at 3.2 kHz). Turn it down to tame fizz, up to add "
      "bite and clarity. 0 dB leaves it as the model sounds." },

    // IR
    { "ir", "Level",
      "The volume of the cab after the impulse response, without changing its tone. Use it to balance this cab "
      "against the rest of the chain. 0 dB leaves the level as it is." },
    { "ir", "Mix", MIX_HELP("cab sound") " Most of the time you want 100% for a cab; lower values let some of the raw "
                   "amp sound through." },
    { "ir", "Low Cut",
      "Removes the lowest frequencies after the cab. Turn it up to tighten a boomy or muddy cab, often to 80-120 Hz "
      "for a band mix. All the way down (20 Hz) it's off." },
    { "ir", "High Cut",
      "Removes the highest frequencies after the cab. Turn it down to soften fizz or harshness, often to 6-10 kHz. "
      "All the way up (20 kHz) it's off." },
    { "ir", "Normalize",
      "Brings every IR to about the same loudness, so switching cabs doesn't make big jumps in volume. Turn it off "
      "to hear the file at its own level." },

    // DI: Studio DI
    { "studiodi", "Iron",
      "How hard the transformer is pushed. A transformer's core saturates on the lows first, so turning this up "
      "thickens and rounds off the low end while the highs stay clean. Around 30% is a big studio DI's weight; 0% is "
      "a plain, transparent DI." },
    { "studiodi", "Low Cut",
      "Cuts the lowest lows: rumble, handling noise and boom you can't hear but that eats headroom. 30 Hz is safe "
      "for any bass, 60 Hz tightens a five-string's low B for a busy mix, and 100 Hz thins the bass out on purpose." },
    { "studiodi", "Air",
      "Lifts or softens the very top (above about 9 kHz): string zing, slap and pick click. Turn it up for a "
      "modern, hi-fi bass and down for a vintage, thumpy one." },
    { "studiodi", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Valve DI
    { "valvedi", "Character",
      "The DI's voicing switch. Flat is the plain tube sound; Deep adds weight below 100 Hz; Bright adds top for "
      "slap and pick; Mid pushes the middle so the bass is heard on small speakers; Scoop cuts the mids for a modern, "
      "hi-fi sound; Vintage rolls off the top for an old, round, thumpy tone." },
    { "valvedi", "Warmth",
      "How hard the tube is driven. Low settings add a little thickness and smooth the attack; higher settings "
      "round the peaks off and add the tube's even harmonics, for a warmer, slightly compressed bass." },
    { "valvedi", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Tube Preamp
    { "tubepreamp", "Gain",
      "How hard the preamp tubes are driven. Low settings are big and clean; past about 50% the classic tube "
      "growl comes in when you dig in, and near the top it's an overdriven rock bass sound." },
    { "tubepreamp", "Bass",
      "Boosts or cuts the low end (a shelf around 60 Hz). Boost for a bigger bottom, cut if the bass booms in the "
      "room or the mix." },
    { "tubepreamp", "Mid",
      "Boosts or cuts the midrange at the Mid Freq you choose. Boost to be heard in a band, cut for a scooped, "
      "deep sound." },
    { "tubepreamp", "Mid Freq",
      "Where the Mid knob works: 220 Hz for warmth and body, 450 Hz and 800 Hz for growl and punch, 1.6 kHz and "
      "3 kHz for finger and string attack." },
    { "tubepreamp", "Treble",
      "Boosts or cuts the highs (a shelf around 4 kHz): more string and pick when up, a rounder, older sound when "
      "down." },
    { "tubepreamp", "Ultra Lo",
      "The classic deep switch: more extreme lows and a big cut in the mids around 500 Hz. Great on its own or for "
      "a scooped rock sound; it can disappear in a busy band." },
    { "tubepreamp", "Ultra Hi",
      "Adds a bright, glassy top (a 9 dB shelf from about 8 kHz) for slap and pick attack." },
    { "tubepreamp", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Driver DI
    { "driverdi", "Drive",
      "How hard the emulated amp is driven: from a warm, clean amp sound to a fat, overdriven one. The driven side "
      "always goes through a bass speaker's voicing, like a miked amp." },
    { "driverdi", "Blend",
      "Mixes the amp sound with your clean bass. 0% is the clean bass only, 100% the amp only. In between, the clean "
      "signal keeps the low end solid and the attack clear while the amp adds the grit and growl." },
    { "driverdi", "Presence",
      "Boosts or cuts the upper mids and highs of the amp sound only: more bite and definition when up, smoother "
      "and darker when down." },
    { "driverdi", "Bass",
      "Boosts or cuts the lows of the whole sound (a shelf around 80 Hz): more weight when up, tighter when down." },
    { "driverdi", "Treble",
      "Boosts or cuts the highs of the whole sound (a shelf around 5 kHz): more string and pick when up, rounder down." },
    { "driverdi", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Modern Drive
    { "moderndrive", "Drive",
      "How much distortion: from a gritty edge to a modern, high-gain bass sound. Use Blend to keep the low end "
      "clean under it." },
    { "moderndrive", "Blend",
      "Mixes the distortion with your clean bass. The clean bass keeps the lows tight and the notes clear; the "
      "distortion adds the snarl on top. 50-70% is a typical modern rock and metal setting." },
    { "moderndrive", "Grunt",
      "How much low end goes into the distortion. Low settings keep it tight and focused; high settings give a "
      "fatter, more fuzzy, saturated low end." },
    { "moderndrive", "Attack",
      "How much top goes into the distortion. Up for a cutting, clanky, aggressive edge; down for a smoother, "
      "darker drive." },
    { "moderndrive", "Bass",
      "Boosts or cuts the lows of the whole sound (a shelf around 80 Hz), after the blend: weight and thump." },
    { "moderndrive", "Lo Mid",
      "Boosts or cuts around 500 Hz: body and punch when up, a deeper, scooped modern sound when down." },
    { "moderndrive", "Hi Mid",
      "Boosts or cuts around 1.5 kHz: the growl and note definition that let a distorted bass be heard in a mix." },
    { "moderndrive", "Treble",
      "Boosts or cuts the highs of the whole sound (a shelf around 5 kHz): clank and fizz up, smoother down." },
    { "moderndrive", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Crossover Drive
    { "crossoverdrive", "Crossover",
      "Where the bass is split: below this frequency it stays clean, above it is distorted. Low settings (80-150 Hz) "
      "distort more of the bass for a bigger dirty sound; high settings (300-500 Hz) keep the whole body clean and "
      "only distort the growl and attack." },
    { "crossoverdrive", "Lows",
      "The level of the clean lows below the crossover. Turn it up for a bigger, solid bottom under the dirt." },
    { "crossoverdrive", "Drive",
      "How much the highs above the crossover are distorted, from a light edge to a heavy, saturated growl." },
    { "crossoverdrive", "Tone",
      "Brightness of the distorted highs: darker and smoother when turned down, more fizz and bite when turned up." },
    { "crossoverdrive", "Highs",
      "The level of the distorted highs. Balance it against Lows: more for an aggressive sound, less for a subtle edge." },
    { "crossoverdrive", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Dual Drive
    { "dualdrive", "Clean",
      "The level of the clean channel. Turn it all the way down for distortion only, or up to keep the low end solid." },
    { "dualdrive", "Drive", "How much the distortion channel distorts: from crunch to a hard, buzzy fuzz-like edge." },
    { "dualdrive", "Dist",
      "The level of the distortion channel, mixed with the clean one. Bring it up under the clean channel for "
      "grit that keeps the bass solid." },
    { "dualdrive", "Color",
      "Scoops the distortion channel: cuts its mids and lifts its lows and highs, for a more aggressive, "
      "modern edge." },
    { "dualdrive", "Bass",
      "Boosts or cuts the lows of the mix of both channels (a shelf around 80 Hz): more weight up, tighter down." },
    { "dualdrive", "Mid",
      "Boosts or cuts the mids of the mix around 600 Hz: punch and presence in a band up, a scooped sound down." },
    { "dualdrive", "Treble",
      "Boosts or cuts the highs of the mix (a shelf around 3.5 kHz): more edge and fizz up, smoother and darker down." },
    { "dualdrive", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Hi-Fi Preamp
    { "hifipreamp", "Gain",
      "How hard the tube stage is driven: clean and transparent at 0, warmer and rounder as you turn it up. It "
      "stays fairly clean, for a hi-fi sound." },
    { "hifipreamp", "Comp",
      "The built-in compressor: evens out your playing so every note sits at the same level. Great for slap and "
      "fingerstyle. The meter shows how much it is turning down." },
    { "hifipreamp", "Bass",
      "Boosts or cuts the lows (a shelf around 60 Hz). Boost for a fuller bottom; cut if the bass booms or muddies the mix." },
    { "hifipreamp", "Mid",
      "Boosts or cuts the mids at Mid Freq. Boost to be heard in a band, cut for a scooped, modern slap sound." },
    { "hifipreamp", "Mid Freq",
      "Where the Mid knob works, from 180 Hz (warmth) to 1 kHz (definition). Cut around 400 Hz for a modern, "
      "scooped slap sound; boost around 700-800 Hz to cut through a band." },
    { "hifipreamp", "Treble",
      "Boosts or cuts the highs (a shelf around 4 kHz): more finger, string and slap when up, rounder and older when down." },
    { "hifipreamp", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: British Preamp
    { "britishpreamp", "Gain",
      "How hard the preamp is driven: clean and punchy low, a little edge and compression higher up." },
    { "britishpreamp", "Pre Shape",
      "The classic British bass preamp switch: a big mid cut with a lift in the lows and highs. Instantly a "
      "modern, hi-fi, slap-ready sound." },
    { "britishpreamp", "63 Hz",
      "The graphic EQ's lowest band, at 63 Hz: the deep bottom you feel. Boost for weight, cut if the room booms." },
    { "britishpreamp", "200 Hz",
      "The 200 Hz band: warmth and body. Cut a little if the bass sounds muddy or covers the kick drum and guitars." },
    { "britishpreamp", "600 Hz",
      "The 600 Hz band: punch and growl, where a bass is heard in a band. Cut it for a scooped, hi-fi sound." },
    { "britishpreamp", "1.6 kHz",
      "The 1.6 kHz band: note definition and finger attack. Boost it to hear every note clearly in a dense mix." },
    { "britishpreamp", "5 kHz",
      "The 5 kHz band: string zing, slap and pick click. Boost for a bright modern sound, cut to tame string noise." },
    { "britishpreamp", "Level",
      "The output volume. Set it so the bass is as loud as with the block bypassed, or louder to push what comes next." },

    // DI: Opto DI
    { "optodi", "Peak Reduction",
      "How much the optical compressor turns down your peaks. Low settings just catch the loudest notes; high "
      "settings even the playing right out. Its slow, smooth release keeps it natural. The meter shows how much it "
      "is turning down." },
    { "optodi", "Gain",
      "Make-up gain after the compressor: bring the level back up to where it was before the compression." },
    { "optodi", "Mix",
      "Parallel compression: blends the compressed sound with the uncompressed one. Below 100% the attack of each "
      "note comes through while the sustain is evened out." },
    { "optodi", "Bright",
      "Adds a little top (a shelf from about 6 kHz) for a clearer, more present bass that cuts through a busy mix." },

    // Boost
    { "boost", "Gain",
      "How much louder the boost makes your signal, from 0 to 24 dB. Before an amp, a boost pushes it into more "
      "drive and tightens the sound; after an amp, it's a solo volume boost. Try 6-10 dB in front of a dirty amp." },
    { "boost", "Type",
      "The flavour of the boost. Clean raises everything evenly. Treble boosts the highs, like a vintage treble "
      "booster for cutting leads. Mid pushes the middle, which helps a solo sit on top of the band." },
    { "boost", "Tone",
      "Tilts the sound darker or brighter. 50% is flat; below that the boost is darker, above it brighter." },

    // Overdrive
    { "overdrive", "Drive",
      "How much the pedal distorts. Low settings add a little warmth and grit that cleans up when you play softly; "
      "high settings give a thick, singing overdrive. Into an already dirty amp, keep it low and use Level to push "
      "the amp instead." },
    { "overdrive", "Tone",
      "Brightness of the drive. Turn it down for a smoother, darker sound and up for more bite and presence." },
    { "overdrive", "Level",
      "The pedal's output volume. Set it so the pedal is as loud as bypass, or louder to push the amp after it." },
    { "overdrive", "Type",
      "The character of the drive. Screamer has the famous mid hump and keeps your clean signal underneath. Blues is "
      "flatter and more open. Transparent keeps your guitar's natural tone and only adds gain as Drive goes up." },
    { "overdrive", "Mix", MIX_HELP("overdrive") },

    // Distortion
    { "distortion", "Drive",
      "How much distortion. Low settings give a crunchy rhythm sound; high settings give a saturated lead or "
      "high-gain rhythm sound with lots of sustain." },
    { "distortion", "Tone",
      "Brightness of the distortion. With the RAT type it works like the original's Filter, darkening the sound as "
      "you turn it down. Turn it up for more cut, down to tame fizz." },
    { "distortion", "Level",
      "The pedal's output volume. Distortion pedals often get louder as you add Drive; use this to match the level "
      "with bypass." },
    { "distortion", "Type",
      "The kind of distortion. RAT is a raw, hard-clipped classic that gets darker as Drive goes up. DS-1 is bright "
      "and scooped. Metal has two gain stages, tight lows and scooped mids for heavy riffing." },

    // Fuzz
    { "fuzz", "Fuzz",
      "How much fuzz. Low settings are a raspy, broken-up sound; high settings are thick, woolly and sustain for a "
      "long time. Fuzz pedals clean up nicely if you turn down your guitar's volume." },
    { "fuzz", "Tone",
      "Brightness of the fuzz. Turn it down for a dark, woolly sound and up for a buzzy, cutting one." },
    { "fuzz", "Level",
      "The pedal's output volume. Fuzz pedals can be much louder or quieter than bypass depending on the Fuzz "
      "setting; set this to match bypass, or louder for a solo." },
    { "fuzz", "Type",
      "The kind of fuzz. Face is a vintage germanium fuzz, warm and touch sensitive. Muff is a thick, sustaining "
      "fuzz with scooped mids. Octave adds a ringing octave up, especially high on the neck." },
    { "fuzz", "Bias",
      "Changes how the fuzz's transistor is set up. 50% is normal. Lower settings starve it, so notes break up into "
      "a gated, sputtering sound as they decay; higher settings change the texture of the clipping." },

    // Bitcrusher
    { "bitcrusher", "Bits",
      "How many bits the sound is reduced to. 16 is clean; fewer bits add more grit and noise, down to a harsh, "
      "broken sound at 1-4. Try 6-8 for a lo-fi video-game texture." },
    { "bitcrusher", "Rate",
      "The sample rate the sound is reduced to. Lower rates add metallic, ringing overtones (aliasing) and lose the "
      "highs, like an old sampler. Try 4-8 kHz for an obvious lo-fi sound." },
    { "bitcrusher", "Tone",
      "A filter after the crushing that softens its harsh highs. Turn it down to tame the fizz. All the way up it's "
      "off." },
    { "bitcrusher", "Mix", MIX_HELP("crushed sound") },

    // Compressor pedals: Diode Bridge
    { "diodebridge", "Threshold",
      "The level where the compressor starts working. Lower it to compress more of your playing; watch the "
      "Reduction meter. Around -20 dB gives a few dB of gentle levelling for most guitar parts." },
    { "diodebridge", "Ratio",
      "How firmly it squeezes the parts over the Threshold, in steps like the original's switch: 1.5:1 and 2:1 are "
      "gentle glue, 3:1 and 4:1 a firm, even sound, 6:1 close to limiting." },
    { "diodebridge", "Recovery",
      "How fast it lets go after a loud passage. The fixed times go from a quick 100 ms (lively, can pump) to a "
      "smooth 1.5 s. Auto 1 and Auto 2 follow your playing: short peaks recover fast and long loud passages slowly, "
      "so it stays even without pumping. Auto 2 is the slower, smoother of the two." },
    { "diodebridge", "Limit",
      "The limiter after the compressor: nothing gets louder than this, however hard you dig in. At 0 dB it only "
      "catches peaks near full scale; lower it to hold the peaks back. The meter shows both together." },
    { "diodebridge", "Gain",
      "Make-up gain after compressing. Raise it until the block is about as loud as bypass, or louder to push what "
      "comes next. The limiter comes after it, so it also sets how hard the limiter works." },
    { "diodebridge", "Mix",
      MIX_HELP("compressed sound") " Lower settings are parallel compression: the glue and weight with your pick "
      "attack kept." },

    // Compressor pedals: Console Comp
    { "consolecomp", "Drive",
      "How hard the channel's class-A preamp and its transformers are pushed before the compressor: clean and full "
      "at 0, thicker and warmer as you turn it up, with a gentle console grit at the top. The level stays about the "
      "same." },
    { "consolecomp", "Threshold",
      "The level where the compressor starts working. Lower it to compress more; watch the Reduction meter. Drive "
      "makes the signal a little louder, so it also compresses more when you turn Drive up." },
    { "consolecomp", "Ratio",
      "How firmly it squeezes the parts over the Threshold: 1.5:1 and 2:1 are gentle, 3:1 and 4:1 firm, 6:1 close to "
      "limiting." },
    { "consolecomp", "Attack",
      "How fast it grabs a loud note. Fast catches the pick attack and smooths it; Slow lets the pick snap through "
      "before it turns down, for a punchier sound. Medium is in between." },
    { "consolecomp", "Release",
      "How fast it lets go after the level drops. Short times are lively and can pump with the beat; long ones are "
      "smooth and steady. 200-500 ms suits most parts." },
    { "consolecomp", "Level",
      "The output volume. Raise it to make up for the compression, so the block is as loud as bypass." },
    { "consolecomp", "Mix",
      MIX_HELP("compressed sound") " Lower settings are parallel compression: the sustain with your natural attack." },

    // Compressor pedals: Opto Leveler
    { "optoleveler", "Peak Reduction",
      "How much it levels your playing: turn it up until the Reduction meter shows a few dB. The optical cell reacts "
      "smoothly and lets go slowly, so it evens out a part without sounding squashed." },
    { "optoleveler", "Gain",
      "Make-up gain, into the tube output stage. Bring the level back up to bypass; pushed hard, the tube adds a "
      "little warmth and roundness." },
    { "optoleveler", "Mode",
      "Compress is the gentle, natural levelling (about 3:1). Limit holds the peaks much more firmly (about 10:1), "
      "for a flatter, more controlled sound." },
    { "optoleveler", "Emphasis",
      "Makes the compressor listen more to the treble. At 0 it reacts to everything; turn it up and bright, spiky "
      "notes are turned down more while the lows stay full. Handy for a sharp pick attack or a bright amp." },
    { "optoleveler", "Mix",
      MIX_HELP("levelled sound") " Lower settings are parallel compression: an even level with your dynamics kept." },

    // Compressor pedals: Opto Stomp
    { "optostomp", "Sustain",
      "How much it compresses: notes ring longer and your playing evens out as you turn it up. The optical cell "
      "is smooth and lets the pick attack through, so even high settings stay natural. It adds some level as you go." },
    { "optostomp", "Tone",
      "A treble control after the compressor: turn it up to bring back sparkle that compression takes away, down to "
      "soften a bright guitar." },
    { "optostomp", "Level",
      "The output volume. Set it so the block is as loud as bypass, or a little louder for solos." },
    { "optostomp", "Blend",
      "Mixes the compressed sound with your dry guitar. 100% is all compressed; around 50% you keep your pick attack "
      "and dynamics with the compressor's sustain under them." },

    // Compressor pedals: FET Limiter
    { "fetlimiter", "Input",
      "How hard you drive the compressor: the threshold is fixed, so more Input means more compression, like the "
      "original. The level is compensated, so it doesn't get louder; pushed hard, the output stage adds grit." },
    { "fetlimiter", "Output",
      "Make-up gain: raise it to bring the level back up after compressing, or past it to push what comes next." },
    { "fetlimiter", "Attack",
      "How fast it grabs the peaks, from 0.02 ms (instant: it shaves off the pick attack) to 0.8 ms (lets a bit of "
      "the snap through). All of it is very fast; this is what the FET is famous for." },
    { "fetlimiter", "Release",
      "How fast it lets go. Short releases (50-100 ms) are aggressive and bring up the room and the sustain, and can "
      "distort on low notes; long ones (500 ms and up) are smoother." },
    { "fetlimiter", "Ratio",
      "4:1 is a firm compressor, 8:1 and 12:1 heavier, 20:1 a limiter. All is all the buttons pushed in at once: it "
      "over-compresses loud notes and distorts, for that aggressive, exploding sound." },
    { "fetlimiter", "Mix",
      MIX_HELP("compressed sound") " Parallel compression is great with All: the smash under your clean attack." },

    // Compressor pedals: Squash Comp
    { "squashcomp", "Sustain",
      "How much quiet notes are brought up while loud ones are held back: more sustain and a flatter, squashier "
      "sound as you turn it up. The classic chicken-picking country sound sits around the middle." },
    { "squashcomp", "Attack",
      "Fast clamps down on the pick at once, for the smoothest squash. Slow lets the pick click through before it "
      "squeezes, for more snap and that bouncy, pumping feel." },
    { "squashcomp", "Level",
      "The output volume. Set it so the block is as loud as bypass, or a little louder for a solo boost." },
    { "squashcomp", "Blend",
      "Mixes the squashed sound with your dry guitar. Around 50% keeps your pick attack and adds the sustain under it." },

    // Compressor
    { "compressor", "Threshold",
      "The level where the compressor starts turning things down. Anything louder than this gets squeezed. The "
      "lower you set it, the more of your playing is compressed; watch the Reduction meter to see how much it's "
      "working." },
    { "compressor", "Ratio",
      "How strongly it squeezes the parts above the Threshold. 2:1 is gentle, 4:1 is a firm, even sound, and 10:1 "
      "or more is close to a limiter. Higher ratios give more sustain and a more squashed sound." },
    { "compressor", "Attack",
      "How fast it reacts to a loud note. Fast attack catches the pick attack and smooths it; slower attack (10-30 "
      "ms) lets the pick snap through before turning down, which keeps the sound punchy." },
    { "compressor", "Release",
      "How fast it lets go after the level drops. Short release recovers quickly but can pump; long release is "
      "smoother. 100-300 ms suits most guitar parts." },
    { "compressor", "Knee",
      "How gradually compression starts around the Threshold. 0 dB is a hard knee that clamps down suddenly; higher "
      "values ease in gently for a more natural sound." },
    { "compressor", "Makeup",
      "Adds volume back after compressing, since squeezing makes things quieter. Raise it until the block is about "
      "as loud as bypass." },
    { "compressor", "Mix",
      MIX_HELP("compressed sound") " Lower settings are parallel compression: sustain and evenness while keeping "
      "your natural pick attack." },

    // Noise Gate
    { "gate", "Threshold",
      "The level below which the gate closes and silences the sound. Set it just above the hiss and hum you hear "
      "when you're not playing. Too high and it cuts off the end of your notes." },
    { "gate", "Reduction",
      "How much the sound is turned down when the gate is closed. 80-90 dB is near silence; 10-20 dB just lowers "
      "the noise, which sounds more natural." },
    { "gate", "Attack",
      "How fast the gate opens when you start playing. Keep it short (around 1 ms) so the pick attack isn't lost." },
    { "gate", "Hold",
      "How long the gate stays open after the level drops below the Threshold, before it starts closing. A little "
      "hold stops it from chattering on and off during short gaps." },
    { "gate", "Release",
      "How fast the gate closes once it starts. Short release cuts tightly, good for staccato metal; longer release "
      "lets notes fade out naturally." },
    { "gate", "Hysteresis",
      "How far the level has to fall below the Threshold before the gate closes again. A few dB stops the gate from "
      "flickering open and closed on notes hovering around the Threshold." },
    { "gate", "Type",
      "Gate closes all the way (by Reduction) when you stop playing. Expander turns quiet sounds down gradually, "
      "the further below the Threshold they are, which is more natural." },
    { "gate", "Key",
      "What the gate listens to. Block In listens to the sound coming into this block. Rig In listens to your clean "
      "guitar before the whole chain, so a gate placed after a high-gain amp follows your real playing, not the "
      "amp's noise." },

    // Volume
    { "volume", "Volume",
      "The volume, from silence (0%) to full (100%). Map an expression pedal to it with MIDI learn (right-click the "
      "knob) to use it as a volume pedal for swells and fades." },
    { "volume", "Taper",
      "How the knob's travel maps to loudness. Audio feels even all the way through, like a real volume pedal. "
      "Linear stays quiet longer and comes in late, which suits slow swells." },

    // Auto Swell
    { "swell", "Attack",
      "How long each note takes to fade in. Short times soften the pick attack; long times (1 s or more) give a "
      "violin-like or pad-like swell." },
    { "swell", "Threshold",
      "How loud a note has to be to start a new swell. Set it above the noise but below your softest notes. If "
      "swells don't start, lower it; if noise triggers them, raise it." },
    { "swell", "Retrigger",
      "When on, a fresh pick attack starts a new swell even while the last note is still ringing. Turn it off so "
      "only notes played after a pause swell." },

    // Graphic EQ
    { "eq.graphic", "100",
      "Boosts or cuts the deep lows around 100 Hz: the thump and weight. Cut it to tighten a boomy sound; boost for "
      "more body." },
    { "eq.graphic", "200",
      "Boosts or cuts the low mids around 200 Hz: warmth and fullness. Too much sounds muddy; cutting it clears the "
      "mix." },
    { "eq.graphic", "400",
      "Boosts or cuts around 400 Hz, where boxy and wooden sounds live. Cut it to open the sound up." },
    { "eq.graphic", "800",
      "Boosts or cuts the mids around 800 Hz: the core of the guitar's sound. Boost it to cut through a band mix, "
      "cut it for a scooped sound." },
    { "eq.graphic", "1.6k",
      "Boosts or cuts the upper mids around 1.6 kHz: bite, attack and presence. A small boost helps a solo stand "
      "out; too much sounds honky." },
    { "eq.graphic", "3.2k",
      "Boosts or cuts around 3.2 kHz: clarity and edge. Cut it if the sound is harsh or hurts the ears." },
    { "eq.graphic", "6.4k",
      "Boosts or cuts the highs around 6.4 kHz: sparkle and air, and also fizz. Cut it to smooth a fizzy distortion." },
    { "eq.graphic", "Level",
      "Overall volume after the EQ. Boosting bands makes things louder; use this to match the level with bypass." },

    // Parametric EQ
    { "eq.parametric", "Low Cut",
      "Removes everything below this frequency. Use it to get rid of rumble and boom, often 60-100 Hz. All the way "
      "down (20 Hz) it's off." },
    { "eq.parametric", "Low",
      "Raises or lowers everything below about 100 Hz, like a bass knob. Boost for weight, cut to tighten." },
    { "eq.parametric", "Mid Freq",
      "Chooses which frequency the Mid knob works on. Sweep it while boosting Mid to find the spot you want, then "
      "set Mid to taste: around 400 Hz is boxy, 800 Hz is body, 1.5-2 kHz is bite." },
    { "eq.parametric", "Mid",
      "Boosts or cuts the band at Mid Freq. Boost to bring the guitar forward, cut to remove an unpleasant spot." },
    { "eq.parametric", "Q",
      "How narrow the Mid band is. Low values affect a broad, musical range; high values hit a narrow spot, which is "
      "good for cutting a single harsh or ringing frequency." },
    { "eq.parametric", "High",
      "Raises or lowers everything above about 4 kHz, like a treble knob. Boost for sparkle, cut to soften." },
    { "eq.parametric", "High Cut",
      "Removes everything above this frequency. Use it to remove fizz and harshness, often 6-10 kHz. All the way up "
      "(20 kHz) it's off." },
    { "eq.parametric", "Level",
      "Overall volume after the EQ. Use it to match the level with bypass after boosting or cutting." },

    // Chorus
    { "chorus", "Rate",
      "How fast the chorus sweeps. Slow rates (0.3-1 Hz) are lush and gentle; fast rates become a wobbly vibrato or "
      "Leslie-like shimmer." },
    { "chorus", "Depth",
      "How far the pitch wavers. Low depth is a subtle thickening; high depth is a seasick, detuned sound." },
    { "chorus", "Mix", MIX_HELP("chorus") " For a classic chorus, 50% is the sweet spot." },
    { "chorus", "Type",
      "The kind of chorus. Classic is one voice, like a vintage chorus pedal. Dimension is two subtle voices for a "
      "wide, still sound. Tri is three voices for a big, lush ensemble. Vibrato is pitch wobble only, no dry "
      "signal." },
    { "chorus", "Tone",
      "Darkens the chorused sound. Turn it down for a warmer, more vintage chorus. All the way up it's off." },
    { "chorus", "Width", WIDTH_HELP("chorus") },

    // Flanger
    { "flanger", "Rate",
      "How fast the flanger sweeps up and down. Slow rates give the classic jet-plane whoosh; fast rates a wobbling "
      "warble." },
    { "flanger", "Sync", SYNC_HELP("rate") },
    { "flanger", "Depth",
      "How far the sweep travels. At 0% it doesn't move, leaving a fixed comb-filter tone set by Manual; at 100% it "
      "sweeps widely." },
    { "flanger", "Manual",
      "The centre of the sweep. Short times give a high, metallic sweep; longer times a lower, deeper one. With "
      "Depth at 0%, sweep this knob by hand (or with MIDI) for a manual flange." },
    { "flanger", "Feedback",
      "How intense and resonant the flanging is. Positive values give the classic jet sound; negative values a "
      "hollow, nasal one. Near the extremes it rings like metal." },
    { "flanger", "Mix", MIX_HELP("flanged sound") " 50% gives the deepest notches." },
    { "flanger", "Width", WIDTH_HELP("flanger") },

    // Phaser
    { "phaser", "Rate",
      "How fast the phaser sweeps. Slow rates are a gentle swoosh; fast rates a bubbly, rotary-like sound." },
    { "phaser", "Sync", SYNC_HELP("rate") },
    { "phaser", "Depth", "How far the sweep travels. Low depth is subtle; high depth a big, obvious swirl." },
    { "phaser", "Center",
      "The middle of the sweep. Low settings make it throaty, high settings make it thin and airy." },
    { "phaser", "Feedback",
      "How resonant and vocal the phasing sounds. Higher values make the sweep more pronounced and whistling; "
      "negative values change its character." },
    { "phaser", "Mix", MIX_HELP("phased sound") " 50% gives the strongest phasing; with Vibe at 100% you get its "
                       "vibrato." },
    { "phaser", "Type",
      "How many stages the phaser has. 4 Stage is the classic subtle swirl; 6, 8 and 12 stages give more and deeper "
      "notches. Vibe imitates a Uni-Vibe: a throbbing, liquid sound." },
    { "phaser", "Width", WIDTH_HELP("phaser") },

    // Modulation Sweeper
    { "modsweeper", "Effect 1",
      "The first effect of the sweep. The sweeper starts here, then moves on to Effect 2 (and Effect 3, if one is "
      "picked). Choose from chorus, flanger, phaser and vibrato." },
    { "modsweeper", "Effect 2", "The second effect of the sweep: the one it moves to after Effect 1. Picking two that contrast, like chorus "
      "and flanger, makes the sweep easy to hear." },
    { "modsweeper", "Effect 3",
      "An optional third effect. None sweeps between Effects 1 and 2 only; pick an effect to sweep through all "
      "three." },
    { "modsweeper", "Pattern",
      "The order it moves through three effects. Back & Forth goes 1, 2, 3, 2, 1, 2... like a pendulum. Cycle goes "
      "1, 2, 3, 1, 2, 3... With two effects both simply alternate." },
    { "modsweeper", "Sweep Rate",
      "How often it moves on to the next effect. 0.25 Hz is a new effect every 4 seconds; 1 Hz every second. Slow "
      "it right down for a sound that changes over a whole song section." },
    { "modsweeper", "Sync",
      SYNC_HELP("sweep rate") " Each note value is one step, so at 1 bar it moves to the next effect every bar, "
      "right on the downbeat while the DAW plays." },
    { "modsweeper", "Glide",
      "How smoothly it moves between effects. At 0% it holds each effect and switches quickly (without a click). "
      "Around 50% it holds for half the step and morphs over the other half. At 100% it's always morphing from "
      "one effect into the next." },
    { "modsweeper", "Depth",
      "How strong the modulation is, for every effect: how far the chorus and vibrato waver in pitch and how far "
      "the flanger and phaser sweep." },
    { "modsweeper", "Mix", MIX_HELP("chorus, flanger and phaser") " The vibrato is always 100% effect." },
    { "modsweeper", "Feedback",
      "How intense and resonant the flanger and phaser are. Positive values give the classic jet and whistle; "
      "negative values a hollow sound. It doesn't change the chorus or vibrato." },
    { "modsweeper", "Width", WIDTH_HELP("modulation") },
    { "modsweeper", "Chorus Rate",
      "How fast the chorus wavers while it's playing. 0.5-1 Hz is lush and gentle; faster rates get wobbly." },
    { "modsweeper", "Flanger Rate",
      "How fast the flanger sweeps up and down while it's playing. Slow rates give the classic jet-plane whoosh." },
    { "modsweeper", "Phaser Rate",
      "How fast the phaser swirls while it's playing. Slow rates are a gentle swoosh; fast rates bubbly." },
    { "modsweeper", "Vibrato Rate",
      "How fast the vibrato wobbles the pitch while it's playing. 4-7 Hz is a typical singer-like vibrato." },

    // Tremolo
    { "tremolo", "Rate",
      "How fast the volume pulses. Slow rates give a gentle wave; fast rates a choppy, helicopter-like stutter." },
    { "tremolo", "Sync", SYNC_HELP("rate") " The loudest point of each pulse lands on the beat while the DAW plays." },
    { "tremolo", "Depth",
      "How much the volume dips. Low depth is a gentle shimmer; 100% chops all the way to silence." },
    { "tremolo", "Type",
      "The shape of the pulse. Sine is smooth, Triangle is even, Square is choppy. Opto is the warm, slightly "
      "uneven pulse of a tube amp's tremolo. Harmonic pulses the lows and highs in turns, like a vintage Fender. Pan "
      "moves the sound left and right in Stereo mode." },
    { "tremolo", "Level", "Output volume of the block. Use it to make up for the volume the tremolo takes away." },

    // Rotary
    { "rotary", "Speed",
      "Switches the rotating speaker between Slow (chorale) and Fast (tremolo). The change speeds up or slows down "
      "gradually, like the real thing. Map a footswitch to it with MIDI learn." },
    { "rotary", "Slow",
      "How fast the horn spins at the Slow setting. The classic slow (chorale) speed is under 1 Hz: a gentle, "
      "swirling chorus-like sound." },
    { "rotary", "Fast",
      "How fast the horn spins at the Fast setting. The classic fast speed is around 6-7 Hz: a fast, throbbing "
      "tremolo with a strong pitch wobble." },
    { "rotary", "Ramp",
      "How long the speakers take to speed up or slow down when you switch Speed. 100% is the few seconds of a real "
      "Leslie; lower is quicker." },
    { "rotary", "Drive",
      "Overdrives the amp in front of the rotating speaker. A little adds warmth; a lot gives the growly sound "
      "of a cranked organ." },
    { "rotary", "Balance",
      "The level of the high horn against the low drum. Raise it for a brighter, more swirly sound, lower it for a "
      "darker, more throbbing one." },
    { "rotary", "Width",
      "How far apart the two virtual microphones are, so how wide the stereo image is. Only works in Stereo mode." },
    { "rotary", "Mix", MIX_HELP("rotary sound") },

    // Doubler
    { "doubler", "Voices",
      "How many extra takes are added. Single adds one double, like a second guitar track. Double adds two, panned "
      "left and right in Stereo mode, for a big wall of guitars." },
    { "doubler", "Delay",
      "How far behind your playing the doubles are. 10-25 ms sounds like a tight second take; longer times start to "
      "sound like a slapback." },
    { "doubler", "Drift",
      "How much the doubles wander in timing and pitch, like a real second performance. 0% is a static, robotic "
      "copy; around 40% sounds natural; high values sound loose and wobbly." },
    { "doubler", "Tone",
      "Darkens the doubles so they sit slightly behind your main part instead of competing with it, like a "
      "second take played on a warmer guitar. All the way up it's off." },
    { "doubler", "Width",
      "How far the doubles are panned to the sides. Works in Stereo mode; best after the amp and cab." },
    { "doubler", "Mix", MIX_HELP("doubles") },

    // Ring Mod
    { "ringmod", "Frequency",
      "The pitch of the tone your guitar is multiplied with. It creates new, unrelated pitches: low settings give a "
      "warbling tremolo, higher ones bell-like, robotic or clangy sounds." },
    { "ringmod", "Mix", MIX_HELP("ring-modulated sound") },
    { "ringmod", "Rate", "How fast the LFO sweeps the Frequency up and down (set Depth above 0% to hear it)." },
    { "ringmod", "Depth",
      "How far the LFO sweeps the Frequency. 0% keeps it fixed; 100% sweeps an octave up and down, for sci-fi "
      "sounds." },
    { "ringmod", "Wave",
      "The shape of the tone your guitar is multiplied with. Sine is smooth and bell-like; Square is harsher "
      "and buzzier, with more new pitches." },

    // Reverb
    { "reverb", "Type",
      "The kind of space. Room is a small, natural space. Hall is a big, smooth concert hall. Plate is the bright, "
      "dense studio reverb heard on many records." },
    { "reverb", "Decay",
      "How long the reverb takes to die away, in seconds. Short decays add space without washing things out; long "
      "decays give huge ambient tails." },
    { "reverb", "Pre-Delay",
      "A short gap before the reverb starts. A little pre-delay (10-40 ms) keeps your notes clear in front of the "
      "reverb." },
    { "reverb", "Damping",
      "How quickly the high frequencies fade in the tail. More damping sounds warmer and darker, like a room full "
      "of soft surfaces; less is brighter." },
    { "reverb", "Low Cut",
      "Removes lows from the reverb only, so the tail doesn't muddy your sound. All the way down it's off." },
    { "reverb", "High Cut",
      "Removes highs from the reverb only, for a darker, smoother tail. All the way up it's off." },
    { "reverb", "Width", "How wide the reverb tail is. 100% is fully wide; 0% is centred. Stereo mode only." },
    { "reverb", "Mix", MIX_HELP("reverb") " 15-30% adds space; higher settings push the guitar far back." },

    // Spring Reverb
    { "reverb.spring", "Decay",
      "How long the springs ring on, in seconds. Short decays give a tight, slappy reverb; long decays a big, "
      "washy surf sound." },
    { "reverb.spring", "Tension",
      "How tight the springs are. Tighter springs sound shorter and brighter; looser ones darker and longer." },
    { "reverb.spring", "Drip",
      "How strongly the springs chirp and 'drip'. High settings give the wet, surfy boing of a vintage amp's spring "
      "tank." },
    { "reverb.spring", "Tone",
      "Brightness of the springs. Turn it down for a darker, more vintage reverb; turn it up for a bright, "
      "splashy one." },
    { "reverb.spring", "Dwell",
      "How hard the springs are driven, like the Dwell knob of a surf reverb unit. More dwell gives a more intense, "
      "splashier reverb." },
    { "reverb.spring", "Mix", MIX_HELP("spring reverb") " For surf, go 40% and up." },
    { "reverb.spring", "Width",
      "How wide the two springs spread across the stereo field. 0% is centred; higher settings sound wider. "
      "Stereo mode only." },

    // Shimmer
    { "shimmer", "Decay",
      "How long the reverb tail lasts, in seconds. Short decays keep the shimmer subtle; long decays build "
      "huge, ambient pads that keep rising." },
    { "shimmer", "Shimmer",
      "How much pitch-shifted sound is fed back into the reverb. More shimmer builds a rising, choir-like halo; 0% "
      "is a plain reverb." },
    { "shimmer", "Interval",
      "How far the shimmer is shifted. +1 Oct is the classic angelic shimmer; +5th and +Oct+5th give an organ-like "
      "chord; -1 Oct gives a dark, droning tail." },
    { "shimmer", "Tone",
      "Brightness of the tail. Turn it down if the shimmer gets too glassy or piercing; turn it up for more "
      "sparkle." },
    { "shimmer", "Size",
      "How big the space sounds. Small sizes are more intimate; larger sizes give a slower, more spread-out "
      "tail." },
    { "shimmer", "Low Cut",
      "Removes the lows from the reverb only, so the tail doesn't muddy your playing. Raise it for an airier, "
      "more ethereal shimmer." },
    { "shimmer", "Mix", MIX_HELP("shimmer reverb") },

    // Freeze
    { "freeze", "Freeze",
      "Captures the sound you just played and holds it forever as a pad, so you can play over it. Turn it off and "
      "the pad fades away. Map a footswitch to it with MIDI learn." },
    { "freeze", "Layer",
      "While on (and frozen), whatever you play is added into the held pad, so you can build up chords and layers." },
    { "freeze", "Fade In",
      "How quickly the held pad fades in when you freeze. Short times capture the sound almost instantly; long "
      "times bring the pad in slowly and smoothly, like a swell." },
    { "freeze", "Fade Out",
      "How slowly the held pad fades away when you turn Freeze off. Short times stop it quickly; long times let "
      "it die away gently under your playing." },
    { "freeze", "Tone",
      "Darkens the held pad so it sits behind your playing instead of clashing with it. Turn it down for a "
      "soft, warm drone. All the way up it's off." },
    { "freeze", "Level",
      "The volume of the held pad. Lower it so the pad sits quietly underneath what you play. Your dry playing "
      "always passes at full level." },

    // Delay
    { "delay", "Time",
      "The time between repeats. Short times (60-150 ms) give a slapback; 300-500 ms a classic echo; longer times "
      "spacious ambient repeats. With Sync on, it picks a note value instead." },
    { "delay", "Sync", SYNC_HELP("time") },
    { "delay", "Feedback",
      "How many repeats you hear. Low settings give one or two echoes; high settings many; near 100% the repeats "
      "go on and on." },
    { "delay", "Mix", MIX_HELP("echoes") " 20-35% keeps the repeats behind your playing." },
    { "delay", "Tone",
      "Darkens each repeat, so the echoes get warmer as they fade, like an analog delay. All the way up the "
      "repeats stay bright." },
    { "delay", "Mode",
      "Mono repeats in the centre. Stereo uses separate left and right delays (offset by Spread). Ping-Pong "
      "bounces the repeats from left to right. Both work in Mono mode too: the chain turns stereo at the Delay." },
    { "delay", "Spread",
      "Delays the right side a little more than the left, for a wide, spacious echo. Stereo and Ping-Pong modes." },
    { "delay", "Mod",
      "Adds a gentle wobble to the repeats, like a tape or analog delay, for a lusher sound. 0% is clean digital "
      "repeats." },

    // Doppelganger Delay
    { "doppelganger", "Time",
      "The time between echoes. Echoes alternate between flanged and chorused, so longer times make the alternation "
      "easier to hear. With Sync on, it picks a note value instead." },
    { "doppelganger", "Sync", SYNC_HELP("time") },
    { "doppelganger", "Feedback",
      "How many echoes you hear. Higher settings give more repeats, alternating between the two effects." },
    { "doppelganger", "Mix", MIX_HELP("echoes") },
    { "doppelganger", "Tone",
      "Darkens each repeat, so the echoes get warmer as they fade. All the way up it's off." },
    { "doppelganger", "Order",
      "Which effect comes first. Flange 1st makes the 1st, 3rd, 5th echoes flanged and the 2nd, 4th, 6th chorused; "
      "Chorus 1st swaps them." },
    { "doppelganger", "Width",
      "How differently the effects move on the left and right sides, for a wider sound. Stereo mode only." },
    { "doppelganger", "Bounce",
      "Leans the flanged echoes to the left and the chorused echoes to the right, so they bounce across the stereo "
      "field. Stereo mode only." },
    { "doppelganger", "Flange Rate",
      "How fast the flanger on the flanged echoes sweeps up and down. Slow rates give a slow jet-plane whoosh "
      "across the echoes; fast rates a quick warble." },
    { "doppelganger", "Flange Depth",
      "How far the flanger on the flanged echoes sweeps. Low depth gives a subtle metallic colour; high depth a "
      "big, obvious whoosh on each flanged echo." },
    { "doppelganger", "Flange Regen",
      "How intense the flanger is: higher values give a thicker, jet-like flange; negative values a hollow one." },
    { "doppelganger", "Flange Mix",
      "How much flanger is on the flanged echoes. 100% gives the full flange; 0% makes those echoes plain, "
      "clean repeats, so only the chorused echoes carry an effect." },
    { "doppelganger", "Chorus Rate",
      "How fast the chorus on the chorused echoes sweeps. Slow rates (0.5-1 Hz) are lush and gentle; faster "
      "rates sound wobblier, like a vibrato." },
    { "doppelganger", "Chorus Depth",
      "How much the chorus on the chorused echoes wavers in pitch. Low depth is a gentle thickening; high depth "
      "is a lush, seasick shimmer." },
    { "doppelganger", "Voices",
      "How many chorus voices the chorused echoes have. More voices sound thicker and lusher." },
    { "doppelganger", "Chorus Mix",
      "How much chorus is on the chorused echoes. 100% gives the full chorus; 0% makes those echoes plain, "
      "clean repeats, so only the flanged echoes carry an effect." },

    // Tape Echo
    { "tapeecho", "Time",
      "The time to the first playback head. The other heads repeat at two and three times this. With Sync on, it "
      "picks a note value instead. Turning it while playing bends the repeats' pitch, like changing tape speed." },
    { "tapeecho", "Sync", SYNC_HELP("time") },
    { "tapeecho", "Feedback",
      "How many repeats you hear. High settings make the tape echo build up and run away into self-oscillation." },
    { "tapeecho", "Mix", MIX_HELP("echoes") },
    { "tapeecho", "Tone",
      "How dark the repeats are. Tape loses highs on every pass; turn it down for a murkier, more vintage sound." },
    { "tapeecho", "Wow",
      "How much the tape speed wavers, which bends the repeats' pitch. A little sounds warm and vintage; a lot "
      "sounds like a worn-out machine." },
    { "tapeecho", "Drive",
      "How hard the tape is saturated. A little drive gives warm, slightly compressed repeats; more gives "
      "fuzzy, gritty repeats that break up as they build with Feedback." },
    { "tapeecho", "Heads",
      "Which playback heads are on. Each head repeats at a different time (1x, 2x and 3x Time); combining heads "
      "gives rhythmic patterns of echoes." },

    // Ducking Delay
    { "duckdelay", "Time",
      "The time between repeats. Short times give a slapback, 300-500 ms a classic echo, longer times spacious "
      "repeats. With Sync on, it picks a note value instead." },
    { "duckdelay", "Sync", SYNC_HELP("time") },
    { "duckdelay", "Feedback",
      "How many repeats you hear. Low settings give one or two echoes; high settings a long trail of repeats, "
      "which still stay out of the way while you play." },
    { "duckdelay", "Mix", MIX_HELP("echoes") },
    { "duckdelay", "Tone", "Darkens each repeat, so the echoes get warmer as they fade. All the way up it's off." },
    { "duckdelay", "Threshold",
      "How loud your playing has to be to push the repeats down. Lower settings duck the echoes more often." },
    { "duckdelay", "Duck",
      "How much the repeats are turned down while you play. They come back up in the gaps, so they never crowd "
      "your notes." },
    { "duckdelay", "Release",
      "How quickly the repeats swell back up after you stop playing. Short releases bring them back almost at "
      "once; long releases make them fade in slowly, like a swell." },

    // Looper
    { "looper", "Rec/Dub",
      "Press to record. Press again to close the loop and play it back. While it plays, press to overdub more "
      "layers, and again to go back to just playing. In Count In mode the first press starts the count-in (press "
      "again to call it off), and the loop closes by itself after the bars you set. Map a footswitch to it with "
      "MIDI learn." },
    { "looper", "Play/Stop",
      "Stops the loop, or starts it again from the beginning. While recording, it closes the loop and plays it." },
    { "looper", "Clear",
      "Erases the loop completely, so you can record a new one. There's no undo for this, so be sure you're "
      "done with the loop. Map a footswitch to it with MIDI learn." },
    { "looper", "Level",
      "The volume of the loop playback. Lower it so the loop sits behind what you play live. Your live playing "
      "always passes at full level." },
    { "looper", "Feedback",
      "How much of the loop is kept on each overdub pass. 100% keeps everything; lower values fade old layers out "
      "as you add new ones." },
    { "looper", "Reverse",
      "Plays the loop backwards, for a swelling, otherworldly sound. Turn it off to play the loop forwards "
      "again." },
    { "looper", "Half Speed",
      "Plays the loop at half speed, so it sounds an octave lower and twice as long. Great for a slowed-down, "
      "dreamy version of what you just played." },
    { "looper", "Mode",
      "Count In: pressing Rec/Dub starts a metronome that counts you in, records exactly the bars you set (Measures) "
      "with the click going, then plays the loop back without it. Free: recording starts the moment you press "
      "Rec/Dub and stops when you press it again, with no click." },
    { "looper", "BPM",
      "The tempo of the count-in metronome, in beats per minute. It sets how long the loop is: Measures bars of "
      "Beats beats at this tempo. Set it before pressing Rec/Dub." },
    { "looper", "Beats",
      "Beats to the bar for the count-in metronome (4 for 4/4, 3 for a waltz). The first beat of each bar clicks "
      "higher." },
    { "looper", "Count-In",
      "How many clicks the metronome counts before recording starts. 8 gives you two bars of 4/4 to get ready; 4 "
      "one bar; 0 starts recording on the first click." },
    { "looper", "Measures",
      "How many bars Count In mode records. When they're done the loop closes on its own and plays back, so you "
      "don't need to hit the footswitch on the beat." },
    { "looper", "Loops",
      "How many times round the loop Count In mode records. 1 records the loop once; with more, each extra pass "
      "overdubs on top (the click keeps going) before it settles into playback." },
    { "looper", "Click",
      "The volume of the count-in metronome, up to 200%. 100% is as loud as the main metronome's default. The click "
      "is only heard, never recorded into the loop, and blocks after the looper don't change it, so an amp after "
      "it won't distort it." },
    { "looper", "Tap",
      "Tap it in time, three or four times, to set the BPM for the next count-in to the tempo you tap. Map a "
      "footswitch to it with MIDI learn to set the tempo with your foot." },
    { "looper", "Latency",
      "Your audio interface's round trip: how late what you play reaches the plugin after the sound you played to "
      "left it. The looper makes up for it, so a Count In loop starts on the beat you played and comes round on the "
      "click, and overdubs land where you heard the loop. If loops or overdubs come in late, raise it; early, lower "
      "it. Your DAW may show the figure: Reaper shows the input and output latency (add them) top right in its main "
      "window. Set as Default "
      "keeps it for new loopers." },

    // Octave
    { "octave", "Sub 1",
      "Level of the voice one octave below what you play, for a fat, bass-like sound. Play single notes; chords "
      "confuse it." },
    { "octave", "Sub 2", "Level of the voice two octaves below what you play, for a deep, synth-like bass." },
    { "octave", "Up", "Level of the voice one octave above what you play, for a ringing, fuzzy octave-up." },
    { "octave", "Dry",
      "The level of your original signal. Keep some dry for a big, doubled sound; turn it all the way down to "
      "hear only the octave voices, like a bass guitar." },
    { "octave", "Tone",
      "Darkens the octave voices so they sound smoother and rounder. Turn it down for a deep, synth-bass sound, "
      "up for more growl." },
    { "octave", "Tracking",
      "Helps the detector find your note. If the octaves glitch or flip, lower it for low notes or raise it for "
      "higher notes." },

    // Poly Octave
    { "polyoctave", "Sub 2",
      "The level of the voice two octaves below what you play, for a deep, rumbling bass. Works with chords "
      "too." },
    { "polyoctave", "Sub 1",
      "The level of the voice one octave below what you play, for an organ-like or bass-doubled sound. Works "
      "with chords too." },
    { "polyoctave", "Up 1",
      "The level of the voice one octave above what you play, for a 12-string guitar or organ-like shimmer. "
      "Works with chords too." },
    { "polyoctave", "Up 2",
      "The level of the voice two octaves above what you play, for a bright, glassy, bell-like sound. Works "
      "with chords too." },
    { "polyoctave", "Dry",
      "The level of your original signal. Keep it up and add octaves for a bigger sound, or turn it down for "
      "octave voices only." },
    { "polyoctave", "Tone",
      "Darkens the octave voices, which smooths the up octaves and makes the sub octaves rounder. All the way "
      "up it's off." },
    { "polyoctave", "Window", WINDOW_HELP },

    // Pitch Shifter
    { "pitch", "Shift 1", "How many semitones voice 1 is shifted, up to two octaves up or down. 12 is an octave." },
    { "pitch", "Level 1",
      "The volume of voice 1. Balance it against Dry: equal levels give an even harmony, a lower level a subtle "
      "one. At 0% voice 1 is off." },
    { "pitch", "Shift 2",
      "How many semitones voice 2 is shifted, up to two octaves up or down. 12 is an octave and 7 a fifth. Try "
      "+4 with voice 1 at +7 for a major chord." },
    { "pitch", "Level 2",
      "The volume of voice 2. At 0% voice 2 is off; raise it to add a second shifted voice, for example to make "
      "a chord with voice 1." },
    { "pitch", "Detune",
      "Shifts voice 1 slightly up and voice 2 slightly down, in cents. With the shifts at 0 and a few cents of "
      "detune, you get a thick, chorus-like doubling." },
    { "pitch", "Dry", "The volume of your original signal. Turn it to 0% to hear only the shifted voices." },
    { "pitch", "Window", WINDOW_HELP },
    { "pitch", "Tone",
      "Darkens the shifted voices, which smooths out any graininess and helps them sit behind your playing. All "
      "the way up it's off." },

    // Whammy
    { "whammy", "Position",
      "The pedal position, from no shift (0%) to the full Interval (100%). Map an expression pedal to it with MIDI "
      "learn and rock it for pitch bends and dive bombs." },
    { "whammy", "Interval",
      "How far the pitch bends at 100% Position, from two octaves up to two octaves down. Dive drops three octaves." },
    { "whammy", "Mode",
      "Whammy plays only the shifted sound, for bends. Harmony adds your dry signal, for a fixed harmony." },
    { "whammy", "Window", WINDOW_HELP },

    // Harmonizer
    { "harmonizer", "Key",
      "The key of the song. The harmonies stay in this key, so a 'third above' is major or minor as the key needs." },
    { "harmonizer", "Scale",
      "The scale of the song, together with Key: it decides which notes the harmonies use. Major and Minor "
      "cover most songs; the modes suit other styles." },
    { "harmonizer", "Voice 1",
      "How far voice 1 is from your note, in scale steps: +3rd is a third above, -4th a fourth below, and so on." },
    { "harmonizer", "Level 1",
      "The volume of voice 1. Set it a little below your dry signal so the harmony supports your playing "
      "instead of covering it. At 0% voice 1 is off." },
    { "harmonizer", "Voice 2", "How far voice 2 is from your note, in scale steps. Try +3rd and +5th for a triad." },
    { "harmonizer", "Level 2",
      "The volume of voice 2. At 0% voice 2 is off; raise it to add a second harmony and make a three-part "
      "chord with your playing." },
    { "harmonizer", "Dry",
      "The volume of your original signal. Keep it at 100% to play along with the harmonies; turn it down to "
      "hear only the harmony voices." },
    { "harmonizer", "Window", WINDOW_HELP },

    // Synth
    { "synth", "Wave",
      "The synth's basic sound. Square is hollow and woody, Saw is bright and buzzy, Pulse is nasal, and Sub adds a "
      "square wave an octave down for weight. Play single notes." },
    { "synth", "Octave",
      "Moves the synth up or down in octaves from the note you play. -1 or -2 give a synth bass; +1 a bright "
      "lead." },
    { "synth", "Cutoff",
      "How bright the synth's filter is before your playing opens it. Low settings are dark and muffled; high "
      "settings bright." },
    { "synth", "Resonance", "Adds a squelchy, whistling peak to the filter. High values give an acid-synth sound." },
    { "synth", "Env",
      "How far your playing opens the filter. Pick harder and the sound gets brighter, for a funky, quacky sound." },
    { "synth", "Attack",
      "How long each synth note takes to fade in. Short attacks follow your picking closely; long attacks give "
      "a soft, swelling pad." },
    { "synth", "Glide",
      "How long the synth slides from one note to the next. Short glide follows your playing tightly; longer "
      "glide gives a smooth portamento, like a vintage mono synth lead." },
    { "synth", "Mix", MIX_HELP("synth") },
    { "synth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "synth", "Track", TRACK_HELP },
    { "octave", "Track", TRACK_HELP },
    { "harmonizer", "Track", TRACK_HELP },
    { "polychip", "Track", TRACK_HELP },

    // Filter
    { "filter", "Type",
      "The kind of filter. Low-pass keeps the lows (a 'wah' sweep), Band-pass keeps a narrow band (vocal), "
      "High-pass keeps the highs (thin) and Notch removes a narrow band (phaser-like)." },
    { "filter", "Cutoff",
      "Where the filter sits: the middle of the sweep. Low settings sound dark and muffled, high settings "
      "bright and thin. The LFO moves the filter up and down around this point." },
    { "filter", "Resonance", "Adds a ringing peak at the cutoff. High values make the sweep more vocal and squelchy." },
    { "filter", "Shape",
      "How the filter moves: smooth Sine or Triangle waves, ramping Saw Up or Saw Down, jumping Square, or random "
      "Steps for a sample-and-hold sound." },
    { "filter", "Rate",
      "How fast the filter sweeps. Slow rates give a slow, evolving movement; fast rates a rhythmic, pulsing "
      "one. With Sync on, it picks a note value instead." },
    { "filter", "Sync", SYNC_HELP("rate") },
    { "filter", "Depth",
      "How far the filter sweeps up and down around the Cutoff. 0% holds it still, as a fixed filter; 100% "
      "sweeps three octaves each way, for a dramatic, rhythmic effect." },
    { "filter", "Mix", MIX_HELP("filtered sound") },

    // Wah
    { "wah", "Position",
      "The pedal position, from heel (0%, dark) to toe (100%, bright). Map an expression pedal to it with MIDI learn "
      "and rock it as you play. Parked halfway, it's a 'cocked wah' tone." },
    { "wah", "Type",
      "The voicing. Classic is the famous Cry Baby. Vox is smoother and vocal. Wide sweeps further. Bass is tuned "
      "lower for bass guitar." },
    { "wah", "Resonance",
      "How sharp and vocal the wah's peak is. 50% is the type's own setting; higher values give a more "
      "pronounced, quacky wah, lower values a smoother one." },
    { "wah", "Level",
      "The output volume of the block. A wah can change the level as it sweeps; use this to match the volume "
      "with bypass." },

    // Auto Wah
    { "autowah", "Sensitivity",
      "How strongly your playing moves the filter. Raise it until your picking opens the wah the way you want." },
    { "autowah", "Decay",
      "How quickly the filter falls back after each note. Short decays give a snappy, percussive quack on every "
      "pick; long decays make the filter glide down slowly, for a smoother, more vocal sweep." },
    { "autowah", "Low",
      "Where the filter sits when you play softly (or don't play). Low settings start dark and muffled, higher "
      "settings start brighter. The sweep moves between Low and High." },
    { "autowah", "High",
      "How far up the filter sweeps when you play hard. Higher settings give a brighter, more dramatic sweep; "
      "lower settings keep it subtle and warm. Keep it above Low." },
    { "autowah", "Resonance",
      "How sharp and vocal the sweep sounds. Low values are smooth and subtle; high values give a squelchy, "
      "quacky funk sound that really talks." },
    { "autowah", "Direction",
      "Up opens the filter as you play harder, the classic envelope filter. Down closes it instead." },
    { "autowah", "Filter",
      "The filter's voicing: Wah like a wah pedal, Low-pass for a bubbly, synth-like sound, Band-pass for a vocal "
      "sound, or High-pass for a thin, quirky one." },
    { "autowah", "Mix", MIX_HELP("filtered sound") },

    // Gated Fuzz
    { "gatedfuzz", "Drive",
      "How much fuzz. Low settings give a ragged, broken-up crunch; high settings a thick, saturated wall of fuzz "
      "with long sustain." },
    { "gatedfuzz", "Gate",
      "Cuts the fuzz off when your playing drops under a level, for abrupt, choppy stops and no hiss between "
      "notes. 0% is off; turn it up until the fuzz stops where you want, higher for a spitting, gated sound." },
    { "gatedfuzz", "Comp",
      "How smooth the fuzz is. High settings are full and compressed; turning it down starves the fuzz so quiet "
      "parts of each note break up into a sputtery, velcro-like rasp, especially as notes decay." },
    { "gatedfuzz", "Stab",
      "Stability. At 100% the fuzz is well behaved. Turn it down and it starts to feed back on itself, squealing "
      "and oscillating along with your notes, with higher squeals as you go lower. The squeal dies away when you "
      "stop playing." },
    { "gatedfuzz", "Tone",
      "Brightness of the fuzz. Turn it down for a dark, woolly sound; up for a cutting, buzzy one." },
    { "gatedfuzz", "Level",
      "Output volume of the pedal, without changing its sound. Set it so switching the fuzz on doesn't jump "
      "the volume. 0 dB leaves the level as it is." },

    // Envelope Drive
    { "envdrive", "Drive",
      "How much drive you get when you play softly. Keep it low for a clean or slightly gritty sound at a light "
      "touch, and let Range add the rest when you dig in." },
    { "envdrive", "Range",
      "How much more drive playing harder adds. Positive values get dirtier as you dig in (clean when soft, "
      "crunchy when hard); negative values do the opposite and clean up as you hit harder. 0% is a plain "
      "overdrive." },
    { "envdrive", "Sensitivity",
      "How easily your playing reaches the full Range. Turn it up if you play lightly or the signal is quiet; "
      "turn it down if the drive changes too easily and you want it only on hard picking." },
    { "envdrive", "Release",
      "How quickly the drive settles back after you dig in. Short times follow every pick closely; long times "
      "let the extra dirt linger through a phrase." },
    { "envdrive", "Tone",
      "Brightness of the drive. Turn it down for a smoother, darker sound; up for more bite and presence." },
    { "envdrive", "Level",
      "Output volume of the pedal, without changing its sound. The pedal already keeps soft notes soft and loud "
      "ones loud; use this to match it to the rest of the chain. 0 dB leaves the level as it is." },

    // Kill Switch
    { "killswitch", "Kill",
      "Mutes the sound while held, for kill-switch stutters played by hand (or silent until pressed, with "
      "Invert). Map a footswitch or a MIDI button to it with MIDI learn." },
    { "killswitch", "Stutter",
      "While held, chops the sound on and off at the Time rate, for machine-gun stutters. Map a footswitch or a "
      "MIDI button to it with MIDI learn." },
    { "killswitch", "Latch",
      "Off, Kill and Stutter work only while you hold them. On, each press switches them on, and the next press "
      "switches them off again, so you don't have to keep holding." },
    { "killswitch", "Invert",
      "Turns Kill around: the sound is muted until you press Kill, so pressing it lets notes through, like "
      "tapping out a rhythm on a switch." },
    { "killswitch", "Time",
      "The length of one on-off cycle of the stutter, in milliseconds (or a note value with Sync). Shorter "
      "times give a faster stutter." },
    { "killswitch", "Sync", SYNC_HELP("stutter time") },
    { "killswitch", "Duty",
      "How much of each stutter cycle sounds. 50% is even on and off; lower values give short, clipped blips, "
      "higher values only brief gaps." },
    { "killswitch", "Fade",
      "How quickly the sound cuts in and out, in milliseconds. Short fades give a hard, clicky chop; longer "
      "fades soften the edges." },
    { "killswitch", "Rate Mod",
      "Changes the stutter's speed while it runs. Off: a steady stutter at Time. Steps: the stutter doubles its "
      "speed in steps (with Time at 200 ms: 200, then 100, then 50 ms) and starts over, like a drum roll speeding "
      "up. Sweep: the speed glides smoothly through the same range instead of jumping." },
    { "killswitch", "Speeds",
      "How many speeds Rate Mod goes through, each twice as fast as the last. 3 with Time at 200 ms gives 200, 100 "
      "and 50 ms; 5 goes all the way to 12.5 ms." },
    { "killswitch", "Step Len",
      "How long Rate Mod stays at each speed, counted in cycles of Time: with Time at 200 ms, 2 holds each speed "
      "for 400 ms (2 chops at 200 ms, 4 at 100 ms, 8 at 50 ms). With Sync on, the steps land on the beat. Sweep "
      "takes as long to go through all the speeds." },
    { "killswitch", "Direction",
      "Which way Rate Mod goes. Faster: from Time to the fastest speed, then starts over. Slower: from the fastest "
      "speed down to Time. Both: faster, then slower again, back and forth (for Sweep, a smooth rise and fall in "
      "speed)." },

    // Barber Pole
    { "barberpole", "Rate",
      "How fast the sweep climbs (or falls), in sweeps per second. Slow rates give an endless, hypnotic rise; "
      "faster ones a spinning, rotating feel." },
    { "barberpole", "Sync", SYNC_HELP("rate") },
    { "barberpole", "Direction",
      "Whether the sweep seems to rise forever (Up) or fall forever (Down). Up feels like tension building; "
      "Down like sinking." },
    { "barberpole", "Stages",
      "How many notches the phaser makes. 4 stages is soft and subtle, 6 is the classic sound, 8 is deeper and "
      "more pronounced." },
    { "barberpole", "Feedback",
      "Adds resonance to the sweep, so it whistles and sounds more intense. Turn it down for a smooth, gentle "
      "effect." },
    { "barberpole", "Width", WIDTH_HELP("sweep") },
    { "barberpole", "Mix", MIX_HELP("phased sound") " Around 50% gives the deepest notches." },

    // Warble
    { "warble", "Depth",
      "How far the pitch wobbles, like a worn tape or a warped record. Low settings give a subtle, seasick "
      "drift; high settings a woozy, detuned wobble." },
    { "warble", "Rate",
      "How fast the slow wobble goes, in cycles per second. Slow rates sound like a warped record; faster ones "
      "more like a vibrato." },
    { "warble", "Flutter",
      "A faster, finer wobble on top, like a cheap tape transport. Adds a nervous, lo-fi shimmer to the pitch." },
    { "warble", "Random",
      "How uneven the wobble is. 0% swings back and forth evenly; higher values make it wander unpredictably, "
      "like old tape." },
    { "warble", "Tone",
      "Darkens the sound like worn tape or an old record. Turn it down for a muffled, nostalgic sound. All the "
      "way up it's off." },
    { "warble", "Drive",
      "Adds a little tape saturation, rounding off peaks and thickening the sound. 0% is clean." },
    { "warble", "Mix", MIX_HELP("wobbling sound") " At 100% you hear only the wobbling pitch; lower values give a "
                       "chorus-like blend." },

    // Slicer
    { "slicer", "Step 1", "The volume of step 1 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 2", "The volume of step 2 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 3", "The volume of step 3 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 4", "The volume of step 4 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 5", "The volume of step 5 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 6", "The volume of step 6 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 7", "The volume of step 7 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Step 8", "The volume of step 8 of the pattern. 100% is full volume and 0% is silent; set the steps "
                          "to tap out the rhythm you want." },
    { "slicer", "Steps",
      "How many steps the pattern uses before it starts again. Use 3 or 6 for triplet feels, 4 or 8 for straight "
      "ones, and odd lengths for patterns that drift against the beat." },
    { "slicer", "Time",
      "The length of each step, in milliseconds (or a note value with Sync, like 1/16). Shorter steps make a "
      "faster pattern." },
    { "slicer", "Sync", SYNC_HELP("step time") " While the DAW plays, step 1 lands on the bar." },
    { "slicer", "Smooth",
      "How softly the steps change. 0% gives hard, choppy edges; higher values glide between levels for a "
      "pulsing, tremolo-like pattern." },
    { "slicer", "Depth",
      "How strong the pattern is. 100% goes all the way to each step's level; lower values only dip the volume, "
      "for a subtler rhythm." },

    // Bloom Reverb
    { "bloomreverb", "Decay",
      "How long the reverb tail lasts, in seconds. Long decays with a slow Bloom give big, swelling ambient "
      "washes." },
    { "bloomreverb", "Bloom",
      "How slowly the reverb swells up after each new note. Short times sound like a normal reverb; long times "
      "keep the pick attack dry and let the reverb rise up behind the note." },
    { "bloomreverb", "Size",
      "How big the space sounds. Small sizes are more intimate; larger sizes give a slower, more spread-out "
      "tail." },
    { "bloomreverb", "Tone",
      "Brightness of the tail. Turn it down for a dark, soft bloom; up for more air and sparkle." },
    { "bloomreverb", "Low Cut",
      "Removes the lows from the reverb only, so the swell doesn't muddy your playing. Raise it for a lighter, "
      "airier bloom." },
    { "bloomreverb", "Mix", MIX_HELP("reverb") },

    // Gated Reverb
    { "gatedreverb", "Size",
      "How big the space sounds. Smaller sizes give a tight, punchy burst; larger ones a bigger, fuller one." },
    { "gatedreverb", "Decay",
      "How long the reverb would ring without the gate. Longer decays keep the burst dense and loud right up to "
      "the cut." },
    { "gatedreverb", "Threshold",
      "How loud your playing must be to keep the gate open. Lower settings keep the reverb open on quiet playing "
      "too; higher settings only let loud hits through." },
    { "gatedreverb", "Hold",
      "How long the reverb stays open after your playing drops under the Threshold, before it's cut. Short holds "
      "give a tight, clipped burst; long ones a bigger splash." },
    { "gatedreverb", "Release",
      "How quickly the gate closes at the end of the hold. Short releases give the classic abrupt 80s cut; "
      "longer ones a smoother fade." },
    { "gatedreverb", "Tone",
      "Brightness of the reverb. Turn it down for a darker burst; up for a brighter, splashier one." },
    { "gatedreverb", "Mix", MIX_HELP("reverb") },

    // Reverse Delay
    { "reversedelay", "Time",
      "The length of each reversed slice, in milliseconds (or a note value with Sync). Longer times play longer "
      "phrases backwards; shorter times give quick reverse swells." },
    { "reversedelay", "Sync", SYNC_HELP("time") },
    { "reversedelay", "Feedback",
      "How many times the reversed sound comes round again. Low settings give one backwards echo; high settings "
      "keep it going into a swirling, psychedelic wash." },
    { "reversedelay", "Mix", MIX_HELP("reversed sound") " At 100% you hear only the backwards sound." },
    { "reversedelay", "Tone",
      "Darkens the reversed repeats so they sit behind your playing. All the way up it's off." },

    // Pitch Delay
    { "pitchdelay", "Time",
      "The time between repeats, in milliseconds (or a note value with Sync). Each repeat steps the pitch once "
      "more, so shorter times climb faster." },
    { "pitchdelay", "Sync", SYNC_HELP("time") },
    { "pitchdelay", "Feedback",
      "How many repeats you hear. Each repeat is shifted again, so more feedback climbs (or falls) further." },
    { "pitchdelay", "Mix", MIX_HELP("repeats") },
    { "pitchdelay", "Shift",
      "How far each repeat is shifted, in semitones. +12 makes each repeat an octave higher than the last, +7 "
      "climbs in fifths, negative values fall. 0 with a little Fine gives a slowly drifting pitch." },
    { "pitchdelay", "Fine",
      "Adds a small shift to each repeat, in cents. On its own (Shift 0) it makes the repeats drift slowly out of "
      "tune, for a wobbly, dreamy sound." },
    { "pitchdelay", "Tone",
      "Darkens each repeat, so climbing repeats fade out softly instead of turning shrill. All the way up it's "
      "off." },

    // Beat Repeat
    { "beatrepeat", "Repeat",
      "While held, repeats the last Slice of what you played over and over, instead of your playing; let go and "
      "you're back. Map a footswitch or a MIDI button to it with MIDI learn." },
    { "beatrepeat", "Latch",
      "Off, the repeats last only while you hold Repeat. On, one press starts them and the next stops them." },
    { "beatrepeat", "Slice",
      "How long the repeated slice is, in milliseconds (or a note value with Sync). Short slices give a buzzing, "
      "glitchy stutter; long ones repeat whole phrases." },
    { "beatrepeat", "Sync", SYNC_HELP("slice") },
    { "beatrepeat", "Gate",
      "How much of each repeat sounds. 100% plays the whole slice; lower values chop each repeat short for a "
      "staccato stutter." },
    { "beatrepeat", "Decay",
      "How much quieter each repeat gets. 0% repeats at full volume for as long as you hold; higher values fade "
      "the repeats away." },
    { "beatrepeat", "Pitch",
      "Shifts each repeat by this many semitones from the last, like a tape speeding up or slowing down. 0 keeps "
      "the pitch; -1 or -2 gives a falling, tape-stop-like stutter." },
    { "beatrepeat", "Reverse",
      "Plays each repeat backwards, for reversed, sucked-in stutters. Combine it with a short Slice for a "
      "glitchy, scratchy effect." },
    { "beatrepeat", "Mix", MIX_HELP("repeats") " At 100% the repeats replace your playing while held; lower values "
                           "keep some of your playing under them." },

    // Detune
    { "detune", "Detune",
      "How far the voices are from your note, in cents (hundredths of a semitone). 5-15 cents thickens the "
      "sound like a double-tracked part; more gives an out-of-tune, wobbly character." },
    { "detune", "Voices",
      "Which voices play: Both (one sharp and one flat, the richest), Up (sharp only) or Down (flat only)." },
    { "detune", "Width", WIDTH_HELP("voices") " The sharp voice leans left and the flat one right." },
    { "detune", "Mix", MIX_HELP("detuned voices") },
    { "detune", "Window", WINDOW_HELP },

    // Capo
    { "capo", "Shift",
      "How many semitones everything is shifted. -1 is half-step-down tuning, -2 a whole step down, +2 like a "
      "capo at the second fret. Chords shift too." },
    { "capo", "Fine",
      "A small extra shift in cents, to match a recording or an instrument that isn't at concert pitch." },
    { "capo", "Dry",
      "Mixes in your unshifted sound. Keep it at 0% for a capo or a drop tuning; raise it to play along with the "
      "shifted sound as a harmony." },
    { "capo", "Window", WINDOW_HELP },

    // Arpeggiator
    { "arpeggiator", "Chord",
      "The notes the pattern steps through, built on the note you play: octaves, or the notes of a major, minor, "
      "power, sus4 or seventh chord." },
    { "arpeggiator", "Pattern",
      "The order the notes play in: Up, Down, Up-Down (up then back down), or Random for an unpredictable, "
      "sequencer-like line." },
    { "arpeggiator", "Range",
      "Whether the pattern covers one octave or climbs through two. Two octaves gives longer, sweeping runs." },
    { "arpeggiator", "Rate",
      "How fast the pattern steps, in steps per second (or a note value with Sync, like 1/16)." },
    { "arpeggiator", "Sync", SYNC_HELP("rate") " While the DAW plays, the steps land on the beat." },
    { "arpeggiator", "Gate",
      "How much of each step sounds. 100% runs the notes together; lower values give short, staccato blips." },
    { "arpeggiator", "Glide",
      "Slides from one note to the next instead of jumping, in milliseconds. 0 jumps straight to each note." },
    { "arpeggiator", "Mix", MIX_HELP("arpeggio") " At 100% you hear only the pattern." },
    { "arpeggiator", "Window", WINDOW_HELP },

    // Organ
    { "organ", "Sub",
      "The level of an octave below your note, like an organ's 16-foot drawbar. Adds weight and depth." },
    { "organ", "Fund",
      "The level of your own note, like the 8-foot drawbar: the core of the organ sound." },
    { "organ", "Octave",
      "The level of an octave above, like the 4-foot drawbar. Adds brightness and a classic organ shimmer." },
    { "organ", "12th",
      "The level of an octave and a fifth above, like the 2 2/3-foot drawbar. Adds a hollow, churchy colour." },
    { "organ", "2 Oct",
      "The level of two octaves above, like the 2-foot drawbar. Adds sparkle and a whistling top." },
    { "organ", "Attack",
      "How quickly each note swells in. Short times keep some of the pick; longer ones hide it completely, like "
      "pressing an organ key slowly." },
    { "organ", "Tone",
      "Brightness of the organ. Turn it down for a soft, mellow sound; up for a brighter, buzzier one. All the "
      "way up it's off." },
    { "organ", "Dry",
      "Mixes in your normal guitar sound under the organ, pick attack and all. 0% is organ only." },
    { "organ", "Window", WINDOW_HELP },

    // Sustainer
    { "sustainer", "Sustain",
      "How long notes are held. Higher settings keep each note at the level you picked it for much longer, like "
      "an E-Bow; 0% lets notes fade naturally." },
    { "sustainer", "Bloom",
      "How long a held note takes to grow into its harmonic, like an amp slowly starting to feed back." },
    { "sustainer", "Harmonic",
      "The note the sustain blooms into: an octave up, an octave and a fifth, two octaves, or a fifth." },
    { "sustainer", "Feedback",
      "How far the note blooms into the harmonic. Low settings add a hint of it; 100% lets the harmonic take "
      "over, like screaming feedback." },
    { "sustainer", "Tone",
      "Brightness of the sustained sound. Turn it down to soften the bloom; all the way up it's off." },
    { "sustainer", "Mix", MIX_HELP("sustained sound") },

    // Robot Filter
    { "robotfilter", "Type",
      "Low-pass keeps the lows and moves the top end; Band-pass keeps only a narrow, vocal-sounding band." },
    { "robotfilter", "Source",
      "Where each new cutoff comes from: Random, a random pattern that repeats every eight steps (Loop 8), or "
      "how hard you're playing at each step (Envelope)." },
    { "robotfilter", "Low",
      "The lowest cutoff a step can land on. Together with High it sets the range the filter jumps around in." },
    { "robotfilter", "High",
      "The highest cutoff a step can land on. Together with Low it sets the range the filter jumps around in." },
    { "robotfilter", "Resonance",
      "How sharp and vocal the filter sounds. Higher settings give the bubbling, robotic squelch; lower ones a "
      "smoother tone change." },
    { "robotfilter", "Rate",
      "How often the filter jumps to a new cutoff, in steps per second (or a note value with Sync)." },
    { "robotfilter", "Sync", SYNC_HELP("rate") " While the DAW plays, the steps land on the beat." },
    { "robotfilter", "Glide",
      "Slides from one step to the next instead of jumping. 0% is the classic hard sample-and-hold; more glides "
      "into a smooth, wandering filter." },
    { "robotfilter", "Mix", MIX_HELP("filtered sound") },

    // Talk Box
    { "talkbox", "Position",
      "Where along the vowels the filter sits, from the first vowel to the last. Map an expression pedal to it "
      "with MIDI learn to make the guitar talk. With Envelope it's where it rests; with LFO, how far it sweeps." },
    { "talkbox", "Vowels",
      "The vowels Position moves through: Oo-Ah (the classic talk box), Ee-Ah, Oo-Ee, or all five vowels." },
    { "talkbox", "Source",
      "What moves the vowels: Pedal (the Position knob), Envelope (your picking: harder playing opens it) or LFO "
      "(a steady sweep at Rate)." },
    { "talkbox", "Sensitivity",
      "With Envelope: how easily your playing opens the vowel. Turn it up if you play lightly." },
    { "talkbox", "Rate",
      "With LFO: how fast the vowels sweep, in cycles per second. Slow rates give a lazy wah-wah; fast ones a "
      "babbling, yammering voice." },
    { "talkbox", "Resonance",
      "How sharp the vowels are. Higher settings sound more like a mouth and more pronounced; lower ones are "
      "softer and more subtle." },
    { "talkbox", "Mix", MIX_HELP("talking sound") },
    { "talkbox", "Level",
      "Output volume of the effect, to make up for the volume the vowels take away. 0 dB leaves it as it is." },

    // Super Saw
    { "supersaw", "Detune",
      "How far apart the seven saws are tuned. A little gives a rich, chorused tone; more gives the huge, "
      "shimmering trance-lead sound; very high values sound wild and out of tune." },
    { "supersaw", "Width", WIDTH_HELP("saws") },
    { "supersaw", "Cutoff",
      "The filter's brightness when you play softly. Turn it down for a darker, warmer pad; up for a bright, "
      "cutting lead." },
    { "supersaw", "Resonance",
      "Emphasis at the filter's cutoff. Low values sound smooth; higher values add a vocal, whistling peak." },
    { "supersaw", "Env",
      "How much harder playing opens the filter. Higher values make the synth brighten as you dig in, like a real "
      "instrument." },
    { "supersaw", "Attack", SYNTH_ATTACK_HELP },
    { "supersaw", "Release", SYNTH_RELEASE_HELP },
    { "supersaw", "Octave", SYNTH_OCTAVE_HELP },
    { "supersaw", "Glide", SYNTH_GLIDE_HELP },
    { "supersaw", "Mix", SYNTH_MIX_HELP },
    { "supersaw", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "supersaw", "Track", TRACK_HELP },

    // FM Synth
    { "fmsynth", "Ratio",
      "The modulator's pitch against your note, which picks the kind of sound: 1:1 and 1:2 give electric pianos "
      "and brassy tones, 1:0.5 deep basses, 1:3.5 and 1:7 bells and metallic, clangy sounds." },
    { "fmsynth", "Index",
      "How bright and complex the tone is at the pick. 0 is a pure sine; higher values add more and more "
      "harmonics, up to buzzy and metallic." },
    { "fmsynth", "Decay",
      "How quickly the brightness falls after each pick, from Index down to Sustain. Short decays give plucked, "
      "piano-like notes; long ones keep the tone bright." },
    { "fmsynth", "Sustain",
      "How much of the brightness stays while the note is held, after the Decay. 0% fades to a pure, soft tone; "
      "100% keeps it as bright as the pick." },
    { "fmsynth", "Feedback",
      "Lets the modulator modulate itself, adding grit and noise-like edges to the tone. 0% is clean." },
    { "fmsynth", "Attack", SYNTH_ATTACK_HELP },
    { "fmsynth", "Release", SYNTH_RELEASE_HELP },
    { "fmsynth", "Octave", SYNTH_OCTAVE_HELP },
    { "fmsynth", "Glide", SYNTH_GLIDE_HELP },
    { "fmsynth", "Mix", SYNTH_MIX_HELP },
    { "fmsynth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "fmsynth", "Track", TRACK_HELP },

    // Fold Synth
    { "foldsynth", "Fold",
      "How hard the sine is folded. Low values are soft and round; higher values fold the wave over and over into "
      "bright, rich, ringing harmonics." },
    { "foldsynth", "Env",
      "How much digging in adds to the fold, so harder picks sound brighter and more complex. 0% keeps the same "
      "tone whatever you play." },
    { "foldsynth", "Symmetry",
      "Shifts the wave before folding, so it folds unevenly. Away from 0% it adds hollow, even harmonics and a "
      "more nasal tone." },
    { "foldsynth", "Gate",
      "Follow makes the synth follow your playing's loudness. Pluck pings on each pick and dies away over Decay, "
      "darkening as it fades, for a woody, bongo-like sound." },
    { "foldsynth", "Decay",
      "With Pluck: how long each ping takes to die away. Short decays give percussive plinks; long ones ring like "
      "a struck bar." },
    { "foldsynth", "Tone",
      "How bright the gate gets when fully open. Lower it for a softer, warmer sound; raise it for more sparkle." },
    { "foldsynth", "Octave", SYNTH_OCTAVE_HELP },
    { "foldsynth", "Glide", SYNTH_GLIDE_HELP },
    { "foldsynth", "Mix", SYNTH_MIX_HELP },
    { "foldsynth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "foldsynth", "Track", TRACK_HELP },

    // Acid Synth
    { "acidsynth", "Wave",
      "Saw is the buzzier, classic acid sound; Square is hollower and rounder, with a woodier, more reedy squelch." },
    { "acidsynth", "Cutoff",
      "The filter's resting point. Low settings give a dark, rubbery bass; higher ones a brighter tone. The "
      "envelope opens it from here on each pick." },
    { "acidsynth", "Resonance",
      "How much the filter squeals at its cutoff. High settings give the famous squelchy, rubbery acid sound." },
    { "acidsynth", "Env Mod",
      "How far the filter snaps open on each pick before it closes again. More gives a bigger 'wow' on every "
      "note." },
    { "acidsynth", "Decay",
      "How quickly the filter closes after each pick. Short decays give tight, plucky blips; long ones a slow, "
      "wah-like sweep." },
    { "acidsynth", "Accent",
      "How much harder picks open the filter further and make it ring more. Dig in on some notes to make them "
      "jump out of the line." },
    { "acidsynth", "Drive",
      "Overdrives the oscillator into the filter, for a thicker, more aggressive acid sound." },
    { "acidsynth", "Octave", SYNTH_OCTAVE_HELP },
    { "acidsynth", "Glide",
      "The slide between notes. Acid lines live on slides: around 50-100 ms gives the classic slurred bass "
      "line; short times jump between notes." },
    { "acidsynth", "Mix", SYNTH_MIX_HELP },
    { "acidsynth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "acidsynth", "Track", TRACK_HELP },

    // String Machine
    { "stringsynth", "Low",
      "The level of the voice an octave below your note. Adds body and warmth, like a cello section." },
    { "stringsynth", "Mid",
      "The level of the voice at your note: the core of the string sound, like a viola section." },
    { "stringsynth", "High",
      "The level of the voice an octave above your note. Adds a bright, airy violin shimmer." },
    { "stringsynth", "Ensemble",
      "The string machine's chorus, which makes one voice sound like a whole section. Turn it up for a lush, "
      "swirling 70s sound; down for a thinner, organ-like tone." },
    { "stringsynth", "Tone",
      "Brightness of the strings. Turn it down for soft, distant strings; up for a brighter, buzzier sound." },
    { "stringsynth", "Attack", SYNTH_ATTACK_HELP },
    { "stringsynth", "Release", SYNTH_RELEASE_HELP },
    { "stringsynth", "Octave", SYNTH_OCTAVE_HELP },
    { "stringsynth", "Mix", SYNTH_MIX_HELP },
    { "stringsynth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "stringsynth", "Track", TRACK_HELP },

    // Chip Synth
    { "chipsynth", "Wave",
      "The console's waveforms: thin, nasal pulses at 12% and 25%, a hollow 50% square, or the stepped, "
      "flute-like triangle used for basses." },
    { "chipsynth", "Arp",
      "Plays a fast arpeggio from your note, the way old games faked chords: an octave, a major or minor chord, or "
      "a fifth. Off plays your note only." },
    { "chipsynth", "Arp Rate",
      "How fast the arpeggio cycles, in notes per second. Fast rates blur into a chord-like warble; slower ones "
      "sound like separate notes." },
    { "chipsynth", "Quantize",
      "Snaps your notes to the nearest semitone, as a game console plays them, so bends jump in steps. Turn it off "
      "to follow your bends and vibrato smoothly." },
    { "chipsynth", "Vibrato",
      "A vibrato that fades in shortly after each note, like a game's held lead notes. 0% is off." },
    { "chipsynth", "Crush",
      "The console's coarse volume and sample steps. Turn it up for a grittier, more lo-fi 8-bit sound; 0% is "
      "clean." },
    { "chipsynth", "Octave", SYNTH_OCTAVE_HELP },
    { "chipsynth", "Mix", SYNTH_MIX_HELP },
    { "chipsynth", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "chipsynth", "Track", TRACK_HELP },

    // Poly Chip
    { "polychip", "Wave",
      "The console's waveforms: thin, nasal pulses at 12% and 25%, a hollow 50% square, or the stepped, "
      "flute-like triangle used for basses." },
    { "polychip", "Voices",
      "How many notes can sound at once. 3 or 4 suit most chords; 1 plays only the loudest note, like the mono "
      "Chip Synth. Old consoles had 2 or 3." },
    { "polychip", "Focus",
      "How hard it ignores the overtones of your notes. A guitar string also rings at an octave, an octave and a "
      "fifth and so on above its note, and those can turn into extra synth notes. Raise Focus if you hear notes you "
      "didn't play; lower it if chord notes an octave or a fifth above another go missing." },
    { "polychip", "Arp",
      "Plays the notes of your chord one after another instead of together, the way game music does it: Up from the "
      "lowest note, Down from the highest. Off plays the whole chord." },
    { "polychip", "Arp Rate",
      "How fast the arpeggio steps through your chord, in notes per second. Fast rates blur into a shimmering chord; "
      "slower ones sound like separate notes." },
    { "polychip", "Release", SYNTH_RELEASE_HELP },
    { "polychip", "Crush",
      "The console's coarse volume and sample steps. Turn it up for a grittier, more lo-fi 8-bit sound; 0% is "
      "clean." },
    { "polychip", "Octave", SYNTH_OCTAVE_HELP },
    { "polychip", "Mix", SYNTH_MIX_HELP },
    { "polychip", "Quantize",
      "Snaps every note to the nearest semitone, as a game console plays them, so bends jump in steps. Turn it off "
      "to follow your bends, slides and vibrato smoothly at the pitch you play." },
    { "polychip", "Pick Filter",
      "Keeps the click of your pick from turning into short, squeaky high notes. A new note has to last this long "
      "before it sounds. Raise it if you hear chirps at the start of notes; lower it for a faster response. Around "
      "10-20 ms suits most playing; 0 is off." },
    // Third set
    { "sympathetic", "Key", "The key the strings are tuned to. Set it to your song's key, so the notes you play most set them ringing." },
    { "sympathetic", "Tuning",
      "How the twelve strings are tuned in that key. Drone: the root and fifth over four octaves, like a sitar's "
      "drone strings. Major and Minor: the chord's notes. Sitar: a raga's scale, so most notes find a string. "
      "Open: a guitar's open strings, twice (with the key as the low string)." },
    { "sympathetic", "Decay",
      "How long the strings ring once something has set them going. Long decays build a big, harp-like halo; short "
      "ones a subtle shimmer that follows your playing." },
    { "sympathetic", "Damping",
      "How quickly the strings' highs die away. Low damping rings bright and metallic; high damping is soft and "
      "warm, like strings with old, dull wire." },
    { "sympathetic", "Excite", "How strongly your playing sets the strings ringing. Raise it for a louder, faster bloom." },
    { "sympathetic", "Width", WIDTH_HELP("strings") },
    { "sympathetic", "Level",
      "How loud the ringing strings are, added to your guitar (which stays at full level). Start low and bring it "
      "up until you hear the halo." },
    { "jawari", "Buzz",
      "How much the string buzzes against the curved bridge. A little adds a bright, sitar-like sizzle; a lot "
      "gives the full twang." },
    { "jawari", "Bloom",
      "Makes the buzz grow as each note fades, the way a sitar's buzz opens up as the string settles onto the "
      "bridge. 0% keeps the buzz the same all the way through." },
    { "jawari", "Color", "The pitch of the resonance the buzz rings through: lower is nasal and hollow, higher is bright and fizzy." },
    { "jawari", "Mix", MIX_HELP("buzzing sound") },
    { "jawari", "Level",
      "The output level. Use it to match the volume with the pedal switched on and off, or to drive the next "
      "pedal." },
    { "graincloud", "Size",
      "The length of each grain. Short grains sound grainy and glittery; long ones blur into smooth, overlapping "
      "washes." },
    { "graincloud", "Density",
      "How many grains play per second. Low densities give scattered sparkles; high densities a thick, continuous "
      "cloud." },
    { "graincloud", "Delay", "How far back in time the grains start. Raise it to let the cloud trail behind your playing." },
    { "graincloud", "Spray",
      "How widely the grains are scattered in time on top of Delay. Higher spray mixes moments from further back, "
      "for a more diffuse, smeared cloud." },
    { "graincloud", "Pitch",
      "Shifts the grains: up an octave, down one, either at random, up a fifth, a mix of unison, fifth and octave, "
      "or a random choice of fifths and octaves for each grain. 0 keeps them at your pitch." },
    { "graincloud", "Reverse", "The share of grains played backwards. Backwards grains swell in, for a dreamier cloud." },
    { "graincloud", "Feedback",
      "Feeds the cloud back into what the grains read, so it builds on itself into dense, evolving textures. High "
      "settings grow long tails." },
    { "graincloud", "Width", WIDTH_HELP("grains") },
    { "graincloud", "Freeze",
      "Stops recording: the grains keep playing what's already in the buffer, so you can hold a cloud and play "
      "over it." },
    { "graincloud", "Mix", MIX_HELP("grain cloud") },
    { "swarm", "Voices",
      "How many voices the swarm has. More voices make a thicker, busier sound; fewer sound like a few players." },
    { "swarm", "Spread",
      "The range of the voices' short delays. Small spreads sound like a doubled or 12-string guitar; larger ones "
      "come apart into a crowd." },
    { "swarm", "Drift",
      "How far each voice wanders in pitch. A little gives a lush chorus; a lot sounds out of tune and alive, like "
      "a swarm." },
    { "swarm", "Speed", "How fast the voices wander. Slow drift is a gentle shimmer; fast drift a nervous, buzzing swirl." },
    { "swarm", "Buzz",
      "A fast flutter in each voice's level, like beating wings. Raise it all the way, with high Drift, for a "
      "beehive." },
    { "swarm", "Tone",
      "Rolls off the swarm's highs. Lower it for a softer, darker swarm that sits behind your playing." },
    { "swarm", "Width", WIDTH_HELP("voices") },
    { "swarm", "Mix", MIX_HELP("swarm") },
    { "tapestop", "Stop",
      "Hold to slow the sound to a halt, like a turntable losing power; let go and it spins back up. Map it to a "
      "footswitch or MIDI button. With Latch, a press stops it and the next press starts it again." },
    { "tapestop", "Latch",
      "Makes Stop switch on and off with each press, instead of only while held: one press stops, the next "
      "starts." },
    { "tapestop", "Stop Time",
      "How long the slowdown takes, from full speed to a stop. Short times give a quick 'record scratch' drop." },
    { "tapestop", "Start Time",
      "How long the spin back up takes when you let go. 0 jumps straight back to your playing; longer times give "
      "a slow, warping start that lands back in time." },
    { "tapestop", "Curve",
      "The shape of the slowdown and spin-up. 0% is a steady slope; higher settings drop quickly at first and "
      "then crawl, like a heavy platter coasting to a stop." },
    { "bouncedelay", "Time",
      "The first gap between echoes (with Fall) or the last (with Rise), in milliseconds (or a note value with "
      "Sync)." },
    { "bouncedelay", "Sync", SYNC_HELP("bounce time") },
    { "bouncedelay", "Gravity",
      "How quickly the bounces speed up: each gap is this much shorter than the last. Low gravity is a light ball "
      "bouncing on; high gravity settles fast into a quick rattle." },
    { "bouncedelay", "Direction",
      "Fall: the echoes come faster and faster, like a ball bouncing to rest. Rise: the other way round, starting "
      "with a fast rattle and spreading out." },
    { "bouncedelay", "Bounces",
      "How many echoes the ball makes before it comes to rest. More bounces give a longer, finer rattle at the "
      "end." },
    { "bouncedelay", "Bounce",
      "How loud each bounce is compared with the one before. 100% keeps them all equal; lower values let them die "
      "away." },
    { "bouncedelay", "Tone", "How bright the bounces are. Each bounce is a little darker than the last, like a ball losing energy." },
    { "bouncedelay", "Spread",
      "Bounces the echoes alternately left and right, so the ball seems to hop across the stereo field. Stereo "
      "mode only." },
    { "bouncedelay", "Mix", MIX_HELP("echoes") },
    { "patterndelay", "Tap 1", PATTERN_TAP_HELP("one step") },
    { "patterndelay", "Tap 2", PATTERN_TAP_HELP("two steps") },
    { "patterndelay", "Tap 3", PATTERN_TAP_HELP("three steps") },
    { "patterndelay", "Tap 4", PATTERN_TAP_HELP("four steps") },
    { "patterndelay", "Tap 5", PATTERN_TAP_HELP("five steps") },
    { "patterndelay", "Tap 6", PATTERN_TAP_HELP("six steps") },
    { "patterndelay", "Tap 7", PATTERN_TAP_HELP("seven steps") },
    { "patterndelay", "Tap 8", PATTERN_TAP_HELP("eight steps") },
    { "patterndelay", "Time",
      "The length of one step of the pattern, in milliseconds (or a note value with Sync). A 16th note gives the "
      "classic rhythmic delay patterns." },
    { "patterndelay", "Sync", SYNC_HELP("step time") },
    { "patterndelay", "Feedback", "Plays the whole pattern again after its eighth step, quieter each time. 0% plays it once." },
    { "patterndelay", "Pitch",
      "Shifts every other echo: an octave up, an octave down or a fifth up; Up + Down sends alternate echoes up "
      "and down an octave, for cascading arpeggios." },
    { "patterndelay", "Spread",
      "Bounces the echoes alternately left and right, for a wide, ping-pong rhythm. Only works in Stereo mode." },
    { "patterndelay", "Tone",
      "How bright the echoes are. Lower it for darker, more distant repeats that sit behind your playing." },
    { "patterndelay", "Mix", MIX_HELP("echoes") },
    { "chaosmod", "Speed", "How fast the chaos moves. Low speeds drift slowly; high speeds wobble and churn." },
    { "chaosmod", "Chaos",
      "How wild the motion is. Low settings swing in fairly regular loops; high settings jump unpredictably "
      "between them." },
    { "chaosmod", "Filter",
      "How far the chaos sweeps the filter, up to three octaves either side of Cutoff. 0% leaves the filter off." },
    { "chaosmod", "Cutoff",
      "The filter's centre point. The chaos sweeps it up and down from here; set it high to leave only the "
      "volume and pitch." },
    { "chaosmod", "Resonance",
      "Adds a peak at the filter's cutoff, so its movement sings and whistles. High settings sound vocal and "
      "squelchy." },
    { "chaosmod", "Volume",
      "How much the chaos moves the volume, for an unpredictable tremolo that swells and dips on its own." },
    { "chaosmod", "Pitch",
      "How much the chaos bends the pitch, for a wobbly, seasick vibrato that never settles into a pattern." },
    { "chaosmod", "Width",
      "Lets the right channel follow its own chaos, which drifts away from the left's, for a wide, moving stereo "
      "image. 0% moves both sides together." },
    { "chaosmod", "Mix", MIX_HELP("chaos-moved sound") },
    { "acoustic", "Body",
      "The kind of acoustic guitar: Dreadnought (big and deep), Jumbo (boomier still), Parlor (small, focused "
      "mids) or Classical (warm nylon strings)." },
    { "acoustic", "Pickup",
      "The pickup you're playing through, so its colour can be undone: Single Coil, Humbucker, or Neutral (for "
      "piezo or an already-flat signal)." },
    { "acoustic", "Resonance", "How strongly the body resonates. Raise it for a bigger, boxier acoustic; lower it for a subtler effect." },
    { "acoustic", "Top", "The strings' sparkle: the bright top end of an acoustic. Lower it if it sounds harsh." },
    { "acoustic", "Bass",
      "Adds or takes away low end, for a fuller, boomier acoustic or a tighter one that sits better in a band "
      "mix." },
    { "acoustic", "Width", "Adds a little room around the guitar, spread across the stereo field. Only works in Stereo mode." },
    { "acoustic", "Mix", MIX_HELP("acoustic sound") },
    { "acoustic", "Level",
      "The output level. Use it to match the volume with the pedal switched on and off, or to drive the next "
      "pedal." },
    { "strum", "Chord",
      "Which chord each note becomes. Diatonic builds it from Key and Scale, so every note gets the chord that "
      "belongs in the key. The others are fixed shapes on top of your note: major, minor, power chord, sus4, "
      "dominant 7th, major 7th, minor 7th, or octaves." },
    { "strum", "Key",
      "The key for Diatonic chords. Set it to your song's key, so each note gets the chord that belongs to it." },
    { "strum", "Scale",
      "Major or minor key, for Diatonic chords: it decides which notes are major, minor or diminished chords." },
    { "strum", "Notes", "How many notes are added to the one you play: 2 for a triad, 3 or 4 for fuller chords." },
    { "strum", "Strum",
      "The time between strings in the strum. Short times sound like a fast, crisp strum; long ones like slowly "
      "rolling a chord. 0 plays them all at once." },
    { "strum", "Direction", "Down strums from low to high, Up from high to low, Alternate switches each time you pick." },
    { "strum", "Level",
      "How loud the added notes of the chord are, against the note you play (Dry). Lower it for a subtler "
      "chord." },
    { "strum", "Dry",
      "How loud the note you play is, under the strummed notes. Turn it down to hear only the added chord "
      "notes." },
    { "strum", "Window", WINDOW_HELP },
    { "strum", "Track", TRACK_HELP },
    { "hardtune", "Key",
      "The key the notes are pulled into. Set it to your song's key, so every note lands on one that belongs." },
    { "hardtune", "Scale",
      "Which notes are allowed. Chromatic pulls to the nearest semitone; the others only to the notes of that scale "
      "in the key." },
    { "hardtune", "Speed",
      "How quickly the pitch is corrected. 0 snaps at once, for the robotic hard-tune effect; longer times glide "
      "gently into the note, for subtle correction that keeps your bends and vibrato." },
    { "hardtune", "Amount", "How far the pitch is pulled: 100% all the way to the note, less for only part of the way." },
    { "hardtune", "Mix", MIX_HELP("corrected sound") },
    { "hardtune", "Window", WINDOW_HELP },
    { "hardtune", "Track", TRACK_HELP },
    { "drumtrigger", "Mode",
      "Which drum each hit plays. Auto picks by how bright the hit is: low thumps play the kick, brighter hits "
      "the snare, the brightest the hi-hat. Kick, Snare and Hi-Hat play only that drum; Alternate switches between "
      "kick and snare." },
    { "drumtrigger", "Split",
      "Where Auto draws the line between kick and snare. Raise it if too many hits play the snare; lower it if "
      "too many play the kick." },
    { "drumtrigger", "Sensitivity", "How soft a hit can be and still play a drum. Lower it if noise or ringing strings trigger drums." },
    { "drumtrigger", "Kick",
      "The kick drum's level. Low, thumpy hits play it in Auto mode; turn it up for a bigger bottom end." },
    { "drumtrigger", "Snare",
      "The snare drum's level. Brighter hits play it in Auto mode, like a slap or a scratch near the bridge." },
    { "drumtrigger", "Hi-Hat",
      "The hi-hat's level. The brightest hits play it in Auto mode; turn it down to keep only kick and snare." },
    { "drumtrigger", "Tune",
      "Tunes all the drums down (lower, fatter) or up (higher, tighter). 50% is as designed." },
    { "drumtrigger", "Decay", "How long the drums ring: lower for tight, dead drums, higher for boomy ones. 50% is as designed." },
    { "drumtrigger", "Dynamics", "How much the drums follow how hard you hit. 0% plays every drum at the same level." },
    { "drumtrigger", "Dry",
      "How loud your guitar is under the drums. 0% plays the drums only; raise it to hear your strings as well." },
    { "fart", "Type",
      "Which fart. Random picks a different one for every note; or choose your favourite, from a tiny Squeaker to "
      "a long Motorboat." },
    { "fart", "Length",
      "Stretches or shortens every fart, from a quick toot to a long, drawn-out performance." },
    { "fart", "Wetness",
      "Adds the airy noise and the gurgles. Low settings are dry and buzzy; high settings, well, you know." },
    { "fart", "Tone", "Moves the fart's resonance down (a deep, round rumble) or up (a thin, reedy squeak). 50% is as designed." },
    { "fart", "Octave", SYNTH_OCTAVE_HELP },
    { "fart", "Mix", SYNTH_MIX_HELP },
    { "fart", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "fart", "Track", TRACK_HELP },
    { "trumpet", "Bright", "How much brighter the trumpet gets as you play harder. Low settings stay mellow, like a flugelhorn." },
    { "trumpet", "Growl",
      "Adds a brassy growl to hard attacks, like a player pushing air through the horn. 0% keeps it smooth." },
    { "trumpet", "Scoop", "Each note starts this far below its pitch and slides up into it, like a player lipping up into the note. 0 is off." },
    { "trumpet", "Vibrato",
      "A vibrato that comes in once a note has held for a moment, like a trumpeter's. 0% is off." },
    { "trumpet", "Mute", "Open, or a mute in the bell: Harmon (thin and buzzy, the jazz ballad sound) or Cup (soft and round)." },
    { "trumpet", "Attack", SYNTH_ATTACK_HELP },
    { "trumpet", "Release", SYNTH_RELEASE_HELP },
    { "trumpet", "Octave", SYNTH_OCTAVE_HELP },
    { "trumpet", "Glide", SYNTH_GLIDE_HELP },
    { "trumpet", "Mix", SYNTH_MIX_HELP },
    { "trumpet", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "trumpet", "Track", TRACK_HELP },
    { "violin", "Body", "The size of the instrument: Violin, Viola (a little deeper) or Cello (deep and woody). Use Octave to play it in its range." },
    { "violin", "Vibrato", "How deep the vibrato is. It fades in after the start of each note, the way string players add it." },
    { "violin", "Rate",
      "How fast the vibrato is. Around 5-6 Hz sounds like a classical player; slower is lazier, faster more "
      "nervous." },
    { "violin", "Bow",
      "The sound of the bow's hair on the string: a breathy rasp under the note. Turn it up for a rougher, "
      "folky bow." },
    { "violin", "Ensemble", "Turns one player into a small section of three, each a little apart in pitch and vibrato." },
    { "violin", "Attack", SYNTH_ATTACK_HELP },
    { "violin", "Release", SYNTH_RELEASE_HELP },
    { "violin", "Octave", SYNTH_OCTAVE_HELP },
    { "violin", "Glide", SYNTH_GLIDE_HELP },
    { "violin", "Mix", SYNTH_MIX_HELP },
    { "violin", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "violin", "Track", TRACK_HELP },
    { "piano", "Bright", "How bright the hammers are. The harder you pick, the brighter each note, as on a real piano." },
    { "piano", "Decay", "How long the notes ring. Low notes ring longer than high ones, as on a real piano." },
    { "piano", "Sustain",
      "The sustain pedal: notes keep ringing after you let go of them. Off, each note is damped when your guitar "
      "note ends." },
    { "piano", "Detune",
      "How far apart the strings of each note are tuned. A little gives a piano's lively beating; more sounds like "
      "an old honky-tonk upright." },
    { "piano", "Hammer",
      "The thump of the felt hammer hitting the strings, at the start of each note. Turn it up for a percussive "
      "attack." },
    { "piano", "Tone",
      "Rolls off the highs, for a softer, felted sound, like closing the piano's lid. Higher settings are "
      "bright and open." },
    { "piano", "Octave", SYNTH_OCTAVE_HELP },
    { "piano", "Mix", SYNTH_MIX_HELP },
    { "piano", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "piano", "Track", TRACK_HELP },
    { "midiout", "Mode",
      "Mono follows single notes and sends pitch bend for your bends and vibrato. Poly finds the notes of chords "
      "and sends them all, without bend; it makes more mistakes than Mono, so use Mono for lines and solos." },
    { "midiout", "Channel",
      "The MIDI channel the notes go out on. Set your synth to listen on the same channel (or on all of them)." },
    { "midiout", "Octave",
      "Shifts the notes sent by whole octaves, for a synth bass an octave down or a lead an octave up. Your guitar's "
      "sound isn't changed." },
    { "midiout", "Transpose",
      "Shifts the notes sent by semitones, to play in another key than you're fingering, or to send harmonies with "
      "a second MIDI Out." },
    { "midiout", "Bend",
      "Mono only: sends your bends and vibrato as pitch bend. Set it to your synth's bend range (2 semitones is the "
      "usual default; 12 or 24 for wide bends and slides). Off sends plain notes, for synths that ignore bend." },
    { "midiout", "Dynamics",
      "How much the notes' velocity follows how hard you pick. 0% sends every note at velocity 100; 100% follows "
      "your picking from very soft to full." },
    { "midiout", "Focus",
      "Poly only: how readily a note is dropped as another note's overtone. Lower it if octaves in your chords go "
      "missing; raise it if extra high notes appear." },
    { "midiout", "Audio",
      "Through passes your guitar on to the rest of the chain, so you hear it with the synth. Mute silences it, so "
      "only the synth the MIDI drives is heard." },
    { "midiout", "Pick Filter", SYNTH_PICK_FILTER_HELP },
    { "midiout", "Track", TRACK_HELP },
};

#undef MIX_HELP
#undef SYNC_HELP
#undef WINDOW_HELP
#undef WIDTH_HELP
#undef PATTERN_TAP_HELP

struct TypeHelp {
    const char* typeId;
    const char* help;
};

// What each block is, what it's for and how to use it: shown when hovering a block's tile or its entry in Add Block
constexpr TypeHelp kTypeHelp[] = {
    { "nam",
      "A Neural Amp Modeler capture: a machine-learning model of a real amp, pedal or full rig, loaded from a .nam "
      "file in your library. It responds to your playing dynamics like the original. Put drive pedals before it and "
      "a cab IR after it (unless the capture already includes the cab), and set Input for the amount of gain you "
      "want." },
    { "ir",
      "A cabinet impulse response: a .wav file that recreates the sound of a speaker cabinet and the microphone on "
      "it. Put it right after an amp model that has no cab of its own; without one, an amp sounds harsh and fizzy. "
      "Low Cut and High Cut tidy up the result for a mix." },
    { "studiodi",
      "A passive studio DI box with a big transformer: the cleanest way to record a bass, with the weight a good "
      "transformer adds to the low end (Iron). Put it first, or after your bass pedals, as the bass's whole chain or "
      "in front of an amp model." },
    { "valvedi",
      "A studio tube DI: the bass gets a little tube warmth and one of six voicings, from Flat to Vintage. A one-box "
      "recording chain for bass; use it on its own or before a cab IR." },
    { "optodi",
      "A DI with an optical compressor, like the classic studio levelling amps: it evens out your playing slowly "
      "and smoothly, so the bass sits steadily in a mix. Put it first, or after a drive to tame it." },
    { "hifipreamp",
      "A clean, modern bass preamp with a built-in compressor, a gentle tube stage and an EQ with a sweepable mid. "
      "Made for slap, fingerstyle and clear, defined modern bass tones; use it on its own as a DI." },
    { "tubepreamp",
      "The preamp of a classic all-tube bass amp: the big, warm rock bass sound, clean or growling, with the famous "
      "Ultra Lo and Ultra Hi switches. Use it on its own as a DI, or follow it with a bass cab IR." },
    { "britishpreamp",
      "A punchy British solid-state bass preamp with a five-band graphic EQ and the Pre Shape switch for an "
      "instant scooped, hi-fi sound. Great for slap and modern rock; use it on its own as a DI." },
    { "driverdi",
      "A tube-amp emulating DI pedal: a driven bass amp and its speaker, blended with your clean bass, in one box. "
      "The classic way to get a miked-amp sound straight into a desk. No cab IR needed after it." },
    { "dualdrive",
      "A two-channel bass distortion: a clean channel and a distorted one with their own levels, mixed, so the "
      "bass keeps its low end under the dirt. Color scoops the distortion for a more modern edge." },
    { "moderndrive",
      "A modern high-gain bass preamp: a tight, aggressive distortion with a clean blend to keep the lows solid, "
      "Grunt and Attack to shape what goes into the drive, and a four-band EQ. For modern rock, metal and djent." },
    { "crossoverdrive",
      "A crossover bass distortion: the bass is split in two, the lows stay clean and only the highs are driven. "
      "However much gain you use, the low end stays tight and the notes clear." },
    { "boost",
      "A clean boost to push the next block harder or lift your level for solos. Clean adds level without "
      "colouring the sound, Treble brightens it like a classic treble booster, and Mid pushes the midrange so a lead "
      "cuts through. Put it before an amp model for more gain, or at the end of the chain for a pure volume lift." },
    { "overdrive",
      "A smooth, amp-like drive pedal. Screamer has the famous mid hump that tightens the low end, Blues is a "
      "looser, more open crunch, and Transparent keeps your guitar's own tone. Use it on its own for crunch, or "
      "with low Drive and high Level in front of a driven amp model to tighten and push it." },
    { "distortion",
      "A hard-clipping distortion pedal with more gain than an overdrive. RAT is gritty and raw, DS-1 is bright and "
      "buzzy, and Metal is a tight, high-gain sound for heavy riffs. Put it before an amp model set fairly clean, "
      "or use it with a cab IR for a complete pedal-into-cab sound." },
    { "fuzz",
      "A vintage fuzz pedal: thick, saturated and woolly. Face is the round, warm 60s fuzz, Muff is the big, "
      "sustaining wall of fuzz, and Octave adds a ringing upper octave, best on the neck pickup above the 12th fret. "
      "Bias starves the circuit for a sputtery, gated sound. Fuzzes like to be first in the chain." },
    { "bitcrusher",
      "A lo-fi effect that lowers the bit depth and sample rate, for crunchy, broken, video-game sounds. Fewer Bits "
      "add grainy distortion, a lower Rate adds metallic, aliased tones. Use Mix to blend a little grit in, or go "
      "all the way for something extreme." },
    { "diodebridge",
      "A compressor and limiter modelled on a classic British bus compressor: a diode bridge between big "
      "transformers, with a stepped ratio and Auto recovery that follows your playing. It glues and thickens, and "
      "adds a little warm grit as it works harder. Use it near the end of the chain, or after an amp for a "
      "finished, even sound." },
    { "consolecomp",
      "A British console channel: the class-A preamp and its transformers (Drive), then the channel's diode-bridge "
      "compressor. Thick, warm and punchy. Put it first for a fat clean tone, or after an amp and cab to make the "
      "guitar sound recorded." },
    { "optoleveler",
      "A classic optical levelling amplifier: one knob of Peak Reduction evens out your playing slowly and smoothly, "
      "with a tube output stage. It never sounds grabby, so it suits clean parts, ballads and leads. Put it early "
      "for even sustain, or at the end of the chain to level the whole rig." },
    { "optostomp",
      "An optical compressor pedal: a light shining on a photocell turns your signal down, so it reacts gently and "
      "lets the pick attack through. Natural, transparent sustain; turn it on and leave it on. Put it first, before "
      "your drives, or after them to even out their level." },
    { "fetlimiter",
      "A FET compressor modelled on the famous studio limiting amplifier: very fast attack, a bright, aggressive "
      "character and the All mode for extreme smash. Great for punchy clean rhythm, in-your-face leads and parallel "
      "smash. Use it first for pick attack control or after the amp for studio punch." },
    { "squashcomp",
      "The classic stompbox squash: quiet notes come up and loud ones are held back, for the bouncy, snappy sound of "
      "country chicken picking and funk. A little dark and squishy, like the originals. Put it first in the chain, "
      "before the drives." },
    { "compressor",
      "A studio compressor that evens out your level: it turns loud notes down so quiet ones sound fuller. Use it "
      "early in the chain for snappy, even clean tones and more sustain, or at the end to glue everything together. "
      "Start with Threshold so a few dB are reduced, then raise Makeup to match the level." },
    { "gate",
      "A noise gate that silences hiss and hum when you stop playing. Put it after high-gain blocks, or set Key to "
      "Rig In so it listens to your guitar before any drive, which keeps it from chopping off sustained notes. Raise "
      "Threshold until the noise between notes disappears, but not so far that quiet notes get cut." },
    { "volume",
      "A volume pedal. Map Volume to an expression pedal with MIDI learn (right-click the knob) to fade in, swell or "
      "turn down. Before drive blocks it changes how hard they're hit; after them it only changes the level. Audio "
      "taper feels natural to the ear." },
    { "swell",
      "Automatic volume swells: each note fades in, removing the pick attack, for violin-like pads. Attack sets "
      "how slow the fade-in is. It works best with a delay or reverb after it, which smears the swells into an "
      "ambient wash." },
    { "eq.graphic",
      "A 7-band graphic EQ, like the classic pedal: each slider boosts or cuts one range from 100 Hz (low end) to "
      "6.4 kHz (presence and fizz). Use it before an amp model to shape what drives it, or after the cab to shape "
      "your final tone. Level makes up for the change in volume." },
    { "eq.parametric",
      "A precise studio EQ: low and high shelves, a sweepable mid band with adjustable width (Q), and low and high "
      "cuts. Use it after the cab to fix problems, like cutting a boomy low end, taming harsh highs or boosting "
      "the mids so a solo cuts through." },
    { "chorus",
      "Chorus thickens your sound by adding slightly detuned, moving copies of it, for shimmering cleans and lush "
      "80s tones. Classic is the familiar pedal, Dimension is a subtle, wide studio thickener, Tri is three voices "
      "for a rich ensemble, and Vibrato is pitch wobble only. Best after drive, in Stereo mode for width." },
    { "flanger",
      "A flanger sweeps a short, comb-filtered echo for the classic jet-plane whoosh. More Feedback makes it more "
      "metallic and resonant; Manual sets where the sweep sits. Slow rates suit chords; fast ones give a "
      "warbling effect. Put it after drive for a dramatic sweep." },
    { "phaser",
      "A phaser sweeps notches through your tone for a swirling, liquid movement. More stages give a deeper, more "
      "dramatic sweep; Vibe recreates the throbbing Uni-Vibe of 60s and 70s rock. Slow rates are great on cleans "
      "and funk, faster ones on leads." },
    { "tremolo",
      "Tremolo rhythmically turns your volume up and down, from the gentle pulse of a vintage amp to a hard "
      "chop. Opto is the soft, classic amp tremolo, Harmonic pulses the lows and highs in turns for a subtle, "
      "swirling sound, and Pan moves your sound from side to side in Stereo mode. Try Sync to lock it to the song's tempo." },
    { "rotary",
      "A rotating speaker cabinet, as used with organs and guitars: a spinning horn and drum give a swirling, "
      "Doppler sound. Switch Speed between Slow (chorale) and Fast (tremolo) while you play, and the rotors speed up "
      "and slow down naturally. Map Speed to a footswitch with MIDI learn." },
    { "doubler",
      "Makes it sound like you recorded the same part twice (or three times), for a wide, thick rhythm sound "
      "without the swirl of a chorus. Use it in Stereo mode with Width up to spread the copies across the "
      "speakers. Short delays sound tight, longer ones more like separate takes." },
    { "ringmod",
      "A ring modulator multiplies your signal with a tone, for metallic, bell-like and robotic sounds that often "
      "ignore the key you're playing in. Low frequencies give a tremolo-like warble, higher ones clangy "
      "inharmonic tones. Rate and Depth sweep the frequency for sci-fi effects." },
    { "reverb",
      "Studio reverb puts your guitar in a space. Room is small and natural, Hall is big and lush, and Plate is "
      "bright and dense like a classic studio plate. Put it at the end of the chain, after delay. Keep Mix low "
      "for a natural sense of space, or raise Decay and Mix for ambient sounds." },
    { "reverb.spring",
      "The drippy, splashy spring reverb from vintage amps and surf rock. Drip and Dwell bring out the springy "
      "splash when you pick hard. Put it after the amp model, as in a real amp; more Mix and Dwell give the classic "
      "surf sound." },
    { "shimmer",
      "An ambient reverb that pitches its tail up (or down), for a glowing, choir-like halo behind your notes. "
      "Shimmer sets how much of the shifted sound feeds back into the tail. Long Decay and slow playing make "
      "huge pads; keep it at the end of the chain." },
    { "freeze",
      "Holds the sound you're playing as an endless pad. Turn Freeze on to capture it and play over it; Layer adds "
      "what you play next on top. Fade In and Fade Out make the frozen sound appear and disappear smoothly. Map "
      "Freeze to a footswitch with MIDI learn for live use." },
    { "delay",
      "A clean digital delay: repeats of what you play. Use short times (80-150 ms) for slapback, around 300-500 ms "
      "for leads, or Sync to lock the repeats to the song's tempo. Ping-Pong bounces the repeats between the "
      "speakers in Stereo mode. Usually goes after drive and the amp, before reverb." },
    { "doppelganger",
      "A delay whose echoes take turns being flanged and chorused, so each repeat has its own colour as it "
      "fades. Order picks which effect comes first, and the Flange and Chorus controls shape each one. Great for "
      "lush, moving leads and ambient parts; in Stereo mode, Bounce moves the echoes between the speakers." },
    { "tapeecho",
      "A vintage tape echo: warm, darkening repeats with a little wobble and saturation, like the classic "
      "multi-head machines. Heads picks which playback heads sound, for rhythmic patterns. High Feedback makes it "
      "build into self-oscillation. Put it after the amp, before reverb." },
    { "duckdelay",
      "A delay that gets out of the way while you play: the echoes are turned down while you're playing and come "
      "back up when you stop. You get big, long delays on leads without the repeats cluttering fast phrases. Set "
      "Threshold so your playing ducks the echoes, and Duck for how much." },
    { "looper",
      "Records a phrase and plays it back as a loop, so you can play over it. Rec/Dub records, then overdubs on "
      "top; Play/Stop and Clear control the loop. In Count In mode a metronome counts you in and clicks while you "
      "record a set number of bars, then the loop plays on without it. Reverse and Half Speed change the playback. "
      "Map the buttons to MIDI footswitches with MIDI learn. Keeps one loop of up to a minute." },
    { "octave",
      "Adds notes one and two octaves below, and one above, what you play, for bass-like riffs and big, organ-like "
      "sounds. It tracks single notes; play cleanly and put it before any drive for the best tracking. Raise "
      "Sub 1 for a bass line under your riff." },
    { "polyoctave",
      "An octave pedal that works on chords too: adds copies one or two octaves below and above everything you "
      "play. Use the subs for huge, organ-like chords or a bass under your riffs, and the octaves up for 12-string "
      "sparkle. Put it before drive and time-based effects." },
    { "pitch",
      "Shifts your sound up or down by up to two octaves, with two independent voices. Use small Detune amounts for "
      "a thick, chorus-like doubling, a few semitones for harmonies, or a full shift to play in a different tuning. "
      "Put it early in the chain, before drive." },
    { "whammy",
      "The classic pitch-bending pedal. Position bends the pitch from no shift to the chosen Interval, for "
      "dive bombs, octave squeals and smooth bends. Map Position to an expression pedal with MIDI learn. Harmony "
      "mode keeps the dry sound so you get a fixed interval harmony instead." },
    { "harmonizer",
      "Adds harmony voices that follow the key and scale you choose, so a third above stays in key as you play "
      "up the neck. Set Key and Scale to the song, then pick intervals for the two voices. Play single notes "
      "cleanly; put it before drive for the clearest harmonies." },
    { "synth",
      "Turns your guitar into a monophonic synthesizer: it tracks the pitch you play and drives an oscillator "
      "through a resonant filter. Wave picks the tone, Env makes the filter open on each note, and Glide slides "
      "between notes. Play single notes cleanly, near the neck, for the best tracking." },
    { "filter",
      "A resonant filter swept by an LFO, for rhythmic, synth-like movement: auto-wah sweeps, pulsing low-pass "
      "chops, or a fixed filter to shape your tone. Sync locks the movement to the song's tempo, and Shape picks "
      "how it moves, from a smooth sine to stepped random-like patterns." },
    { "wah",
      "A wah pedal: a sweeping, vocal-like filter you control with Position. Map Position to an expression pedal "
      "with MIDI learn to rock it like a real wah, or leave it fixed for a cocked-wah tone. Classic is the familiar "
      "pedal, Vox is smoother, Wide sweeps further and Bass suits low notes. Put it before drive." },
    { "autowah",
      "A wah that sweeps by itself as you play: the harder you pick, the further it opens, for funky, quacking "
      "rhythms. Sensitivity sets how much your picking moves it, Decay how fast it falls back. Down reverses the "
      "sweep for a different, vocal feel. Put it before drive." },
    { "gatedfuzz",
      "A wild, unstable fuzz in the style of the Fuzz Factory. Comp starves it into a sputtery, velcro-like rasp, "
      "Gate chops it off between notes, and turning Stab down makes it squeal and oscillate along with your notes. "
      "Put it first in the chain, before any other drive or an amp model." },
    { "envdrive",
      "A drive that follows your picking: clean when you play softly and dirty when you dig in (or, with a negative "
      "Range, the other way round), while soft notes stay soft and loud ones loud. Great for playing dynamics into "
      "a clean amp model. Put it before the amp." },
    { "killswitch",
      "A kill switch and stutter: Kill mutes while held, Stutter chops the sound on and off in time. Map both to "
      "footswitches or MIDI buttons for hand-played stutters, or use Latch so a press switches them on and off. "
      "Put it at the end of the chain so it cuts everything." },
    { "barberpole",
      "A phaser whose sweep seems to rise (or fall) forever, like the stripes on a barber's pole, for an endless, "
      "hypnotic swirl that never turns round. Slow rates sound like constant tension building. Put it after drive, "
      "like other modulation." },
    { "modsweeper",
      "Several modulation pedals in one that it moves between over time: pick two or three of chorus, flanger, "
      "phaser and vibrato, and it sweeps from one to the next (back and forth, or round in a cycle) every Sweep "
      "Rate step, or every bar or beat with Sync. Glide sets how smoothly it morphs. Put it after drive, like other "
      "modulation." },
    { "warble",
      "The sound of worn tape or a warped record: a slow, uneven pitch wobble with a faster flutter, a darker tone "
      "and a bit of saturation. Use it subtly for lo-fi, nostalgic textures, or deep for a seasick wobble. Put it "
      "after drive, or after the amp for a whole-mix tape feel." },
    { "slicer",
      "A rhythmic volume sequencer: up to eight steps, each with its own level, chop your playing into a pattern. "
      "With Sync on, the pattern locks to your DAW's tempo and starts on the bar. Use it for trance-gate chops or "
      "pulsing pads; put it after the amp and before delay and reverb." },
    { "bloomreverb",
      "A reverb that swells up after each note instead of splashing on the pick attack, so your playing stays clear "
      "and the reverb blooms in behind it. Long Bloom and Decay give big, ambient washes. Put it at the end of the "
      "chain, after delay." },
    { "gatedreverb",
      "The classic 80s gated reverb: a big, dense reverb that is cut off abruptly shortly after you stop playing, "
      "so it sounds huge without washing over the next note. Great for punchy chords and stabs. Put it at the end "
      "of the chain." },
    { "reversedelay",
      "A delay whose repeats play backwards, swelling in like a tape played in reverse, for psychedelic, dreamlike "
      "leads. Time sets how long each reversed slice is. Put it after the amp, before reverb." },
    { "pitchdelay",
      "A delay where every repeat is pitch shifted again, so the echoes climb (or fall) up the octaves, or drift "
      "slowly out of tune with a few cents of Fine. Great for cascading, shimmering leads and ambient swells. Put "
      "it after the amp, before reverb." },
    { "beatrepeat",
      "A glitch and stutter effect: hold Repeat and the last slice of what you played repeats over and over in "
      "place of your playing, then let go to come back. Shorter slices buzz and glitch; Pitch and Decay make tape-"
      "stop-like falls. Map Repeat to a footswitch; put it near the end of the chain." },
    { "detune",
      "A micro-pitch doubler: one or two voices a few cents sharp and flat, spread across the stereo field, for a "
      "thick, wide, double-tracked sound without a chorus's sweep. Put it after the amp, or after drive." },
    { "capo",
      "A virtual capo and drop tuner: shifts everything you play (chords too) up or down by whole semitones, so you "
      "can play in Eb, drop D shapes or capo positions without retuning. Put it first in the chain, before any "
      "drive." },
    { "arpeggiator",
      "Turns a held note or chord into a moving arpeggio, stepping through the notes of a chord in a pattern, in "
      "time with your DAW when synced. Use it for synth-like sequences and rhythmic textures. Put it before drive, "
      "or after the amp for a cleaner shift." },
    { "organ",
      "Turns the guitar into an organ: drawbar-like levels add an octave below and octaves and a twelfth above "
      "your note, and Attack swells each note in like a pressed key instead of a pick. Put it first in the chain, "
      "and try a rotary speaker after the amp for the classic sound." },
    { "sustainer",
      "Infinite sustain, like an E-Bow or a sustainer pickup: holds each note at the level you picked it, and over "
      "Bloom lets it grow into a harmonic like an amp feeding back. A new pick starts again. Put it first in the "
      "chain, before drive." },
    { "robotfilter",
      "A sample-and-hold filter: a resonant filter that jumps to a new cutoff on every step, for bubbling, robotic, "
      "synth-like textures. The steps can be random, a repeating eight-step pattern, or follow your picking. Put it "
      "before or after drive." },
    { "talkbox",
      "A talk box: formant filters that make the guitar sound like it's saying vowels. Map Position to an "
      "expression pedal to make it talk, let your picking open it (Envelope), or sweep it with the LFO. Put it "
      "after drive, where there are more harmonics to shape." },
    { "supersaw",
      "A huge, detuned synth: seven saws slightly out of tune with each other and spread across the stereo field, "
      "the classic trance lead and big pad sound. Play single notes; it follows your pitch and your dynamics. Put "
      "it first in the chain, or after a compressor for an even level." },
    { "fmsynth",
      "An FM synth: one sine modulating another, for electric pianos, glassy bells, plucky basses and metallic "
      "tones that brighten on every pick and mellow as they ring. Ratio picks the kind of sound. Play single notes; "
      "put it first in the chain." },
    { "foldsynth",
      "A west-coast style synth: a sine folded into rich harmonics as you dig in, through a low-pass gate that "
      "follows your playing or pings on each pick like a struck wooden bar. Play single notes; put it first in the "
      "chain." },
    { "acidsynth",
      "An acid bass synth in the style of the 303: a saw or square through a squelchy resonant filter that snaps "
      "open on every pick, with slides between notes. Play single-note lines, often an octave down; put it first "
      "in the chain, and try a distortion after it." },
    { "stringsynth",
      "A 70s string machine: saws in three octaves through a lush ensemble chorus, with slow swells, for warm, "
      "sweeping string pads. Play single notes or slow lines; it follows your pitch. Put it first in the chain and "
      "add reverb after it." },
    { "chipsynth",
      "An 8-bit game console synth: thin pulse waves or a stepped triangle, notes snapped to semitones, fast "
      "arpeggios that fake chords, and a delayed vibrato, for chiptune leads and basses. Play single notes; put it "
      "first in the chain." },
    { "polychip",
      "A polyphonic 8-bit synth: it hears every note you play, chords included, and plays each one as a pulse or "
      "triangle voice snapped to the semitone, or arpeggiates the chord. Low notes take a moment longer to speak "
      "than in the mono synths. Play cleanly and put it first in the chain." },
    { "sympathetic",
      "Twelve strings tuned to a key, which ring along when you play their notes, like the drone strings of a "
      "sitar or a harp left undamped: a shimmering halo that follows the harmony. Put it after drive and before "
      "delay or reverb." },
    { "jawari",
      "The buzzing bridge of a sitar: each note's top is folded back as the string grazes the bridge, for the "
      "bright, twangy sitar buzz that blooms as the note fades. Best on a clean or lightly driven sound, early in "
      "the chain." },
    { "graincloud",
      "Turns your playing into a cloud of tiny grains, scattered in time, some shifted in pitch, some backwards: "
      "single notes become shimmering, ambient textures. Freeze holds the cloud. Put it near the end of the chain, "
      "before reverb." },
    { "swarm",
      "A dozen short delays, each drifting in pitch and fluttering in level. A little gives a lush 12-string or "
      "choir shimmer; all the way up it sounds like a swarm of bees. Put it after drive, with the modulation "
      "pedals." },
    { "tapestop",
      "Hold its Stop button and the sound slows to a halt like a turntable losing power; let go and it spins back "
      "up, landing back in time. A performance effect: map Stop to a footswitch. Put it at the end of the chain." },
    { "bouncedelay",
      "Echoes that come faster and faster, like a ball bouncing to rest (or slower and slower with Rise). Great "
      "on single hits and chord stabs. Put it after drive and modulation, with the delays." },
    { "patterndelay",
      "Eight echoes on a rhythmic grid, each with its own level, so the echoes play a rhythm; every other echo can "
      "be pitch-shifted for cascading arpeggios. Sync it to your DAW for rhythmic delay parts." },
    { "chaosmod",
      "Modulation from chaos: a Lorenz attractor sweeps a filter, the volume and the pitch in smooth motion that "
      "never repeats, somewhere between an LFO and random. Put it after drive." },
    { "acoustic",
      "Makes an electric guitar sound acoustic: it undoes the pickup's colour, adds the resonances of an acoustic "
      "body and the strings' sparkle. Use it on a clean signal, with no amp after it (or a clean one)." },
    { "strum",
      "Turns each note you pick into a strummed chord: the chord's notes come in one after another, like a hand "
      "sweeping across the strings, and fit the key with Diatonic. Play single notes; put it early in the chain, "
      "before drive." },
    { "hardtune",
      "Pitch correction for guitar: pulls your notes to the nearest note of a key and scale. Set Speed to 0 for "
      "the robotic hard-tune sound, or higher to just tidy up your intonation. Play single notes; put it early in "
      "the chain." },
    { "drumtrigger",
      "Plays drums when you hit muted strings: low thumps play a kick, brighter hits a snare, the brightest a "
      "hi-hat, all made in code. Tap out beats on your muted strings. Put it first in the chain." },
    { "trumpet",
      "A trumpet played from your notes: brighter as you dig in, scooping up into notes, with a vibrato and mutes. "
      "Play single notes, with Track on Rig In if it comes after an amp." },
    { "violin",
      "A bowed violin, viola or cello played from your notes, with bow noise and vibrato, or a small string "
      "section with Ensemble. Play single notes, slowly and expressively." },
    { "piano",
      "A piano played from your notes: each note you pick strikes a piano note that rings and decays on its own, "
      "brighter the harder you pick. Play single notes, or arpeggios with Sustain on." },
    { "midiout",
      "Turns your playing into MIDI notes, to play a synth or sampler plugin from your guitar. In Reaper, put the "
      "synth after Xythos in the same FX chain (it receives the notes), or send the track's MIDI to another track. "
      "Mono follows single notes with pitch bend; Poly sends chords. Lookahead makes the notes start on time." },
    { "fart",
      "Plays a fart at the pitch of every note you pick: thirteen kinds, from a tiny squeak to a motorboat, or a "
      "random one each time. Because why not. Play single notes; an octave down sounds best." },
};

struct GlobalHelp {
    const char* symbol;
    const char* help;
};

constexpr GlobalHelp kGlobalHelp[] = {
    { "input_mode",
      "Mono takes your guitar from the first input and processes one channel; Stereo processes both inputs, so "
      "stereo effects (wide delays, reverbs, choruses) can spread across the speakers, and uses about twice the "
      "CPU. In Mono the chain still turns stereo after a split's merge and at the Delay, so two panned amps and "
      "ping-pong echoes work with a mono guitar." },
    { "input",
      "The level of your guitar going into the chain. Turn it up if your guitar is quiet, so amps and drives react "
      "as they should; turn it down if the IN meter clips." },
    { "bass", "A global EQ at the end of the chain: boosts or cuts the lows, for weight and warmth." },
    { "middle", "A global EQ at the end of the chain: boosts or cuts the mids, the body and punch of the guitar." },
    { "treble", "A global EQ at the end of the chain: boosts or cuts the highs, for brightness and clarity." },
    { "output",
      "The final volume of the plugin. Use it to set the level for your DAW without changing the sound." },
    { "bypass",
      "Turns the whole plugin off: your guitar passes through unprocessed, and the tuner and metronome go quiet." },
    { "limiter",
      "A safety limiter at the very end that stops the output from going over 0 dB, so it never clips. Keep it on "
      "unless you want to hear the raw level." },
    { "merge_balance",
      "The balance between the two branches of the split. In the middle both play at full level; turn left to fade "
      "out branch B, right to fade out branch A." },
    { "merge_level_a",
      "The volume of branch A (top row) where the branches merge. Use it to balance the two branches, for "
      "example a dry amp against a wet effects branch." },
    { "merge_level_b",
      "The volume of branch B (bottom row) where the branches merge. Use it to balance the two branches, for "
      "example a dry amp against a wet effects branch." },
    { "merge_pan_a",
      "Pans branch A (top row) left or right, for example to put two amps on opposite sides for a wide stereo "
      "sound. Works in Mono mode too: the chain is stereo after the merge." },
    { "merge_pan_b",
      "Pans branch B (bottom row) left or right, for example to put two amps on opposite sides for a wide "
      "stereo sound. Works in Mono mode too: the chain is stereo after the merge." },
    { "merge_invert_a",
      "Flips branch A's phase. If the merged sound gets thin or hollow, the branches are cancelling each other; try "
      "flipping one." },
    { "merge_invert_b",
      "Flips branch B's phase. If the merged sound gets thin or hollow, the branches are cancelling each other; try "
      "flipping one." },
    { "merge_align",
      "Lines up the two branches in time. Two amp captures can be a fraction of a millisecond apart, and mixed "
      "together they then sound thin, hollow or phasey. With both branches at the same level, turn it slowly from "
      "the middle (right delays branch B, left delays branch A) until the sound is fullest; try Invert too." },
    { "tuner",
      "Opens the tuner in place of the chain. Play a single open string and tune until the display shows you're "
      "in tune. While it's open your guitar is muted, unless Mute is off." },
    { "tuner_mute",
      "Mutes your guitar while the tuner is open, so you can tune silently on stage or in a session. Turn it "
      "off to hear yourself while tuning." },
    { "tuner_ref",
      "The pitch of A4 that the tuner tunes to. 440 Hz is standard; change it only to play along with something "
      "tuned differently." },
    { "tuner_strobe",
      "Switches between the needle view and the strobe view. The strobe bands drift left when you're flat and "
      "right when you're sharp, and stand still when you're in tune; it's the most precise view." },
    { "metro", "Starts and stops the metronome. With DAW sync on, it clicks while the DAW plays." },
    { "metro_tempo",
      "The metronome's speed in beats per minute. Drag the slider, use the -/+ buttons, the arrow keys, or tap "
      "the tempo with the Tap button." },
    { "metro_volume",
      "The volume of the click. The click is added after the Output knob and before the limiter, so it can't "
      "clip the output." },
    { "metro_sync",
      "Follows your DAW's tempo, time signature and transport instead of the metronome's own settings." },
    { "metro_beats",
      "How many beats there are in a bar. Set it to the top number of the time signature: 4 for 4/4, 3 for 3/4, "
      "7 for 7/8." },
    { "metro_note",
      "Which note value gets the beat, the bottom number of the time signature: 4 for quarter notes (4/4, 3/4), "
      "8 for eighths (6/8, 7/8), and so on." },
    { "metro_grouping",
      "How the beats are grouped, for odd and compound meters. For example, 7/8 can be 2+2+3 or 3+2+2. Each group "
      "starts with an accent." },
    { "metro_sub",
      "Adds clicks between the beats: eighths, triplets, sixteenths, quintuplets, sextuplets, swing, dotted and "
      "mixed figures. Great for practicing rhythmic precision." },
    { "metro_sound",
      "The click's sound: Classic is a woodblock-like click, Digital a short electronic tick and Beep a pure "
      "tone. Pick whatever cuts through your playing best." },
    { "metro_sub_volume",
      "The volume of the subdivision clicks, relative to the main volume. Keep it lower than the beats so the "
      "beat stays clear." },
    { "metro_trainer",
      "Changes the tempo for you as you practice. Step raises (or lowers) it by a set amount every few bars; "
      "Gradual ramps it smoothly from the start to the end tempo." },
    { "metro_tr_start",
      "The tempo the trainer starts at when you start the metronome. Start at a tempo you can play cleanly." },
    { "metro_tr_end",
      "The tempo the trainer ends at. It can be slower than the start tempo, to practice slowing down. The "
      "trainer stays at this tempo once it gets there." },
    { "metro_tr_step",
      "How many BPM the tempo changes by on each step. Small steps (2-5 BPM) build speed gradually; bigger "
      "steps push you harder." },
    { "metro_tr_every",
      "How many bars the trainer plays at each tempo before it steps to the next. More bars give you more time "
      "to settle in." },
    { "metro_tr_bars",
      "How many bars the gradual ramp takes to go from the start tempo to the end tempo. More bars make the "
      "change slower and easier to follow." },
    { "metro_gap",
      "Mutes the click for some bars, then brings it back, so you can check that you keep time on your own." },
    { "metro_gap_play",
      "How many bars the click plays during the gap trainer, before it mutes. Use a few bars to lock in the "
      "tempo before the gap." },
    { "metro_gap_mute",
      "How many bars the click stays muted during the gap trainer. Longer gaps test your timing harder. Keep "
      "playing in time; the click comes back where it should be." },
};

constexpr const char* kAccentHelp =
    "Click a beat to cycle it through accent (loudest), normal, soft and mute, to make the click's pattern.";

} // namespace

const char* blockParamHelp(const char* typeId, const char* paramName)
{
    if (typeId == nullptr || paramName == nullptr)
        return nullptr;
    for (const BlockHelp& entry : kBlockHelp)
    {
        if (std::strcmp(entry.typeId, typeId) == 0 && std::strcmp(entry.param, paramName) == 0)
            return entry.help;
    }
    return nullptr;
}

const char* globalParamHelp(uint32_t index)
{
    if (isSlotParam(index) || index >= kParamCount)
        return nullptr;
    if (index >= kParamMetroAccent1 && index <= kParamMetroAccent16)
        return kAccentHelp;
    const char* symbol = globalParamSpec(index).symbol;
    for (const GlobalHelp& entry : kGlobalHelp)
    {
        if (std::strcmp(entry.symbol, symbol) == 0)
            return entry.help;
    }
    return nullptr;
}

const char* blockTypeHelp(const char* typeId)
{
    if (typeId == nullptr)
        return nullptr;
    for (const TypeHelp& entry : kTypeHelp)
    {
        if (std::strcmp(entry.typeId, typeId) == 0)
            return entry.help;
    }
    return nullptr;
}

} // namespace xythos
