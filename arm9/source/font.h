#pragma once

#include <nds.h>
#include <string>

// Simplified font rendering for CJK support
// Based on TWiLightMenu's FontGraphic, adapted for direct 16-bit rendering

class Font {
private:
    u8 tileWidth = 0, tileHeight = 0;
    u16 tileSize = 0;
    int tileAmount = 0;
    u16 questionMark = 0;
    u8 *fontTiles = nullptr;
    u8 *fontWidths = nullptr;
    u16 *fontMap = nullptr;

    u16 getCharIndex(char16_t c);

public:
    static std::u16string utf8to16(const char* text);
    static std::u16string utf16FromBanner(const u16* banner_title, int max_chars);

    Font(const char* path);
    ~Font();

    bool isLoaded() const { return fontTiles != nullptr && fontWidths != nullptr; }
    u8 height() const { return tileHeight; }

    int calcWidth(const char* text);
    int calcWidth(const std::u16string& text);

    // Print text directly to 16-bit framebuffer at x,y position
    void print16(u16* fb, int x, int y, const char* text, u16 color);
    void print16(u16* fb, int x, int y, const std::u16string& text, u16 color);

    // Print text to 8-bit (paletted) framebuffer; writes basePalette+pixelVal as palette index
    void print8(u8* fb, int x, int y, const std::u16string& text, u8 basePalette);
};

// Global font instance
extern Font* g_font;

// Initialize font system - call once at startup after FAT init
void fontInit();

// Check if CJK font is available
bool fontAvailable();

// Print UTF-16 title directly to 16-bit framebuffer
int fontPrintTitle16(u16* fb, int x, int y, const u16* utf16_title, u16 color);

// Print UTF-16 title to 8-bit paletted framebuffer
int fontPrintTitle8(u8* fb, int x, int y, const u16* utf16_title, u8 basePalette);

