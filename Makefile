export BLOCKSDS			?= /opt/blocksds/core
export BLOCKSDSEXT		?= /opt/blocksds/external

export WONDERFUL_TOOLCHAIN	?= /opt/wonderful
ARM_NONE_EABI_PATH	?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/

NAME		:= Snapshot

GAME_TITLE	:= Snapshot
GAME_AUTHOR	:= Hymmel
GAME_ICON	:= arm9/icon.bmp

ARM9DIR		:= arm9
ARM7DIR		:= arm7

SDROOT		:= sdroot
SDIMAGE		:= image.bin

NITROFSDIR	:= nitrofiles

ARM9ELF		:= $(ARM9DIR)/arm9.elf
ARM7ELF		:= $(ARM7DIR)/arm7.elf
ROM		:= $(NAME).nds

PREFIX		:= $(ARM_NONE_EABI_PATH)arm-none-eabi-
MKDIR		:= mkdir
RM		:= rm -rf

.PHONY: all clean arm9 arm7

all: $(ROM)

$(ROM): arm9 arm7
	@echo "  NDSTOOL $@"
	@$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 $(ARM7ELF) -9 $(ARM9ELF) \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_AUTHOR)" \
		-d $(NITROFSDIR)

arm9:
	@$(MAKE) -C $(ARM9DIR)

arm7:
	@$(MAKE) -C $(ARM7DIR)

clean:
	@echo "  CLEAN ALL"
	@$(MAKE) -C $(ARM9DIR) clean
	@$(MAKE) -C $(ARM7DIR) clean
	@$(RM) $(ROM) $(SDIMAGE)
