#include <nds.h>

#include "globals.h"

// Force correct register addresses (same as libnds/devkitPro)
#undef REG_AUXSPICNT
#undef REG_AUXSPIDATA
#define REG_AUXSPICNT  (*(vu16*)0x040001A0)
#define REG_AUXSPIDATA (*(vu8*)0x040001A2)

// Custom delay function to match libnds behavior
static inline void auxspi_delay(u32 cycles) {
    // Simple busy loop delay
    for (volatile u32 i = 0; i < cycles; i++) {
        __asm__ volatile ("nop");
    }
}

inline void auxspi_wait_busy() {
	while (REG_AUXSPICNT & 0x80);
}

inline void auxspi_wait_wip() {
	do { REG_AUXSPIDATA = 0; auxspi_wait_busy(); } while (REG_AUXSPIDATA & 0x01);
}

inline void auxspi_open(uint8 device) {
	REG_AUXSPICNT = 0xa040 | (device & 3);
	auxspi_wait_busy();
}

inline void auxspi_close() {
	REG_AUXSPIDATA = 0;
	auxspi_wait_busy();
	REG_AUXSPICNT = 0;
	auxspi_wait_busy();
}

inline void auxspi_close_lite() {
	REG_AUXSPICNT = 0x40;
	auxspi_wait_busy();
}

inline uint8 auxspi_transfer(uint8 out) {
	REG_AUXSPIDATA = out;
	auxspi_wait_busy();
	return REG_AUXSPIDATA;
}

inline void auxspi_write(uint8 out) {
	auxspi_transfer(out);
}

inline uint8 auxspi_read() {
	return auxspi_transfer(0);
}

inline uint16 auxspi_read_16() {
	REG_AUXSPIDATA = 0;
	auxspi_wait_busy();
	return REG_AUXSPIDATA;
}

inline void auxspi_disable_infrared_core() {
	auxspi_open(0);
	auxspi_delay(1200);
	auxspi_open(2);
	auxspi_write(0);
	auxspi_delay(1200);
}
