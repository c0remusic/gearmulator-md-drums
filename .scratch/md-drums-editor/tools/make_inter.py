"""Turn Inter 4.1's static TTFs into the four faces MD Drums' skin bundles.

    python -I make_inter.py <Inter-4.1/extras/ttf> <output folder>

RmlUi's font engine (FreeType, no HarfBuzz) applies no OpenType feature and kerns only from a legacy 'kern' table.
So, for each of Regular, Medium, SemiBold and Bold:
- the 'tnum' substitutions of the digits are frozen into the cmap: 0-9 are the tabular ones everywhere, as the
  mockup's font-variant-numeric: tabular-nums gives its values;
- the GPOS pair kerning of printable ASCII is copied into a 'kern' table, as Chrome kerns the mockup;
- the font keeps Latin-1 and the few signs the editor shows, and drops its layout tables.
The weights stay in OS/2 usWeightClass (400, 500, 600, 700), which RmlUi reads, under the family "Inter".
"""

import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0

WEIGHTS = ["Regular", "Medium", "SemiBold", "Bold"]
EXTRA = [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2039, 0x203A, 0x2190, 0x2191, 0x2192,
         0x2193, 0x2212, 0x25B4, 0x25B6, 0x25BE, 0x25C0]
UNICODES = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + EXTRA


def lookups(gsub_or_gpos, feature_tag):
    """The feature's lookups in order, each as the list of its subtables."""
    table = gsub_or_gpos.table
    indices = set()
    for record in table.FeatureList.FeatureRecord:
        if record.FeatureTag == feature_tag:
            indices.update(record.Feature.LookupListIndex)
    for index in sorted(indices):
        # Extension lookups (GSUB 7, GPOS 9) wrap the real subtable
        yield [getattr(sub, "ExtSubTable", sub) for sub in table.LookupList.Lookup[index].SubTable]


def freeze_tnum(font):
    mapping = {}
    for subtables in lookups(font["GSUB"], "tnum"):
        for sub in subtables:
            if hasattr(sub, "mapping"):
                mapping.update(sub.mapping)
    # Digits only: Inter's tnum also widens the space, the hyphen and the punctuation, which every label uses
    frozen = 0
    for table in font["cmap"].tables:
        for code, glyph in list(table.cmap.items()):
            if 0x30 <= code <= 0x39 and glyph in mapping:
                table.cmap[code] = mapping[glyph]
                frozen += 1
    return frozen


def pair_value(sub, left, right):
    if sub.Format == 1:
        if left not in sub.Coverage.glyphs:
            return None
        pair_set = sub.PairSet[sub.Coverage.glyphs.index(left)]
        for record in pair_set.PairValueRecord:
            if record.SecondGlyph == right:
                value = record.Value1
                return getattr(value, "XAdvance", 0) if value else 0
        return None
    if sub.Format == 2:
        if left not in sub.Coverage.glyphs:
            return None
        class1 = sub.ClassDef1.classDefs.get(left, 0)
        class2 = sub.ClassDef2.classDefs.get(right, 0)
        value = sub.Class1Record[class1].Class2Record[class2].Value1
        return getattr(value, "XAdvance", 0) if value else 0
    return None


def legacy_kern(font):
    cmap = font.getBestCmap()
    glyphs = [cmap[code] for code in range(0x20, 0x7F) if code in cmap]
    kern_lookups = [[sub for sub in subtables if hasattr(sub, "PairSet") or hasattr(sub, "Class1Record")]
                    for subtables in lookups(font["GPOS"], "kern")]
    pairs = {}
    for left in glyphs:
        for right in glyphs:
            # Every lookup adds its adjustment; within one, the first subtable that covers the pair applies
            total = 0
            for subtables in kern_lookups:
                for sub in subtables:
                    value = pair_value(sub, left, right)
                    if value is not None:
                        total += value
                        break
            if total:
                pairs[(left, right)] = total
    kern = newTable("kern")
    kern.version = 0
    table = KernTable_format_0()
    table.version = 0
    table.coverage = 1
    table.format = 0
    table.kernTable = pairs
    kern.kernTables = [table]
    font["kern"] = kern
    return len(pairs)


def make(source, target, weight):
    font = TTFont(source)
    frozen = freeze_tnum(font)
    kerned = legacy_kern(font)

    options = subset.Options()
    options.layout_features = []
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "STAT"]
    options.legacy_kern = True
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    options.notdef_outline = True
    options.hinting = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=UNICODES)
    subsetter.subset(font)

    # One family, the weight in OS/2: RmlUi tells the faces apart by usWeightClass
    name = font["name"]
    style = "Regular" if weight == "Regular" else weight
    name.setName("Inter", 1, 3, 1, 0x409)
    name.setName(style, 2, 3, 1, 0x409)
    name.setName("Inter " + style, 4, 3, 1, 0x409)
    name.setName("Inter-" + style, 6, 3, 1, 0x409)
    font.save(target)
    return frozen, kerned, font["OS/2"].usWeightClass, len(font.getGlyphOrder())


def main():
    source, output = sys.argv[1], sys.argv[2]
    os.makedirs(output, exist_ok=True)
    for weight in WEIGHTS:
        target = os.path.join(output, "Inter-%s.ttf" % weight)
        frozen, kerned, weight_class, glyph_count = make(os.path.join(source, "Inter-%s.ttf" % weight), target, weight)
        print("%s: weight %d, %d glyphs, %d digits frozen tabular, %d kerning pairs, %d bytes"
              % (os.path.basename(target), weight_class, glyph_count, frozen, kerned, os.path.getsize(target)))


if __name__ == "__main__":
    main()
