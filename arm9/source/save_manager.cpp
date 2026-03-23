#include "save_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nds.h>
#include <nds/card.h>
#include <nds/system.h>
#include <nds/arm9/card.h>
#include <algorithm>

#include "auxspi.h"
#include "globals.h"

#define BACKUP_BUFFER_SIZE 32768
#define BACKUP_BASE_DIR "sd:/hsb/backups"

static void create_dir_recursive(const char *dir) {
    char tmp[MAX_PATH_LEN];
    char *p = nullptr;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for(p = tmp + 1; *p; p++)
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}

static const char* copy_file(const char* src_path, const char* dst_path) {
    FILE* src = fopen(src_path, "rb");
    if (!src) return "Failed to open source save for reading";

    FILE* dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return "Failed to open destination for writing";
    }

    char* buffer = (char*)malloc(BACKUP_BUFFER_SIZE);
    if (!buffer) {
        fclose(src);
        fclose(dst);
        return "Out of memory for backup buffer";
    }

    size_t bytes_read;
    const char* error = nullptr;
    while ((bytes_read = fread(buffer, 1, BACKUP_BUFFER_SIZE, src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            error = "Write error during copy";
            break;
        }
        refresh_clock_only();
    }

    free(buffer);
    fclose(src);
    fclose(dst);
    return error;
}

static void progress_bar_begin(const char* label) { }
static void progress_bar_step(uint32_t current, uint32_t total, int* printed) { }
static void progress_bar_finish(int* printed) { }

static const char* backup_slot1(const char* dst_path) {
    if (isDSiMode()) {
        if (REG_SCFG_MC == 0x11) {
            disableSlot1();
            for(int i=0; i<30; i++) swiWaitForVBlank();
            enableSlot1();
            for(int i=0; i<30; i++) swiWaitForVBlank();
        } else if (REG_SCFG_MC == 0x10) {
            disableSlot1();
            for(int i = 0; i < 25; i++) { swiWaitForVBlank(); }
            enableSlot1();
        }
    }
    sysSetCardOwner(BUS_OWNER_ARM9);
    sysSetBusOwners(true, true);

    auxspi_extra card_type = auxspi_has_extra();
    bool auxspi = (card_type == AUXSPI_INFRARED);

    int type = 0;
    uint32_t size = 0;
    int size_log2 = 0;

    if (auxspi) {
        size_log2 = auxspi_save_size_log_2(card_type);
        type = auxspi_save_type(card_type);
        size = (1u << size_log2);
    } else {
        type = cardEepromGetType();
        size = cardEepromGetSize();
    }

    printf("\n  Chip: T:%d S:%lu", type, size);
    if (size == 0 || type == 999 || type == -1) return "Unknown EEPROM size / No cart.";

    FILE* f = fopen(dst_path, "wb");
    if (!f) return "Fail open destination";

    progress_bar_begin("Reading");
    int printed = 0;

    if (auxspi) {
        int size_blocks = 0;
        if(size_log2 < 16)
            size_blocks = 1;
        else
            size_blocks = 1 << (size_log2 - 16);
        u32 chunk_len = std::min(1u << size_log2, 1u << 16);
        u8* buffer = (u8*)malloc(chunk_len * size_blocks);
        if (!buffer) {
            fclose(f);
            return "Out of RAM";
        }
        auxspi_read_data(0, buffer, chunk_len * size_blocks, type, card_type);
        fwrite(buffer, 1, chunk_len * size_blocks, f);
        progress_bar_step(1, 1, &printed);
        free(buffer);
    } else {
        u8* buffer = (u8*)malloc(size);
        if (!buffer) {
            fclose(f);
            return "Out of RAM";
        }
        cardReadEeprom(0, buffer, size, type);
        fwrite(buffer, 1, size, f);
        progress_bar_step(1, 1, &printed);
        free(buffer);
    }

    progress_bar_finish(&printed);
    fclose(f);

    return nullptr;
}

