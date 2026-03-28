#ifndef SNAPSHOT_SAVE_LOWLEVEL_H
#define SNAPSHOT_SAVE_LOWLEVEL_H

#include <nds.h>
#include <nds/arm9/card.h>
#include "ndsheaderbanner.h"

#ifdef __cplusplus
extern "C" {
#endif

// Low-level functions for save operations
// These wrap the existing libnds/auxspi functions

// Card initialization (simplified - uses libnds cardInit)
int cardInit_save(sNDSHeaderExt* ndsHeader);

// EEPROM read/write (wrapper around libnds functions)
void cardReadEepromSave(u32 address, void *data, u32 length, u32 addrtype);
void cardWriteEepromSave(u32 address, void *data, u32 length, u32 addrtype);

// NAND support is limited - requires GodMode9i's read_card.c for full support
void cardReadSave(u32 src, void* dest, bool nandSave);
void cardWriteNandSave(void* src, u32 dest);

// AuxSPI wrapper functions (C-compatible, no default parameters)
int auxspi_has_extra_save(void);
uint8 auxspi_save_type_save(int extra);
uint32 auxspi_save_size_save(int extra);
void auxspi_read_data_save(uint32 addr, void* buf, uint32 cnt, uint8 type, int extra);
void auxspi_write_data_save(uint32 addr, void *buf, uint32 cnt, uint8 type, int extra);

// Card info functions
u32 cardGetIdSave(void);
u32 cardEepromReadIDSave(void);
u8 cardEepromCommandSave(u8 command);

// NAND-specific variables (for NAND cards)
extern u32 cardNandRomEnd;
extern u32 cardNandRwStart;

#ifdef __cplusplus
}
#endif

#endif // SNAPSHOT_SAVE_LOWLEVEL_H
