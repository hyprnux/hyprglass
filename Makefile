# HyprGlass Plugin

CXX ?= g++
CC ?= gcc
WAYLAND_SCANNER ?= wayland-scanner
CXXFLAGS = -fPIC -g -O2 -std=c++23
LDFLAGS = -shared
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland) -ldl

# Shown by `hyprctl plugin list`. Release builds pass HYPRGLASS_VERSION; other
# builds describe their own checkout (never an enclosing repository), keeping
# only characters safe in a C string. Changing it needs `make clean`.
ifndef HYPRGLASS_VERSION
HYPRGLASS_VERSION := $(shell [ -e .git ] && git describe --tags --dirty 2>/dev/null | sed 's/^v//' | tr -cd 'A-Za-z0-9._+-')
endif
ifeq ($(HYPRGLASS_VERSION),)
HYPRGLASS_VERSION := dev
endif
CXXFLAGS += -DHYPRGLASS_VERSION='"$(HYPRGLASS_VERSION)"'

ifeq ($(basename $(CXX)),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprglass.so
SOURCES = src/main.cpp src/GlassDecoration.cpp src/GlassPassElement.cpp src/GlassRenderer.cpp src/GlassLayerSurface.cpp src/GlassLayerPassElement.cpp src/GlassLayerCompositeElement.cpp src/BackgroundDamageObserver.cpp src/PluginConfig.cpp src/ShaderManager.cpp src/Diagnostics.cpp src/GlassSubsurfaceState.cpp src/GlassSubsurfacePassElement.cpp src/GlassSubsurfaceCompositeElement.cpp src/ItemHints.cpp
OBJ = $(SOURCES:.cpp=.o)
HEADERS = $(wildcard src/*.hpp)

# ── hyprglass_item_v1 protocol helper ────────────────────────────────────────
# Built as its own shared library and embedded (via ItemHelperBlob.S) inside
# hyprglass.so, so it can be dlopen'd with RTLD_NODELETE and outlive a plugin
# reload — see AGENTS.md's unload-safety note. Depends only on
# libwayland-server, never on Hyprland headers.

PROTOCOL_XML    = protocols/hyprglass-item-v1.xml
ITEM_HELPER_DIR = src/item-helper
PROTOCOL_HEADER = $(ITEM_HELPER_DIR)/hyprglass-item-v1-server-protocol.h
PROTOCOL_CODE   = $(ITEM_HELPER_DIR)/hyprglass-item-v1-protocol.c

HELPER_SO       = $(ITEM_HELPER_DIR)/hyprglass-item-helper.so
HELPER_SOURCES  = $(ITEM_HELPER_DIR)/helper.c $(PROTOCOL_CODE)
HELPER_OBJ      = $(HELPER_SOURCES:.c=.o)

WAYLAND_SERVER_CFLAGS = $(shell pkg-config --cflags wayland-server)
WAYLAND_SERVER_LIBS   = $(shell pkg-config --libs wayland-server)

ITEM_HELPER_BLOB_OBJ = src/ItemHelperBlob.o

all: $(TARGET)

%.o : %.cpp $(HEADERS)
	@echo "[$(CXX)] $<"
	@$(CXX) -c $(CXXFLAGS) $(INCLUDES) $< -o $@

$(PROTOCOL_HEADER): $(PROTOCOL_XML)
	@echo "[wayland-scanner] server-header $<"
	@$(WAYLAND_SCANNER) server-header $< $@

$(PROTOCOL_CODE): $(PROTOCOL_XML)
	@echo "[wayland-scanner] private-code $<"
	@$(WAYLAND_SCANNER) private-code $< $@

# helper.c includes the generated server header; every helper object depends
# on it existing first, not just the .c file the pattern rule already covers.
$(HELPER_OBJ): $(PROTOCOL_HEADER)

$(ITEM_HELPER_DIR)/%.o: $(ITEM_HELPER_DIR)/%.c
	@echo "[$(CC)] $<"
	@$(CC) -c -fPIC -O2 -std=c11 $(WAYLAND_SERVER_CFLAGS) -I$(ITEM_HELPER_DIR) $< -o $@

$(HELPER_SO): $(HELPER_OBJ)
	@echo "Linking $(HELPER_SO)..."
	@$(CC) -shared -fPIC -o $@ $(HELPER_OBJ) $(WAYLAND_SERVER_LIBS)

# .incbin's path is relative to this Makefile's own working directory (the
# repo root), the same base every other SOURCES path here is written against.
$(ITEM_HELPER_BLOB_OBJ): src/ItemHelperBlob.S $(HELPER_SO)
	@echo "[as] $<"
	@$(CC) -c $< -o $@

$(TARGET): $(OBJ) $(ITEM_HELPER_BLOB_OBJ)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(LDFLAGS) $(OBJ) $(ITEM_HELPER_BLOB_OBJ) -o $@ $(LIBS)
	@echo "Done!"

clean:
	rm -f $(OBJ) $(TARGET) $(ITEM_HELPER_BLOB_OBJ) $(HELPER_OBJ) $(HELPER_SO) $(PROTOCOL_HEADER) $(PROTOCOL_CODE)

.PHONY: all clean
