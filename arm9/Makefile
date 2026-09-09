# SPDX-License-Identifier: CC0-1.0
#
# SPDX-FileContributor: Antonio Niño Díaz, 2023

export BLOCKSDS			?= /opt/blocksds/core
TARGET			?= cart_flasher
.DEFAULT_GOAL		:= all

BUILDDIR	:= build
GENERATEDDIR	:= generated
LIBNCGC_UPSTREAM := vendor/libncgc
LIBNCGC_DIR := $(GENERATEDDIR)/libncgc
LIBNCGC_OVERLAYDIR := overlays/libncgc
LIBNCGC_STAMP := $(LIBNCGC_DIR)/.prepared
LIBNCGC_UPSTREAM_FILES := $(shell find -L $(LIBNCGC_UPSTREAM) -type f -not -path "*/.git/*")
LIBNCGC_OVERLAY_FILES := $(shell find -L $(LIBNCGC_OVERLAYDIR)/files -type f)
LIBNCGC := $(LIBNCGC_DIR)

FLASHCART_CORE_UPSTREAM := vendor/flashcart_core
FLASHCART_CORE_DIR := $(GENERATEDDIR)/flashcart_core
FLASHCART_CORE_OVERLAYDIR := overlays/flashcart_core
FLASHCART_CORE_STAMP := $(FLASHCART_CORE_DIR)/.prepared
FLASHCART_CORE_SOURCE_NAMES := \
	device.cpp \
	optional_platform.cpp \
	devices/ace3dsplus.cpp \
	devices/ak2i.cpp \
	devices/datel.cpp \
	devices/dstt.cpp \
	devices/r4igold3ds.cpp \
	devices/r4isdhc.cpp \
	devices/r4isdhchk.cpp
FLASHCART_CORE_SOURCES := $(addprefix $(FLASHCART_CORE_DIR)/,$(FLASHCART_CORE_SOURCE_NAMES))
FLASHCART_CORE_UPSTREAM_FILES := $(shell find -L $(FLASHCART_CORE_UPSTREAM) -type f -not -path "*/.git/*")
FLASHCART_CORE_OVERLAY_FILES := $(shell find -L $(FLASHCART_CORE_OVERLAYDIR)/files -type f)

SOURCEDIRS	:= source
INCLUDEDIRS	:= $(FLASHCART_CORE_DIR) $(BUILDDIR) $(BUILDDIR)/data
DATA		:= data

DEFINES		:= -DARM9 \
		   -DCART_FLASHER_VERSION=\"$(CART_FLASHER_VERSION)\" \
		   -DCART_FLASHER_COMMIT=\"$(CART_FLASHER_COMMIT)\" \
		   -DCART_FLASHER_BUILD_KIND=\"$(CART_FLASHER_BUILD_KIND)\" \
		   -DNCGC_PLATFORM_NTR

ifneq ($(strip $(testing)),)
	DEFINES += -DTESTING=1
endif

LIBS		:= -lncgc -lstdc++ -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/libnds $(LIBNCGC)

NAME		:= arm9
ELF		:= $(CURDIR)/$(TARGET).elf
MAP		:= $(CURDIR)/$(TARGET).map

WONDERFUL_TOOLCHAIN	?= /opt/wonderful
ARM_NONE_EABI_PATH	?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/
PREFIX		:= $(ARM_NONE_EABI_PATH)arm-none-eabi-
CC		:= $(PREFIX)gcc
CXX		:= $(PREFIX)g++
LD		:= $(PREFIX)gcc
RM		:= rm -rf
MKDIR		:= mkdir -p

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

SOURCES_S	:= $(shell find -L $(SOURCEDIRS) -name "*.s")
SOURCES_C	:= $(shell find -L $(SOURCEDIRS) -name "*.c")
SOURCES_CPP	:= $(filter-out source/debug_simulated_cart.cpp, $(shell find -L $(SOURCEDIRS) -name "*.cpp")) $(FLASHCART_CORE_SOURCES)
SOURCES_BIN	:= $(filter-out data/debug_simulated_banner.bin data/blowfish_ntrboot_twl_%.bin data/blowfish_cartridge_twl-%.bin, $(shell find -L $(DATA) -name "*.*"))

ifeq ($(CART_FLASHER_BUILD_KIND),Debug)
DEFINES += -DCART_FLASHER_DEBUG_SIM=1
SOURCES_CPP += source/debug_simulated_cart.cpp
SOURCES_BIN += data/debug_simulated_banner.bin
$(BUILDDIR)/source/debug_simulated_cart.cpp.o: $(BUILDDIR)/data/debug_simulated_banner.bin.o
endif

