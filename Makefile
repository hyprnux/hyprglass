# HyprGlass Plugin

CXX ?= g++
CXXFLAGS = -fPIC -g -O2 -std=c++23
LDFLAGS = -shared
INCLUDES = $(shell pkg-config --cflags hyprland pixman-1 libdrm)
LIBS = $(shell pkg-config --libs hyprland)

ifeq ($(basename $(CXX)),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET = hyprglass.so
SOURCES = src/main.cpp src/GlassDecoration.cpp src/GlassPassElement.cpp src/PluginConfig.cpp src/ShaderManager.cpp
OBJ = $(SOURCES:.cpp=.o)

all: $(TARGET)

%.o : %.cpp
	@echo "[$(CXX)] $<"
	@$(CXX) -c $(CXXFLAGS) $(INCLUDES) $< -o $@

$(TARGET): $(OBJ)
	@echo "Linking $(TARGET)..."
	@$(CXX) $(LDFLAGS) $(OBJ) -o $@ $(LIBS)
	@echo "Done!"

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
