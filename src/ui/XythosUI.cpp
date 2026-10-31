// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"
#include <memory>
#include "src/nanovg/nanovg.h" // DPF's NanoVG class has no fallback-font call

#include "BlockGlyphs.hpp"
#include "BackgroundImage.hpp"
#include "LogoImage.hpp"
#include "BlockImages.hpp"
#include "WebpImage.hpp"
#include "FileLooks.hpp"
#include "FontData.hpp"
#include "FolderPicker.hpp"
#include "HttpGet.hpp"
#include "KnobImages.hpp"
#include "LedImages.hpp"
#include "OpenUrl.hpp"
#include "ParamHelp.hpp"
#include "PedalFaces.hpp"
#include "TextEdit.hpp"
#include "Vsync.hpp"
#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/PhaseCheck.hpp"
#include "engine/TapTempo.hpp"
#include "engine/blocks/CompressorBlock.hpp"
#include "engine/blocks/DelayBlock.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/LooperBlock.hpp"
#include "engine/blocks/NamBlock.hpp"
#include "state/ChainExchange.hpp"
#include "state/ChainState.hpp"
#include "state/Ink.hpp"
#include "state/Library.hpp"
#include "state/LibraryScanner.hpp"
#include "state/Paths.hpp"
#include "state/Preset.hpp"
#include "state/Settings.hpp"
#include "state/UndoStack.hpp"
#include "state/UpdateCheck.hpp"
#include "state/UserImages.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace xythos;

namespace {

// Theme tokens. Very dark: the owner's dark background image, and every box translucent black over it with a hairline
// grey outline (surface()).
namespace theme {
const Color background(0x00, 0x00, 0x00);
// Everything in the window is translucent black over the background image (the owner's choice, 2026-10-29: 50%, after
// 90% let next to nothing of the dark background through)
const Color panelFill(0x00, 0x00, 0x00, 0.5f);  // bars, panels, tiles, buttons, the tuner and the metronome
const Color itemFill(0x00, 0x00, 0x00, 0.55f);  // items listed in dialogs, a little darker
// Popups over the chain stay nearly opaque so they read clearly (owner, 2026-10-29: 95%, no blur; a blur of what's
// behind them was tried and dropped): tooltips, dropdown and right-click menus, Add Block and Settings, the other
// dialogs (Save Preset, Manage Presets, Images, Cab Image) and the toast. The rule: what the window shows in its base
// state is 50%; everything that appears over it is popupFill.
const Color popupFill(0x00, 0x00, 0x00, 0.95f);
const Color popupItemFill(0x00, 0x00, 0x00, 0.97f); // the blocks in Add Block
const Color tile(0x16, 0x16, 0x1a);       // small grey fills (hover in lists, meter boxes)
const Color tileHover(0x1d, 0x1d, 0x22);
const Color inset(0x05, 0x05, 0x06);
const Color overlay(0, 0, 0, 0.7f);
const Color border(255, 255, 255, 0.10f);
const Color text(0xe6, 0xe6, 0xea);
const Color textDim(0x8a, 0x8a, 0x94);
const Color textFaint(0x5c, 0x5c, 0x64); // the category under each tile
const Color good(0x4c, 0xc9, 0x7a);
const Color warn(0xf0, 0xc0, 0x40);
const Color bad(0xe5, 0x48, 0x4d);
// Highlight colour, chosen in Settings from these
const Color accents[] = {
    // pale, which reads better than vivid colours on the black window (owner, 2026-10-30)
    Color(0xc4, 0xab, 0xff), // lavender (was purple)
    Color(0xa8, 0xc6, 0xff), // pale blue
    Color(0x9e, 0xe3, 0xd6), // pale teal
    Color(0xab, 0xe6, 0xbf), // pale green
    Color(0xf7, 0xd6, 0x9c), // pale amber
    Color(0xf5, 0xab, 0xab), // pale red
    Color(0xf4, 0xb4, 0xdc), // pale pink
    Color(0xff, 0xff, 0xff), // white (default, Settings::accent)
};
constexpr int accentCount = sizeof(accents) / sizeof(accents[0]);
// The order Settings shows them in, the default (white) first. Settings stores the index into `accents`, so that
// list keeps its order.
constexpr int accentOrder[accentCount] = { 7, 1, 0, 2, 3, 4, 5, 6 };
Color accent = accents[7];
const Color faceLabel(255, 255, 255, 0.55f); // knob names on pedal images
const Color led(0xff, 0x2a, 0x1c);            // the drawn pedals' and amps' LEDs, lit
const Color neonBody(0x0a, 0x0a, 0x0d);
const Color amp(0xd8, 0x30, 0x24);
const Color cab(0x7e, 0x86, 0x96);
const Color pedalDelay(0x2f, 0x9e, 0x91);
const Color pedalReverb(0x4c, 0x7c, 0xe0);
const Color pedalDynamics(0xd0, 0x82, 0x2e);
const Color pedalDrive(0xc8, 0x44, 0x3c);
const Color pedalModulation(0x8e, 0x5c, 0xd0);
const Color pedalEq(0x9a, 0xa0, 0xa8);
const Color pedalPitch(0xd0, 0xb0, 0x40);
const Color pedalSynth(0xd0, 0x4c, 0x9c);
const Color pedalFilter(0x5c, 0xa0, 0x3a);
const Color pedalDi(0x40, 0xb4, 0xc8);
const Color pedalOther(0x70, 0x70, 0x7a);
} // namespace theme

// A pedal's LED: lit while the block is on, dark while it's bypassed; None where there's no block (Add Block)
enum class Led { None, Off, On };

// Kinds of box, for surface(): each has its own outline brightness
enum class Surface { Bar, Panel, Tile, Button, Dialog, Inset, Item, Popup, PopupItem };

constexpr float kTopBarHeight = 72.0f; // room for the big logo (owner, 2026-10-29; was 60 with the text logo, then 80
                                       // until the logo shrank 10%, 2026-10-31)
constexpr float kFontScale = 1.69f; // text is drawn this much larger than layout sizes (see fontSize())
constexpr float kBottomBarHeight = 60.0f; // one row, names beside the knobs (owner, 2026-10-31: idea A; was 132)
constexpr float kTileSize = 120.0f;
constexpr float kSplitTileSize = 100.0f; // tiles are smaller with a split, to fit two rows
constexpr float kTileGap = 16.0f;
constexpr float kRowGap = 14.0f;
constexpr float kForkWidth = 28.0f;
constexpr float kMergeWidth = 64.0f;
constexpr float kChainTop = kTopBarHeight + 24.0f;
constexpr float kKnobRadius = 22.0f;
// Block panel: one cell per control, right-justified next to the block image
constexpr float kCellWidth = 96.0f;
constexpr float kCellHeight = 128.0f;
constexpr float kCompactCellHeight = 104.0f; // rows this tight when that makes room for two
constexpr float kPanelMargin = 16.0f;
constexpr float kListBarWidth = 14.0f; // a dialog list's scroll bar, at its right edge
constexpr uint kDoubleClickMs = 350;
constexpr auto kTunerHold = std::chrono::milliseconds(1000); // the tuner keeps showing the last note this long

// The Lookahead button of the pitch-following blocks opens a menu of these (ms; 0 = off)
constexpr double kLookaheads[] = { 0.0, 5.0, 10.0, 15.0, 20.0, 30.0 };

std::string lookaheadText(double ms)
{
    return ms > 0.0 ? std::to_string(static_cast<int>(std::lround(ms))) + " ms" : std::string("Off");
}

constexpr const char* kLookaheadHelp =
    "Lets the pedal hear your notes a few milliseconds before it plays them, so it can find each note's pitch "
    "before the note starts: notes start on time, without wrong notes from the pick's attack. The whole plugin's "
    "sound is delayed by that much; your DAW makes up for it on playback and recording, but you hear the delay "
    "while you play (10 ms is like standing 3.4 m from your amp). Click to pick Off, 5, 10, 15, 20 or "
    "30 ms. Off adds no delay.";
constexpr const char* kA2SizeHelp =
    "For A2 models, which come in two sizes in one file. Full is the whole model: the most accurate sound, and "
    "the most CPU. Lite is a slimmed-down version of it that sounds nearly the same for a fraction of the CPU; use "
    "it when you run several amps, a split or a slow computer. Click to switch. Other models ignore it. The size "
    "new blocks start with is in Settings.";
constexpr float kTunerInTuneCents = 2.0f;

// How long the audio engine gets to start a changed chain before the UI reports blocks that aren't running
constexpr auto kStatusGrace = std::chrono::milliseconds(2500);

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Hit {
    Knob,         // index = host parameter
    Toggle,       // index = host parameter (0/1 parameters)
    Momentary,    // index = host parameter: 1 while the mouse is held, then 0
    Tile,         // index = position in the chain
    TileBypass,
    TileRemove,
    CabWarning,   // index = position in the chain (a NAM block with no cab after it, or with two)
    PhaseFix,     // the merge panel's out-of-phase warning: click to flip the Invert switch it names
    AddBlock,     // index = Section
    Split,        // add an empty split; index = position in the chain
    Insert,       // add a block at a gap; index = drop point in the layout
    ChainScroll,  // the chain's scroll bar
    ListScroll,   // a dialog list's scroll bar
    Merge,        // select the merge
    MergeRemove,  // remove the split
    SetValue,     // index = host parameter, set to `value` (real units)
    Step,         // index = host parameter, add `value` (real units), clamped
    Slider,       // index = host parameter; horizontal drag over the rect
    MetroPanel,   // open/close the metronome panel
    MetroAccent,  // index = beat: cycle accent / normal / soft / mute
    MetroGrouping, // next beat grouping
    MetroResetAccents,
    Tap,          // tap tempo
    FaceSwitch,   // a pedal image's footswitch; index = position in the chain: turn the block on or off
    Fretbored,    // open the Fretbored web app
    GitHub,       // open the project's GitHub page
    A2Size,       // index = position in the chain
    Lookahead,    // index = position in the chain: a menu of the lookaheads
    Choice,       // index = host parameter; click opens a menu of the choices
    InClip,
    OutClip,
    DialogBackdrop,
    DialogPanel,
    DialogClose,
    DialogOk,       // Add Block's OK: keeps the block being previewed
    DialogCategory, // index = Category
    DialogItem,     // index = position in the current category's item list
    ListRow,        // index = row in fRows (the library list or the search results)
    ListStar,       // index = row in fRows
    LibraryFavorites,
    LibraryBrowse,
    LibraryUp,      // the library list: up a folder
    LibraryHome,    // the library list: straight back to the library folder (the NAM or IR root)
    LibraryCrumb,   // the library list's path: index = how many folders below the library root to go to
    TextField,      // index = Field
    FieldClear,     // index = Field: empty it
    OpenSettings,
    SettingsChoose, // index = Field (the folder field it fills)
    SettingsRescan,
    SettingsA2,     // index = 1 for Lite
    SettingsNormalize,
    SettingsParallel,
    SettingsAccent, // index = accent
    InkMode,        // index = ink::Mode: Solid (the accent), Smooth or Splotchy
    InkPreset,      // index = position in ink::presets()
    InkCustom,      // opens the colour picker
    InkCount,       // index = how many colours (2 or 3)
    InkSlot,        // index = the colour the picker edits
    AccentCustom,   // opens the colour picker for the accent (Solid)
    InkWheel,       // the picker's colour wheel: hue and saturation (drag)
    InkBrightness,  // the picker's brightness slider (drag)
    InkCopy,        // copies the picker's hex value
    InkSave,        // saves the picker's colour to the saved colours
    InkSaved,       // index = a saved colour: use it
    InkForget,      // index = a saved colour: remove it
    SettingsUpdates, // the update check on/off
    UpdateOpen,     // the update banner: open the release page
    UpdateDismiss,  // the update banner's ×: hide it for this version
    PresetMenu,
    Undo,
    Redo,
    MenuItem,       // index = position in fMenu
    MenuFolder,     // index = position in fMenu: a preset folder (hovering opens its submenu; a click does too)
    Locate,         // index = position in the chain
    UseLibraryMatch, // index = position in the chain
    SaveFolder,     // Save Preset dialog: index = folder in fPresetFolders, -1 for the top level
    SaveConfirm,
    PresetRow,      // Presets dialog: index = row in fPresetRows
    PresetDefault,  // index = row in fPresetRows: make it the default (again: no default)
    PresetDelete,   // index = row in fPresetRows: the first click asks, the second deletes
    BlockImage,     // the block panel's Image button: index = position in the chain
    SetDefaults,    // the block panel's Set as Default: index = position in the chain
    ClearDefaults,  // ... and Clear Default: index = position in the chain
    ImageChoose,    // Images dialog: index = image in fImageList, -1 for the stock image
    ImageDelete,    // index = image in fImageList: the first click asks, the second deletes
    ImageAdd,       // add an image file to the library and use it
    CabImage,       // an IR block's cab image (block panel): index = position in the chain; opens the Cab Image dialog
    CabSection,     // Cab Image dialog: index = size in kCabSizes: show or hide its images
    CabChoose,      // Cab Image dialog: index = image in builtinCabIds(), -1 for the default
};

// What a context or preset menu item does
enum class MenuAction {
    None, // separator / label
    MidiLearn,
    MidiClear,
    ResetParam,
    ToggleBypass,
    RemoveBlock,
    ReplaceBlock,
    CopyBlock,
    PasteBlock,   // after fMenuTile, or at the end of fAddSection if it's -1
    PresetNew,
    PresetBrowse, // the preset folder's list
    PresetOpen,   // a preset file anywhere (system dialog)
    PresetSave,
    PresetSaveAs,
    PresetExport, // a copy anywhere, with its files (system dialog)
    PresetEmbed,
    PresetLoad,   // arg = index in fMenuPresets (the preset folder's presets)
    SetChoice,    // arg = the choice (steps from the spec's min) for fMenuParam
    SetLookahead, // arg = index in kLookaheads, for the block fMenuTile
    CabImage,     // the Cab Image dialog for the IR block fMenuTile
};

// The block copied with the right-click menu's Copy: its type, settings, values, bypass and image (not its MIDI
// mappings). Shared by the instances in the host's process, so a block can be pasted into another one.
std::optional<BlockState> gBlockClipboard;

struct MenuEntry {
    std::string label;
    MenuAction action;
    int arg = 0;
    bool current = false; // the open preset
    // Submenus (the preset folders): the folder an entry is listed in ("" for the menu itself), and for a folder the
    // path its own entries name as their parent. A folder opens its submenu when hovered.
    std::string parent;
    std::string submenu;
};

// Editable text fields
enum Field {
    kFieldSearch,
    kFieldNamDir,
    kFieldIrDir,
    kFieldPresetName,   // Save Preset dialog
    kFieldPresetFolder,
    kFieldInkHex,       // the colour picker's hex value
    kFieldCount
};

// Which built-in photos the Cab Image dialog picks from: cabs (IR blocks), generic pedals (NAM pedal captures), amp
// heads (NAM amps)
enum class PhotoKind { Cab, Pedal, Amp };

enum class Dialog { None, AddBlock, Settings, Menu, SavePreset, Presets, Images, CabImages, InkPicker };

// What a file picked in the file browser is for
enum class BrowseTarget { AddBlock, NamDir, IrDir, OpenPreset, ExportPreset, Locate, AddImage };

// The folders a library list showed before and after the current one (back and forward)
struct LibraryHistory {
    std::vector<std::string> back, forward;
};
constexpr size_t kMaxLibraryHistory = 100;

struct HitArea {
    Rect rect;
    Hit kind;
    int index;
    float value = 0.0f;
};

// Where a dragged block can land: position `position` of `section`; `x` is where the block's tile would start
struct DropPoint {
    Section section;
    int position;
    float x;
    int row;
};

// Positions of everything in the chain view, worked out before drawing and kept for drag and drop.
// With a split, branch A and everything outside the split are on row 0, branch B on row 1.
struct ChainLayout {
    float tile = kTileSize;
    float rowY[2] {};
    float height = kTileSize;
    std::vector<Rect> tiles;          // per block index
    Rect add[4];                      // "+" per Section; w == 0 if there's none
    Rect merge;                       // merge tile (split)
    float forkX = 0.0f;               // where the chain splits
    std::vector<Rect> wires[2];       // items on each row in order, joined by signal-flow lines
    std::vector<DropPoint> drops;
};

constexpr const char* kFretboredUrl = "https://singularity098.github.io/fretbored/";
constexpr const char* kGitHubUrl = "https://github.com/singularity098/xythos";
constexpr int kNoteValues[4] = { 2, 4, 8, 16 };

// Beat groupings, as in Fretbored. A grouping is stored as a bit mask of the beats that start a group.
std::vector<int> groupsFromMask(int beats, uint32_t mask)
{
    std::vector<int> groups;
    for (int i = 0; i < beats; ++i)
    {
        if (i == 0 || (mask >> i) & 1u)
            groups.push_back(0);
        ++groups.back();
    }
    return groups;
}

uint32_t maskFromGroups(const std::vector<int>& groups)
{
    uint32_t mask = 0;
    int beat = 0;
    for (const int g : groups)
    {
        mask |= 1u << beat;
        beat += g;
    }
    return mask;
}

// Fretbored's default: groups of 3, ending in 2+2 or 2 (7/8 is 2+2+3, 5/8 is 3+2); none below 5 beats or for
// quarter and half notes
std::vector<int> defaultGroups(int beats, int noteValue)
{
    if (noteValue < 8 || beats < 5)
        return {};
    if (beats % 3 == 0)
        return std::vector<int>(beats / 3, 3);
    if (beats == 5)
        return { 3, 2 };
    if (beats == 7)
        return { 2, 2, 3 };
    std::vector<int> groups;
    int left = beats;
    while (left > 4)
    {
        groups.push_back(3);
        left -= 3;
    }
    if (left == 4)
    {
        groups.push_back(2);
        groups.push_back(2);
    }
    else if (left > 0)
    {
        groups.push_back(left);
    }
    return groups;
}

// Every way to split `beats` into groups of 2 and 3, for the Grouping button to cycle through
void groupings(int beats, std::vector<int>& current, std::vector<std::vector<int>>& out)
{
    if (beats == 0)
    {
        out.push_back(current);
        return;
    }
    for (const int g : { 2, 3 })
    {
        if (g <= beats)
        {
            current.push_back(g);
            groupings(beats - g, current, out);
            current.pop_back();
        }
    }
}

std::string groupingText(const std::vector<int>& groups)
{
    std::string text;
    for (const int g : groups)
        text += (text.empty() ? "" : "+") + std::to_string(g);
    return text.empty() ? "None" : text;
}

const char* tempoMarking(double bpm)
{
    static const std::pair<double, const char*> marks[] = { { 40, "Grave" }, { 60, "Largo" }, { 66, "Larghetto" },
        { 76, "Adagio" }, { 108, "Andante" }, { 120, "Moderato" }, { 156, "Allegro" }, { 176, "Vivace" },
        { 200, "Presto" } };
    for (const auto& [limit, name] : marks)
    {
        if (bpm < limit)
            return name;
    }
    return "Prestissimo";
}

const char* const kNoteNames[12] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };

std::string fileStem(const std::string& path)
{
    return pathToUtf8(pathFromUtf8(path).stem());
}

std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Whether every space-separated word of `query` appears in `text` (case-insensitive)
bool matchesWords(const std::string& text, const std::string& query)
{
    const std::string haystack = lowerCase(text);
    size_t pos = 0;
    const std::string q = lowerCase(query);
    while (pos < q.size())
    {
        const size_t end = q.find(' ', pos);
        const std::string word = q.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!word.empty() && haystack.find(word) == std::string::npos)
            return false;
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    return true;
}

bool matchesSearch(const LibraryEntry& entry, const std::string& query)
{
    return matchesWords(entry.name + " " + entry.folder + " " + entry.info + " " +
                            namGearName(namGear(entry.gearType)) + " " + entry.arch,
                        query);
}

bool matchesSearch(const BlockDescriptor& type, const std::string& query)
{
    return matchesWords(std::string(type.displayName) + " " + categoryName(type.category), query);
}

// Appends a Unicode code point as UTF-8
void appendUtf8(std::string& text, uint32_t c)
{
    if (c < 0x80)
    {
        text += static_cast<char>(c);
    }
    else if (c < 0x800)
    {
        text += static_cast<char>(0xc0 | (c >> 6));
        text += static_cast<char>(0x80 | (c & 0x3f));
    }
    else if (c < 0x10000)
    {
        text += static_cast<char>(0xe0 | (c >> 12));
        text += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (c & 0x3f));
    }
    else if (c < 0x110000)
    {
        text += static_cast<char>(0xf0 | (c >> 18));
        text += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
        text += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        text += static_cast<char>(0x80 | (c & 0x3f));
    }
}

// Removes the last UTF-8 character
void popUtf8(std::string& text)
{
    while (!text.empty())
    {
        const auto c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xc0) != 0x80)
            break;
    }
}

int64_t fileTime(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// The block type for a file the user picked, by extension: .nam -> NAM model, .wav -> IR
const BlockDescriptor* blockTypeForFile(const char* filename)
{
    const size_t len = std::strlen(filename);
    if (len < 4)
        return nullptr;
    std::string ext(filename + len - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".nam")
        return &kNamBlockDescriptor;
    if (ext == ".wav")
        return &kIrBlockDescriptor;
    return nullptr;
}

// One row of the Add Block dialog's list (a library folder, or the search results): a category heading, a built-in
// block type, or a library file (`type` is then the NAM or IR block)
struct AddRow {
    const char* heading = nullptr;
    const BlockDescriptor* type = nullptr;
    const LibraryEntry* entry = nullptr;
    std::string folder;      // a subfolder to open (its path relative to the library root), when not empty
    size_t files = 0;        // a subfolder: how many files it holds, all the way down
    bool showFolder = true;  // a file: show the folder it's in (not while browsing that folder)
};

// One built-in block type in the Add Block dialog. (NAM and IR show the library instead.)
struct AddItem {
    std::string label;
    const BlockDescriptor* type;
};

bool isLibraryCategory(Category category)
{
    return category == Category::Nam || category == Category::Ir;
}

std::vector<AddItem> addItems(Category category)
{
    std::vector<AddItem> items;
    if (isLibraryCategory(category))
        return items;
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        if (type->category == category)
            items.push_back({ type->displayName, type });
    }
    return items;
}

} // namespace

// The split branches' phase check (engine/PhaseCheck.hpp), on a thread of its own so the UI never waits for it: one
// check at a time, started again later if one is running; joined when the UI goes away
class PhaseWorker {
public:
    ~PhaseWorker()
    {
        fCancel.store(true);
        if (fThread.joinable())
            fThread.join();
    }

    // Checks these branches, unless a check is still running (try again later: false)
    bool start(std::vector<PhaseBlock> a, std::vector<PhaseBlock> b, double alignMs, std::string key)
    {
        if (fBusy.load())
            return false;
        if (fThread.joinable())
            fThread.join();
        fBusy.store(true);
        fCancel.store(false);
        fThread = std::thread([this, a = std::move(a), b = std::move(b), alignMs, key = std::move(key)] {
            const PhaseResult result = compareBranchPolarity(a, b, alignMs, &fCancel);
            {
                const std::lock_guard<std::mutex> lock(fMutex);
                fResult = result;
                fResultKey = key;
                fFresh = true;
            }
            fBusy.store(false);
        });
        return true;
    }

    void cancel() { fCancel.store(true); }

    // A finished check's result and what it was for, once
    bool take(PhaseResult& result, std::string& key)
    {
        const std::lock_guard<std::mutex> lock(fMutex);
        if (!fFresh)
            return false;
        fFresh = false;
        result = fResult;
        key = fResultKey;
        return true;
    }

private:
    std::thread fThread;
    std::atomic<bool> fBusy { false }, fCancel { false };
    std::mutex fMutex;
    PhaseResult fResult;
    std::string fResultKey;
    bool fFresh = false;
};

class XythosUI : public UI
{
public:
    XythosUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();
        fUiFont = createFontFromMemory("jost", kUiFontLight, kUiFontLightSize, false);
        fUiFontSmall = createFontFromMemory("jost-regular", kUiFontRegular, kUiFontRegularSize, false);
        const FontId dejavu = nvgFindFont(getContext(), NANOVG_DEJAVU_SANS_TTF);
        for (FontId font : { fUiFont, fUiFontSmall })
        {
            if (font >= 0)
                nvgAddFallbackFontId(getContext(), font, dejavu); // ★, ▾
        }

        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (!isSlotParam(i))
                fParams[i] = globalParamSpec(i).def;
        }

        fSettings = Settings::load();
        fDefaultPending = !fSettings.defaultPreset.empty();
        fSettingsTime = fileTime(Settings::defaultPath());
        applySettings();
        rescan();
        fUpdateCache = UpdateCache::load();
        fUpdateCacheTime = fileTime(UpdateCache::defaultPath());

        const double scaleFactor = getScaleFactor();
        setGeometryConstraints(XYTHOS_UI_MIN_WIDTH * scaleFactor, XYTHOS_UI_MIN_HEIGHT * scaleFactor, false);
        if (d_isNotEqual(scaleFactor, 1.0))
            setSize(DISTRHO_UI_DEFAULT_WIDTH * scaleFactor, DISTRHO_UI_DEFAULT_HEIGHT * scaleFactor);
    }

protected:
    // ---------------------------------------------------------------------------------------------------------------
    // DSP -> UI

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= kParamCount)
            return;
        fParams[index] = value;
        // automation and MIDI change values in the host; presets and undo need them too
        if (isSlotParam(index))
            syncChainValue(index, value);
        if (index == kParamInstanceId && value >= 1.0f && static_cast<uint32_t>(value) != fInstanceId)
        {
            fInstanceId = static_cast<uint32_t>(value);
            // The host may not have sent us the chain (Reaper's LV2 doesn't): read the DSP's copy. Only when the
            // instance becomes known: some hosts (jalv) report output parameters over and over, and reloading would
            // throw away the undo history each time.
            if (!fGotChainState)
                loadExchangeChain();
        }
        if (index == kParamMidiCcCount && fLearnParam >= 0 && value != fLearnCcCount)
            finishMidiLearn();
        else if (index == kParamMidiCcCount && fLearnParam < 0)
            forwardMidiCc();
        if (index == kParamTunerFrequency && value > 0.0f)
            tunerReading(value);
        if (index == kParamMetroBeatCount)
            fBeatAt = std::chrono::steady_clock::now();
        if (index == kParamInClip && value >= 0.5f)
            fInClipLatched = true;
        if (index == kParamOutClip && value >= 0.5f)
            fOutClipLatched = true;
        repaint();
    }

    void stateChanged(const char* const key, const char* const value) override
    {
        if (std::strcmp(key, "chain") != 0)
            return;
        if (std::optional<ChainState> chain = ChainState::fromJson(value))
        {
            // an empty state is a new instance's (it may still get the default preset); a saved chain is never empty
            fGotChainState = value[0] != '\0';
            fChain = std::move(*chain);
            fMigratedSlots = migrateBlocks(fChain);
            fSavedChain = fChain; // the host's state: nothing to mark as modified, nothing to undo
            fUndo.clear();
            chainChanged();
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void uiFileBrowserSelected(const char* const filename) override
    {
        if (filename == nullptr)
            return;
        switch (fBrowseTarget)
        {
        case BrowseTarget::OpenPreset:
            openPreset(filename);
            return;
        case BrowseTarget::ExportPreset: {
            std::string path = filename;
            if (!lowerCase(path).ends_with(Preset::kExtension))
                path += Preset::kExtension;
            exportPreset(path);
            return;
        }
        case BrowseTarget::Locate:
            locateFile(fLocateIndex, filename);
            return;
        case BrowseTarget::AddImage:
            addImageFile(filename);
            return;
        default:
            break;
        }
        if (fBrowseTarget == BrowseTarget::NamDir || fBrowseTarget == BrowseTarget::IrDir)
        {
            // without a system folder picker, DPF's browser picks a file and the folder is the one it's in
            const Field field = fBrowseTarget == BrowseTarget::NamDir ? kFieldNamDir : kFieldIrDir;
            fFields[field] = pathToUtf8(pathFromUtf8(filename).parent_path());
            commitField(field);
            return;
        }
        const BlockDescriptor* type = blockTypeForFile(filename);
        if (type == nullptr)
            return;
        nlohmann::json config = { { "path", filename } };
        if (const std::optional<std::string> hash = hashLibraryFile(filename))
            config["hash"] = *hash;
        addBlock(type, std::move(config));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Drawing

    void onNanoDisplay() override
    {
        const auto drawStart = std::chrono::steady_clock::now();
        disableVsync(fVsyncDrawable);
        defineArrowCursor(fCursorDrawable);
        const float scale = static_cast<float>(getScaleFactor());
        fWidth = getWidth() / scale;
        fHeight = getHeight() / scale;
        fHits.clear();
        fDrawnFields.clear();
        ++fFrame;

        save();
        this->scale(scale, scale);
        currentTransform(fFrameXform);
        updateInkImage();
        fontSize(14.0f);

        drawBackground();
        drawTopBar();
        if (fMetroPanel)
        {
            drawMetronome();
        }
        else if (fParams[kParamTuner] >= 0.5f)
        {
            drawTuner();
        }
        else
        {
            drawChain();
            drawSelectedPanel();
        }
        drawBottomBar();
        drawToast();
        if (fDialog == Dialog::AddBlock)
            drawAddDialog();
        else if (fDialog == Dialog::Settings)
            drawSettingsDialog();
        else if (fDialog == Dialog::Menu)
            drawMenu();
        else if (fDialog == Dialog::SavePreset)
            drawSavePresetDialog();
        else if (fDialog == Dialog::Presets)
            drawPresetsDialog();
        else if (fDialog == Dialog::Images)
            drawImagesDialog();
        else if (fDialog == Dialog::CabImages)
            drawCabDialog();
        else if (fDialog == Dialog::InkPicker)
            drawInkPicker();
        drawHelp();
        if (fShowFps)
            drawFps(drawStart);

        restore();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Input

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX() / getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());

        if (!ev.press)
        {
            if (ev.button == kMouseButtonLeft)
                return onRelease(x, y);
            if (ev.button == kMouseButtonMiddle && fPanning)
            {
                fPanning = false;
                return true;
            }
            return false;
        }
        if (ev.button == kMouseButtonRight)
            return onRightClick(x, y);
        if (ev.button == kMouseButtonMiddle)
        {
            // a middle click on a block turns it on or off (owner, 2026-10-30; it removed it); middle-drag anywhere
            // else over the chain scrolls it
            const bool chainShown = !fMetroPanel && fParams[kParamTuner] < 0.5f;
            if (!chainShown || fDialog != Dialog::None || y < kChainTop - 12.0f
                || y > kChainTop + fLayout.height + 20.0f)
                return false;
            if (const HitArea* hit = hitAt(x, y);
                hit && (hit->kind == Hit::Tile || hit->kind == Hit::TileBypass || hit->kind == Hit::TileRemove))
            {
                toggleBlockBypass(hit->index);
                return true;
            }
            fPanning = true;
            fScrollGrabX = x;
            fScrollGrabValue = fScrollX;
            return true;
        }
        // a mouse's back and forward buttons (DPF numbers them 4 and 5 everywhere)
        if (ev.button == 4 || ev.button == 5)
            return stepLibraryHistory(ev.button == 5);
        if (ev.button != kMouseButtonLeft)
            return false;

        const bool doubleClick = ev.time - fLastClickTime < kDoubleClickMs;
        fLastClickTime = ev.time;

        const HitArea* hit = hitAt(x, y);

        // clicking anywhere but the focused field ends editing it
        if (fFocusedField >= 0
            && !(hit && (hit->kind == Hit::TextField || hit->kind == Hit::FieldClear) && hit->index == fFocusedField))
            blurField(true);

        // a Delete waiting for its second click is called off by any other click
        if (!(hit && hit->kind == Hit::PresetDelete))
            fPresetDeleteArmed.clear();

        if (hit == nullptr)
            return false;

        switch (hit->kind)
        {
        case Hit::Knob:
            if (isSlot(hit->index))
                beginEdit(); // one undo step per gesture
            if (doubleClick)
            {
                setNormalized(hit->index, defaultNormalized(hit->index), true);
                return true;
            }
            fDragParam = hit->index;
            fDragStartX = x;
            fDragStartY = y;
            fDragStartValue = normalized(hit->index);
            editParameter(hit->index, true);
            return true;

        case Hit::Momentary:
            // a footswitch press: no undo step, and back to 0 on release
            if (fMomentaryReleasePending)
                releaseMomentary();
            fMomentaryParam = hit->index;
            fMomentaryPressedAt = std::chrono::steady_clock::now();
            fMomentaryReleasePending = false;
            setNormalized(static_cast<uint32_t>(hit->index), 1.0f, true);
            return true;

        case Hit::Toggle:
            if (isSlot(hit->index))
                beginEdit();
            setNormalized(hit->index, normalized(hit->index) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(hit->index);
            return true;

        case Hit::Tile:
            if (doubleClick && fSelected == hit->index)
            {
                // double-click a block to replace it
                fPressTile = -1;
                openReplaceDialog(hit->index);
                return true;
            }
            fSelected = hit->index;
            fMergeSelected = false;
            fPressTile = hit->index;
            fPressX = x;
            fPressY = y;
            fDraggingTile = false;
            repaint();
            return true;

        case Hit::TileBypass:
        case Hit::FaceSwitch:
            toggleBlockBypass(hit->index);
            return true;

        case Hit::TileRemove:
            removeBlock(hit->index);
            return true;

        case Hit::PhaseFix:
            setNormalized(static_cast<uint32_t>(hit->index), fParams[hit->index] >= 0.5f ? 0.0f : 1.0f, true);
            return true;

        case Hit::CabWarning:
            fSelected = hit->index;
            fMergeSelected = false;
            repaint();
            return true;

        case Hit::Split: {
            beginEdit();
            // an empty split at the gap that was clicked: both branches start empty
            fChain.addSplit(hit->index, hit->index);
            for (uint32_t index = kParamMergeBalance; index <= kParamMergeInvertB; ++index)
                setHostParameter(index, globalParamSpec(index).def);
            setHostParameter(kParamMergeAlign, globalParamSpec(kParamMergeAlign).def);
            fMergeSelected = true;
            sendChain();
            return true;
        }

        case Hit::Insert: {
            if (hit->index >= static_cast<int>(fLayout.drops.size()))
                return true;
            const DropPoint& drop = fLayout.drops[hit->index];
            fAddSection = drop.section;
            fAddPosition = drop.position;
            fReplaceSlot = -1;
            openDialog(Dialog::AddBlock);
            return true;
        }

        case Hit::ChainScroll: {
            // a click beside the thumb moves it there; then it follows the mouse
            const float thumbX = scrollThumbX();
            if (x < thumbX || x > thumbX + fScrollThumbW)
                setScrollX((x - fScrollThumbW / 2 - fScrollTrack.x) / (fScrollTrack.w - fScrollThumbW) * fMaxScrollX);
            fScrollDragging = true;
            fScrollGrabX = x;
            fScrollGrabValue = fScrollX;
            return true;
        }

        case Hit::ListScroll: {
            const float thumbY = listThumbY();
            if (y < thumbY || y > thumbY + fListThumbH)
                fListScroll = std::clamp((y - fListThumbH / 2 - fListTrack.y) / (fListTrack.h - fListThumbH)
                                             * fListMaxScroll, 0.0f, fListMaxScroll);
            fListScrollDragging = true;
            fListGrabY = y;
            fListGrabValue = fListScroll;
            repaint();
            return true;
        }

        case Hit::Merge:
            fMergeSelected = true;
            repaint();
            return true;

        case Hit::MergeRemove:
            beginEdit();
            fChain.removeSplit();
            fMergeSelected = false;
            sendChain();
            return true;

        case Hit::AddBlock:
            fAddSection = static_cast<Section>(hit->index);
            fAddPosition = kMaxBlocks;
            fReplaceSlot = -1;
            openDialog(Dialog::AddBlock);
            return true;

        case Hit::Choice: {
            const ParamSpec* spec = slotSpec(hit->index);
            if (!spec)
                return true;
            openChoiceMenu(static_cast<uint32_t>(hit->index), *spec, hit->rect);
            return true;
        }

        case Hit::InkMode:
            updateSettings([mode = hit->index](Settings& settings) { settings.inkMode = mode; });
            return true;

        case Hit::InkPreset:
            updateSettings([index = static_cast<size_t>(hit->index)](Settings& settings) {
                settings.inkColors = hexList(ink::presets()[index].colors);
                if (settings.inkMode == 0)
                    settings.inkMode = 2;
            });
            return true;

        case Hit::InkCustom:
            if (fSettings.inkMode == 0)
                updateSettings([](Settings& settings) { settings.inkMode = 2; });
            openPicker(false);
            return true;

        case Hit::AccentCustom:
            openPicker(true);
            return true;

        case Hit::InkCount:
            updateSettings([count = static_cast<size_t>(hit->index)](Settings& settings) {
                settings.inkColors.resize(count, "#ffffff");
            });
            fInkSlot = std::min(fInkSlot, hit->index - 1);
            pickSlot(fInkSlot);
            return true;

        case Hit::InkSlot:
            pickSlot(hit->index);
            return true;

        case Hit::InkWheel:
        case Hit::InkBrightness:
            fInkDrag = hit->kind == Hit::InkWheel ? 0 : 1;
            dragPicker(x, y);
            return true;

        case Hit::InkCopy:
            copyText(pickerHex(fInkSlot));
            showToast("Copied " + pickerHex(fInkSlot));
            return true;

        case Hit::InkSave:
            updateSettings([hex = pickerHex(fInkSlot)](Settings& settings) {
                if (std::find(settings.savedColors.begin(), settings.savedColors.end(), hex) == settings.savedColors.end())
                    settings.savedColors.push_back(hex);
            });
            return true;

        case Hit::InkSaved:
            if (hit->index < static_cast<int>(fSettings.savedColors.size()))
            {
                if (const auto color = ink::parseHex(fSettings.savedColors[hit->index]))
                {
                    fPickHsv = ink::toHsv(*color);
                    setPickerColor(*color, true);
                }
            }
            return true;

        case Hit::InkForget:
            updateSettings([index = static_cast<size_t>(hit->index)](Settings& settings) {
                if (index < settings.savedColors.size())
                    settings.savedColors.erase(settings.savedColors.begin() + static_cast<std::ptrdiff_t>(index));
            });
            return true;

        case Hit::DialogBackdrop:
        case Hit::DialogClose:
            closeDialog();
            return true;

        case Hit::DialogPanel:
            return true;

        case Hit::DialogCategory:
            fDialogCategory = static_cast<Category>(hit->index);
            fFields[kFieldSearch].clear(); // leave the search results for the category
            fListScroll = 0.0f;
            repaint();
            return true;

        // Add Block: a click picks a block and puts it in the chain to hear it (a live preview); OK, Enter or a
        // double-click keeps it, Cancel or Esc takes it out again
        case Hit::DialogItem: {
            const std::vector<AddItem> items = addItems(fDialogCategory);
            if (hit->index >= static_cast<int>(items.size()))
                return true;
            pickAddItem(items[hit->index]);
            if (doubleClick)
                commitPreview();
            return true;
        }

        case Hit::ListRow: {
            if (hit->index < static_cast<int>(fRows.size()) && !fRows[hit->index].folder.empty())
            {
                openLibraryDir(fRows[hit->index].folder);
                return true;
            }
            if (hit->index >= static_cast<int>(fRows.size()) || fRows[hit->index].type == nullptr)
                return true;
            pickAddRow(fRows[hit->index]);
            if (doubleClick)
                commitPreview();
            return true;
        }

        case Hit::DialogOk:
            commitPreview();
            return true;

        case Hit::ListStar: {
            if (hit->index >= static_cast<int>(fRows.size()) || fRows[hit->index].entry == nullptr)
                return true;
            const std::string hash = fRows[hit->index].entry->hash;
            updateSettings([&hash](Settings& settings) {
                if (!settings.favorites.erase(hash))
                    settings.favorites.insert(hash);
            });
            return true;
        }

        case Hit::LibraryUp: {
            const std::string& dir = libraryDir();
            const size_t slash = dir.rfind('/');
            openLibraryDir(slash == std::string::npos ? std::string() : dir.substr(0, slash));
            return true;
        }

        case Hit::LibraryHome:
            openLibraryDir(std::string());
            return true;

        case Hit::LibraryCrumb: {
            // the first `index` folders of where we are
            const std::string& dir = libraryDir();
            size_t end = 0;
            for (int level = 0; level < hit->index && end != std::string::npos; ++level)
                end = dir.find('/', end == 0 ? 0 : end + 1);
            openLibraryDir(hit->index == 0 ? std::string() : dir.substr(0, end));
            return true;
        }

        case Hit::LibraryFavorites:
            fFavoritesOnly = !fFavoritesOnly;
            fListScroll = 0.0f;
            repaint();
            return true;

        case Hit::LibraryBrowse: {
            const bool ir = fDialogCategory == Category::Ir;
            closeDialog();
            browse(BrowseTarget::AddBlock, ir ? "Add a cab IR (.wav)" : "Add a NAM model (.nam)",
                   ir ? fSettings.irDir : fSettings.namDir);
            return true;
        }

        case Hit::TextField: {
            if (fFocusedField != hit->index)
                focusField(static_cast<Field>(hit->index));
            TextEdit& edit = focusedEdit();
            const size_t pos = fieldPosAt(hit->index, x);
            if (doubleClick)
                edit.selectWordAt(pos);
            else
                edit.setCursor(pos, (ev.mod & kModifierShift) != 0);
            fSelectingText = !doubleClick;
            repaint();
            return true;
        }

        case Hit::FieldClear:
            fFields[hit->index].clear();
            focusField(static_cast<Field>(hit->index));
            if (hit->index == kFieldSearch)
                fListScroll = 0.0f;
            repaint();
            return true;

        case Hit::OpenSettings:
            openDialog(Dialog::Settings);
            return true;

        case Hit::SettingsChoose: {
            const bool nam = hit->index == kFieldNamDir;
            fFolderField = static_cast<Field>(hit->index);
            fFolderPicker.open(nam ? "Choose your NAM folder" : "Choose your IR folder",
                               nam ? fSettings.namDir : fSettings.irDir, getWindow().getNativeWindowHandle());
            return true;
        }

        case Hit::SettingsRescan:
            rescan();
            repaint();
            return true;

        case Hit::SettingsA2:
            updateSettings([lite = hit->index == 1](Settings& settings) { settings.a2Lite = lite; });
            return true;

        case Hit::SettingsNormalize:
            updateSettings([](Settings& settings) { settings.normalize = !settings.normalize; });
            return true;

        case Hit::SettingsParallel:
            updateSettings([](Settings& settings) { settings.parallel = !settings.parallel; });
            sendChain(); // the DSP reads the setting when it builds the chain
            return true;

        case Hit::SettingsUpdates:
            updateSettings([](Settings& settings) { settings.checkUpdates = !settings.checkUpdates; });
            return true;

        case Hit::UpdateOpen:
            openUrl(kLatestReleasePageUrl);
            return true;

        case Hit::UpdateDismiss:
            updateSettings([version = fUpdateCache.latest](Settings& settings) {
                settings.dismissedUpdate = version;
            });
            return true;

        case Hit::SaveFolder:
            fFields[kFieldPresetFolder] = hit->index >= 0 && hit->index < static_cast<int>(fPresetFolders.size())
                                            ? fPresetFolders[hit->index] : std::string();
            focusField(kFieldPresetName);
            repaint();
            return true;

        case Hit::SaveConfirm:
            confirmSavePreset();
            return true;

        case Hit::PresetRow:
            if (hit->index < static_cast<int>(fPresetRows.size()) && !fPresetRows[hit->index].path.empty())
            {
                const std::string path = fPresetRows[hit->index].path;
                closeDialog();
                openPreset(path);
            }
            return true;

        case Hit::PresetDefault:
            if (hit->index < static_cast<int>(fPresetRows.size()))
            {
                const std::string path = fPresetRows[hit->index].path;
                updateSettings([&path](Settings& settings) {
                    settings.defaultPreset = settings.defaultPreset == path ? std::string() : path;
                });
            }
            return true;

        case Hit::PresetDelete:
            if (hit->index < static_cast<int>(fPresetRows.size()))
            {
                const std::string path = fPresetRows[hit->index].path;
                if (fPresetDeleteArmed != path)
                {
                    fPresetDeleteArmed = path;
                    repaint();
                    return true;
                }
                fPresetDeleteArmed.clear();
                if (deletePreset(presetsDir(), path))
                {
                    if (fSettings.defaultPreset == path)
                        updateSettings([](Settings& settings) { settings.defaultPreset.clear(); });
                }
                else
                {
                    showToast("Couldn't delete the preset " + fileStem(path));
                }
                fPresetList = listPresets(presetsDir());
                repaint();
            }
            return true;

        case Hit::SettingsAccent:
            updateSettings([accent = hit->index](Settings& settings) {
                settings.accent = accent;
                settings.accentColor.clear();
            });
            return true;

        case Hit::BlockImage:
            openImagesDialog(hit->index);
            return true;

        case Hit::SetDefaults:
            setBlockDefaults(hit->index);
            return true;

        case Hit::ClearDefaults:
            if (hit->index >= 0 && hit->index < static_cast<int>(fChain.blocks.size()))
                clearBlockDefaults(fChain.blocks[hit->index].typeId);
            return true;

        case Hit::CabImage:
            return false; // the panel's image picks another on a right-click only (owner, 2026-10-31)

        case Hit::CabSection:
            if (hit->index >= 0 && hit->index < static_cast<int>(fCabOpen.size()))
                fCabOpen[hit->index] = !fCabOpen[hit->index];
            repaint();
            return true;

        case Hit::CabChoose: {
            // kept in the settings by the IR's (or NAM pedal's) content, so it follows the file into every chain and
            // preset
            const PhotoKind kind = fCabKind;
            const std::vector<std::string>& ids = photoIds(kind);
            const std::string id = hit->index >= 0 && hit->index < static_cast<int>(ids.size()) ? ids[hit->index]
                                                                                                 : std::string();
            const std::string key = cabKey(fCabHash, fCabPath);
            updateSettings([&key, &id, kind](Settings& settings) {
                std::map<std::string, std::string>& picks = kind == PhotoKind::Pedal ? settings.pedalImages
                                                          : kind == PhotoKind::Amp   ? settings.ampImages
                                                                                     : settings.cabImages;
                if (id.empty())
                    picks.erase(key);
                else
                    picks[key] = id;
            });
            // opened for a block that shows one of the user's images: the built-in photo picked replaces it
            if (const BlockState* block = imagesBlock(); block && !block->image.empty())
                setBlockImage(fImagesBlock, std::string());
            closeDialog();
            return true;
        }

        case Hit::ImageChoose:
            if (const BlockState* block = imagesBlock())
            {
                const std::string id = hit->index >= 0 && hit->index < static_cast<int>(fImageList.size())
                                           ? fImageList[hit->index].id
                                           : std::string();
                if (id != block->image)
                    setBlockImage(fImagesBlock, id);
            }
            closeDialog();
            return true;

        case Hit::ImageDelete:
            if (hit->index >= 0 && hit->index < static_cast<int>(fImageList.size()))
            {
                const UserImage image = fImageList[hit->index];
                if (fImageDeleteArmed != image.id)
                {
                    fImageDeleteArmed = image.id;
                    repaint();
                    return true;
                }
                fImageDeleteArmed.clear();
                // blocks that use it show their stock image again
                if (!deleteUserImage(fImageKind, image.id))
                    showToast("Couldn't delete the image " + image.name);
                fImages.erase(image.path);
                fImageList = listUserImages(fImageKind);
                repaint();
            }
            return true;

        case Hit::ImageAdd:
            browse(BrowseTarget::AddImage, "Add an image (PNG, JPEG or WebP)", fImageBrowseDir);
            return true;


        case Hit::PresetMenu:
            openPresetMenu();
            return true;

        case Hit::Undo:
            undo();
            return true;

        case Hit::Redo:
            redo();
            return true;

        case Hit::MenuItem:
            runMenu(hit->index);
            return true;

        case Hit::MenuFolder:
            return true; // its submenu opens on hover, so a click (or a tap) has nothing left to do

        case Hit::Locate:
            fLocateIndex = hit->index;
            browse(BrowseTarget::Locate, "Locate the missing file",
                   fChain.blocks[hit->index].typeId == kIrBlockDescriptor.typeId ? fSettings.irDir : fSettings.namDir);
            return true;

        case Hit::UseLibraryMatch: {
            const LibraryEntry* match = libraryMatch(fChain.blocks[hit->index]);
            if (match)
                locateFile(hit->index, match->path);
            return true;
        }

        case Hit::A2Size: {
            beginEdit();
            nlohmann::json& config = fChain.blocks[hit->index].config;
            config["a2Size"] = config.value("a2Size", 1.0) >= 0.5 ? 0.0 : 1.0;
            sendChain();
            return true;
        }

        case Hit::Lookahead: {
            fMenuTile = hit->index;
            fMenuParam = kNoParam;
            const double current = fChain.blocks[hit->index].config.value("lookahead", 0.0);
            std::vector<MenuEntry> entries;
            for (size_t i = 0; i < std::size(kLookaheads); ++i)
            {
                MenuEntry item { lookaheadText(kLookaheads[i]), MenuAction::SetLookahead, static_cast<int>(i) };
                item.current = std::fabs(kLookaheads[i] - current) < 0.5;
                entries.push_back(std::move(item));
            }
            openMenu(hit->rect.x, hit->rect.y + hit->rect.h + 4.0f, std::move(entries), 160.0f);
            return true;
        }

        case Hit::SetValue:
            setValue(static_cast<uint32_t>(hit->index), hit->value);
            if (hit->index == static_cast<int>(kParamTunerStrobe)) // Needle or Strobe: the runes view is over them
                setValue(kParamTunerRunes, 0.0f);
            return true;

        case Hit::Slider:
            fDragParam = hit->index;
            fSliderRect = hit->rect;
            editParameter(static_cast<uint32_t>(hit->index), true);
            setNormalized(static_cast<uint32_t>(hit->index), std::clamp((x - hit->rect.x) / hit->rect.w, 0.0f, 1.0f),
                          false);
            return true;

        case Hit::MetroPanel:
            fMetroPanel = !fMetroPanel;
            repaint();
            return true;

        case Hit::MetroAccent: {
            const uint32_t index = kParamMetroAccent1 + static_cast<uint32_t>(hit->index);
            setValue(index, static_cast<float>((static_cast<int>(fParams[index]) + 3) % 4)); // 3 > 2 > 1 > 0 > 3
            return true;
        }

        case Hit::MetroGrouping:
            nextGrouping();
            return true;

        case Hit::MetroResetAccents:
            applyDefaultAccents();
            return true;

        case Hit::Tap:
            tap();
            return true;

        case Hit::Fretbored:
            openUrl(kFretboredUrl);
            return true;

        case Hit::GitHub:
            openUrl(kGitHubUrl);
            return true;

        case Hit::Step:
            setValue(static_cast<uint32_t>(hit->index), fParams[hit->index] + hit->value);
            return true;

        case Hit::InClip:
            fInClipLatched = false;
            repaint();
            return true;

        case Hit::OutClip:
            fOutClipLatched = false;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX() / getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());
        fMouseX = x;
        fMouseY = y;
        ++fMotionCount;

        if (fPanning)
        {
            setScrollX(fScrollGrabValue - (x - fScrollGrabX));
            return true;
        }
        if (fSelectingText && fFocusedField >= 0)
        {
            focusedEdit().setCursor(fieldPosAt(fFocusedField, x), true);
            repaint();
            return true;
        }
        if (fListScrollDragging)
        {
            if (fListTrack.h > fListThumbH)
                fListScroll = std::clamp(fListGrabValue + (y - fListGrabY) * fListMaxScroll / (fListTrack.h - fListThumbH),
                                         0.0f, fListMaxScroll);
            repaint();
            return true;
        }
        if (fScrollDragging)
        {
            if (fScrollTrack.w > fScrollThumbW)
                setScrollX(fScrollGrabValue + (x - fScrollGrabX) * fMaxScrollX / (fScrollTrack.w - fScrollThumbW));
            return true;
        }

        if (fInkDrag >= 0)
        {
            dragPicker(x, y);
            return true;
        }
        if (fDragParam >= 0 && fSliderRect.w > 0.0f)
        {
            setNormalized(static_cast<uint32_t>(fDragParam), std::clamp((x - fSliderRect.x) / fSliderRect.w, 0.0f, 1.0f),
                          false);
            return true;
        }
        if (fDragParam >= 0)
        {
            // 200 px for the full range, up or right; Shift for fine control
            const float range = (ev.mod & kModifierShift) ? 2000.0f : 200.0f;
            const float value =
                std::clamp(fDragStartValue + ((fDragStartY - y) + (x - fDragStartX)) / range, 0.0f, 1.0f);
            setNormalized(static_cast<uint32_t>(fDragParam), value, false);
            fDragStartValue = value;
            fDragStartX = x;
            fDragStartY = y;
            return true;
        }

        if (fPressTile >= 0 && (std::fabs(x - fPressX) > 6.0f || std::fabs(y - fPressY) > 6.0f))
            fDraggingTile = true;

        repaint();
        return fDraggingTile;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!ev.press)
            return fFocusedField >= 0;

        // back and forward through the library list's folders, as in a file browser or web browser: Alt+Left/Right,
        // and Cmd+[ / Cmd+] on macOS (also with the search field focused, where they mean nothing else)
        const bool alt = (ev.mod & kModifierAlt) != 0, super = (ev.mod & kModifierSuper) != 0;
        if (((alt && (ev.key == kKeyLeft || ev.key == kKeyRight)) || (super && (ev.key == '[' || ev.key == ']')))
            && stepLibraryHistory(ev.key == kKeyRight || ev.key == ']'))
            return true;

        // Tab and Shift+Tab move between the text fields on screen, in the order they're drawn
        if (ev.key == kKeyTab && (fFocusedField >= 0 || fDialog != Dialog::None) && tabToField((ev.mod & kModifierShift) != 0))
            return true;

        if (fFocusedField >= 0)
        {
            TextEdit& edit = focusedEdit();
            const bool ctrl = (ev.mod & kModifierControl) != 0;
            const bool shift = (ev.mod & kModifierShift) != 0;
            if (ev.key == kKeyBackspace)
                edit.backspace(ctrl);
            else if (ev.key == kKeyDelete)
                edit.del(ctrl);
            else if (ev.key == kKeyLeft)
                edit.moveLeft(ctrl, shift);
            else if (ev.key == kKeyRight)
                edit.moveRight(ctrl, shift);
            else if (ev.key == kKeyHome)
                edit.moveHome(shift);
            else if (ev.key == kKeyEnd)
                edit.moveEnd(shift);
            else if (ev.key == kKeyEnter && fDialog == Dialog::SavePreset)
                confirmSavePreset();
            else if (fDialog == Dialog::AddBlock && (ev.key == kKeyUp || ev.key == kKeyDown))
                movePick(0, ev.key == kKeyUp ? -1 : 1);
            else if (fDialog == Dialog::AddBlock && ev.key == kKeyEnter)
                commitPreview();
            else if (ev.key == kKeyEnter)
                blurField(true);
            else if (ev.key == kKeyEscape && (fFocusedField == kFieldSearch || fDialog == Dialog::SavePreset))
                closeDialog();
            else if (ev.key == kKeyEscape)
                blurField(false);
            else if (ctrl && (ev.key == 'a' || ev.key == 'A'))
                edit.selectAll();
            else if (ctrl && (ev.key == 'c' || ev.key == 'C'))
                copyText(edit.selectedText());
            else if (ctrl && (ev.key == 'x' || ev.key == 'X'))
            {
                copyText(edit.selectedText());
                edit.deleteSelection();
            }
            else if ((ctrl && (ev.key == 'v' || ev.key == 'V')) || (shift && ev.key == kKeyInsert))
                pasteInto(edit);
            else if (ctrl && (ev.key == 'u' || ev.key == 'U'))
            {
                edit.selectAll();
                edit.deleteSelection();
            }
            else
                return true; // characters arrive through onCharacterInput
            fFields[fFocusedField] = edit.text;
            if (fFocusedField == kFieldSearch)
                fListScroll = 0.0f;
            else if (fFocusedField == kFieldInkHex)
                hexTyped();
            repaint();
            return true;
        }

        if (ev.key == kKeyEscape && fLearnParam >= 0)
        {
            cancelMidiLearn();
            return true;
        }
        if (ev.key == kKeyEscape && fDialog != Dialog::None)
        {
            closeDialog();
            return true;
        }
        if (fDialog == Dialog::AddBlock)
        {
            switch (ev.key)
            {
            case kKeyUp:    movePick(0, -1); return true;
            case kKeyDown:  movePick(0, 1); return true;
            case kKeyLeft:  movePick(-1, 0); return true;
            case kKeyRight: movePick(1, 0); return true;
            case kKeyEnter: commitPreview(); return true;
            default:        break;
            }
        }
        // (not while Add Block may be previewing a block: the chain isn't settled)
        if (fDialog == Dialog::AddBlock)
            return false;
        if ((ev.mod & kModifierControl) && (ev.key == 'z' || ev.key == 'Z'))
        {
            if (ev.mod & kModifierShift)
                redo();
            else
                undo();
            return true;
        }
        if ((ev.mod & kModifierControl) && (ev.key == 'y' || ev.key == 'Y'))
        {
            redo();
            return true;
        }
        return fDialog == Dialog::None && metronomeKey(ev);
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fFocusedField < 0)
            return false;
        // control characters (and Ctrl+key combinations) are handled in onKeyboard
        if (ev.character >= 32 && ev.character != 127)
        {
            // keys a VST3 host forwards (onKeyDown) come with the character but no string
            std::string str;
            if (ev.string[0] != '\0')
                str = ev.string;
            else
                appendUtf8(str, ev.character);
            TextEdit& edit = focusedEdit();
            edit.insert(str);
            fFields[fFocusedField] = edit.text;
            if (fFocusedField == kFieldSearch)
                fListScroll = 0.0f;
            else if (fFocusedField == kFieldInkHex)
                hexTyped();
            repaint();
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (fDialog == Dialog::Menu)
        {
            // the panel under the mouse scrolls: the menu itself or one of its submenus
            const float mx = static_cast<float>(ev.pos.getX() / getScaleFactor());
            const float my = static_cast<float>(ev.pos.getY() / getScaleFactor());
            for (size_t level = fMenuPanels.size(); level-- > 1;)
            {
                if (fMenuPanels[level].contains(mx, my) && level < fMenuScrolls.size())
                {
                    fMenuScrolls[level] = std::max(0.0f, fMenuScrolls[level] - static_cast<float>(ev.delta.getY()) * 40.0f);
                    repaint();
                    return true;
                }
            }
        }
        if (fDialog != Dialog::None)
        {
            fListScroll = std::max(0.0f, fListScroll - static_cast<float>(ev.delta.getY()) * 40.0f);
            repaint();
            return true;
        }
        const float x = static_cast<float>(ev.pos.getX() / getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());
        if (const HitArea* hit = hitAt(x, y); hit && hit->kind == Hit::Knob && ev.delta.getY() != 0.0)
        {
            scrollKnob(static_cast<uint32_t>(hit->index), static_cast<float>(ev.delta.getY()), ev.mod & kModifierShift);
            return true;
        }
        if (y < kChainTop || y > kChainTop + fLayout.height)
            return false;
        const float delta = static_cast<float>(ev.delta.getY() != 0.0 ? ev.delta.getY() : ev.delta.getX());
        setScrollX(fScrollX - delta * 40.0f);
        return true;
    }

    // The wheel over a knob turns it: 2% a notch, Shift for fine control. A burst of notches on one knob is one undo step
    void scrollKnob(uint32_t index, float notches, bool fine)
    {
        const auto now = std::chrono::steady_clock::now();
        if (isSlot(index) && (static_cast<int>(index) != fScrollKnob || now - fScrollKnobAt > std::chrono::seconds(1)))
            beginEdit();
        fScrollKnob = static_cast<int>(index);
        fScrollKnobAt = now;
        const float step = fine ? 0.002f : 0.02f;
        setNormalized(index, std::clamp(normalized(index) + notches * step, 0.0f, 1.0f), true);
    }

    // Metronome shortcuts, while its panel is open (and so only while this window has the keyboard focus):
    // Space start/stop, Up/Down tempo (Shift: 5 BPM), T tap
    bool metronomeKey(const KeyboardEvent& ev)
    {
        if (!ev.press || !fMetroPanel)
            return false;
        const float step = (ev.mod & kModifierShift) ? 5.0f : 1.0f;
        switch (ev.key)
        {
        case kKeySpace:
            setValue(kParamMetronome, fParams[kParamMetronome] >= 0.5f ? 0.0f : 1.0f);
            return true;
        case kKeyUp:
        case kKeyRight:
            setValue(kParamMetroTempo, std::round(fParams[kParamMetroTempo]) + step);
            return true;
        case kKeyDown:
        case kKeyLeft:
            setValue(kParamMetroTempo, std::round(fParams[kParamMetroTempo]) - step);
            return true;
        case 't':
        case 'T':
            tap();
            return true;
        }
        return false;
    }

    void uiIdle() override
    {
        ++fIdleCount;
        pollPhaseCheck();
        if (fShowFps)
            repaint(); // the readout itself counts frames, so keep drawing
        if (fMomentaryReleasePending && std::chrono::steady_clock::now() - fMomentaryPressedAt >= kMomentaryMinHold)
            releaseMomentary();
        if (!fMigratedSlots.empty())
        {
            // the host has the old blocks' unused values (0; -12 dB for NAM tone): the defaults first, then the chain
            // that turns them on
            const std::vector<MigratedSlot> migrated = std::exchange(fMigratedSlots, {});
            for (BlockState& block : fChain.blocks)
            {
                for (const MigratedSlot& done : migrated)
                {
                    if (done.slot != block.slot || block.typeId != done.migration->type->typeId)
                        continue;
                    setMigratedDefaults(block, *done.migration);
                    for (int p = done.migration->first; p <= done.migration->last; ++p)
                        setHostParameter(slotParam(block.slot, p), block.params[p]);
                }
            }
            fSavedChain = fChain;
            sendChain();
        }
        if (!fToast.empty() && std::chrono::steady_clock::now() - fToastAt > std::chrono::seconds(5))
            repaint();
        if (std::optional<LibraryScanner::Result> result = fScanner.poll())
        {
            fNamLibrary = std::move(result->nam);
            fNamInfo.clear();
            fIrLibrary = std::move(result->ir);
            fScanned = true;
            repaint();
        }
        if (fScanning != fScanner.busy())
        {
            fScanning = fScanner.busy();
            repaint();
        }
        pollFolderPicker();
        // give the host a moment to deliver a saved chain before deciding this is a new instance
        if (fDefaultPending && fInstanceId != 0 && std::chrono::steady_clock::now() - fOpenedAt > std::chrono::seconds(1))
            loadDefaultPreset();

        // Settings are shared by all instances: pick up changes made by another one
        const auto now = std::chrono::steady_clock::now();
        if (now - fSettingsCheckedAt > std::chrono::seconds(1))
        {
            fSettingsCheckedAt = now;
            const int64_t time = fileTime(Settings::defaultPath());
            if (time != fSettingsTime)
            {
                fSettingsTime = time;
                const Settings settings = Settings::load();
                const bool foldersChanged = settings.namDir != fSettings.namDir || settings.irDir != fSettings.irDir;
                fSettings = settings;
                applySettings();
                if (foldersChanged)
                    rescan();
                repaint();
            }
            pollUpdateCheck();
        }

        // the metronome's beat pulse
        if (fParams[kParamMetronome] >= 0.5f || fMetroPanel)
            repaint();

        // the tuner animates (needle easing, strobe)
        const float dt = std::min(0.1f, std::chrono::duration<float>(now - fLastIdle).count());
        fLastIdle = now;
        if (fParams[kParamTuner] >= 0.5f)
        {
            const float target = tunerActive() ? fTunerCents : 0.0f;
            fNeedleCents += (target - fNeedleCents) * std::min(1.0f, dt * 12.0f);
            for (int row = 0; row < 3; ++row)
                fStrobePhase[row] += target * 5.0f * static_cast<float>(1 << row) * dt;
            repaint();
        }

        // the hover help appears once the mouse has rested on a control
        if (fHelpKind >= 0 && !fHelpShown && now - fHelpSince >= kHelpDelay)
            repaint();

        // redraw once the grace period ends, so blocks that didn't start get marked
        if (fStatusPending && statusSettled())
        {
            fStatusPending = false;
            repaint();
        }
    }

private:
    // ---------------------------------------------------------------------------------------------------------------
    // Parameters

    bool isSlot(uint32_t index) const { return isSlotParam(index); }

    const BlockState* blockInSlot(int slot) const
    {
        for (const BlockState& block : fChain.blocks)
        {
            if (block.slot == slot)
                return &block;
        }
        return nullptr;
    }

    const ParamSpec* slotSpec(uint32_t index) const
    {
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        if (param < 0)
            return nullptr;
        for (const BlockState& block : fChain.blocks)
        {
            if (block.slot == slot)
            {
                const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
                return type && param < type->numParams ? &type->params[param] : nullptr;
            }
        }
        return nullptr;
    }

    float normalized(uint32_t index) const
    {
        if (isSlot(index))
            return fParams[index];
        const GlobalParamSpec& spec = globalParamSpec(index);
        return (fParams[index] - spec.min) / (spec.max - spec.min);
    }

    float defaultNormalized(uint32_t index) const
    {
        if (isSlot(index))
        {
            // the user's own default for the block's type (Set as Default), else the type's
            const uint32_t offset = index - kSlotParamBase;
            const int param = static_cast<int>(offset % kSlotParamCount) - 1;
            const int block = indexOfSlot(static_cast<int>(offset / kSlotParamCount));
            if (param >= 0 && block >= 0)
                if (const BlockDescriptor* type = BlockRegistry::builtin().find(fChain.blocks[block].typeId);
                    type && param < type->numParams)
                    return blockDefault(*type, param);
            const ParamSpec* spec = slotSpec(index);
            return spec ? spec->toNormalized(spec->def) : 0.0f;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        return (spec.def - spec.min) / (spec.max - spec.min);
    }

    // Control `param` of a new block of `type`, normalized: the user's default (Settings::blockDefaults) or the type's
    float blockDefault(const BlockDescriptor& type, int param) const
    {
        const ParamSpec& spec = type.params[param];
        if (const auto defaults = fSettings.blockDefaults.find(type.typeId); defaults != fSettings.blockDefaults.end())
            if (const auto value = defaults->second.find(spec.name); value != defaults->second.end())
                return std::clamp(spec.toNormalized(value->second), 0.0f, 1.0f);
        return spec.toNormalized(spec.def);
    }

    bool hasBlockDefaults(const std::string& typeId) const { return fSettings.blockDefaults.count(typeId) != 0; }

    // Set as Default: block `index`'s values become what new blocks of its type start with (not the footswitch
    // actions, which are only ever pressed)
    void setBlockDefaults(int index)
    {
        if (index < 0 || index >= static_cast<int>(fChain.blocks.size()))
            return;
        const BlockState& block = fChain.blocks[index];
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        if (type == nullptr)
            return;
        std::map<std::string, float> values;
        for (int p = 0; p < type->numParams; ++p)
        {
            const ParamSpec& spec = type->params[p];
            if (spec.scale != ParamScale::Momentary)
                values[spec.name] = spec.toValue(fParams[slotParam(block.slot, p)]);
        }
        updateSettings([&](Settings& settings) { settings.blockDefaults[type->typeId] = values; });
        showToast(std::string("New ") + type->displayName + " blocks start with these settings");
    }

    void clearBlockDefaults(const std::string& typeId)
    {
        updateSettings([&](Settings& settings) { settings.blockDefaults.erase(typeId); });
        if (const BlockDescriptor* type = BlockRegistry::builtin().find(typeId))
            showToast(std::string("New ") + type->displayName + " blocks start with the stock settings again");
    }

    void setNormalized(uint32_t index, float n, bool gesture)
    {
        float value = n;
        if (!isSlot(index))
        {
            const GlobalParamSpec& spec = globalParamSpec(index);
            value = spec.min + n * (spec.max - spec.min);
        }
        watchLooperTap(index, value);
        if (gesture)
            editParameter(index, true);
        setHostParameter(index, value);
        if (gesture)
            editParameter(index, false);

        if (isSlot(index))
            syncChainValue(index, n);
        repaint();
    }

    // Keeps the chain's copy of a slot parameter (for presets and undo) in step with the host's value
    void syncChainValue(uint32_t index, float value)
    {
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        for (BlockState& block : fChain.blocks)
        {
            if (block.slot != slot)
                continue;
            if (param < 0)
                block.bypass = value >= 0.5f;
            else
                block.params[param] = value;
        }
    }

    // Sets a global parameter in its real units, as one gesture
    void setValue(uint32_t index, float value)
    {
        const GlobalParamSpec& spec = globalParamSpec(index);
        if (spec.flags & (kParamFlagInteger | kParamFlagToggle))
            value = std::round(value);
        setNormalized(index, (std::clamp(value, spec.min, spec.max) - spec.min) / (spec.max - spec.min), true);
        if (index == kParamMetroBeats || index == kParamMetroNoteValue)
            applyDefaultAccents();
    }

    int metroBeats() const { return std::clamp(static_cast<int>(fParams[kParamMetroBeats]), 1, 16); }
    int metroNoteValue() const { return kNoteValues[std::clamp(static_cast<int>(fParams[kParamMetroNoteValue]), 0, 3)]; }

    // Accents follow the grouping: the first beat accented, the first of every other group normal, the rest soft.
    // Without a grouping: the first beat accented, the rest normal.
    void applyGrouping(const std::vector<int>& groups)
    {
        const int beats = metroBeats();
        std::vector<int> levels(16, 2);
        levels[0] = 3;
        int sum = 0;
        for (const int g : groups)
            sum += g;
        if (!groups.empty() && sum == beats)
        {
            int beat = 0;
            for (size_t gi = 0; gi < groups.size(); ++gi)
            {
                for (int k = 0; k < groups[gi]; ++k)
                    levels[beat + k] = k == 0 ? (gi == 0 ? 3 : 2) : 1;
                beat += groups[gi];
            }
        }
        setValue(kParamMetroGrouping, static_cast<float>(groups.empty() || sum != beats ? 0 : maskFromGroups(groups)));
        for (int i = 0; i < 16; ++i)
            setValue(kParamMetroAccent1 + static_cast<uint32_t>(i), static_cast<float>(levels[i]));
    }

    void applyDefaultAccents() { applyGrouping(defaultGroups(metroBeats(), metroNoteValue())); }

    std::vector<int> currentGroups() const
    {
        const auto mask = static_cast<uint32_t>(fParams[kParamMetroGrouping]);
        return mask == 0 ? std::vector<int> {} : groupsFromMask(metroBeats(), mask);
    }

    // Cycles through no grouping and every split of the bar into 2s and 3s
    void nextGrouping()
    {
        std::vector<std::vector<int>> all { {} };
        std::vector<int> current;
        groupings(metroBeats(), current, all);
        const std::vector<int> groups = currentGroups();
        size_t i = 0;
        while (i < all.size() && all[i] != groups)
            ++i;
        applyGrouping(all[(i + 1) % all.size()]);
    }

    void tap()
    {
        const auto now = std::chrono::steady_clock::now();
        fTapAt = now;
        if (const double bpm = fTapTempo.tap(std::chrono::duration<double>(now.time_since_epoch()).count()); bpm > 0.0)
            setValue(kParamMetroTempo, static_cast<float>(std::round(bpm)));
        repaint();
    }

    // A looper's Tap pressed (from the panel or a MIDI footswitch: about to be written to the host as `value`): the
    // UI times the taps as the block does, and sets its BPM control to the tempo tapped, so the knob shows it
    void watchLooperTap(uint32_t index, float value)
    {
        if (!isSlot(index) || value < 0.5f || fParams[index] >= 0.5f)
            return;
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        const BlockState* block = blockInSlot(slot);
        if (param != kLooperParamTap || block == nullptr || block->typeId != kLooperBlockDescriptor.typeId)
            return;
        TapTempo& taps = fLooperTaps[static_cast<size_t>(slot)];
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (const double bpm = taps.tap(seconds); bpm > 0.0)
        {
            const ParamSpec& spec = kLooperBlockDescriptor.params[kLooperParamBpm];
            const uint32_t bpmIndex = slotParam(slot, kLooperParamBpm);
            const float n = spec.toNormalized(std::clamp(static_cast<float>(std::round(bpm)), spec.min, spec.max));
            editParameter(bpmIndex, true);
            setHostParameter(bpmIndex, n);
            editParameter(bpmIndex, false);
            syncChainValue(bpmIndex, n);
        }
    }

    // A new frequency from the tuner. Readings are median-filtered; a clear change of note starts over.
    void tunerReading(float frequency)
    {
        if (fTunerHistory.size() > 0)
        {
            std::vector<float> sorted = fTunerHistory;
            std::sort(sorted.begin(), sorted.end());
            if (std::fabs(1200.0f * std::log2(frequency / sorted[sorted.size() / 2])) > 60.0f)
                fTunerHistory.clear();
        }
        fTunerHistory.push_back(frequency);
        if (fTunerHistory.size() > 5)
            fTunerHistory.erase(fTunerHistory.begin());
        std::vector<float> sorted = fTunerHistory;
        std::sort(sorted.begin(), sorted.end());
        fTunerFrequency = sorted[sorted.size() / 2];

        const float midi = 69.0f + 12.0f * std::log2(fTunerFrequency / fParams[kParamTunerReference]);
        const int note = static_cast<int>(std::lround(midi));
        const float cents = (midi - note) * 100.0f;
        if (note != fTunerNote || !tunerActive())
            fTunerCents = cents;
        else
            fTunerCents += (cents - fTunerCents) * 0.35f;
        fTunerNote = note;
        fTunerSeenAt = std::chrono::steady_clock::now();
    }

    bool tunerActive() const
    {
        return fTunerNote >= 0 && std::chrono::steady_clock::now() - fTunerSeenAt < kTunerHold;
    }

    void setHostParameter(uint32_t index, float value)
    {
        fParams[index] = value;
        setParameterValue(index, value);
    }

    void syncBypassToChain(uint32_t index)
    {
        for (BlockState& block : fChain.blocks)
        {
            if (slotBypassParam(block.slot) == index)
                block.bypass = fParams[index] >= 0.5f;
        }
    }

    std::string valueText(uint32_t index) const
    {
        char buf[32];
        if (isSlot(index))
        {
            const ParamSpec* spec = slotSpec(index);
            if (!spec)
                return "";
            const float v = spec->toValue(fParams[index]);
            if (spec->scale == ParamScale::Toggle)
                return v >= 0.5f ? "On" : "Off";
            if (spec->scale == ParamScale::Choice && spec->choices)
                return spec->choices[static_cast<int>(v - spec->min)];
            const uint32_t offset = index - kSlotParamBase;
            const int slot = static_cast<int>(offset / kSlotParamCount);
            const int param = static_cast<int>(offset % kSlotParamCount) - 1;
            const BlockState* block = blockInSlot(slot);
            const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
            if (type && type->syncedParam == param && fParams[slotParam(slot, type->syncParam)] >= 0.5f)
                return type->syncedIsRate ? rateNoteName(rateNoteIndex(fParams[index])) : noteName(timeNoteIndex(fParams[index]));
            if (spec->unit[0] == ':')
            {
                std::snprintf(buf, sizeof(buf), "%.1f%s", v, spec->unit);
                return buf;
            }
            if (std::strcmp(spec->unit, "ms") == 0 && v > 0.0f && v < 10.0f)
            {
                std::snprintf(buf, sizeof(buf), "%.2f ms", v);
                return buf;
            }
            if (std::strcmp(spec->unit, "BPM") == 0)
                std::snprintf(buf, sizeof(buf), "%.0f BPM", v);
            else if (std::strcmp(spec->unit, "Hz") == 0 && v >= 1000.0f)
                std::snprintf(buf, sizeof(buf), "%.1f kHz", v / 1000.0f);
            else if (std::strcmp(spec->unit, "Hz") == 0 && v < 10.0f)
                std::snprintf(buf, sizeof(buf), "%.2f Hz", v); // LFO rates
            else
                std::snprintf(buf, sizeof(buf), "%.1f %s", v, spec->unit);
            return buf;
        }
        const float v = fParams[index];
        if (index == kParamMergeBalance)
        {
            // how far the other branch is turned down
            if (std::fabs(v) < 0.5f)
                return "A = B";
            std::snprintf(buf, sizeof(buf), v < 0 ? "B -%.0f%%" : "A -%.0f%%", std::fabs(v));
            return buf;
        }
        if (index == kParamMergePanA || index == kParamMergePanB)
        {
            if (std::fabs(v) < 0.5f)
                return "Center";
            std::snprintf(buf, sizeof(buf), v < 0 ? "L %.0f" : "R %.0f", std::fabs(v));
            return buf;
        }
        if (index == kParamMergeAlign)
        {
            // which branch is delayed
            if (std::fabs(v) < 0.005f)
                return "In time";
            std::snprintf(buf, sizeof(buf), v < 0 ? "A +%.2f ms" : "B +%.2f ms", std::fabs(v));
            return buf;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        std::snprintf(buf, sizeof(buf), "%+.1f %s", v, spec.unit);
        return buf;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Undo/redo, presets, MIDI learn, menus

    // Call before changing fChain, so the change can be undone
    void beginEdit() { fUndo.push(fChain); }

    void toggleBlockBypass(int block)
    {
        beginEdit();
        const uint32_t index = slotBypassParam(fChain.blocks[block].slot);
        setNormalized(index, normalized(index) >= 0.5f ? 0.0f : 1.0f, true);
        syncBypassToChain(index);
    }

    // Add Block, opened on the block's own category: the block picked there takes this one's place
    // The Images dialog for the block at `block`: its kind's library (amps, cabs or pedals) and its stock image
    void openImagesDialog(int block)
    {
        if (block < 0 || block >= static_cast<int>(fChain.blocks.size()))
            return;
        const BlockState& state = fChain.blocks[block];
        if (state.typeId == kIrBlockDescriptor.typeId || isNamPedal(state) || isNamAmp(state))
        {
            // the built-in photos to pick from and the user's own, in one dialog (owner, 2026-10-31)
            openCabDialog(block);
            return;
        }
        fImagesBlock = block;
        fImageKind = imageKindFor(state.typeId);
        fImageList = listUserImages(fImageKind);
        fImageDeleteArmed.clear();
        openDialog(Dialog::Images);
    }

    // The image chooser for the IR (or NAM pedal or amp) block at `block`: the built-in photos, and the user's own
    void openCabDialog(int block)
    {
        if (block < 0 || block >= static_cast<int>(fChain.blocks.size()))
            return;
        const BlockState& state = fChain.blocks[block];
        openCabDialog(state.config.value("hash", std::string()), state.config.value("path", std::string()),
                      isNamPedal(state) ? PhotoKind::Pedal : isNamAmp(state) ? PhotoKind::Amp : PhotoKind::Cab);
        // for a block, the user's own images too (its kind's library)
        fImagesBlock = block;
        fImageKind = imageKindFor(state.typeId);
        fImageList = listUserImages(fImageKind);
        fImageDeleteArmed.clear();
    }

    // The Cab Image dialog for an IR file (its content hash and path), or the Pedal Image dialog for a NAM pedal
    // capture; from Add Block, closing it goes back there
    void openCabDialog(const std::string& hash, const std::string& path, PhotoKind kind = PhotoKind::Cab)
    {
        const bool pedal = kind != PhotoKind::Cab;
        fImagesBlock = -1; // from Add Block: a file, not a block (openCabDialog(int) sets it)
        fCabHash = hash;
        fCabPath = path;
        fCabKind = kind;
        fCabReturn = fDialog == Dialog::AddBlock ? Dialog::AddBlock : Dialog::None;
        fCabReturnScroll = fListScroll;
        // the current image's size starts open
        const std::string_view size = pedal ? std::string_view() : cabImageSize(cabImageFor(hash, path));
        for (size_t i = 0; i < fCabOpen.size(); ++i)
            fCabOpen[i] = size == kCabSizes[i];
        blurField(true);
        openDialog(Dialog::CabImages);
    }

    const BlockState* imagesBlock() const
    {
        return fImagesBlock >= 0 && fImagesBlock < static_cast<int>(fChain.blocks.size()) ? &fChain.blocks[fImagesBlock]
                                                                                          : nullptr;
    }

    // Shows block `index` with the user's image `id` ("" for its stock image); undoable, and saved with the chain
    void setBlockImage(int index, const std::string& id)
    {
        beginEdit();
        fChain.blocks[index].image = id;
        sendChain();
    }

    // The file browser picked an image for the Images dialog's block: copy it into the library and use it
    void addImageFile(const std::string& filename)
    {
        fImageBrowseDir = pathToUtf8(pathFromUtf8(filename).parent_path());
        std::string error;
        const std::optional<std::string> id = importUserImage(fImageKind, filename, &error);
        if (!id)
        {
            showToast("Couldn't add the image: " + error);
            return;
        }
        const std::string path = userImagePath(fImageKind, *id);
        fImages.erase(path); // a deleted image's name may be used again, by other content
        if (image(path) == nullptr)
        {
            deleteUserImage(fImageKind, *id);
            showToast("Couldn't show " + fileStem(filename) + ": use an 8-bit PNG, a JPEG or a WebP");
            return;
        }
        fImageList = listUserImages(fImageKind);
        if (const BlockState* block = imagesBlock(); block && block->image != *id)
            setBlockImage(fImagesBlock, *id);
        closeDialog();
    }

    void openReplaceDialog(int block)
    {
        fReplaceSlot = fChain.blocks[block].slot;
        const BlockDescriptor* type = BlockRegistry::builtin().find(fChain.blocks[block].typeId);
        if (type)
            fDialogCategory = type->category;
        openDialog(Dialog::AddBlock);
        // a NAM or IR block: its file's folder, with the file picked, so similar files are a click away (owner,
        // 2026-10-31)
        if (type == &kNamBlockDescriptor || type == &kIrBlockDescriptor)
        {
            const std::string path = fChain.blocks[block].config.value("path", std::string());
            const std::string hash = fChain.blocks[block].config.value("hash", std::string());
            const std::vector<LibraryEntry>& library = type == &kIrBlockDescriptor ? fIrLibrary : fNamLibrary;
            const LibraryEntry* found = nullptr;
            for (const LibraryEntry& entry : library)
                if (!path.empty() && entry.path == path)
                {
                    found = &entry;
                    break;
                }
            if (found == nullptr && !hash.empty())
                for (const LibraryEntry& entry : library)
                    if (entry.hash == hash)
                    {
                        found = &entry;
                        break;
                    }
            if (found != nullptr)
            {
                fFavoritesOnly = false;
                openLibraryDir(found->folder);
                fPickKey = "file:" + found->path;
                fScrollToPick = true;
            }
        }
    }

    // Controls added to a block type after it was released: blocks saved before them lack the config key, and
    // their new controls hold the host's unused values (0), so the DSP ignores them until the key is there
    struct Migration {
        const BlockDescriptor* type;
        const char* key;
        int first, last; // the controls added
    };
    static constexpr Migration kMigrations[] = {
        { &kNamBlockDescriptor, kNamToneKey, kNamParamBass, kNamParamTreble },             // NamBlock.hpp
        { &kLooperBlockDescriptor, kLooperCountInKey, kLooperParamMode, kLooperParamClick }, // LooperBlock.hpp
        { &kLooperBlockDescriptor, kLooperLatencyKey, kLooperParamLatency, kLooperParamLatency },
    };

    // A block's migration: the slot and which one (a type can have several)
    struct MigratedSlot {
        int slot;
        const Migration* migration;
    };

    // Sets a migrated block's new controls to their defaults (NAM tone: 0 dB, flat)
    static void setMigratedDefaults(BlockState& block, const Migration& migration)
    {
        for (int p = migration.first; p <= migration.last; ++p)
        {
            const ParamSpec& spec = migration.type->params[p];
            block.params[p] = spec.toNormalized(spec.def);
        }
    }

    // Blocks from before their type's later controls (kMigrations) get them at their defaults, and the config marks
    // them, so the DSP applies them. Returns the migrations done.
    static std::vector<MigratedSlot> migrateBlocks(ChainState& chain)
    {
        std::vector<MigratedSlot> changed;
        for (BlockState& block : chain.blocks)
        {
            for (const Migration& migration : kMigrations)
            {
                if (block.typeId != migration.type->typeId || block.config.contains(migration.key))
                    continue;
                setMigratedDefaults(block, migration);
                block.config[migration.key] = true;
                changed.push_back({ static_cast<int>(block.slot), &migration });
            }
        }
        return changed;
    }

    // Replaces the chain (undo, redo, presets): the host gets every block's values back
    void applyChain(ChainState chain, bool restoreLatches = false)
    {
        migrateBlocks(chain);
        fChain = std::move(chain);
        for (const BlockState& block : fChain.blocks)
        {
            setHostParameter(slotBypassParam(block.slot), block.bypass ? 1.0f : 0.0f);
            for (int p = 0; p < kParamsPerSlot; ++p)
                setHostParameter(slotParam(block.slot, p), block.params[p]);
        }
        if (fSelected >= static_cast<int>(fChain.blocks.size()))
            fSelected = static_cast<int>(fChain.blocks.size()) - 1;
        if (fSelected < 0 && !fChain.blocks.empty())
            fSelected = 0;
        sendChain(restoreLatches);
    }

    void undo()
    {
        if (std::optional<ChainState> state = fUndo.undo(fChain))
            applyChain(std::move(*state));
    }

    void redo()
    {
        if (std::optional<ChainState> state = fUndo.redo(fChain))
            applyChain(std::move(*state));
    }

    void removeBlock(int index)
    {
        beginEdit();
        takeOutBlock(index);
        if (fSelected >= static_cast<int>(fChain.blocks.size()))
            fSelected = static_cast<int>(fChain.blocks.size()) - 1;
        sendChain();
    }

    // Takes a block out of the chain with its MIDI mappings (they follow the block); no undo step or send
    void takeOutBlock(int index)
    {
        const int slot = fChain.blocks[index].slot;
        fChain.remove(index);
        fChain.clearMidiMappings(slotBypassParam(slot));
        for (int p = 0; p < kParamsPerSlot; ++p)
            fChain.clearMidiMappings(slotParam(slot, p));
    }

    void showToast(std::string message)
    {
        fToast = std::move(message);
        fToastAt = std::chrono::steady_clock::now();
        repaint();
    }

    std::string presetName() const
    {
        return fChain.preset.empty() ? "Untitled" : fileStem(fChain.preset);
    }

    bool presetModified() const { return fChain != fSavedChain; }

    Preset currentPreset() const
    {
        Preset preset;
        preset.chain = withLiveLatches(fChain);
        for (uint32_t index : presetGlobalParams())
            preset.globals[globalParamSpec(index).symbol] = fParams[index];
        return preset;
    }

    // Saves into the preset folder, without embedding the files (Export makes a copy with them)
    void savePreset(const std::string& path)
    {
        std::string error;
        if (!savePresetFile(path, currentPreset(), false, &error))
        {
            showToast("Couldn't save the preset: " + error);
            return;
        }
        fChain.preset = path;
        fSavedChain = fChain;
        updateSettings([&path](Settings& settings) { settings.addRecentPreset(path); });
        sendChain();
        showToast("Saved " + fileStem(path));
    }

    void exportPreset(const std::string& path)
    {
        std::string error;
        if (!savePresetFile(path, currentPreset(), fSettings.embedFiles, &error))
        {
            showToast("Couldn't export the preset: " + error);
            return;
        }
        showToast("Exported " + fileStem(path) + (fSettings.embedFiles ? " with its files" : ""));
    }

    // Save As: the dialog for a name and a folder in the preset folder
    void openSavePresetDialog()
    {
        const std::filesystem::path dir = presetsDir();
        fPresetFolders = listPresetFolders(dir);
        fFields[kFieldPresetName] = fChain.preset.empty() ? std::string() : fileStem(fChain.preset);
        fFields[kFieldPresetFolder].clear();
        if (!fChain.preset.empty() && isInsideDir(dir, pathFromUtf8(fChain.preset)))
        {
            std::string folder = pathToUtf8(pathFromUtf8(fChain.preset).parent_path().lexically_relative(dir));
            std::replace(folder.begin(), folder.end(), '\\', '/');
            fFields[kFieldPresetFolder] = folder == "." ? std::string() : folder;
        }
        openDialog(Dialog::SavePreset);
        focusField(kFieldPresetName);
    }

    void confirmSavePreset()
    {
        const std::optional<std::filesystem::path> path =
            presetPath(presetsDir(), fFields[kFieldPresetFolder], fFields[kFieldPresetName]);
        if (!path)
            return; // the dialog says what's wrong
        closeDialog();
        savePreset(pathToUtf8(*path));
    }

    // New plugin instances load the default preset (Settings::defaultPreset). The UI does it, since presets set host
    // parameters: once it knows the instance, if the host gave it no chain (a new instance rather than a saved one).
    void loadDefaultPreset()
    {
        fDefaultPending = false;
        const std::string path = fSettings.defaultPreset;
        const std::optional<std::string> exchange = readChainExchange(fInstanceId);
        if (path.empty() || fGotChainState || !fChain.blocks.empty() || fChain.split || !fileExists(path)
            || (exchange && !exchange->empty()))
            return;
        openPreset(path);
        fUndo.clear(); // the starting point, not an edit
    }

    void openPreset(const std::string& path)
    {
        std::string error;
        const std::optional<LoadedPreset> loaded = loadPresetFile(path, [this](const std::string& hash) {
            for (const auto* library : { &fNamLibrary, &fIrLibrary })
            {
                for (const LibraryEntry& entry : *library)
                {
                    if (entry.hash == hash)
                        return entry.path;
                }
            }
            return std::string();
        }, &error);
        if (!loaded || !loaded->preset.chain.validate(BlockRegistry::builtin(), &error))
        {
            showToast("Couldn't open the preset: " + error);
            return;
        }

        beginEdit();
        for (uint32_t index : presetGlobalParams())
        {
            // values a preset doesn't have (older presets have no merge settings) go back to their defaults
            const GlobalParamSpec& spec = globalParamSpec(index);
            const auto it = loaded->preset.globals.find(spec.symbol);
            setHostParameter(index, it != loaded->preset.globals.end() ? it->second : spec.def);
        }
        fSelected = loaded->preset.chain.blocks.empty() ? -1 : 0;
        fMergeSelected = false;
        applyChain(loaded->preset.chain, true);
        fSavedChain = fChain;
        updateSettings([&path](Settings& settings) { settings.addRecentPreset(path); });

        const auto missing = std::count(loaded->sources.begin(), loaded->sources.end(), FileSource::Missing);
        if (missing > 0)
            showToast(std::to_string(missing) + (missing == 1 ? " file is" : " files are")
                      + " missing. Select the grayed-out block to locate it.");
        else
            showToast("Opened " + presetName());
    }

    void newPreset()
    {
        beginEdit();
        for (uint32_t index : presetGlobalParams())
            setHostParameter(index, globalParamSpec(index).def);
        ChainState empty;
        applyChain(empty);
        fSavedChain = fChain;
        fSelected = -1;
        fMergeSelected = false;
    }

    // A library file with the same content as a block's (missing) file
    const LibraryEntry* libraryMatch(const BlockState& block) const
    {
        const std::string hash = block.config.value("hash", std::string());
        if (hash.empty())
            return nullptr;
        const std::vector<LibraryEntry>& library =
            block.typeId == kIrBlockDescriptor.typeId ? fIrLibrary : fNamLibrary;
        for (const LibraryEntry& entry : library)
        {
            if (entry.hash == hash)
                return &entry;
        }
        return nullptr;
    }

    // What a NAM block's file says about itself: from the library scan, else read once and kept
    const NamFileInfo& namFileInfo(const BlockState& block) const
    {
        static const NamFileInfo kNone;
        const std::string path = block.config.value("path", std::string());
        if (block.typeId != kNamBlockDescriptor.typeId || path.empty())
            return kNone;
        if (const auto it = fNamInfo.find(path); it != fNamInfo.end())
            return it->second;
        NamFileInfo info;
        const auto entry = std::find_if(fNamLibrary.begin(), fNamLibrary.end(),
                                        [&](const LibraryEntry& e) { return e.path == path; });
        if (entry != fNamLibrary.end())
            info = { entry->info, entry->gearType, entry->arch };
        else if (!fileExists(path))
            return kNone; // not kept: it may be located or put back
        else if (const std::optional<std::string> bytes = readLibraryFile(path))
            info = describeNamFile(*bytes);
        return fNamInfo.emplace(path, std::move(info)).first->second;
    }

    // "Amp Head · A1 Standard": a NAM model's gear type and architecture, either of them if only one is known
    static std::string namTypeText(const std::string& gearType, const std::string& arch, const char* separator = "  ·  ")
    {
        const std::string gear = namGearName(namGear(gearType));
        return gear.empty() || arch.empty() ? gear + arch : gear + separator + arch;
    }

    // A NAM block whose model doesn't fit the cabs after it: an amp without its cab and no cab after it on every
    // path, or an amp with its cab and another cab after it (an IR, or a NAM cabinet). Bypassed blocks don't count.
    enum class CabProblem { None, NoCab, TwoCabs };

    CabProblem cabProblem(int index) const
    {
        if (index < 0 || index >= static_cast<int>(fChain.blocks.size()))
            return CabProblem::None;
        const BlockState& block = fChain.blocks[index];
        if (fParams[slotBypassParam(block.slot)] >= 0.5f)
            return CabProblem::None;
        const NamGear gear = namGear(namFileInfo(block).gearType);
        if (gear != NamGear::Amp && gear != NamGear::AmpCab)
            return CabProblem::None;

        // whether any of `indices` after `index` is a cab
        auto cabIn = [&](Section section) {
            for (const int i : fChain.sectionIndices(section))
            {
                const BlockState& other = fChain.blocks[i];
                if (i <= index || fParams[slotBypassParam(other.slot)] >= 0.5f)
                    continue;
                if (other.typeId == kIrBlockDescriptor.typeId ||
                    namGear(namFileInfo(other).gearType) == NamGear::Cab)
                    return true;
            }
            return false;
        };
        bool any = false, every = false; // a cab after it on some path, on every path
        switch (fChain.sectionOf(index))
        {
        case Section::Pre: {
            const bool pre = cabIn(Section::Pre), post = cabIn(Section::Post);
            const bool a = fChain.split && cabIn(Section::A), b = fChain.split && cabIn(Section::B);
            any = pre || post || a || b;
            every = pre || post || (a && b);
            break;
        }
        case Section::A:
        case Section::B:
            any = every = cabIn(fChain.sectionOf(index)) || cabIn(Section::Post);
            break;
        case Section::Post:
            any = every = cabIn(Section::Post);
            break;
        }
        if (gear == NamGear::AmpCab && any)
            return CabProblem::TwoCabs;
        if (gear == NamGear::Amp && !every)
            return CabProblem::NoCab;
        return CabProblem::None;
    }

    // A small warning sign: a triangle with "!", centred at (cx, cy)
    void drawWarningSign(float cx, float cy, float r)
    {
        beginPath();
        moveTo(cx, cy - r);
        lineTo(cx + r * 1.1f, cy + r * 0.8f);
        lineTo(cx - r * 1.1f, cy + r * 0.8f);
        closePath();
        fillColor(Color(0x00, 0x00, 0x00, 0.6f));
        fill();
        strokeColor(theme::warn);
        strokeWidth(1.2f);
        lineJoin(ROUND);
        stroke();
        beginPath();
        moveTo(cx, cy - r * 0.4f);
        lineTo(cx, cy + r * 0.2f);
        lineCap(ROUND);
        stroke();
        beginPath();
        circle(cx, cy + r * 0.48f, 0.9f);
        fillColor(theme::warn);
        fill();
        lineCap(BUTT);
        lineJoin(MITER);
    }

    void locateFile(int index, const std::string& path)
    {
        if (index < 0 || index >= static_cast<int>(fChain.blocks.size()))
            return;
        beginEdit();
        nlohmann::json& config = fChain.blocks[index].config;
        config["path"] = path;
        if (const std::optional<std::string> hash = hashLibraryFile(path))
            config["hash"] = *hash;
        sendChain();
    }

    // MIDI learn: the next CC the DSP reports is mapped to the parameter
    void startMidiLearn(uint32_t param)
    {
        fLearnParam = static_cast<int>(param);
        fLearnCcCount = fParams[kParamMidiCcCount];
        setHostParameter(kParamMidiLearn, 1.0f);
        repaint();
    }

    void finishMidiLearn()
    {
        const int value = static_cast<int>(fParams[kParamMidiLastCc]);
        beginEdit();
        fChain.setMidiMapping({ value / 128, value % 128, static_cast<uint32_t>(fLearnParam) });
        cancelMidiLearn();
        sendChain();
    }

    // The DSP already applied a mapped CC, but not every format lets it tell the host (LV2 mostly doesn't), so the
    // UI sets the host's value too. That also keeps the knob on screen in step.
    void forwardMidiCc()
    {
        const int cc = static_cast<int>(fParams[kParamMidiLastCc]);
        for (const MidiMapping& mapping : fChain.midi)
        {
            if (mapping.channel * 128 + mapping.cc == cc)
            {
                const float value = midiCcToValue(mapping.param, static_cast<int>(fParams[kParamMidiLastValue]));
                watchLooperTap(mapping.param, value);
                setHostParameter(mapping.param, value);
                if (isSlot(mapping.param))
                    syncChainValue(mapping.param, value);
                repaint();
                return;
            }
        }
    }

    void cancelMidiLearn()
    {
        fLearnParam = -1;
        setHostParameter(kParamMidiLearn, 0.0f);
        repaint();
    }

    std::string paramName(uint32_t index) const
    {
        if (!isSlot(index))
            return globalParamSpec(index).name;
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        const BlockState* block = blockInSlot(slot);
        const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
        std::string name = type ? type->displayName : "Slot";
        if (block && !block->config.value("path", std::string()).empty())
            name = fileStem(block->config.value("path", std::string()));
        return name + " " + (param < 0 ? "Bypass" : (type && param < type->numParams ? type->params[param].name : "?"));
    }

    static std::string ccText(const MidiMapping& mapping)
    {
        return "CC " + std::to_string(mapping.cc) + " (ch " + std::to_string(mapping.channel + 1) + ")";
    }

    void openMenu(float x, float y, std::vector<MenuEntry> entries, float width = 300.0f)
    {
        fMenu = std::move(entries);
        fMenuX = x;
        fMenuY = y;
        fMenuW = width;
        fMenuOpen.clear();
        fMenuScrolls.clear();
        fMenuPanels.clear();
        fPresetMenu = false;
        openDialog(Dialog::Menu);
    }

    bool onRightClick(float x, float y)
    {
        const HitArea* hit = hitAt(x, y);
        if (hit == nullptr)
            return false;
        // an IR in Add Block's list: pick its cab image; a NAM pedal: its pedal image
        if (fDialog == Dialog::AddBlock && hit->kind == Hit::ListRow && hit->index < static_cast<int>(fRows.size())
            && fRows[hit->index].entry != nullptr)
        {
            const AddRow& row = fRows[hit->index];
            if (row.type == &kIrBlockDescriptor)
            {
                openCabDialog(row.entry->hash, row.entry->path);
                return true;
            }
            if (row.type == &kNamBlockDescriptor && namGear(row.entry->gearType) == NamGear::Pedal)
            {
                openCabDialog(row.entry->hash, row.entry->path, PhotoKind::Pedal);
                return true;
            }
            if (row.type == &kNamBlockDescriptor && isAmpGear(namGear(row.entry->gearType)))
            {
                openCabDialog(row.entry->hash, row.entry->path, PhotoKind::Amp);
                return true;
            }
        }
        if (fDialog != Dialog::None)
            return false;
        if (hit->kind == Hit::CabImage)
        {
            openImagesDialog(hit->index);
            return true;
        }

        if (hit->kind == Hit::Knob || hit->kind == Hit::Toggle || hit->kind == Hit::Momentary
            || hit->kind == Hit::Choice)
        {
            const auto param = static_cast<uint32_t>(hit->index);
            if (!isMidiMappable(param))
                return false;
            fMenuParam = param;
            std::vector<MenuEntry> entries { { paramName(param), MenuAction::None } };
            entries.push_back({ "MIDI Learn", MenuAction::MidiLearn });
            if (const MidiMapping* mapping = fChain.midiMappingFor(param))
                entries.push_back({ "Clear MIDI " + ccText(*mapping), MenuAction::MidiClear });
            if (hit->kind == Hit::Knob)
                entries.push_back({ "Reset to default", MenuAction::ResetParam });
            openMenu(x, y, std::move(entries));
            return true;
        }
        if (hit->kind == Hit::Tile)
        {
            fMenuTile = hit->index;
            fSelected = hit->index;
            const bool bypassed = fParams[slotBypassParam(fChain.blocks[hit->index].slot)] >= 0.5f;
            fMenuParam = slotBypassParam(fChain.blocks[hit->index].slot);
            std::vector<MenuEntry> entries {
                { bypassed ? "Enable" : "Bypass", MenuAction::ToggleBypass },
            };
            entries.push_back({ "Replace", MenuAction::ReplaceBlock });
            entries.push_back({ "Remove", MenuAction::RemoveBlock });
            entries.push_back({ "Image…", MenuAction::CabImage });
            entries.push_back({ "Copy", MenuAction::CopyBlock });
            if (gBlockClipboard)
                entries.push_back({ "Paste after", MenuAction::PasteBlock });
            entries.push_back({ "MIDI Learn bypass", MenuAction::MidiLearn });
            if (const MidiMapping* mapping = fChain.midiMappingFor(fMenuParam))
                entries.push_back({ "Clear MIDI " + ccText(*mapping), MenuAction::MidiClear });
            openMenu(x, y, std::move(entries));
            return true;
        }
        if (hit->kind == Hit::AddBlock && gBlockClipboard)
        {
            fMenuTile = -1;
            fMenuParam = kNoParam;
            fAddSection = static_cast<Section>(hit->index);
            openMenu(x, y, { { "Paste", MenuAction::PasteBlock } });
            return true;
        }
        return false;
    }

    // A dropdown of a choice parameter's choices, under its button
    void openChoiceMenu(uint32_t param, const ParamSpec& spec, const Rect& button)
    {
        fMenuParam = param;
        const int steps = static_cast<int>(spec.max - spec.min);
        const int current = static_cast<int>(std::lround(spec.toValue(normalized(param)) - spec.min));
        std::vector<MenuEntry> entries;
        for (int i = 0; i <= steps; ++i)
        {
            MenuEntry item { spec.choices ? spec.choices[i] : std::to_string(i), MenuAction::SetChoice, i };
            item.current = i == current;
            entries.push_back(std::move(item));
        }
        openMenu(button.x, button.y + button.h + 4.0f, std::move(entries), 160.0f);
    }

    void openPresetMenu()
    {
        fMenuParam = kNoParam;
        std::vector<MenuEntry> entries {
            { "New", MenuAction::PresetNew },
            { "Save", MenuAction::PresetSave },
            { "Save As…", MenuAction::PresetSaveAs },
            { "Manage Presets…", MenuAction::PresetBrowse },
            { "Files", MenuAction::None },
            { "Open File…", MenuAction::PresetOpen },
            { "Export…", MenuAction::PresetExport },
            { std::string("Embed files when exporting: ") + (fSettings.embedFiles ? "On" : "Off"),
              MenuAction::PresetEmbed },
        };

        // the preset folder as it is on disk: top-level presets, then a folder entry per folder that opens a submenu
        // of its presets and subfolders when hovered
        const std::vector<PresetEntry> presets = listPresets(presetsDir());
        fMenuPresets.clear();
        entries.push_back({ presets.empty() ? "Presets (none saved yet)" : "Presets", MenuAction::None });
        std::set<std::string> shownFolders;
        for (const PresetEntry& preset : presets)
        {
            std::string parent;
            if (!preset.folder.empty())
            {
                size_t pos = 0;
                for (;;)
                {
                    const size_t slash = preset.folder.find('/', pos);
                    const std::string path = preset.folder.substr(0, slash);
                    if (shownFolders.insert(path).second)
                    {
                        MenuEntry folder { path.substr(pos), MenuAction::None };
                        folder.parent = parent;
                        folder.submenu = path;
                        entries.push_back(std::move(folder));
                    }
                    parent = path;
                    if (slash == std::string::npos)
                        break;
                    pos = slash + 1;
                }
            }
            MenuEntry item { preset.name + (preset.path == fSettings.defaultPreset ? "  (default)" : ""),
                             MenuAction::PresetLoad, static_cast<int>(fMenuPresets.size()) };
            item.parent = parent;
            item.current = preset.path == fChain.preset;
            entries.push_back(std::move(item));
            fMenuPresets.push_back(preset.path);
        }
        openMenu(fPresetButtonX, kTopBarHeight - 6.0f, std::move(entries));
        fPresetMenu = true;
    }

    void runMenu(int index)
    {
        if (index < 0 || index >= static_cast<int>(fMenu.size()))
            return;
        const MenuEntry entry = fMenu[index];
        closeDialog();
        switch (entry.action)
        {
        case MenuAction::None:
            break;
        case MenuAction::MidiLearn:
            startMidiLearn(fMenuParam);
            break;
        case MenuAction::MidiClear:
            beginEdit();
            fChain.clearMidiMappings(fMenuParam);
            sendChain();
            break;
        case MenuAction::ResetParam:
            if (isSlot(fMenuParam))
                beginEdit();
            setNormalized(fMenuParam, defaultNormalized(fMenuParam), true);
            break;
        case MenuAction::ToggleBypass:
            beginEdit();
            setNormalized(fMenuParam, normalized(fMenuParam) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(fMenuParam);
            break;
        case MenuAction::RemoveBlock:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
                removeBlock(fMenuTile);
            break;
        case MenuAction::ReplaceBlock:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
                openReplaceDialog(fMenuTile);
            break;
        case MenuAction::CopyBlock:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
                gBlockClipboard = fChain.blocks[fMenuTile];
            break;
        case MenuAction::PasteBlock:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
            {
                const Section section = fChain.sectionOf(fMenuTile);
                const std::vector<int> indices = fChain.sectionIndices(section);
                pasteBlock(section, static_cast<int>(std::find(indices.begin(), indices.end(), fMenuTile)
                                                     - indices.begin()) + 1);
            }
            else if (fMenuTile < 0)
            {
                pasteBlock(fAddSection, kMaxBlocks);
            }
            break;
        case MenuAction::PresetNew:
            newPreset();
            break;
        case MenuAction::PresetBrowse:
            fPresetList = listPresets(presetsDir());
            fPresetDeleteArmed.clear();
            openDialog(Dialog::Presets);
            break;
        case MenuAction::PresetOpen:
            browse(BrowseTarget::OpenPreset, "Open a Xythos preset (.xythos)", presetDir());
            break;
        case MenuAction::PresetSave:
            // presets opened from elsewhere are saved into the preset folder with Save As
            if (!fChain.preset.empty() && isInsideDir(presetsDir(), pathFromUtf8(fChain.preset)))
            {
                savePreset(fChain.preset);
                break;
            }
            [[fallthrough]];
        case MenuAction::PresetSaveAs:
            openSavePresetDialog();
            break;
        case MenuAction::PresetExport: {
            fBrowseTarget = BrowseTarget::ExportPreset;
            FileBrowserOptions options;
            options.saving = true;
            options.title = "Export the preset";
            const std::string name = presetName() + Preset::kExtension;
            options.defaultName = name.c_str();
            const std::string dir = presetDir();
            if (!dir.empty())
                options.startDir = dir.c_str();
            openFileBrowser(options);
            break;
        }
        case MenuAction::PresetEmbed:
            updateSettings([](Settings& settings) { settings.embedFiles = !settings.embedFiles; });
            break;
        case MenuAction::PresetLoad:
            if (entry.arg < static_cast<int>(fMenuPresets.size()))
                openPreset(fMenuPresets[entry.arg]);
            break;
        case MenuAction::CabImage:
            openImagesDialog(fMenuTile);
            break;
        case MenuAction::SetLookahead:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
            {
                beginEdit();
                fChain.blocks[fMenuTile].config["lookahead"] = kLookaheads[entry.arg];
                sendChain();
            }
            break;
        case MenuAction::SetChoice:
            if (const ParamSpec* spec = slotSpec(fMenuParam))
            {
                beginEdit();
                setNormalized(fMenuParam, spec->toNormalized(spec->min + static_cast<float>(entry.arg)), true);
            }
            break;
        }
    }

    // Where preset browsers start: the current preset's folder, else the last one used
    std::string presetDir() const
    {
        const std::string path = !fChain.preset.empty() ? fChain.preset
                               : !fSettings.recentPresets.empty() ? fSettings.recentPresets.front()
                               : std::string();
        return path.empty() ? std::string() : pathToUtf8(pathFromUtf8(path).parent_path());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Settings, library, dialogs

    void applySettings()
    {
        theme::accent = theme::accents[std::clamp(fSettings.accent, 0, theme::accentCount - 1)];
        if (const auto custom = ink::parseHex(fSettings.accentColor))
            theme::accent = toColor(*custom);
        if (fFocusedField != kFieldInkHex && fDialog == Dialog::InkPicker)
            fFields[kFieldInkHex] = pickerHex(fInkSlot);
        // the folder fields show the home folder as ~ (and take paths typed that way)
        if (fFocusedField != kFieldNamDir)
            fFields[kFieldNamDir] = shortenHomePath(fSettings.namDir);
        if (fFocusedField != kFieldIrDir)
            fFields[kFieldIrDir] = shortenHomePath(fSettings.irDir);
    }

    // Once a second: starts the daily update check when it's due (a few seconds after opening, so it doesn't compete
    // with loading), stops it when it's turned off, and picks up the result, also when another instance checked
    void pollUpdateCheck()
    {
        const bool enabled = fSettings.checkUpdates;
        if (!enabled && fUpdateChecker.busy())
            fUpdateChecker.stop();
        const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        const bool settled = std::chrono::steady_clock::now() - fOpenedAt > std::chrono::seconds(3);
        if (enabled && settled && fUpdateCache.checkDue(now))
            fUpdateChecker.start(now);
        const int64_t time = fileTime(UpdateCache::defaultPath());
        if (fUpdateChecker.poll() || time != fUpdateCacheTime)
        {
            fUpdateCacheTime = time;
            fUpdateCache = UpdateCache::load();
            repaint();
        }
    }

    // The version the update banner offers, if any
    std::optional<std::string> availableUpdate() const
    {
        if (!fSettings.checkUpdates)
            return std::nullopt;
        return updateToShow(fUpdateCache, kAppVersion, fSettings.dismissedUpdate);
    }

    // Changes the settings file: reloads it first, so changes made meanwhile by other instances aren't lost
    template <typename Change>
    void updateSettings(Change change)
    {
        Settings settings = Settings::load();
        change(settings);
        const bool foldersChanged = settings.namDir != fSettings.namDir || settings.irDir != fSettings.irDir;
        fSettings = settings;
        if (!fSettings.save())
            d_stderr2("Xythos: can't save settings to %s", pathToUtf8(Settings::defaultPath()).c_str());
        fSettingsTime = fileTime(Settings::defaultPath());
        applySettings();
        if (foldersChanged)
            rescan();
        repaint();
    }

    void rescan()
    {
        fScanner.start(fSettings.namDir, fSettings.irDir);
        fScanning = true;
    }

    void openDialog(Dialog dialog)
    {
        fDialog = dialog;
        fListScroll = 0.0f;
        if (dialog == Dialog::AddBlock)
        {
            fPickKey.clear();
            fPreviewing = false;
            // ready to type a new search
            fFields[kFieldSearch].clear();
            focusField(kFieldSearch);
        }
        repaint();
    }

    // Starts editing a text field, and asks for the keyboard: hosts often keep it until the plugin window takes it
    void focusField(Field field)
    {
        fFocusedField = field;
        fFieldBeforeEdit = fFields[field];
        fEdit.setText(fFields[field]);
        getWindow().focus();
    }

    // Moves the keyboard focus to the next text field drawn (the previous one with `back`), wrapping around, with its
    // text selected so typing replaces it. From no field: the first (or last). False if no field is on screen.
    bool tabToField(bool back)
    {
        if (fDrawnFields.empty())
            return false;
        const int n = static_cast<int>(fDrawnFields.size());
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (fDrawnFields[static_cast<size_t>(i)] == fFocusedField)
                at = i;
        const int next = at < 0 ? (back ? n - 1 : 0) : (at + (back ? n - 1 : 1)) % n;
        blurField(true);
        focusField(fDrawnFields[static_cast<size_t>(next)]);
        fEdit.selectAll();
        repaint();
        return true;
    }

    void closeDialog()
    {
        if (fDialog == Dialog::AddBlock)
            cancelPreview();
        blurField(true);
        if (fDialog == Dialog::InkPicker)
        {
            // back to Settings, where it was opened
            fDialog = Dialog::Settings;
            repaint();
            return;
        }
        if (fDialog == Dialog::CabImages && fCabReturn != Dialog::None)
        {
            // back to Add Block, as it was
            fDialog = std::exchange(fCabReturn, Dialog::None);
            fListScroll = fCabReturnScroll;
            if (fDialog == Dialog::AddBlock)
                focusField(kFieldSearch);
            repaint();
            return;
        }
        fDialog = Dialog::None;
        repaint();
    }

    // The NAM/IR folder picker has closed: use its folder, or pick a file in the folder if the system has no picker
    void pollFolderPicker()
    {
        std::string path;
        switch (fFolderPicker.poll(path))
        {
        case FolderPicker::Status::Picked:
            fFields[fFolderField] = path;
            commitField(fFolderField);
            repaint();
            break;
        case FolderPicker::Status::Unavailable: {
            const bool nam = fFolderField == kFieldNamDir;
            browse(nam ? BrowseTarget::NamDir : BrowseTarget::IrDir,
                   nam ? "Pick any file in your NAM folder" : "Pick any file in your IR folder",
                   nam ? fSettings.namDir : fSettings.irDir);
            break;
        }
        default:
            break;
        }
    }

    void browse(BrowseTarget target, const char* title, const std::string& startDir)
    {
        fBrowseTarget = target;
        FileBrowserOptions options;
        options.title = title;
        if (!startDir.empty())
            options.startDir = startDir.c_str();
        openFileBrowser(options);
    }

    // Ends editing a text field; folder fields are saved to the settings (commit) or restored
    void blurField(bool commit)
    {
        const int field = fFocusedField;
        if (field < 0)
            return;
        fFocusedField = -1;
        if (!commit)
            fFields[field] = fFieldBeforeEdit;
        else
            commitField(static_cast<Field>(field));
        repaint();
    }

    void commitField(Field field)
    {
        if (field == kFieldNamDir || field == kFieldIrDir)
        {
            const bool nam = field == kFieldNamDir;
            const std::string text = expandHomePath(fFields[field]);
            if (text != (nam ? fSettings.namDir : fSettings.irDir))
                updateSettings([&](Settings& settings) { (nam ? settings.namDir : settings.irDir) = text; });
            fFields[field] = shortenHomePath(text);
        }
        else if (field == kFieldInkHex)
        {
            // a valid value is saved; anything else goes back to the colour's
            if (const auto color = ink::parseHex(fFields[field]))
                setPickerColor(*color, true);
            fFields[field] = pickerHex(fInkSlot);
        }
    }

    // The editor of the focused field, in step with its text (which other code may have set meanwhile)
    TextEdit& focusedEdit()
    {
        if (fFocusedField >= 0 && fEdit.text != fFields[fFocusedField])
            fEdit.setText(fFields[fFocusedField]);
        fEdit.clamp();
        return fEdit;
    }

    void pasteInto(TextEdit& edit)
    {
        size_t size = 0;
        const void* data = getClipboard(size);
        if (data == nullptr)
            return;
        std::string text(static_cast<const char*>(data), size);
        // a pasted line break or tab reads as a space
        for (char& c : text)
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        while (!text.empty() && (text.back() == ' ' || text.back() == '\0'))
            text.pop_back();
        edit.insert(text);
    }

    void copyText(const std::string& text)
    {
        if (!text.empty())
            setClipboard("text/plain", text.data(), text.size());
    }

    // The character boundary in a field nearest to x, from where its text was last drawn
    size_t fieldPosAt(int field, float x) const
    {
        const FieldLayout& layout = fFieldLayout[field];
        size_t best = 0;
        float bestDistance = 1e9f;
        for (size_t i = 0; i < layout.offsets.size(); ++i)
        {
            const float distance = std::fabs(layout.xs[i] - x);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = layout.offsets[i];
            }
        }
        return best;
    }

    // An image file (8-bit PNG, JPEG or WebP), loaded once; nullptr if it can't be loaded
    NanoImage* image(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        auto it = fImages.find(path);
        if (it == fImages.end())
        {
            it = fImages.emplace(path, std::make_unique<NanoImage>()).first;
            if (const std::optional<std::string> bytes = readFile(pathFromUtf8(path)))
                loadImage(*it->second, reinterpret_cast<const uchar*>(bytes->data()), static_cast<uint>(bytes->size()));
            // DPF's image decoder is an older stb_image: 8-bit PNG and JPEG work, 16-bit PNG doesn't
            if (!it->second->isValid())
                d_stderr2("Xythos: can't load image %s (use an 8-bit PNG, a JPEG or a WebP)", path.c_str());
        }
        return it->second->isValid() ? it->second.get() : nullptr;
    }

    // The user's own image for a block (UserImages.hpp) if it has one and it's still in the library, else its
    // type's built-in image (or its thumbnail) if it has one
    NanoImage* blockImage(const BlockState& block, bool thumb = false)
    {
        if (NanoImage* img = userImage(block))
            return img;
        return stockImage(block, thumb);
    }

    // A block's own built-in image: IR blocks show their cab image (cabImageFor()), NAM pedal captures a generic
    // pedal (pedalImageFor()), the others their type's (typeImage())
    NanoImage* stockImage(const BlockState& block, bool thumb)
    {
        if (block.typeId == kIrBlockDescriptor.typeId)
            return cabImage(cabImageFor(block), thumb);
        if (isNamPedal(block))
            return pedalImage(pedalImageFor(block), thumb);
        if (isNamAmp(block))
            return ampImage(ampImageFor(block), thumb);
        return typeImage(BlockRegistry::builtin().find(block.typeId), thumb);
    }

    // A block type's built-in image; a pedal without a photo of its own yet shows a generic pedal picked by its
    // typeId (owner, 2026-10-30), until the owner sends its render. NAM and IR blocks have none of their own.
    NanoImage* typeImage(const BlockDescriptor* type, bool thumb)
    {
        if (type == nullptr)
            return nullptr;
        if (NanoImage* img = thumb ? builtinThumb(type->typeId) : builtinImage(type->typeId))
            return img;
        if (type == &kNamBlockDescriptor || type == &kIrBlockDescriptor)
            return nullptr;
        return pedalImage(defaultPedalImage(type->typeId, builtinPedalIds()), thumb);
    }

    // A built-in generic pedal image (resources/pedals/), or its thumbnail; cached with the block types' images
    NanoImage* pedalImage(const std::string& id, bool thumb)
    {
        if (id.empty())
            return nullptr;
        const std::string key = std::string(kPedalKey) + id;
        return thumb ? builtinThumb(key) : builtinImage(key);
    }

    // The gear types shown as an amp head: amps, amps with their cab, outboard preamps, and models that don't say
    static bool isAmpGear(NamGear gear)
    {
        return gear == NamGear::Unknown || gear == NamGear::Amp || gear == NamGear::AmpCab || gear == NamGear::Outboard;
    }

    // A NAM block with a file whose model is an amp (isAmpGear()); one without a file keeps the drawn art
    bool isNamAmp(const BlockState& block) const
    {
        return block.typeId == kNamBlockDescriptor.typeId && !block.config.value("path", std::string()).empty()
            && isAmpGear(namGear(namFileInfo(block).gearType));
    }

    // A built-in amp head image (resources/amps/), or its thumbnail; cached with the block types' images
    NanoImage* ampImage(const std::string& id, bool thumb)
    {
        if (id.empty())
            return nullptr;
        const std::string key = std::string(kAmpKey) + id;
        return thumb ? builtinThumb(key) : builtinImage(key);
    }

    // The amp head a NAM amp shows: the one the user picked for it (Settings::ampImages), else one picked by its hash
    // among those suiting the style its name suggests
    std::string ampImageFor(const std::string& hash, const std::string& path) const
    {
        const std::vector<std::string>& ids = builtinAmpIds();
        const auto picked = fSettings.ampImages.find(cabKey(hash, path));
        if (picked != fSettings.ampImages.end() && std::find(ids.begin(), ids.end(), picked->second) != ids.end())
            return picked->second;
        return defaultAmpImage(cabKey(hash, path), path.empty() ? std::string() : fileStem(path), ids);
    }

    std::string ampImageFor(const BlockState& block) const
    {
        return ampImageFor(block.config.value("hash", std::string()), block.config.value("path", std::string()));
    }

    static const std::vector<std::string>& photoIds(PhotoKind kind)
    {
        return kind == PhotoKind::Pedal ? builtinPedalIds() : kind == PhotoKind::Amp ? builtinAmpIds() : builtinCabIds();
    }

    // A NAM block whose file captures a pedal (its metadata's gear type)
    bool isNamPedal(const BlockState& block) const
    {
        return block.typeId == kNamBlockDescriptor.typeId && namGear(namFileInfo(block).gearType) == NamGear::Pedal;
    }

    // The pedal image a NAM pedal shows: the one the user picked for it (Settings::pedalImages), else one picked by
    // its hash
    std::string pedalImageFor(const std::string& hash, const std::string& path) const
    {
        const std::vector<std::string>& ids = builtinPedalIds();
        const auto picked = fSettings.pedalImages.find(cabKey(hash, path));
        if (picked != fSettings.pedalImages.end() && std::find(ids.begin(), ids.end(), picked->second) != ids.end())
            return picked->second;
        return defaultPedalImage(cabKey(hash, path), ids);
    }

    std::string pedalImageFor(const BlockState& block) const
    {
        return pedalImageFor(block.config.value("hash", std::string()), block.config.value("path", std::string()));
    }

    // A built-in cab image (resources/cabs/), or its thumbnail; cached with the block types' images
    NanoImage* cabImage(const std::string& id, bool thumb)
    {
        if (id.empty())
            return nullptr;
        const std::string key = std::string(kCabKey) + id;
        return thumb ? builtinThumb(key) : builtinImage(key);
    }

    // What an IR's picked cab image (or a NAM pedal's picked pedal image) is stored under in the settings: its
    // content hash (its path if it has none)
    static std::string cabKey(const std::string& hash, const std::string& path) { return hash.empty() ? path : hash; }

    // The cab image an IR shows: the one the user picked for it (Settings::cabImages), else one picked by its hash
    std::string cabImageFor(const std::string& hash, const std::string& path) const
    {
        const std::vector<std::string>& ids = builtinCabIds();
        const auto picked = fSettings.cabImages.find(cabKey(hash, path));
        if (picked != fSettings.cabImages.end() && std::find(ids.begin(), ids.end(), picked->second) != ids.end())
            return picked->second;
        return defaultCabImage(cabKey(hash, path), path.empty() ? std::string() : fileStem(path), ids);
    }

    std::string cabImageFor(const BlockState& block) const
    {
        return cabImageFor(block.config.value("hash", std::string()), block.config.value("path", std::string()));
    }

    NanoImage* userImage(const BlockState& block)
    {
        return block.image.empty() ? nullptr : image(userImagePath(imageKindFor(block.typeId), block.image));
    }

    // What picks a block's own look in the placeholder art: its file's path
    static std::string fileLookKey(const BlockState& block) { return block.config.value("path", std::string()); }

    // An embedded image: WebP (the block images and the background) through libwebp, else what stb_image reads (PNG,
    // JPEG), into `img`; left as it was (invalid) if it doesn't decode.
    void loadImage(NanoImage& img, const unsigned char* data, unsigned int size)
    {
        if (!isWebp(data, size))
        {
            img = createImageFromMemory(data, size, IMAGE_GENERATE_MIPMAPS);
            return;
        }
        RgbaImage decoded;
        if (decodeWebp(data, size, decoded))
            img = createImageFromRGBA(decoded.width, decoded.height, decoded.pixels.data(), IMAGE_GENERATE_MIPMAPS);
    }

    // The window background behind the panels: the owner's image (BackgroundImage.cpp), scaled to cover the window and
    // centred; the panels over it are translucent black
    void drawBackground()
    {
        fillRect(0, 0, fWidth, fHeight, theme::background);
        if (!fBackgroundTried)
        {
            fBackgroundTried = true;
            loadImage(fBackground, kBackgroundImage, kBackgroundImageSize);
        }
        const Size<uint> size = fBackground.getSize();
        if (!fBackground.isValid() || size.getWidth() == 0 || size.getHeight() == 0)
            return;
        const float scale = std::max(fWidth / size.getWidth(), fHeight / size.getHeight());
        const float w = size.getWidth() * scale, h = size.getHeight() * scale;
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        beginPath();
        rect(0, 0, fWidth, fHeight);
        fillPaint(imagePattern(x, y, w, h, 0.0f, fBackground, 1.0f));
        fill();
    }

    // The built-in images are cached by typeId, the cab images by kCabKey and their id, the generic pedal images by
    // kPedalKey and theirs
    static constexpr std::string_view kCabKey = "cab:";
    static constexpr std::string_view kPedalKey = "pedal:";
    static constexpr std::string_view kAmpKey = "amp:";

    static BuiltinImage builtinData(const std::string& key, bool thumb)
    {
        if (key.starts_with(kCabKey))
        {
            const char* const id = key.c_str() + kCabKey.size();
            return thumb ? builtinCabThumb(id) : builtinCabImage(id);
        }
        if (key.starts_with(kPedalKey))
        {
            const char* const id = key.c_str() + kPedalKey.size();
            return thumb ? builtinPedalThumb(id) : builtinPedalImage(id);
        }
        if (key.starts_with(kAmpKey))
        {
            const char* const id = key.c_str() + kAmpKey.size();
            return thumb ? builtinAmpThumb(id) : builtinAmpImage(id);
        }
        return thumb ? builtinBlockThumb(key.c_str()) : builtinBlockImage(key.c_str());
    }

    // A block type's built-in image (resources/blocks/), if it has one, or a cab image (kCabKey). Decoded when first
    // drawn; the least recently drawn ones beyond kMaxFullImages are freed (never one drawn this frame: NanoVG renders
    // at the frame's end).
    NanoImage* builtinImage(const std::string& typeId)
    {
        auto it = fBuiltinImages.find(typeId);
        if (it == fBuiltinImages.end())
        {
            const BuiltinImage builtin = builtinData(typeId, false);
            it = fBuiltinImages.emplace(typeId, CachedImage { std::make_unique<NanoImage>(), 0 }).first;
            if (builtin.data != nullptr)
                loadImage(*it->second.image, builtin.data, builtin.size);
            if (!it->second.image->isValid())
                return nullptr; // stays in the map, empty, so it isn't looked up again
            evictBuiltinImages(typeId);
        }
        it->second.lastFrame = fFrame;
        return it->second.image->isValid() ? it->second.image.get() : nullptr;
    }

    void evictBuiltinImages(const std::string& keep)
    {
        for (;;)
        {
            size_t loaded = 0;
            auto oldest = fBuiltinImages.end();
            for (auto it = fBuiltinImages.begin(); it != fBuiltinImages.end(); ++it)
            {
                if (!it->second.image->isValid())
                    continue;
                ++loaded;
                if (it->first != keep && it->second.lastFrame < fFrame
                    && (oldest == fBuiltinImages.end() || it->second.lastFrame < oldest->second.lastFrame))
                    oldest = it;
            }
            if (loaded <= kMaxFullImages || oldest == fBuiltinImages.end())
                return;
            fBuiltinImages.erase(oldest);
        }
    }

    // A block type's built-in thumbnail (resources/blocks/thumbs/), for the tiles and lists, if it has one
    NanoImage* builtinThumb(const std::string& typeId)
    {
        auto it = fBuiltinThumbs.find(typeId);
        if (it == fBuiltinThumbs.end())
        {
            it = fBuiltinThumbs.emplace(typeId, std::make_unique<NanoImage>()).first;
            const BuiltinImage builtin = builtinData(typeId, true);
            if (builtin.data != nullptr)
                loadImage(*it->second, builtin.data, builtin.size);
        }
        return it->second->isValid() ? it->second.get() : nullptr;
    }

    // Whether an image is a block type's built-in one (full or thumbnail), so its face's knobs and LEDs fit it
    // The face drawn over an image: its type's, on the type's own built-in image; a generic pedal photo's (its
    // footswitch and LED, no knobs) on one of those; none on a user's image
    const PedalFace* faceOf(const NanoImage& img, const BlockDescriptor* type) const
    {
        if (type != nullptr && isBuiltinImage(img, type->typeId))
            return pedalFace(type->typeId);
        for (const auto& [key, cached] : fBuiltinImages)
        {
            if (cached.image.get() == &img && key.starts_with(kPedalKey))
                return genericPedalFace(key.c_str() + kPedalKey.size());
            if (cached.image.get() == &img && key.starts_with(kAmpKey))
                return ampHeadFace(key.c_str() + kAmpKey.size());
        }
        for (const auto& [key, thumb] : fBuiltinThumbs)
        {
            if (thumb.get() == &img && key.starts_with(kPedalKey))
                return genericPedalFace(key.c_str() + kPedalKey.size());
            if (thumb.get() == &img && key.starts_with(kAmpKey))
                return ampHeadFace(key.c_str() + kAmpKey.size());
        }
        return nullptr;
    }

    bool isBuiltinImage(const NanoImage& img, const char* typeId) const
    {
        const auto full = fBuiltinImages.find(typeId);
        if (full != fBuiltinImages.end() && full->second.image.get() == &img)
            return true;
        const auto thumb = fBuiltinThumbs.find(typeId);
        return thumb != fBuiltinThumbs.end() && thumb->second.get() == &img;
    }

    // Draws an image scaled to fit the rect, centred, keeping its aspect ratio; returns where it went
    Rect drawImageFit(NanoImage& img, float x, float y, float w, float h, float radius)
    {
        const Size<uint> size = img.getSize();
        if (size.getWidth() == 0 || size.getHeight() == 0)
            return {};
        const float scale = std::min(w / size.getWidth(), h / size.getHeight());
        const float dw = size.getWidth() * scale, dh = size.getHeight() * scale;
        const float dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
        beginPath();
        roundedRect(dx, dy, dw, dh, radius);
        fillPaint(imagePattern(dx, dy, dw, dh, 0.0f, img, 1.0f));
        fill();
        return { dx, dy, dw, dh };
    }

    // Draws a block's image, and if it's its type's built-in one, the knobs on its face (PedalFaces.cpp), turned to
    // the slot's values (slot < 0: the defaults). Interactive ones (the block panel's, `block` its position in the
    // chain) turn the same host parameters as the panel's knobs, so either turns both, and their footswitch turns the
    // block on or off. Its LEDs show `led` (Led::None: as the image has them).
    void drawBlockImage(NanoImage& img, const BlockDescriptor* type, int slot, float x, float y, float w, float h,
                        float radius, bool interactive, Led led = Led::None, int block = -1)
    {
        const Rect r = drawImageFit(img, x, y, w, h, radius);
        const PedalFace* face = faceOf(img, type);
        if (face == nullptr || r.w <= 0.0f)
            return;
        if (interactive && block >= 0 && face->footswitch.radius > 0.0f)
        {
            const float sr = face->footswitch.radius * r.w;
            const float sx = r.x + face->footswitch.x * r.w, sy = r.y + face->footswitch.y * r.h;
            fHits.push_back({ { sx - sr, sy - sr, 2 * sr, 2 * sr }, Hit::FaceSwitch, block });
        }
        if (led != Led::None)
        {
            for (const FaceLed& faceLed : face->leds)
            {
                const float cx = r.x + faceLed.x * r.w, cy = r.y + faceLed.y * r.h, lr = faceLed.radius * r.w;
                drawFaceLed(cx, cy, lr, Color(faceLed.r, faceLed.g, faceLed.b), led == Led::On);
            }
        }
        for (const FaceKnob& knob : face->knobs)
        {
            int param = -1;
            for (int p = 0; p < type->numParams; ++p)
                if (std::strcmp(type->params[p].name, knob.param) == 0)
                    param = p;
            if (param < 0)
                continue;
            const ParamSpec& spec = type->params[param];
            const uint32_t index = slot >= 0 ? slotParam(slot, param) : 0;
            const float n = slot >= 0 ? normalized(index) : spec.toNormalized(spec.def);
            const float cx = r.x + knob.x * r.w, cy = r.y + knob.y * r.h, kr = knob.radius * r.w;
            drawFaceKnob(cx, cy, kr, n, face->rim);
            if (!interactive || slot < 0)
                continue;
            if (fLearnParam == static_cast<int>(index))
            {
                beginPath();
                circle(cx, cy, kr + 4.0f);
                strokeColor(theme::accent);
                strokeWidth(1.5f);
                stroke();
            }
            // the knob's name engraved under it, its value while it's turned
            if (kr >= 12.0f)
            {
                const bool turning = fDragParam == static_cast<int>(index);
                label(cx, cy + kr + 10.0f, turning ? valueText(index).c_str() : faceKnobLabel(spec.name),
                      theme::faceLabel, 9.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            }
            fHits.push_back({ { cx - kr, cy - kr, 2 * kr, 2 * kr }, Hit::Knob, static_cast<int>(index) });
        }
    }

    // An LED: a lit lens with its glow, or a dark one, both with a little bezel and a highlight
    void drawLed(float cx, float cy, float r, const Color& color, bool lit)
    {
        if (lit)
        {
            beginPath();
            circle(cx, cy, r * 3.2f);
            fillPaint(radialGradient(cx, cy, r * 0.6f, r * 3.2f, color.withAlpha(0.5f), color.withAlpha(0.0f)));
            fill();
        }
        beginPath();
        circle(cx, cy, r * 1.15f);
        fillColor(Color(0x05, 0x05, 0x05));
        fill();
        Color inner = color, outer = color;
        if (lit)
        {
            inner.interpolate(Color(1.0f, 1.0f, 1.0f), 0.55f);
        }
        else
        {
            inner.interpolate(Color(0, 0, 0), 0.72f);
            outer.interpolate(Color(0, 0, 0), 0.86f);
        }
        beginPath();
        circle(cx, cy, r);
        fillPaint(radialGradient(cx - r * 0.15f, cy - r * 0.15f, r * 0.1f, r, inner, outer));
        fill();
        beginPath();
        circle(cx - r * 0.35f, cy - r * 0.35f, r * 0.28f);
        fillColor(Color(1.0f, 1.0f, 1.0f, lit ? 0.45f : 0.18f));
        fill();
    }

    // What a photo's colour part (LedImages.cpp, the rim knob) is tinted with to turn it `color`: the colour's hue at its
    // saturation, white where it's grey
    static Color photoTint(const Color& color)
    {
        const float hi = std::max({ color.red, color.green, color.blue });
        const float lo = std::min({ color.red, color.green, color.blue });
        if (hi - lo <= 1e-4f)
            return Color(1.0f, 1.0f, 1.0f);
        const float sat = (hi - lo) / hi;
        return Color((color.red - lo) / (hi - lo) * sat + 1.0f - sat, (color.green - lo) / (hi - lo) * sat + 1.0f - sat,
                     (color.blue - lo) / (hi - lo) * sat + 1.0f - sat);
    }

    // An LED on a pedal's face, over the image's own (radius r): the photographed LED (LedImages.cpp) in its chrome
    // bezel, and while lit its lens, bloom and glass added over it. The photos are hot pink; their colour parts are
    // tinted with `color`, so any colour works. Falls back to drawLed() if the images don't load.
    void drawFaceLed(float cx, float cy, float r, const Color& color, bool lit)
    {
        if (!fLedImagesTried)
        {
            fLedImagesTried = true;
            const BuiltinImage images[kLedLayers] = { ledOffImage(),  ledOffColourImage(),   ledLensImage(),
                                                      ledLensColourImage(), ledBloomImage(), ledBloomColourImage(),
                                                      ledGlassImage() };
            for (int i = 0; i < kLedLayers; ++i)
                fLedImages[i] = createImageFromMemory(images[i].data, images[i].size, IMAGE_GENERATE_MIPMAPS);
        }
        for (const NanoImage& img : fLedImages)
            if (!img.isValid())
                return drawLed(cx, cy, r, color, lit);

        // the bezel is larger than the image's LED, so the lens (kLensFraction of it) covers that
        constexpr float kBezelScale = 1.45f, kLensFraction = 0.70f;
        constexpr float kLensWhite = 0.25f, kLensColour = 1.2f, kBloom = 1.0f, kGlass = 0.3f;
        const float br = r * kBezelScale, lensR = br * kLensFraction;

        const Color tint = photoTint(color);

        const auto layer = [&](int index, float half, const Color& paint, bool add) {
            Paint p = imagePattern(cx - half, cy - half, half * 2.0f, half * 2.0f, 0.0f, fLedImages[index], 1.0f);
            p.innerColor = p.outerColor = paint;
            if (add)
                nvgGlobalCompositeBlendFuncSeparate(getContext(), NVG_ONE, NVG_ONE, NVG_ZERO, NVG_ONE);
            beginPath();
            rect(cx - half, cy - half, half * 2.0f, half * 2.0f);
            fillPaint(p);
            fill();
            if (add)
                nvgGlobalCompositeOperation(getContext(), NVG_SOURCE_OVER);
        };
        const auto scaled = [](const Color& c, float k) { return Color(c.red * k, c.green * k, c.blue * k, 1.0f); };

        // hide the image's own (lit) LED at full opacity, even when the image is dimmed, on a soft shadow
        save();
        globalAlpha(1.0f);
        beginPath();
        circle(cx, cy + br * 0.08f, br * 1.3f);
        fillPaint(radialGradient(cx, cy + br * 0.08f, br * 0.9f, br * 1.3f, Color(0.0f, 0.0f, 0.0f, 0.6f),
                                 Color(0.0f, 0.0f, 0.0f, 0.0f)));
        fill();
        beginPath();
        circle(cx, cy, br * 0.98f);
        fillColor(Color(0x05, 0x05, 0x05));
        fill();
        restore();

        save();
        layer(0, br, Color(1.0f, 1.0f, 1.0f), false);
        layer(1, br, tint, true);
        if (lit)
        {
            layer(2, lensR / 0.9f, scaled(Color(1.0f, 1.0f, 1.0f), kLensWhite), true);
            layer(3, lensR / 0.9f, scaled(tint, kLensColour), true);
            const float bloomHalf = lensR / 0.25f * 0.8f;
            layer(4, bloomHalf, scaled(Color(1.0f, 1.0f, 1.0f), kBloom), true);
            layer(5, bloomHalf, scaled(tint, kBloom), true);
            layer(6, lensR, scaled(Color(1.0f, 1.0f, 1.0f), kGlass), true);
        }
        restore();
    }

    // A knob on a pedal's face, on a soft shadow: the knob's body unrotated, so its reflections stay put like a real
    // knob's, its pointer turned to the value (pointing up at the middle of the range, like the panel's knobs) and the
    // sheen over both. With a lit `rim`, the knob with the glowing rim, the rim tinted that colour (like the face
    // LEDs: the colour part added tinted); else black.
    void drawFaceKnob(float cx, float cy, float r, float n, FaceRim rim = {})
    {
        if (!fKnobImagesTried)
        {
            fKnobImagesTried = true;
            const auto load = [this](NanoImage& img, BuiltinImage data) {
                img = createImageFromMemory(data.data, data.size, IMAGE_GENERATE_MIPMAPS);
            };
            load(fKnobImage, knobImage());
            load(fKnobPointer, knobPointerImage());
            load(fKnobSheen, knobSheenImage());
            load(fRimKnob, rimKnobImage());
            load(fRimKnobPointer, rimKnobPointerImage());
            load(fRimKnobColour, rimKnobColourImage());
        }
        const bool rimmed = rim.lit() && fRimKnob.isValid() && fRimKnobPointer.isValid() && fRimKnobColour.isValid();
        constexpr float kSheenStrength = 0.06f; // dim, to suit the dark room (owner, 2026-10-29; was 0.2)
        const float drop = r * 0.08f;
        beginPath();
        circle(cx, cy + drop, r * 1.25f);
        fillPaint(radialGradient(cx, cy + drop, r * 0.9f, r * 1.25f, Color(0.0f, 0.0f, 0.0f, 0.6f),
                                 Color(0.0f, 0.0f, 0.0f, 0.0f)));
        fill();

        const float angle = (std::clamp(n, 0.0f, 1.0f) - 0.5f) * 1.5f * static_cast<float>(M_PI);
        const auto image = [this](NanoImage& img, float x, float y, float half) {
            beginPath();
            rect(x - half, y - half, 2 * half, 2 * half);
            fillPaint(imagePattern(x - half, y - half, 2 * half, 2 * half, 0.0f, img, 1.0f));
            fill();
        };
        const auto pointer = [&](NanoImage& img, float half) {
            save();
            translate(cx, cy);
            rotate(angle);
            image(img, 0.0f, 0.0f, half);
            restore();
        };
        if (rimmed)
        {
            const float half = r * kRimKnobGlow;
            image(fRimKnob, cx, cy, half);
            Paint p = imagePattern(cx - half, cy - half, 2 * half, 2 * half, 0.0f, fRimKnobColour, 1.0f);
            p.innerColor = p.outerColor = photoTint(Color(rim.r, rim.g, rim.b));
            nvgGlobalCompositeBlendFuncSeparate(getContext(), NVG_ONE, NVG_ONE, NVG_ZERO, NVG_ONE);
            beginPath();
            rect(cx - half, cy - half, 2 * half, 2 * half);
            fillPaint(p);
            fill();
            nvgGlobalCompositeOperation(getContext(), NVG_SOURCE_OVER);
            pointer(fRimKnobPointer, half);
            return;
        }
        if (fKnobImage.isValid() && fKnobPointer.isValid())
        {
            image(fKnobImage, cx, cy, r);
            pointer(fKnobPointer, r);
        }
        else
        {
            beginPath();
            circle(cx, cy, r);
            fillColor(theme::background);
            fill();
            save();
            translate(cx, cy);
            rotate(angle);
            beginPath();
            moveTo(0.0f, -r * 0.8f);
            lineTo(0.0f, -r * 0.45f);
            strokeColor(theme::text);
            strokeWidth(std::max(1.0f, r * 0.08f));
            stroke();
            restore();
        }

        if (fKnobSheen.isValid())
        {
            beginPath();
            circle(cx, cy, r);
            fillPaint(imagePattern(cx - r, cy - r, 2 * r, 2 * r, 0.0f, fKnobSheen, kSheenStrength));
            fill();
        }
    }

    // Whether a block's file exists (for a file inside a zip: whether the zip does)
    static bool fileExists(const std::string& path)
    {
        std::error_code ec;
        const std::optional<ArchivePath> archive = splitArchivePath(path);
        return std::filesystem::exists(pathFromUtf8(archive ? archive->archive : path), ec);
    }

    // Adds a block where Add Block was opened for (the end of a section, a gap, or in place of a block being
    // replaced), starting from its defaults (parameter values live in the host)
    void addBlock(const BlockDescriptor* type, nlohmann::json config, bool undoable = true)
    {
        Section section = fAddSection;
        int position = std::exchange(fAddPosition, kMaxBlocks);
        const int replace = indexOfSlot(std::exchange(fReplaceSlot, -1));
        if (replace < 0 && fChain.freeSlot() < 0)
            return;
        if (undoable)
            beginEdit();
        if (replace >= 0)
        {
            section = fChain.sectionOf(replace);
            const std::vector<int> indices = fChain.sectionIndices(section);
            position = static_cast<int>(std::find(indices.begin(), indices.end(), replace) - indices.begin());
            takeOutBlock(replace);
        }
        const int slot = fChain.freeSlot();

        BlockState block;
        block.typeId = type->typeId;
        block.slot = slot;
        block.config = std::move(config);

        setHostParameter(slotBypassParam(slot), 0.0f);
        for (int p = 0; p < type->numParams; ++p)
        {
            const ParamSpec& spec = type->params[p];
            block.params[p] = spec.toNormalized(spec.def);
        }
        // defaults from Settings (the user's own for the type, Set as Default, come after these)
        if (type == &kNamBlockDescriptor)
        {
            block.config["a2Size"] = fSettings.a2Lite ? 0.0 : 1.0;
            block.config[kNamToneKey] = true;
            block.params[kNamParamNormalize] = fSettings.normalize ? 1.0f : 0.0f;
        }
        else if (type == &kLooperBlockDescriptor)
        {
            block.config[kLooperCountInKey] = true;
            block.config[kLooperLatencyKey] = true;
        }
        else if (type == &kIrBlockDescriptor)
        {
            block.params[kIrParamNormalize] = fSettings.normalize ? 1.0f : 0.0f;
        }
        if (hasBlockDefaults(type->typeId))
            for (int p = 0; p < type->numParams; ++p)
                block.params[p] = blockDefault(*type, p);
        for (int p = 0; p < type->numParams; ++p)
            setHostParameter(slotParam(slot, p), block.params[p]);

        fChain.insert(section, position, std::move(block));
        fSelected = indexOfSlot(slot);
        fMergeSelected = false;
        sendChain();
    }

    // ---- Add Block's live preview
    // The first pick takes a snapshot of the chain (and where the block goes); each pick puts the snapshot back and
    // adds the picked block, so what's heard follows the selection. Keeping it records one undo step from the
    // snapshot; cancelling puts the snapshot back with none.

    // What identifies a row or item across frames (fRows is rebuilt as it's drawn): its file, folder or type
    static std::string pickKey(const AddRow& row)
    {
        if (!row.folder.empty())
            return "dir:" + row.folder;
        if (row.entry != nullptr)
            return "file:" + row.entry->path;
        return row.type != nullptr ? std::string("type:") + row.type->typeId : std::string();
    }

    static std::string pickKey(const AddItem& item) { return std::string("type:") + item.type->typeId; }

    void pickAddItem(const AddItem& item)
    {
        if (fPreviewing && fPickKey == pickKey(item))
            return; // already in the chain (the first click of a double-click)
        fPickKey = pickKey(item);
        fPickLabel = item.label;
        previewBlock(item.type, nlohmann::json::object());
    }

    // A row: a file or block type is previewed; a folder is only selected (Enter opens it)
    void pickAddRow(const AddRow& row)
    {
        if (fPreviewing && fPickKey == pickKey(row))
            return;
        fPickKey = pickKey(row);
        if (!row.folder.empty() || row.type == nullptr)
        {
            repaint();
            return;
        }
        fPickLabel = row.entry != nullptr ? row.entry->name : std::string(row.type->displayName);
        nlohmann::json config = nlohmann::json::object();
        if (row.entry)
            config = { { "path", row.entry->path }, { "hash", row.entry->hash } };
        previewBlock(row.type, std::move(config));
    }

    void previewBlock(const BlockDescriptor* type, nlohmann::json config)
    {
        if (!fPreviewing)
        {
            fPreviewBase = fChain;
            fPreviewBaseSelected = fSelected;
            fPreviewSection = fAddSection;
            fPreviewPosition = fAddPosition;
            fPreviewReplaceSlot = fReplaceSlot;
            fPreviewing = true;
        }
        else
        {
            restorePreviewBase();
        }
        fAddSection = fPreviewSection;
        fAddPosition = fPreviewPosition;
        fReplaceSlot = fPreviewReplaceSlot;
        addBlock(type, std::move(config), false);
    }

    // The chain as it was before the preview, its values back in the host (a replaced block's slot may have been
    // reused); not sent
    void restorePreviewBase()
    {
        fChain = fPreviewBase;
        for (const BlockState& block : fChain.blocks)
        {
            setHostParameter(slotBypassParam(block.slot), block.bypass ? 1.0f : 0.0f);
            for (int p = 0; p < kParamsPerSlot; ++p)
                setHostParameter(slotParam(block.slot, p), block.params[p]);
        }
        fSelected = fPreviewBaseSelected;
    }

    // OK, Enter or a double-click: keeps the previewed block (one undo step) and closes Add Block. On a folder row,
    // Enter opens the folder instead.
    void commitPreview()
    {
        if (!fPreviewing && fPickKey.starts_with("dir:"))
        {
            openLibraryDir(fPickKey.substr(4));
            return;
        }
        if (!fPreviewing)
            return;
        fPreviewing = false;
        fUndo.push(fPreviewBase);
        fPickKey.clear();
        closeDialog();
    }

    // Cancel, Esc, × or a click outside: the chain goes back to how it was
    void cancelPreview()
    {
        if (!fPreviewing)
            return;
        fPreviewing = false;
        fPickKey.clear();
        fAddSection = fPreviewSection;
        fAddPosition = kMaxBlocks;
        fReplaceSlot = -1;
        restorePreviewBase();
        sendChain();
    }

    // The arrow keys: the next row (dy) or, in the two-column grid of block types, the next item across (dx); each
    // one is previewed as it's reached
    void movePick(int dx, int dy)
    {
        const bool rows = !fFields[kFieldSearch].empty() || isLibraryCategory(fDialogCategory);
        if (rows)
        {
            if (dy == 0)
                return;
            const int count = static_cast<int>(fRows.size());
            int at = -1;
            for (int i = 0; i < count; ++i)
                if (!fPickKey.empty() && pickKey(fRows[static_cast<size_t>(i)]) == fPickKey)
                    at = i;
            int next = at < 0 ? (dy > 0 ? -1 : count) : at;
            do
                next += dy;
            while (next >= 0 && next < count && fRows[static_cast<size_t>(next)].heading != nullptr);
            if (next < 0 || next >= count)
                return;
            fScrollToPick = true;
            pickAddRow(fRows[static_cast<size_t>(next)]);
            return;
        }
        const std::vector<AddItem> items = addItems(fDialogCategory);
        const int count = static_cast<int>(items.size());
        if (count == 0)
            return;
        int at = -1;
        for (int i = 0; i < count; ++i)
            if (pickKey(items[static_cast<size_t>(i)]) == fPickKey)
                at = i;
        const int columns = std::max(1, fItemColumns);
        int next = at < 0 ? 0 : at + dx + dy * columns;
        if (at >= 0 && (next < 0 || next >= count))
            return;
        next = std::clamp(next, 0, count - 1);
        fScrollToPick = true;
        pickAddItem(items[static_cast<size_t>(next)]);
    }

    // Inserts a copy of the copied block at a position in a section (past its end: at the end), with its values
    void pasteBlock(Section section, int position)
    {
        if (!gBlockClipboard || BlockRegistry::builtin().find(gBlockClipboard->typeId) == nullptr)
            return;
        if (fChain.freeSlot() < 0)
        {
            showToast("The chain is full (32 blocks)");
            return;
        }
        beginEdit();
        BlockState block = *gBlockClipboard;
        block.slot = fChain.freeSlot();
        setHostParameter(slotBypassParam(block.slot), block.bypass ? 1.0f : 0.0f);
        for (int p = 0; p < kParamsPerSlot; ++p)
            setHostParameter(slotParam(block.slot, p), block.params[p]);
        const int slot = block.slot;
        fChain.insert(section, position, std::move(block));
        fSelected = indexOfSlot(slot);
        fMergeSelected = false;
        sendChain();
    }

    // The chain with the footswitches latched now: only the DSP knows them (Block::latched())
    ChainState withLiveLatches(ChainState chain) const
    {
        for (BlockState& block : chain.blocks)
            block.latched = latchedBits(block.slot);
        return chain;
    }

    uint32_t latchedBits(int slot) const
    {
        return slot >= 0 && slot < kMaxBlocks ? static_cast<uint32_t>(fParams[slotLatchParam(slot)]) : 0u;
    }

    // restoreLatches: the chain is a preset being loaded, so its latched footswitches apply to the blocks the DSP
    // keeps too; otherwise the blocks keep theirs, and new ones get the latched state seen last
    void sendChain(bool restoreLatches = false)
    {
        ChainState chain = restoreLatches ? fChain : withLiveLatches(fChain);
        chain.restoreLatches = restoreLatches;
        const std::string json = chain.toJson();
#ifndef XYTHOS_TEST_NO_STATE_MESSAGE // test builds only: behave like a host that drops state messages
        setState("chain", json.c_str());
#endif

        // Second route for hosts that drop the state message (see state/ChainExchange.hpp)
        if (fInstanceId != 0 && writeChainExchange(fInstanceId, json))
        {
            const uint32_t revision = (static_cast<uint32_t>(fParams[kParamChainRevision]) % kMaxExchangeValue) + 1;
            setHostParameter(kParamChainRevision, static_cast<float>(revision));
        }

        chainChanged();
        repaint();
    }

    void loadExchangeChain()
    {
        const std::optional<std::string> json = readChainExchange(fInstanceId);
        if (!json)
            return;
        if (std::optional<ChainState> chain = ChainState::fromJson(*json))
        {
            fChain = std::move(*chain);
            fMigratedSlots = migrateBlocks(fChain);
            fSavedChain = fChain;
            fUndo.clear();
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void chainChanged()
    {
        fChainChangedAt = std::chrono::steady_clock::now();
        fStatusPending = true;
    }

    bool statusSettled() const { return std::chrono::steady_clock::now() - fChainChangedAt > kStatusGrace; }

    // Whether the audio engine reports a block running in this slot
    bool slotRunning(int slot) const
    {
        const uint32_t low = static_cast<uint32_t>(fParams[kParamRunningSlotsLow]);
        const uint32_t high = static_cast<uint32_t>(fParams[kParamRunningSlotsHigh]);
        return ((low | (high << 16)) >> slot) & 1u;
    }

    void releaseMomentary()
    {
        setNormalized(static_cast<uint32_t>(fMomentaryParam), 0.0f, true);
        fMomentaryParam = -1;
        fMomentaryReleasePending = false;
    }

    bool onRelease(float x, float y)
    {
        if (fSelectingText)
        {
            fSelectingText = false;
            return true;
        }
        if (fScrollDragging || fListScrollDragging)
        {
            fScrollDragging = fListScrollDragging = false;
            repaint();
            return true;
        }
        if (fMomentaryParam >= 0)
        {
            // a quick click is held a moment longer (uiIdle releases it), so the DSP sees the press even if the
            // release would land in the same audio buffer
            if (std::chrono::steady_clock::now() - fMomentaryPressedAt < kMomentaryMinHold)
            {
                fMomentaryReleasePending = true;
                return true;
            }
            releaseMomentary();
            return true;
        }
        if (fInkDrag >= 0)
        {
            fInkDrag = -1;
            setPickerColor(ink::fromHsv(fPickHsv), true);
            return true;
        }
        if (fDragParam >= 0)
        {
            editParameter(static_cast<uint32_t>(fDragParam), false);
            fDragParam = -1;
            fSliderRect = Rect {};
            return true;
        }
        if (fPressTile >= 0)
        {
            if (fDraggingTile)
            {
                if (const DropPoint* drop = dropPointAt(x, y))
                {
                    const int slot = fChain.blocks[fPressTile].slot;
                    const ChainState before = fChain;
                    fChain.move(fPressTile, drop->section, drop->position);
                    fSelected = indexOfSlot(slot);
                    if (!(fChain == before))
                    {
                        fUndo.push(before);
                        sendChain();
                    }
                }
            }
            fPressTile = -1;
            fDraggingTile = false;
            repaint();
            return true;
        }
        return false;
    }

    int indexOfSlot(int slot) const
    {
        for (int i = 0; i < static_cast<int>(fChain.blocks.size()); ++i)
        {
            if (fChain.blocks[i].slot == slot)
                return i;
        }
        return -1;
    }

    // The drop point nearest to a tile dragged with the mouse at (x, y)
    const DropPoint* dropPointAt(float x, float y) const
    {
        const int row = fChain.split && y >= fLayout.rowY[1] - kRowGap / 2 ? 1 : 0;
        const float left = x - fLayout.tile / 2;
        const DropPoint* best = nullptr;
        for (const DropPoint& drop : fLayout.drops)
        {
            if (drop.row == row && (!best || std::fabs(drop.x - left) < std::fabs(best->x - left)))
                best = &drop;
        }
        return best;
    }

    const HitArea* hitAt(float x, float y) const
    {
        // last drawn is on top
        for (auto it = fHits.rbegin(); it != fHits.rend(); ++it)
        {
            if (it->rect.contains(x, y))
                return &*it;
        }
        return nullptr;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Drawing helpers

    // ---------------------------------------------------------------------------------------------------------------
    // Ink: with a gradient on, the highlight colour (outlines, fills, knobs) is drawn with the window's paint: one
    // texture over the window, so the colours sit in the same place wherever a shape is drawn. Text keeps its plain
    // colour (see text()).

    // The colour calls below shadow NanoVG's: an ink colour gets the window's paint instead
    using NanoVG::fillColor;
    using NanoVG::strokeColor;
    using NanoVG::text;
    using NanoVG::textBox;
    void fillColor(const Color& color)
    {
        fFillColor = color;
        fFillIsInk = isInk(color);
        if (fFillIsInk)
            fillPaint(inkPaint(color.alpha));
        else
            NanoVG::fillColor(color);
    }

    // NanoVG draws glyphs with its font atlas and the paint's colour alone, and a window paint's colour is white, so
    // text can't follow the window's paint: ink text is drawn in its plain colour, the rest of the window in the paint
    float text(float x, float y, const char* string, const char* end)
    {
        return withPlainText([&] { return NanoVG::text(x, y, string, end); });
    }
    void textBox(float x, float y, float breakRowWidth, const char* string, const char* end = nullptr)
    {
        withPlainText([&] {
            NanoVG::textBox(x, y, breakRowWidth, string, end);
            return 0.0f;
        });
    }
    template <typename Draw>
    float withPlainText(Draw draw)
    {
        if (!fFillIsInk)
            return draw();
        NanoVG::fillColor(fFillColor);
        const float result = draw();
        fillPaint(inkPaint(fFillColor.alpha));
        return result;
    }
    void strokeColor(const Color& color)
    {
        if (isInk(color))
            strokePaint(inkPaint(color.alpha));
        else
            NanoVG::strokeColor(color);
    }

    // The highlight and text colours (matched by their RGB, so a faded one keeps its alpha), while a gradient is on;
    // text itself is drawn plain (see text())
    bool isInk(const Color& color) const
    {
        if (!fInkImage)
            return false;
        auto same = [&color](const Color& other) {
            return color.red == other.red && color.green == other.green && color.blue == other.blue;
        };
        return same(theme::accent) || same(theme::text);
    }

    // The window's paint at `alpha`. A paint is taken in the coordinates the current transform gives, so the
    // window's logical rectangle is mapped back through it: the colours stay put wherever the shape is drawn.
    Paint inkPaint(float alpha)
    {
        float current[6], inverse[6];
        currentTransform(current);
        nvgTransformInverse(inverse, current);
        auto toCall = [&inverse, this](float wx, float wy, float& cx, float& cy) {
            float px = 0.0f, py = 0.0f;
            nvgTransformPoint(&px, &py, fFrameXform, wx, wy);
            nvgTransformPoint(&cx, &cy, inverse, px, py);
        };
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        toCall(0.0f, 0.0f, x0, y0);
        toCall(fWidth, fHeight, x1, y1);
        return imagePattern(x0, y0, x1 - x0, y1 - y0, 0.0f, *fInkImage, alpha);
    }

    // Makes the texture when the mode or the colours change; on the frame, so the GL context is current
    void updateInkImage()
    {
        std::vector<std::string> key = fSettings.inkColors;
        key.insert(key.begin(), std::to_string(fSettings.inkMode));
        if (key == fInkKey)
            return;
        fInkKey = std::move(key);
        fInkImage.reset();
        fInkPixels.clear();
        if (fSettings.inkMode < 1 || fSettings.inkMode > 2)
            return;
        std::vector<ink::Rgb> colors;
        for (const std::string& hex : fSettings.inkColors)
            if (const auto color = ink::parseHex(hex))
                colors.push_back(ink::lighten(*color));
        if (colors.empty())
            return;
        fInkPixels = ink::makeTexture(static_cast<ink::Mode>(fSettings.inkMode), colors);
        fInkImage = std::make_unique<NanoImage>(
            createImageFromRGBA(ink::kTextureWidth, ink::kTextureHeight, fInkPixels.data(), 0));
    }

    // The highlight colour at a point of the window: the window paint's colour there, or the accent
    Color highlightAt(float x, float y) const
    {
        if (fInkPixels.empty() || fWidth <= 0.0f || fHeight <= 0.0f)
            return theme::accent;
        const int tx = std::clamp(static_cast<int>(x / fWidth * ink::kTextureWidth), 0, ink::kTextureWidth - 1);
        const int ty = std::clamp(static_cast<int>(y / fHeight * ink::kTextureHeight), 0, ink::kTextureHeight - 1);
        const uint8_t* p = &fInkPixels[(static_cast<size_t>(ty) * ink::kTextureWidth + tx) * 4];
        return Color(p[0], p[1], p[2]);
    }

    static std::vector<std::string> hexList(const std::vector<ink::Rgb>& colors)
    {
        std::vector<std::string> hex;
        for (const ink::Rgb& color : colors)
            hex.push_back(ink::toHex(color));
        return hex;
    }

    void fillRect(float x, float y, float w, float h, const Color& color, float radius = 0.0f)
    {
        beginPath();
        if (radius > 0.0f)
            roundedRect(x, y, w, h, radius);
        else
            rect(x, y, w, h);
        fillColor(color);
        fill();
    }

    // A box: translucent black inside (the background image shows through) with a 1 px grey outline that brightens on hover. Bars are black with one dividing line on
    // their inner edge.
    void surface(float x, float y, float w, float h, float radius, Surface kind, bool hover = false)
    {
        float alpha = 0.12f;
        switch (kind)
        {
        case Surface::Bar: alpha = 0.12f; break;
        case Surface::Panel: alpha = 0.10f; break;
        case Surface::Tile: alpha = hover ? 0.30f : 0.14f; break;
        case Surface::Button: alpha = hover ? 0.32f : 0.16f; break;
        case Surface::Dialog: alpha = 0.16f; break;
        case Surface::Inset: alpha = 0.08f; break;
        case Surface::Item: alpha = hover ? 0.30f : 0.14f; break;
        case Surface::Popup: alpha = 0.16f; break;
        case Surface::PopupItem: alpha = hover ? 0.30f : 0.14f; break;
        }
        Color fill = theme::panelFill;
        switch (kind)
        {
        case Surface::Dialog: fill = theme::popupFill; break;
        case Surface::Item: fill = theme::itemFill; break;
        case Surface::Popup: fill = theme::popupFill; break;
        case Surface::PopupItem: fill = theme::popupItemFill; break;
        default: break;
        }
        fillRect(x, y, w, h, fill, radius);
        beginPath();
        if (kind == Surface::Bar)
        {
            const float ly = y > 0.5f ? y + 0.5f : y + h - 0.5f; // the top bar's bottom edge, the bottom bar's top
            moveTo(x, ly);
            lineTo(x + w, ly);
        }
        else
        {
            roundedRect(x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f, radius);
        }
        strokeColor(Color(1.0f, 1.0f, 1.0f, alpha));
        strokeWidth(1.0f);
        stroke();
    }

    // A button that's on: an outline in its colour over a faint tint of it
    void onSurface(const Rect& r, const Color& color, float radius)
    {
        fillRect(r.x, r.y, r.w, r.h, color.withAlpha(0.16f), radius);
        beginPath();
        roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, radius);
        strokeColor(color);
        strokeWidth(1.0f);
        stroke();
    }

    // Jost is small for its size (a low x-height), so text is drawn larger than the sizes the layout names, and small
    // text in Regular rather than Light so it stays readable. Hides NanoVG::fontSize for everything in this class.
    void fontSize(float size)
    {
        constexpr float kSmallBelow = 13.0f; // layout sizes under this are small text
        NanoVG::fontSize(size * kFontScale);
        const FontId face = size < kSmallBelow ? fUiFontSmall : fUiFont;
        if (face >= 0)
            fontFaceId(face);
    }

    // `str` shortened with … to fit `width` at the current font size
    std::string fitText(const std::string& str, float width)
    {
        Rectangle<float> bounds;
        if (textBounds(0, 0, str.c_str(), nullptr, bounds) <= width)
            return str;
        std::string text = str;
        while (!text.empty())
        {
            popUtf8(text);
            const std::string candidate = text + "…";
            if (textBounds(0, 0, candidate.c_str(), nullptr, bounds) <= width)
                return candidate;
        }
        return "…";
    }

    void label(float x, float y, const char* str, const Color& color, float size, int align)
    {
        if (str == nullptr || str[0] == '\0')
            return;
        fontSize(size);
        fillColor(dimmed(color));
        textAlign(align);
        text(x, y, str, nullptr);
    }

    // A colour as the block panel's controls draw it: dark grey while their block is bypassed (fDimControls), so they
    // show they don't change the sound (they still turn)
    Color dimmed(const Color& color) const
    {
        if (!fDimControls)
            return color;
        const float grey = (color.red + color.green + color.blue) / 3.0f * 0.42f;
        return Color(grey, grey, grey, color.alpha);
    }

    void drawButton(const Rect& r, const char* str, bool on, const Color& onColor, Hit kind, int index)
    {
        if (on)
            onSurface(r, dimmed(onColor), 6.0f);
        else
            surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Button, r.contains(fMouseX, fMouseY));
        label(r.x + r.w / 2, r.y + r.h / 2, str, on ? onColor : theme::text, 13.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ r, kind, index });
    }

    enum class Icon { Power, Gear, TuningFork, Metronome, Fretbored, GitHub, Up, Home, Star, Folder };

    // A square button with an icon instead of a label
    void drawIconButton(const Rect& r, Icon icon, bool on, Hit kind, int index)
    {
        const bool hover = r.contains(fMouseX, fMouseY);
        if (on)
            onSurface(r, theme::accent, 6.0f);
        else
            surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Button, hover);
        drawIcon(icon, r.x + r.w / 2, r.y + r.h / 2, on ? theme::accent : theme::text);
        fHits.push_back({ r, kind, index });
    }

    // A small hairline "v", for buttons that open a menu or panel
    void drawChevron(float cx, float cy, const Color& color)
    {
        beginPath();
        moveTo(cx - 4.5f, cy - 2.0f);
        lineTo(cx, cy + 2.5f);
        lineTo(cx + 4.5f, cy - 2.0f);
        strokeColor(color);
        strokeWidth(1.2f);
        lineCap(ROUND);
        lineJoin(ROUND);
        stroke();
        lineCap(BUTT);
        lineJoin(MITER);
    }

    // An icon about 18 px across, centred on (cx, cy)
    void drawIcon(Icon icon, float cx, float cy, const Color& color)
    {
        constexpr float pi = static_cast<float>(M_PI);
        lineCap(ROUND);
        lineJoin(ROUND);
        strokeColor(color);
        strokeWidth(1.2f); // hairline, like the rest of the UI
        switch (icon)
        {
        case Icon::Power:
            // a ring open at the top, with a bar through the gap
            beginPath();
            arc(cx, cy + 1.0f, 7.0f, -pi / 2 + 0.75f, -pi / 2 - 0.75f + 2 * pi, CW);
            stroke();
            beginPath();
            moveTo(cx, cy - 8.0f);
            lineTo(cx, cy);
            stroke();
            break;

        case Icon::Gear:
        {
            // eight teeth around a ring, outlined
            constexpr int teeth = 8;
            beginPath();
            for (int i = 0; i < teeth * 4; ++i)
            {
                const float a = (i - 1.0f) * 2 * pi / (teeth * 4);
                const float radius = (i % 4 < 2) ? 8.5f : 6.2f;
                if (i == 0)
                    moveTo(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
                else
                    lineTo(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
            }
            closePath();
            circle(cx, cy, 3.0f);
            stroke();
            break;
        }

        case Icon::TuningFork:
            // two prongs joined by a U, on a stem
            beginPath();
            moveTo(cx - 3.5f, cy - 8.0f);
            lineTo(cx - 3.5f, cy - 1.0f);
            arc(cx, cy - 1.0f, 3.5f, pi, 0.0f, CCW);
            lineTo(cx + 3.5f, cy - 8.0f);
            moveTo(cx, cy + 2.5f);
            lineTo(cx, cy + 8.5f);
            stroke();
            break;

        case Icon::Metronome:
            // a tapered case with a swinging arm
            beginPath();
            moveTo(cx - 2.0f, cy - 8.0f);
            lineTo(cx + 2.0f, cy - 8.0f);
            lineTo(cx + 6.5f, cy + 8.0f);
            lineTo(cx - 6.5f, cy + 8.0f);
            closePath();
            moveTo(cx - 5.0f, cy + 3.5f);
            lineTo(cx + 5.0f, cy + 3.5f);
            moveTo(cx, cy + 3.5f);
            lineTo(cx + 5.5f, cy - 6.5f);
            stroke();
            break;

        case Icon::Fretbored:
        {
            // the Fretbored app icon (its assets/icon.svg, a 120-unit square; its Ring look since 2026-10-30), flat in
            // the icon colour like the others: five strings and three frets, faint, and the notes spelling an F as
            // rings, its root note's ring heavier. A little larger than the other icons (28 px), so the rings stay open.
            const float k = 28.0f / 120.0f;
            const float x0 = cx - 14.0f, y0 = cy - 14.0f;
            beginPath();
            for (const float y : { 22.0f, 41.0f, 60.0f, 79.0f, 98.0f })
            {
                moveTo(x0 + 14.0f * k, y0 + y * k);
                lineTo(x0 + 106.0f * k, y0 + y * k);
            }
            strokeColor(color.withAlpha(color.alpha * 0.3f));
            strokeWidth(1.0f);
            stroke();
            beginPath();
            for (const float x : { 43.0f, 65.0f, 87.0f })
            {
                moveTo(x0 + x * k, y0 + 12.0f * k);
                lineTo(x0 + x * k, y0 + 108.0f * k);
            }
            strokeColor(color.withAlpha(color.alpha * 0.15f));
            strokeWidth(1.0f);
            stroke();
            // the rings are filled dark first, so the strings don't show through them
            constexpr float notes[][2] = { { 32, 41 }, { 32, 60 }, { 32, 79 }, { 32, 98 }, { 54, 22 },
                                           { 76, 22 }, { 98, 22 }, { 54, 60 }, { 76, 60 } };
            beginPath();
            for (const auto& n : notes)
                circle(x0 + n[0] * k, y0 + n[1] * k, 7.5f * k);
            circle(x0 + 32.0f * k, y0 + 22.0f * k, 8.5f * k);
            NanoVG::fillColor(Color(0x0e, 0x10, 0x14));
            fill();
            strokeColor(color);
            strokeWidth(0.8f);
            stroke();
            beginPath();
            circle(x0 + 32.0f * k, y0 + 22.0f * k, 8.5f * k);
            strokeWidth(1.3f);
            stroke();
            strokeWidth(1.2f);
            break;
        }

        case Icon::GitHub:
            // the GitHub mark in outline: a ring, and inside it the cat's head with its ears, its neck running
            // down to the ring and its tail curling out to the left
            beginPath();
            circle(cx, cy, 9.5f);
            stroke();
            beginPath();
            moveTo(cx - 1.8f, cy + 9.3f);
            lineTo(cx - 1.8f, cy + 4.6f);
            bezierTo(cx - 3.8f, cy + 4.0f, cx - 4.6f, cy + 2.4f, cx - 4.6f, cy + 0.4f);
            bezierTo(cx - 4.6f, cy - 0.8f, cx - 4.3f, cy - 1.6f, cx - 3.8f, cy - 2.2f);
            lineTo(cx - 3.8f, cy - 5.0f);
            lineTo(cx - 1.8f, cy - 3.7f);
            bezierTo(cx - 0.6f, cy - 4.0f, cx + 0.6f, cy - 4.0f, cx + 1.8f, cy - 3.7f);
            lineTo(cx + 3.8f, cy - 5.0f);
            lineTo(cx + 3.8f, cy - 2.2f);
            bezierTo(cx + 4.3f, cy - 1.6f, cx + 4.6f, cy - 0.8f, cx + 4.6f, cy + 0.4f);
            bezierTo(cx + 4.6f, cy + 2.4f, cx + 3.8f, cy + 4.0f, cx + 1.8f, cy + 4.6f);
            lineTo(cx + 1.8f, cy + 9.3f);
            moveTo(cx - 1.8f, cy + 6.4f);
            bezierTo(cx - 3.4f, cy + 6.9f, cx - 4.2f, cy + 5.4f, cx - 5.4f, cy + 5.0f);
            stroke();
            break;

        case Icon::Up:
            // an arrow pointing up: up a folder
            beginPath();
            moveTo(cx, cy + 7.0f);
            lineTo(cx, cy - 7.0f);
            moveTo(cx - 5.5f, cy - 1.5f);
            lineTo(cx, cy - 7.0f);
            lineTo(cx + 5.5f, cy - 1.5f);
            stroke();
            break;

        case Icon::Home:
            // a house: the roof, the walls and a door
            beginPath();
            moveTo(cx - 8.0f, cy - 0.5f);
            lineTo(cx, cy - 7.5f);
            lineTo(cx + 8.0f, cy - 0.5f);
            moveTo(cx - 5.5f, cy - 2.5f);
            lineTo(cx - 5.5f, cy + 7.0f);
            lineTo(cx + 5.5f, cy + 7.0f);
            lineTo(cx + 5.5f, cy - 2.5f);
            moveTo(cx - 2.0f, cy + 7.0f);
            lineTo(cx - 2.0f, cy + 2.5f);
            lineTo(cx + 2.0f, cy + 2.5f);
            lineTo(cx + 2.0f, cy + 7.0f);
            stroke();
            break;

        case Icon::Star:
            // a five-pointed star
            beginPath();
            for (int k = 0; k < 10; ++k)
            {
                const float a = -pi / 2 + k * pi / 5;
                const float rr = k % 2 == 0 ? 8.5f : 3.6f;
                if (k == 0)
                    moveTo(cx + rr * std::cos(a), cy + 0.8f + rr * std::sin(a));
                else
                    lineTo(cx + rr * std::cos(a), cy + 0.8f + rr * std::sin(a));
            }
            closePath();
            stroke();
            break;

        case Icon::Folder:
            // a folder: browse for another library folder
            drawFolderIcon(cx - 10.0f, cy - 9.0f, 20.0f, 18.0f, color);
            break;
        }
        lineCap(BUTT);
        lineJoin(MITER);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Hover help: a translucent popup explaining the control under the mouse, after it rests there a moment

    struct HelpText {
        std::string title;
        const char* body = nullptr;
    };

    HelpText helpFor(const HitArea& hit) const
    {
        switch (hit.kind)
        {
        case Hit::Knob:
        case Hit::Toggle:
        case Hit::Momentary:
        case Hit::Choice:
        case Hit::Slider:
        case Hit::Step:
        case Hit::SetValue:
        {
            const uint32_t index = static_cast<uint32_t>(hit.index);
            if (!isSlot(index))
                return { globalParamSpec(index).name, globalParamHelp(index) };
            const uint32_t offset = index - kSlotParamBase;
            const int param = static_cast<int>(offset % kSlotParamCount) - 1;
            const BlockState* block = blockInSlot(static_cast<int>(offset / kSlotParamCount));
            const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
            if (type == nullptr || param < 0 || param >= type->numParams)
                return {};
            const char* name = type->params[static_cast<size_t>(param)].name;
            return { name, blockParamHelp(type->typeId, name) };
        }
        case Hit::Insert:
            return { "Insert Block", "Adds a new block here, between the blocks on either side." };
        case Hit::Lookahead:
            return { "Lookahead", kLookaheadHelp };
        case Hit::A2Size:
            return { "A2 Full/Lite", kA2SizeHelp };
        case Hit::CabImage:
            if (hit.index >= 0 && hit.index < static_cast<int>(fChain.blocks.size()) && isNamAmp(fChain.blocks[hit.index]))
                return { "Amp Image",
                         "The amp head shown for this NAM model. Each model gets one of the built-in heads from its "
                         "file's content, one that suits the style its name suggests (clean, jazz, crunch, lead, or "
                         "very heavy for the strangest heads). Its knobs turn the block's Input, Bass, Middle, Treble "
                         "and Output, and its power switch turns the block on or off. Right-click it (or use Image…) "
                         "to pick another head or one of your own images: a built-in pick follows the file into every "
                         "chain and preset, until you pick again or reset it to the default." };
            if (hit.index >= 0 && hit.index < static_cast<int>(fChain.blocks.size()) && isNamPedal(fChain.blocks[hit.index]))
                return { "Pedal Image",
                         "The pedal shown for this NAM pedal capture. Each capture gets one of the built-in pedals "
                         "from its file's content. Right-click it (or use Image…) to pick another or one of your own "
                         "images: a built-in pick follows the file into every chain and preset, until you pick again "
                         "or reset it to the default." };
            if (hit.index >= 0 && hit.index < static_cast<int>(fChain.blocks.size())
                && fChain.blocks[hit.index].typeId == kIrBlockDescriptor.typeId)
                return { "Cab Image",
                         "The speaker cabinet shown for this IR. Each IR gets one of the built-in cabs from its file's "
                         "content (one of the size its name gives, like 4x12, if it gives one). Right-click it (or use "
                         "Image…) to pick another or one of your own images: a built-in pick follows the IR file into "
                         "every chain and preset, until you pick again or reset it to the default. You can also "
                         "right-click an IR in Add Block." };
            return { "Block Image",
                     "This block's image. Right-click it (or use Image…) to show one of your own images instead." };
        case Hit::CabChoose:
            if (hit.index < 0)
                return { "Reset to Default",
                         fCabKind == PhotoKind::Pedal
                             ? "Shows the pedal this capture gets by default again: picked from its file's content."
                         : fCabKind == PhotoKind::Amp
                             ? "Shows the amp head this model gets by default again: picked from its file's content "
                               "and name."
                             : "Shows the cab this IR gets by default again: picked from its file's content." };
            return {};
        case Hit::BlockImage:
            return { "Image",
                     "Picks the image this block shows: its stock image (for IRs and NAM models, any of the built-in "
                     "cabs, pedals or amp heads) or one of your own, from your library of amp, cab or pedal images. "
                     "Add a photo of your real gear (PNG, JPEG or WebP) and pick it. Your images are shown as they "
                     "are, with no knobs, power light or footswitch on them. Deleting an image from the library puts "
                     "the stock image back on the blocks that used it. Right-clicking the block's image opens the "
                     "same chooser." };
        case Hit::SetDefaults:
            return { "Set as Default",
                     "Makes this block's current settings the ones new blocks of its type start with, in every "
                     "instance, instead of the stock ones. Double-clicking a control (or Reset to default in its "
                     "menu) goes back to your default too. Blocks already in a chain don't change. Clear Default "
                     "puts the stock settings back." };
        case Hit::ClearDefaults:
            return { "Clear Default",
                     "Forgets the settings you saved with Set as Default for this block type: new blocks start with "
                     "the stock settings again. Blocks already in a chain don't change." };
        case Hit::ImageAdd:
            return { "Add Image",
                     "Picks an image file (8-bit PNG, JPEG or WebP) and copies it into your library, so it stays "
                     "even if you move or delete the original, and shows it on this block." };
        case Hit::Split:
            return { "Split",
                     "Splits the chain here into two parallel paths, A and B, which both start empty. Add blocks to "
                     "each with its +; the merge after them mixes the two back together. One split at a time." };
        case Hit::PhaseFix:
            return { "Out of Phase",
                     "Xythos compared the NAM and IR blocks in the two branches by playing a quiet test signal through "
                     "copies of them, and one branch comes out with its polarity flipped against the other. Merged, "
                     "the two cancel each other's low end, so the sound goes thin. Click to flip the Invert switch "
                     "named here; the warning goes away once they're in phase. The check runs in the background each "
                     "time the branches' amps, cabs, their settings or Align change." };
        case Hit::CabWarning:
            if (cabProblem(hit.index) == CabProblem::TwoCabs)
                return { "Two Cabs",
                         "This NAM model was captured with its cab (Amp + Cab), so it already sounds like an amp "
                         "through a speaker. An IR block (or a cabinet model) after it adds a second cab, which "
                         "usually sounds muffled and boxy. Bypass or remove the IR, or use an amp-only model." };
            return { "No Cab",
                     "This NAM model is an amp without its cab (Amp Head), so on its own it sounds harsh and fizzy, "
                     "like an amp's output without a speaker. Add an IR block after it, or a cabinet model, for the "
                     "sound of the amp through a speaker cab. With a split, each path after it needs one." };
        case Hit::Tile:
            if (hit.index >= 0 && hit.index < static_cast<int>(fChain.blocks.size()))
                return typeHelp(BlockRegistry::builtin().find(fChain.blocks[hit.index].typeId));
            return {};
        case Hit::DialogItem: {
            const std::vector<AddItem> items = addItems(fDialogCategory);
            return hit.index >= 0 && hit.index < static_cast<int>(items.size()) ? typeHelp(items[hit.index].type)
                                                                                 : HelpText {};
        }
        case Hit::ListRow:
            // built-in blocks in the search results (library files are just files of the NAM or IR block)
            if (hit.index >= 0 && hit.index < static_cast<int>(fRows.size()) && fRows[hit.index].entry == nullptr
                && fRows[hit.index].folder.empty())
                return typeHelp(fRows[hit.index].type);
            if (hit.index >= 0 && hit.index < static_cast<int>(fRows.size()) && fRows[hit.index].entry != nullptr
                && fRows[hit.index].type == &kIrBlockDescriptor)
                return { fRows[hit.index].entry->name,
                         "Click to hear this IR in the chain, double-click to add it. Right-click it to pick the cab "
                         "image it shows, here and in every chain and preset." };
            return {};
        case Hit::MetroAccent:
            return { "Beat " + std::to_string(hit.index + 1), globalParamHelp(kParamMetroAccent1) };
        case Hit::MetroGrouping:
            return { "Beat Grouping", globalParamHelp(kParamMetroGrouping) };
        case Hit::Tap:
            return { "Tap Tempo",
                     "Click this button in time with the music, four times or more, and the metronome takes the "
                     "tempo you tap. The T key does the same while the metronome panel is open." };
        case Hit::MetroPanel:
            return { "Metronome Settings",
                     "Opens the metronome panel: tempo, time signature, accents, beat grouping, subdivisions, "
                     "sounds, DAW sync, and the tempo and gap trainers." };
        case Hit::OpenSettings:
            return { "Settings",
                     "Opens the settings: the folders for your NAM and IR files, the default NAM A2 size, volume "
                     "normalization for new blocks, parallel processing of a split on a second CPU core, the "
                     "highlight colour, and the daily check for a new version." };
        case Hit::UpdateOpen:
            return { "New Version",
                     "A newer version of Xythos has been released. Click to open its release page in your web "
                     "browser, where you can read what's new and download it. Close your DAW before installing. "
                     "Your presets and projects keep working." };
        case Hit::UpdateDismiss:
            return { "Hide",
                     "Hides this notice until there's an even newer version. To stop checking altogether, turn off "
                     "Check for updates in the settings." };
        case Hit::SettingsUpdates:
            return { "Check for Updates",
                     "Once a day, Xythos asks GitHub for the version number of its latest release and shows a "
                     "notice in the top bar when there's a newer one. Nothing else is sent, and nothing is "
                     "downloaded or installed. Turn it off to never contact the internet." };
        case Hit::LibraryUp:
            return { "Up", "Goes up one folder in your library." };
        case Hit::LibraryHome:
            return { "Home",
                     "Goes straight back to the top of your library: the NAM or IR folder set in the settings." };
        case Hit::LibraryFavorites:
            return { "Favorites",
                     "Shows only your favorite files, from every folder of the library. Click again to browse the "
                     "folders. Star a file in the list to make it a favorite." };
        case Hit::LibraryBrowse:
            return { "Browse",
                     "Opens your system's file dialog to add a NAM model (or, in IR, a cab IR) from anywhere on your "
                     "computer, outside the library folder." };
        case Hit::GitHub:
            return { "Xythos on GitHub",
                     "Opens Xythos's page on GitHub in your web browser: the source code, new releases, and the "
                     "place to report a bug or suggest a feature." };
        case Hit::Fretbored:
            return { "Fretbored",
                     "Opens Fretbored in your web browser: a free app for learning the guitar neck, with scales, "
                     "chords, arpeggios, the circle of fifths, a fretboard trainer, a tuner and a metronome." };
        default:
            return {};
        }
    }

    static HelpText typeHelp(const BlockDescriptor* type)
    {
        if (type == nullptr)
            return {};
        return { type->displayName, blockTypeHelp(type->typeId) };
    }

    // Is a mouse button held on something (a knob drag, a tile drag)?
    bool mouseBusy() const
    {
        return fDragParam >= 0 || fPressTile >= 0;
    }

    // Frame-rate readout (XYTHOS_SHOW_FPS=1 in the host's environment), to see how often the host lets the UI run
    // and redraw: idle ticks and frames per second, the longest gap between frames, and the CPU time of a draw
    void drawFps(std::chrono::steady_clock::time_point drawStart)
    {
        const auto now = std::chrono::steady_clock::now();
        ++fFpsFrames;
        fFpsMaxGap = std::max(fFpsMaxGap, std::chrono::duration<float, std::milli>(drawStart - fFpsLastFrame).count());
        fFpsLastFrame = drawStart;
        fFpsDrawMs += std::chrono::duration<float, std::milli>(now - drawStart).count();
        const float elapsed = std::chrono::duration<float>(now - fFpsSince).count();
        if (elapsed >= 1.0f)
        {
            std::snprintf(fFpsText, sizeof(fFpsText),
                          "idle %.0f/s · mouse %.0f/s · frames %.0f/s · worst gap %.0f ms · draw %.2f ms",
                          fIdleCount / elapsed, fMotionCount / elapsed, fFpsFrames / elapsed, fFpsMaxGap,
                          fFpsDrawMs / std::max(1, fFpsFrames));
            fMotionCount = 0;
            fFpsSince = now;
            fIdleCount = 0;
            fFpsFrames = 0;
            fFpsMaxGap = 0.0f;
            fFpsDrawMs = 0.0f;
        }
        fillRect(fWidth - 452.0f, fHeight - 24.0f, 444.0f, 18.0f, Color(0, 0, 0, 0.7f), 4.0f);
        label(fWidth - 12.0f, fHeight - 15.0f, fFpsText, theme::warn, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawHelp()
    {
        const auto now = std::chrono::steady_clock::now();
        const HitArea* hit = mouseBusy() ? nullptr : hitAt(fMouseX, fMouseY);
        const HelpText help = hit != nullptr ? helpFor(*hit) : HelpText {};
        const int kind = help.body != nullptr ? static_cast<int>(hit->kind) : -1;
        const int index = help.body != nullptr ? hit->index : -1;
        if (kind != fHelpKind || index != fHelpIndex)
        {
            fHelpKind = kind;
            fHelpIndex = index;
            fHelpSince = now;
            fHelpShown = false;
            return;
        }
        if (kind < 0 || now - fHelpSince < kHelpDelay)
            return;
        fHelpShown = true;

        // measure the wrapped text, then place the popup below the control (above it if there's no room)
        constexpr float width = 440.0f, pad = 14.0f, titleSize = 14.0f, bodySize = 12.5f;
        fontSize(bodySize);
        textLineHeight(1.35f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        float bounds[4] = {};
        textBoxBounds(0.0f, 0.0f, width - 2 * pad, help.body, nullptr, bounds);
        const float bodyHeight = bounds[3] - bounds[1];
        const float titleH = titleSize * kFontScale; // drawn size
        const float h = pad + titleH + 6.0f + bodyHeight + pad;
        const Rect& r = hit->rect;
        float x = std::clamp(r.x + r.w / 2 - width / 2, 8.0f, fWidth - width - 8.0f);
        float y = r.y + r.h + 6.0f;
        if (y + h > fHeight - 8.0f)
            y = std::max(8.0f, r.y - h - 6.0f);

        beginPath();
        roundedRect(x, y, width, h, 8.0f);
        fillColor(theme::popupFill);
        fill();
        strokeColor(theme::accent.withAlpha(0.45f));
        strokeWidth(1.0f);
        stroke();

        label(x + pad, y + pad + titleH / 2, help.title.c_str(), theme::accent, titleSize, ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(bodySize);
        textLineHeight(1.35f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(theme::text);
        textBox(x + pad, y + pad + titleH + 6.0f, width - 2 * pad, help.body, nullptr);
        textLineHeight(1.0f);
    }

    void drawKnob(float cx, float cy, uint32_t index, const char* name)
    {
        drawKnobDial(cx, cy, kKnobRadius, index);
        label(cx, cy + kKnobRadius + 18.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(cx, cy + kKnobRadius + 38.0f, valueText(index).c_str(), theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);

        fHits.push_back({ { cx - kKnobRadius - 6, cy - kKnobRadius - 6, 2 * kKnobRadius + 12, 2 * kKnobRadius + 48 },
                          Hit::Knob, static_cast<int>(index) });
    }

    // A small knob with its name above its value to its right (the bottom bar; owner, 2026-10-31). Returns its width.
    float drawSideKnob(float x, float cy, uint32_t index, const char* name)
    {
        constexpr float r = 14.0f;
        drawKnobDial(x + r + 4.0f, cy, r, index);
        const float tx = x + 2 * r + 14.0f;
        label(tx, cy - 8.0f, name, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(tx, cy + 9.0f, valueText(index).c_str(), theme::text, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        constexpr float width = 2 * r + 14.0f + 62.0f;
        fHits.push_back({ { x - 2.0f, cy - r - 8.0f, width + 4.0f, 2 * r + 16.0f }, Hit::Knob, static_cast<int>(index) });
        return width;
    }

    // A knob's dial: a black body with a hairline edge, a scale of ticks lit up to the value, and a dot at the position
    void drawKnobDial(float cx, float cy, float radius, uint32_t index)
    {
        const float kKnobRadius = radius; // the shapes scale with it
        const float scale = radius / ::kKnobRadius;
        constexpr float start = 0.75f * static_cast<float>(M_PI);
        constexpr float sweep = 1.5f * static_cast<float>(M_PI);
        const float n = std::clamp(normalized(index), 0.0f, 1.0f);
        const float angle = start + n * sweep;

        // a black body with a hairline edge, a scale of ticks lit up to the value, and a dot marking the position
        beginPath();
        circle(cx, cy, kKnobRadius - 3.0f);
        fillColor(theme::background);
        fill();
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.18f));
        strokeWidth(1.0f);
        stroke();

        constexpr int ticks = 21; // a longer tick every quarter of the range
        for (int t = 0; t < ticks; ++t)
        {
            const float tickAngle = start + sweep * t / (ticks - 1);
            const float r0 = kKnobRadius + 1.0f, r1 = kKnobRadius + (t % 5 == 0 ? 6.0f : 4.0f) * std::max(0.7f, scale);
            beginPath();
            moveTo(cx + std::cos(tickAngle) * r0, cy + std::sin(tickAngle) * r0);
            lineTo(cx + std::cos(tickAngle) * r1, cy + std::sin(tickAngle) * r1);
            strokeColor(tickAngle <= angle + 1e-4f ? dimmed(theme::accent) : Color(1.0f, 1.0f, 1.0f, 0.18f));
            strokeWidth(1.0f);
            stroke();
        }

        beginPath();
        circle(cx + std::cos(angle) * (kKnobRadius - 8.0f * scale), cy + std::sin(angle) * (kKnobRadius - 8.0f * scale),
               std::max(1.6f, 2.2f * scale));
        fillColor(dimmed(theme::accent));
        fill();

        if (fLearnParam == static_cast<int>(index))
        {
            beginPath();
            circle(cx, cy, kKnobRadius + 9.0f * std::max(0.7f, scale));
            strokeColor(theme::accent);
            strokeWidth(1.5f);
            stroke();
        }
    }

    // A choice parameter: its name, and a button showing the current choice that opens a menu of them
    void drawChoice(float cx, float cy, uint32_t index, const char* name)
    {
        label(cx, cy - 26.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawButton({ cx - 42.0f, cy - 14.0f, 84.0f, 28.0f }, valueText(index).c_str(), false, theme::accent,
                   Hit::Choice, static_cast<int>(index));
    }

    // The looper's state, position in the loop and length
    // The looper's status: its state, a bar for the position in the loop (or how much of a Count In recording is
    // done), and the loop's length; while counting in, the click being counted ("2 / 4"). `slot` is the block's, for
    // its Count-In and Measures controls.
    void drawLooperStatus(float cx, float cy, float value, int slot)
    {
        const LooperStatus status = LooperStatus::decode(value);
        static constexpr const char* names[] = { "Empty", "Recording", "Playing", "Overdub", "Stopped", "Count In" };
        const Color colors[] = { theme::textDim, theme::bad, theme::good, theme::warn, theme::textDim, theme::warn };
        const int state = static_cast<int>(status.state);
        label(cx, cy - 18.0f, names[state], colors[state], 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        const auto control = [&](int param) {
            return static_cast<int>(kLooperBlockDescriptor.params[param].toValue(fParams[slotParam(slot, param)]));
        };
        char text[40];
        if (status.state == LooperState::CountIn)
        {
            std::snprintf(text, sizeof(text), "%d / %d", status.progress, control(kLooperParamCountIn));
            label(cx, cy + 4.0f, text, theme::text, 16.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }
        const float w = 84.0f;
        fillRect(cx - w / 2, cy - 4.0f, w, 8.0f, theme::inset, 3.0f);
        // a timed (Count In) recording counts its progress; a free one doesn't know its end
        const bool timed = status.state == LooperState::Recording && status.progress > 0;
        if (status.state == LooperState::Playing || status.state == LooperState::Overdubbing || timed)
            fillRect(cx - w / 2, cy - 4.0f, w * (status.progress + 1) / 100.0f, 8.0f, colors[state], 3.0f);
        if (timed)
        {
            const int measures = std::max(1, control(kLooperParamMeasures));
            std::snprintf(text, sizeof(text), "Bar %d / %d", std::min(measures, status.progress * measures / 100 + 1),
                          measures);
        }
        else
            std::snprintf(text, sizeof(text), "%.1f s", status.lengthTenths / 10.0);
        label(cx, cy + 18.0f, text, theme::text, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // Gain-reduction style meter for a block (fills downward from the top), in dB
    void drawBlockMeter(float cx, float cy, float db, float maxDb, const char* name)
    {
        constexpr float w = 10.0f, h = 64.0f;
        const float x = cx - w / 2, y = cy - h / 2 - 6.0f;
        fillRect(x, y, w, h, theme::inset, 2.0f);
        const float level = std::clamp(db / maxDb, 0.0f, 1.0f);
        if (level > 0.0f)
            fillRect(x, y, w, h * level, theme::warn, 2.0f);
        label(cx, cy + kKnobRadius + 18.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        char text[24];
        if (db >= 0.05f)
            std::snprintf(text, sizeof(text), "-%.1f dB", db);
        else
            std::snprintf(text, sizeof(text), "0.0 dB");
        label(cx, cy + kKnobRadius + 38.0f, text, theme::text, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    static Color artColor(Category category)
    {
        switch (category)
        {
        case Category::Nam:   return theme::amp;
        case Category::Ir:    return theme::cab;
        case Category::Di:         return theme::pedalDi;
        case Category::Delay:      return theme::pedalDelay;
        case Category::Reverb:     return theme::pedalReverb;
        case Category::Dynamics:   return theme::pedalDynamics;
        case Category::Distortion: return theme::pedalDrive;
        case Category::Modulation: return theme::pedalModulation;
        case Category::Eq:         return theme::pedalEq;
        case Category::Pitch:      return theme::pedalPitch;
        case Category::Synth:      return theme::pedalSynth;
        case Category::Filter:
        case Category::Wah:        return theme::pedalFilter;
        default:                   return theme::pedalOther;
        }
    }

    // Placeholder art until real images exist: a pedal, an amp head or a speaker cab, fitted into the rect, as black
    // enclosures outlined in glowing lines. `detailed` (the block panel) adds knobs; thumbnails keep the
    // shapes bold so they read at small sizes. `key` (the file's path) gives each amp and cab its own look
    // (FileLooks.hpp); empty for the generic one.
    void drawBlockArt(const BlockDescriptor* type, float x, float y, float w, float h, bool detailed,
                      const std::string& key = std::string(), Led led = Led::None)
    {
        if (type == nullptr)
            return;
        lineCap(ROUND);
        lineJoin(ROUND);
        drawBlockArtNeon(type, x, y, w, h, detailed, key, led);
        lineCap(BUTT);
        lineJoin(MITER);
    }

    // Strokes the current path as a glowing neon line: wide faint passes under a crisp one
    void glowStroke(const Color& color, float width)
    {
        width = std::max(1.0f, width);
        strokeColor(color.withAlpha(0.14f));
        strokeWidth(width * 4.0f);
        stroke();
        strokeColor(color.withAlpha(0.3f));
        strokeWidth(width * 2.2f);
        stroke();
        strokeColor(color);
        strokeWidth(width);
        stroke();
    }

    // A colour turned around the hue circle by `degrees`
    static Color shiftHue(const Color& color, float degrees)
    {
        if (degrees == 0.0f)
            return color;
        const float hi = std::max({ color.red, color.green, color.blue });
        const float lo = std::min({ color.red, color.green, color.blue });
        const float l = (hi + lo) / 2, d = hi - lo;
        if (d <= 0.0f)
            return color;
        const float s = d / (1.0f - std::fabs(2 * l - 1));
        float h = hi == color.red ? std::fmod((color.green - color.blue) / d + 6.0f, 6.0f)
                : hi == color.green ? (color.blue - color.red) / d + 2.0f
                : (color.red - color.green) / d + 4.0f;
        h = std::fmod(h / 6.0f + degrees / 360.0f + 1.0f, 1.0f);
        return Color::fromHSL(h, s, l, color.alpha);
    }

    // A file's logo, centred on (bx, by), about 2r across, on a black backing so patterns behind it stop short
    void drawBadge(Badge badge, float bx, float by, float r, const Color& color, float width)
    {
        if (badge == BadgeNone)
            return;
        const auto polygon = [&](std::initializer_list<std::pair<float, float>> points) {
            bool first = true;
            for (const auto& [px, py] : points)
            {
                if (first)
                    moveTo(bx + px * r, by + py * r);
                else
                    lineTo(bx + px * r, by + py * r);
                first = false;
            }
            closePath();
        };
        const auto shape = [&] {
            switch (badge)
            {
            case BadgeCircle:
                circle(bx, by, r);
                break;
            case BadgeDiamond:
                polygon({ { 0, -1.1f }, { 1.1f, 0 }, { 0, 1.1f }, { -1.1f, 0 } });
                break;
            case BadgeBolt:
                polygon({ { 0.25f, -1.1f }, { -0.55f, 0.15f }, { 0, 0.15f }, { -0.25f, 1.1f }, { 0.55f, -0.15f },
                          { 0, -0.15f } });
                break;
            case BadgeStar:
            {
                std::pair<float, float> points[10];
                for (int k = 0; k < 10; ++k)
                {
                    const float a = static_cast<float>(M_PI) * (k / 5.0f - 0.5f), pr = k % 2 == 0 ? 1.15f : 0.5f;
                    points[k] = { std::cos(a) * pr, std::sin(a) * pr };
                }
                moveTo(bx + points[0].first * r, by + points[0].second * r);
                for (int k = 1; k < 10; ++k)
                    lineTo(bx + points[k].first * r, by + points[k].second * r);
                closePath();
                break;
            }
            case BadgePlate:
                roundedRect(bx - 1.6f * r, by - 0.55f * r, 3.2f * r, 1.1f * r, 0.4f * r);
                break;
            case BadgeTriangle:
                polygon({ { 0, -1.05f }, { 1.1f, 0.85f }, { -1.1f, 0.85f } });
                break;
            default:
                break;
            }
        };
        beginPath();
        shape();
        fillColor(theme::neonBody);
        fill();
        glowStroke(color, width);
        if (badge == BadgePlate)
        {
            // a line of "script" on the plate
            beginPath();
            moveTo(bx - 1.0f * r, by + 0.1f * r);
            bezierTo(bx - 0.6f * r, by - 0.5f * r, bx - 0.3f * r, by + 0.5f * r, bx, by);
            bezierTo(bx + 0.3f * r, by - 0.5f * r, bx + 0.6f * r, by + 0.4f * r, bx + 1.0f * r, by - 0.1f * r);
            glowStroke(color, width * 0.8f);
        }
    }

    // A grille pattern filling the rect (clipped to it)
    void drawGrille(AmpLook::Grille grille, float gx, float gy, float gw, float gh, bool detailed, const Color& color,
                    float width)
    {
        save();
        intersectScissor(gx, gy, gw, gh);
        beginPath();
        const int rows = detailed ? 5 : 3;
        switch (grille)
        {
        case AmpLook::Slats:
            for (int row = 0; row < rows; ++row)
            {
                const float ly = gy + gh * (row + 0.5f) / rows;
                moveTo(gx + gw * 0.1f, ly);
                lineTo(gx + gw * 0.9f, ly);
            }
            break;
        case AmpLook::Bars:
        {
            const int bars = detailed ? 9 : 5;
            for (int k = 0; k < bars; ++k)
            {
                const float lx = gx + gw * (0.1f + 0.8f * (k + 0.5f) / bars);
                moveTo(lx, gy + gh * 0.15f);
                lineTo(lx, gy + gh * 0.85f);
            }
            break;
        }
        case AmpLook::Crosshatch:
        {
            const float step = gh / (detailed ? 3.0f : 2.0f);
            for (float d = -gh; d < gw + gh; d += step)
            {
                moveTo(gx + d, gy);
                lineTo(gx + d + gh, gy + gh);
                moveTo(gx + d + gh, gy);
                lineTo(gx + d, gy + gh);
            }
            break;
        }
        case AmpLook::Perforated:
        {
            const int dotRows = detailed ? 4 : 3, dotColumns = static_cast<int>(dotRows * gw / gh + 0.5f);
            const float r = gh * (detailed ? 0.045f : 0.07f);
            for (int row = 0; row < dotRows; ++row)
            {
                for (int col = 0; col < dotColumns; ++col)
                {
                    const float offset = row % 2 == 0 ? 0.25f : 0.75f;
                    circle(gx + gw * (col + offset) / dotColumns, gy + gh * (row + 0.5f) / dotRows, r);
                }
            }
            fillColor(color);
            fill();
            restore();
            return;
        }
        case AmpLook::Waves:
            for (int row = 0; row < rows; ++row)
            {
                const float ly = gy + gh * (row + 0.5f) / rows, amplitude = gh * 0.05f;
                for (int k = 0; k <= 24; ++k)
                {
                    const float u = k / 24.0f;
                    const float px = gx + gw * (0.08f + 0.84f * u);
                    const float py = ly + amplitude * std::sin(u * 6.0f * static_cast<float>(M_PI));
                    if (k == 0)
                        moveTo(px, py);
                    else
                        lineTo(px, py);
                }
            }
            break;
        case AmpLook::Zigzag:
            for (int row = 0; row < rows; ++row)
            {
                const float ly = gy + gh * (row + 0.5f) / rows, amplitude = gh * 0.06f;
                const int teeth = detailed ? 10 : 6;
                for (int k = 0; k <= teeth * 2; ++k)
                {
                    const float px = gx + gw * (0.08f + 0.84f * k / (teeth * 2.0f));
                    const float py = ly + (k % 2 == 0 ? -amplitude : amplitude);
                    if (k == 0)
                        moveTo(px, py);
                    else
                        lineTo(px, py);
                }
            }
            break;
        default:
            break;
        }
        glowStroke(color, width);
        restore();
    }

    // Black enclosures outlined in glowing lines of the kind of block's colour
    void drawBlockArtNeon(const BlockDescriptor* type, float x, float y, float w, float h, bool detailed,
                          const std::string& key, Led led)
    {
        const float cx = x + w / 2, cy = y + h / 2;
        Color color = artColor(type->category);
        color.interpolate(Color(1.0f, 1.0f, 1.0f), 0.25f); // lit: a little brighter
        const float line = detailed ? 1.0f : 2.5f; // thumbnails get bolder lines

        if (type->category == Category::Ir)
        {
            // cab: a 2x2 of speaker rings in a box, the cones, cloth, logo and colour from the file (cabLook())
            const CabLook look = cabLook(key);
            if (look.hue != 0.0f)
            {
                // the cab grey has little colour to turn; give it a little more
                color = shiftHue(color, look.hue);
                color.interpolate(shiftHue(Color(0x80, 0x98, 0xc0), look.hue), 0.35f);
            }
            const float band = look.badge != BadgeNone ? 0.22f : 0.0f; // room for the logo, in speaker cells
            constexpr int rows = 2, columns = 2;
            const float aspect = columns / (rows + band);
            const float bw = std::min(w, h * aspect), bh = bw / aspect;
            const float size = std::min(bw, bh);
            const float bx = cx - bw / 2, by = cy - bh / 2;
            const float radius = look.corners ? size * 0.03f : size * 0.08f;
            fillRect(bx, by, bw, bh, theme::neonBody, radius);
            beginPath();
            roundedRect(bx + size * 0.02f, by + size * 0.02f, bw - size * 0.04f, bh - size * 0.04f,
                        std::max(0.0f, radius - size * 0.01f));
            glowStroke(color, size * 0.012f * line);

            const float inset = size * 0.06f;
            const float top = by + inset + (bh - 2 * inset) * band / (rows + band);
            const float cellW = (bw - 2 * inset) / columns, cellH = (by + bh - inset - top) / rows;
            const float speaker = std::min(cellW, cellH) * 0.39f;
            if (look.cloth != CabLook::Bare)
            {
                Color cloth = color;
                cloth.interpolate(theme::neonBody, 0.55f);
                save();
                intersectScissor(bx + inset, top, bw - 2 * inset, by + bh - inset - top);
                beginPath();
                const float step = size * (detailed ? 0.06f : 0.1f);
                if (look.cloth == CabLook::Diagonal)
                {
                    for (float d = -bh; d < bw; d += step)
                    {
                        moveTo(bx + d, by + bh);
                        lineTo(bx + d + bh, by);
                    }
                }
                else
                {
                    for (float lx = bx + inset + step / 2; lx < bx + bw - inset; lx += step)
                    {
                        moveTo(lx, top);
                        lineTo(lx, by + bh - inset);
                    }
                }
                glowStroke(cloth, size * 0.006f * line);
                restore();
            }
            for (int r = 0; r < rows; ++r)
            {
                for (int c = 0; c < columns; ++c)
                {
                    const float sx = bx + inset + cellW * (c + 0.5f), sy = top + cellH * (r + 0.5f);
                    beginPath();
                    circle(sx, sy, speaker);
                    fillColor(theme::neonBody);
                    fill();
                    switch (look.cone)
                    {
                    case CabLook::Plain:
                        if (detailed)
                            circle(sx, sy, speaker * 0.6f);
                        circle(sx, sy, speaker * 0.2f);
                        break;
                    case CabLook::Rim:
                        circle(sx, sy, speaker * 0.84f);
                        circle(sx, sy, speaker * 0.22f);
                        break;
                    case CabLook::Spokes:
                        for (int k = 0; k < 4; ++k)
                        {
                            const float a = static_cast<float>(M_PI) * (0.25f + 0.5f * k);
                            moveTo(sx + std::cos(a) * speaker * 0.3f, sy + std::sin(a) * speaker * 0.3f);
                            lineTo(sx + std::cos(a) * speaker * 0.85f, sy + std::sin(a) * speaker * 0.85f);
                        }
                        circle(sx, sy, speaker * 0.18f);
                        break;
                    case CabLook::BigCap:
                        circle(sx, sy, speaker * 0.42f);
                        if (detailed)
                            circle(sx, sy, speaker * 0.12f);
                        break;
                    default:
                        break;
                    }
                    glowStroke(color, size * 0.012f * line);
                }
            }
            if (look.badge != BadgeNone)
            {
                const float logo = std::min((top - by - inset) * 0.42f, bw * 0.08f);
                drawBadge(look.badge, cx, (by + inset + top) / 2, logo, color, size * 0.01f * line);
            }
            if (look.corners)
            {
                beginPath();
                const float c = size * 0.09f;
                for (int k = 0; k < 4; ++k)
                {
                    const float ex = k % 2 == 0 ? bx : bx + bw, ey = k < 2 ? by : by + bh;
                    const float dx = k % 2 == 0 ? 1.0f : -1.0f, dy = k < 2 ? 1.0f : -1.0f;
                    moveTo(ex + dx * c, ey + dy * size * 0.01f);
                    lineTo(ex + dx * size * 0.01f, ey + dy * size * 0.01f);
                    lineTo(ex + dx * size * 0.01f, ey + dy * c);
                }
                glowStroke(color, size * 0.014f * line);
            }
            return;
        }

        if (type->category == Category::Nam)
        {
            // amp: a wide head, knobs along the top, a glowing grille; the knobs, grille, logo and colour (a red or
            // an orange) from the file (ampLook())
            const AmpLook look = ampLook(key);
            color = Color::fromHSL(look.hue / 360.0f + (look.hue < 0.0f ? 1.0f : 0.0f), 0.78f, 0.52f);
            color.interpolate(Color(1.0f, 1.0f, 1.0f), 0.2f); // lit
            constexpr float ratio = 2.4f;
            float aw = w, ah = w / ratio;
            if (ah > h)
            {
                ah = h;
                aw = h * ratio;
            }
            const float unit = ah; // the head's height, which sizes the details
            const float ax = cx - aw / 2, ay = cy - ah / 2;
            const float inset = unit * 0.017f;
            fillRect(ax, ay, aw, ah, theme::neonBody, unit * 0.085f);
            beginPath();
            roundedRect(ax + inset, ay + inset, aw - 2 * inset, ah - 2 * inset, unit * 0.075f);
            glowStroke(color, unit * 0.012f * line);
            float gy = ay + ah * 0.16f, gh = ah * 0.68f;
            if (detailed)
            {
                const float ky = ay + unit * 0.17f, r = unit * 0.065f;
                const float step = look.knobs >= 6 ? 0.1f : 0.6f / (look.knobs - 1);
                for (int k = 0; k < look.knobs; ++k)
                {
                    const float kx = ax + aw * (0.14f + step * k);
                    const float a = (0.75f + 1.5f * look.turns[k]) * static_cast<float>(M_PI);
                    beginPath();
                    circle(kx, ky, r);
                    moveTo(kx + std::cos(a) * r * 0.3f, ky + std::sin(a) * r * 0.3f);
                    lineTo(kx + std::cos(a) * r * 0.85f, ky + std::sin(a) * r * 0.85f);
                    glowStroke(color, unit * 0.012f);
                }
                // the pilot light
                if (led == Led::None)
                {
                    beginPath();
                    circle(ax + aw * 0.86f, ky, unit * 0.03f);
                    fillColor(color);
                    fill();
                }
                else
                {
                    drawLed(ax + aw * 0.86f, ky, unit * 0.034f, theme::led, led == Led::On);
                }
                gy = ay + unit * 0.34f;
                gh = ah - unit * 0.46f;
            }
            else
            {
                // thumbnails: the knobs as dots, so their number shows too
                const float step = look.knobs >= 6 ? 0.1f : 0.6f / (look.knobs - 1);
                beginPath();
                for (int k = 0; k < look.knobs; ++k)
                    circle(ax + aw * (0.14f + step * k), ay + ah * 0.14f, unit * 0.04f);
                fillColor(color);
                fill();
                gy = ay + ah * 0.26f;
                gh = ah * 0.62f;
            }
            const float gx = ax + unit * 0.12f, gw = aw - unit * 0.24f;
            beginPath();
            roundedRect(gx, gy, gw, gh, unit * 0.042f);
            glowStroke(color, unit * 0.012f * line);
            drawGrille(look.grille, gx, gy, gw, gh, detailed, color, unit * 0.012f * line);
            drawBadge(look.badge, gx + gw / 2, gy + gh / 2, std::min(gh * 0.2f, gw * 0.09f), color,
                      unit * 0.011f * line);
            return;
        }

        // pedal, portrait: outline, knobs, the pedal's symbol, the name and a footswitch, in the same 124x200
        // frame as the panel style
        const float s = std::min(w / 116.0f, h / 196.0f);
        const float px = cx - 58.0f * s, py = cy - 98.0f * s;
        const auto at = [&](float fx, float fy) { return std::pair { px + (fx - 4.0f) * s, py + (fy - 2.0f) * s }; };
        const auto circleAt = [&](float fx, float fy, float r) {
            const auto [ccx, ccy] = at(fx, fy);
            circle(ccx, ccy, r * s);
        };

        fillRect(px, py, 116.0f * s, 196.0f * s, theme::neonBody, 12.0f * s);
        beginPath();
        roundedRect(px + 2.0f * s, py + 2.0f * s, 112.0f * s, 192.0f * s, 10.0f * s);
        glowStroke(color, 1.6f * s * line);

        if (detailed)
        {
            static constexpr float turns[] = { 0.35f, 0.6f, 0.45f };
            beginPath();
            for (int k = 0; k < 3; ++k)
            {
                const auto [kx, ky] = at(30.0f + 32.0f * k, 36.0f);
                const float r = 10.0f * s, a = (0.75f + 1.5f * turns[k]) * static_cast<float>(M_PI);
                circle(kx, ky, r);
                moveTo(kx + std::cos(a) * r * 0.45f, ky + std::sin(a) * r * 0.45f);
                lineTo(kx + std::cos(a) * r * 0.9f, ky + std::sin(a) * r * 0.9f);
            }
            glowStroke(color, 1.4f * s);
        }

        // the pedal's own symbol (blockGlyph()); thumbnails draw it a little higher, as they have no knobs
        const Glyph* glyph = blockGlyph(type->typeId);
        if (glyph == nullptr)
            glyph = &categoryGlyph(type->category);
        const float lift = detailed ? 0.0f : 6.0f;
        beginPath();
        for (const auto& points : glyph->strokes)
        {
            for (size_t k = 0; k < points.size(); ++k)
            {
                const auto [gx, gy] = at(points[k].first, points[k].second - lift);
                if (k == 0)
                    moveTo(gx, gy);
                else
                    lineTo(gx, gy);
            }
        }
        glowStroke(color, (detailed ? 2.6f : 6.0f) * s);
        for (const Glyph::Dot& dot : glyph->dots)
        {
            beginPath();
            circleAt(dot.x, dot.y - lift, dot.r * (detailed ? 1.0f : 1.4f));
            fillColor(color);
            fill();
        }

        beginPath();
        circleAt(62, 164, 14);
        if (detailed)
            circleAt(62, 164, 7);
        glowStroke(color, 1.6f * s * line);

        // the status LED (no name: the panel already shows it)
        if (led != Led::None)
        {
            const auto [lx, ly] = at(104, 164);
            drawLed(lx, ly, (detailed ? 3.2f : 5.5f) * s, theme::led, led == Led::On);
        }
        else if (detailed)
        {
            beginPath();
            circleAt(104, 164, 2.6f);
            fillColor(color);
            fill();
        }
    }

    void drawMeter(float x, float y, float h, uint32_t left, uint32_t right, bool latched, Hit clipHit,
                   const char* name)
    {
        constexpr float barWidth = 8.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float bx = x + ch * (barWidth + 3.0f);
            fillRect(bx, y, barWidth, h, theme::inset, 2.0f);
            const float db = fParams[ch == 0 ? left : right];
            const float level = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f); // -60..+6 dB
            const Color color = db > -0.5f ? theme::bad : (db > -12.0f ? theme::warn : theme::good);
            fillRect(bx, y + h * (1.0f - level), barWidth, h * level, color, 2.0f);
        }
        // clip light, click to reset
        const Rect clip { x, y - 14.0f, 2 * barWidth + 3.0f, 8.0f };
        fillRect(clip.x, clip.y, clip.w, clip.h, latched ? theme::bad : theme::tile, 2.0f);
        fHits.push_back({ { clip.x - 4, clip.y - 4, clip.w + 8, clip.h + 8 }, clipHit, 0 });
        label(x + barWidth + 1.5f, y + h + 12.0f, name, theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // A stereo meter lying down (the bottom bar): its name on the left, two bars, and the clip light at the end
    void drawMeterH(float x, float cy, float w, uint32_t left, uint32_t right, bool latched, Hit clipHit,
                    const char* name)
    {
        label(x, cy, name, theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const float bx = x + 30.0f, bw = w - 30.0f - 14.0f;
        constexpr float barH = 5.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float by = cy - barH - 1.5f + ch * (barH + 3.0f);
            fillRect(bx, by, bw, barH, theme::inset, 2.0f);
            const float db = fParams[ch == 0 ? left : right];
            const float level = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f); // -60..+6 dB
            const Color color = db > -0.5f ? theme::bad : (db > -12.0f ? theme::warn : theme::good);
            fillRect(bx, by, bw * level, barH, color, 2.0f);
        }
        // clip light, click to reset
        const Rect clip { bx + bw + 5.0f, cy - 6.5f, 8.0f, 13.0f };
        fillRect(clip.x, clip.y, clip.w, clip.h, latched ? theme::bad : theme::tile, 2.0f);
        fHits.push_back({ { clip.x - 4, clip.y - 4, clip.w + 8, clip.h + 8 }, clipHit, 0 });
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Sections

    void drawTopBar()
    {
        surface(0, 0, fWidth, kTopBarHeight, 0.0f, Surface::Bar);
        // the logo at full size from ~1000 px wide; narrower windows shrink it so the links still fit (see below)
        const float logoRight = drawLogo(10.0f, kTopBarHeight / 2, fWidth - 682.0f);

        // preset menu with the preset's name (• when changed since it was saved or opened)
        constexpr float bh = 36.0f, by = (kTopBarHeight - bh) / 2;
        const std::string preset = presetName() + (presetModified() ? " •" : "");
        // narrower on a small window, so the icon buttons on the right (316 px with both links) still fit
        const float presetWidth = std::clamp(fWidth - 316.0f - 12.0f - (logoRight + 28.0f) - 8.0f - 158.0f, 160.0f,
                                             280.0f);
        const Rect presetButton { logoRight + 28.0f, by, presetWidth, bh };
        fPresetButtonX = presetButton.x;
        const bool presetMenuOpen = fDialog == Dialog::Menu && fPresetMenu;
        drawButton(presetButton, preset.c_str(), presetMenuOpen, theme::accent, Hit::PresetMenu, 0);
        drawChevron(presetButton.x + presetButton.w - 20.0f, presetButton.y + presetButton.h / 2,
                    presetMenuOpen ? theme::accent : theme::textDim);
        const Rect undo { presetButton.x + presetButton.w + 8.0f, by, 76.0f, bh };
        const Rect redo { undo.x + undo.w + 6.0f, by, 76.0f, bh };
        drawButton(undo, "Undo", false, theme::accent, Hit::Undo, 0);
        drawButton(redo, "Redo", false, theme::accent, Hit::Redo, 0);
        if (!fUndo.canUndo())
            fillRect(undo.x, undo.y, undo.w, undo.h, theme::background.withAlpha(0.6f), 6.0f);
        if (!fUndo.canRedo())
            fillRect(redo.x, redo.y, redo.w, redo.h, theme::background.withAlpha(0.6f), 6.0f);

        float bannerEnd = redo.x + redo.w;
        if (fLearnParam >= 0)
        {
            const std::string text = "MIDI learn: move a controller for " + paramName(fLearnParam) + "  (Esc cancels)";
            label(redo.x + redo.w + 16.0f, kTopBarHeight / 2, text.c_str(), theme::accent, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
        }
        // icon buttons on the right; hovering shows what they do
        constexpr float iw = 40.0f, step = 46.0f;
        const Rect bypass { fWidth - 16.0f - iw, by, iw, bh };
        drawIconButton(bypass, Icon::Power, fParams[kParamBypass] >= 0.5f, Hit::Toggle, kParamBypass);
        drawIconButton({ bypass.x - step, by, iw, bh }, Icon::Gear, fDialog == Dialog::Settings, Hit::OpenSettings, 0);
        drawIconButton({ bypass.x - 2 * step, by, iw, bh }, Icon::TuningFork, fParams[kParamTuner] >= 0.5f,
                       Hit::Toggle, kParamTuner);

        // Metronome: on/off, flashing on every beat, and ▾ for its panel
        const bool running = fParams[kParamMetronome] >= 0.5f;
        const Rect metro { bypass.x - 3 * step - 30.0f, by, iw, bh };
        drawIconButton(metro, Icon::Metronome, running, Hit::Toggle, kParamMetronome);
        const float pulse = beatPulse();
        if (running && pulse > 0.0f)
            fillRect(metro.x, metro.y, metro.w, metro.h, Color(255, 255, 255, 0.35f * pulse), 6.0f);
        const Rect metroPanel { metro.x + metro.w + 2.0f, by, 26.0f, bh };
        drawButton(metroPanel, "", fMetroPanel, theme::accent, Hit::MetroPanel, 0);
        drawChevron(metroPanel.x + metroPanel.w / 2, metroPanel.y + metroPanel.h / 2,
                    fMetroPanel ? theme::accent : theme::text);

        // a new release: after Redo (not during MIDI learn, which shows its message there), before the links
        if (const std::optional<std::string> update = availableUpdate(); update && fLearnParam < 0)
            bannerEnd = drawUpdateBanner(redo.x + redo.w + 16.0f, metro.x - 12.0f, by, bh, *update);

        // the links, Fretbored and then GitHub, as far as there's room: past the Redo button (and the update
        // banner), and not at all next to the MIDI learn message unless the window is wide
        const float room = fLearnParam >= 0 && fWidth < 1200.0f ? metro.x : bannerEnd + 12.0f;
        const Rect fretbored { metro.x - step, by, iw, bh };
        const Rect github { fretbored.x - step, by, iw, bh };
        if (fretbored.x >= room)
            drawIconButton(fretbored, Icon::Fretbored, false, Hit::Fretbored, 0);
        if (github.x >= room)
            drawIconButton(github, Icon::GitHub, false, Hit::GitHub, 0);
    }

    // The logo (LogoImage.cpp: the crop circle and the wordmark, with their glow), its left edge at x and centred on
    // y, ending by maxRight if it can (down to 70% of its size); returns where it ends. Its images have room around
    // them for the glow, so it's drawn a little past the bar's padding. The crop circle takes the highlight colour
    // (owner, 2026-10-30; it was fixed violet): with a gradient on, the gradient's colour where it sits.
    float drawLogo(float x, float y, float maxRight)
    {
        // the image's height, glow included: letters 32 px high, crop circle 49 across (owner, 2026-10-31: 10% smaller
        // than the 76 it was, for room)
        constexpr float kLogoHeight = 68.4f;
        if (!fLogoTried)
        {
            fLogoTried = true;
            loadImage(fLogoMark, kLogoMark, kLogoMarkSize);
            loadImage(fLogoWordmark, kLogoWordmark, kLogoWordmarkSize);
        }
        const Size<uint> size = fLogoWordmark.getSize();
        if (!fLogoMark.isValid() || !fLogoWordmark.isValid() || size.getHeight() == 0)
        {
            fontSize(24.0f);
            fillColor(theme::text);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            return text(x + 6.0f, y, "Xythos", nullptr);
        }
        const float aspect = static_cast<float>(size.getWidth()) / size.getHeight();
        const float h = std::clamp((maxRight - x) / (aspect - 0.06f), kLogoHeight * 0.7f, kLogoHeight);
        const float w = h * aspect, top = y - h / 2;
        // the crop circle's centre is 76 of the image's 152 units across (resources/logo/xythos.svg's viewBox)
        Paint mark = imagePattern(x, top, w, h, 0.0f, fLogoMark, 1.0f);
        mark.innerColor = mark.outerColor = highlightAt(x + h * 76.0f / 152.0f, y);
        beginPath();
        rect(x, top, w, h);
        fillPaint(mark);
        fill();
        beginPath();
        rect(x, top, w, h);
        fillPaint(imagePattern(x, top, w, h, 0.0f, fLogoWordmark, 1.0f));
        fill();
        return x + w - h * 0.06f; // less the glow's margin on the right
    }

    // "Xythos 1.2.0 is out" (shorter when space is tight) and a ×, between x and maxX; returns where it ends
    float drawUpdateBanner(float x, float maxX, float y, float h, const std::string& version)
    {
        constexpr float closeW = 30.0f;
        fontSize(13.0f);
        Rectangle<float> bounds;
        std::string text = "Xythos " + version + " is out";
        float textW = textBounds(0, 0, text.c_str(), nullptr, bounds);
        if (x + textW + 24.0f + closeW > maxX)
        {
            text = version + " is out";
            textW = textBounds(0, 0, text.c_str(), nullptr, bounds);
        }
        const Rect open { x, y, textW + 24.0f, h };
        if (open.x + open.w + closeW > maxX)
            return x - 16.0f; // no room at all on a narrow window; Settings still has the toggle
        const Rect close { open.x + open.w, y, closeW, h };
        const Rect all { x, y, open.w + close.w, h };
        surface(all.x, all.y, all.w, all.h, 6.0f, Surface::Button, all.contains(fMouseX, fMouseY));
        beginPath();
        roundedRect(all.x + 0.5f, all.y + 0.5f, all.w - 1.0f, all.h - 1.0f, 6.0f);
        strokeColor(theme::accent.withAlpha(0.8f));
        strokeWidth(1.0f);
        stroke();
        const bool overOpen = open.contains(fMouseX, fMouseY), overClose = close.contains(fMouseX, fMouseY);
        label(open.x + open.w / 2, y + h / 2, text.c_str(), overOpen ? theme::accent : theme::text, 13.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        label(close.x + close.w / 2 - 2.0f, y + h / 2, "×", overClose ? theme::text : theme::textDim, 18.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ open, Hit::UpdateOpen, 0 });
        fHits.push_back({ close, Hit::UpdateDismiss, 0 });
        return all.x + all.w;
    }

    // 1 right after a beat, fading to 0 over 150 ms
    float beatPulse() const
    {
        if (fParams[kParamMetroBeat] < 0.0f || fParams[kParamMetroSilent] >= 0.5f)
            return 0.0f;
        const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - fBeatAt).count();
        return std::clamp(1.0f - since / 0.15f, 0.0f, 1.0f);
    }

    void drawSlider(const Rect& r, uint32_t index, bool enabled = true)
    {
        const float n = std::clamp(normalized(index), 0.0f, 1.0f);
        globalAlpha(enabled ? 1.0f : 0.4f);
        fillRect(r.x, r.y + r.h / 2 - 2.0f, r.w, 4.0f, theme::inset, 2.0f);
        fillRect(r.x, r.y + r.h / 2 - 2.0f, r.w * n, 4.0f, theme::accent, 2.0f);
        beginPath();
        circle(r.x + r.w * n, r.y + r.h / 2, 7.0f);
        fillColor(theme::text);
        fill();
        globalAlpha(1.0f);
        // the hit rect maps x to the value, so it's exactly the track
        if (enabled)
            fHits.push_back({ r, Hit::Slider, static_cast<int>(index) });
    }

    // [−] value [+]
    void drawStepper(float x, float y, float w, const char* text, uint32_t index, float step)
    {
        drawValueButton({ x, y, 24.0f, 24.0f }, "−", false, Hit::Step, index, -step);
        label(x + w / 2, y + 12.0f, text, theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawValueButton({ x + w - 24.0f, y, 24.0f, 24.0f }, "+", false, Hit::Step, index, step);
    }

    void sectionTitle(float x, float y, const char* text)
    {
        label(x, y, text, theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawMetronome()
    {
        const Rect panel { 16.0f, kTopBarHeight + 16.0f, fWidth - 32.0f,
                           fHeight - kBottomBarHeight - kTopBarHeight - 32.0f };
        surface(panel.x, panel.y, panel.w, panel.h, 10.0f, Surface::Panel);
        label(panel.x + 16.0f, panel.y + 30.0f, "Metronome", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawValueButton({ panel.x + panel.w - 30.0f, panel.y + 6.0f, 24.0f, 24.0f }, "×", false, Hit::MetroPanel, 0, 0);

        const float gap = 20.0f;
        const float mainWidth = std::clamp(panel.w * 0.36f, 300.0f, 420.0f);
        const float colWidth = (panel.w - mainWidth - 4 * gap) / 2.0f;
        drawMetronomeMain({ panel.x + gap, panel.y + 52.0f, mainWidth, panel.h - 64.0f });
        drawMetronomeMeter({ panel.x + 2 * gap + mainWidth, panel.y + 16.0f, colWidth, panel.h - 28.0f });
        drawMetronomeTrainers({ panel.x + 3 * gap + mainWidth + colWidth, panel.y + 52.0f, colWidth,
                                panel.h - 64.0f });
    }

    void drawMetronomeMain(const Rect& r)
    {
        const bool running = fParams[kParamMetronome] >= 0.5f;
        const bool sync = fParams[kParamMetroSync] >= 0.5f;
        const bool trainer = fParams[kParamMetroTrainer] >= 0.5f;
        const bool live = running && fParams[kParamMetroBeat] >= 0.0f;
        const float cx = r.x + r.w / 2;

        // tempo: what's being played if the trainer or the DAW sets it
        float shown = fParams[kParamMetroTempo];
        if ((trainer || sync) && live)
            shown = fParams[kParamMetroLiveTempo];
        else if (trainer && !sync)
            shown = fParams[kParamMetroTrainerStart];
        char bpm[16];
        std::snprintf(bpm, sizeof(bpm), sync && !live ? "DAW" : "%.0f", shown);
        label(cx, r.y + 30.0f, bpm, theme::text, 56.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(cx, r.y + 64.0f, sync && !live ? "tempo" : (std::string("BPM · ") + tempoMarking(shown)).c_str(),
              theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        // beat dots: click to cycle accent / normal / soft / mute; the beat being heard lights up
        const int beats = metroBeats();
        const float dot = std::min(22.0f, (r.w - 8.0f) / beats - 4.0f);
        const float dotsWidth = beats * (dot + 4.0f) - 4.0f;
        const float dy = r.y + 96.0f;
        const int current = live ? static_cast<int>(fParams[kParamMetroBeat]) : -1;
        const float pulse = beatPulse();
        for (int i = 0; i < beats; ++i)
        {
            const float x = cx - dotsWidth / 2 + i * (dot + 4.0f);
            const int level = static_cast<int>(fParams[kParamMetroAccent1 + static_cast<uint32_t>(i)]);
            const float height = dot * (level == 3 ? 1.0f : level == 2 ? 0.72f : level == 1 ? 0.45f : 0.2f);
            const bool lit = i == current;
            fillRect(x, dy + dot - height, dot, height, lit ? theme::accent : theme::tile, 4.0f);
            if (lit && pulse > 0.0f)
                fillRect(x, dy + dot - height, dot, height, Color(255, 255, 255, 0.5f * pulse), 4.0f);
            fHits.push_back({ { x, dy - 4.0f, dot, dot + 8.0f }, Hit::MetroAccent, i });
        }
        label(cx, dy + dot + 12.0f, "Click a beat: accent · normal · soft · mute", theme::textDim, 10.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);

        // tempo controls (locked while the trainer or the DAW sets the tempo)
        const bool locked = trainer || sync;
        const float ty = dy + dot + 36.0f;
        globalAlpha(locked ? 0.4f : 1.0f);
        if (!locked)
        {
            drawValueButton({ r.x, ty, 34.0f, 26.0f }, "−5", false, Hit::Step, kParamMetroTempo, -5.0f);
            drawValueButton({ r.x + 38.0f, ty, 30.0f, 26.0f }, "−1", false, Hit::Step, kParamMetroTempo, -1.0f);
            drawValueButton({ r.x + r.w - 68.0f, ty, 30.0f, 26.0f }, "+1", false, Hit::Step, kParamMetroTempo, 1.0f);
            drawValueButton({ r.x + r.w - 34.0f, ty, 34.0f, 26.0f }, "+5", false, Hit::Step, kParamMetroTempo, 5.0f);
        }
        globalAlpha(1.0f);
        drawSlider({ r.x + 76.0f, ty, r.w - 152.0f, 26.0f }, kParamMetroTempo, !locked);

        const float by = ty + 40.0f;
        const float tapFlash = std::clamp(1.0f - std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                                                              fTapAt).count() / 0.15f, 0.0f, 1.0f);
        drawButton({ r.x, by, 80.0f, 32.0f }, "Tap", tapFlash > 0.0f, theme::accent, Hit::Tap, 0);
        drawButton({ r.x + 88.0f, by, r.w - 176.0f, 32.0f }, running ? "■  Stop" : "▶  Start", running, theme::accent,
                   Hit::Toggle, kParamMetronome);
        drawButton({ r.x + r.w - 80.0f, by, 80.0f, 32.0f }, "DAW sync", sync, theme::accent, Hit::Toggle,
                   kParamMetroSync);

        char status[96];
        if (!running)
            std::snprintf(status, sizeof(status), "Stopped");
        else if (!live)
            std::snprintf(status, sizeof(status), sync ? "Waiting for the DAW to play" : "Starting…");
        else
            std::snprintf(status, sizeof(status), "Bar %d · Beat %d%s", static_cast<int>(fParams[kParamMetroBar]) + 1,
                          current + 1, fParams[kParamMetroSilent] >= 0.5f ? " · (muted)" : "");
        label(cx, by + 50.0f, status, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        if (by + 74.0f < r.y + r.h)
            label(cx, by + 70.0f, "Space start/stop · ↑/↓ tempo (Shift ×5) · T tap", theme::textDim, 10.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawMetronomeMeter(const Rect& r)
    {
        float y = r.y + 14.0f;
        sectionTitle(r.x, y, "METER");
        y += 16.0f;
        char text[32];
        std::snprintf(text, sizeof(text), "%d beats", metroBeats());
        drawStepper(r.x, y, 100.0f, text, kParamMetroBeats, 1.0f);
        label(r.x + 107.0f, y + 12.0f, "/", theme::textDim, 14.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        for (int i = 0; i < 4; ++i)
        {
            std::snprintf(text, sizeof(text), "%d", kNoteValues[i]);
            drawValueButton({ r.x + 114.0f + i * 30.0f, y, 28.0f, 24.0f }, text,
                            static_cast<int>(fParams[kParamMetroNoteValue]) == i, Hit::SetValue, kParamMetroNoteValue,
                            static_cast<float>(i));
        }
        y += 32.0f;
        const std::string grouping = "Grouping: " + groupingText(currentGroups());
        drawButton({ r.x, y, std::min(150.0f, r.w / 2 - 4.0f), 24.0f }, grouping.c_str(), false, theme::accent,
                   Hit::MetroGrouping, 0);
        drawButton({ r.x + std::min(154.0f, r.w / 2), y, std::min(110.0f, r.w / 2 - 4.0f), 24.0f }, "Reset accents",
                   false, theme::accent, Hit::MetroResetAccents, 0);

        y += 42.0f;
        sectionTitle(r.x, y, "SUBDIVISION");
        y += 14.0f;
        static constexpr const char* subdivisions[] = { "Quarter", "Eighths", "Triplets", "16ths", "Quintuplets",
            "Sextuplets", "Swing", "Dotted", "8 + 16 16", "16 16 + 8", "16 8 16" };
        static constexpr const char* shortNames[] = { "1/4", "8ths", "Triplets", "16ths", "5-tuplets", "6-tuplets",
            "Swing", "Dotted", "8 16 16", "16 16 8", "16 8 16" };
        const float pill = (r.w - 3 * 4.0f) / 4.0f;
        for (int i = 0; i < 11; ++i)
        {
            drawValueButton({ r.x + (i % 4) * (pill + 4.0f), y + (i / 4) * 28.0f, pill, 24.0f },
                            pill >= 84.0f ? subdivisions[i] : shortNames[i],
                            static_cast<int>(fParams[kParamMetroSubdivision]) == i, Hit::SetValue,
                            kParamMetroSubdivision, static_cast<float>(i));
        }

        y += 3 * 28.0f + 18.0f;
        sectionTitle(r.x, y, "SOUND");
        y += 14.0f;
        static constexpr const char* sounds[] = { "Classic", "Digital", "Beep" };
        const float seg = (r.w - 2 * 4.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            drawValueButton({ r.x + i * (seg + 4.0f), y, seg, 24.0f }, sounds[i],
                            static_cast<int>(fParams[kParamMetroSound]) == i, Hit::SetValue, kParamMetroSound,
                            static_cast<float>(i));
        y += 34.0f;
        for (const uint32_t index : { kParamMetroVolume, kParamMetroSubVolume })
        {
            std::snprintf(text, sizeof(text), "%s %.0f%%", index == kParamMetroVolume ? "Volume" : "Subdivisions",
                          fParams[index]);
            label(r.x, y + 12.0f, text, theme::text, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            drawSlider({ r.x + 110.0f, y, r.w - 110.0f, 24.0f }, index);
            y += 28.0f;
        }
    }

    void drawMetronomeTrainers(const Rect& r)
    {
        float y = r.y - 22.0f;
        sectionTitle(r.x, y, "TEMPO TRAINER");
        y += 14.0f;
        static constexpr const char* modes[] = { "Off", "Step", "Gradual" };
        const int mode = static_cast<int>(fParams[kParamMetroTrainer]);
        const float seg = (r.w - 2 * 4.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            drawValueButton({ r.x + i * (seg + 4.0f), y, seg, 24.0f }, modes[i], mode == i, Hit::SetValue,
                            kParamMetroTrainer, static_cast<float>(i));
        y += 32.0f;

        char text[48];
        auto row = [&](const char* name, uint32_t index, const char* unit, float step) {
            label(r.x, y + 12.0f, name, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(text, sizeof(text), "%.0f %s", fParams[index], unit);
            drawStepper(r.x + r.w - 130.0f, y, 130.0f, text, index, step);
            y += 28.0f;
        };
        globalAlpha(mode == 0 ? 0.4f : 1.0f);
        row("Start", kParamMetroTrainerStart, "BPM", 1.0f);
        row("End", kParamMetroTrainerEnd, "BPM", 1.0f);
        if (mode == 2)
        {
            row("Over", kParamMetroTrainerBars, "bars", 1.0f);
        }
        else
        {
            row("Change by", kParamMetroTrainerStep, "BPM", 1.0f);
            row("Every", kParamMetroTrainerEvery, "bars", 1.0f);
        }
        globalAlpha(1.0f);
        if (mode != 0 && fParams[kParamMetroSync] >= 0.5f)
            label(r.x, y + 8.0f, "The trainer is off while the DAW sets the tempo", theme::warn, 11.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);

        y += 30.0f;
        sectionTitle(r.x, y, "GAP TRAINER");
        y += 14.0f;
        const bool gapOn = fParams[kParamMetroGap] >= 0.5f;
        drawButton({ r.x, y, 64.0f, 24.0f }, gapOn ? "On" : "Off", gapOn, theme::accent, Hit::Toggle, kParamMetroGap);
        label(r.x + 74.0f, y + 12.0f, "Mute the click for a few bars", theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        y += 32.0f;
        globalAlpha(gapOn ? 1.0f : 0.4f);
        row("Play", kParamMetroGapPlay, "bars", 1.0f);
        row("Then mute", kParamMetroGapMute, "bars", 1.0f);
        globalAlpha(1.0f);
    }

    // Small button that sets a parameter to a value, or steps it
    void drawValueButton(const Rect& r, const char* str, bool on, Hit kind, uint32_t index, float value)
    {
        drawButton(r, str, on, theme::accent, kind, static_cast<int>(index));
        fHits.back().value = value;
    }

    void drawTuner()
    {
        const Rect panel { 16.0f, kTopBarHeight + 16.0f, fWidth - 32.0f,
                           fHeight - kBottomBarHeight - kTopBarHeight - 32.0f };
        surface(panel.x, panel.y, panel.w, panel.h, 10.0f, Surface::Panel);
        const bool active = tunerActive();
        const float cents = active ? fTunerCents : 0.0f;
        const bool inTune = active && std::fabs(cents) <= kTunerInTuneCents;
        const Color stateColor = !active ? theme::textDim : inTune ? theme::good
                                                          : std::fabs(cents) <= 10.0f ? theme::warn : theme::accent;

        // header: view, mute, reference pitch
        const float hy = panel.y + 16.0f;
        label(panel.x + 16.0f, hy + 14.0f, "Tuner", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        // the view: Runes (the default; owner, 2026-10-31) is a parameter of its own, over Needle/Strobe
        const bool runes = fParams[kParamTunerRunes] >= 0.5f;
        const bool strobe = !runes && fParams[kParamTunerStrobe] >= 0.5f;
        drawValueButton({ panel.x + 96.0f, hy, 76.0f, 28.0f }, "Runes", runes, Hit::SetValue, kParamTunerRunes, 1);
        drawValueButton({ panel.x + 176.0f, hy, 76.0f, 28.0f }, "Needle", !runes && !strobe, Hit::SetValue,
                        kParamTunerStrobe, 0);
        drawValueButton({ panel.x + 256.0f, hy, 76.0f, 28.0f }, "Strobe", strobe, Hit::SetValue, kParamTunerStrobe, 1);
        drawButton({ panel.x + 352.0f, hy, 76.0f, 28.0f }, "Mute", fParams[kParamTunerMute] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamTunerMute);

        char ref[32];
        std::snprintf(ref, sizeof(ref), "A4 = %.0f Hz", fParams[kParamTunerReference]);
        const float rx = panel.x + panel.w - 16.0f;
        drawValueButton({ rx - 28.0f, hy, 28.0f, 28.0f }, "+", false, Hit::Step, kParamTunerReference, 1.0f);
        label(rx - 84.0f, hy + 14.0f, ref, theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawValueButton({ rx - 168.0f, hy, 28.0f, 28.0f }, "−", false, Hit::Step, kParamTunerReference, -1.0f);
        fHits.push_back({ { rx - 136.0f, hy, 104.0f, 28.0f }, Hit::Knob, static_cast<int>(kParamTunerReference) });

        // note, cents, frequency and guidance
        const float cx = panel.x + panel.w / 2;
        const float noteY = panel.y + 56.0f + (panel.h - 56.0f) * 0.28f;
        if (fTunerNote >= 0)
        {
            fontSize(84.0f);
            DGL_NAMESPACE::Rectangle<float> bounds;
            const char* name = kNoteNames[((fTunerNote % 12) + 12) % 12];
            textBounds(0, 0, name, nullptr, bounds);
            const float w = bounds.getWidth();
            label(cx - 10.0f, noteY, name, active ? stateColor : theme::textDim, 84.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            char octave[16];
            std::snprintf(octave, sizeof(octave), "%d", fTunerNote / 12 - 1);
            label(cx - 10.0f + w / 2 + 4.0f, noteY + 22.0f, octave, theme::textDim, 26.0f, ALIGN_LEFT | ALIGN_MIDDLE);

            char info[64];
            std::snprintf(info, sizeof(info), "%s%.1f ¢     %.2f Hz", cents >= 0 ? "+" : "−", std::fabs(cents),
                          fTunerFrequency);
            label(cx, noteY + 58.0f, info, theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        else
        {
            label(cx, noteY, "—", theme::textDim, 84.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        const char* status = !active ? "Play a note" : inTune ? "In tune" : cents < 0 ? "Tune up ↑" : "Tune down ↓";
        label(cx, noteY + 82.0f, status, stateColor, 14.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        const float viewTop = noteY + 104.0f;
        const float viewHeight = panel.y + panel.h - 16.0f - viewTop;
        if (viewHeight < 40.0f)
            return;
        if (runes)
            drawRunes({ panel.x + 16.0f, viewTop, panel.w - 32.0f, viewHeight }, active, inTune, stateColor);
        else if (strobe)
        {
            const float w = std::min(panel.w - 32.0f, 640.0f);
            drawStrobe({ cx - w / 2, viewTop, w, std::min(viewHeight, 160.0f) }, active, inTune);
        }
        else
            drawNeedle({ panel.x + 16.0f, viewTop, panel.w - 32.0f, viewHeight }, active, inTune);
    }

    // Needle: Spine (owner, 2026-10-31; needle concept 6 of the second round of tuner mockups). A row of hairline ticks,
    // one every 2 cents over ±50, that swell into a ridge where the pitch is; the ridge walks to the centre, between
    // two green markers, as you tune. Lit in the highlight, green in tune.
    void drawNeedle(const Rect& r, bool active, bool inTune)
    {
        constexpr float kLabels = 22.0f;  // the −50 / +50 labels under the row
        constexpr float kAbove = 69.0f, kBelow = 39.0f; // the tallest tick and the markers, at full size
        const float scale = std::clamp((r.h - kLabels - 8.0f) / (kAbove + kBelow), 0.4f, 1.0f);
        const float half = std::min(r.w, 720.0f) / 2 - 20.0f;
        const float cx = r.x + r.w / 2;
        const float y = r.y + 4.0f + kAbove * scale;
        const float pitch = std::clamp(fNeedleCents, -50.0f, 50.0f);
        auto xOf = [&](float c) { return cx + c / 50.0f * half; };

        for (int c = -50; c <= 50; c += 2)
        {
            const float d = static_cast<float>(c) - pitch;
            const float bump = active ? std::exp(-d * d / (2.0f * 5.5f * 5.5f)) : 0.0f;
            const float h = (7.0f + (c % 10 == 0 ? 4.0f : 0.0f) + 46.0f * bump) * scale;
            const float x = xOf(static_cast<float>(c));
            beginPath();
            moveTo(x, y - h);
            lineTo(x, y + h * 0.45f);
            if (bump > 0.08f)
                strokeColor((inTune ? theme::good : theme::accent).withAlpha(0.35f + 0.65f * bump));
            else
                strokeColor(std::abs(c) <= 2 ? theme::good.withAlpha(0.45f) : Color(1.0f, 1.0f, 1.0f, 0.16f));
            strokeWidth(c % 10 == 0 ? 1.5f : 1.1f);
            stroke();
        }

        // the spine itself, and the in-tune markers above and below the centre
        beginPath();
        moveTo(xOf(-50.0f) - 10.0f, y);
        lineTo(xOf(50.0f) + 10.0f, y);
        strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        beginPath();
        for (const float my : { y - 64.0f * scale, y + 34.0f * scale })
        {
            moveTo(cx, my - 5.0f);
            lineTo(cx + 4.0f, my);
            lineTo(cx, my + 5.0f);
            lineTo(cx - 4.0f, my);
            closePath();
        }
        fillColor(inTune ? theme::good : theme::good.withAlpha(0.5f));
        fill();

        const float ly = y + kBelow * scale + kLabels / 2;
        label(xOf(-50.0f), ly, "−50", theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(xOf(50.0f), ly, "+50", theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // Runes (owner, 2026-10-31; concept 8 of the tuner mockups): a row of 21 crystal glyphs, 5 cents each over ±50,
    // taller toward the centre, each with its own notches. The glyphs from the centre out to the pitch light up in the
    // highlight, the one at the pitch in the state colour; in tune only the centre one is lit, green. Nothing moves
    // but the pitch.
    void drawRunes(const Rect& r, bool active, bool inTune, const Color& color)
    {
        constexpr int kGlyphs = 21, kCentre = kGlyphs / 2;
        constexpr float kLabels = 24.0f; // the −50 … +50 labels under the row
        const float scale = std::clamp((r.h - kLabels - 8.0f) / 150.0f, 0.35f, 1.0f);
        const float pitch = std::min(32.0f, r.w / kGlyphs);
        const float half = std::min(11.0f, pitch * 0.34f); // half a glyph's width
        const float cy = r.y + 4.0f + 75.0f * scale;
        const float x0 = r.x + r.w / 2 - kCentre * pitch;
        const int at = active ? std::clamp(static_cast<int>(std::lround(fNeedleCents / 5.0f)) + kCentre, 0, kGlyphs - 1)
                              : -1;
        const Color unlit(1.0f, 1.0f, 1.0f, 0.16f);

        auto glyph = [&](int i) {
            const float x = x0 + i * pitch;
            const float h = (150.0f - std::abs(i - kCentre) * 7.0f) * scale;
            const float tip = std::min(10.0f * scale, half);
            const float y0 = cy - h / 2, y1 = cy + h / 2;
            beginPath();
            moveTo(x - half, y0 + tip);
            lineTo(x, y0);
            lineTo(x + half, y0 + tip);
            lineTo(x + half, y1 - tip);
            lineTo(x, y1);
            lineTo(x - half, y1 - tip);
            closePath();
        };
        for (int i = 0; i < kGlyphs; ++i)
        {
            const bool trail = at >= 0 && i != at && (at > kCentre ? i >= kCentre && i < at : i <= kCentre && i > at);
            glyph(i);
            if (i == at)
            {
                fillColor(inTune ? theme::good : color);
                fill();
                strokeColor(inTune ? theme::good : color);
            }
            else if (trail)
            {
                fillColor(theme::accent.withAlpha(0.28f));
                fill();
                strokeColor(theme::accent);
            }
            else
                strokeColor(i == kCentre ? theme::good.withAlpha(0.45f) : unlit); // the in-tune glyph
            strokeWidth(1.0f);
            stroke();

            // the glyph's notches: two cuts from opposite sides, placed by its index, so each has its own pattern
            const float x = x0 + i * pitch;
            const float n1 = cy + static_cast<float>((i * 37) % 5 - 2) * 14.0f * scale;
            const float n2 = cy - (static_cast<float>((i * 23) % 4) - 1.5f) * 22.0f * scale;
            beginPath();
            moveTo(x - half, n1);
            lineTo(x + half * 0.27f, n1);
            moveTo(x - half * 0.27f, n2);
            lineTo(x + half, n2);
            strokeColor(Color(0.0f, 0.0f, 0.0f, 0.85f));
            strokeWidth(2.5f);
            stroke();
        }

        // markers above and below the centre glyph, and the scale's labels
        const float top = cy - 75.0f * scale - 4.0f, bottom = cy + 75.0f * scale + 4.0f;
        const float mid = x0 + kCentre * pitch;
        beginPath();
        moveTo(mid - 7.0f, top - 9.0f);
        lineTo(mid + 7.0f, top - 9.0f);
        lineTo(mid, top - 1.0f);
        closePath();
        moveTo(mid - 7.0f, bottom + 9.0f);
        lineTo(mid + 7.0f, bottom + 9.0f);
        lineTo(mid, bottom + 1.0f);
        closePath();
        fillColor(Color(1.0f, 1.0f, 1.0f, 0.5f));
        fill();
        const float ly = bottom + 9.0f + kLabels / 2;
        for (const auto& [i, text] : { std::pair { 0, "−50" }, { 5, "−25" }, { 15, "+25" }, { 20, "+50" } })
            label(x0 + i * pitch, ly, text, theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // Strobe: Hive (owner, 2026-10-31; strobe concept 6 of the second round of tuner mockups). A honeycomb window with
    // diagonal bands of light sweeping across its cells, at a speed proportional to how far out of tune the note is;
    // still in tune. The cells are drawn in a few brightness steps, one path each.
    void drawStrobe(const Rect& r, bool active, bool inTune)
    {
        // a translucent black window with a hairline edge
        fillRect(r.x, r.y, r.w, r.h, theme::panelFill, 10.0f);
        beginPath();
        roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 10.0f);
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.18f));
        strokeWidth(1.0f);
        stroke();
        scissor(r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f);

        const Color color = !active ? Color(1.0f, 1.0f, 1.0f, 0.18f) : inTune ? theme::good : theme::accent;
        constexpr float kSide = 13.0f; // a cell's corner radius
        constexpr int kSteps = 8;      // brightness steps
        const float across = kSide * std::sqrt(3.0f);
        const float shift = fStrobePhase[0] * 0.6f;
        auto band = [&](float x, float y) { // 0..1, the diagonal bands
            const float b = 0.5f + 0.5f * std::cos((x * 0.8f + y * 0.6f - shift) / 120.0f * 2.0f * static_cast<float>(M_PI));
            return b * b * b * b;
        };
        auto hexagon = [&](float x, float y) {
            for (int k = 0; k < 6; ++k)
            {
                const float a = static_cast<float>(k) * static_cast<float>(M_PI) / 3.0f + static_cast<float>(M_PI) / 6.0f;
                const float px = x + std::cos(a) * (kSide - 2.0f), py = y + std::sin(a) * (kSide - 2.0f);
                k == 0 ? moveTo(px, py) : lineTo(px, py);
            }
            closePath();
        };
        const int rows = static_cast<int>(r.h / (kSide * 1.5f)) + 2;
        const int cols = static_cast<int>(r.w / across) + 2;
        for (int step = 0; step < kSteps; ++step)
        {
            const float lo = static_cast<float>(step) / kSteps, hi = static_cast<float>(step + 1) / kSteps;
            const float b = (lo + hi) / 2;
            beginPath();
            for (int row = 0; row < rows; ++row)
                for (int col = -1; col < cols; ++col)
                {
                    const float x = col * across + (row % 2) * across / 2, y = row * kSide * 1.5f;
                    const float v = band(x, y);
                    if (v >= lo && (v < hi || step == kSteps - 1))
                        hexagon(r.x + x, r.y + y);
                }
            if (b > 0.5f)
            {
                globalAlpha((b - 0.5f) * 0.35f);
                fillColor(color);
                fill();
            }
            globalAlpha(0.12f + 0.88f * b);
            strokeColor(color);
            strokeWidth(1.0f);
            stroke();
        }
        globalAlpha(1.0f);
        resetScissor();
    }

    void layoutChain()
    {
        ChainLayout& l = fLayout;
        const bool split = fChain.split.has_value();
        l = ChainLayout {};
        l.tile = split ? kSplitTileSize : kTileSize;
        l.rowY[0] = kChainTop;
        l.rowY[1] = kChainTop + l.tile + kRowGap;
        l.height = split ? 2 * l.tile + kRowGap : l.tile;
        l.tiles.resize(fChain.blocks.size());

        const float step = l.tile + kTileGap;
        float x = 16.0f - fScrollX;

        // Lays out one section's tiles on a row from `x`, with a drop point before each and one at the end
        auto section = [&](Section s, int row, float x0) {
            const std::vector<int> indices = fChain.sectionIndices(s);
            float sx = x0;
            for (int k = 0; k < static_cast<int>(indices.size()); ++k)
            {
                l.drops.push_back({ s, k, sx, row });
                l.tiles[indices[k]] = { sx, l.rowY[row], l.tile, l.tile };
                l.wires[row].push_back(l.tiles[indices[k]]);
                sx += step;
            }
            l.drops.push_back({ s, static_cast<int>(indices.size()), sx, row });
            return sx;
        };
        auto addTile = [&](Section s, int row, float ax) {
            l.add[static_cast<int>(s)] = { ax, l.rowY[row], l.tile, l.tile };
            l.wires[row].push_back(l.add[static_cast<int>(s)]);
        };

        x = section(Section::Pre, 0, x);
        if (!split)
        {
            addTile(Section::Pre, 0, x);
            return;
        }

        l.forkX = x + kForkWidth / 2 - kTileGap / 2;
        const Rect fork { l.forkX, l.rowY[0], 0.0f, l.tile };
        l.wires[0].push_back(fork);
        l.wires[1].push_back({ l.forkX, l.rowY[1], 0.0f, l.tile });
        x += kForkWidth;

        const float endA = section(Section::A, 0, x);
        const float endB = section(Section::B, 1, x);
        addTile(Section::A, 0, endA);
        addTile(Section::B, 1, endB);
        x = std::max(endA, endB) + step;

        l.merge = { x, l.rowY[0], kMergeWidth, l.height };
        l.wires[0].push_back({ x, l.rowY[0], kMergeWidth, l.tile });
        l.wires[1].push_back({ x, l.rowY[1], kMergeWidth, l.tile });
        x += kMergeWidth + kTileGap;

        x = section(Section::Post, 0, x);
        addTile(Section::Post, 0, x);
    }

    void drawWire(float x0, float x1, float y)
    {
        if (x1 - x0 < 4.0f)
            return;
        beginPath();
        moveTo(x0 + 3.0f, y);
        lineTo(x1 - 3.0f, y);
        strokeColor(theme::border);
        strokeWidth(2.0f);
        stroke();
    }

    void drawChain()
    {
        layoutChain();
        // how far the chain can scroll: its right end (16 px margin) to the window's edge
        fMaxScrollX = std::max(0.0f, chainRight() + fScrollX + 16.0f - fWidth);
        if (fScrollX > fMaxScrollX)
        {
            fScrollX = fMaxScrollX;
            layoutChain();
        }
        const ChainLayout& l = fLayout;
        const int count = static_cast<int>(fChain.blocks.size());

        scissor(0, kTopBarHeight, fWidth, l.height + 48.0f);

        // signal-flow lines between neighbours on each row, and the fork down to branch B
        for (int row = 0; row < 2; ++row)
        {
            for (size_t i = 1; i < l.wires[row].size(); ++i)
                drawWire(l.wires[row][i - 1].x + l.wires[row][i - 1].w, l.wires[row][i].x,
                         l.rowY[row] + l.tile / 2);
        }
        if (fChain.split)
        {
            beginPath();
            moveTo(l.forkX, l.rowY[0] + l.tile / 2);
            lineTo(l.forkX, l.rowY[1] + l.tile / 2);
            strokeColor(theme::border);
            strokeWidth(2.0f);
            stroke();
            beginPath();
            circle(l.forkX, l.rowY[0] + l.tile / 2, 4.0f);
            fillColor(theme::accent);
            fill();
            drawMergeTile();
        }

        for (int i = 0; i < count; ++i)
        {
            if (fDraggingTile && i == fPressTile)
                continue; // drawn last, under the mouse
            drawTile(i, l.tiles[i].x, l.tiles[i].y, l.tile);
        }
        for (int s = 0; s < 4; ++s)
        {
            if (l.add[s].w > 0.0f)
                drawAddTile(l.add[s], static_cast<Section>(s));
        }
        if (!fDraggingTile)
            drawGaps();

        if (fDraggingTile && fPressTile >= 0 && fPressTile < count)
        {
            if (const DropPoint* drop = dropPointAt(fMouseX, fMouseY))
            {
                const float dx = drop->x - kTileGap / 2;
                fillRect(dx - 1.5f, l.rowY[drop->row] - 4.0f, 3.0f, l.tile + 8.0f, theme::accent, 1.5f);
            }
            drawTile(fPressTile, fMouseX - l.tile / 2, fMouseY - l.tile / 2, l.tile);
        }
        resetScissor();
        drawChainScrollBar();

        if (count == 0 && !fChain.split)
            label(l.add[0].x + l.add[0].w + 24.0f, l.rowY[0] + kTileSize / 2, "Click + to add a block",
                  theme::textDim, 14.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        bool anyRunning = false;
        for (const BlockState& block : fChain.blocks)
            anyRunning |= slotRunning(block.slot);
        if (count > 0 && !anyRunning && statusSettled())
            label(16.0f, kChainTop + l.height + 14.0f,
                  "The audio engine hasn't started this chain. The host may not pass the plugin's UI messages "
                  "to it (some LV2 hosts don't); try the VST3 version.",
                  theme::bad, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawAddTile(const Rect& r, Section section)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 10.0f);
        strokeColor(theme::border);
        strokeWidth(2.0f);
        stroke();
        label(r.x + r.w / 2, r.y + r.h / 2 - 2, "+", theme::textDim, r.w * 0.37f, ALIGN_CENTER | ALIGN_MIDDLE);
        if (fChain.blocks.size() < static_cast<size_t>(kMaxBlocks))
            fHits.push_back({ r, Hit::AddBlock, static_cast<int>(section) });
    }

    // Small buttons on the signal line between blocks, shown while the mouse is over the gap: insert a block there
    // and, while there's no split, split the chain there (the upper half of the gap inserts, the lower half splits)
    void drawGaps()
    {
        const ChainLayout& l = fLayout;
        const bool canInsert = fChain.blocks.size() < static_cast<size_t>(kMaxBlocks);
        const bool canSplit = !fChain.split;
        for (size_t k = 0; k < l.drops.size(); ++k)
        {
            const DropPoint& drop = l.drops[k];
            const float cx = drop.x - kTileGap / 2;
            const float top = l.rowY[drop.row];
            const float cy = top + l.tile / 2;
            // just the gap, so a click near the edge of a tile still selects the tile
            const Rect zone { cx - kTileGap / 2, top, kTileGap, l.tile };
            const Rect insertZone = canSplit ? Rect { zone.x, zone.y, zone.w, zone.h / 2 } : zone;
            const Rect splitZone { zone.x, cy, zone.w, zone.h / 2 };
            if (canInsert)
                fHits.push_back({ insertZone, Hit::Insert, static_cast<int>(k) });
            if (canSplit)
                fHits.push_back({ splitZone, Hit::Split, drop.position });
            if (!zone.contains(fMouseX, fMouseY) || fDialog != Dialog::None)
                continue;
            if (canInsert)
            {
                const float y = canSplit ? cy - 13.0f : cy;
                gapButton(cx, y, insertZone.contains(fMouseX, fMouseY));
                beginPath();
                moveTo(cx - 4.5f, y);
                lineTo(cx + 4.5f, y);
                moveTo(cx, y - 4.5f);
                lineTo(cx, y + 4.5f);
                strokeColor(theme::accent);
                strokeWidth(1.2f);
                lineCap(ROUND);
                stroke();
                lineCap(BUTT);
            }
            if (canSplit)
            {
                const float y = canInsert ? cy + 13.0f : cy;
                gapButton(cx, y, splitZone.contains(fMouseX, fMouseY));
                // fork: one line in, two out
                beginPath();
                moveTo(cx - 5.0f, y);
                lineTo(cx - 1.0f, y);
                lineTo(cx + 4.5f, y - 4.0f);
                moveTo(cx - 1.0f, y);
                lineTo(cx + 4.5f, y + 4.0f);
                strokeColor(theme::accent);
                strokeWidth(1.2f);
                lineCap(ROUND);
                lineJoin(ROUND);
                stroke();
                lineCap(BUTT);
                lineJoin(MITER);
            }
        }
    }

    // The round button behind a gap icon; the one under the mouse gets a heavier ring
    void gapButton(float cx, float cy, bool hover)
    {
        beginPath();
        circle(cx, cy, 10.0f);
        fillColor(theme::background);
        fill();
        strokeColor(theme::accent);
        strokeWidth(hover ? 2.0f : 1.0f);
        stroke();
    }

    // The right end of the chain's last tile, + box or merge, as laid out (scrolled)
    float chainRight() const
    {
        const ChainLayout& l = fLayout;
        float right = 0.0f;
        for (const Rect& r : l.tiles)
            right = std::max(right, r.x + r.w);
        for (const Rect& r : l.add)
            if (r.w > 0.0f)
                right = std::max(right, r.x + r.w);
        if (fChain.split)
            right = std::max(right, l.merge.x + l.merge.w);
        return right;
    }

    void setScrollX(float x)
    {
        fScrollX = std::clamp(x, 0.0f, fMaxScrollX);
        repaint();
    }

    float scrollThumbX() const
    {
        return fScrollTrack.x + (fScrollTrack.w - fScrollThumbW) * (fMaxScrollX > 0.0f ? fScrollX / fMaxScrollX : 0.0f);
    }

    // A thin scroll bar under the chain, when it's wider than the window
    void drawChainScrollBar()
    {
        if (fMaxScrollX <= 0.0f)
        {
            fScrollTrack = Rect {};
            return;
        }
        fScrollTrack = { 16.0f, kChainTop + fLayout.height + 9.0f, fWidth - 32.0f, 5.0f };
        fScrollThumbW = std::max(40.0f, fScrollTrack.w * fWidth / (fWidth + fMaxScrollX));
        const Rect grab { fScrollTrack.x, fScrollTrack.y - 5.0f, fScrollTrack.w, fScrollTrack.h + 10.0f };
        const bool hot = fScrollDragging || fPanning || grab.contains(fMouseX, fMouseY);
        fillRect(fScrollTrack.x, fScrollTrack.y, fScrollTrack.w, fScrollTrack.h, theme::inset, 2.5f);
        fillRect(scrollThumbX(), fScrollTrack.y, fScrollThumbW, fScrollTrack.h, hot ? theme::accent : theme::border,
                 2.5f);
        fHits.push_back({ grab, Hit::ChainScroll, 0 });
    }

    float listThumbY() const
    {
        return fListTrack.y + (fListTrack.h - fListThumbH) * (fListMaxScroll > 0.0f ? fListScroll / fListMaxScroll : 0.0f);
    }

    // The scroll bar of a dialog's list (fListScroll), in the kListBarWidth strip at x, when the content is taller than
    // the list: drag it, or click beside the thumb to jump there, for those without a scroll wheel
    void drawListScrollBar(float x, float y, float h, float contentH)
    {
        fListMaxScroll = std::max(0.0f, contentH - h);
        if (fListMaxScroll <= 0.0f || h <= 0.0f)
        {
            fListTrack = Rect {};
            return;
        }
        fListTrack = { x + (kListBarWidth - 6.0f) / 2, y, 6.0f, h };
        fListThumbH = std::min(h, std::max(30.0f, h * h / contentH));
        const Rect grab { x, y, kListBarWidth, h };
        const bool hot = fListScrollDragging || grab.contains(fMouseX, fMouseY);
        fillRect(fListTrack.x, fListTrack.y, fListTrack.w, fListTrack.h, theme::inset, 3.0f);
        beginPath();
        roundedRect(fListTrack.x + 0.5f, fListTrack.y + 0.5f, fListTrack.w - 1.0f, fListTrack.h - 1.0f, 3.0f);
        strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        fillRect(fListTrack.x, listThumbY(), fListTrack.w, fListThumbH, hot ? theme::accent : theme::textDim.withAlpha(0.5f),
                 3.0f);
        fHits.push_back({ grab, Hit::ListScroll, 0 });
    }

    // ---- The split branches' phase check (owner, 2026-10-31)
    // What it depends on: the NAM and IR blocks in each branch (file, settings, bypass) and Align; "" without a split
    // or without such a block in both branches
    std::string phaseKey() const
    {
        if (!fChain.split)
            return {};
        std::string key;
        int counts[2] = {};
        for (int side = 0; side < 2; ++side)
        {
            key += side == 0 ? "A:" : "|B:";
            for (const int index : fChain.sectionIndices(side == 0 ? Section::A : Section::B))
            {
                const BlockState& block = fChain.blocks[static_cast<size_t>(index)];
                const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
                if (!phaseRelevant(type) || fParams[slotBypassParam(block.slot)] >= 0.5f)
                    continue;
                ++counts[side];
                key += block.typeId + block.config.dump();
                for (int p = 0; p < type->numParams; ++p)
                    key += std::to_string(std::lround(fParams[slotParam(block.slot, p)] * 1000.0f)) + ",";
            }
        }
        if (counts[0] == 0 || counts[1] == 0)
            return {};
        return key + "|align:" + std::to_string(std::lround(fParams[kParamMergeAlign] * 100.0f));
    }

    std::vector<PhaseBlock> phaseBranch(Section section) const
    {
        std::vector<PhaseBlock> branch;
        for (const int index : fChain.sectionIndices(section))
        {
            const BlockState& block = fChain.blocks[static_cast<size_t>(index)];
            const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
            if (!phaseRelevant(type) || fParams[slotBypassParam(block.slot)] >= 0.5f)
                continue;
            PhaseBlock b;
            b.type = type;
            b.config = block.config;
            for (int p = 0; p < type->numParams; ++p)
                b.params[static_cast<size_t>(p)] = fParams[slotParam(block.slot, p)];
            branch.push_back(std::move(b));
        }
        return branch;
    }

    // Starts a check once what it depends on has stayed the same for a moment (not on every step of a knob drag),
    // and takes in finished ones. Cheap when nothing changed: one string built and compared per idle tick.
    void pollPhaseCheck()
    {
        PhaseResult result;
        std::string key;
        if (fPhaseWorker.take(result, key) && key == fPhaseWanted)
        {
            fPhase = result;
            fPhaseKey = key;
            repaint();
        }
        const std::string wanted = phaseKey();
        const auto now = std::chrono::steady_clock::now();
        if (wanted != fPhaseWanted)
        {
            fPhaseWanted = wanted;
            fPhaseChangedAt = now;
            fPhaseStarted.clear();
            fPhaseWorker.cancel();
            repaint();
        }
        if (!fPhaseWanted.empty() && fPhaseWanted != fPhaseKey && fPhaseWanted != fPhaseStarted
            && now - fPhaseChangedAt > std::chrono::milliseconds(600)
            && fPhaseWorker.start(phaseBranch(Section::A), phaseBranch(Section::B), fParams[kParamMergeAlign],
                                  fPhaseWanted))
            fPhaseStarted = fPhaseWanted;
    }

    // What the merge's Invert switches do to the branches' polarity: Opposite when the merged sound cancels its low end
    // (the branches flipped against each other and no switch fixing it, or a switch flipping branches that were fine)
    Polarity mergedPolarity() const
    {
        if (fPhaseKey.empty() || fPhaseKey != fPhaseWanted || fPhase.polarity == Polarity::Unknown)
            return Polarity::Unknown;
        const bool flipped = (fParams[kParamMergeInvertA] >= 0.5f) != (fParams[kParamMergeInvertB] >= 0.5f);
        const bool opposite = fPhase.polarity == Polarity::Opposite;
        return opposite != flipped ? Polarity::Opposite : Polarity::Same;
    }

    // The fix the warning offers: the Invert switch to flip (one that's on, if one is; else Invert B)
    uint32_t phaseFixParam() const
    {
        if (fParams[kParamMergeInvertB] >= 0.5f)
            return kParamMergeInvertB;
        if (fParams[kParamMergeInvertA] >= 0.5f)
            return kParamMergeInvertA;
        return kParamMergeInvertB;
    }

    void drawMergeTile()
    {
        const Rect& r = fLayout.merge;
        const bool hover = r.contains(fMouseX, fMouseY);
        surface(r.x, r.y, r.w, r.h, 10.0f, Surface::Tile, hover);
        if (fMergeSelected)
        {
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 10.0f);
            strokeColor(theme::accent);
            strokeWidth(2.0f);
            stroke();
        }
        const float rowA = fLayout.rowY[0] + fLayout.tile / 2;
        const float rowB = fLayout.rowY[1] + fLayout.tile / 2;
        label(r.x + r.w / 2, rowA, "A", theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(r.x + r.w / 2, rowB, "B", theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(r.x + r.w / 2, (rowA + rowB) / 2, "MERGE", theme::textDim, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ r, Hit::Merge, 0 });
        if (mergedPolarity() == Polarity::Opposite)
            drawWarningSign(r.x + 13.0f, r.y + 13.0f, 7.0f); // out of phase: the merge panel says more

        const Rect remove { r.x + r.w - 22, r.y + 4, 18, 18 };
        label(remove.x + 9, remove.y + 9, "×", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ remove, Hit::MergeRemove, 0 });
    }

    // Draws block `i` as a `size` square at (x, y). The tile is designed at kTileSize and scaled.
    void drawTile(int i, float x, float y, float size)
    {
        const float k = size / kTileSize;
        auto hitRect = [&](float rx, float ry, float rw, float rh) {
            return Rect { x + rx * k, y + ry * k, rw * k, rh * k };
        };

        const BlockState& block = fChain.blocks[i];
        const std::string path = block.config.value("path", std::string());
        const bool missing = !path.empty() && !fileExists(path);
        const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
        const bool notRunning = !missing && statusSettled() && !slotRunning(block.slot);
        const bool hover = Rect { x, y, size, size }.contains(fMouseX, fMouseY);

        save();
        translate(x, y);
        this->scale(k, k);

        surface(0, 0, kTileSize, kTileSize, 10.0f, Surface::Tile, hover);
        if (i == fSelected && !fMergeSelected)
        {
            beginPath();
            roundedRect(0, 0, kTileSize, kTileSize, 10.0f);
            strokeColor(theme::accent);
            strokeWidth(2.0f / k);
            stroke();
        }

        // thumbnail (placeholder art until real images)
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        globalAlpha(bypassed || missing || notRunning ? 0.35f : 1.0f);
        const Led led = bypassed || missing || notRunning ? Led::Off : Led::On;
        if (NanoImage* img = blockImage(block, true))
            drawBlockImage(*img, type, static_cast<int>(block.slot), 16, 24, kTileSize - 32, 58, 4.0f, false, led);
        else
            drawBlockArt(type, 16, 24, kTileSize - 32, 58, false, fileLookKey(block), led);
        std::string name = missing ? "Missing file"
                         : notRunning ? "Not running"
                         : path.empty() ? (type ? type->displayName : block.typeId)
                         : fileStem(path);
        fontSize(12.0f);
        name = fitText(name, kTileSize - 12.0f);
        label(kTileSize / 2, 92, name.c_str(), missing || notRunning ? theme::bad : theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        globalAlpha(1.0f);

        // under the name: the category, or for a NAM model what it captures and its architecture; in amber with a
        // warning sign when it doesn't fit the cabs after it
        const CabProblem cab = cabProblem(i);
        std::string kind = type ? categoryName(type->category) : "";
        if (type == &kNamBlockDescriptor)
        {
            const NamFileInfo& nam = namFileInfo(block);
            if (const std::string text = namTypeText(nam.gearType, nam.arch, " · "); !text.empty())
                kind = text;
        }
        fontSize(9.0f);
        kind = fitText(kind, kTileSize - 12.0f);
        label(kTileSize / 2, 109, kind.c_str(), cab != CabProblem::None ? theme::warn : theme::textFaint, 9.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        if (cab != CabProblem::None)
            drawWarningSign(kTileSize - 36, 14, 7.0f);

        // bypass (top left) and remove (top right)
        const bool powerHover = hitRect(4, 3, 24, 24).contains(fMouseX, fMouseY);
        drawPowerButton(16, 15, 9.0f, !bypassed, powerHover);
        label(kTileSize - 15, 13, "×", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        restore();

        fHits.push_back({ { x, y, size, size }, Hit::Tile, i });
        fHits.push_back({ hitRect(4, 3, 24, 24), Hit::TileBypass, i });
        fHits.push_back({ hitRect(kTileSize - 24, 4, 18, 18), Hit::TileRemove, i });
        if (cab != CabProblem::None)
            fHits.push_back({ hitRect(kTileSize - 46, 4, 20, 20), Hit::CabWarning, i });
    }

    // A block's power button: a round black button with a hairline bezel and the power symbol, white with a soft glow
    // while the block is on, dim grey while it's bypassed
    void drawPowerButton(float cx, float cy, float r, bool on, bool hover)
    {
        // a faint lift off the tile, and the button's face, lit a little from above
        beginPath();
        circle(cx, cy + r * 0.08f, r * 1.25f);
        fillPaint(radialGradient(cx, cy + r * 0.08f, r * 0.9f, r * 1.25f, Color(0.0f, 0.0f, 0.0f, 0.7f),
                                 Color(0.0f, 0.0f, 0.0f, 0.0f)));
        fill();
        beginPath();
        circle(cx, cy, r);
        fillPaint(linearGradient(cx, cy - r, cx, cy + r, Color(0x1c, 0x1c, 0x20), Color(0x06, 0x06, 0x07)));
        fill();
        strokeColor(Color(255, 255, 255, hover ? 0.35f : on ? 0.22f : 0.12f));
        strokeWidth(1.0f);
        stroke();

        const Color ink = on ? Color(1.0f, 1.0f, 1.0f) : Color(0x6a, 0x6a, 0x72);
        const float ir = r * 0.48f;                                      // the symbol's ring
        const float gap = static_cast<float>(M_PI) * 0.30f;              // half the ring's opening, at the top
        const auto symbol = [&](const Color& color, float width) {
            beginPath();
            arc(cx, cy, ir, -static_cast<float>(M_PI) / 2 + gap, 3 * static_cast<float>(M_PI) / 2 - gap, CW);
            moveTo(cx, cy - ir * 1.25f);
            lineTo(cx, cy - ir * 0.15f);
            lineCap(ROUND);
            strokeColor(color);
            strokeWidth(width);
            stroke();
        };
        if (on)
        {
            beginPath();
            circle(cx, cy, r * 0.95f);
            fillPaint(radialGradient(cx, cy, ir * 0.4f, r * 0.95f, Color(1.0f, 1.0f, 1.0f, 0.16f),
                                     Color(1.0f, 1.0f, 1.0f, 0.0f)));
            fill();
            symbol(Color(1.0f, 1.0f, 1.0f, 0.25f), 3.2f); // glow
        }
        symbol(ink, 1.5f);
        lineCap(BUTT);
    }

    void drawSelectedPanel()
    {
        const float top = kChainTop + fLayout.height + 28.0f;
        const float left = 16.0f;
        const float width = fWidth - 32.0f;
        const float height = fHeight - kBottomBarHeight - top - 16.0f;
        if (height < 60.0f)
            return;
        surface(left, top, width, height, 10.0f, Surface::Panel);

        if (fMergeSelected && fChain.split)
        {
            drawMergePanel(top);
            return;
        }

        if (fSelected < 0 || fSelected >= static_cast<int>(fChain.blocks.size()))
        {
            label(fWidth / 2, top + height / 2, "Select a block to edit it", theme::textDim, 14.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }

        const BlockState& block = fChain.blocks[fSelected];
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        if (!type)
            return;

        // The controls: one cell each, plus block-specific extras
        enum class Cell { Param, A2Size, Lookahead, Meter };
        struct Control {
            Cell cell;
            int param;
        };
        std::vector<Control> controls;
        if (type == &kNamBlockDescriptor)
        {
            // the order the owner asked for; the slots keep theirs, since they're host parameters
            for (const int p : { kNamParamInput, kNamParamBass, kNamParamMiddle, kNamParamTreble, kNamParamOutput,
                                 kNamParamNormalize })
                controls.push_back({ Cell::Param, p });
            controls.push_back({ Cell::A2Size, 0 });
        }
        else if (type == &kLooperBlockDescriptor)
        {
            // Tap sits next to BPM, which it sets (owner, 2026-10-31); the slots keep their order
            for (int p = 0; p < type->numParams; ++p)
            {
                if (p == kLooperParamTap)
                    continue;
                controls.push_back({ Cell::Param, p });
                if (p == kLooperParamBpm)
                    controls.push_back({ Cell::Param, kLooperParamTap });
            }
        }
        else
        {
            for (int p = 0; p < type->numParams; ++p)
                controls.push_back({ Cell::Param, p });
        }
        if (type->lookahead)
            controls.push_back({ Cell::Lookahead, 0 });
        if (type->meterName)
            controls.push_back({ Cell::Meter, 0 });
        const int count = static_cast<int>(controls.size());

        // Layout: the block image on the left, the controls right-justified in rows. The image should fill the
        // panel's height, so use fewer columns (more rows) while one row would leave it too narrow for that and
        // there's height for the rows; in a small window, shrink the image, then narrow the cells.
        constexpr float kHeaderHeight = 64.0f, kImagePad = 8.0f;
        NanoImage* const img = blockImage(block);
        float aspect = 1.0f; // the image's width / height (the drawn art is square)
        if (img && img->getSize().getHeight() > 0)
            aspect = std::clamp(static_cast<float>(img->getSize().getWidth()) / img->getSize().getHeight(), 0.4f, 2.0f);
        const float imageMaxH = height - 2 * kPanelMargin;
        const float imageMaxW = (imageMaxH - 2 * kImagePad) * aspect + 2 * kImagePad;
        const float rowsSpace = height - kHeaderHeight - kPanelMargin;
        // two rows fit in less height with the rows a little tighter (a split's shorter panel: owner, 2026-10-31)
        int rowsAvailable = std::max(1, static_cast<int>(rowsSpace / kCellHeight));
        if (rowsAvailable < 2 && rowsSpace >= 2 * kCompactCellHeight)
            rowsAvailable = 2;
        const int minColumns = (count + rowsAvailable - 1) / rowsAvailable;
        const float freeWidth = width - 3 * kPanelMargin;
        int columns = std::max(count, 1);
        while (columns > minColumns && freeWidth - columns * kCellWidth < imageMaxW)
            --columns;
        const int rows = (count + columns - 1) / columns;
        float cellWidth = kCellWidth;
        const float imageWanted = std::min(imageMaxW, 140.0f);
        if (freeWidth - columns * cellWidth < imageWanted)
            cellWidth = std::max(76.0f, (freeWidth - imageWanted) / columns);
        const float imageW = std::clamp(freeWidth - columns * cellWidth, 0.0f, imageMaxW);
        const float imageH = imageW >= imageMaxW ? imageMaxH
                                                 : std::max(0.0f, (imageW - 2 * kImagePad) / aspect + 2 * kImagePad);

        // Image area: the block's image, or the drawn art; centred in the panel's height when it can't fill it
        const float imageX = left + kPanelMargin, imageY = top + (height - imageH) / 2;
        const bool showImage = imageW >= 60.0f && imageH >= 60.0f;
        if (showImage)
        {
            surface(imageX, imageY, imageW, imageH, 8.0f, Surface::Inset);
            const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
            // right-click the image to pick another (the Image… button's dialog); under the image's footswitch and
            // knobs, which drawBlockImage() adds
            fHits.push_back({ { imageX, imageY, imageW, imageH }, Hit::CabImage, fSelected });
            globalAlpha(bypassed ? 0.35f : 1.0f);
            const Led led = bypassed ? Led::Off : Led::On;
            if (img)
                drawBlockImage(*img, type, static_cast<int>(block.slot), imageX + kImagePad, imageY + kImagePad,
                               imageW - 2 * kImagePad, imageH - 2 * kImagePad, 6.0f, true, led, fSelected);
            else
                drawBlockArt(type, imageX + imageW * 0.1f, imageY + imageH * 0.1f, imageW * 0.8f, imageH * 0.8f, true,
                             fileLookKey(block), led);
            globalAlpha(1.0f);
        }

        // Header: name and file, above the controls, and the Image button (the user's own images) on the right
        const float textX = showImage ? imageX + imageW + kPanelMargin : left + kPanelMargin;
        const std::string path = block.config.value("path", std::string());
        const Rect imageButton { left + width - kPanelMargin - 92.0f, top + 14.0f, 92.0f, 28.0f };
        drawButton(imageButton, "Image…", userImage(block) != nullptr, theme::accent, Hit::BlockImage, fSelected);
        // the user's own defaults for new blocks of this type (owner, 2026-10-31)
        float buttonsX = imageButton.x;
        if (hasBlockDefaults(type->typeId))
        {
            const Rect clear { buttonsX - 8.0f - 108.0f, imageButton.y, 108.0f, imageButton.h };
            drawButton(clear, "Clear Default", false, theme::accent, Hit::ClearDefaults, fSelected);
            buttonsX = clear.x;
        }
        const Rect setDefault { buttonsX - 8.0f - 112.0f, imageButton.y, 112.0f, imageButton.h };
        drawButton(setDefault, "Set as Default", false, theme::accent, Hit::SetDefaults, fSelected);
        buttonsX = setDefault.x;
        fontSize(16.0f);
        const std::string name = fitText(path.empty() ? type->displayName : fileStem(path),
                                         std::max(0.0f, buttonsX - 12.0f - textX));
        label(textX, top + 28.0f, name.c_str(), theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        // the slot names the block's parameters in the DAW ("Slot 05 P2")
        char slotText[32];
        std::snprintf(slotText, sizeof(slotText), "Slot %02d", block.slot + 1);
        std::string detail = path.empty() ? categoryName(type->category) + std::string("  ·  ") + slotText
                                          : slotText + std::string("  ·  ") + shortenHomePath(path);
        if (type == &kNamBlockDescriptor)
        {
            const NamFileInfo& nam = namFileInfo(block);
            if (const std::string text = namTypeText(nam.gearType, nam.arch); !text.empty())
                detail = text + "  ·  " + detail;
        }
        fontSize(11.0f);
        detail = fitText(detail, std::max(0.0f, buttonsX - 12.0f - textX));
        label(textX, top + 54.0f, detail.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        if (const CabProblem cab = path.empty() || !fileExists(path) ? CabProblem::None : cabProblem(fSelected);
            cab != CabProblem::None)
        {
            const char* text = cab == CabProblem::NoCab
                                   ? "This model is an amp without a cab: add an IR block after it."
                                   : "This model includes its cab: bypass or remove the IR after it.";
            drawWarningSign(textX + 7.0f, top + 70.0f, 7.0f);
            label(textX + 20.0f, top + 70.0f, text, theme::warn, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            fHits.push_back({ { textX, top + 61.0f, 420.0f, 18.0f }, Hit::CabWarning, fSelected });
        }

        if (!path.empty() && !fileExists(path))
        {
            label(textX, top + 70.0f, "File not found. The block passes audio through until it's located.", theme::bad,
                  12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            float bx = textX;
            if (const LibraryEntry* match = libraryMatch(block))
            {
                const std::string text = "Use " + match->name + " from the library";
                drawButton({ bx, top + 86.0f, 260.0f, 28.0f }, text.c_str(), true, theme::accent, Hit::UseLibraryMatch,
                           fSelected);
                bx += 268.0f;
            }
            drawButton({ bx, top + 86.0f, 96.0f, 28.0f }, "Locate…", false, theme::accent, Hit::Locate, fSelected);
        }

        // The controls, centred in the space right of the image and spread out to fill it (up to a point, so they
        // stay a group); the last row centred too. A bypassed block's are dark grey: they still turn, but don't
        // change the sound.
        const float gridLeft = showImage ? textX : left + kPanelMargin;
        const float gridWidth = left + width - kPanelMargin - gridLeft;
        const float cellW = std::clamp(gridWidth / columns, cellWidth, cellWidth * 1.6f);
        const float rowH = std::clamp(rowsSpace / rows, rows * kCellHeight > rowsSpace ? kCompactCellHeight : kCellHeight,
                                      kCellHeight * 1.35f);
        // where a cell's knob centre is below its top: the content (knob ticks to value text) is about 96 px high
        const float knobDrop = rowH >= kCellHeight ? (rowH - kCellHeight) / 2 + 8.0f + kKnobRadius + 6.0f
                                                   : (rowH - 96.0f) / 2 + 28.0f;
        const float gridTop = top + kHeaderHeight + std::max(0.0f, (rowsSpace - rows * rowH) / 2);
        const float gridMid = gridLeft + gridWidth / 2;
        fDimControls = fParams[slotBypassParam(block.slot)] >= 0.5f;
        for (int i = 0; i < count; ++i)
        {
            const int row = i / columns;
            const int col = i % columns;
            const int inRow = row == rows - 1 ? count - row * columns : columns;
            const float cx = gridMid + (col - (inRow - 1) / 2.0f) * cellW;
            const float cy = gridTop + row * rowH + knobDrop;
            const Control& control = controls[i];

            if (control.cell == Cell::A2Size)
            {
                const bool lite = block.config.value("a2Size", 1.0) < 0.5;
                drawButton({ cx - 40.0f, cy - 14.0f, 80.0f, 28.0f }, lite ? "A2: Lite" : "A2: Full", lite,
                           theme::accent, Hit::A2Size, fSelected);
                continue;
            }
            if (control.cell == Cell::Lookahead)
            {
                const double ms = block.config.value("lookahead", 0.0);
                const std::string text = lookaheadText(ms);
                label(cx, cy - 26.0f, "Lookahead", theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
                drawButton({ cx - 42.0f, cy - 14.0f, 84.0f, 28.0f }, text.c_str(), ms > 0.0, theme::accent,
                           Hit::Lookahead, fSelected);
                continue;
            }
            if (control.cell == Cell::Meter)
            {
                if (type->meterStyle == MeterStyle::Looper)
                    drawLooperStatus(cx, cy, fParams[slotMeterParam(block.slot)], static_cast<int>(block.slot));
                else
                    drawBlockMeter(cx, cy, fParams[slotMeterParam(block.slot)], type->meterRange, type->meterName);
                continue;
            }

            const ParamSpec& spec = type->params[control.param];
            const uint32_t index = slotParam(block.slot, control.param);
            if (spec.scale == ParamScale::Toggle)
                drawButton({ cx - 40.0f, cy - 14.0f, 80.0f, 28.0f }, spec.name, fParams[index] >= 0.5f, theme::accent,
                           Hit::Toggle, static_cast<int>(index));
            else if (spec.scale == ParamScale::Momentary)
                drawButton({ cx - 40.0f, cy - 22.0f, 80.0f, 44.0f }, spec.name,
                           fParams[index] >= 0.5f || ((latchedBits(block.slot) >> control.param) & 1u) != 0,
                           theme::accent, Hit::Momentary, static_cast<int>(index));
            else if (spec.scale == ParamScale::Choice)
                drawChoice(cx, cy, index, spec.name);
            else
                drawKnob(cx, cy, index, spec.name);
        }
        fDimControls = false;
    }

    void drawAddDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(820.0f, fWidth - 64.0f);
        const float h = std::min(520.0f, fHeight - 64.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Popup);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        const bool replacing = (fPreviewing ? fPreviewReplaceSlot : fReplaceSlot) >= 0;
        label(x + 20.0f, y + 26.0f, replacing ? "Replace Block" : "Add Block", theme::text, 16.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        // categories on the left
        const float listX = x + 12.0f, listTop = y + 52.0f, listW = 210.0f;
        const bool searching = !fFields[kFieldSearch].empty();
        drawTextField({ listX + listW + 16.0f, y + 11.0f, close.x - 8.0f - (listX + listW + 16.0f), 30.0f },
                      kFieldSearch, "Search all blocks, models and IRs", true);
        const float rowH = std::min(26.0f, (h - 64.0f) / kCategoryCount);
        for (int c = 0; c < kCategoryCount; ++c)
        {
            const auto category = static_cast<Category>(c);
            const Rect r { listX, listTop + c * rowH, listW, rowH - 2.0f };
            const bool selected = category == fDialogCategory && !searching;
            const bool available = isLibraryCategory(category) || !addItems(category).empty();
            if (selected)
                fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(0.25f), 5.0f);
            else if (r.contains(fMouseX, fMouseY))
                fillRect(r.x, r.y, r.w, r.h, theme::tileHover, 5.0f);
            label(r.x + 10.0f, r.y + r.h / 2, categoryName(category), available ? theme::text : theme::textDim, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
            fHits.push_back({ r, Hit::DialogCategory, c });
        }

        // the footer: what's being heard, Cancel and OK (Enter and Esc do the same; a double-click is pick and OK)
        constexpr float kFooterH = 48.0f;
        const float listBottomY = y + h - kFooterH;
        {
            const Rect ok { x + w - 16.0f - 96.0f, y + h - 40.0f, 96.0f, 30.0f };
            const Rect cancel { ok.x - 8.0f - 96.0f, ok.y, 96.0f, 30.0f };
            drawButton(cancel, "Cancel", false, theme::accent, Hit::DialogClose, 0);
            drawButton(ok, replacing ? "Replace" : "Add", fPreviewing, theme::accent, Hit::DialogOk, 0);
            if (!fPreviewing)
                fillRect(ok.x, ok.y, ok.w, ok.h, theme::background.withAlpha(0.6f), 6.0f);
            fontSize(12.0f);
            const std::string hint = fPreviewing
                ? fitText("Hearing " + fPickLabel + " in the chain", std::max(0.0f, cancel.x - 16.0f - (x + 20.0f)))
                : fitText("Pick a block to hear it in the chain; double-click to add it",
                          std::max(0.0f, cancel.x - 16.0f - (x + 20.0f)));
            label(x + 20.0f, ok.y + ok.h / 2, hint.c_str(), fPreviewing ? theme::text : theme::textDim, 12.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
        }

        // the category's blocks on the right, or what matches the search in every category
        const float itemsX = listX + listW + 16.0f, itemsW = x + w - 16.0f - itemsX;
        if (searching)
        {
            drawSearchResults(itemsX, listTop, itemsW, listBottomY - listTop);
            return;
        }
        if (isLibraryCategory(fDialogCategory))
        {
            drawLibraryList(itemsX, listTop, itemsW, listBottomY - listTop);
            return;
        }
        const std::vector<AddItem> items = addItems(fDialogCategory);
        if (items.empty())
        {
            label(itemsX + 8.0f, listTop + 16.0f, "No blocks in this category yet.", theme::textDim, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
            return;
        }
        // one column of rows while they fit, then two, in reading order; rows keep their height (big enough to see
        // the art) and the list scrolls
        constexpr float itemH = 112.0f, gap = 8.0f, thumbW = 92.0f;
        const float itemsH = listBottomY - listTop;
        const int count = static_cast<int>(items.size());
        const int columns = count * (itemH + gap) - gap > itemsH ? 2 : 1;
        fItemColumns = columns;
        const int rows = (count + columns - 1) / columns;
        const float contentH = rows * (itemH + gap) - gap;
        if (fScrollToPick)
        {
            fScrollToPick = false;
            for (int i = 0; i < count; ++i)
            {
                if (pickKey(items[static_cast<size_t>(i)]) != fPickKey)
                    continue;
                const float top = (i / columns) * (itemH + gap);
                fListScroll = std::clamp(fListScroll, top + itemH - itemsH, top);
            }
        }
        float gridW = itemsW;
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - itemsH));
        if (contentH > itemsH)
        {
            gridW -= kListBarWidth + 4.0f;
            drawListScrollBar(itemsX + gridW + 4.0f, listTop, itemsH, contentH);
        }
        const float itemW = (gridW - (columns - 1) * gap) / columns;
        const float listBottom = listTop + itemsH;
        save();
        scissor(itemsX, listTop, gridW, itemsH);
        for (int i = 0; i < count; ++i)
        {
            const int col = i % columns, row = i / columns;
            const Rect r { itemsX + col * (itemW + gap), listTop + row * (itemH + gap) - fListScroll, itemW, itemH };
            if (r.y + r.h < listTop || r.y > listBottom)
                continue;
            const bool inside = fMouseY >= listTop && fMouseY < listBottom;
            surface(r.x, r.y, r.w, r.h, 8.0f, Surface::PopupItem, inside && r.contains(fMouseX, fMouseY));
            if (pickKey(items[i]) == fPickKey)
                drawPickOutline(r, 8.0f);
            if (NanoImage* img = typeImage(items[i].type, true))
                drawBlockImage(*img, items[i].type, -1, r.x + 10.0f, r.y + 6.0f, thumbW, r.h - 12.0f, 3.0f, false);
            else
                drawBlockArt(items[i].type, r.x + 10.0f, r.y + 8.0f, thumbW, r.h - 16.0f, false);
            fontSize(14.0f);
            const std::string name = fitText(items[i].label, r.w - thumbW - 30.0f);
            label(r.x + thumbW + 22.0f, r.y + r.h / 2, name.c_str(), theme::text, 14.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            const float top = std::max(r.y, listTop), bottom = std::min(r.y + r.h, listBottom);
            if (bottom > top)
                fHits.push_back({ { r.x, top, r.w, bottom - top }, Hit::DialogItem, i });
        }
        restore();
    }

    // The picked row or item in Add Block (the one being heard): a highlight outline over its surface
    void drawPickOutline(const Rect& r, float radius)
    {
        fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(0.12f), radius);
        beginPath();
        roundedRect(r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f, radius);
        strokeColor(theme::accent);
        strokeWidth(1.5f);
        stroke();
    }

    // A single-line text field. Click to edit; Enter or clicking elsewhere ends editing. A clearable field shows an ×
    // to empty it while it has text.
    void drawTextField(const Rect& r, Field field, const char* placeholder, bool clearable = false)
    {
        fDrawnFields.push_back(field);
        const bool clearButton = clearable && !fFields[field].empty();
        const float textRight = clearButton ? 26.0f : 0.0f; // room for the ×
        const bool focused = fFocusedField == field;
        fillRect(r.x, r.y, r.w, r.h, theme::inset, 6.0f);
        beginPath();
        roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 6.0f);
        strokeColor(focused ? theme::accent : theme::border);
        strokeWidth(1.0f);
        stroke();

        save();
        scissor(r.x + 6.0f, r.y, r.w - 12.0f - textRight, r.h);
        const std::string& text = focused ? focusedEdit().text : fFields[field];
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

        // where each character starts, relative to the text's left edge
        FieldLayout& layout = fFieldLayout[field];
        std::vector<NanoVG::GlyphPosition> glyphs(text.size() + 1);
        const int count = text.empty() ? 0
            : textGlyphPositions(0, 0, text.c_str(), nullptr, glyphs[0], static_cast<int>(glyphs.size()));
        Rectangle<float> bounds;
        const float textWidth = text.empty() ? 0.0f : textBounds(0, 0, text.c_str(), nullptr, bounds);
        std::vector<float> starts;
        layout.offsets.clear();
        for (int i = 0; i < count; ++i)
        {
            layout.offsets.push_back(static_cast<size_t>(glyphs[i].str - text.c_str()));
            starts.push_back(glyphs[i].x);
        }
        layout.offsets.push_back(text.size());
        starts.push_back(textWidth);
        auto xOf = [&](size_t pos) {
            for (size_t i = 0; i < layout.offsets.size(); ++i)
                if (layout.offsets[i] >= pos)
                    return starts[i];
            return starts.back();
        };

        // scroll to keep the cursor in view while editing; otherwise show the end (the useful part of a path)
        const float visible = r.w - 24.0f - textRight;
        float& scroll = fFieldScroll[field];
        if (textWidth <= visible)
            scroll = 0.0f;
        else if (!focused)
            scroll = textWidth - visible;
        else
        {
            const float cursorX = xOf(fEdit.cursor);
            if (cursorX - scroll > visible)
                scroll = cursorX - visible;
            if (cursorX < scroll)
                scroll = cursorX;
            scroll = std::clamp(scroll, 0.0f, textWidth - visible);
        }
        const float tx = r.x + 10.0f - scroll;
        for (float& start : starts)
            start += tx;
        layout.xs = starts;

        if (focused && fEdit.hasSelection())
        {
            const float x0 = xOf(fEdit.selectionStart()), x1 = xOf(fEdit.selectionEnd());
            fillRect(x0, r.y + 6.0f, x1 - x0, r.h - 12.0f, theme::accent.withAlpha(0.35f));
        }
        if (text.empty() && !focused)
            label(r.x + 10.0f, r.y + r.h / 2, placeholder, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        else
            label(tx, r.y + r.h / 2, text.c_str(), theme::text, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        if (focused)
            fillRect(xOf(fEdit.cursor), r.y + 7.0f, 1.5f, r.h - 14.0f, theme::accent);
        restore();

        fHits.push_back({ r, Hit::TextField, field });
        if (clearButton)
        {
            const Rect clear { r.x + r.w - 28.0f, r.y, 28.0f, r.h };
            const bool hover = clear.contains(fMouseX, fMouseY);
            const float cx = clear.x + 13.0f, cy = r.y + r.h / 2, arm = 3.5f;
            beginPath();
            circle(cx, cy, 8.0f);
            fillColor(hover ? theme::tileHover : theme::tile);
            fill();
            beginPath();
            moveTo(cx - arm, cy - arm);
            lineTo(cx + arm, cy + arm);
            moveTo(cx + arm, cy - arm);
            lineTo(cx - arm, cy + arm);
            strokeColor(hover ? theme::text : theme::textDim);
            strokeWidth(1.4f);
            stroke();
            fHits.push_back({ clear, Hit::FieldClear, field });
        }
    }

    // The library list for NAM / IR: favorites filter, browse, and the scanned files
    // The folder the NAM or IR list shows, relative to the library root ("" at the top)
    std::string& libraryDir() { return fLibraryDir[fDialogCategory == Category::Ir ? 1 : 0]; }
    LibraryHistory& libraryHistory() { return fLibraryHistory[fDialogCategory == Category::Ir ? 1 : 0]; }

    // Shows another folder in the library list, remembering where we were for Back
    void openLibraryDir(const std::string& dir)
    {
        std::string& current = libraryDir();
        if (dir == current)
            return;
        LibraryHistory& history = libraryHistory();
        history.back.push_back(std::exchange(current, dir));
        if (history.back.size() > kMaxLibraryHistory)
            history.back.erase(history.back.begin());
        history.forward.clear();
        fListScroll = 0.0f;
        repaint();
    }

    // Back (or forward) to the folder the library list showed before; false when the list isn't shown or there's
    // nowhere to go
    bool stepLibraryHistory(bool forward)
    {
        if (fDialog != Dialog::AddBlock || !isLibraryCategory(fDialogCategory) || fFavoritesOnly
            || !fFields[kFieldSearch].empty())
            return false;
        LibraryHistory& history = libraryHistory();
        std::vector<std::string>& from = forward ? history.forward : history.back;
        if (from.empty())
            return false;
        (forward ? history.back : history.forward).push_back(std::exchange(libraryDir(), from.back()));
        from.pop_back();
        fListScroll = 0.0f;
        repaint();
        return true;
    }

    // The library list for NAM / IR: the current folder's subfolders and files (or every favorite), with Up, the
    // folder, favorites and browse above it
    void drawLibraryList(float x, float y, float w, float h)
    {
        const bool ir = fDialogCategory == Category::Ir;
        const std::vector<LibraryEntry>& library = ir ? fIrLibrary : fNamLibrary;
        const std::string& folder = ir ? fSettings.irDir : fSettings.namDir;
        const BlockDescriptor* type = ir ? &kIrBlockDescriptor : &kNamBlockDescriptor;
        std::string& dir = libraryDir();

        auto inside = [](const std::string& entryFolder, const std::string& parent) {
            return parent.empty() || entryFolder == parent
                || (entryFolder.size() > parent.size() && entryFolder.compare(0, parent.size(), parent) == 0
                    && entryFolder[parent.size()] == '/');
        };
        // a folder that's gone (renamed, or another library folder chosen) goes back to the top
        if (!dir.empty() && !fScanning
            && std::none_of(library.begin(), library.end(),
                            [&](const LibraryEntry& entry) { return inside(entry.folder, dir); }))
            dir.clear();
        const bool browsing = !fFavoritesOnly;

        // header: up, where we are, favorites, browse
        const float headerH = 30.0f;
        float pathX = x + 2.0f;
        if (browsing && !folder.empty())
        {
            // up a folder, and home: the library folder itself (owner, 2026-10-30); icons only, so the path has
            // the room (their names are in the hover help)
            globalAlpha(dir.empty() ? 0.4f : 1.0f);
            drawIconButton({ x, y, headerH, headerH }, Icon::Up, false, Hit::LibraryUp, 0);
            drawIconButton({ x + headerH + 6.0f, y, headerH, headerH }, Icon::Home, false, Hit::LibraryHome, 0);
            globalAlpha(1.0f);
            pathX = x + 2 * headerH + 16.0f;
        }
        if (!folder.empty())
        {
            // where we are: the library folder (home as ~), then each folder below it; a click on one goes there
            struct Crumb {
                std::string text;
                int level; // folders below the root; -1: not clickable
            };
            std::vector<Crumb> crumbs { { shortenHomePath(folder), browsing ? 0 : -1 } };
            if (browsing)
            {
                size_t start = 0;
                for (int level = 1; start < dir.size(); ++level)
                {
                    const size_t slash = std::min(dir.find('/', start), dir.size());
                    crumbs.push_back({ dir.substr(start, slash - start), level });
                    start = slash + 1;
                }
            }
            else
                crumbs.push_back({ "favorites in every folder", -1 });
            const char* separator = browsing ? "  ›  " : "  ·  ";
            fontSize(12.0f);
            Rectangle<float> bounds;
            auto width = [&](const std::string& str) { return textBounds(0, 0, str.c_str(), nullptr, bounds); };
            const float sepW = width(separator);
            float total = -sepW;
            for (const Crumb& crumb : crumbs)
                total += width(crumb.text) + sepW;
            // too long: the first folders give way to "…" (which goes to the last of them), keeping where we are
            const float room = x + w - 2 * headerH - 16.0f - pathX;
            size_t first = 0;
            while (total > room && first + 1 < crumbs.size())
            {
                total -= width(crumbs[first].text) + sepW;
                ++first;
            }
            if (first > 0)
            {
                --first;
                crumbs[first].text = "…";
            }
            save();
            scissor(pathX, y, std::max(0.0f, room), headerH);
            float cx = pathX;
            for (size_t i = first; i < crumbs.size(); ++i)
            {
                const Crumb& crumb = crumbs[i];
                const float cw = width(crumb.text);
                const bool here = i + 1 == crumbs.size();
                const Rect r { cx - 3.0f, y, cw + 6.0f, headerH };
                const bool clickable = crumb.level >= 0 && !here;
                const bool hover = clickable && r.contains(fMouseX, fMouseY);
                label(cx, y + headerH / 2, crumb.text.c_str(),
                      hover ? theme::accent : here ? theme::text : theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                if (clickable)
                    fHits.push_back({ r, Hit::LibraryCrumb, crumb.level });
                cx += cw;
                if (!here)
                {
                    label(cx, y + headerH / 2, separator, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                    cx += sepW;
                }
            }
            restore();
        }
        drawIconButton({ x + w - 2 * headerH - 6.0f, y, headerH, headerH }, Icon::Star, fFavoritesOnly,
                       Hit::LibraryFavorites, 0);
        drawIconButton({ x + w - headerH, y, headerH, headerH }, Icon::Folder, false, Hit::LibraryBrowse, 0);

        const float listY = y + headerH + 10.0f;
        const float listH = h - headerH - 10.0f - kCountHeight;

        fRows.clear();
        size_t files = 0, folders = 0;
        if (browsing)
        {
            // subfolders first (sorted by name, as the scan sorts them), each with how many files it holds
            std::map<std::string, size_t> subfolders;
            for (const LibraryEntry& entry : library)
            {
                if (entry.folder == dir || !inside(entry.folder, dir))
                    continue;
                const size_t start = dir.empty() ? 0 : dir.size() + 1;
                const size_t end = entry.folder.find('/', start);
                ++subfolders[entry.folder.substr(0, end)];
            }
            for (const auto& [path, count] : subfolders)
            {
                AddRow row;
                row.folder = path;
                row.files = count;
                fRows.push_back(std::move(row));
            }
            folders = fRows.size();
            for (const LibraryEntry& entry : library)
            {
                if (entry.folder == dir)
                    fRows.push_back({ nullptr, type, &entry, {}, 0, false });
            }
            files = fRows.size() - folders;
        }
        else
        {
            for (const LibraryEntry& entry : library)
            {
                if (fSettings.favorites.count(entry.hash) != 0)
                    fRows.push_back({ nullptr, type, &entry });
            }
            files = fRows.size();
        }

        auto message = [&](const char* text) {
            label(x + 8.0f, listY + 18.0f, text, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        };
        if (folder.empty())
        {
            message(ir ? "Set your IR folder in Settings to list your cab IRs here."
                       : "Set your NAM folder in Settings to list your models here.");
            drawButton({ x + 8.0f, listY + 40.0f, 120.0f, 28.0f }, "Open Settings", false, theme::accent,
                       Hit::OpenSettings, 0);
            return;
        }
        if (fRows.empty())
        {
            if (fScanning)
                message("Scanning…");
            else if (library.empty())
                message(ir ? "No .wav files found in your IR folder." : "No .nam files found in your NAM folder.");
            else if (browsing)
                message("This folder is empty.");
            else
                message("No favorites yet: click a file's ☆ to add it.");
            return;
        }
        drawRows(x, listY, w, listH);

        char count[96];
        if (browsing && folders > 0)
            std::snprintf(count, sizeof(count), "%zu folder%s · %zu file%s here · %zu in all", folders,
                          folders == 1 ? "" : "s", files, files == 1 ? "" : "s", library.size());
        else if (browsing)
            std::snprintf(count, sizeof(count), "%zu file%s here · %zu in all", files, files == 1 ? "" : "s",
                          library.size());
        else
            std::snprintf(count, sizeof(count), "%zu of %zu", files, library.size());
        label(x + w, y + h + 2.0f, count, theme::textDim, 10.0f, ALIGN_RIGHT | ALIGN_BOTTOM);
    }

    // What matches the search in every category: built-in blocks, NAM models and IRs, under category headings
    void drawSearchResults(float x, float y, float w, float h)
    {
        const std::string& query = fFields[kFieldSearch];
        fRows.clear();
        size_t matches = 0;
        for (int c = 0; c < kCategoryCount; ++c)
        {
            const auto category = static_cast<Category>(c);
            const size_t heading = fRows.size();
            fRows.push_back({ categoryName(category), nullptr, nullptr });
            if (isLibraryCategory(category))
            {
                const bool ir = category == Category::Ir;
                for (const LibraryEntry& entry : ir ? fIrLibrary : fNamLibrary)
                {
                    if (matchesSearch(entry, query))
                        fRows.push_back({ nullptr, ir ? &kIrBlockDescriptor : &kNamBlockDescriptor, &entry });
                }
            }
            else
            {
                for (const AddItem& item : addItems(category))
                {
                    if (matchesSearch(*item.type, query))
                        fRows.push_back({ nullptr, item.type, nullptr });
                }
            }
            if (fRows.size() == heading + 1)
                fRows.pop_back(); // nothing in this category
            else
                matches += fRows.size() - heading - 1;
        }

        if (fRows.empty())
        {
            label(x + 8.0f, y + 16.0f, fScanning ? "Scanning your library…" : "Nothing matches.", theme::textDim, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
            return;
        }
        drawRows(x, y, w, h - kCountHeight);

        char count[64];
        std::snprintf(count, sizeof(count), matches == 1 ? "%zu match" : "%zu matches", matches);
        label(x + w, y + h + 2.0f, count, theme::textDim, 10.0f, ALIGN_RIGHT | ALIGN_BOTTOM);
    }

    // A folder outline (tab and body), fitted into the rect
    void drawFolderIcon(float x, float y, float w, float h, const Color& color)
    {
        const float fw = std::min(w, h * 1.3f) * 0.78f, fh = fw * 0.72f;
        const float fx = x + (w - fw) / 2, fy = y + (h - fh) / 2;
        beginPath();
        moveTo(fx, fy + fh * 0.16f);
        lineTo(fx, fy + 2.0f);
        quadTo(fx, fy, fx + 2.0f, fy);
        lineTo(fx + fw * 0.36f, fy);
        lineTo(fx + fw * 0.46f, fy + fh * 0.16f);
        lineTo(fx + fw - 2.0f, fy + fh * 0.16f);
        quadTo(fx + fw, fy + fh * 0.16f, fx + fw, fy + fh * 0.16f + 2.0f);
        lineTo(fx + fw, fy + fh - 2.0f);
        quadTo(fx + fw, fy + fh, fx + fw - 2.0f, fy + fh);
        lineTo(fx + 2.0f, fy + fh);
        quadTo(fx, fy + fh, fx, fy + fh - 2.0f);
        closePath();
        moveTo(fx, fy + fh * 0.3f);
        lineTo(fx + fw, fy + fh * 0.3f);
        strokeColor(color);
        strokeWidth(1.2f);
        stroke();
    }

    static constexpr float kCountHeight = 12.0f; // under the list: "12 of 40", "5 matches"

    // Draws fRows as a scrolling list: headings, and rows to click (library files with a favorite star)
    void drawRows(float x, float listY, float w, float listH)
    {
        constexpr float rowH = 76.0f, headingH = 28.0f, thumbW = 96.0f, textX = thumbW + 20.0f;
        float contentH = 0.0f, pickTop = -1.0f;
        for (const AddRow& row : fRows)
        {
            if (row.heading == nullptr && !fPickKey.empty() && pickKey(row) == fPickKey)
                pickTop = contentH;
            contentH += row.heading ? headingH : rowH;
        }
        if (std::exchange(fScrollToPick, false) && pickTop >= 0.0f)
            fListScroll = std::clamp(fListScroll, pickTop + rowH - listH, pickTop);
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));
        if (contentH > listH)
        {
            w -= kListBarWidth + 4.0f;
            drawListScrollBar(x + w + 4.0f, listY, listH, contentH);
        }

        save();
        scissor(x, listY, w, listH);
        float ry = listY - fListScroll;
        for (int i = 0; i < static_cast<int>(fRows.size()) && ry < listY + listH; ++i)
        {
            const AddRow& row = fRows[i];
            const float rh = row.heading ? headingH : rowH;
            const float y0 = ry;
            ry += rh;
            if (ry < listY)
                continue;
            if (row.heading)
            {
                label(x + 4.0f, y0 + headingH - 9.0f, row.heading, theme::accent, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                continue;
            }

            const Rect r { x, y0, w, rowH - 4.0f };
            const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
            surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Item, hover);
            if (!fPickKey.empty() && pickKey(row) == fPickKey)
                drawPickOutline(r, 6.0f);
            const float top = std::max(r.y, listY), bottom = std::min(r.y + r.h, listY + listH);

            if (!row.folder.empty())
            {
                drawFolderIcon(r.x + 8.0f, r.y + 10.0f, thumbW, r.h - 20.0f, hover ? theme::accent : theme::textDim);
                const size_t slash = row.folder.rfind('/');
                const std::string name = slash == std::string::npos ? row.folder : row.folder.substr(slash + 1);
                label(r.x + textX, r.y + r.h / 2 - 9.0f, name.c_str(), theme::text, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                char detail[48];
                std::snprintf(detail, sizeof(detail), "%zu file%s", row.files, row.files == 1 ? "" : "s");
                label(r.x + textX, r.y + r.h / 2 + 9.0f, detail, theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                label(r.x + r.w - 20.0f, r.y + r.h / 2, "›", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
                if (bottom > top)
                    fHits.push_back({ { r.x, top, r.w, bottom - top }, Hit::ListRow, i });
                continue;
            }

            NanoImage* img = row.entry == nullptr ? typeImage(row.type, true)
                           : row.type == &kIrBlockDescriptor
                               ? cabImage(cabImageFor(row.entry->hash, row.entry->path), true)
                           : namGear(row.entry->gearType) == NamGear::Pedal
                               ? pedalImage(pedalImageFor(row.entry->hash, row.entry->path), true)
                           : isAmpGear(namGear(row.entry->gearType))
                               ? ampImage(ampImageFor(row.entry->hash, row.entry->path), true)
                               : nullptr;
            if (img)
                drawBlockImage(*img, row.type, -1, r.x + 8.0f, r.y + 4.0f, thumbW, r.h - 8.0f, 3.0f, false);
            else
                drawBlockArt(row.type, r.x + 8.0f, r.y + 6.0f, thumbW, r.h - 12.0f, false,
                             row.entry ? row.entry->path : std::string());

            std::string detail;
            if (row.entry)
            {
                if (row.showFolder)
                    detail = row.entry->folder;
                if (!row.entry->info.empty())
                    detail += (detail.empty() ? "" : "  ·  ") + row.entry->info;
                if (const std::string kind = namTypeText(row.entry->gearType, row.entry->arch); !kind.empty())
                    detail += (detail.empty() ? "" : "  ·  ") + kind;
            }
            else
            {
                detail = categoryName(row.type->category);
            }
            label(r.x + textX, r.y + r.h / 2 - 9.0f, row.entry ? row.entry->name.c_str() : row.type->displayName,
                  theme::text, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            label(r.x + textX, r.y + r.h / 2 + 9.0f, detail.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

            // only rows inside the list area are clickable
            if (!row.entry)
            {
                if (bottom > top)
                    fHits.push_back({ { r.x, top, r.w, bottom - top }, Hit::ListRow, i });
                continue;
            }
            const bool favorite = fSettings.favorites.count(row.entry->hash) != 0;
            const Rect star { r.x + r.w - 40.0f, r.y, 40.0f, r.h };
            label(star.x + 20.0f, star.y + r.h / 2, favorite ? "★" : "☆", favorite ? theme::accent : theme::textDim,
                  18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            if (bottom > top)
            {
                fHits.push_back({ { r.x, top, r.w - 40.0f, bottom - top }, Hit::ListRow, i });
                fHits.push_back({ { star.x, top, star.w, bottom - top }, Hit::ListStar, i });
            }
        }
        restore();
    }

    void drawMenu()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fMenuPanels.clear();
        Rect panel = drawMenuPanel(0, std::string(), fMenuX, fMenuY, -1.0f);
        // the open submenus, each beside the row of the folder that opened it (fMenuOpen can grow while drawing:
        // hovering a folder opens its submenu)
        for (size_t level = 0; level < fMenuOpen.size(); ++level)
        {
            const auto [path, rowY] = fMenuOpen[level];
            panel = drawMenuPanel(static_cast<int>(level) + 1, path, panel.x + panel.w - 4.0f, rowY - 4.0f, panel.x);
        }
    }

    // One panel of the menu: the entries listed in `parent`, at (x, y), or left of `leftOf` when there's no room to
    // the right (-1: the menu itself). Returns where it was drawn.
    Rect drawMenuPanel(int level, const std::string& parent, float x, float y, float leftOf)
    {
        constexpr float itemH = 30.0f;
        std::vector<size_t> items;
        for (size_t i = 0; i < fMenu.size(); ++i)
            if (fMenu[i].parent == parent)
                items.push_back(i);
        const float menuW = level == 0 ? fMenuW : std::min(fMenuW, 240.0f); // submenus narrower, so a few fit side by side
        const float contentH = items.size() * itemH + 8.0f;
        const float h = std::min(contentH, fHeight - 16.0f); // a long list scrolls
        if (leftOf >= 0.0f && x + menuW > fWidth - 8.0f)
            x = leftOf - menuW + 4.0f;
        x = std::clamp(x, 8.0f, std::max(8.0f, fWidth - menuW - 8.0f));
        y = std::clamp(y, 8.0f, std::max(8.0f, fHeight - h - 8.0f));
        if (fMenuScrolls.size() <= static_cast<size_t>(level))
            fMenuScrolls.resize(level + 1, 0.0f);
        float& scroll = level == 0 ? fListScroll : fMenuScrolls[level];
        scroll = std::clamp(scroll, 0.0f, contentH - h);
        const Rect bounds { x, y, menuW, h };
        fMenuPanels.push_back(bounds);
        surface(x, y, menuW, h, 8.0f, Surface::Popup);
        fHits.push_back({ bounds, Hit::DialogPanel, 0 });
        const float itemsW = contentH > h ? menuW - kListBarWidth - 2.0f : menuW;
        if (contentH > h)
        {
            // the list scroll bar drives fListScroll: the menu itself's
            if (level == 0)
                drawListScrollBar(x + itemsW - 2.0f, y + 6.0f, h - 12.0f, contentH - 12.0f);
        }

        const bool inside = bounds.contains(fMouseX, fMouseY);
        save();
        scissor(x, y + 2.0f, menuW, h - 4.0f);
        for (size_t k = 0; k < items.size(); ++k)
        {
            const size_t i = items[k];
            const MenuEntry& entry = fMenu[i];
            const Rect r { x + 4.0f, y + 4.0f + k * itemH - scroll, itemsW - 8.0f, itemH };
            if (r.y + r.h < y || r.y > y + h)
                continue;
            const bool folder = !entry.submenu.empty();
            const bool heading = entry.action == MenuAction::None && !folder;
            const bool hover = inside && r.contains(fMouseX, fMouseY);
            // hovering a row opens its folder's submenu, or closes the submenus of this panel's other folders
            if (hover)
            {
                const bool alreadyOpen = folder && fMenuOpen.size() > static_cast<size_t>(level)
                                         && fMenuOpen[level].first == entry.submenu;
                if (!alreadyOpen && fMenuOpen.size() > static_cast<size_t>(level))
                    fMenuOpen.resize(level);
                if (folder && !alreadyOpen)
                {
                    fMenuOpen.emplace_back(entry.submenu, r.y);
                    fMenuScrolls.resize(level + 1);
                }
            }
            const bool open = folder && fMenuOpen.size() > static_cast<size_t>(level)
                              && fMenuOpen[level].first == entry.submenu;
            if (!heading && (hover || open))
                fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(open && !hover ? 0.18f : 0.3f), 5.0f);
            const float tx = r.x + 10.0f;
            // the open preset in the highlight colour
            const Color color = heading ? theme::textDim : entry.current ? theme::accent : theme::text;
            const float size = heading ? 11.0f : 13.0f;
            fontSize(size);
            const std::string text = fitText(entry.label, r.x + r.w - (folder ? 24.0f : 8.0f) - tx);
            label(tx, r.y + r.h / 2, text.c_str(), color, size, ALIGN_LEFT | ALIGN_MIDDLE);
            if (folder)
            {
                // a hairline ">" at the right: it opens a submenu
                beginPath();
                moveTo(r.x + r.w - 14.0f, r.y + r.h / 2 - 4.5f);
                lineTo(r.x + r.w - 9.5f, r.y + r.h / 2);
                lineTo(r.x + r.w - 14.0f, r.y + r.h / 2 + 4.5f);
                strokeColor(theme::textDim);
                strokeWidth(1.2f);
                lineCap(ROUND);
                lineJoin(ROUND);
                stroke();
                lineCap(BUTT);
                lineJoin(MITER);
            }
            if (!heading)
            {
                const float top = std::max(r.y, y), bottom = std::min(r.y + r.h, y + h);
                if (bottom > top)
                    fHits.push_back({ { r.x, top, r.w, bottom - top }, folder ? Hit::MenuFolder : Hit::MenuItem,
                                      static_cast<int>(i) });
            }
        }
        restore();
        return bounds;
    }

    void drawToast()
    {
        if (fToast.empty())
            return;
        if (std::chrono::steady_clock::now() - fToastAt > std::chrono::seconds(5))
        {
            fToast.clear();
            return;
        }
        fontSize(13.0f);
        Rectangle<float> bounds;
        const float w = textBounds(0, 0, fToast.c_str(), nullptr, bounds) + 32.0f;
        const float x = (fWidth - w) / 2, y = fHeight - kBottomBarHeight - 52.0f;
        fillRect(x, y, w, 32.0f, theme::popupFill, 8.0f);
        beginPath();
        roundedRect(x, y, w, 32.0f, 8.0f);
        strokeColor(theme::accent.withAlpha(0.6f));
        strokeWidth(1.0f);
        stroke();
        label(fWidth / 2, y + 16.0f, fToast.c_str(), theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // Save As: a name and a folder inside the preset folder, with the existing folders to pick from
    void drawSavePresetDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(660.0f, fWidth - 64.0f);
        const float h = std::min(420.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Dialog);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, "Save Preset", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        const float labelX = x + 24.0f, controlX = x + 110.0f, controlW = x + w - 24.0f - controlX;
        float row = y + 60.0f;
        label(labelX, row + 14.0f, "Name", theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawTextField({ controlX, row, controlW, 28.0f }, kFieldPresetName, "Preset name", true);
        row += 40.0f;
        label(labelX, row + 14.0f, "Folder", theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawTextField({ controlX, row, controlW, 28.0f }, kFieldPresetFolder, "Top level", true);
        row += 36.0f;
        label(controlX, row + 6.0f, "Pick a folder below, or type a new one (use / for subfolders: Clean/Fender)",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += 18.0f;

        // the existing folders
        constexpr float folderH = 26.0f;
        const float listY = row, listH = y + h - 92.0f - row;
        const float contentH = (fPresetFolders.size() + 1) * folderH;
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));
        const float folderW = contentH > listH ? controlW - kListBarWidth - 4.0f : controlW;
        if (contentH > listH)
            drawListScrollBar(controlX + folderW + 4.0f, listY, listH, contentH);
        save();
        scissor(controlX, listY, folderW, listH);
        for (int i = -1; i < static_cast<int>(fPresetFolders.size()); ++i)
        {
            const float ry = listY + (i + 1) * folderH - fListScroll;
            if (ry + folderH < listY || ry > listY + listH)
                continue;
            const std::string& folder = i < 0 ? std::string() : fPresetFolders[i];
            const Rect r { controlX, ry, folderW, folderH - 2.0f };
            const bool selected = fFields[kFieldPresetFolder] == folder;
            const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
            if (selected)
                fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(0.25f), 5.0f);
            else if (hover)
                fillRect(r.x, r.y, r.w, r.h, theme::tileHover, 5.0f);
            label(r.x + 10.0f, r.y + r.h / 2, i < 0 ? "Top level" : folder.c_str(), i < 0 ? theme::textDim : theme::text,
                  13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            const float top = std::max(r.y, listY), bottom = std::min(r.y + r.h, listY + listH);
            if (bottom > top)
                fHits.push_back({ { r.x, top, r.w, bottom - top }, Hit::SaveFolder, i });
        }
        restore();

        // what Save will do
        const std::optional<std::filesystem::path> path =
            presetPath(presetsDir(), fFields[kFieldPresetFolder], fFields[kFieldPresetName]);
        const bool noName = fFields[kFieldPresetName].find_first_not_of(" \t") == std::string::npos;
        std::string status;
        Color statusColor = theme::textDim;
        if (noName)
        {
            status = "Type a name for the preset.";
        }
        else if (!path)
        {
            status = "Names and folders can't contain < > : \" / \\ | ? * or end with a dot.";
            statusColor = theme::bad;
        }
        else if (fileExists(pathToUtf8(*path)))
        {
            status = "There's already a preset with this name here. Save replaces it.";
            statusColor = theme::warn;
        }
        else
        {
            status = "Saved presets don't include their NAM and IR files. Use Export to share one.";
        }
        label(x + 24.0f, y + h - 74.0f, status.c_str(), statusColor, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        const Rect saveButton { x + w - 24.0f - 96.0f, y + h - 52.0f, 96.0f, 30.0f };
        const Rect cancel { saveButton.x - 104.0f, saveButton.y, 96.0f, 30.0f };
        drawButton(cancel, "Cancel", false, theme::accent, Hit::DialogClose, 0);
        if (path)
            drawButton(saveButton, "Save", true, theme::accent, Hit::SaveConfirm, 0);
        else
            label(saveButton.x + saveButton.w / 2, saveButton.y + saveButton.h / 2, "Save", theme::textDim, 13.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // The user's images for the selected block (its kind's library), and its stock image: click one to show it on
    // the block; Delete asks again, and blocks that used the image show their stock one again
    void drawImagesDialog()
    {
        const BlockState* block = imagesBlock();
        const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
        if (type == nullptr)
        {
            closeDialog();
            return;
        }
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(760.0f, fWidth - 64.0f);
        const float h = std::min(540.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Dialog);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        const std::string title = std::string(imageKindName(fImageKind)) + " Images";
        label(x + 20.0f, y + 26.0f, title.c_str(), theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });
        const std::string folderText = pathToUtf8(userImagesDir(fImageKind));
        label(x + 20.0f, y + 48.0f, folderText.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        // a grid of tiles: the stock image first, then the library's
        constexpr float tileW = 120.0f, tileH = 188.0f, gap = 10.0f, thumbH = 140.0f;
        const float listX = x + 16.0f, listY = y + 66.0f, listH = h - 66.0f - 64.0f;
        float listW = w - 32.0f;
        const int count = static_cast<int>(fImageList.size()) + 1;
        const auto columnsFor = [&](float width) { return std::max(1, static_cast<int>((width + gap) / (tileW + gap))); };
        const auto heightFor = [&](int columns) { return ((count + columns - 1) / columns) * (tileH + gap) - gap; };
        int columns = columnsFor(listW);
        float contentH = heightFor(columns);
        if (contentH > listH)
        {
            listW -= kListBarWidth + 4.0f;
            columns = columnsFor(listW);
            contentH = heightFor(columns);
        }
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));
        if (contentH > listH)
            drawListScrollBar(listX + listW + 4.0f, listY, listH, contentH);

        save();
        scissor(listX, listY, listW, listH);
        for (int i = 0; i < count; ++i)
        {
            const int image = i - 1; // -1: the stock image
            const Rect r { listX + (i % columns) * (tileW + gap), listY + (i / columns) * (tileH + gap) - fListScroll,
                           tileW, tileH };
            if (r.y + r.h < listY || r.y > listY + listH)
                continue;
            const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
            surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Item, hover);
            const bool current = image < 0 ? userImage(*block) == nullptr : fImageList[image].id == block->image;
            if (current)
            {
                beginPath();
                roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 6.0f);
                strokeColor(theme::accent);
                strokeWidth(1.5f);
                stroke();
            }

            const Rect thumb { r.x + 8.0f, r.y + 8.0f, r.w - 16.0f, thumbH };
            std::string name;
            if (image < 0)
            {
                name = "Stock";
                if (NanoImage* img = stockImage(*block, true))
                    drawBlockImage(*img, type, -1, thumb.x, thumb.y, thumb.w, thumb.h, 3.0f, false);
                else
                    drawBlockArt(type, thumb.x, thumb.y + 10.0f, thumb.w, thumb.h - 20.0f, false, fileLookKey(*block));
            }
            else
            {
                name = fImageList[image].name;
                if (NanoImage* img = this->image(fImageList[image].path))
                    drawImageFit(*img, thumb.x, thumb.y, thumb.w, thumb.h, 3.0f);
                else
                    label(thumb.x + thumb.w / 2, thumb.y + thumb.h / 2, "Can't show it", theme::bad, 11.0f,
                          ALIGN_CENTER | ALIGN_MIDDLE);
            }
            fontSize(12.0f);
            name = fitText(name, r.w - 12.0f);
            label(r.x + r.w / 2, r.y + r.h - 22.0f, name.c_str(), current ? theme::accent : theme::text, 12.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);

            // only what's inside the list area is clickable
            const auto clipped = [&](const Rect& b) {
                const float top = std::max(b.y, listY), bottom = std::min(b.y + b.h, listY + listH);
                return Rect { b.x, top, b.w, std::max(0.0f, bottom - top) };
            };
            fHits.push_back({ clipped(r), Hit::ImageChoose, image });
            const bool armed = image >= 0 && fImageList[image].id == fImageDeleteArmed;
            if (image >= 0 && (hover || armed))
            {
                const Rect del { r.x + r.w - 70.0f, r.y + 6.0f, 64.0f, 24.0f };
                drawButton(del, armed ? "Delete?" : "Delete", armed, theme::bad, Hit::ImageDelete, image);
                fHits.back().rect = clipped(del);
            }
        }
        restore();
        if (fImageList.empty())
        {
            label(listX + tileW + gap + 8.0f, listY + 20.0f, "No images of your own here yet. Add one below.",
                  theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        }

        const Rect add { x + w - 24.0f - 120.0f, y + h - 48.0f, 120.0f, 30.0f };
        drawButton(add, "Add Image…", false, theme::accent, Hit::ImageAdd, 0);
        label(x + 20.0f, y + h - 40.0f, "Your images are shown as they are: no knobs, power light or footswitch on them.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(x + 20.0f, y + h - 24.0f, "Delete asks again. Blocks that used a deleted image show their stock image.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // The Cab Image dialog: the built-in cab images by size, in sections to open (bass: 1x10, 2x10, 4x10; guitar:
    // 1x12, 2x12, 4x12). Clicking one shows it for the IR (fCabHash) from then on; Reset to Default undoes that.
    // For a NAM pedal (PhotoKind::Pedal) it's the Pedal Image dialog: the generic pedal images, all in one grid; for a NAM
    // amp (PhotoKind::Amp) the Amp Image dialog, the amp heads likewise.
    void drawCabDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(760.0f, fWidth - 64.0f);
        const float h = std::min(580.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Dialog);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        const PhotoKind kind = fCabKind;
        const bool pedal = kind != PhotoKind::Cab; // one grid without size sections (pedals, amps)
        const bool amp = kind == PhotoKind::Amp;
        label(x + 20.0f, y + 26.0f, amp ? "Amp Image" : pedal ? "Pedal Image" : "Cab Image", theme::text, 16.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });
        fontSize(11.0f);
        const std::string file = fitText(fCabPath.empty() ? std::string(pedal ? "NAM" : "IR") : fileStem(fCabPath),
                                         w - 80.0f);
        label(x + 20.0f, y + 48.0f, file.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        const std::vector<std::string>& ids = photoIds(kind);
        const std::string stem = fCabPath.empty() ? std::string() : fileStem(fCabPath);
        const std::string current = amp ? ampImageFor(fCabHash, fCabPath)
                                  : pedal ? pedalImageFor(fCabHash, fCabPath) : cabImageFor(fCabHash, fCabPath);
        const std::string fallback = amp ? defaultAmpImage(cabKey(fCabHash, fCabPath), stem, ids)
                                   : pedal ? defaultPedalImage(cabKey(fCabHash, fCabPath), ids)
                                           : defaultCabImage(cabKey(fCabHash, fCabPath), stem, ids);
        // the sections: a cab size each, or for pedals and amps one without a heading
        std::vector<std::vector<std::string>> sizes;
        if (pedal)
            sizes.push_back(cabImagesOfSize(ids, amp ? "amp" : "pedal"));
        else
            for (const char* size : kCabSizes)
                sizes.push_back(cabImagesOfSize(ids, size));
        const auto open = [&](size_t i) { return pedal || fCabOpen[i]; };
        // opened for a block: the user's own images first, then an Add Image… tile (owner, 2026-10-31: one chooser)
        const BlockState* block = imagesBlock();
        const bool own = block != nullptr && !block->image.empty()
                      && std::any_of(fImageList.begin(), fImageList.end(),
                                     [&](const UserImage& image) { return image.id == block->image; });
        const size_t ownTiles = block != nullptr ? fImageList.size() + 1 : 0;

        constexpr float groupH = 30.0f, sectionH = 40.0f, gap = 10.0f;
        const float tile = amp ? 160.0f : 124.0f; // the heads are wide
        const float tileH = amp ? tile * 0.62f : tile;
        const float listX = x + 16.0f, listY = y + 66.0f, listH = h - 66.0f - 64.0f;
        float listW = w - 32.0f;
        const auto columnsFor = [&](float width) { return std::max(1, static_cast<int>((width + gap) / (tile + gap))); };
        const auto ownHeight = [&](int columns) {
            return ownTiles == 0 ? 0.0f
                                 : groupH + ((ownTiles + columns - 1) / columns) * (tileH + gap) + gap
                                       + (pedal ? groupH : 0.0f);
        };
        const auto contentFor = [&](int columns) {
            float total = (pedal ? 0.0f : 2 * groupH) + ownHeight(columns);
            for (size_t i = 0; i < sizes.size(); ++i)
            {
                total += pedal ? 0.0f : sectionH;
                if (open(i) && !sizes[i].empty())
                    total += ((sizes[i].size() + columns - 1) / columns) * ((amp ? tile * 0.62f : tile) + gap) + gap;
            }
            return total;
        };
        int columns = columnsFor(listW);
        float contentH = contentFor(columns);
        if (contentH > listH)
        {
            listW -= kListBarWidth + 4.0f;
            columns = columnsFor(listW);
            contentH = contentFor(columns);
        }
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));
        if (contentH > listH)
            drawListScrollBar(listX + listW + 4.0f, listY, listH, contentH);

        // only what's inside the list area is clickable
        const auto clipped = [&](const Rect& b) {
            const float top = std::max(b.y, listY), bottom = std::min(b.y + b.h, listY + listH);
            return Rect { b.x, top, b.w, std::max(0.0f, bottom - top) };
        };
        const auto visible = [&](float top, float height) { return top + height >= listY && top <= listY + listH; };

        save();
        scissor(listX, listY, listW, listH);
        float ry = listY - fListScroll;
        if (ownTiles > 0)
        {
            if (visible(ry, groupH))
                label(listX + 4.0f, ry + groupH - 10.0f, "Your images", theme::accent, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            ry += groupH;
            for (size_t k = 0; k < ownTiles; ++k)
            {
                const Rect r { listX + (k % columns) * (tile + gap), ry + (k / columns) * (tileH + gap), tile, tileH };
                if (!visible(r.y, r.h))
                    continue;
                const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
                surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Item, hover);
                if (k == fImageList.size())
                {
                    // the last tile adds one: a plus and its name
                    const float cx = r.x + r.w / 2, cy = r.y + r.h / 2 - 8.0f, arm = 12.0f;
                    beginPath();
                    moveTo(cx - arm, cy);
                    lineTo(cx + arm, cy);
                    moveTo(cx, cy - arm);
                    lineTo(cx, cy + arm);
                    strokeColor(hover ? theme::accent : theme::textDim);
                    strokeWidth(1.5f);
                    stroke();
                    label(cx, cy + arm + 14.0f, "Add Image…", hover ? theme::accent : theme::text, 12.0f,
                          ALIGN_CENTER | ALIGN_MIDDLE);
                    fHits.push_back({ clipped(r), Hit::ImageAdd, 0 });
                    continue;
                }
                const UserImage& image = fImageList[k];
                if (NanoImage* img = this->image(image.path))
                    drawImageFit(*img, r.x + 6.0f, r.y + 6.0f, r.w - 12.0f, r.h - 26.0f, 3.0f);
                else
                    label(r.x + r.w / 2, r.y + r.h / 2 - 10.0f, "Can't show it", theme::bad, 11.0f,
                          ALIGN_CENTER | ALIGN_MIDDLE);
                const bool current = image.id == block->image;
                fontSize(11.0f);
                const std::string name = fitText(image.name, r.w - 12.0f);
                label(r.x + r.w / 2, r.y + r.h - 11.0f, name.c_str(), current ? theme::accent : theme::textDim, 11.0f,
                      ALIGN_CENTER | ALIGN_MIDDLE);
                if (current)
                {
                    beginPath();
                    roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 6.0f);
                    strokeColor(theme::accent);
                    strokeWidth(2.0f);
                    stroke();
                }
                fHits.push_back({ clipped(r), Hit::ImageChoose, static_cast<int>(k) });
                const bool armed = image.id == fImageDeleteArmed;
                if (hover || armed)
                {
                    const Rect del { r.x + r.w - 70.0f, r.y + 6.0f, 64.0f, 24.0f };
                    drawButton(del, armed ? "Delete?" : "Delete", armed, theme::bad, Hit::ImageDelete,
                               static_cast<int>(k));
                    fHits.back().rect = clipped(del);
                }
            }
            ry += ((ownTiles + columns - 1) / columns) * (tileH + gap) + gap;
            if (pedal)
            {
                // the built-in ones' heading (the cabs have theirs: Bass and Guitar)
                if (visible(ry, groupH))
                    label(listX + 4.0f, ry + groupH - 10.0f, amp ? "Built-in heads" : "Built-in pedals", theme::accent,
                          12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
                ry += groupH;
            }
        }
        for (size_t i = 0; i < sizes.size(); ++i)
        {
            if (!pedal && (i == 0 || i == kBassCabSizes))
            {
                if (visible(ry, groupH))
                    label(listX + 4.0f, ry + groupH - 10.0f, i == 0 ? "Bass" : "Guitar", theme::accent, 12.0f,
                          ALIGN_LEFT | ALIGN_MIDDLE);
                ry += groupH;
            }

            // the section's heading: an arrow, the size, and how many cabs
            const Rect heading { listX, ry, listW, sectionH - 6.0f };
            if (!pedal && visible(heading.y, heading.h))
            {
                const bool hover = heading.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
                surface(heading.x, heading.y, heading.w, heading.h, 6.0f, Surface::Item, hover);
                const float ax = heading.x + 18.0f, ay = heading.y + heading.h / 2;
                beginPath();
                if (fCabOpen[i])
                {
                    moveTo(ax - 5.0f, ay - 3.0f);
                    lineTo(ax + 5.0f, ay - 3.0f);
                    lineTo(ax, ay + 4.0f);
                }
                else
                {
                    moveTo(ax - 3.0f, ay - 5.0f);
                    lineTo(ax + 4.0f, ay);
                    lineTo(ax - 3.0f, ay + 5.0f);
                }
                closePath();
                fillColor(hover ? theme::accent : theme::textDim);
                fill();
                const std::string name = std::string(kCabSizes[i]) + "\"";
                const bool holdsCurrent = !own && cabImageSize(current) == kCabSizes[i];
                label(heading.x + 34.0f, ay, name.c_str(), holdsCurrent ? theme::accent : theme::text, 14.0f,
                      ALIGN_LEFT | ALIGN_MIDDLE);
                char count[32];
                std::snprintf(count, sizeof(count), "%zu cab%s", sizes[i].size(), sizes[i].size() == 1 ? "" : "s");
                label(heading.x + heading.w - 14.0f, ay, count, theme::textDim, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
                fHits.push_back({ clipped(heading), Hit::CabSection, static_cast<int>(i) });
            }
            if (!pedal)
                ry += sectionH;
            if (!open(i) || sizes[i].empty())
                continue;

            // its cabs: the IR's current one outlined, the default one labelled
            for (size_t k = 0; k < sizes[i].size(); ++k)
            {
                const Rect r { listX + (k % columns) * (tile + gap), ry + (k / columns) * (tileH + gap), tile, tileH };
                if (!visible(r.y, r.h))
                    continue;
                const std::string& id = sizes[i][k];
                const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
                surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Item, hover);
                if (NanoImage* img = amp ? ampImage(id, true) : pedal ? pedalImage(id, true) : cabImage(id, true))
                    drawImageFit(*img, r.x + 6.0f, r.y + 6.0f, r.w - 12.0f, r.h - 12.0f, 3.0f);
                if (id == current && !own)
                {
                    beginPath();
                    roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 6.0f);
                    strokeColor(theme::accent);
                    strokeWidth(2.0f);
                    stroke();
                }
                if (id == fallback)
                {
                    const Rect pill { r.x + r.w / 2 - 30.0f, r.y + r.h - 26.0f, 60.0f, 18.0f };
                    beginPath();
                    roundedRect(pill.x, pill.y, pill.w, pill.h, 9.0f);
                    fillColor(theme::popupFill);
                    fill();
                    label(pill.x + pill.w / 2, pill.y + pill.h / 2, "Default", theme::textDim, 10.0f,
                          ALIGN_CENTER | ALIGN_MIDDLE);
                }
                const int index = static_cast<int>(std::find(ids.begin(), ids.end(), id) - ids.begin());
                fHits.push_back({ clipped(r), Hit::CabChoose, index });
            }
            ry += ((sizes[i].size() + columns - 1) / columns) * ((amp ? tile * 0.62f : tile) + gap) + gap;
        }
        restore();

        const bool picked = (amp ? fSettings.ampImages : pedal ? fSettings.pedalImages : fSettings.cabImages)
                                .count(cabKey(fCabHash, fCabPath)) != 0;
        const Rect reset { x + w - 24.0f - 150.0f, y + h - 48.0f, 150.0f, 30.0f };
        if (picked || own)
            drawButton(reset, "Reset to Default", false, theme::accent, Hit::CabChoose, -1);
        label(x + 20.0f, y + h - 40.0f,
              own ? "This block shows one of your images, as it is: no knobs, power light or footswitch on it."
              : amp ? (picked ? "You picked this model's amp head. It follows the file into every chain and preset."
                            : "This model shows its default head, picked from the file's content and name.")
              : pedal ? (picked ? "You picked this capture's pedal. It follows the file into every chain and preset."
                              : "This capture shows its default pedal, picked from the file's content.")
                    : (picked ? "You picked this IR's cab. It follows the file into every chain and preset."
                              : "This IR shows its default cab, picked from the file's content."),
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(x + 20.0f, y + h - 24.0f,
              amp ? "Click a head to show it for this model from now on."
              : pedal ? "Click a pedal to show it for this capture from now on."
                    : "Click a cab to show it for this IR from now on.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    // The saved presets, by folder: click one to load it; Default marks the one new plugin instances start with
    void drawPresetsDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(700.0f, fWidth - 64.0f);
        const float h = std::min(480.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Dialog);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, "Presets", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });
        const std::string folderText = pathToUtf8(presetsDir());
        label(x + 20.0f, y + 48.0f, folderText.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        fPresetRows.clear();
        std::string folder;
        for (const PresetEntry& entry : fPresetList)
        {
            if (entry.folder != folder)
            {
                folder = entry.folder;
                fPresetRows.push_back({ folder, {}, {} });
            }
            fPresetRows.push_back({ entry.folder, entry.path, entry.name });
        }

        const float listX = x + 16.0f, listY = y + 66.0f, listH = h - 66.0f - 40.0f;
        float listW = w - 32.0f;
        if (fPresetRows.empty())
        {
            label(listX + 4.0f, listY + 16.0f, "No saved presets yet. Choose Save in the preset menu to keep a sound here.",
                  theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        }

        constexpr float rowH = 38.0f, headingH = 28.0f;
        float contentH = 0.0f;
        for (const PresetRow& row : fPresetRows)
            contentH += row.path.empty() ? headingH : rowH;
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));
        if (contentH > listH)
        {
            listW -= kListBarWidth + 4.0f;
            drawListScrollBar(listX + listW + 4.0f, listY, listH, contentH);
        }

        save();
        scissor(listX, listY, listW, listH);
        float ry = listY - fListScroll;
        for (int i = 0; i < static_cast<int>(fPresetRows.size()) && ry < listY + listH; ++i)
        {
            const PresetRow& row = fPresetRows[i];
            const float y0 = ry;
            ry += row.path.empty() ? headingH : rowH;
            if (ry < listY)
                continue;
            if (row.path.empty())
            {
                label(listX + 4.0f, y0 + headingH - 9.0f, row.folder.c_str(), theme::accent, 11.0f,
                      ALIGN_LEFT | ALIGN_MIDDLE);
                continue;
            }
            const Rect r { listX, y0, listW, rowH - 4.0f };
            const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
            surface(r.x, r.y, r.w, r.h, 6.0f, Surface::Tile, hover);
            const bool current = row.path == fChain.preset;
            label(r.x + 14.0f, r.y + r.h / 2, row.name.c_str(), current ? theme::accent : theme::text, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);

            const bool isDefault = row.path == fSettings.defaultPreset;
            const bool armed = row.path == fPresetDeleteArmed;
            const Rect def { r.x + r.w - 92.0f, r.y + 5.0f, 84.0f, r.h - 10.0f };
            const Rect del { def.x - 92.0f, def.y, 84.0f, def.h };
            if (isDefault || hover)
                drawButton(def, "Default", isDefault, theme::accent, Hit::PresetDefault, i);
            if (armed || hover)
                drawButton(del, armed ? "Delete?" : "Delete", armed, theme::bad, Hit::PresetDelete, i);
            const float top = std::max(r.y, listY), bottom = std::min(r.y + r.h, listY + listH);
            // a button's hit area, cut to the list
            const auto buttonHit = [&](const Rect& b, Hit kind) {
                fHits.push_back({ { b.x, std::max(b.y, listY), b.w,
                                    std::min(b.y + b.h, listY + listH) - std::max(b.y, listY) },
                                  kind, i });
            };
            if (bottom > top)
            {
                fHits.push_back({ { r.x, top, r.w - 192.0f, bottom - top }, Hit::PresetRow, i });
                if (isDefault || hover)
                    buttonHit(def, Hit::PresetDefault);
                if (armed || hover)
                    buttonHit(del, Hit::PresetDelete);
            }
        }
        restore();

        label(x + 20.0f, y + h - 20.0f,
              "Click a preset to load it. The Default preset loads when you add Xythos to a track. "
              "Delete asks again before it deletes.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawSettingsDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(800.0f, fWidth - 64.0f);
        const float h = std::min(536.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Popup);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, "Settings", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        const float labelX = x + 24.0f;
        const float controlX = x + 220.0f;
        const float controlW = x + w - 24.0f - controlX;
        float row = y + 64.0f;
        constexpr float rowH = 44.0f;
        auto rowLabel = [&](const char* text) {
            label(labelX, row + 14.0f, text, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        };

        rowLabel("NAM folder");
        drawTextField({ controlX, row, controlW - 96.0f, 28.0f }, kFieldNamDir, "Not set");
        drawButton({ controlX + controlW - 88.0f, row, 88.0f, 28.0f }, "Choose…", false, theme::accent,
                   Hit::SettingsChoose, kFieldNamDir);
        row += rowH;

        rowLabel("IR folder");
        drawTextField({ controlX, row, controlW - 96.0f, 28.0f }, kFieldIrDir, "Not set");
        drawButton({ controlX + controlW - 88.0f, row, 88.0f, 28.0f }, "Choose…", false, theme::accent,
                   Hit::SettingsChoose, kFieldIrDir);
        row += rowH;

        rowLabel("Library");
        drawButton({ controlX, row, 88.0f, 28.0f }, "Rescan", false, theme::accent, Hit::SettingsRescan, 0);
        char status[96];
        if (fScanning)
            std::snprintf(status, sizeof(status), "Scanning…");
        else
            std::snprintf(status, sizeof(status), "%zu NAM model%s, %zu IR%s, %zu favorite%s", fNamLibrary.size(),
                          fNamLibrary.size() == 1 ? "" : "s", fIrLibrary.size(), fIrLibrary.size() == 1 ? "" : "s",
                          fSettings.favorites.size(), fSettings.favorites.size() == 1 ? "" : "s");
        label(controlX + 100.0f, row + 14.0f, status, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Default A2 size");
        drawButton({ controlX, row, 72.0f, 28.0f }, "Full", !fSettings.a2Lite, theme::accent, Hit::SettingsA2, 0);
        drawButton({ controlX + 78.0f, row, 72.0f, 28.0f }, "Lite", fSettings.a2Lite, theme::accent,
                   Hit::SettingsA2, 1);
        label(controlX + 166.0f, row + 14.0f, "for new NAM blocks; each block can override it", theme::textDim,
              11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Normalize new blocks");
        drawButton({ controlX, row, 72.0f, 28.0f }, fSettings.normalize ? "On" : "Off", fSettings.normalize,
                   theme::accent, Hit::SettingsNormalize, 0);
        label(controlX + 88.0f, row + 14.0f, "loudness normalization for new NAM and IR blocks", theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Multi-core splits");
        drawButton({ controlX, row, 72.0f, 28.0f }, fSettings.parallel ? "On" : "Off", fSettings.parallel,
                   theme::accent, Hit::SettingsParallel, 0);
        label(controlX + 88.0f, row + 14.0f, "run the second branch of a split on another core", theme::textDim,
              11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Check for updates");
        drawButton({ controlX, row, 72.0f, 28.0f }, fSettings.checkUpdates ? "On" : "Off", fSettings.checkUpdates,
                   theme::accent, Hit::SettingsUpdates, 0);
        label(controlX + 88.0f, row + 14.0f,
              "once a day, from GitHub; a notice in the top bar when there's a new version", theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Accent colour");
        const auto custom = ink::parseHex(fSettings.accentColor);
        float swatchX = controlX + 14.0f;
        const auto drawSwatch = [&](const Color& color, bool selected, Hit kind, int index) {
            const float cy = row + 14.0f;
            beginPath();
            circle(swatchX, cy, 11.0f);
            NanoVG::fillColor(color); // a plain swatch, even for the one that is the highlight
            fill();
            if (selected)
            {
                beginPath();
                circle(swatchX, cy, 14.5f);
                NanoVG::strokeColor(theme::text);
                strokeWidth(1.5f);
                stroke();
            }
            fHits.push_back({ { swatchX - 15.0f, cy - 15.0f, 30.0f, 30.0f }, kind, index });
            swatchX += 34.0f;
        };
        for (int k = 0; k < theme::accentCount; ++k)
        {
            const int i = theme::accentOrder[k];
            drawSwatch(theme::accents[i], !custom && i == fSettings.accent, Hit::SettingsAccent, i);
        }
        if (custom)
            drawSwatch(toColor(*custom), true, Hit::AccentCustom, 0);
        drawButton({ swatchX - 8.0f, row, 84.0f, 28.0f }, "Custom…", false, theme::accent, Hit::AccentCustom, 0);
        row += rowH;

        rowLabel("Text & outlines");
        static constexpr const char* modes[] = { "Solid", "Smooth", "Splotchy" };
        for (int m = 0; m < 3; ++m)
            drawButton({ controlX + m * 80.0f, row, 74.0f, 28.0f }, modes[m], fSettings.inkMode == m, theme::accent,
                       Hit::InkMode, m);
        label(controlX + 252.0f, row + 14.0f,
              fSettings.inkMode == 0 ? "the highlight is the accent colour above"
                                     : "outlines, fills and the logo use the colours below",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Colours");
        const auto& palettes = ink::presets();
        bool customInk = fSettings.inkMode != 0;
        constexpr float chipStep = 86.0f;
        for (size_t p = 0; p < palettes.size(); ++p)
        {
            const bool on = fSettings.inkMode != 0 && fSettings.inkColors == hexList(palettes[p].colors);
            customInk = customInk && !on;
            const std::string name(palettes[p].name);
            const Rect chip { controlX + p * chipStep, row, chipStep - 6.0f, 28.0f };
            drawButton(chip, name.c_str(), on, theme::accent, Hit::InkPreset, static_cast<int>(p));
            // the set's colours along the bottom edge
            const auto& colors = palettes[p].colors;
            const float segment = (chip.w - 12.0f) / static_cast<float>(colors.size());
            for (size_t c = 0; c < colors.size(); ++c)
                fillRect(chip.x + 6.0f + segment * static_cast<float>(c), chip.y + chip.h - 5.0f, segment, 2.0f,
                         toColor(colors[c]));
        }
        drawButton({ controlX + palettes.size() * chipStep, row, 84.0f, 28.0f }, "Custom…", customInk, theme::accent,
                   Hit::InkCustom, 0);
        row += rowH;

        label(x + 24.0f, y + h - 18.0f, shortenHomePath(pathToUtf8(Settings::defaultPath())).c_str(), theme::textDim, 10.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
    }

    static Color toColor(ink::Rgb color) { return Color(color.r, color.g, color.b); }

    // The colour picker (owner, 2026-10-30): a colour wheel (hue around it, saturation out from the middle) with a
    // brightness slider, the colour's name and its hex value (selectable, copyable, typed), and the user's saved
    // colours. It edits the accent (Solid) or one of the gradient's 2-3 colours. Colours too dark to read on the window
    // are lifted toward white (ink::lighten()).
    void openPicker(bool solid)
    {
        fPickSolid = solid;
        openDialog(Dialog::InkPicker);
        pickSlot(0);
    }

    int pickerCount() const { return fPickSolid ? 1 : static_cast<int>(fSettings.inkColors.size()); }

    std::string pickerHex(int slot) const
    {
        if (fPickSolid)
            return ink::toHex(ink::Rgb { static_cast<uint8_t>(std::lround(theme::accent.red * 255.0f)),
                                         static_cast<uint8_t>(std::lround(theme::accent.green * 255.0f)),
                                         static_cast<uint8_t>(std::lround(theme::accent.blue * 255.0f)) });
        if (slot < 0 || slot >= static_cast<int>(fSettings.inkColors.size()))
            return "#ffffff";
        return fSettings.inkColors[slot];
    }

    // The picker edits another of the colours: the wheel moves to it
    void pickSlot(int slot)
    {
        fInkSlot = std::clamp(slot, 0, std::max(pickerCount() - 1, 0));
        fPickHsv = ink::toHsv(ink::parseHex(pickerHex(fInkSlot)).value_or(ink::Rgb { 255, 255, 255 }));
        if (fFocusedField == kFieldInkHex)
            blurField(false);
        fFields[kFieldInkHex] = pickerHex(fInkSlot);
        repaint();
    }

    // Sets the colour being edited (lifted if too dark); saved to the settings when `save`, else only shown (while
    // dragging: saved on release)
    void setPickerColor(ink::Rgb color, bool save)
    {
        const std::string hex = ink::toHex(ink::lighten(color));
        if (fPickSolid)
        {
            fSettings.accentColor = hex;
            if (save)
                updateSettings([hex](Settings& settings) { settings.accentColor = hex; });
        }
        else if (fInkSlot < static_cast<int>(fSettings.inkColors.size()))
        {
            fSettings.inkColors[fInkSlot] = hex;
            if (save)
                updateSettings([colors = fSettings.inkColors](Settings& settings) { settings.inkColors = colors; });
        }
        applySettings();
        if (fFocusedField != kFieldInkHex)
            fFields[kFieldInkHex] = hex;
        repaint();
    }

    // A drag on the wheel or the brightness slider
    void dragPicker(float x, float y)
    {
        if (fInkDrag == 0 && fPickWheelRadius > 0.0f)
        {
            const float dx = x - fPickWheelX, dy = y - fPickWheelY;
            double hue = std::atan2(dy, dx) * 180.0 / M_PI;
            if (hue < 0.0)
                hue += 360.0;
            fPickHsv.h = hue;
            fPickHsv.s = std::clamp(std::hypot(dx, dy) / fPickWheelRadius, 0.0f, 1.0f);
        }
        else if (fInkDrag == 1 && fPickSlider.h > 0.0f)
            fPickHsv.v = std::clamp(1.0f - (y - fPickSlider.y) / fPickSlider.h, 0.0f, 1.0f);
        if (fFocusedField == kFieldInkHex)
            blurField(false);
        setPickerColor(ink::fromHsv(fPickHsv), false);
    }

    // The hex field changed: a complete value moves the wheel to it (saved when the field is left)
    void hexTyped()
    {
        if (const auto color = ink::parseHex(fFields[kFieldInkHex]))
        {
            fPickHsv = ink::toHsv(*color);
            setPickerColor(*color, false);
        }
    }

    void drawInkPicker()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(560.0f, fWidth - 32.0f);
        const float h = std::min(500.0f, fHeight - 32.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        surface(x, y, w, h, 12.0f, Surface::Popup);
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, fPickSolid ? "Accent colour" : "Gradient colours", theme::text, 16.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        // the gradient's colours: how many, and which one the wheel edits
        float top = y + 56.0f;
        if (!fPickSolid)
        {
            const int count = static_cast<int>(fSettings.inkColors.size());
            drawButton({ x + 20.0f, top, 96.0f, 28.0f }, "2 colours", count == 2, theme::accent, Hit::InkCount, 2);
            drawButton({ x + 122.0f, top, 96.0f, 28.0f }, "3 colours", count == 3, theme::accent, Hit::InkCount, 3);
            fInkSlot = std::clamp(fInkSlot, 0, std::max(count - 1, 0));
            for (int s = 0; s < count; ++s)
            {
                const Rect slot { x + 240.0f + s * 64.0f, top, 56.0f, 28.0f };
                fillRect(slot.x, slot.y, slot.w, slot.h,
                         toColor(ink::parseHex(fSettings.inkColors[s]).value_or(ink::Rgb { 255, 255, 255 })), 6.0f);
                if (s == fInkSlot)
                {
                    beginPath();
                    roundedRect(slot.x - 3.0f, slot.y - 3.0f, slot.w + 6.0f, slot.h + 6.0f, 8.0f);
                    NanoVG::strokeColor(theme::text);
                    strokeWidth(1.5f);
                    stroke();
                }
                fHits.push_back({ slot, Hit::InkSlot, s });
            }
            top += 44.0f;
        }

        // the wheel: hue around it, saturation from grey in the middle to full at the rim, at the slider's brightness
        constexpr float radius = 104.0f;
        const float cx = x + 20.0f + radius, cy = top + 8.0f + radius;
        fPickWheelX = cx;
        fPickWheelY = cy;
        fPickWheelRadius = radius;
        const float v = static_cast<float>(fPickHsv.v);
        constexpr int sectors = 48;
        constexpr float tau = 2.0f * static_cast<float>(M_PI);
        for (int i = 0; i < sectors; ++i)
        {
            const float a0 = tau * i / sectors, a1 = tau * (i + 1) / sectors;
            const auto hueColor = [v](float a) {
                return toColor(ink::fromHsv({ a * 180.0 / M_PI, 1.0, v }));
            };
            beginPath();
            moveTo(cx, cy);
            arc(cx, cy, radius, a0 - 0.01f, a1 + 0.01f, CW);
            closePath();
            fillPaint(linearGradient(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                                     cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, hueColor(a0),
                                     hueColor(a1)));
            fill();
        }
        beginPath();
        circle(cx, cy, radius);
        fillPaint(radialGradient(cx, cy, 0.0f, radius, Color(v, v, v, 1.0f), Color(v, v, v, 0.0f)));
        fill();
        fHits.push_back({ { cx - radius, cy - radius, radius * 2, radius * 2 }, Hit::InkWheel, 0 });
        {
            const float a = static_cast<float>(fPickHsv.h * M_PI / 180.0);
            const float r = static_cast<float>(fPickHsv.s) * radius;
            const float mx = cx + std::cos(a) * r, my = cy + std::sin(a) * r;
            beginPath();
            circle(mx, my, 7.0f);
            NanoVG::strokeColor(Color(0.0f, 0.0f, 0.0f, 0.7f));
            strokeWidth(3.0f);
            stroke();
            beginPath();
            circle(mx, my, 7.0f);
            NanoVG::strokeColor(Color(1.0f, 1.0f, 1.0f));
            strokeWidth(1.5f);
            stroke();
        }

        // brightness: from the wheel's colour at full brightness down to black
        fPickSlider = { cx + radius + 20.0f, cy - radius, 18.0f, radius * 2 };
        beginPath();
        roundedRect(fPickSlider.x, fPickSlider.y, fPickSlider.w, fPickSlider.h, 6.0f);
        fillPaint(linearGradient(0.0f, fPickSlider.y, 0.0f, fPickSlider.y + fPickSlider.h,
                                 toColor(ink::fromHsv({ fPickHsv.h, fPickHsv.s, 1.0 })), Color(0, 0, 0)));
        fill();
        beginPath();
        roundedRect(fPickSlider.x + 0.5f, fPickSlider.y + 0.5f, fPickSlider.w - 1.0f, fPickSlider.h - 1.0f, 6.0f);
        NanoVG::strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        {
            const float hy = fPickSlider.y + (1.0f - v) * fPickSlider.h;
            beginPath();
            roundedRect(fPickSlider.x - 3.0f, hy - 3.0f, fPickSlider.w + 6.0f, 6.0f, 3.0f);
            NanoVG::fillColor(theme::text);
            fill();
        }
        fHits.push_back({ { fPickSlider.x - 6.0f, fPickSlider.y - 6.0f, fPickSlider.w + 12.0f, fPickSlider.h + 12.0f },
                          Hit::InkBrightness, 0 });

        // the colour: a swatch, its name, its hex value (select it, copy it, or type one)
        const ink::Rgb current = ink::parseHex(pickerHex(fInkSlot)).value_or(ink::Rgb { 255, 255, 255 });
        const float rx = fPickSlider.x + fPickSlider.w + 24.0f, rw = x + w - 20.0f - rx;
        fillRect(rx, cy - radius, rw, 64.0f, toColor(current), 8.0f);
        label(rx, cy - radius + 88.0f, ink::colorName(current).c_str(), theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawTextField({ rx, cy - radius + 112.0f, rw - 70.0f, 28.0f }, kFieldInkHex, "#rrggbb");
        drawButton({ rx + rw - 64.0f, cy - radius + 112.0f, 64.0f, 28.0f }, "Copy", false, theme::accent, Hit::InkCopy,
                   0);
        const bool saved = std::find(fSettings.savedColors.begin(), fSettings.savedColors.end(), ink::toHex(current))
                           != fSettings.savedColors.end();
        drawButton({ rx, cy - radius + 152.0f, rw, 28.0f }, saved ? "Saved" : "Save colour", saved, theme::accent,
                   Hit::InkSave, 0);

        // the saved colours: click one to use it, its × forgets it
        float sy = cy + radius + 28.0f;
        label(x + 20.0f, sy, fSettings.savedColors.empty() ? "Saved colours: none yet (Save colour keeps one here)"
                                                           : "Saved colours",
              theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        sy += 24.0f;
        constexpr float dot = 24.0f, step = 32.0f;
        const int perRow = std::max(1, static_cast<int>((w - 40.0f) / step));
        for (size_t i = 0; i < fSettings.savedColors.size(); ++i)
        {
            const float sx = x + 20.0f + (i % perRow) * step + dot / 2;
            const float syc = sy + (i / perRow) * step + dot / 2;
            if (syc > y + h - 56.0f)
                break;
            const ink::Rgb color = ink::parseHex(fSettings.savedColors[i]).value_or(ink::Rgb { 255, 255, 255 });
            beginPath();
            circle(sx, syc, dot / 2);
            NanoVG::fillColor(toColor(color));
            fill();
            if (ink::toHex(color) == ink::toHex(current))
            {
                beginPath();
                circle(sx, syc, dot / 2 + 3.0f);
                NanoVG::strokeColor(theme::text);
                strokeWidth(1.5f);
                stroke();
            }
            const Rect r { sx - dot / 2, syc - dot / 2, dot, dot };
            fHits.push_back({ r, Hit::InkSaved, static_cast<int>(i) });
            if (r.contains(fMouseX, fMouseY))
            {
                const float bx = r.x + r.w - 2.0f, by = r.y + 2.0f;
                beginPath();
                circle(bx, by, 6.5f);
                NanoVG::fillColor(Color(0.0f, 0.0f, 0.0f, 0.9f));
                fill();
                label(bx, by, "×", theme::text, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
                fHits.push_back({ { bx - 7.0f, by - 7.0f, 14.0f, 14.0f }, Hit::InkForget, static_cast<int>(i) });
            }
        }

        label(x + 20.0f, y + h - 30.0f, "Colours too dark to read on the window are lifted toward white.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawButton({ x + w - 108.0f, y + h - 44.0f, 88.0f, 28.0f }, "Done", false, theme::accent, Hit::DialogClose, 0);
    }

    void drawMergePanel(float top)
    {
        label(32.0f, top + 22.0f, "Merge", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(32.0f, top + 42.0f,
              "Mixes branch A (top row) and branch B (bottom row) back together, in stereo (in Mono mode too)",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        float x = 32.0f + kKnobRadius + 8.0f;
        const float cy = top + 42.0f + 24.0f + kKnobRadius + 14.0f;
        static constexpr std::pair<uint32_t, const char*> knobs[] = {
            { kParamMergeBalance, "A / B" }, { kParamMergeLevelA, "Level A" }, { kParamMergeLevelB, "Level B" },
            { kParamMergePanA, "Pan A" },    { kParamMergePanB, "Pan B" },     { kParamMergeAlign, "Align" },
        };
        for (const auto& [index, name] : knobs)
        {
            drawKnob(x, cy, index, name);
            x += 100.0f;
        }
        drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, "Invert A", fParams[kParamMergeInvertA] >= 0.5f,
                   theme::accent, Hit::Toggle, kParamMergeInvertA);
        x += 100.0f;
        drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, "Invert B", fParams[kParamMergeInvertB] >= 0.5f,
                   theme::accent, Hit::Toggle, kParamMergeInvertB);

        // the phase check's warning, under the controls: click it to flip the switch it names
        if (mergedPolarity() == Polarity::Opposite)
        {
            const uint32_t fix = phaseFixParam();
            const bool on = fParams[fix] >= 0.5f;
            const std::string text = std::string("The branches are out of phase, so their low end cancels. Click to turn ")
                                   + (on ? "off " : "on ") + (fix == kParamMergeInvertA ? "Invert A." : "Invert B.");
            const float wy = cy + kKnobRadius + 64.0f;
            drawWarningSign(39.0f, wy, 7.0f);
            label(52.0f, wy, text.c_str(), theme::warn, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(12.0f);
            Rectangle<float> bounds;
            const float w = textBounds(0, 0, text.c_str(), nullptr, bounds);
            fHits.push_back({ { 30.0f, wy - 10.0f, w + 30.0f, 20.0f }, Hit::PhaseFix, static_cast<int>(fix) });
        }
    }

    // The global controls in one row (owner, 2026-10-31: idea A of the mockups): Mono/Stereo, the input meter, the five
    // knobs with their names and values beside them, the output meter, Limiter (and its gain reduction)
    void drawBottomBar()
    {
        const float top = fHeight - kBottomBarHeight;
        surface(0, top, fWidth, kBottomBarHeight, 0.0f, Surface::Bar);

        const float cy = top + kBottomBarHeight / 2;
        const bool stereo = fParams[kParamInputMode] >= 0.5f;
        drawButton({ 20.0f, cy - 14.0f, 80.0f, 28.0f }, stereo ? "Stereo" : "Mono", stereo, theme::accent,
                   Hit::Toggle, kParamInputMode);
        // narrower meters and knob slots in a narrow window (900 px at the least)
        const float meterW = std::clamp(70.0f + (fWidth - 900.0f) * 0.4f, 70.0f, 110.0f), limiterW = 80.0f;
        const float knobW = fWidth < 1000.0f ? 94.0f : 104.0f;
        drawMeterH(116.0f, cy, meterW, kParamInPeakL, kParamInPeakR, fInClipLatched, Hit::InClip, "IN");

        // the knobs share what's between the meters, evenly
        const float limiterX = fWidth - 20.0f - limiterW;
        const float outX = limiterX - 16.0f - meterW;
        const float knobsLeft = 116.0f + meterW + 16.0f, knobsRight = outX - 16.0f;
        const float gap = std::max(4.0f, (knobsRight - knobsLeft - 5 * knobW) / 5.0f);
        const uint32_t knobs[] = { kParamInput, kParamBass, kParamMiddle, kParamTreble, kParamOutput };
        for (int i = 0; i < 5; ++i)
            drawSideKnob(knobsLeft + gap / 2 + i * (knobW + gap), cy, knobs[i], globalParamSpec(knobs[i]).name);

        drawMeterH(outX, cy, meterW, kParamOutPeakL, kParamOutPeakR, fOutClipLatched, Hit::OutClip, "OUT");

        const bool limiter = fParams[kParamLimiter] >= 0.5f;
        drawButton({ limiterX, cy - 14.0f, limiterW, 28.0f }, "Limiter", limiter, theme::accent, Hit::Toggle,
                   kParamLimiter);
        if (limiter && fParams[kParamLimiterReduction] > 0.5f)
        {
            char gr[24];
            std::snprintf(gr, sizeof(gr), "-%.1f dB", fParams[kParamLimiterReduction]);
            label(limiterX + limiterW / 2, cy + 22.0f, gr, theme::warn, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------

    float fParams[kParamCount] {};
    ChainState fChain;
    int fSelected = -1;
    bool fMergeSelected = false;
    Section fAddSection = Section::Pre; // where the block picked in Add Block (or the file browser) goes
    int fAddPosition = kMaxBlocks;      // ... its position in that section (kMaxBlocks: the end)
    int fReplaceSlot = -1;              // ... or the slot of the block it replaces
    ChainLayout fLayout;

    std::vector<HitArea> fHits;
    float fWidth = DISTRHO_UI_DEFAULT_WIDTH;
    float fHeight = DISTRHO_UI_DEFAULT_HEIGHT;
    float fMouseX = -1.0f, fMouseY = -1.0f;
    float fScrollX = 0.0f;
    float fMaxScrollX = 0.0f;   // how far the chain scrolls (0: it fits)
    Rect fScrollTrack;          // the scroll bar, as last drawn
    float fScrollThumbW = 0.0f;
    bool fScrollDragging = false; // dragging the scroll bar's thumb
    bool fPanning = false;        // middle-dragging the chain
    float fScrollGrabX = 0.0f, fScrollGrabValue = 0.0f;
    Rect fListTrack;              // the open dialog's list scroll bar, as last drawn (fListScroll scrolls it)
    float fListThumbH = 0.0f, fListMaxScroll = 0.0f;
    bool fListScrollDragging = false;
    float fListGrabY = 0.0f, fListGrabValue = 0.0f;

    // Ink: the window's paint (its texture is made when the mode or the colours change), the frame's transform, and
    // the colour picker's state
    std::unique_ptr<NanoImage> fInkImage;
    std::vector<uint8_t> fInkPixels; // its pixels (RGBA), for highlightAt()
    std::vector<std::string> fInkKey; // the mode and colours fInkImage was made from
    float fFrameXform[6] = {};        // the frame's transform (logical to pixels), before anything is drawn
    Color fFillColor;                 // the colour of the last fillColor() call, and whether it was ink
    bool fFillIsInk = false;
    int fInkSlot = 0;                 // the colour the picker edits
    int fInkDrag = -1;                // what of the picker is being dragged: 0 the wheel, 1 the brightness slider
    bool fPickSolid = false;          // the picker edits the accent (Solid), else the gradient's colours
    ink::Hsv fPickHsv;                // where the wheel and slider are (kept apart from the colour, which is lifted)
    float fPickWheelX = 0.0f, fPickWheelY = 0.0f, fPickWheelRadius = 0.0f; // the wheel, as drawn
    Rect fPickSlider;                 // the brightness slider, as drawn
    int fDragParam = -1;
    int fMomentaryParam = -1; // a momentary button being held
    std::chrono::steady_clock::time_point fMomentaryPressedAt;
    bool fMomentaryReleasePending = false;
    static constexpr std::chrono::milliseconds kMomentaryMinHold { 80 };
    float fDragStartX = 0.0f;
    float fDragStartY = 0.0f;
    float fDragStartValue = 0.0f;
    uint fLastClickTime = 0;
    int fScrollKnob = -1; // the knob last turned with the wheel, and when
    std::chrono::steady_clock::time_point fScrollKnobAt;

    int fPressTile = -1;
    float fPressX = 0.0f, fPressY = 0.0f;
    bool fDraggingTile = false;

    static constexpr uint32_t kNoParam = 0xffffffffu;

    Dialog fDialog = Dialog::None;
    std::vector<MenuEntry> fMenu;
    float fMenuX = 0.0f, fMenuY = 0.0f, fMenuW = 300.0f;
    std::vector<std::pair<std::string, float>> fMenuOpen; // the open submenus: their folder, and its row's top
    std::vector<float> fMenuScrolls;                      // each submenu's scroll (the menu itself uses fListScroll)
    std::vector<Rect> fMenuPanels;                        // the menu's panels, as last drawn
    bool fPresetMenu = false; // the open menu is the preset menu
    uint32_t fMenuParam = kNoParam; // the parameter a context menu is for
    int fMenuTile = -1;

    UndoStack fUndo;
    ChainState fSavedChain; // the chain as last saved or opened, for the modified mark
    int fLocateIndex = -1;
    int fLearnParam = -1;
    float fLearnCcCount = 0.0f;
    std::string fToast;
    std::chrono::steady_clock::time_point fToastAt;
    FontId fUiFont = -1;      // Jost Light
    FontId fUiFontSmall = -1; // Jost Regular, for small text
    Category fDialogCategory = Category::Nam;
    FolderPicker fFolderPicker;
    Field fFolderField = kFieldNamDir;
    float fListScroll = 0.0f;
    bool fFavoritesOnly = false;
    std::string fLibraryDir[2]; // the folder the NAM [0] and IR [1] lists show, relative to the library root
    LibraryHistory fLibraryHistory[2]; // where they were before, for back and forward
    std::vector<AddRow> fRows; // the Add Block list rows drawn last, for hit indices
    BrowseTarget fBrowseTarget = BrowseTarget::AddBlock;

    std::string fFields[kFieldCount];
    int fFocusedField = -1;
    std::string fFieldBeforeEdit;
    TextEdit fEdit; // the focused field's cursor and selection
    bool fSelectingText = false; // dragging a selection in the focused field
    struct FieldLayout {
        std::vector<size_t> offsets; // character boundaries...
        std::vector<float> xs;       // ...and where they were drawn
    };
    FieldLayout fFieldLayout[kFieldCount];
    float fFieldScroll[kFieldCount] = {};

    Settings fSettings;
    int64_t fSettingsTime = 0;
    std::chrono::steady_clock::time_point fSettingsCheckedAt = std::chrono::steady_clock::now();

    LibraryScanner fScanner;

    UpdateChecker fUpdateChecker { std::make_unique<HttpGet>() };
    UpdateCache fUpdateCache;      // the last check's result (shared by every instance)
    int64_t fUpdateCacheTime = 0;
    std::vector<LibraryEntry> fNamLibrary, fIrLibrary;
    mutable std::unordered_map<std::string, NamFileInfo> fNamInfo; // by path: namFileInfo()
    bool fScanning = false;
    bool fScanned = false;

    std::map<std::string, std::unique_ptr<NanoImage>> fImages;        // by image path
    // The built-in block images (BlockImages.cpp), by typeId: the full ones (the block panel's) are freed once a few
    // others have been drawn since, so browsing every block doesn't keep every image in memory; thumbnails stay.
    struct CachedImage {
        std::unique_ptr<NanoImage> image;
        uint64_t lastFrame = 0; // the frame it was last drawn in
    };
    static constexpr size_t kMaxFullImages = 4;
    std::map<std::string, CachedImage> fBuiltinImages;
    std::map<std::string, std::unique_ptr<NanoImage>> fBuiltinThumbs;
    uint64_t fFrame = 0;
    NanoImage fKnobImage, fKnobPointer, fKnobSheen; // the pedal faces' knob, its pointer and its lighting (KnobImages.cpp)
    NanoImage fRimKnob, fRimKnobPointer, fRimKnobColour; // the knob with a glowing rim, tinted per pedal (KnobImages.cpp)
    bool fKnobImagesTried = false;
    static constexpr int kLedLayers = 7;
    NanoImage fLedImages[kLedLayers]; // the pedal faces' LED, in layers (LedImages.cpp)
    bool fLedImagesTried = false;

    // metronome
    bool fMetroPanel = false;
    Rect fSliderRect; // the slider being dragged (w == 0: a knob is)
    TapTempo fTapTempo;                          // the metronome's
    std::array<TapTempo, kMaxBlocks> fLooperTaps; // each looper's Tap, by slot
    std::chrono::steady_clock::time_point fTapAt, fBeatAt;

    // tuner display
    std::vector<float> fTunerHistory;
    float fTunerFrequency = 0.0f;
    float fTunerCents = 0.0f;
    float fNeedleCents = 0.0f;
    float fStrobePhase[3] {};
    int fTunerNote = -1; // MIDI note
    std::chrono::steady_clock::time_point fTunerSeenAt;
    std::chrono::steady_clock::time_point fLastIdle = std::chrono::steady_clock::now();

    // hover help: the control under the mouse (kind -1: none with help) and since when
    static constexpr std::chrono::milliseconds kHelpDelay { 600 };
    int fHelpKind = -1;
    uintptr_t fVsyncDrawable = 0;
    uintptr_t fCursorDrawable = 0; // the window given its arrow cursor (defineArrowCursor())

    // frame-rate readout (XYTHOS_SHOW_FPS)
    const bool fShowFps = [] {
        const char* value = std::getenv("XYTHOS_SHOW_FPS");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    int fIdleCount = 0;
    int fMotionCount = 0; // mouse motion events
    int fFpsFrames = 0;
    float fFpsMaxGap = 0.0f;
    float fFpsDrawMs = 0.0f;
    std::chrono::steady_clock::time_point fFpsSince = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point fFpsLastFrame = std::chrono::steady_clock::now();
    char fFpsText[128] = "measuring…";
    int fHelpIndex = -1;
    std::chrono::steady_clock::time_point fHelpSince;
    bool fHelpShown = false;

    bool fInClipLatched = false;
    bool fOutClipLatched = false;

    uint32_t fInstanceId = 0;
    bool fGotChainState = false;
    bool fDefaultPending = false; // the default preset may still be loaded (a new instance)
    NanoImage fBackground;
    bool fBackgroundTried = false;
    NanoImage fLogoMark, fLogoWordmark;
    bool fLogoTried = false;
    bool fDimControls = false; // drawing a bypassed block's controls (dimmed())
    std::vector<MigratedSlot> fMigratedSlots; // the host's chain had blocks without their later controls (slots; kMigrations)
    std::chrono::steady_clock::time_point fOpenedAt = std::chrono::steady_clock::now();
    std::vector<std::string> fPresetFolders;  // Save Preset dialog
    std::vector<std::string> fMenuPresets;    // the preset menu's presets, by MenuEntry::arg
    float fPresetButtonX = 140.0f;
    std::vector<PresetEntry> fPresetList;     // Presets dialog
    std::string fPresetDeleteArmed;           // the preset whose Delete was clicked once (Presets dialog)
    struct PresetRow {
        std::string folder; // a folder heading when path is empty
        std::string path;
        std::string name;
    };
    std::vector<PresetRow> fPresetRows;       // drawn last, for hit indices
    // Images dialog: the user's images for the block at fImagesBlock (its kind's library)
    int fImagesBlock = -1;
    ImageKind fImageKind = ImageKind::Pedal;
    std::vector<UserImage> fImageList;
    std::string fImageDeleteArmed;            // the image whose Delete was clicked once
    std::string fImageBrowseDir;              // where the last added image was
    // Cab Image dialog: the IR whose cab image is picked, the dialog to go back to, and which sizes are shown
    std::string fCabHash;
    std::string fCabPath;
    Dialog fCabReturn = Dialog::None;
    float fCabReturnScroll = 0.0f;
    std::array<bool, kCabSizes.size()> fCabOpen {};
    // Add Block's live preview (previewBlock())
    bool fPreviewing = false;      // a picked block is in the chain, to be kept or taken out
    ChainState fPreviewBase;       // the chain before it
    int fPreviewBaseSelected = -1;
    Section fPreviewSection = Section::Pre;
    int fPreviewPosition = kMaxBlocks;
    int fPreviewReplaceSlot = -1;
    std::string fPickKey;          // the picked row or item (pickKey()); empty: none
    std::string fPickLabel;        // its name, for the dialog's footer
    bool fScrollToPick = false;    // scroll the list to show it (picked with the arrow keys)
    int fItemColumns = 1;          // the block types' grid, as drawn
    std::vector<Field> fDrawnFields;
    // the phase check (pollPhaseCheck()): what it should be for, what it was last started for, its result and what that
    // was for
    PhaseWorker fPhaseWorker;
    std::string fPhaseWanted, fPhaseStarted, fPhaseKey;
    PhaseResult fPhase;
    std::chrono::steady_clock::time_point fPhaseChangedAt; // the text fields drawn this frame, in order (Tab moves through them)
    PhotoKind fCabKind = PhotoKind::Cab; // the dialog picks a NAM pedal's generic pedal image or an amp's head instead

    std::chrono::steady_clock::time_point fChainChangedAt = std::chrono::steady_clock::now();
    bool fStatusPending = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XythosUI)
};

UI* createUI()
{
    return new XythosUI();
}

END_NAMESPACE_DISTRHO
