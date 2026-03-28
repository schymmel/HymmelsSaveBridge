#include "globals.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} BMPHeader;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits;
    uint32_t compression;
    uint32_t imagesize;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t important_colors;
} BMPInfoHeader;
#pragma pack(pop)

extern "C" bool take_screenshot(void) {
    mkdir("sd:/_nds/snapshot", 0777);
    mkdir("sd:/_nds/snapshot/screenshots", 0777);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char path[128];
    snprintf(path, sizeof(path), "sd:/_nds/snapshot/screenshots/Snapshot_%04d%02d%02d_%02d%02d%02d.bmp",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    vramSetBankD(VRAM_D_LCD);

    swiWaitForVBlank();

    REG_DISPCAPCNT = BIT(31) | (3 << 16) | (3 << 20) | (0 << 24);

    while(REG_DISPCAPCNT & BIT(31));

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    int width = 256;
    int height = 192 * 2;
    int row_size = (width * 3 + 3) & ~3;
    uint32_t image_size = row_size * height;

    BMPHeader header;
    header.type = 0x4D42;
    header.size = sizeof(BMPHeader) + sizeof(BMPInfoHeader) + image_size;
    header.reserved1 = 0;
    header.reserved2 = 0;
    header.offset = sizeof(BMPHeader) + sizeof(BMPInfoHeader);

    BMPInfoHeader info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(BMPInfoHeader);
    info.width = width;
    info.height = height;
    info.planes = 1;
    info.bits = 24;
    info.imagesize = image_size;

    fwrite(&header, sizeof(header), 1, f);
    fwrite(&info, sizeof(info), 1, f);

    uint8_t* row = (uint8_t*)malloc(row_size);
    if (!row) { fclose(f); return false; }

    uint16_t* capture_vram = (uint16_t*)VRAM_D;
    uint8_t* bottom_ptr = (uint8_t*)bgGetGfxPtr(g_bgBottom);

    uint16_t* sub_map = (uint16_t*)bgGetMapPtr(4);
    uint8_t* sub_tiles = (uint8_t*)bgGetGfxPtr(4);

    for (int y = 191; y >= 0; y--) {
        memset(row, 0, row_size);
        for (int x = 0; x < 256; x++) {
            uint8_t pixel = bottom_ptr[y * 256 + x];
            uint16_t color = BG_PALETTE_SUB[pixel];

            int tx = x / 8;
            int ty = y / 8;
            uint16_t tile_entry = sub_map[ty * 32 + tx];
            int tile_idx = tile_entry & 0x3FF;
            int px = x % 8;
            int py = y % 8;

            uint8_t* tile_ptr = sub_tiles + (tile_idx * 32);
            uint8_t tile_byte = tile_ptr[py * 4 + (px / 2)];
            uint8_t text_color_idx = (px % 2 == 0) ? (tile_byte & 0xF) : (tile_byte >> 4);

            if (text_color_idx != 0) {
                color = RGB15(31,31,31);
            }

            row[x * 3 + 0] = (color >> 10) << 3;
            row[x * 3 + 1] = ((color >> 5) & 0x1F) << 3;
            row[x * 3 + 2] = (color & 0x1F) << 3;
        }
        fwrite(row, 1, row_size, f);
    }

    for (int y = 191; y >= 0; y--) {
        memset(row, 0, row_size);
        for (int x = 0; x < 256; x++) {
            uint16_t color = capture_vram[y * 256 + x];
            row[x * 3 + 0] = (color >> 10) << 3;
            row[x * 3 + 1] = ((color >> 5) & 0x1F) << 3;
            row[x * 3 + 2] = (color & 0x1F) << 3;
        }
        fwrite(row, 1, row_size, f);
    }

    vramSetBankD(VRAM_D_LCD);
    free(row);
    fclose(f);
    return true;
}