static const char* restore_slot1(const char* src_path) {
    if (isDSiMode()) {
        if (REG_SCFG_MC == 0x11) {
            disableSlot1();
            for(int i=0; i<30; i++) swiWaitForVBlank();
            enableSlot1();
            for(int i=0; i<30; i++) swiWaitForVBlank();
        } else if (REG_SCFG_MC == 0x10) {
            disableSlot1();
            for(int i = 0; i < 25; i++) { swiWaitForVBlank(); }
            enableSlot1();
        }
    }

    sysSetCardOwner(BUS_OWNER_ARM9);
    sysSetBusOwners(true, true);

    auxspi_extra card_type = auxspi_has_extra();
    bool auxspi = (card_type == AUXSPI_INFRARED);

    int type = 0;
    uint32_t size = 0;
    int size_log2 = 0;

    if (auxspi) {
        size_log2 = auxspi_save_size_log_2(card_type);
        type = auxspi_save_type(card_type);
    } else {
        type = cardEepromGetType();
        size = cardEepromGetSize();
    }

    uint32_t num_blocks = 0;
    uint32_t shift = 0;
    uint32_t len = 0;

    if (auxspi) {
        switch (type) {
            case 1: shift = 4; break;
            case 2: shift = 5; break;
            case 3: shift = 8; break;
            default: return "Unknown cartridge save type";
        }
        len = 1u << shift;
        num_blocks = 1u << (size_log2 - shift);
        size = len * num_blocks;
    }

    printf("\n  Chip: Type %d, Size %lu", type, size);
    if (size == 0) return "No Cart detected";
    if (type == -1 || type == 999) return "Unknown save type / NAND saves not supported";

    FILE* f = fopen(src_path, "rb");
    if (!f) return "Failed source open";

    fseek(f, 0, SEEK_END);
    uint32_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size != size) {
        printf("\n  Mismatch: %lu != %lu", file_size, size);
        fclose(f);
        return "Size mismatch";
    }

    if (type == 3) {
        progress_bar_begin("Erasing");
        int erase_printed = 0;
        if (auxspi)
            auxspi_erase(card_type);
        else
            cardEepromChipErase();
        progress_bar_step(1, 1, &erase_printed);
        progress_bar_finish(&erase_printed);
    }

    progress_bar_begin("Writing");
    int write_printed = 0;
    if (auxspi) {
        u8* buffer = new unsigned char[len];
        if (!buffer) {
            fclose(f);
            return "OOM";
        }
        for (uint32_t i = 0; i < num_blocks; i++) {
            if (fread(buffer, 1, len, f) != len) {
                delete[] buffer;
                fclose(f);
                return "Read fail";
            }
            auxspi_write_data(i << shift, buffer, len, type, card_type);
            progress_bar_step(i + 1, num_blocks, &write_printed);
            refresh_clock_only();
        }
        delete[] buffer;
    } else {
        int blocks = size / 32;
        int written = 0;
        u8* buffer = new unsigned char[blocks];
        if (!buffer) {
            fclose(f);
            return "OOM";
        }
        for (int i = 0; i < 32; i++) {
            if (fread(buffer, 1, blocks, f) != (size_t)blocks) {
                delete[] buffer;
                fclose(f);
                return "Read fail";
            }
            cardWriteEeprom(written, buffer, blocks, type);
            written += blocks;
            progress_bar_step(i + 1, 32, &write_printed);
            refresh_clock_only();
        }
        delete[] buffer;
    }

    progress_bar_finish(&write_printed);

    progress_bar_begin("Verifying");
    fseek(f, 0, SEEK_SET);
    int verify_printed = 0;
    u8* v_buf = (u8*)malloc(0x1000);
    u8* c_buf = (u8*)malloc(0x1000);
    uint32_t verified = 0;
    const char* v_err = nullptr;

    while(verified < size) {
        uint32_t chunk = (size - verified > 0x1000) ? 0x1000 : (size - verified);
        if (fread(v_buf, 1, chunk, f) != chunk) { v_err = "Verify Read Fail"; break; }

        if (auxspi) {
            auxspi_read_data(verified, c_buf, chunk, type, card_type);
        } else {
            cardReadEeprom(verified, c_buf, chunk, type);
        }

        if (memcmp(v_buf, c_buf, chunk) != 0) {
            v_err = "Verify Failed (Data mismatch)";
            break;
        }
        verified += chunk;
        progress_bar_step(verified, size, &verify_printed);
        refresh_clock_only();
    }

    free(v_buf);
    free(c_buf);
    fclose(f);

    if (v_err) return v_err;
    return nullptr;
}

const char* create_backup(GameInstance* inst, const char* game_id, const char* custom_name) {
    if (!inst || !game_id || !custom_name) return "Invalid argument";

    char backup_dir[MAX_PATH_LEN];
    snprintf(backup_dir, sizeof(backup_dir), "%s/%s", BACKUP_BASE_DIR, game_id);
    create_dir_recursive(backup_dir);

    char dst_path[MAX_PATH_LEN * 2];
    snprintf(dst_path, sizeof(dst_path), "%s/%s.sav", backup_dir, custom_name);

    if (inst->is_slot1) {
        return backup_slot1(dst_path);
    } else {
        return copy_file(inst->save_path, dst_path);
    }
}

const char* restore_backup(GameInstance* inst, const char* backup_file_path) {
    if (!inst || !backup_file_path) return "Invalid argument";

    if (inst->is_slot1) {
        return restore_slot1(backup_file_path);
    } else {
        return copy_file(backup_file_path, inst->save_path);
    }
}

BackupInfo* get_backups_for_game(const char* game_id) {
    if (!game_id) return nullptr;

    char backup_dir[MAX_PATH_LEN];
    snprintf(backup_dir, sizeof(backup_dir), "%s/%s", BACKUP_BASE_DIR, game_id);

    DIR* dir = opendir(backup_dir);
    if (!dir) return nullptr;

    BackupInfo* head = nullptr;
    BackupInfo* tail = nullptr;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        const char* ext = strrchr(entry->d_name, '.');
        if (ext && strcasecmp(ext, ".sav") == 0) {
            BackupInfo* b = (BackupInfo*)calloc(1, sizeof(BackupInfo));
            if (!b) break;
            snprintf(b->file_name, sizeof(b->file_name), "%s", entry->d_name);
            snprintf(b->full_path, sizeof(b->full_path), "%s/%s", backup_dir, entry->d_name);

            if (!head) {
                head = b;
                tail = b;
            } else {
                tail->next = b;
                tail = b;
            }
        }
    }
    closedir(dir);
    return head;
}

void free_backup_list(BackupInfo* list) {
    while (list) {
        BackupInfo* next = list->next;
        free(list);
        list = next;
    }
}
