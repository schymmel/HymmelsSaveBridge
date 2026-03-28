#ifndef SNAPSHOT_SAVE_H
#define SNAPSHOT_SAVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <nds.h>
#include <nds/arm9/card.h>
#include "ndsheaderbanner.h" // For sNDSHeaderExt

#define MAX_PATH_LEN 1280
#define GAME_ID_LEN 4
#define TITLE_LEN 12

// Forward declaration from game_manager.h
typedef struct GameInstance GameInstance;

typedef enum {
    SAVE_TYPE_UNKNOWN = 0,
    SAVE_TYPE_SPI_EEPROM,      // Includes 512B, 4KB, 8KB EEPROM
    SAVE_TYPE_SPI_FLASH,       // 128KB-64MB Flash
    SAVE_TYPE_NAND_DSI,        // DSi NAND (e.g., for DSiWare)
    SAVE_TYPE_AUXSPI_IR,       // Infrared carts (e.g., Pokémon HG/SS)
    SAVE_TYPE_GBA_SRAM,        // GBA SRAM (32KB)
    SAVE_TYPE_GBA_EEPROM,      // GBA EEPROM (4KB, 64KB)
    SAVE_TYPE_GBA_FLASH        // GBA Flash (512KB, 1MB)
} SaveType;

typedef struct {
    SaveType type;
    uint32_t size;             // Size in bytes
    char chip_id[8];           // Optional: JEDEC ID for Flash, 0-terminated
    bool requires_encryption;  // True if DSi-enhanced/exclusive and needs decryption
    bool is_gba_save;          // True if it's a GBA save type
} SaveInfo;

typedef struct {
    uint32_t crc32;
    uint32_t timestamp;        // Unix timestamp of backup
    char game_id[GAME_ID_LEN + 1]; // 4-char game ID + null terminator
    char title[TITLE_LEN + 1];     // 12-char game title + null terminator
    uint8_t metadata_version;  // For future format changes
} SaveMetadata;

// Callback for progress updates during long operations
typedef void (*ProgressCb)(uint32_t current, uint32_t total, const char* stage);

// Main API functions
int detect_save_type(SaveInfo* out_info, sNDSHeaderExt* nds_header);
const char* backup_save(const char* dest_path, GameInstance* inst, SaveInfo* info, ProgressCb progress_cb);
const char* restore_save(const char* src_path, GameInstance* inst, SaveInfo* info, ProgressCb progress_cb);
const char* verify_save(const char* src_path, SaveInfo* info);

// Utility for CRC32 calculation
uint32_t calculate_crc32(const void* data, size_t length);

#endif // SNAPSHOT_SAVE_H
