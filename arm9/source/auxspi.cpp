#include "auxspi.h"
#include "globals.h"

#include <algorithm>
#include <stdio.h>

using std::max;

#include "auxspi_core.inc"

uint8 jedec_table(uint32 id) {
	switch (id) {
	case 0x204012:
	case 0x621600:
		return 0x12;

	case 0x204013:
	case 0x621100:
		return 0x13;

	case 0x204014:
		return 0x14;

	case 0x204015:
		return 0x15;

	case 0x202017:
	case 0x204017:
		return 0x17;
	default: {
		for (int i = 0; i < EXTRA_ARRAY_SIZE; i++) {
			if (extra_id[i] == id)
				return extra_size[i];
		}
		return 0;
	}
	};
}

uint8 type2_size(auxspi_extra extra) {
	static const uint32 offset0 = (8*1024-1);
	static const uint32 offset1 = (2*8*1024-1);
	u8 buf1;
	u8 buf2;
	u8 buf3;
	u8 buf4;
	auxspi_read_data(offset0, &buf1, 1, 2, extra);
	auxspi_read_data(offset1, &buf2, 1, 2, extra);
	buf3=~buf1;
	auxspi_write_data(offset0, &buf3, 1, 2, extra);
	auxspi_read_data (offset1, &buf4, 1, 2, extra);
	auxspi_write_data(offset0, &buf1, 1, 2, extra);
	if(buf4!=buf2)
		return 0x0d;
	else
		return 0x10;
}

uint8 auxspi_save_type(auxspi_extra extra) {
	uint32 jedec = auxspi_save_jedec_id(extra);
	int8 sr = auxspi_save_status_register(extra);

	if ((sr & 0xfd) == 0xF0 && (jedec == 0x00ffffff)) return 1;
	if ((sr & 0xfd) == 0x00 && (jedec == 0x00ffffff)) return 2;
	if ((sr & 0xfd) == 0x00 && (jedec != 0x00ffffff)) return 3;

	return 0;
}

uint32 auxspi_save_size(auxspi_extra extra) {
	return 1 << auxspi_save_size_log_2(extra);
}

uint8 auxspi_save_size_log_2(auxspi_extra extra) {
	uint8 type = auxspi_save_type(extra);
	switch (type) {
	case 1:
		return 0x09;
		break;
	case 2:
		return type2_size(extra);
		break;
	case 3:
		return jedec_table(auxspi_save_jedec_id(extra));
		break;
	default:
		return 0;
	}
}

uint32 auxspi_save_jedec_id(auxspi_extra extra) {
	uint32 id = 0;
	if (extra)
		auxspi_disable_extra(extra);

	auxspi_open(0);
	auxspi_write(0x9f);
	id |= auxspi_read() << 16;
	id |= auxspi_read() << 8;
	id |= auxspi_read();
	auxspi_close();

	return id;
}

uint8 auxspi_save_status_register(auxspi_extra extra) {
	uint8 sr = 0;
	if (extra)
		auxspi_disable_extra(extra);
	auxspi_open(0);
	auxspi_write(0x05);
	sr = auxspi_read();
	auxspi_close();
	return sr;
}

void auxspi_read_data(uint32 addr, uint8* buf, uint32 cnt, uint8 type, auxspi_extra extra) {
	if (type == 0)
		type = auxspi_save_type(extra);
	if (type == 0)
		return;
	if (extra)
		auxspi_disable_extra(extra);
	auxspi_open(0);
	auxspi_write(0x03 | ((type == 1) ? addr>>8<<3 : 0));
    if (type == 3) {
		auxspi_write((addr >> 16) & 0xFF);
    }
    if (type >= 2) {
		auxspi_write((addr >> 8) & 0xFF);
    }
	auxspi_write(addr & 0xFF);
    while (cnt > 0) {
        *buf++ = auxspi_read();
        cnt--;
    }
	auxspi_close();
}

void auxspi_write_data(uint32 addr, uint8 *buf, uint32 cnt, uint8 type, auxspi_extra extra) {
	if (type == 0)
		type = auxspi_save_type();
	if (type == 0)
		return;

	uint32 addr_end = addr + cnt;
	unsigned int i;
    unsigned int maxblocks = 32;
    if(type == 1) maxblocks = 16;
    if(type == 2) maxblocks = 32;
    if(type == 3) maxblocks = 256;

	while (addr < addr_end) {
		if (extra)
			auxspi_disable_extra(extra);

		auxspi_open(0);

		auxspi_write(0x06);
		auxspi_close_lite();

		if (extra)
			auxspi_disable_extra(extra);

		auxspi_open(0);

        if(type == 1) {
			auxspi_write(0x02 | (addr & BIT(8)) >> (8-3));
			auxspi_write(addr & 0xFF);
        }
        else if(type == 2) {
			auxspi_write(0x02);
            auxspi_write((addr >> 8) & 0xff);
            auxspi_write(addr & 0xFF);
        }
        else if(type == 3) {
			auxspi_write(0x02);
            auxspi_write((addr >> 16) & 0xff);
            auxspi_write((addr >> 8) & 0xff);
            auxspi_write(addr & 0xFF);
        }

		for (i=0; addr < addr_end && i < maxblocks; i++, addr++) {
			auxspi_write(*buf++);
        }

		auxspi_close_lite();

		if (extra)
			auxspi_disable_extra(extra);

		auxspi_open(0);
		auxspi_write(5);
		auxspi_wait_wip();
        auxspi_wait_busy();
		auxspi_close();
}
}

