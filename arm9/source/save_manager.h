#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include "game_manager.h"
#include <stdbool.h>

const char* create_backup(GameInstance* inst, const char* game_id, const char* custom_name);
const char* restore_backup(GameInstance* inst, const char* backup_file_path);

typedef struct BackupInfo {
    char file_name[1024];
    char full_path[2048];
    struct BackupInfo* next;
} BackupInfo;

BackupInfo* get_backups_for_game(const char* game_id);
void free_backup_list(BackupInfo* list);

#endif
