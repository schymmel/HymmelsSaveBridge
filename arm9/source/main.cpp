#include <stdio.h>
#include <nds.h>
#include <fat.h>

#include "game_manager.h"
#include "save_manager.h"
#include "snapshot_save.h"
#include "bg_top.h"
#include "bg_bottom.h"
#include <time.h>
#include "globals.h"

typedef enum {
    TAB_BACKUP,
    TAB_RESTORE
} BottomTab;

typedef enum {
    BSTATE_NONE,
    BSTATE_SELECT_SRC,
    BSTATE_SELECT_SLOT,
    BSTATE_ACTION
} BottomState;

PrintConsole topScreen;
PrintConsole bottomScreen;
int g_bgTopImage;
int g_bgTopIcons;
int g_bgBottom;

int g_selected_game_idx = 0;
int g_total_games = 0;
GameInfo* g_selected_game = nullptr;

BottomState g_bottom_state = BSTATE_NONE;
BottomTab g_current_tab = TAB_BACKUP;

BackupInfo* g_current_backups = nullptr;
int g_total_backups = 0;
int g_selected_backup_idx = 0;

int g_selected_instance_idx = 0;
int g_total_instances = 0;

int g_available_slots[10];
int g_num_available_slots = 0;
int g_selected_slot_menu_idx = 0;
int g_active_save_slot = 0;
bool g_block_slot1_refresh = false;

#ifndef SCFG_MC_IS_EJECTED
#define SCFG_MC_IS_EJECTED (1U << 0)
#endif

static int s_cached_clock_second = -1;
static int s_cached_clock_hour = 0;
static int s_cached_clock_minute = 0;
static int s_cached_clock_display_second = 0;
static bool s_slot1_state_known = false;
static bool s_slot1_present = false;
static int s_slot1_poll_frames = 0;

static void update_clock_cache(void) {
    time_t t = time(nullptr);
    if (t == (time_t)-1) return;

    if ((int)t == s_cached_clock_second) return;

    struct tm* tm = localtime(&t);
    if (!tm) return;

    s_cached_clock_second = (int)t;
    s_cached_clock_hour = tm->tm_hour;
    s_cached_clock_minute = tm->tm_min;
    s_cached_clock_display_second = tm->tm_sec;
}

static void draw_clock_bar(void) {
    consoleSelect(&topScreen);
    printf("\x1b[0;0H%02d:%02d:%02d                 v0.1.0",
           s_cached_clock_hour,
           s_cached_clock_minute,
           s_cached_clock_display_second);
}

static void prime_slot1_refresh_state(void) {
    s_slot1_poll_frames = 0;
    if (isDSiMode()) {
        s_slot1_present = (REG_SCFG_MC & SCFG_MC_IS_EJECTED) == 0;
        s_slot1_state_known = true;
    } else {
        s_slot1_present = false;
        s_slot1_state_known = false;
    }
}

static bool should_refresh_slot1_now(void) {
    if (g_block_slot1_refresh) return false;

    if (isDSiMode()) {
        bool slot1_present_now = (REG_SCFG_MC & SCFG_MC_IS_EJECTED) == 0;
        if (!s_slot1_state_known) {
            s_slot1_present = slot1_present_now;
            s_slot1_state_known = true;
            return false;
        }
        if (slot1_present_now != s_slot1_present) {
            s_slot1_present = slot1_present_now;
            return true;
        }
        return false;
    }

    s_slot1_poll_frames++;
    if (s_slot1_poll_frames < 1800) return false;
    s_slot1_poll_frames = 0;
    return true;
}

void get_slot_path(char* out_path, GameInstance* inst, int slot) {
    if (inst->is_slot1) {
        strcpy(out_path, inst->save_path);
    } else {
        if (slot == 0) {
            strcpy(out_path, inst->save_path);
        } else {
            snprintf(out_path, MAX_PATH_LEN + 16, "%s%d", inst->save_path, slot);
        }
    }
}

