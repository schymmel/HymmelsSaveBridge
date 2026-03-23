#include "game_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <nds.h>
#include <nds/card.h>
#include <nds/system.h>

extern int g_total_games;

GameInfo* g_game_list = nullptr;

static int s_scanned_folders = 0;
static int s_found_nds = 0;

const char* TWILIGHT_INI = "sd:/_nds/TWiLightMenu/settings.ini";

static const char* maker_name_from_code(const char* maker_code) {
    if (strcmp(maker_code, "01") == 0) return "Nintendo";
    if (strcmp(maker_code, "08") == 0) return "Capcom";
    if (strcmp(maker_code, "13") == 0) return "Electronic Arts";
    if (strcmp(maker_code, "36") == 0) return "Ubisoft";
    if (strcmp(maker_code, "49") == 0) return "Square Enix";
    if (strcmp(maker_code, "52") == 0) return "Namco";
    if (strcmp(maker_code, "54") == 0) return "Konami";
    if (strcmp(maker_code, "60") == 0) return "Sega";
    if (strcmp(maker_code, "69") == 0) return "SNK";
    if (strcmp(maker_code, "70") == 0) return "Atlus";
    if (strcmp(maker_code, "78") == 0) return "THQ";
    if (strcmp(maker_code, "83") == 0) return "Amuze";
    if (strcmp(maker_code, "EB") == 0) return "Idea Factory";
    return "";
}

static void set_publisher_from_maker(GameInfo* info, const char* maker_code) {
    const char* name = maker_name_from_code(maker_code);
    if (name[0] != '\0') {
        snprintf(info->publisher, sizeof(info->publisher), "%s", name);
    } else {
        snprintf(info->publisher, sizeof(info->publisher), "Maker: %s", maker_code);
    }
}

bool get_twilight_save_setting(void) {
    FILE* f = fopen(TWILIGHT_INI, "r");
    if (!f) return false;

    char line[256];
    bool in_folder = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "SAVES_IN_SD_FOLDER") && strstr(line, "= 1")) {
            in_folder = true;
            break;
        }
    }
    fclose(f);
    return in_folder;
}

bool read_rom_header(const char* path, char* out_id, char* out_name) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;

    char header[512];
    if (fread(header, 1, 512, fp) != 512) {
        fclose(fp);
        return false;
    }

    memcpy(out_name, &header[0], 12);
    out_name[12] = '\0';
    for (int i = 0; i < 12; i++) {
        if (!isprint((int)out_name[i])) {
            out_name[i] = '\0';
            break;
        }
    }

    memcpy(out_id, &header[0x0C], 4);
    out_id[4] = '\0';

    fclose(fp);
    return true;
}

static bool decode_banner_data(const uint8_t* banner_data, GameInfo* info) {
    uint16_t version = banner_data[0] | (banner_data[1] << 8);
    if (version == 0 || version == 0xFFFF) return false;

    const uint8_t* tile_data = banner_data + 0x20;
    const uint16_t* palette = (const uint16_t*)(banner_data + 0x220);

    for (int ty = 0; ty < 4; ty++) {
        for (int tx = 0; tx < 4; tx++) {
            int tile_idx = ty * 4 + tx;
            const uint8_t* tile = &tile_data[tile_idx * 32];
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px += 2) {
                    uint8_t b = tile[py * 4 + (px / 2)];
                    uint8_t c1 = b & 0x0F;
                    uint8_t c2 = (b >> 4) & 0x0F;

                    int abs_y = ty * 8 + py;
                    int abs_x1 = tx * 8 + px;
                    int abs_x2 = abs_x1 + 1;

                    info->icon[abs_y * 32 + abs_x1] = (c1 == 0) ? 0 : (palette[c1] | BIT(15));
                    info->icon[abs_y * 32 + abs_x2] = (c2 == 0) ? 0 : (palette[c2] | BIT(15));
                }
            }
        }
    }

    uint32_t title_offsets[] = {0x240, 0x340, 0x440, 0x540, 0x640, 0x740};
    uint32_t title_offset = 0;

    for (int l = 1; l >= 0; l--) {
        uint32_t off = title_offsets[l];
        const uint16_t* test_ptr = (const uint16_t*)(banner_data + off);
        if (test_ptr[0] != 0 && test_ptr[0] != 0xFFFF) {
            title_offset = off;
            break;
        }
    }

    if (title_offset == 0) title_offset = title_offsets[0];

    const uint16_t* utf16_title = (const uint16_t*)(banner_data + title_offset);
    int out_idx = 0;
    for (int i = 0; i < 128 && out_idx < LONG_NAME_LEN - 3; i++) {
        uint16_t wc = utf16_title[i];
        if (wc == 0) break;
        if (wc == '\n' || wc == '\r') {
             if (out_idx > 0 && info->long_name[out_idx-1] != ' ')
                 info->long_name[out_idx++] = ' ';
             continue;
        }

        char c1 = 0, c2 = 0;
        if (wc < 0x80) c1 = (char)wc;
        else if (wc == 0x00E4) { c1 = 'a'; c2 = 'e'; }
        else if (wc == 0x00F6) { c1 = 'o'; c2 = 'e'; }
        else if (wc == 0x00FC) { c1 = 'u'; c2 = 'e'; }
        else if (wc == 0x00C4) { c1 = 'A'; c2 = 'e'; }
        else if (wc == 0x00D6) { c1 = 'O'; c2 = 'e'; }
        else if (wc == 0x00DC) { c1 = 'U'; c2 = 'e'; }
        else if (wc == 0x00DF) { c1 = 's'; c2 = 's'; }
        else if (wc == 0x00E9) { c1 = 'e'; }
        else if (wc < 0x100) { c1 = (char)wc; }
        else c1 = '?';

        info->long_name[out_idx++] = c1;
        if (c2) info->long_name[out_idx++] = c2;
    }
    info->long_name[out_idx] = '\0';

    return true;
}

