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
#include "read_card.h"
#include "snapshot_save.h"
#include "snapshot_save_lowlevel.h"

#define BACKUP_BUFFER_SIZE 32768
#define BACKUP_BASE_DIR "sd:/_nds/snapshot/backups"

// libnds-compatible EEPROM functions (to ensure correct behavior regardless of SDK)
// Taken from https://github.com/devkitPro/libnds/blob/master/source/common/cardEeprom.c

#define REG_AUXSPICNT_SNAPSHOT  (*(vu16*)0x040001A0)
#define REG_AUXSPIDATA_SNAPSHOT (*(vu8*)0x040001A2)
#define CARD_SPI_BUSY_SNAPSHOT  (1<<7)

static inline void eepromWaitBusySnapshot() {
    while (REG_AUXSPICNT_SNAPSHOT & CARD_SPI_BUSY_SNAPSHOT);
}

static void cardReadEepromSnapshot(u32 address, u8 *data, u32 length, u32 addrtype) {
    REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
    REG_AUXSPIDATA_SNAPSHOT = 0x03 | ((addrtype == 1) ? address>>8<<3 : 0);
    eepromWaitBusySnapshot();

    if (addrtype == 3) {
        REG_AUXSPIDATA_SNAPSHOT = (address >> 16) & 0xFF;
        eepromWaitBusySnapshot();
        REG_AUXSPIDATA_SNAPSHOT = (address >> 8) & 0xFF;
        eepromWaitBusySnapshot();
    } else if (addrtype == 2) {
        REG_AUXSPIDATA_SNAPSHOT = (address >> 8) & 0xFF;
        eepromWaitBusySnapshot();
    }

    REG_AUXSPIDATA_SNAPSHOT = (address) & 0xFF;
    eepromWaitBusySnapshot();

    while (length > 0) {
        REG_AUXSPIDATA_SNAPSHOT = 0;
        eepromWaitBusySnapshot();
        *data++ = REG_AUXSPIDATA_SNAPSHOT;
        length--;
    }

    eepromWaitBusySnapshot();
    REG_AUXSPICNT_SNAPSHOT = 0x40;
}

static void cardWriteEepromSnapshot(u32 address, u8 *data, u32 length, u32 addrtype) {
    u32 address_end = address + length;
    int i;
    int maxblocks = 32;
    if(addrtype == 1) maxblocks = 16;
    if(addrtype == 2) maxblocks = 32;
    if(addrtype == 3) maxblocks = 256;

    while (address < address_end) {
        // set WEL (Write Enable Latch)
        REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
        REG_AUXSPIDATA_SNAPSHOT = 0x06;
        eepromWaitBusySnapshot();
        REG_AUXSPICNT_SNAPSHOT = 0x40;

        // program
        REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;

        if(addrtype == 1) {
            REG_AUXSPIDATA_SNAPSHOT = 0x02 | (address & BIT(8)) >> (8-3);
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = address & 0xFF;
            eepromWaitBusySnapshot();
        }
        else if(addrtype == 2) {
            REG_AUXSPIDATA_SNAPSHOT = 0x02;
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = (address >> 8) & 0xFF;
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = address & 0xFF;
            eepromWaitBusySnapshot();
        }
        else if(addrtype == 3) {
            REG_AUXSPIDATA_SNAPSHOT = 0x02;
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = (address >> 16) & 0xFF;
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = (address >> 8) & 0xFF;
            eepromWaitBusySnapshot();
            REG_AUXSPIDATA_SNAPSHOT = address & 0xFF;
            eepromWaitBusySnapshot();
        }

        // Write until end of data OR end of page (important for all types)
        for (i=0; address<address_end && i<maxblocks; i++, address++) {
            REG_AUXSPIDATA_SNAPSHOT = *data++;
            eepromWaitBusySnapshot();
            // Page boundary checks: Type 1: 16b, Type 2: 32b, Type 3: 256b
            if (addrtype == 1 && (address & 0x0F) == 0) break;
            if (addrtype == 2 && (address & 0x1F) == 0) break;
            if (addrtype == 3 && (address & 0xFF) == 0) break;
        }
        REG_AUXSPICNT_SNAPSHOT = 0x40;

        // wait programming to finish
        REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
        REG_AUXSPIDATA_SNAPSHOT = 0x05;
        eepromWaitBusySnapshot();
        do {
            REG_AUXSPIDATA_SNAPSHOT = 0;
            eepromWaitBusySnapshot();
        } while (REG_AUXSPIDATA_SNAPSHOT & 0x01); // WIP (Write In Progress)
        eepromWaitBusySnapshot();
        REG_AUXSPICNT_SNAPSHOT = 0x40;
    }
}