void check_available_slots(GameInstance* inst) {
    g_num_available_slots = 0;
    g_selected_slot_menu_idx = 0;

    if (inst->is_slot1) {
        g_available_slots[g_num_available_slots++] = 0;
        return;
    }

    // Always allow all 10 slots for SD games so user can restore to a new one
    for (int i = 0; i <= 9; i++) {
        g_available_slots[g_num_available_slots++] = i;
    }
}

void count_games(void) {
    g_total_games = 0;
    GameInfo* info = g_game_list;
    while (info) {
        g_total_games++;
        info = info->next;
    }
}

GameInfo* get_game_at_index(int idx) {
    GameInfo* info = g_game_list;
    for (int i = 0; i < idx && info != nullptr; i++) {
        info = info->next;
    }
    return info;
}

BackupInfo* get_backup_at_index(int idx) {
    BackupInfo* b = g_current_backups;
    for (int i = 0; i < idx && b != nullptr; i++) {
        b = b->next;
    }
    return b;
}

GameInstance* get_instance_at_index(int idx) {
    if (!g_selected_game) return nullptr;
    GameInstance* inst = g_selected_game->instances;
    for (int i = 0; i < idx && inst != nullptr; i++) {
        inst = inst->next;
    }
    return inst;
}

void reload_instances(void) {
    g_total_instances = 0;
    g_selected_instance_idx = 0;
    if (g_selected_game) {
        GameInstance* inst = g_selected_game->instances;
        while(inst) {
            g_total_instances++;
            inst = inst->next;
        }
    }
}

void reload_backups(void) {
    if (g_current_backups) {
        free_backup_list(g_current_backups);
        g_current_backups = nullptr;
    }
    g_total_backups = 0;
    g_selected_backup_idx = 0;

    if (g_selected_game) {
        g_current_backups = get_backups_for_game(g_selected_game->game_id);
        BackupInfo* b = g_current_backups;
        while (b) {
            g_total_backups++;
            b = b->next;
        }
    }
}

void draw_scanning(void) {
    consoleSelect(&bottomScreen);
    consoleClear();
    printf("\n\n\n\n");
    printf("   ============================\n");
    printf("            Snapshot\n");
    printf("   ============================\n");

    printf("\n        Scanning Media...\n");
    printf("\n        (SD & Slot-1)\n");
    swiWaitForVBlank();
}

void scan_progress_cb(int folders, int files, const char* current_path) {
    consoleSelect(&bottomScreen);
    consoleSetCursor(nullptr, 0, 12);
    printf("  Folders scanned: %d       \n", folders);
    printf("  Games found:     %d       \n", files);

    char short_path[32];
    strncpy(short_path, current_path, 31);
    short_path[31] = '\0';
    printf("  Dir: %-26s\n", short_path);

    static int tick_counter = 0;
    tick_counter++;

    if (tick_counter % 5 == 0) {
        swiWaitForVBlank();
        scanKeys();
    }
}

extern "C" void refresh_clock_only(void) {
    update_clock_cache();
    const PrintConsole* prev = consoleGetDefault();
    draw_clock_bar();
    if (prev) consoleSelect((PrintConsole*)prev);
}

