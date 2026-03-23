#include <nds.h>

int main(int argc, char **argv) {
    readUserSettings();
    irqInit();
    fifoInit();
    installSystemFIFO();
    installSoundFIFO();
    // Setup RTC IRQ to update the time structure used by ARM9
    initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);
    irqEnable(IRQ_VBLANK);
    while (1) {
        swiWaitForVBlank();
    }
}