static void cardEepromSectorEraseSnapshot(u32 address) {
    // set WEL (Write Enable Latch)
    REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
    REG_AUXSPIDATA_SNAPSHOT = 0x06;
    eepromWaitBusySnapshot();
    REG_AUXSPICNT_SNAPSHOT = 0x40;

    // SectorErase 0xD8
    REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
    REG_AUXSPIDATA_SNAPSHOT = 0xD8;
    eepromWaitBusySnapshot();
    REG_AUXSPIDATA_SNAPSHOT = (address >> 16) & 0xFF;
    eepromWaitBusySnapshot();
    REG_AUXSPIDATA_SNAPSHOT = (address >> 8) & 0xFF;
    eepromWaitBusySnapshot();
    REG_AUXSPIDATA_SNAPSHOT = address & 0xFF;
    eepromWaitBusySnapshot();
    REG_AUXSPICNT_SNAPSHOT = 0x40;

    // wait erase to finish
    REG_AUXSPICNT_SNAPSHOT = 0x8000 | 0x2000 | 0x40;
    REG_AUXSPIDATA_SNAPSHOT = 0x05;
    eepromWaitBusySnapshot();
    do {
        REG_AUXSPIDATA_SNAPSHOT = 0;
        eepromWaitBusySnapshot();
    } while (REG_AUXSPIDATA_SNAPSHOT & 0x01); // WIP
    REG_AUXSPICNT_SNAPSHOT = 0x40;
}

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

typedef struct Slot1SaveInfo {
    bool auxspi;
    bool nand;
    int type;
    uint32_t size;
    int size_log2;
    auxspi_extra card_type;
    sNDSHeaderExt header;
} Slot1SaveInfo;

static void prepare_slot1_bus(void) {
    if (isDSiMode()) {
        if (REG_SCFG_MC == 0x11) {
            disableSlot1();
            for (int i = 0; i < 30; i++) swiWaitForVBlank();
            enableSlot1();
            for (int i = 0; i < 30; i++) swiWaitForVBlank();
        } else if (REG_SCFG_MC == 0x10) {
            disableSlot1();
            for (int i = 0; i < 25; i++) swiWaitForVBlank();
            enableSlot1();
        }
    }

    sysSetCardOwner(BUS_OWNER_ARM9);
    sysSetBusOwners(true, true);
}

static int cardEepromGetTypeFixed(void) {
    int sr = cardEepromCommand(0x05);
    int id = (int)cardEepromReadID();

    if ((sr == 0xff && id == 0xffffff) || (sr == 0 && id == 0)) return -1;
    if (sr == 0xf0 && id == 0xffffff) return 1;
    if (sr == 0x00 && id == 0xffffff) return 2;
    if (id != 0xffffff || (sr == 0x02 && id == 0xffffff)) return 3;
    return 0;
}