void draw_top_screen(void) {
    uint16_t* vram = (uint16_t*)bgGetGfxPtr(g_bgTopIcons);
    memset(vram, 0, 128 * 1024);

    uint16_t bar_color = RGB15(0, 4, 12) | BIT(15);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 256; x++) vram[y * 256 + x] = bar_color;
    }

    consoleSelect(&topScreen);
    consoleClear();

    update_clock_cache();
    draw_clock_bar();

    if (g_total_games == 0) {
        printf("\n\n  No games found.\n");
        return;
    }

    int icons_per_page = 28;
    int page = g_selected_game_idx / icons_per_page;
    int start_idx = page * icons_per_page;

    for (int i = 0; i < icons_per_page; i++) {
        int idx = start_idx + i;
        if (idx >= g_total_games) break;

        GameInfo* info = get_game_at_index(idx);
        if (!info) break;

        int col = i % 7;
        int row = i / 7;

        int draw_x = (col * 35) + 6;
        int draw_y = (row * 36) + 16;

        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                uint16_t color = info->icon[y * 32 + x];
                if ((color & BIT(15))) {
                   vram[(draw_y + y) * 256 + (draw_x + x)] = color;
                }
            }
        }

        if (idx == g_selected_game_idx) {
            uint16_t border = RGB15(31,0,0) | BIT(15);
            uint16_t border2 = RGB15(20,0,0) | BIT(15);
            for (int p = 0; p < 38; p++) {
                vram[(draw_y - 3) * 256 + (draw_x - 3 + p)] = border;
                vram[(draw_y - 2) * 256 + (draw_x - 3 + p)] = border;
                vram[(draw_y - 1) * 256 + (draw_x - 3 + p)] = border2;

                vram[(draw_y + 32) * 256 + (draw_x - 3 + p)] = border2;
                vram[(draw_y + 33) * 256 + (draw_x - 3 + p)] = border;
                vram[(draw_y + 34) * 256 + (draw_x - 3 + p)] = border;

                vram[(draw_y - 3 + p) * 256 + (draw_x - 3)] = border;
                vram[(draw_y - 3 + p) * 256 + (draw_x - 2)] = border;
                vram[(draw_y - 3 + p) * 256 + (draw_x - 1)] = border2;

                vram[(draw_y - 3 + p) * 256 + (draw_x + 32)] = border2;
                vram[(draw_y - 3 + p) * 256 + (draw_x + 33)] = border;
                vram[(draw_y - 3 + p) * 256 + (draw_x + 34)] = border;
            }
        } else {
            uint16_t border = RGB15(10,10,10) | BIT(15);
            for (int p = 0; p < 34; p++) {
                vram[(draw_y - 1) * 256 + (draw_x - 1 + p)] = border;
                vram[(draw_y + 32) * 256 + (draw_x - 1 + p)] = border;
                vram[(draw_y - 1 + p) * 256 + (draw_x - 1)] = border;
                vram[(draw_y - 1 + p) * 256 + (draw_x + 32)] = border;
            }
        }
    }
}

static bool confirm_action(const char* action, const char* details) {
    uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
    dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

    consoleSelect(&bottomScreen);
    consoleClear();
    printf("\x1b[0;0H[ Confirm %s ]\n\n%s\n\nA: Confirm    B: Cancel", action, details);
    while (1) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_A) return true;
        if (keysDown() & KEY_B) return false;
    }
}

