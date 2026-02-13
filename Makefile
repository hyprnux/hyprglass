# Hyprnux-Liquid Plugin

CXX ?= g++
CXXFLAGS = -shared -fPIC -g -O2 -std=c++23
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland)

ifeq ($(CXX),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprnux-liquid.so
SOURCES = src/main.cpp src/LiquidGlassDecoration.cpp src/LiquidGlassPassElement.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $@ $(LIBS)
	@echo "Done!"

clean:
	rm -f $(TARGET)

.PHONY: all clean
