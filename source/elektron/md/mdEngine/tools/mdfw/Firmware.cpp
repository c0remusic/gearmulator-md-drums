#include "Firmware.h"
#include <cstring>
#include <fstream>

namespace md::fw {

std::vector<uint8_t> readFile(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) throw FirmwareError("cannot open " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

FlashImage parseSysex(const std::vector<uint8_t>& syx, uint8_t device)
{
    const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x3C, device, 0x00, 0x7E};
    FlashImage img;
    bool haveBase = false;
    uint32_t expect = 0;
    size_t i = 0;
    while (i < syx.size()) {
        if (syx[i] != 0xF0) { ++i; continue; }
        size_t j = i;
        while (j < syx.size() && syx[j] != 0xF7) ++j;
        if (j >= syx.size()) break;
        const size_t len = j - i + 1;   // incl. F7
        const uint8_t* m = &syx[i];
        if (len > 9 + 6 && std::memcmp(m, hdr, sizeof(hdr)) == 0) {
            uint32_t addr = 0;
            for (int k = 0; k < 6; ++k) addr = (addr << 4) | (m[9 + k] & 0xF);
            const size_t payloadLen = len - 16;   // between a0 and F7
            if (payloadLen % 3 != 0) throw FirmwareError("sysex data block with bad length");
            if (!haveBase) { img.base = addr; expect = addr; haveBase = true; }
            if (addr != expect) throw FirmwareError("non-contiguous sysex data block");
            for (size_t k = 15; k + 2 < len - 1; k += 3) {
                const uint32_t w = (uint32_t(m[k]) << 14) | (uint32_t(m[k + 1]) << 7) | m[k + 2];
                img.bytes.push_back(uint8_t(w >> 8));
                img.bytes.push_back(uint8_t(w & 0xFF));
            }
            expect += uint32_t(payloadLen / 3 * 2);
        } else {
            img.trailers.emplace_back(m, m + len);
        }
        i = j + 1;
    }
    if (!haveBase) throw FirmwareError("no OS data messages for this device found");
    return img;
}

namespace {
struct Bits {
    const uint8_t* d; size_t n, i = 0; uint32_t tag = 0; int left = 0; bool err = false;
    Bits(const uint8_t* data, size_t size) : d(data), n(size) {}
    int bit() {
        if (left == 0) {
            if (i >= n) { err = true; return 0; }
            tag = d[i++]; left = 8;
        }
        --left; return (tag >> left) & 1;
    }
    uint32_t byte() { if (i >= n) { err = true; return 0; } return d[i++]; }
    uint32_t gamma() {
        uint32_t v = 1;
        while (!err) { v = (v << 1) | uint32_t(bit()); if (bit()) return v; }
        return v;
    }
};
}

std::vector<uint8_t> aplibDepack(const uint8_t* stream, size_t size)
{
    constexpr uint32_t kOffsetBias = 767, kFar = 3328;
    Bits b(stream, size);
    std::vector<uint8_t> out;
    out.reserve(size * 3);
    uint32_t last = 1;
    while (!b.err) {
        if (b.bit()) {
            if (b.i >= b.n) break;
            out.push_back(uint8_t(b.byte()));
            continue;
        }
        const uint32_t g = b.gamma();
        uint32_t off;
        if (g == 2) off = last;
        else {
            const uint32_t raw = (g << 8) + b.byte();
            if (raw == kOffsetBias) break;
            off = raw - kOffsetBias; last = off;
        }
        const uint32_t sl = (uint32_t(b.bit()) << 1) | uint32_t(b.bit());
        uint32_t L = sl ? sl : b.gamma() + 2;
        if (off > kFar) ++L;
        if (b.err) break;
        if (off == 0 || off > out.size()) throw FirmwareError("aplib: bad match offset");
        for (uint32_t k = 0; k <= L; ++k) out.push_back(out[out.size() - off]);
    }
    return out;
}

static uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

Container parseContainer(const FlashImage& img)
{
    Container c;
    const auto& d = img.bytes;
    size_t p = 0; int n = 0;
    while (p + 8 <= d.size()) {
        const uint32_t size = be32(&d[p]), chk = be32(&d[p + 4]);
        const bool plausible = size >= 16 && size <= d.size() - p - 8;
        uint32_t sum = 0;
        if (plausible) for (size_t k = 0; k < size; ++k) sum += d[p + 8 + k];
        const bool sumOk = plausible && sum == chk;
        if (!(plausible && (sumOk || n == 0))) {   // community builds patch section 0 in place without fixing its sum
            // Version tag between sections: 8 ASCII bytes on the Monomachine ("   1.32B"), 4 on the Machinedrum ("163 ").
            auto ascii = [&](size_t n) {
                if (p + n > d.size()) return false;
                for (size_t k = 0; k < n; ++k) if (d[p + k] < 32 || d[p + k] >= 127) return false;
                return true;
            };
            if (ascii(8)) { c.version.assign(reinterpret_cast<const char*>(&d[p]), 8); p += 8; continue; }
            if (ascii(4)) { c.version.assign(reinterpret_cast<const char*>(&d[p]), 4); p += 4; continue; }
            break;
        }
        Section s;
        s.index = n++; s.flashAddr = img.base + uint32_t(p); s.streamSize = size; s.streamSum = chk; s.sumOk = sumOk;
        s.data = aplibDepack(&d[p + 8], size);
        c.sections.push_back(std::move(s));
        p += 8 + size;
    }
    return c;
}

DspImage parseDspRecords(const std::vector<uint8_t>& sec)
{
    DspImage img;
    const size_t nw = sec.size() / 3;
    auto w = [&](size_t k) { return uint32_t(sec[3 * k]) | (uint32_t(sec[3 * k + 1]) << 8) | (uint32_t(sec[3 * k + 2]) << 16); };
    size_t p = 0;
    while (p < nw) {
        const uint32_t t = w(p);
        if (t == 3) { if (p + 1 >= nw) break; img.startAddr = w(p + 1); p += 2; continue; }
        if (t == 4) { if (p + 1 >= nw) break; img.marker4.push_back(w(p + 1)); p += 2; continue; }
        if (t > 2 || p + 3 > nw) throw FirmwareError("dsp records: bad record header");
        DspRecord r; r.space = Space(t); r.addr = w(p + 1);
        const uint32_t cnt = w(p + 2);
        if (p + 3 + cnt > nw) throw FirmwareError("dsp records: truncated record");
        r.words.resize(cnt);
        for (uint32_t k = 0; k < cnt; ++k) r.words[k] = w(p + 3 + k);
        img.records.push_back(std::move(r));
        p += 3 + cnt;
    }
    return img;
}

Firmware loadFirmware(const std::filesystem::path& syxPath)
{
    const auto c = parseContainer(parseSysex(readFile(syxPath)));
    if (c.sections.size() < 3) throw FirmwareError("OS file has " + std::to_string(c.sections.size()) + " sections, expected >= 3 (not a Machinedrum OS?)");
    for (int k = 1; k <= 2; ++k) if (!c.sections[k].sumOk) throw FirmwareError("section " + std::to_string(k) + " checksum mismatch");
    Firmware f;
    f.dspA = parseDspRecords(c.sections[1].data);
    f.dspB = parseDspRecords(c.sections[2].data);
    f.version = c.version;
    f.source = syxPath;
    return f;
}

FlashOs loadFirmwareFromFlash(const std::vector<uint8_t>& flash)
{
    // The OS container sits at $4000; the sample and Kit data from $100000 on is not part of it.
    constexpr size_t kOsStart = 0x4000, kOsEnd = 0x100000;
    if (flash.size() < kOsEnd) throw FirmwareError("flash image too small for a Machinedrum OS");
    FlashImage img;
    img.base = kOsStart;
    img.bytes.assign(flash.begin() + kOsStart, flash.begin() + kOsEnd);
    auto c = parseContainer(img);
    if (c.sections.size() < 3) throw FirmwareError("flash holds " + std::to_string(c.sections.size()) + " OS sections, expected >= 3");
    for (int k = 1; k <= 2; ++k) if (!c.sections[k].sumOk) throw FirmwareError("section " + std::to_string(k) + " checksum mismatch");
    FlashOs os;
    os.firmware.dspA = parseDspRecords(c.sections[1].data);
    os.firmware.dspB = parseDspRecords(c.sections[2].data);
    os.firmware.version = c.version;
    os.osImage = std::move(c.sections[0].data);
    return os;
}

RomBank loadRomBankFromFlash(const std::vector<uint8_t>& flash)
{
    // The bank (flash 0x100000): 0xabcd, the sample count, then per sample 12 big-endian words (tag 0x1a00 + slot,
    // bits, period in ns low/high, length low/high, loop start low/high, loop end low/high, loop type with 0x7f for
    // none, gain) and its 16-bit samples. The OS's conversion table (0x7a0000): 65536 big-endian words, the 12-bit code
    // of each 16-bit sample read as unsigned.
    constexpr size_t kBank = 0x100000, kTable = 0x7a0000, kTableEnd = kTable + 0x20000;
    RomBank bank;
    if (flash.size() < kTableEnd) return bank;
    const auto w16 = [&](const size_t o) -> uint32_t { return o + 1 < flash.size() ? (uint32_t(flash[o]) << 8) | flash[o + 1] : 0xffff; };
    const auto lohi = [&](const size_t o) { return w16(o) | (w16(o + 2) << 16); };
    if (w16(kBank) != 0xabcd) return bank;
    const uint32_t count = w16(kBank + 2);
    if (count == 0 || count > RomBank::SlotCount) return bank;

    struct Sample { size_t data; uint32_t period, length, loopStart, loopEnd, loopType, gain; };
    std::vector<std::optional<Sample>> slots(RomBank::SlotCount);
    size_t pos = kBank + 4;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t tag = w16(pos), slot = tag & 0xff;
        const Sample s{pos + 24, lohi(pos + 4), lohi(pos + 8), lohi(pos + 12), lohi(pos + 16), w16(pos + 20), w16(pos + 22)};
        if ((tag & 0xff00) != 0x1a00 || slot >= RomBank::SlotCount || s.data + 2 * size_t(s.length) > kTable) return {};
        slots[slot] = s;
        pos = s.data + 2 * size_t(s.length);
    }