static bool read_slot1_banner(const uint8_t* header, GameInfo* info) {
    uint32_t banner_offset = header[0x68] | (header[0x69] << 8) | (header[0x6A] << 16) | (header[0x6B] << 24);
    if (banner_offset == 0) return false;

    uint8_t banner_data[2560] = {0};
    sysSetCardOwner(BUS_OWNER_ARM9);
    for(int i=0; i<10; i++) swiWaitForVBlank();

    cardRead(banner_data, banner_offset, 2560, 0);

    return decode_banner_data(banner_data, info);
}

bool read_rom_banner(const char* path, GameInfo* info) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;

    uint32_t banner_offset;
    fseek(fp, 0x68, SEEK_SET);
    if (fread(&banner_offset, 1, 4, fp) != 4) { fclose(fp); return false; }
    if (banner_offset == 0 || banner_offset > 0x10000000) { fclose(fp); return false; }

    uint8_t banner_data[0x840];
    fseek(fp, banner_offset, SEEK_SET);
    if (fread(banner_data, 1, sizeof(banner_data), fp) != sizeof(banner_data)) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return decode_banner_data(banner_data, info);
}

static void add_game_instance(const char* rom_path, const char* save_path,
                              const char* game_id, const char* game_name, bool is_slot1) {
    GameInfo* info = g_game_list;
    GameInfo* prev = nullptr;

    while (info) {
        if (strcmp(info->game_id, game_id) == 0) break;
        prev = info;
        info = info->next;
    }

    if (!info) {
        info = (GameInfo*)calloc(1, sizeof(GameInfo));
        snprintf(info->game_id, sizeof(info->game_id), "%s", game_id);
        snprintf(info->game_name, sizeof(info->game_name), "%s", game_name);

        if (!is_slot1 && rom_path && rom_path[0] != '\0') {
            read_rom_banner(rom_path, info);
        }

        if (info->long_name[0] == '\0') {
            snprintf(info->long_name, sizeof(info->long_name), "%s", game_name);
            for (int i=0; i<32*32; i++) info->icon[i] = RGB15(10,10,10) | BIT(15);
        }

        if (prev) prev->next = info;
        else g_game_list = info;
        g_total_games++;
    }

    GameInstance* inst = (GameInstance*)calloc(1, sizeof(GameInstance));
    if (rom_path) snprintf(inst->rom_path, sizeof(inst->rom_path), "%s", rom_path);
    snprintf(inst->save_path, sizeof(inst->save_path), "%s", save_path);
    inst->is_slot1 = is_slot1;

    inst->next = info->instances;
    info->instances = inst;
}

const char* IGNORE_DIRS[] = {
    "3ds", "Nintendo 3DS", "_nds", "luma", "gm9", "hsb", "System Volume Information", "photochannel", "DCIM", "private", "Nintendo DSi", "DSiWare", nullptr
};

static bool is_blacklisted(const char* dir_name) {
    for (int i = 0; IGNORE_DIRS[i] != nullptr; i++) {
        if (strcasecmp(dir_name, IGNORE_DIRS[i]) == 0) return true;
    }
    return false;
}

