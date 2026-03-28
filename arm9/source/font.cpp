#include "font.h"
#include "tonccpy.h"
#include <cstring>
#include <cstdio>
#include <new>

Font* g_font = nullptr;

std::u16string Font::utf8to16(const char* text) {
    std::u16string out;
    if (!text) return out;
    
    for (int i = 0; text[i]; ) {
        char16_t c = 0;
        if (!(text[i] & 0x80)) {
            c = text[i++];
        } else if ((text[i] & 0xE0) == 0xC0) {
            c  = (text[i++] & 0x1F) << 6;
            c |=  text[i++] & 0x3F;
        } else if ((text[i] & 0xF0) == 0xE0) {
            c  = (text[i++] & 0x0F) << 12;
            c |= (text[i++] & 0x3F) << 6;
            c |=  text[i++] & 0x3F;
        } else {
            i++;
        }
        if (c) out += c;
    }
    return out;
}

std::u16string Font::utf16FromBanner(const u16* banner_title, int max_chars) {
    std::u16string out;
    if (!banner_title) return out;
    
    for (int i = 0; i < max_chars && banner_title[i]; i++) {
        char16_t c = banner_title[i];
        if (c == '\n' || c == '\r') {
            if (!out.empty() && out.back() != u' ') out += u' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == u' ') out.pop_back();
    return out;
}

// NFTR loader based on TWiLightMenu's FontGraphic approach.
// Uses FINF block offsets instead of chunk scanning.
Font::Font(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return;
    
    fseek(file, 0, SEEK_END);
    u32 fileSize = ftell(file);

    if (fileSize < 0x30) { fclose(file); return; }

    // At offset 0x14 in FINF: a byte indicating offset to CGLP data
    fseek(file, 0x14, SEEK_SET);
    u8 finfSize = fgetc(file);
    fseek(file, finfSize - 1, SEEK_CUR);

    // Now positioned right at CGLP chunkSize (TWiLightMenu's approach
    // lands us here: past the CGLP signature, at the size field)
    u32 chunkSize;
    fread(&chunkSize, 4, 1, file);
    tileWidth = fgetc(file);
    tileHeight = fgetc(file);
    fread(&tileSize, 2, 1, file);

    if (tileSize == 0 || tileWidth == 0 || tileHeight == 0) { fclose(file); return; }

    // Load character glyphs
    tileAmount = (chunkSize - 0x10) / tileSize;
    if (tileAmount <= 0 || tileAmount > 25000) { fclose(file); return; }

    fseek(file, 4, SEEK_CUR); // skip baseline, maxCharWidth, bpp, rotateFlag
    fontTiles = new (std::nothrow) u8[tileSize * tileAmount];
    if (!fontTiles) { fclose(file); return; }
    fread(fontTiles, tileSize, tileAmount, file);

    // Load character widths via HDWC offset from FINF (at file offset 0x24)
    fseek(file, 0x24, SEEK_SET);
    u32 locHDWC;
    fread(&locHDWC, 4, 1, file);
    if (locHDWC > 0 && locHDWC < fileSize) {
        fseek(file, locHDWC - 4, SEEK_SET);
        u32 hdwcSize;
        fread(&hdwcSize, 4, 1, file);
        fseek(file, 8, SEEK_CUR); // skip firstCode, lastCode, next
        fontWidths = new (std::nothrow) u8[3 * tileAmount];
        if (fontWidths) fread(fontWidths, 3, tileAmount, file);
    }

    // Load character maps via PAMC linked list from FINF (at file offset 0x28)
    fontMap = new (std::nothrow) u16[tileAmount];
    if (!fontMap) { fclose(file); return; }
    memset(fontMap, 0, sizeof(u16) * tileAmount);

    fseek(file, 0x28, SEEK_SET);
    u32 locPAMC;
    fread(&locPAMC, 4, 1, file);

    while (locPAMC && locPAMC < fileSize) {
        u16 firstChar, lastChar;
        u32 mapType;
        fseek(file, locPAMC, SEEK_SET);
        fread(&firstChar, 2, 1, file);
        fread(&lastChar, 2, 1, file);
        fread(&mapType, 4, 1, file);
        fread(&locPAMC, 4, 1, file); // next PAMC offset (linked list!)

        if (mapType == 0) {
            u16 firstTile;
            fread(&firstTile, 2, 1, file);
            for (u32 i = firstChar; i <= lastChar && (firstTile + i - firstChar) < (u32)tileAmount; i++)
                fontMap[firstTile + i - firstChar] = i;
        } else if (mapType == 1) {
            for (u32 i = firstChar; i <= lastChar; i++) {
                u16 tile;
                fread(&tile, 2, 1, file);
                if (tile < (u32)tileAmount) fontMap[tile] = i;
            }
        } else if (mapType == 2) {
            u16 groups;
            fread(&groups, 2, 1, file);
            for (int i = 0; i < groups; i++) {
                u16 c, t;
                fread(&c, 2, 1, file);
                fread(&t, 2, 1, file);
                if (t < (u32)tileAmount) fontMap[t] = c;
            }
        }
    }

    questionMark = getCharIndex(0xFFFD);
    if (questionMark == 0) questionMark = getCharIndex('?');
    
    fclose(file);
}

Font::~Font() {
    if (fontTiles) delete[] fontTiles;
    if (fontWidths) delete[] fontWidths;
    if (fontMap) delete[] fontMap;
}

u16 Font::getCharIndex(char16_t c) {
    if (!fontMap) return 0;
    for (int i = 0; i < tileAmount; i++) {
        if (fontMap[i] == c) return i;
    }
    return questionMark;
}

int Font::calcWidth(const std::u16string& text) {
    if (!fontWidths) return text.length() * 8;
    int x = 0;
    for (char16_t c : text) {
        u16 index = getCharIndex(c);
        x += fontWidths[(index * 3) + 2];
    }
    return x;
}

// Render text to 16-bit framebuffer using TWiLightMenu's proven 2bpp decoding
void Font::print16(u16* fb, int x, int y, const std::u16string& text, u16 color) {
    if (!fontTiles || !fb || !fontWidths) return;
    
    for (char16_t c : text) {
        u16 index = getCharIndex(c);
        int charWidth = fontWidths[(index * 3) + 2];
        int charOffset = fontWidths[(index * 3)];
        
        for (int py = 0; py < tileHeight; py++) {
            int fbY = y + py;
            if (fbY < 0 || fbY >= 192) continue;
            for (int px = 0; px < tileWidth; px++) {
                int fbX = x + charOffset + px;
                if (fbX < 0 || fbX >= 256) continue;
                
                // 2bpp pixel decoding - same as TWiLightMenu's FontGraphic
                u8 pixelVal = fontTiles[(index * tileSize) + (py * tileWidth + px) / 4]
                              >> ((3 - ((py * tileWidth + px) % 4)) * 2) & 3;
                
                if (pixelVal > 0) {
                    if (pixelVal == 3) {
                        fb[fbY * 256 + fbX] = color | BIT(15);
                    } else if (pixelVal == 2) {
                        // Semi-opaque: blend with background
                        u16 existing = fb[fbY * 256 + fbX];
                        u16 r = (((color & 0x1F) * 3) + (existing & 0x1F)) >> 2;
                        u16 g = ((((color >> 5) & 0x1F) * 3) + ((existing >> 5) & 0x1F)) >> 2;
                        u16 b = ((((color >> 10) & 0x1F) * 3) + ((existing >> 10) & 0x1F)) >> 2;
                        fb[fbY * 256 + fbX] = (r | (g << 5) | (b << 10)) | BIT(15);
                    } else {
                        // Light: 50% blend
                        u16 existing = fb[fbY * 256 + fbX];
                        u16 r = ((color & 0x1F) + (existing & 0x1F)) >> 1;
                        u16 g = (((color >> 5) & 0x1F) + ((existing >> 5) & 0x1F)) >> 1;
                        u16 b = (((color >> 10) & 0x1F) + ((existing >> 10) & 0x1F)) >> 1;
                        fb[fbY * 256 + fbX] = (r | (g << 5) | (b << 10)) | BIT(15);
                    }
                }
            }
        }
        x += charWidth;
    }
}

void fontInit() {
    if (g_font) return;
    
    const char* paths[] = {
        "sd:/_nds/TWiLightMenu/extras/fonts/Default/small.nftr",
        "fat:/_nds/TWiLightMenu/extras/fonts/Default/small.nftr",
        "sd:/_nds/TWiLightMenu/extras/fonts/Default/large.nftr",
        "fat:/_nds/TWiLightMenu/extras/fonts/Default/large.nftr",
        "sd:/_nds/TWiLightMenu/extras/fonts/small.nftr",
        "fat:/_nds/TWiLightMenu/extras/fonts/small.nftr",
        "sd:/_nds/TWiLightMenu/font/small.nftr",
        "fat:/_nds/TWiLightMenu/font/small.nftr",
        "sd:/_nds/snapshot/font.nftr",
        "fat:/_nds/snapshot/font.nftr",
        "sd:/font.nftr",
        "fat:/font.nftr",
        "nitro:/font.nftr"
    };

    for (const char* path : paths) {
        g_font = new Font(path);
        if (g_font->isLoaded()) return;
        delete g_font;
    }
    g_font = nullptr;
}

bool fontAvailable() {
    return g_font != nullptr && g_font->isLoaded();
}

int fontPrintTitle16(u16* fb, int x, int y, const u16* utf16_title, u16 color) {
    if (!g_font || !utf16_title || !fb) return 0;
    std::u16string title = Font::utf16FromBanner(utf16_title, 128);
    if (title.empty()) return 0;
    g_font->print16(fb, x, y, title, color);
    return g_font->calcWidth(title);
}

// 8bpp version: renders font glyphs as palette indices
void Font::print8(u8* fb, int x, int y, const std::u16string& text, u8 basePalette) {
    if (!fontTiles || !fb || !fontWidths) return;
    
    for (char16_t c : text) {
        u16 index = getCharIndex(c);
        int charWidth = fontWidths[(index * 3) + 2];
        int charOffset = fontWidths[(index * 3)];
        
        for (int py = 0; py < tileHeight; py++) {
            int fbY = y + py;
            if (fbY < 0 || fbY >= 192) continue;
            for (int px = 0; px < tileWidth; px++) {
                int fbX = x + charOffset + px;
                if (fbX < 0 || fbX >= 256) continue;
                
                u8 pixelVal = fontTiles[(index * tileSize) + (py * tileWidth + px) / 4]
                              >> ((3 - ((py * tileWidth + px) % 4)) * 2) & 3;
                
                if (pixelVal > 0) {
                    fb[fbY * 256 + fbX] = basePalette + pixelVal;
                }
            }
        }
        x += charWidth;
    }
}

int fontPrintTitle8(u8* fb, int x, int y, const u16* utf16_title, u8 basePalette) {
    if (!g_font || !utf16_title || !fb) return 0;
    std::u16string title = Font::utf16FromBanner(utf16_title, 128);
    if (title.empty()) return 0;
    g_font->print8(fb, x, y, title, basePalette);
    return g_font->calcWidth(title);
}