static uint32_t cardEepromGetSizeFixed(void) {
    int type = cardEepromGetTypeFixed();

    if (type == -1) return 0;
    if (type == 0) return 8192;
    if (type == 1) return 512;

    if (type == 2) {
        uint32_t original_word = 0;
        uint32_t probe_word = 0;
        uint32_t probe_value = 0x54534554;

        cardReadEepromSnapshot(0, (u8*)&original_word, sizeof(original_word), type);
        cardWriteEepromSnapshot(0, (u8*)&probe_value, sizeof(probe_value), type);

        uint32_t size = 8192;
        while (1) {
            cardReadEepromSnapshot(size, (u8*)&probe_word, sizeof(probe_word), type);
            if (probe_word == probe_value) {
                uint32_t second_probe = 0x74736574;
                cardWriteEepromSnapshot(0, (u8*)&second_probe, sizeof(second_probe), type);
                cardReadEepromSnapshot(size, (u8*)&probe_word, sizeof(probe_word), type);
                if (probe_word == second_probe) break;
                cardWriteEepromSnapshot(0, (u8*)&probe_value, sizeof(probe_value), type);
            }
            size += 8192;
            if (size >= 128 * 1024) break; // Safety limit
        }

        cardWriteEepromSnapshot(0, (u8*)&original_word, sizeof(original_word), type);
        return size;
    }

    if (type == 3) {
        uint32_t id = cardEepromReadID();
        uint16_t device = id & 0xffff;

        if (((id >> 16) & 0xff) == 0x20) {
            switch (device) {
                case 0x4014: return 1024 * 1024;
                case 0x4013:
                case 0x8013: return 512 * 1024;
                case 0x2017: return 8 * 1024 * 1024;
                default: return 256 * 1024;
            }
        }

        if (((id >> 16) & 0xff) == 0x62 && device == 0x1100) return 512 * 1024;

        if (((id >> 16) & 0xff) == 0xC2) {
            switch (device) {
                case 0x2211: return 128 * 1024;
                case 0x2017: return 8 * 1024 * 1024;
                default: return 256 * 1024;
            }
        }

        if (id == 0xffffff && cardEepromCommand(0x05) == 2) return 128 * 1024;
        return 256 * 1024;
    }

    return 0;
}

static void cardEepromChipEraseFixed(void) {
    int sz = cardEepromGetSizeFixed();
    for (int sector = 0; sector < sz; sector += 0x10000) {
        cardEepromSectorEraseSnapshot(sector);
    }
}

static uint32_t cardNandGetSaveSize(const sNDSHeaderExt* nds_header) {
    uint32_t game_code = *(const uint32_t*)nds_header->gameCode & 0x00FFFFFF;

    switch (game_code) {
        case 0x00425855: return 8 << 20;
        case 0x00524F55: return 16 << 20;
        case 0x004B5355: return 64 << 20;
        case 0x00444755: return 128 << 20;
        default: return 0;
    }
}

static const char* detect_slot1_save(Slot1SaveInfo* info) {
    if (!info) return "Invalid save descriptor";

    memset(info, 0, sizeof(*info));
    prepare_slot1_bus();

    // First check if it's an AuxSPI cartridge (infrared, BBDX, etc.) - do NOT call gm9CardInit for this!
    info->card_type = auxspi_has_extra();
    // Any extra hardware type except DEFAULT and FLASH_CARD is AuxSPI
    info->auxspi = (info->card_type != AUXSPI_DEFAULT && info->card_type != AUXSPI_FLASH_CARD);


    if (info->auxspi) {
        info->size_log2 = auxspi_save_size_log_2(info->card_type);
        info->type = auxspi_save_type(info->card_type);
        if (info->size_log2 <= 0 || info->type <= 0) return "Unknown AuxSPI save type";
        info->size = 1u << info->size_log2;
        return nullptr;
    }

    // Check EEPROM type first (GodMode9i does this BEFORE cardInit for SPI saves)
    info->type = cardEepromGetTypeFixed();
    
    if (info->type == -1) {
        // NAND save - needs full card init
        info->nand = true;
        if (gm9CardInit(&info->header) != 0) return "Failed to initialize cartridge";
        info->size = cardNandGetSaveSize(&info->header);
        if (info->size == 0) return "Unsupported NAND save cartridge";
        return nullptr;
    }

    // Regular SPI save - NO gm9CardInit needed (would mess up SPI state)
    info->size = cardEepromGetSizeFixed();
    if (info->size == 0 || info->type == 999) return "Unknown cartridge save type";
    return nullptr;
}