static void scan_dir_recursive(const char* dir_path, bool saves_in_folder, scan_callback_t cb) {
    DIR* dir = opendir(dir_path);
    if (!dir) return;

    s_scanned_folders++;
    if (cb) cb(s_scanned_folders, s_found_nds, dir_path);

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (is_blacklisted(entry->d_name)) continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_dir_recursive(full_path, saves_in_folder, cb);
            } else {
                const char* ext = strrchr(entry->d_name, '.');
                if (ext && strcasecmp(ext, ".nds") == 0) {
                    char game_id[5];
                    char game_name[13];
                    if (read_rom_header(full_path, game_id, game_name)) {
                        char save_path[MAX_PATH_LEN];
                        if (saves_in_folder) {
                            snprintf(save_path, sizeof(save_path), "sd:/saves/%s.sav", entry->d_name);
                        } else {
                            snprintf(save_path, sizeof(save_path), "%s", full_path);
                            char* s_ext = strrchr(save_path, '.');
                            if (s_ext) strcpy(s_ext, ".sav");
                        }
                        add_game_instance(full_path, save_path, game_id, game_name, false);
                        s_found_nds++;
                    }
                }
            }
        }
    }
    closedir(dir);
}

extern int g_total_games;

void refresh_slot1(void) {
    GameInfo* info = g_game_list;
    GameInfo* prev = nullptr;
    while (info) {
        if (info->instances && info->instances->is_slot1) {
            if (prev) prev->next = info->next;
            else g_game_list = info->next;

            GameInfo* target = info;
            info = info->next;
            free(target->instances);
            free(target);
            g_total_games--;
        } else {
            prev = info;
            info = info->next;
        }
    }

    sysSetCardOwner(BUS_OWNER_ARM9);

    uint8_t header[512] = {0};
    cardReadHeader(header);
    char game_id[5] = {0};
    memcpy(game_id, &header[0x0C], 4);

    if (game_id[0] != 0x00 && game_id[0] != 0xFF) {
        char maker_code[3] = { (char)header[0x10], (char)header[0x11], 0 };
        char game_name[13] = {0};
        memcpy(game_name, &header[0x00], 12);
        for (int i=0; i<12; i++) {
            if (!isprint((int)game_name[i]) || game_name[i] == '\r' || game_name[i] == '\n') {
                game_name[i] = '\0';
                break;
            }
        }

        GameInfo* info = g_game_list;
        while (info) {
            if (strcmp(info->game_id, game_id) == 0) break;
            info = info->next;
        }

        if (info) {
            GameInstance* inst = (GameInstance*)calloc(1, sizeof(GameInstance));
            strcpy(inst->save_path, "slot1:/save");
            inst->is_slot1 = true;
            inst->next = info->instances;
            info->instances = inst;
        } else {
            GameInfo* n_info = (GameInfo*)calloc(1, sizeof(GameInfo));
            snprintf(n_info->game_id, sizeof(n_info->game_id), "%s", game_id);
            snprintf(n_info->game_name, sizeof(n_info->game_name), "%s", game_name);
            set_publisher_from_maker(n_info, maker_code);
            if (!read_slot1_banner(header, n_info)) {
                snprintf(n_info->long_name, sizeof(n_info->long_name), "%.12s (No Banner)", game_name);
                for (int i = 0; i < 32 * 32; i++) n_info->icon[i] = RGB15(31,10,10) | BIT(15);
            }

            GameInstance* inst = (GameInstance*)calloc(1, sizeof(GameInstance));
            strcpy(inst->save_path, "slot1:/save");
            inst->is_slot1 = true;

            n_info->instances = inst;
            n_info->next = g_game_list;
            g_game_list = n_info;
            g_total_games++;
        }
    }
}

static void check_slot1(void) {
    refresh_slot1();
}

void scan_all_media(scan_callback_t cb) {
    s_scanned_folders = 0;
    s_found_nds = 0;

    bool saves_in_folder = get_twilight_save_setting();

    check_slot1();

    scan_dir_recursive("sd:/", saves_in_folder, cb);
}

void free_game_list(void) {
    GameInfo* info = g_game_list;
    while (info) {
        GameInfo* next_info = info->next;
        GameInstance* inst = info->instances;
        while (inst) {
            GameInstance* next_inst = inst->next;
            free(inst);
            inst = next_inst;
        }
        free(info);
        info = next_info;
    }
    g_game_list = nullptr;
    g_total_games = 0;
}
