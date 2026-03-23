#ifndef GLOBALS_H
#define GLOBALS_H

#include <nds.h>
#include "auxspi.h"

extern uint8 data[0x8000];

extern int ir_delay;

extern char device[16];

extern char txt[256];

#define EXTRA_ARRAY_SIZE 16

extern u32 extra_id[EXTRA_ARRAY_SIZE];
extern u8 extra_size[EXTRA_ARRAY_SIZE];

#ifdef __cplusplus
extern "C" {
#endif
void refresh_clock_only(void);
bool take_screenshot(void);
#ifdef __cplusplus
}
#endif

extern int g_bgTopImage;
extern int g_bgTopIcons;
extern int g_bgBottom;

#endif