static const char* backup_slot1(const char* dst_path) {
    Slot1SaveInfo info;
    const char* err = detect_slot1_save(&info);
    if (err) return err;

    FILE* f = fopen(dst_path, "wb");
    if (!f) return "Fail open destination";

    progress_bar_begin("Reading");
    int printed = 0;

    if (info.nand) {
        u8* buffer = (u8*)malloc(0x8000);
        if (!buffer) {
            fclose(f);
            return "Out of RAM";
        }

        for (uint32_t src = 0; src < info.size; src += 0x8000) {
            uint32_t write_size = (info.size - src >= 0x8000) ? 0x8000 : (info.size - src);
            
            // Prepare bus before cartridge read
            sysSetCardOwner(BUS_OWNER_ARM9);
            sysSetBusOwners(true, true);
            
            for (uint32_t offset = 0; offset < write_size; offset += 0x200) {
                gm9CardRead(cardNandRwStart + src + offset, buffer + offset, true);
            }
            if (fwrite(buffer, 1, write_size, f) != write_size) {
                free(buffer);
                fclose(f);
                return "Write error during NAND backup";
            }
            progress_bar_step(src + write_size, info.size, &printed);
            refresh_clock_only();
        }

        free(buffer);
    } else if (info.auxspi) {
        int size_blocks = 0;
        if (info.size_log2 < 16)
            size_blocks = 1;
        else
            size_blocks = 1 << (info.size_log2 - 16);
        u32 chunk_len = std::min(1u << info.size_log2, 1u << 16);
        u8* buffer = (u8*)malloc(chunk_len * size_blocks);
        if (!buffer) {
            fclose(f);
            return "Out of RAM";
        }
        sysSetCardOwner(BUS_OWNER_ARM9);
        sysSetBusOwners(true, true);
        auxspi_read_data(0, buffer, chunk_len * size_blocks, info.type, info.card_type);
        fwrite(buffer, 1, chunk_len * size_blocks, f);
        progress_bar_step(1, 1, &printed);
        free(buffer);
    } else {
        u8* buffer = (u8*)malloc(info.size);
        if (!buffer) {
            fclose(f);
            return "Out of RAM";
        }
        sysSetCardOwner(BUS_OWNER_ARM9);
        sysSetBusOwners(true, true);
        cardReadEepromSnapshot(0, buffer, info.size, info.type);
        fwrite(buffer, 1, info.size, f);
        progress_bar_step(1, 1, &printed);
        free(buffer);
    }

    progress_bar_finish(&printed);
    fclose(f);

    return nullptr;
}

