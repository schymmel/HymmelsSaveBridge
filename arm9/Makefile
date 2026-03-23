export BLOCKSDS			?= /opt/wonderful/thirdparty/blocksds/core
export BLOCKSDSEXT		?= /opt/wonderful/thirdparty/blocksds/external

export WONDERFUL_TOOLCHAIN	?= /opt/wonderful
ARM_NONE_EABI_PATH	?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/

NAME		:= arm9
BUILDDIR	:= build
ELF		:= $(NAME).elf

SOURCEDIRS	:= source
GFXDIRS		:= graphics
BINDIRS		:=
INCLUDEDIRS	:=

DEFINES		:=

LIBS		:= -lnds9 -lstdc++ -lc -lm
LIBDIRS		:= $(BLOCKSDS)/libs/libnds

PREFIX		:= $(ARM_NONE_EABI_PATH)arm-none-eabi-
CC		:= $(PREFIX)gcc
CXX		:= $(PREFIX)g++
LD		:= $(PREFIX)gcc

ARCH		:= -mthumb -mcpu=arm946e-s+nofp
SPECS		:= $(BLOCKSDS)/sys/crts/ds_arm9.specs

WARNFLAGS	:= -Wall

INCLUDEFLAGS	:= $(foreach path,$(INCLUDEDIRS),-I$(path)) \
		   $(foreach path,$(LIBDIRS),-I$(path)/include)

LIBDIRSFLAGS	:= $(foreach path,$(LIBDIRS),-L$(path)/lib)

CFLAGS		+= $(WARNFLAGS) $(INCLUDEFLAGS) $(DEFINES) \
		   $(ARCH) -O2 -ffunction-sections -fdata-sections \
		   -specs=$(SPECS)

CXXFLAGS	+= $(WARNFLAGS) $(INCLUDEFLAGS) $(DEFINES) \
		   $(ARCH) -O2 -ffunction-sections -fdata-sections \
		   -fno-exceptions -fno-rtti \
		   -specs=$(SPECS)

LDFLAGS		+= $(ARCH) $(LIBDIRSFLAGS) $(DEFINES) \
		   -Wl,--start-group $(LIBS) -Wl,--end-group -specs=$(SPECS)

SOURCES_PNG	:= $(shell find -L $(GFXDIRS) -name "*.png")
OBJS_ASSETS	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_PNG)))
HEADERS_ASSETS	:= $(patsubst %.png,%.h,$(addprefix $(BUILDDIR)/,$(SOURCES_PNG)))

INCLUDEFLAGS	+= -I$(BUILDDIR)/$(GFXDIRS)

SOURCES_S	:= $(shell find -L $(SOURCEDIRS) -name "*.s")
SOURCES_C	:= $(shell find -L $(SOURCEDIRS) -name "*.c")
SOURCES_CPP	:= $(shell find -L $(SOURCEDIRS) -name "*.cpp")

OBJS_SOURCES	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_S))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_C))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_CPP)))

OBJS		:= $(OBJS_ASSETS) $(OBJS_SOURCES)

.PHONY: all clean

all: $(ELF)

$(ELF): $(OBJS)
	@echo "  LD      $@"
	@$(LD) -o $@ $(OBJS) $(LDFLAGS)

clean:
	@echo "  CLEAN ARM9"
	@rm -rf $(BUILDDIR) $(ELF)

$(OBJS_SOURCES): $(HEADERS_ASSETS)

$(BUILDDIR)/%.s.o : %.s
	@echo "  AS      $<"
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.c.o : %.c
	@echo "  CC      $<"
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.cpp.o : %.cpp
	@echo "  CXX     $<"
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/%.png.o $(BUILDDIR)/%.h : %.png %.grit
	@echo "  GRIT    $<"
	@mkdir -p $(@D)
	@/opt/wonderful/thirdparty/blocksds/core/tools/grit/grit $< -ftc -W1 -o$(BUILDDIR)/$*
	@$(CC) $(CFLAGS) -c -o $(BUILDDIR)/$*.png.o $(BUILDDIR)/$*.c
	@touch $(BUILDDIR)/$*.png.o $(BUILDDIR)/$*.h