void draw_bottom_screen(void) {
    uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
    dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

    consoleSelect(&bottomScreen);
    consoleClear();

    if (!g_selected_game) return;

    char buffered_long_name[512] = {0};
    int b_idx = 0;
    for (int i = 0; g_selected_game->long_name[i] != '\0' && b_idx < 510; i++) {
        char c = g_selected_game->long_name[i];
        buffered_long_name[b_idx++] = c;
    }
    printf("\x1b[0;0H%s", buffered_long_name);
    printf("\x1b[2;0HID: %s", g_selected_game->game_id);

    GameInstance* inst = get_instance_at_index(g_selected_instance_idx);
    const char* media_type = inst ? (inst->is_slot1 ? "Cartridge" : "SD Card") : "Unknown";
    printf("\x1b[3;0HMedia type: %s", media_type);
    if (g_selected_game->publisher[0] != '\0')
        printf("\x1b[4;0HPublisher: %s", g_selected_game->publisher);

    (void)g_selected_game;

    if (g_bottom_state == BSTATE_SELECT_SRC) {
        printf("\x1b[8;0H   [ SELECT TARGET INSTANCE ]\n");
        printf("  ============================\n\n");
        GameInstance* cur = g_selected_game->instances;
        int i = 0;
        int display_lines = 0;
        while (cur && display_lines < 6) {
            if (i == g_selected_instance_idx) printf(" > ");
            else printf("   ");
            if (cur->is_slot1) printf("Slot-1 Cartridge\n");
            else {
                char short_path[28];
                strncpy(short_path, cur->rom_path, 27);
                short_path[27] = '\0';
                printf("SD: %s\n", short_path);
            }
            cur = cur->next;
            i++;
            display_lines++;
        }
    } else if (g_bottom_state == BSTATE_SELECT_SLOT) {
        printf("\x1b[8;0H   [ SELECT SAVE SLOT ]\n");
        printf("  ============================\n\n");
        GameInstance* inst = get_instance_at_index(g_selected_instance_idx);
        int start_idx = g_selected_slot_menu_idx > 3 ? g_selected_slot_menu_idx - 3 : 0;
        if (start_idx > g_num_available_slots - 6) start_idx = g_num_available_slots - 6;
        if (start_idx < 0) start_idx = 0;

        for (int i = start_idx; i < start_idx + 6 && i < g_num_available_slots; i++) {
            int slot_id = g_available_slots[i];
            if (i == g_selected_slot_menu_idx) printf(" > ");
            else printf("   ");

            char path_buf[MAX_PATH_LEN + 16];
            get_slot_path(path_buf, inst, slot_id);
            bool exists = access(path_buf, F_OK) == 0;

            if (slot_id == 0) printf("Default Slot (.sav)");
            else printf("Save Slot %d (.sav%d)", slot_id, slot_id);
            
            if (!exists && !inst->is_slot1) printf(" [Empty]");
            printf("\n");
        }
    } else if (g_bottom_state == BSTATE_ACTION || g_bottom_state == BSTATE_NONE) {
        BG_PALETTE_SUB[210] = RGB15(5,5,5);
        BG_PALETTE_SUB[211] = RGB15(3,3,3);
        BG_PALETTE_SUB[212] = RGB15(15,15,15);
        BG_PALETTE_SUB[213] = RGB15(16,8,22);

        for (int y = 80; y < 184; y++) {
            for (int x = 0; x < 163; x++) vram[y * 256 + x] = 210;
        }

        for (int bt = 0; bt < 2; bt++) {
            for (int y = 80 + bt * 40; y < 80 + bt * 40 + 35; y++) {
                for (int x = 163; x < 256; x++) {
                   if (y == 80 + bt * 40 || y == 80 + bt * 40 + 34 || x == 163 || x == 255)
                       vram[y * 256 + x] = 212;
                   else
                       vram[y * 256 + x] = 211;
                }
            }
        }

        int list_idx = g_selected_backup_idx;
        for (int y = 78 + list_idx * 16; y < 78 + list_idx * 16 + 12; y++) {
            if (y >= 184) break;
            for (int x = 0; x < 163; x++) vram[y * 256 + x] = 213;
        }

        printf("\x1b[10;0H New...");
        BackupInfo* b = g_current_backups;
        int line = 12;
        int max_lines = 5;
        int drawn_lines = 0;
        int b_idx = 1;

        int print_start_idx = g_selected_backup_idx > 5 ? g_selected_backup_idx - 5 : 0;

        while (b && drawn_lines < max_lines) {
            if (b_idx >= print_start_idx + 1) {
                char trunc_name[21];
                strncpy(trunc_name, b->file_name, 20);
                trunc_name[20] = '\0';
                printf("\x1b[%d;0H %s", line, trunc_name);
                line += 2;
                drawn_lines++;
            }
            b = b->next;
            b_idx++;
        }

        printf("\x1b[12;21HBackup [L]");
        printf("\x1b[17;21HRestore [R]");

        if (g_bottom_state == BSTATE_NONE) {
            printf("\x1b[23;0H Press [A] to backup or restore.");
        } else {
            printf("\x1b[23;0H Press [B] to return.");
        }
    }
}

