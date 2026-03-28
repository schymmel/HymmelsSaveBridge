#include "snapshot_save_lowlevel.h"
#include <nds/arm9/card.h>
#include <string.h>
#include "read_card.h"

int cardInit_save(sNDSHeaderExt* ndsHeader) {
    return gm9CardInit(ndsHeader);
}

// EEPROM read/write - wraps libnds functions
void cardReadEepromSave(u32 address, void *data, u32 length, u32 addrtype) {
    cardReadEeprom(address, (u8*)data, length, addrtype);
}

void cardWriteEepromSave(u32 address, void *data, u32 length, u32 addrtype) {
    cardWriteEeprom(address, (u8*)data, length, addrtype);
}

void cardReadSave(u32 src, void* dest, bool nandSave) {
    gm9CardRead(src, dest, nandSave);
}

void cardWriteNandSave(void* src, u32 dest) {
    gm9CardWriteNand(src, dest);
}

u32 cardGetIdSave(void) {
    return gm9CardGetId();
}

u32 cardEepromReadIDSave(void) {
    return cardEepromReadID();
}

u8 cardEepromCommandSave(u8 command) {
    return cardEepromCommand(command);
}

// AuxSPI wrapper functions (C-compatible, no default parameters)
int auxspi_has_extra_save(void) {
    // Stub - auxspi_has_extra requires C++ linkage
    return 0; // AUXSPI_DEFAULT
}

uint8 auxspi_save_type_save(int extra) {
    // Stub - auxspi_save_type requires C++ linkage
    (void)extra;
    return 0;
}

uint32 auxspi_save_size_save(int extra) {
    // Stub - auxspi_save_size requires C++ linkage
    (void)extra;
    return 0;
}

void auxspi_read_data_save(uint32 addr, void* buf, uint32 cnt, uint8 type, int extra) {
    // Stub - auxspi_read_data requires C++ linkage
    (void)addr; (void)buf; (void)cnt; (void)type; (void)extra;
    memset(buf, 0, cnt);
}

void auxspi_write_data_save(uint32 addr, void *buf, uint32 cnt, uint8 type, int extra) {
    // Stub - auxspi_write_data requires C++ linkage
    (void)addr; (void)buf; (void)cnt; (void)type; (void)extra;
}
