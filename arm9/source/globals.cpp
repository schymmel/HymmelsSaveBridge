#include "globals.h"

uint8 data[0x8000]  = {0};

int ir_delay = 8192;

char device[16] = "/";

char txt[256] = "";

u32 extra_id[EXTRA_ARRAY_SIZE];
u8 extra_size[EXTRA_ARRAY_SIZE];