static const char* restore_slot1(const char* src_path) {
    Slot1SaveInfo info;
    const char* err = detect_slot1_save(&info);
    if (err) return err;

    uint32_t num_blocks = 0;
    uint32_t shift = 0;
    uint32_t len = 0;

    if (info.auxspi) {
        switch (info.type) {
            case 1: shift = 4; break;
            case 2: shift = 5; break;
            case 3: shift = 8; break;
            default: return "Unknown cartridge save type";
        }
        len = 1u << shift;
        num_blocks = 1u << (info.size_log2 - shift);
        info.size = len * num_blocks;
    }

    if (info.size == 0) return "No Cart detected";

    FILE* f = fopen(src_path, "rb");
    if (!f) return "Failed source open";

    fseek(f, 0, SEEK_END);
    uint32_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size != info.size) {
        fclose(f);
        return "Size mismatch";
    }

    // Re-prepare bus after file operations (SD card access may have changed bus state)
    sysSetCardOwner(BUS_OWNER_ARM9);
    sysSetBusOwners(true, true);

    if (info.type == 3 && !info.nand) {
        progress_bar_begin("Erasing");
        int erase_printed = 0;
        if (info.auxspi)
            auxspi_erase(info.card_type);
        else
            cardEepromChipEraseFixed();
        progress_bar_step(1, 1, &erase_printed);
        progress_bar_finish(&erase_printed);
    }

    progress_bar_begin("Writing");
    int write_printed = 0;
    if (info.nand) {
        u8* buffer = (u8*)malloc(0x8000);
        u8* verify = (u8*)malloc(0x8000);
        if (!buffer || !verify) {
            free(buffer);
            free(verify);
            fclose(f);
            return "OOM";
        }

        for (uint32_t dest = 0; dest < info.size; dest += 0x8000) {
            uint32_t chunk = (info.size - dest >= 0x8000) ? 0x8000 : (info.size - dest);
            if (fread(buffer, 1, chunk, f) != chunk) {
                free(buffer);
                free(verify);
                fclose(f);
                return "Read fail";
            }

            // Re-prepare bus after SD card access
            sysSetCardOwner(BUS_OWNER_ARM9);
            sysSetBusOwners(true, true);

            for (uint32_t offset = 0; offset < chunk; offset += 0x800) {
                gm9CardWriteNand(buffer + offset, cardNandRwStart + dest + offset);
            }

            for (uint32_t offset = 0; offset < chunk; offset += 0x200) {
                gm9CardRead(cardNandRwStart + dest + offset, verify + offset, true);
            }

            if (memcmp(buffer, verify, chunk) != 0) {
                free(buffer);
                free(verify);
                fclose(f);
                return "Verify failed";
            }

            progress_bar_step(dest + chunk, info.size, &write_printed);
            refresh_clock_only();
        }

        free(buffer);
        free(verify);
    } else if (info.auxspi) {
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
            auxspi_write_data(i << shift, buffer, len, info.type, info.card_type);
            progress_bar_step(i + 1, num_blocks, &write_printed);
            refresh_clock_only();
        }
        delete[] buffer;
    } else {
        int blocks = info.size / 32;
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
            // Re-prepare bus after SD card access
            sysSetCardOwner(BUS_OWNER_ARM9);
            sysSetBusOwners(true, true);
            cardWriteEepromSnapshot(written, buffer, blocks, info.type);
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

    while(verified < info.size) {
        uint32_t chunk = (info.size - verified > 0x1000) ? 0x1000 : (info.size - verified);
        if (fread(v_buf, 1, chunk, f) != chunk) { v_err = "Verify Read Fail"; break; }

        // Re-prepare bus after SD card access
        // sysSetCardOwner(BUS_OWNER_ARM9);
        // sysSetBusOwners(true, true);

        if (info.nand) {
            for (uint32_t offset = 0; offset < chunk; offset += 0x200) {
                gm9CardRead(cardNandRwStart + verified + offset, c_buf + offset, true);
            }
        } else if (info.auxspi) {
            auxspi_read_data(verified, c_buf, chunk, info.type, info.card_type);
        } else {
            cardReadEepromSnapshot(verified, c_buf, chunk, info.type);
        }

        if (memcmp(v_buf, c_buf, chunk) != 0) {
            v_err = "Verify Failed (Data mismatch)";
            break;
        }
        verified += chunk;
        progress_bar_step(verified, info.size, &verify_printed);
        refresh_clock_only();
    }

    free(v_buf);
    free(c_buf);
    fclose(f);

    if (v_err) return v_err;
    return nullptr;
}

// create_backup and restore_backup are defined later using enhanced snapshot_save system

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

// New save system functions using the snapshot_save API

// Enhanced backup function using the new snapshot_save system
const char* create_backup_enhanced(GameInstance* inst, const char* game_id, const char* custom_name) {
    if (!inst || !game_id || !custom_name) return "Invalid argument";

    char backup_dir[MAX_PATH_LEN];
    snprintf(backup_dir, sizeof(backup_dir), "%s/%s", BACKUP_BASE_DIR, game_id);
    create_dir_recursive(backup_dir);

    char dst_path[MAX_PATH_LEN * 2];
    snprintf(dst_path, sizeof(dst_path), "%s/%s.sav", backup_dir, custom_name);

    if (inst->is_slot1) {
        // Use the existing backup_slot1 function which works with libnds
        return backup_slot1(dst_path);
    } else {
        // For non-slot1 games, use the existing file copy method
        return copy_file(inst->save_path, dst_path);
    }
}

// Enhanced restore function using the new snapshot_save system
const char* restore_backup_enhanced(GameInstance* inst, const char* backup_file_path) {
    if (!inst || !backup_file_path) return "Invalid argument";

    if (inst->is_slot1) {
        // Use the existing restore_slot1 function which works with libnds
        return restore_slot1(backup_file_path);
    } else {
        // For non-slot1 games, use the existing file copy method
        return copy_file(backup_file_path, inst->save_path);
    }
}

// Replace the original functions with our enhanced versions
const char* create_backup(GameInstance* inst, const char* game_id, const char* custom_name) {
    return create_backup_enhanced(inst, game_id, custom_name);
}

const char* restore_backup(GameInstance* inst, const char* backup_file_path) {
    return restore_backup_enhanced(inst, backup_file_path);
}
