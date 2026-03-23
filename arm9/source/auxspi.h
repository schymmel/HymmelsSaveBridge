#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <nds.h>

typedef enum {
	AUXSPI_DEFAULT,
	AUXSPI_INFRARED,
	AUXSPI_BBDX,
	AUXSPI_BLUETOOTH,
	AUXSPI_FLASH_CARD = 999
} auxspi_extra;

uint8 auxspi_save_type(auxspi_extra extra = AUXSPI_DEFAULT);
uint32 auxspi_save_size(auxspi_extra extra = AUXSPI_DEFAULT);
uint8 auxspi_save_size_log_2(auxspi_extra extra = AUXSPI_DEFAULT);
uint32 auxspi_save_jedec_id(auxspi_extra extra = AUXSPI_DEFAULT);
uint8 auxspi_save_status_register(auxspi_extra extra = AUXSPI_DEFAULT);
void auxspi_read_data(uint32 addr, uint8* buf, uint32 cnt, uint8 type = 0,auxspi_extra extra = AUXSPI_DEFAULT);
void auxspi_write_data(uint32 addr, uint8 *buf, uint32 cnt, uint8 type = 0,auxspi_extra extra = AUXSPI_DEFAULT);
void auxspi_erase(auxspi_extra extra = AUXSPI_DEFAULT);
void auxspi_erase_sector(u32 sector, auxspi_extra extra = AUXSPI_DEFAULT);

auxspi_extra auxspi_has_extra();

void auxspi_disable_extra(auxspi_extra extra = AUXSPI_DEFAULT);
void auxspi_disable_infrared();
void auxspi_disable_big_protection();

#endif
