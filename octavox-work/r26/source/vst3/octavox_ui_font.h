// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- minimal editor: embedded 5x7 bitmap glyph table (R33)
// ============================================================================
// PROVENANCE: our own retyping of the classic public-domain 5x7 terminal
// glyph set (the ASCII subset 32..126 used by character LCDs and GLCD
// displays since the 1980s; no vendor claims, no extracted font data from
// any product). Encoding: 5 bytes per glyph, one byte per column, bit 0 =
// the TOP row, bit 6 = the bottom row (the classic column-encoding, e.g.
// '!' = {0,0,0x5F,0,0}).
//
// WHY EMBEDDED: the minimal editor deliberately has NO font-stack dependency
// (no fontconfig/freetype on Linux, no GDI font selection on Windows) --
// the software renderer rasterises text from THIS table, so a rendered
// frame is bit-reproducible on every platform and the golden-pixel tests
// can pin exact output. Glyph advance = 6 px (5 glyph + 1 spacing) at
// scale 1; 5*scale + 1*scale at integer scales.
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_UI_FONT_H
#define OCTAVOX_UI_FONT_H

#include <cstdint>

namespace octavox {
namespace vst3wrap {
namespace ui {

/// Glyph table for ASCII 32..126 (95 glyphs x 5 columns). Indexed by
/// (code - 32). Columns are byte-per-column, bit0 = top row, bits 0..6 used.
extern const uint8_t kFont5x7[95][5];

/// One column of one glyph (0 for codes outside 32..126). col 0..4.
inline uint8_t glyphColumn(unsigned char code, int col) {
    if (code < 32 || code > 126 || col < 0 || col > 4) return 0;
    return kFont5x7[code - 32][static_cast<unsigned>(col)];
}

}  // namespace ui
}  // namespace vst3wrap
}  // namespace octavox

#endif  // OCTAVOX_UI_FONT_H