void auxspi_disable_extra(auxspi_extra extra) {
	switch (extra) {
		case AUXSPI_INFRARED:
			auxspi_disable_infrared_core();
			break;
		case AUXSPI_BBDX:

			auxspi_disable_big_protection();
			break;
		case AUXSPI_BLUETOOTH:

			break;
		default:;
	}
	swiWaitForVBlank();
}

void auxspi_disable_infrared() {
	auxspi_disable_infrared_core();
}

void auxspi_disable_big_protection() {
	static bool doonce = false;
	if (doonce)
		return;
	doonce = true;
	sysSetBusOwners(true, true);

	auxspi_open(3);
	auxspi_write(0xf1);
	auxspi_wait_busy();
	auxspi_close_lite();

	auxspi_open(3);
	auxspi_write(0x6);
	auxspi_wait_busy();
	auxspi_close_lite();

	auxspi_open(3);
	auxspi_write(0xfa);
	auxspi_wait_busy();
	auxspi_write(0x1);
	auxspi_wait_busy();
	auxspi_write(0x31);
	auxspi_wait_busy();
	auxspi_close();

	auxspi_open(3);
	auxspi_write(0x14);
	auxspi_wait_busy();
	auxspi_close_lite();

	auxspi_open(3);
	auxspi_write(0x6);
	auxspi_wait_busy();
	auxspi_close_lite();

	auxspi_open(3);
	auxspi_write(0xf8);
	auxspi_wait_busy();
	auxspi_write(0x1);
	auxspi_wait_busy();
	auxspi_write(0x0);
	auxspi_wait_busy();
	auxspi_close();

	auxspi_open(3);
	auxspi_write(0xe);
	auxspi_wait_busy();
	auxspi_close();
}

auxspi_extra auxspi_has_extra() {
	sysSetBusOwners(true, true);

	u8 size2 = auxspi_save_size_log_2(AUXSPI_INFRARED);
	if (size2 > 0)
		return AUXSPI_INFRARED;

	u8 size1 = auxspi_save_size_log_2();
	if (size1 > 0)
		return AUXSPI_DEFAULT;

#if 0

	uint32 jedec = auxspi_save_jedec_id();

	if (jedec == 0x00ffffff)
		return AUXSPI_BBDX;
#endif

	return AUXSPI_FLASH_CARD;
}

void auxspi_erase(auxspi_extra extra) {
	uint8 type = auxspi_save_type(extra);
	if (type == 3) {
		uint32 size;
		size = 1 << (auxspi_save_size_log_2(extra) - 16);
		for (unsigned int i = 0; i < size; i++) {
			if (extra)
				auxspi_disable_extra(extra);
			auxspi_open(0);

			auxspi_write(0x06);
			auxspi_close_lite();

			if (extra)
				auxspi_disable_extra(extra);
			auxspi_open(0);
			auxspi_write(0xd8);
			auxspi_write(i);
			auxspi_write(0);
			auxspi_write(0);
			auxspi_close_lite();

			if (extra)
				auxspi_disable_extra(extra);
			auxspi_open(0);
			auxspi_write(5);
			auxspi_wait_wip();
			auxspi_wait_busy();
			auxspi_close();
		}
	} else {
		int32 size = 1 << max(0, (auxspi_save_size_log_2(extra) - 15));
		memset(data, 0, 0x8000);
		for (int i = 0; i < size; i++) {
			auxspi_write_data(i << 15, data, 0x8000, type, extra);
		}
	}
}

void auxspi_erase_sector(u32 sector, auxspi_extra extra) {
	uint8 type = auxspi_save_type(extra);
	if (type == 3) {
		if (extra)
			auxspi_disable_extra(extra);
		auxspi_open(0);

		auxspi_write(0x06);
		auxspi_close_lite();

		if (extra)
			auxspi_disable_extra(extra);
		auxspi_open(0);
		auxspi_write(0xd8);
		auxspi_write(sector & 0xff);
		auxspi_write((sector >> 8) & 0xff);
		auxspi_write((sector >> 8) & 0xff);
		auxspi_close_lite();

		if (extra)
			auxspi_disable_extra(extra);
		auxspi_open(0);
		auxspi_write(5);
		auxspi_wait_wip();
		auxspi_wait_busy();
		auxspi_close();
	}
}
