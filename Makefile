# HyprGlass Plugin

CXX ?= g++
CXXFLAGS = -shared -fPIC -g -O2 -std=c++23
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland)

ifeq ($(CXX),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprglass.so
SOURCES = src/main.cpp src/GlassDecoration.cpp src/GlassPassElement.cpp src/PluginConfig.cpp src/ShaderManager.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $@ $(LIBS)
	@echo "Done!"

clean:
	rm -f $(TARGET)

.PHONY: all clean