    // The OS reads a sample from its own copy of it in 64 KiB flash blocks, where erased flash (0xffff) follows the
    // sample's end. A gain at or below $74ff scales the samples first, as the OS does.
    const auto value = [&](const Sample& _s, const uint32_t _k) -> uint16_t {
        const uint32_t raw = _k < _s.length ? w16(_s.data + 2 * size_t(_k)) : 0xffff;
        if (_s.gain > 0x74ff) return uint16_t(raw);
        const int32_t v = int16_t(uint16_t(raw));
        if (_s.gain <= 0x1fff) return uint16_t(v * 4);
        const int32_t factor = 0x3fffffff / int32_t(_s.gain);
        return uint16_t(int32_t(uint32_t(v) * uint32_t(factor)) >> 15);
    };
    const auto code = [&](const uint16_t _v) { return w16(kTable + 2 * size_t(_v)) & 0xfff; };

    uint32_t offset = 0;
    for (uint32_t slot = 0; slot < RomBank::SlotCount; ++slot) {
        RomBank::Entry entry{RomBank::DirectoryAddr + 4 * (slot < 32 ? slot : slot + 16), {0, 0, 0xffffff, 0}};
        const auto& s = slots[slot];
        // The OS leaves a slot empty when its rate is below 2 kHz (a period above 500 us).
        if (s && s->period != 0 && s->period <= 0x7a120) {
            uint32_t used = s->length, loop = 0xffffff;
            // A loop shorter than 151 samples, or outside the sample, is ignored; a loop ends the sample at its end.
            if (s->loopType != 0x7f && s->loopEnd > s->loopStart && s->loopEnd <= s->length && s->loopEnd - s->loopStart > 0x96) {
                loop = s->loopStart;
                used = s->loopEnd + 1;
            }
            entry.words[0] = (RomBank::DataAddr + offset) & 0xffffff;
            entry.words[1] = used & 0xffffff;
            entry.words[2] = loop & 0xffffff;
            entry.words[3] = (uint32_t(0x16250000 / int32_t(s->period)) << 4) & 0xffffff;
            // The OS's chunks, one per flash block: 0x7ff4 samples, then 0x7ffe; the last takes one sample more than
            // remain; each packs floor(n / 2) words.
            uint32_t remaining = used, k = 0, chunk = 0x7ff4;
            while (remaining > 0) {
                const uint32_t n = chunk <= remaining ? chunk : remaining + 1;
                for (uint32_t j = 0; j + 1 < n; j += 2)
                    bank.data.push_back((code(value(*s, k + j)) << 12) | code(value(*s, k + j + 1)));
                offset += n / 2;
                k += n;
                remaining = n >= remaining ? 0 : remaining - n;
                chunk = 0x7ffe;
            }
            ++bank.samples;
        }
        bank.directory.push_back(entry);
    }
    return bank;
}

std::vector<uint8_t> loadPatchImageFromFlash(const std::vector<uint8_t>& flash, const bool uw)
{
    constexpr size_t kOsStart = 0x4000, kOsEnd = 0x100000, kImageSize = 0x80000;
    if (flash.size() < kOsEnd) return {};
    FlashImage img;
    img.base = kOsStart;
    img.bytes.assign(flash.begin() + kOsStart, flash.begin() + kOsEnd);
    auto c = parseContainer(img);
    const size_t index = uw ? 4 : 3;
    if (c.sections.size() <= index || !c.sections[index].sumOk || c.sections[index].data.size() != kImageSize) return {};
    return std::move(c.sections[index].data);
}

} // namespace md::fw
