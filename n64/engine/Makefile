BUILD_DIR=build
SOURCE_DIR=src
PROJECT_NAME=engine
LIBDRAGON_PREVIEW=2

include $(N64_INST)/include/n64.mk
include $(N64_INST)/include/t3d.mk

# n64.mk adds -MMD; -MP emits phony targets per header so deleted/renamed
# headers don't break incremental builds via the included .d files below.
CFLAGS += -MP
CXXFLAGS += -MP

N64_CXXFLAGS += -std=gnu++20 -fno-exceptions -O3 -Iinclude \
	-Wall -Wextra -Werror \
	-Wformat-signedness -fno-common \
	-Wshadow -Wdouble-promotion -Wformat-security -Wformat-overflow -Wformat-truncation \
	-Wfatal-errors

# extra engine flags from the project (ENGINE_CXXFLAGS in its Makefile.custom), e.g. -DP64_PHYS_* tuning.
# Not tracked by make: clean the engine build after changing them.
N64_CXXFLAGS += $(ENGINE_CXXFLAGS)

src = $(wildcard src/*.cpp) $(wildcard src/vi/*.cpp) $(wildcard src/lib/*.cpp)
src += $(wildcard src/scene/*.cpp) $(wildcard src/audio/*.cpp) $(wildcard src/assets/*.cpp)
src += $(wildcard src/collision/*.cpp) $(wildcard src/collision/shapes/*.cpp)
src += $(wildcard src/debug/*.cpp) $(wildcard src/debug/overlay/*.cpp)
src += $(wildcard src/scene/components/*.cpp) $(wildcard src/renderer/*.cpp)
src += $(wildcard src/renderer/hdr/*.cpp) $(wildcard src/renderer/bigtex/*.cpp)
src += $(wildcard src/renderer/particles/*.cpp) $(wildcard src/script/*.cpp)
src += $(wildcard src/libdragon/*.cpp)

# HDR / Bloom ucode
extraObj = $(BUILD_DIR)/renderer/hdr/rsp_hdr.o
# BigTex ucode + raw ASM
extraObj += $(BUILD_DIR)/renderer/bigtex/applyTexture.o
extraObj += $(BUILD_DIR)/renderer/bigtex/rsp_bigtex.o

all: $(BUILD_DIR)/$(PROJECT_NAME).a

$(BUILD_DIR)/$(PROJECT_NAME).a: $(src:%.cpp=$(BUILD_DIR)/%.o) $(extraObj)
	@mkdir -p $(dir $@)
	@echo "    [LD_LIB] $<"
	$(N64_LD) -r -o $(BUILD_DIR)/$(PROJECT_NAME).a $^

# Sources
$(BUILD_DIR)/src/%.o: $(SOURCE_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "    [CC_LIB] $<"
	$(N64_CC) -c $(CFLAGS) $(N64_CXXFLAGS) -o $@ $<

$(BUILD_DIR)/renderer/bigtex/applyTexture.o: $(SOURCE_DIR)/renderer/bigtex/applyTexture.S
	@mkdir -p $(dir $@)
	echo "    [AS] $<"
	$(N64_CC) -c $(N64_ASFLAGS) -MMD -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Collect -MMD dep files recursively: .d files sit next to each .o in nested dirs
# (build/src/renderer/particles/, build/renderer/bigtex/, ...), a flat wildcard
# misses them and header edits then don't trigger recompiles.
rwildcard = $(foreach d,$(wildcard $(1)*),$(call rwildcard,$(d)/,$(2)) $(filter $(subst *,%,$(2)),$(d)))
-include $(call rwildcard,$(BUILD_DIR)/,*.d)

.PHONY: all clean
