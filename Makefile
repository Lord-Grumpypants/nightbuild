CXX := clang++
PKG_CONFIG := pkg-config

TARGET := nightbuild
BOOTSTRAP_DIR := bootstrap

NOTCURSES_CFLAGS := $(shell $(PKG_CONFIG) --cflags notcurses)
NOTCURSES_LIBS := $(shell $(PKG_CONFIG) --libs notcurses)

CXXFLAGS := \
    -std=c++23 \
    -O3 \
    -march=native \
    -mtune=native \
    -flto=full \
    -fstrict-aliasing \
    -fomit-frame-pointer \
    -Iinclude \
    $(NOTCURSES_CFLAGS)

LDFLAGS := \
    -O3 \
    -march=native \
    -mtune=native \
    -flto=full

LDLIBS := \
    $(NOTCURSES_LIBS) \
    -framework CoreServices

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:src/%.cpp=$(BOOTSTRAP_DIR)/%.o)
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@printf '  LINK    %s\\n' "$@"
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o "$@"

$(BOOTSTRAP_DIR)/%.o: src/%.cpp
	@mkdir -p $(BOOTSTRAP_DIR)
	@printf '  CXX     %s\\n' "$<"
	$(CXX) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

-include $(DEPFILES)

clean:
	@printf '  CLEAN\\n'
	rm -rf $(BOOTSTRAP_DIR) $(TARGET)
