// ---------------------------------------------------------------------------
// PITCHLAB HARMONIZER -- minimal editor: software renderer interface (R33)
// ============================================================================
// PROVENANCE: our own design. A deliberately tiny rasteriser over a caller
//-owned 0x00RRGGBB pixel buffer (the platform layers hand it their XImage
// bits / DIB section; tests hand it a plain array). No fonts, no images, no
// OS calls -- bit-reproducible output everywhere (pinned by golden tests).
// ---------------------------------------------------------------------------
#ifndef OCTAVOX_UI_RENDERER_H
#define OCTAVOX_UI_RENDERER_H

#include <cstdint>
#include <cmath>

namespace octavox {
namespace vst3wrap {
namespace ui {

/// Minimal 2D software renderer. All coordinates are pixels; all drawing is
/// clipped to the buffer (no crashes on out-of-range input).
class UiRenderer {
public:
    /// Renders into a caller-owned buffer (w*h pixels, row-major, top-down,
    /// one uint32_t per pixel, 0x00RRGGBB). The buffer must outlive the
    /// renderer (or setSize-ed uses; see the view).
    UiRenderer(uint32_t* pixels, int w, int h);

    /// The palette entries used by the minimal editor (0x00RRGGBB). Named,
    /// neutral-warm dark theme with a single amber accent (no blue/indigo).
    static constexpr uint32_t kColBg = 0x17191D;        ///< window background
    static constexpr uint32_t kColPanel = 0x1C1F24;     ///< panel fill
    static constexpr uint32_t kColPanelHi = 0x22262C;   ///< raised element
    static constexpr uint32_t kColLine = 0x2A2E35;      ///< hairlines/borders
    static constexpr uint32_t kColText = 0xE8E6E1;      ///< primary text
    static constexpr uint32_t kColDim = 0x8B8F98;       ///< secondary text
    static constexpr uint32_t kColFaint = 0x55595F;     ///< inactive labels
    static constexpr uint32_t kColAccent = 0xE8A33D;    ///< amber accent
    static constexpr uint32_t kColAccentDim = 0x8A6224; ///< accent fill (bars)
    static constexpr uint32_t kColGreen = 0x7FBF5A;     ///< activity LED
    static constexpr uint32_t kColRed = 0xC4554B;       ///< bypass/mute state
    static constexpr uint32_t kColTrack = 0x101216;     ///< slider groove

    void clear(uint32_t c);
    void rect(int x, int y, int w, int h, uint32_t c);  ///< filled rectangle
    void frame(int x, int y, int w, int h, uint32_t c); ///< 1px border
    void hline(int x, int y, int w, uint32_t c);
    void vline(int x, int y, int h, uint32_t c);
    void disc(int cx, int cy, int r, uint32_t c);       ///< filled circle
    int textWidth(const char* s, int scale = 1) const;
    void text(int x, int y, const char* s, uint32_t c, int scale = 1);
    void textRight(int xRight, int y, const char* s, uint32_t c,
                   int scale = 1);
    void textCenter(int cx, int y, const char* s, uint32_t c, int scale = 1);

    int width() const { return w_; }
    int height() const { return h_; }

private:
    uint32_t* pixels_;
    int w_;
    int h_;
};

}  // namespace ui
}  // namespace vst3wrap
}  // namespace octavox

#endif  // OCTAVOX_UI_RENDERER_H
