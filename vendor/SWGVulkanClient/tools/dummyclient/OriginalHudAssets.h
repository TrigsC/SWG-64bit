#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dummyclient {
using HudAssetReader = std::function<std::optional<std::vector<uint8_t>>(const std::string&)>;
std::map<std::string,std::u16string> decodeOriginalStringTable(const std::vector<uint8_t>& bytes);
struct HudRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
struct HudGlyph {
    uint32_t code = 0;
    int advance = 0, advancePre = 0;
    HudRect source;
    std::string texturePath;
};
struct HudImageSlice { std::string texturePath; HudRect source; };
struct HudImageFrame {
    HudImageSlice image;
    int offsetX=0,offsetY=0,proportionalX=0,proportionalY=0;
    uint32_t duration=1;
};
struct HudImageStyle {
    std::vector<HudImageFrame> frames;
    float playTime=.5f;
    bool looping=true,reversing=false,fallback=false;
    std::optional<size_t> frameAt(uint64_t tick) const;
};
struct HudRgbaImage { int width = 0, height = 0; std::vector<uint8_t> pixels; };
struct HudWidgetLayout {
    int x = 0, y = 0, width = 0, height = 0;
    std::string packLocation, packSize;
    int minimumWidth = 0, minimumHeight = 0, maximumWidth = 16384, maximumHeight = 16384;
    bool hasMinimumSize = false, hasMaximumSize = false;
};
struct HudPostureWidget {
    HudWidgetLayout layout;
    std::array<float,4> color{1,1,1,1};
};
struct HudStateVolume {
    HudWidgetLayout composite,volume;
    int cellWidth=0,cellHeight=0;
    std::array<float,4> sampleColor{1,1,1,1};
};
struct HudPageAppearance {
    std::array<float,4> backgroundColor{1,1,1,1}, backgroundTint{1,1,1,1}, color{1,1,1,1};
    float backgroundOpacity = 0;
    bool hasBackgroundColor = false, hasBackgroundTint = false, hasColor = false, hasBackgroundOpacity = false;
};
struct HudRectangleAsset {
    std::map<std::string,HudImageSlice> slices;
    std::array<float,4> color{1,1,1,1};
    float opacity = 1;
    bool stretchCenterH = true, stretchCenterV = true, stretchNorth = true, stretchSouth = true, stretchWest = true, stretchEast = true;
    bool shrinkCenterH = true, shrinkCenterV = true, shrinkNorth = true, shrinkSouth = true, shrinkWest = true, shrinkEast = true;
};
struct HudBarLayer { HudWidgetLayout layout; HudPageAppearance appearance; HudRectangleAsset style; };
struct HudHamRow {
    HudWidgetLayout layout, inner;
    HudPageAppearance innerAppearance;
    HudBarLayer normalMax, current, currentMax,recentCurrent,recentMax;
    bool hasRecentMaximum=false;
};
struct HudHamLayout {
    HudWidgetLayout composite, ham;
    std::map<std::string,HudHamRow> rows; // authored h/a/m and optional f; sample widths are not server values
    bool hasAuthoredForceRow = false;
};
struct HudBitmapFont {int leading=0;std::map<uint32_t,HudGlyph> glyphs;};
struct HudChatNode {
    HudWidgetLayout layout;
    HudPageAppearance appearance;
    std::optional<HudRectangleAsset> style;
    int parent=-1;
    std::string path;
    bool visible=true;
};
struct OriginalChatAssets {
    std::vector<HudChatNode> nodes;
    std::map<std::string,HudBitmapFont> fonts;
    std::map<std::string,std::array<float,4>> palette;
    size_t output=0,input=0,channel=0,tabs=0;
    std::array<int,4> outputMargin{},inputPadding{};
    std::array<float,4> outputColor{},inputColor{},caretColor{},channelColor{};
    int caretWidth=0;
    std::array<HudRectangleAsset,3> tabStyles; // normal, activated, hover
    std::array<std::array<float,4>,3> tabTextColors;
    std::array<std::u16string,4> tabLabels;
    std::array<int,4> tabTextMargin{};
    static OriginalChatAssets load(const HudAssetReader&);
};

struct OriginalScreenNoticeAssets {
    HudWidgetLayout page,composite,sample;
    std::array<int,4> margin{};std::array<float,4> textColor{};
    std::array<int,2> sizeIncrement{};
    int spacing=0;float fadeInSpeed=0,fadeOutSpeed=0,textMinimum=0,unrollSpeed=180,parentFadeSpeed=0,parentIdleOpacity=0;
    static OriginalScreenNoticeAssets load(const HudAssetReader&);
};

// A bounded extraction of the authored target frame/name, not a general UI engine.
// Malformed or missing required assets throw std::runtime_error.
struct OriginalHudAssets {
    int targetWidth = 0, targetHeight = 0, targetOffsetY = 0;
    float targetPackWidthRatio = 0;
    int targetFrameX = 0, targetFrameY = 0, targetFrameWidth = 0, targetFrameHeight = 0;
    int targetPostureFrameX = 0, targetPostureFrameY = 0, targetPostureFrameWidth = 0, targetPostureFrameHeight = 0;
    int targetNameX = 0, targetNameY = 0, targetNameWidth = 0, targetNameHeight = 0;
    int targetLabelX = 0, targetLabelY = 0, targetLabelWidth = 0, targetLabelHeight = 0;
    int fontLeading = 0;
    std::array<float,4> nameColor{}, labelColor{}, frameColor{};
    std::map<uint32_t,HudGlyph> glyphs;
    std::map<std::string,HudImageSlice> frameSlices;
    std::map<std::string,HudImageSlice> postureFrameSlices;
    std::u16string targetLabel;
    bool targetLabelFromStf = false;
    int selfWidth = 0, selfHeight = 0, selfOffsetX = 0, selfOffsetY = 0;
    float selfPackWidthRatio = 0;
    int selfFrameX = 0, selfFrameY = 0, selfFrameWidth = 0, selfFrameHeight = 0;
    int selfPostureFrameX = 0, selfPostureFrameY = 0, selfPostureFrameWidth = 0, selfPostureFrameHeight = 0;
    int selfNameX = 0, selfNameY = 0, selfNameWidth = 0, selfNameHeight = 0;
    std::string selfNameAlignmentVertical, selfNameAlignmentHorizontal;
    bool selfNameHorizontalAlignmentAuthored = false;
    std::array<float,4> selfNameColor{}, selfFrameColor{}, selfPostureFrameColor{};
    std::map<std::string,HudImageSlice> selfFrameSlices, selfPostureFrameSlices;
    HudHamLayout selfHam, targetHam;
    HudPostureWidget selfPostureIcon,targetPostureIcon;
    std::array<HudImageStyle,15> postureIcons;
    HudStateVolume selfStates,targetStates;
    std::array<HudImageStyle,37> stateIcons;
    std::array<std::optional<std::array<float,4>>,37> stateColors;
    float stateFadeSpeed=0;
    static OriginalHudAssets load(const HudAssetReader& reader);
};

// Decode only the confirmed uncompressed 32-bit RGB+alpha font DDS format.
// Compressed frame atlases continue to use the project's existing DDS loader.
HudRgbaImage decodeHudRgbaDds(const std::vector<uint8_t>& bytes);
}
