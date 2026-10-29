.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SUFFIXES:

CXX := clang++
OBJCXX := clang++
CC := clang
AR := ar

TARGET := nightbuild
BUILD_DIR := bootstrap
XXHASH_DIR := third_party/xxhash
XXHASH_LIB := $(BUILD_DIR)/libxxhash.a

CXX_SOURCES := \
	src/main.cpp \
	src/cli.cpp \
	src/toml.cpp \
	src/scheduler.cpp \
	src/fsevents.cpp \
	src/compile_commands.cpp \
	src/remote/digest.cpp

OBJCXX_SOURCES := \
	src/ui/license_window.mm

C_SOURCES := \
	$(XXHASH_DIR)/xxhash.c

CXX_OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CXX_SOURCES))
OBJCXX_OBJECTS := $(patsubst src/%.mm,$(BUILD_DIR)/%.o,$(OBJCXX_SOURCES))
C_OBJECTS := $(patsubst $(XXHASH_DIR)/%.c,$(BUILD_DIR)/xxhash/%.o,$(C_SOURCES))
OBJECTS := $(CXX_OBJECTS) $(OBJCXX_OBJECTS) $(C_OBJECTS)
DEPFILES := $(OBJECTS:.o=.d)

CPPFLAGS := -Isrc -Iinclude -I$(XXHASH_DIR)

COMMON_FLAGS := \
	-O3 \
	-march=native \
	-mtune=native \
	-fstrict-aliasing \
	-fomit-frame-pointer

CXXFLAGS := \
	-std=c++23 \
	$(COMMON_FLAGS) \
	-flto=full \
	-fno-rtti

OBJCXXFLAGS := $(CXXFLAGS)

CFLAGS := \
	-std=c11 \
	$(COMMON_FLAGS)

LDFLAGS := \
	-O3 \
	-march=native \
	-mtune=native \
	-flto=full \
	-Wl,-dead_strip

LDLIBS := -framework CoreServices -framework AppKit

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(CXX_OBJECTS) $(OBJCXX_OBJECTS) $(XXHASH_LIB)
	@printf '  LINK    %s\n' "$@"
	$(CXX) $(LDFLAGS) $(CXX_OBJECTS) $(OBJCXX_OBJECTS) $(XXHASH_LIB) $(LDLIBS) -o "$@"

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p "$(@D)"
	@printf '  CXX     %s\n' "$<"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/%.o: src/%.mm
	@mkdir -p "$(@D)"
	@printf '  OBJCXX  %s\n' "$<"
	$(OBJCXX) $(CPPFLAGS) $(OBJCXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD_DIR)/xxhash/%.o: $(XXHASH_DIR)/%.c
	@mkdir -p "$(@D)"
	@printf '  CC      %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(XXHASH_LIB): $(C_OBJECTS)
	@printf '  AR      %s\n' "$@"
	@mkdir -p "$(@D)"
	rm -f "$@"
	$(AR) rcs "$@" $(C_OBJECTS)

rebuild: clean all

clean:
	@printf '  CLEAN   %s\n' "$(BUILD_DIR) $(TARGET)"
	rm -rf "$(BUILD_DIR)" "$(TARGET)"

-include $(DEPFILES)
