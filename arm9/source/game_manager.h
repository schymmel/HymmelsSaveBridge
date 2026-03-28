#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <dirent.h>

#define MAX_PATH_LEN 1280
#define GAME_ID_LEN 4
#define GAME_NAME_LEN 16
#define LONG_NAME_LEN 128

typedef struct GameInstance {
    char rom_path[MAX_PATH_LEN];
    char save_path[MAX_PATH_LEN];
    bool is_slot1;
    struct GameInstance* next;
} GameInstance;

typedef struct GameInfo {
    char game_id[GAME_ID_LEN + 1];
    char game_name[GAME_NAME_LEN + 1];
    char long_name[LONG_NAME_LEN];
    char publisher[64];
    uint16_t icon[32 * 32];
    GameInstance* instances;
    struct GameInfo* next;
} GameInfo;

extern GameInfo* g_game_list;

bool get_twilight_save_setting(void);
bool read_rom_header(const char* path, char* out_id, char* out_name);

typedef void (*scan_callback_t)(int folders, int files, const char* current_path);

void scan_all_media(scan_callback_t cb);
bool refresh_slot1(void);
void free_game_list(void);

#endif
