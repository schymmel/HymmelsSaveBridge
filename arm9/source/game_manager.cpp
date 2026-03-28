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
#include "read_card.h"

extern int g_total_games;

GameInfo* g_game_list = nullptr;

static int s_scanned_folders = 0;
static int s_found_nds = 0;

const char* TWILIGHT_INI = "sd:/_nds/TWiLightMenu/settings.ini";

typedef struct {
    bool saves_in_folder;
    char custom_save_path[MAX_PATH_LEN];
} SaveConfig;

static bool is_hidden_homebrew_id(const char* game_id) {
    if (!game_id) return false;
    // Skip homebrew and TWiLight Menu (SRLA)
    return strcmp(game_id, "####") == 0 || strcmp(game_id, "SRLA") == 0;
}

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

static void trim_twilight_line(char* str) {
    char* end;
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

void get_twilight_save_config(SaveConfig* config) {
    memset(config, 0, sizeof(SaveConfig));
    // Default to saves folder if something is wrong
    config->saves_in_folder = true;

    FILE* f = fopen(TWILIGHT_INI, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* ptr = strchr(line, '=');
        if (!ptr) continue;

        *ptr = '\0';
        char* key = line;
        char* val = ptr + 1;
        trim_twilight_line(key);
        trim_twilight_line(val);

        if (strcmp(key, "SAVES_IN_SD_FOLDER") == 0) {
            config->saves_in_folder = (val[0] == '1');
        } else if (strcmp(key, "SAVE_LOCATION") == 0) {
            // 0: saves folder, 1: rom folder, 2: _nds/TWiLightMenu/saves
            if (val[0] == '0') {
                config->saves_in_folder = true;
            } else if (val[0] == '1') {
                config->saves_in_folder = false;
            } else if (val[0] == '2') {
                snprintf(config->custom_save_path, sizeof(config->custom_save_path), "sd:/_nds/TWiLightMenu/saves");
            }
        } else if (strcmp(key, "SAVES_PATH") == 0) {
            // TWiLight Menu uses paths like "/saves" or "sd:/saves"
            if (val[0] != '\0') {
                if (strncmp(val, "sd:", 3) == 0) {
                    snprintf(config->custom_save_path, sizeof(config->custom_save_path), "%s", val);
                } else if (val[0] == '/') {
                    snprintf(config->custom_save_path, sizeof(config->custom_save_path), "sd:%s", val);
                } else {
                    snprintf(config->custom_save_path, sizeof(config->custom_save_path), "sd:/%s", val);
                }
                
                // Remove trailing slash
                size_t len = strlen(config->custom_save_path);
                if (len > 0 && config->custom_save_path[len-1] == '/') {
                    config->custom_save_path[len-1] = '\0';
                }
            }
        }
    }
    fclose(f);
}

bool get_twilight_save_setting(void) {
    SaveConfig config;
    get_twilight_save_config(&config);
    return config.saves_in_folder;
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
    for (uint32_t offset = 0; offset < sizeof(banner_data); offset += 0x200) {
        gm9CardRead(banner_offset + offset, banner_data + offset, false);
    }

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
    if (is_hidden_homebrew_id(game_id)) return;

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
    "3ds", "Nintendo 3DS", "_nds", "luma", "gm9", "System Volume Information", "photochannel", "DCIM", "private", "Nintendo DSi", "DSiWare", nullptr
};

static bool is_blacklisted(const char* dir_name) {
    for (int i = 0; IGNORE_DIRS[i] != nullptr; i++) {
        if (strcasecmp(dir_name, IGNORE_DIRS[i]) == 0) return true;
    }
    return false;
}

static void scan_dir_recursive(const char* dir_path, const SaveConfig* config, scan_callback_t cb) {
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
                scan_dir_recursive(full_path, config, cb);
            } else {
                const char* ext = strrchr(entry->d_name, '.');
                if (ext && strcasecmp(ext, ".nds") == 0) {
                    char game_id[5];
                    char game_name[13];
                    if (read_rom_header(full_path, game_id, game_name)) {
                        char save_path[MAX_PATH_LEN];
                        
                        // Extract ROM name without extension
                        char rom_name[MAX_PATH_LEN];
                        strncpy(rom_name, entry->d_name, sizeof(rom_name) - 1);
                        rom_name[sizeof(rom_name) - 1] = '\0';
                        char* dot = strrchr(rom_name, '.');
                        if (dot) *dot = '\0';

                        if (config->custom_save_path[0] != '\0') {
                            strncpy(save_path, config->custom_save_path, sizeof(save_path) - 1);
                            save_path[sizeof(save_path) - 1] = '\0';
                            strncat(save_path, "/", sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, rom_name, sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, ".sav", sizeof(save_path) - strlen(save_path) - 1);
                        } else if (config->saves_in_folder) {
                            strncpy(save_path, dir_path, sizeof(save_path) - 1);
                            save_path[sizeof(save_path) - 1] = '\0';
                            strncat(save_path, "/saves/", sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, rom_name, sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, ".sav", sizeof(save_path) - strlen(save_path) - 1);
                        } else {
                            strncpy(save_path, dir_path, sizeof(save_path) - 1);
                            save_path[sizeof(save_path) - 1] = '\0';
                            strncat(save_path, "/", sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, rom_name, sizeof(save_path) - strlen(save_path) - 1);
                            strncat(save_path, ".sav", sizeof(save_path) - strlen(save_path) - 1);
                        }
                        
                        if (!is_hidden_homebrew_id(game_id)) {
                            add_game_instance(full_path, save_path, game_id, game_name, false);
                            s_found_nds++;
                        }
                    }
                }
            }
        }
    }
    closedir(dir);
}

extern int g_total_games;

bool refresh_slot1(void) {
    bool changed = false;
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
            changed = true;
        } else {
            prev = info;
            info = info->next;
        }
    }

    sNDSHeaderExt header_info = {0};
    if (gm9CardInit(&header_info) != 0) {
        return changed;
    }

    uint8_t header_bytes[0x200] = {0};
    gm9CardRead(0, header_bytes, false);
    char game_id[5] = {0};
    memcpy(game_id, &header_bytes[0x0C], 4);

    if (game_id[0] != 0x00 && game_id[0] != 0xFF && !is_hidden_homebrew_id(game_id)) {
        char maker_code[3] = { (char)header_bytes[0x10], (char)header_bytes[0x11], 0 };
        char game_name[13] = {0};
        memcpy(game_name, &header_bytes[0x00], 12);
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
            changed = true;
        } else {
            GameInfo* n_info = (GameInfo*)calloc(1, sizeof(GameInfo));
            snprintf(n_info->game_id, sizeof(n_info->game_id), "%s", game_id);
            snprintf(n_info->game_name, sizeof(n_info->game_name), "%s", game_name);
            set_publisher_from_maker(n_info, maker_code);
            if (!read_slot1_banner(header_bytes, n_info)) {
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
            changed = true;
        }
    }

    return changed;
}

static void check_slot1(void) {
    refresh_slot1();
}

void scan_all_media(scan_callback_t cb) {
    s_scanned_folders = 0;
    s_found_nds = 0;

    SaveConfig config;
    get_twilight_save_config(&config);

    check_slot1();

    scan_dir_recursive("sd:/", &config, cb);
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
