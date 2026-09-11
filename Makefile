# Nintendo Switch homebrew build (devkitA64 + libnx + SDL2/SDL2_ttf).
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Open the devkitPro/MSYS2 shell before running make")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET       := switch-vitrine
BUILD        := build
SOURCES      := source source/core source/ui
DATA         := data
INCLUDES     := include include/core include/ui

APP_TITLE    := Vitrine
APP_AUTHOR   := Mateus Mendes
APP_VERSION  := 1.2.0

ARCH         := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS       := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS       += -D__SWITCH__ $(INCLUDE) `aarch64-none-elf-pkg-config --cflags SDL2_ttf libcurl jansson SDL2_image`
CXXFLAGS     := $(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions
ASFLAGS      := -g $(ARCH)
LDFLAGS      := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# Use the ports' metadata instead of maintaining transitive static dependencies.
LIBS         := `aarch64-none-elf-pkg-config --static --libs SDL2_ttf libcurl jansson SDL2_image`
LIBDIRS      := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT     := $(CURDIR)/$(TARGET)
export TOPDIR     := $(CURDIR)
export VPATH      := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                     $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR    := $(CURDIR)/$(BUILD)

CFILES            := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES          := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES            := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES          := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD         := $(CXX)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)
export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
icons := $(wildcard *.jpg *.png)
ifneq (,$(findstring icon.jpg,$(icons)))
export APP_ICON := $(TOPDIR)/icon.jpg
else ifneq (,$(findstring icon.png,$(icons)))
export APP_ICON := $(TOPDIR)/icon.png
else ifneq (,$(findstring logo.png,$(icons)))
export APP_ICON := $(TOPDIR)/logo.png
endif
else
export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifneq ($(strip $(APP_ICON)),)
export NROFLAGS += --icon=$(APP_ICON)
endif
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

ifneq ($(wildcard $(TOPDIR)/romfs),)
export NROFLAGS += --romfsdir=$(TOPDIR)/romfs
endif

.PHONY: $(BUILD) clean all
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)
endif