int main(int argc, char **argv) {
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);

    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    vramSetBankC(VRAM_C_SUB_BG);

    REG_BLDCNT = 0;
    REG_BLDCNT_SUB = 0;

    consoleInit(&topScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 3, 0, true, true);
    consoleInit(&bottomScreen, 0, BgType_Text4bpp, BgSize_T_256x256, 3, 0, false, true);

    g_bgTopImage = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    dmaCopy(bg_topBitmap, bgGetGfxPtr(g_bgTopImage), bg_topBitmapLen);
    dmaCopy(bg_topPal, BG_PALETTE, bg_topPalLen);

    g_bgTopIcons = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 12, 0);

    g_bgBottom = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    dmaCopy(bg_bottomPal, BG_PALETTE_SUB, bg_bottomPalLen);

    BG_PALETTE[255] = RGB15(31,31,31);
    BG_PALETTE_SUB[255] = RGB15(31,31,31);

    bgSetPriority(g_bgTopImage, 3);
    bgSetPriority(g_bgTopIcons, 2);
    bgSetPriority(topScreen.bgId, 0);

    bgSetPriority(g_bgBottom, 3);
    bgSetPriority(bottomScreen.bgId, 0);

    REG_BLDCNT = 0;
    REG_BLDY = 0;

    REG_BLDCNT_SUB = 0;
    REG_BLDY_SUB = 0;

    draw_scanning();

    if (!fatInitDefault()) {
        consoleSelect(&topScreen);
        printf("Failed to init FAT!\n");
        while (1) { swiWaitForVBlank(); }
    }

    scan_all_media(scan_progress_cb);
    count_games();
    prime_slot1_refresh_state();

    if (g_total_games > 0) {
        g_selected_game = get_game_at_index(0);
        reload_instances();
        reload_backups();
    }

    draw_top_screen();
    draw_bottom_screen();

    keysSetRepeat(20, 4);

    while (1) {
        swiWaitForVBlank();
        scanKeys();

        bool redraw_top = false;
        bool redraw_bottom = false;

        static int tick_frames = 0;
        tick_frames++;

        uint16_t keys_down = keysDown();
        uint16_t keys_held = keysHeld();
        if ((keys_held & KEY_L) && (keys_held & KEY_R) && (keys_down & KEY_SELECT)) {
            take_screenshot();
            consoleSelect(&bottomScreen);
            printf("\x1b[23;0H Screenshot saved!           ");
        }

        if (should_refresh_slot1_now()) {
            if (refresh_slot1()) {
                if (g_selected_game_idx >= g_total_games) g_selected_game_idx = 0;
                g_selected_game = get_game_at_index(g_selected_game_idx);
                reload_instances();
                reload_backups();
                redraw_top = true;
                redraw_bottom = true;
            }
        }
        if (tick_frames % 60 == 0) {
            refresh_clock_only();
        }

        keys_down = keysDown();
        uint16_t keys_repeat = keysDownRepeat();

        if (g_bottom_state == BSTATE_NONE) {
            if (keys_repeat & KEY_UP) {
                if (g_selected_game_idx >= 7) g_selected_game_idx -= 7;
                else g_selected_game_idx = 0;
            }
            if (keys_repeat & KEY_DOWN) {
                if (g_selected_game_idx + 7 < g_total_games) g_selected_game_idx += 7;
                else g_selected_game_idx = g_total_games - 1;
            }
            if (keys_repeat & KEY_LEFT) {
                if (g_selected_game_idx > 0) g_selected_game_idx--;
                else g_selected_game_idx = g_total_games - 1;
            }
            if (keys_repeat & KEY_RIGHT) {
                if (g_selected_game_idx < g_total_games - 1) g_selected_game_idx++;
                else g_selected_game_idx = 0;
            }

            if (keys_repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
                g_selected_game = get_game_at_index(g_selected_game_idx);
                reload_instances();
                reload_backups();
                redraw_top = true;
                redraw_bottom = true;
            }

            if (keys_down & KEY_A && g_selected_game && g_selected_game->instances) {
                check_available_slots(get_instance_at_index(g_selected_instance_idx));
                if (g_num_available_slots <= 1 && g_total_instances <= 1) {
                   g_active_save_slot = g_available_slots[0];
                   g_bottom_state = BSTATE_ACTION;
                } else if (g_total_instances > 1) {
                   g_bottom_state = BSTATE_SELECT_SRC;
                } else {
                   g_bottom_state = BSTATE_SELECT_SLOT;
                }
                redraw_top = true;
                redraw_bottom = true;
            }
} else if (g_bottom_state == BSTATE_SELECT_SRC) {
            if (keys_down & KEY_B) {
                g_bottom_state = BSTATE_NONE;
                redraw_top = true;
                redraw_bottom = true;
            }
            if (keys_repeat & KEY_UP && g_total_instances > 1) {
                if (g_selected_instance_idx > 0) g_selected_instance_idx--;
                else g_selected_instance_idx = g_total_instances - 1;
                redraw_bottom = true;
            }
            if (keys_repeat & KEY_DOWN && g_total_instances > 1) {
                if (g_selected_instance_idx < g_total_instances - 1) g_selected_instance_idx++;
                else g_selected_instance_idx = 0;
                redraw_bottom = true;
            }

            if (keys_down & KEY_A) {
                check_available_slots(get_instance_at_index(g_selected_instance_idx));
                if (g_num_available_slots <= 1) {
                   g_active_save_slot = g_available_slots[0];
                   g_bottom_state = BSTATE_ACTION;
                } else {
                   g_bottom_state = BSTATE_SELECT_SLOT;
                }
                redraw_bottom = true;
            }
} else if (g_bottom_state == BSTATE_SELECT_SLOT) {
            if (keys_down & KEY_B) {
                if (g_total_instances > 1) g_bottom_state = BSTATE_SELECT_SRC;
                else g_bottom_state = BSTATE_NONE;
                redraw_bottom = true;
            }
            if (keys_repeat & KEY_UP && g_num_available_slots > 1) {
                if (g_selected_slot_menu_idx > 0) g_selected_slot_menu_idx--;
                else g_selected_slot_menu_idx = g_num_available_slots - 1;
                redraw_bottom = true;
            }
            if (keys_repeat & KEY_DOWN && g_num_available_slots > 1) {
                if (g_selected_slot_menu_idx < g_num_available_slots - 1) g_selected_slot_menu_idx++;
                else g_selected_slot_menu_idx = 0;
                redraw_bottom = true;
            }
            if (keys_down & KEY_A) {
                g_active_save_slot = g_available_slots[g_selected_slot_menu_idx];
                g_bottom_state = BSTATE_ACTION;
                redraw_bottom = true;
            }
} else if (g_bottom_state == BSTATE_ACTION) {
            if (keys_down & KEY_B) {
                if (g_num_available_slots > 1) g_bottom_state = BSTATE_SELECT_SLOT;
                else if (g_total_instances > 1) g_bottom_state = BSTATE_SELECT_SRC;
                else g_bottom_state = BSTATE_NONE;
                redraw_bottom = true;
            }

            if (keys_repeat & KEY_UP) {
                if (g_selected_backup_idx > 0) g_selected_backup_idx--;
                else g_selected_backup_idx = g_total_backups;
                redraw_bottom = true;
            }
            if (keys_repeat & KEY_DOWN) {
                if (g_selected_backup_idx < g_total_backups) g_selected_backup_idx++;
                else g_selected_backup_idx = 0;
                redraw_bottom = true;
            }

            if (keys_down & KEY_L) {
                GameInstance* target_inst = get_instance_at_index(g_selected_instance_idx);
                char check_path[MAX_PATH_LEN + 16];
                get_slot_path(check_path, target_inst, g_active_save_slot);
                if (access(check_path, F_OK) != 0 && !target_inst->is_slot1) {
                    confirm_action("Error", "No save data found in this slot.");
                    redraw_bottom = true;
                    continue;
                }

                if (g_selected_backup_idx > 0) {
                   if (!confirm_action("Overwrite Backup", "Are you sure you want to overwrite?")) {
                       redraw_bottom = true;
                       continue;
                   }
                }

                char backup_name[64] = "Backup";

                if (g_selected_backup_idx > 0) {
                   BackupInfo* b = g_current_backups;
                   for(int c=1; c<g_selected_backup_idx && b; c++) b=b->next;
                   if (b) {
                       snprintf(backup_name, 63, "%.62s", b->file_name);
                       char* dot = strrchr(backup_name, '.');
                       if (dot) *dot = '\0';
                   }
                } else {
                   time_t t = time(nullptr);
                   struct tm *tm = localtime(&t);
                   snprintf(backup_name, 64, "%04d%02d%02d-%02d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
                }

                int pos = strlen(backup_name);
                bool do_backup = true;

                if (g_selected_backup_idx == 0) {
                   while (1) {
                       swiWaitForVBlank();
                       scanKeys();
                       uint16_t kb_down = keysDown();
                       uint16_t kb_repeat = keysDownRepeat();

                       uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
                       dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

                       consoleSelect(&bottomScreen);
                       consoleClear();
                       printf("\x1b[0;0H[ Custom Backup Name ]\n\n> %s_\n\nUP/DOWN: Change Letter\nA: Next Letter\nB: Backspace\nSTART: Confirm\nSELECT: Cancel", backup_name);

                       char c = backup_name[pos];
                       if (c == '\0') c = 'A';

                       if (kb_repeat & KEY_UP) {
                           if (c == 'A') c = '9';
                           else if (c == '0') c = 'Z';
                           else c--;
                       }
                       if (kb_repeat & KEY_DOWN) {
                           if (c == '9') c = 'A';
                           else if (c == 'Z') c = '0';
                           else c++;
                       }

                       backup_name[pos] = c;
                       backup_name[pos+1] = '\0';

                       if (kb_repeat & KEY_LEFT) {
                           if (pos > 0) pos--;
                       }
                       if (kb_repeat & KEY_RIGHT) {
                           if (pos < 60 && backup_name[pos] != '\0') pos++;
                       }

                       if (kb_down & KEY_A) {
                           if (pos < 60) pos++;
                       }
                       if (kb_down & KEY_B) {
                           if (pos > 0) {
                               backup_name[pos] = '\0';
                               pos--;
                           }
                       }
                       if (kb_down & KEY_START) {
                           break;
                       }
                       if (kb_down & KEY_SELECT) {
                           backup_name[0] = '\0';
                           do_backup = false;
                           break;
                       }
                   }
                }

                if (do_backup && backup_name[0] != '\0') {
                   consoleSelect(&bottomScreen);
                   consoleClear();
                   printf("\n  Creating Backup...\n");
                   swiWaitForVBlank();

                   g_block_slot1_refresh = true;
                   GameInstance* target_inst = get_instance_at_index(g_selected_instance_idx);

                   GameInstance temp_inst = *target_inst;
                   get_slot_path(temp_inst.save_path, target_inst, g_active_save_slot);

                   const char* err = create_backup(&temp_inst, g_selected_game->game_id, backup_name);
                   g_block_slot1_refresh = false;

                   uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
                   dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

                   consoleSelect(&bottomScreen);
                   consoleClear();
                   if (err == nullptr) {
                       printf("\x1b[0;0HSUCCESS!\n\nBackup created.");
                       reload_backups();
                   } else {
                       printf("\x1b[0;0HFAILED:\n\n%s", err);
                   }

                   printf("\n\nPress A or B to continue.");
                   while (1) {
                       swiWaitForVBlank();
                       scanKeys();
                       refresh_clock_only();
                       if (keysDown() & (KEY_B | KEY_A)) break;
                   }
                }
                redraw_bottom = true;
} else if (keys_down & KEY_R && g_selected_backup_idx > 0) {
                BackupInfo* b = g_current_backups;
                for(int c=1; c<g_selected_backup_idx && b; c++) b=b->next;

                GameInstance* target_inst = get_instance_at_index(g_selected_instance_idx);
                const char* confirm_msg = target_inst->is_slot1 ? "Overwrite cartridge with backup?" : "Overwrite save with backup?";

                if (b && confirm_action("Restore Backup", confirm_msg)) {
                   consoleSelect(&bottomScreen);
                   consoleClear();
                   printf("\n  Restoring Backup...\n  %s\n", b->file_name);
                   swiWaitForVBlank();

                   g_block_slot1_refresh = true;
                   GameInstance* target_inst = get_instance_at_index(g_selected_instance_idx);
                   GameInstance temp_inst = *target_inst;
                   get_slot_path(temp_inst.save_path, target_inst, g_active_save_slot);

                   const char* err = restore_backup(&temp_inst, b->full_path);
                   g_block_slot1_refresh = false;

                   uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
                   dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

                   consoleSelect(&bottomScreen);
                   consoleClear();
                   if (err == nullptr) {
                       printf("\x1b[0;0HSUCCESS!\n\nBackup restored.");
                   } else {
                       printf("\x1b[0;0HFAILED:\n\n%s", err);
                   }

                   printf("\n\nPress A or B to continue.");
                   while (1) {
                       swiWaitForVBlank();
                       scanKeys();
                       refresh_clock_only();
                       if (keysDown() & (KEY_B | KEY_A)) break;
                   }
                }
                redraw_bottom = true;
            } else if (keys_down & KEY_X && g_selected_backup_idx > 0) {
                BackupInfo* b = g_current_backups;
                for(int c=1; c<g_selected_backup_idx && b; c++) b=b->next;
                if (b && confirm_action("Delete Backup", "Are you sure you want to delete this?")) {
                   unlink(b->full_path);
                   g_selected_backup_idx--;
                   reload_backups();
                }
                redraw_bottom = true;
            } else if (keys_down & KEY_Y && g_selected_backup_idx > 0) {
                BackupInfo* b = g_current_backups;
                for(int c=1; c<g_selected_backup_idx && b; c++) b=b->next;
                if (b) {
                   char rename_buf[64] = "Renamed";
                   snprintf(rename_buf, 63, "%.62s", b->file_name);
                   char* dot = strrchr(rename_buf, '.');
                   if (dot) *dot = '\0';

                   int pos = strlen(rename_buf);

                   while (1) {
                       swiWaitForVBlank();
                       scanKeys();
                       uint16_t kb_down = keysDown();
                       uint16_t kb_repeat = keysDownRepeat();

                       uint8_t* vram = (uint8_t*)bgGetGfxPtr(g_bgBottom);
                       dmaCopy(bg_bottomBitmap, vram, 64 * 1024);

                       consoleSelect(&bottomScreen);
                       consoleClear();
                       printf("\x1b[0;0H[ Rename Backup ]\n\n> %s_\n\nUP/DOWN: Change Letter\nA: Next Letter\nB: Backspace\nSTART: Confirm\nSELECT: Cancel", rename_buf);

                       char c = rename_buf[pos];
                       if (c == '\0') c = 'A';

                       if (kb_repeat & KEY_UP) {
                           if (c == 'A') c = '9';
                           else if (c == '0') c = 'Z';
                           else c--;
                       }
                       if (kb_repeat & KEY_DOWN) {
                           if (c == '9') c = 'A';
                           else if (c == 'Z') c = '0';
                           else c++;
                       }

                       rename_buf[pos] = c;
                       rename_buf[pos+1] = '\0';

                       if (kb_repeat & KEY_LEFT) {
                           if (pos > 0) pos--;
                       }
                       if (kb_repeat & KEY_RIGHT) {
                           if (pos < 60 && rename_buf[pos] != '\0') pos++;
                       }

                       if (kb_down & KEY_A) {
                           if (pos < 60) pos++;
                       }
                       if (kb_down & KEY_B) {
                           if (pos > 0) {
                               rename_buf[pos] = '\0';
                               pos--;
                           }
                       }
                       if (kb_down & KEY_START) {
                           break;
                       }
                       if (kb_down & KEY_SELECT) {
                           rename_buf[0] = '\0';
                           break;
                       }
                   }

                   if (rename_buf[0] != '\0') {
                       char new_path[512];
                       snprintf(new_path, sizeof(new_path), "sd:/_nds/snapshot/backups/%s/%s.sav", g_selected_game->game_id, rename_buf);
                       rename(b->full_path, new_path);
                   }

                   reload_backups();
                   redraw_bottom = true;
                }
            }
        }

        if (keys_down & KEY_START) {
            break;
        }

        if (redraw_top) draw_top_screen();
        if (redraw_bottom) draw_bottom_screen();
    }

    if (g_current_backups) free_backup_list(g_current_backups);
    free_game_list();
    return 0;
}
