#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	src
DATA		:=	data
INCLUDES	:=	src src/windows/agg/include
APP_TITLE	:=  DeSmuME
#ROMFS		:=	romfs

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard

CFLAGS	:=	-g -Wall -O2 -mword-relocations -Wfatal-errors \
			-fomit-frame-pointer -ffunction-sections \
			-ffast-math $(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM11 -D_3DS -DHAVE_LIBZ -DHAVE_JIT

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=  -lctru -lm -lstdc++ -lz

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB) $(DEVKITPRO)/portlibs/armv6k


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

SOURCES_CXX +=  armcpu.cpp \
		        arm_instructions.cpp \
		        bios.cpp \
		        cp15.cpp \
				common.cpp \
				debug.cpp \
				Disassembler.cpp \
				driver.cpp \
				emufile.cpp \
				encrypt.cpp \
				firmware.cpp \
				fs-3ds.cpp \
				FIFO.cpp \
				GPU.cpp \
			    mc.cpp \
				readwrite.cpp \
				wifi.cpp \
				path.cpp \
			    MMU.cpp \
			    NDSSystem.cpp \
				ROMReader.cpp \
				render3D.cpp \
				rasterize.cpp \
				rtc.cpp \
			    saves.cpp \
			    SPU.cpp \
				matrix.cpp \
				gfx3d.cpp \
				texcache.cpp \
			    thumb_instructions.cpp \
				movie.cpp \
				mic.cpp \
				cheatSystem.cpp \
				slot1.cpp \
				slot2.cpp \
				version.cpp \
				addons/slot2_auto.cpp addons/slot2_mpcf.cpp addons/slot2_paddle.cpp addons/slot2_gbagame.cpp addons/slot2_none.cpp addons/slot2_rumblepak.cpp addons/slot2_guitarGrip.cpp addons/slot2_expMemory.cpp addons/slot2_piano.cpp addons/slot2_passme.cpp addons/slot1_none.cpp addons/slot1_r4.cpp addons/slot1_retail_nand.cpp addons/slot1_retail_auto.cpp addons/slot1_retail_mcrom.cpp addons/slot1_retail_mcrom_debug.cpp addons/slot1comp_mc.cpp addons/slot1comp_rom.cpp addons/slot1comp_protocol.cpp \
				utils/advanscene.cpp \
				utils/datetime.cpp \
				utils/xstring.cpp \
				utils/vfat.cpp \
				utils/fsnitro.cpp \
				utils/dlditool.cpp \
				utils/emufat.cpp \
				utils/decrypt/decrypt.cpp \
				utils/decrypt/header.cpp \
				utils/tinyxml/tinyxml.cpp \
				utils/tinyxml/tinystr.cpp \
				utils/tinyxml/tinyxmlerror.cpp \
				utils/tinyxml/tinyxmlparser.cpp \
				utils/libfat/cache.cpp \
				utils/libfat/directory.cpp \
				utils/libfat/disc.cpp \
				utils/libfat/fatdir.cpp \
				utils/libfat/fatfile.cpp \
				utils/libfat/filetime.cpp \
				utils/libfat/file_allocation_table.cpp \
				utils/libfat/libfat.cpp \
				utils/libfat/libfat_public_api.cpp \
				metaspu/metaspu.cpp \
				3ds/3ds_task.cpp \
				3ds/main.cpp \
				3ds/input.cpp \
				3ds/arm_arm/arm_gen.cpp \
				3ds/arm_arm/arm_jit.cpp

CFILES		:= 3ds/svchax.c 3ds/heap.c 3ds/memory.c
CPPFILES	:=
SFILES		:=
PICAFILES	:=
SHLISTFILES	:=
BINFILES	:=

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(SOURCES_CXX:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@/utils/decrypt && mkdir -p $@/addons && mkdir -p $@/3ds/arm_arm && mkdir -p $@/utils/tinyxml && mkdir -p $@/utils/libfat && mkdir -p $@/metaspu
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(NO_SMDH)),)
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
else
$(OUTPUT).3dsx	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $(patsubst %.shbin.o,%.shbin,$(notdir $@)))
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $@
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
endef

%.shbin.o : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)/$(file)))

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------