ARCH		:= -marm -mthumb-interwork -march=armv5te -mtune=arm946e-s
SPECS		:= $(BLOCKSDS)/sys/crts/ds_arm9.specs

INCLUDEFLAGS	:= $(foreach path,$(INCLUDEDIRS),-I$(path)) \
		   $(foreach path,$(LIBDIRS),-I$(path)/include)

LIBDIRSFLAGS	:= $(foreach path,$(LIBDIRS),-L$(path)/lib)

ASFLAGS		+= -x assembler-with-cpp $(DEFINES) $(INCLUDEFLAGS) \
		   $(ARCH) -ffunction-sections -fdata-sections -specs=$(SPECS)

CFLAGS		+= -std=gnu17 -Wall $(DEFINES) $(INCLUDEFLAGS) \
		   $(ARCH) -O2 -ffunction-sections -fdata-sections -specs=$(SPECS)

CXXFLAGS	+= -std=gnu++14 -Wall $(DEFINES) $(INCLUDEFLAGS) \
		   $(ARCH) -O2 -ffunction-sections -fdata-sections \
		   -fno-rtti -fno-exceptions -specs=$(SPECS)

LDFLAGS		:= $(ARCH) $(LIBDIRSFLAGS) -Wl,-Map,$(MAP),--gc-sections $(DEFINES) \
		   -Wl,--start-group $(LIBS) -lc -Wl,--end-group -specs=$(SPECS)

OBJS_BIN	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_BIN)))
HEADERS_BIN	:= $(addsuffix .h,$(subst .,_,$(addprefix $(BUILDDIR)/,$(SOURCES_BIN))))

OBJS_SOURCES	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_S))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_C))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_CPP)))

OBJS		:= $(OBJS_BIN) $(OBJS_SOURCES)
DEPS		:= $(OBJS_SOURCES:.o=.d)

.PHONY: all clean force_libncgc

all: $(ELF)

# Without a prerequisite, make would skip this once lib/libncgc.a exists and
# silently ignore libncgc edits. Delegating every build is a no-op when
# nothing changed.
$(LIBNCGC_STAMP): $(LIBNCGC_UPSTREAM_FILES) $(LIBNCGC_OVERLAY_FILES) scripts/prepare_libncgc.sh
	@echo "  PREPARE.9 libncgc"
	$(V)$(SHELL) scripts/prepare_libncgc.sh "$(LIBNCGC_UPSTREAM)" "$(LIBNCGC_OVERLAYDIR)" "$(LIBNCGC_DIR)"
	$(V)touch $@

$(LIBNCGC)/lib/libncgc.a: $(LIBNCGC_STAMP) force_libncgc
	@$(MAKE) PLATFORM=ntr -C $(LIBNCGC) fordka

force_libncgc:

$(FLASHCART_CORE_STAMP): $(FLASHCART_CORE_UPSTREAM_FILES) $(FLASHCART_CORE_OVERLAY_FILES) scripts/prepare_flashcart_core.sh
	@echo "  PREPARE.9 flashcart_core"
	$(V)$(SHELL) scripts/prepare_flashcart_core.sh "$(FLASHCART_CORE_UPSTREAM)" "$(FLASHCART_CORE_OVERLAYDIR)" "$(FLASHCART_CORE_DIR)"
	$(V)touch $@

$(FLASHCART_CORE_SOURCES): $(FLASHCART_CORE_STAMP)
	@:

# App sources include flashcart_core headers from the generated patched copy,
# so its stamp must be ready before any ARM9 compilation can start.
$(OBJS_SOURCES): $(FLASHCART_CORE_STAMP) $(LIBNCGC_STAMP)

$(ELF): $(LIBNCGC)/lib/libncgc.a $(OBJS)
	@echo "  LD.9    $@"
	$(V)$(LD) -o $@ $(OBJS) $(LDFLAGS)

clean:
	@echo "  CLEAN.9"
	$(V)$(RM) $(ELF) $(MAP) $(BUILDDIR) $(GENERATEDDIR)

$(BUILDDIR)/%.s.o : %.s
	@echo "  AS.9    $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(ASFLAGS) -c $< -o $@

$(BUILDDIR)/%.c.o : %.c
	@echo "  CC.9    $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/%.cpp.o : %.cpp
	@echo "  CXX.9   $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Rule for binary data files
$(BUILDDIR)/%.bin.o: %.bin
	@echo "  BIN.9   $<"
	@$(MKDIR) -p $(@D)
	$(V)$(BLOCKSDS)/tools/bin2c/bin2c $< $(@D)
	$(V)$(CC) $(CFLAGS) -c $(BUILDDIR)/$*_bin.c -o $@

-include $(DEPS)
