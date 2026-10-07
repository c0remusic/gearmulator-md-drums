// Adapted from shnolk/monomodule src/core/firmware (AGPL-3.0) for the Machinedrum SPS-1 UW OS.
// Elektron OS (.syx) file decoder: sysex transport -> flash container -> compressed sections
// (an aPLib-style LZ scheme) -> DSP56303 memory records. The OS file is supplied by the user at run time.
#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace md::fw {

struct FirmwareError : std::runtime_error { using std::runtime_error::runtime_error; };

struct FlashImage {
    uint32_t base = 0;                 // flash address of bytes[0] (0x4000 for OS files)
    std::vector<uint8_t> bytes;
    std::vector<std::vector<uint8_t>> trailers;  // non-data messages (end marker, version)
};

// F0 00 20 3C dd 00 7E ck ck a5..a0 <32 triples> F7 ; triple [c a b] -> w = c<<14 | a<<7 | b (BE 16-bit)
// dd = device: 02 Machinedrum, 03 Monomachine.
constexpr uint8_t kDeviceMachinedrum = 0x02, kDeviceMonomachine = 0x03;
FlashImage parseSysex(const std::vector<uint8_t>& syx, uint8_t device = kDeviceMachinedrum);

// aPLib-style LZ77 depacker (see aplib.py for the bit-level spec). Depacks stream[0..size).
std::vector<uint8_t> aplibDepack(const uint8_t* stream, size_t size);

struct Section {
    int index = -1;
    uint32_t flashAddr = 0, streamSize = 0, streamSum = 0;
    bool sumOk = false;
    std::vector<uint8_t> data;       // decompressed
};

struct Container {
    // Monomachine: 0 = main-processor OS, 1 = DSP kernel A, 2 = kernel B, 3 = DSP payload, 4 = RAM image.
    // Machinedrum OS 1.63: 0 = main-processor OS, 1 and 2 = one complete DSP program each (see docs/FIRMWARE.md).
    std::vector<Section> sections;
    std::string version;             // 8 ASCII bytes between section 3 and 4, e.g. "   1.32B"
};
Container parseContainer(const FlashImage& img);

enum class Space : uint8_t { P = 0, X = 1, Y = 2 };

struct DspRecord {
    Space space;
    uint32_t addr;
    std::vector<uint32_t> words;     // 24-bit
};
struct DspImage {
    std::vector<DspRecord> records;
    std::optional<uint32_t> startAddr;   // from the [3][addr] marker
    std::vector<uint32_t> marker4;       // arguments of [4][arg] markers (Machinedrum; meaning unknown, 0 in OS 1.63)
};
// 24-bit little-endian words: [space][addr][count] + count words; 3 = start marker, 4 = Machinedrum marker (2 words each)
DspImage parseDspRecords(const std::vector<uint8_t>& section);

struct Firmware {
    DspImage dspA, dspB;             // sections 1 and 2
    std::string version;
    std::filesystem::path source;
};
std::vector<uint8_t> readFile(const std::filesystem::path& p);
Firmware loadFirmware(const std::filesystem::path& syxPath);

// md-drums: the same OS read from a full 8 MB Machinedrum UW flash image, which holds the OS file's decoded image
// from $4000 on. Returns the DSP programs and the decompressed ColdFire OS (section 0), what EngineT takes.
struct FlashOs {
    Firmware firmware;
    std::vector<uint8_t> osImage;
};
FlashOs loadFirmwareFromFlash(const std::vector<uint8_t>& flash);

// md-drums: the UW sample bank as OS 1.63's boot leaves it in the voice DSP (DSP2), redone from the flash image alone
// (the OS's ROM sample transfer, $20d20e): a directory at P:$147e00 of 4-word entries (start, length, loop start or
// $ffffff, rate as a fraction of 44.1 kHz with 2^18 = 1), ROM slots 0-31 at entries 0-31 and 32-47 at entries 48-63
// (entries 32-47 belong to the RAM machines), and the samples, two 12-bit codes per word, first in the high half, from
// P:$150000. The bank's 16-bit samples become 12-bit codes through the OS's own table in the flash. Write both into
// the voice DSP (VoiceEngine::writeP) after constructing it; nothing is filled if the image holds no bank.
struct RomBank {
    static constexpr uint32_t DirectoryAddr = 0x147e00, DataAddr = 0x150000, SlotCount = 48;
    struct Entry { uint32_t addr; uint32_t words[4]; };
    std::vector<Entry> directory;   // one per ROM slot, filled or empty
    std::vector<uint32_t> data;     // from DataAddr
    uint32_t samples = 0;           // ROM slots that hold a sample
};
RomBank loadRomBankFromFlash(const std::vector<uint8_t>& flash);

} // namespace md::fw
