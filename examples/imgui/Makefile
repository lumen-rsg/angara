# Angara ImGui Example — Build Configuration
#
# Builds and runs the Dear ImGui demo application.
#
# Prerequisites:
#   - angc compiler (in PATH or set ANGC=)
#   - GLFW3 (brew install glfw)
#   - OpenGL (system framework on macOS)
#   - Dear ImGui source (auto-downloaded if missing)
#
# Usage:
#   make          # Build and run
#   make module   # Build only the imgui module
#   make run      # Run the demo
#   make clean    # Remove build artifacts

# --- Configuration ---
ANGC      ?= angc
SRC       := demo.an
NAME      := demo
BUILD_DIR := build

# --- ImGui vendor directory (relative to Angara repo root) ---
REPO_ROOT  := ../..
IMGUI_DIR  ?= $(REPO_ROOT)/vendor/imgui

# --- OS Detection ---
UNAME_S    := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SO_EXT     := dylib
    FRAMEWORKS := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
    SO_EXT     := so
    FRAMEWORKS := -lGL -lX11
endif

# --- GLFW ---
GLFW_CFLAGS := $(shell pkg-config --cflags glfw3 2>/dev/null)
GLFW_LIBS   := $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")

# --- Dear ImGui sources ---
IMGUI_SRCS := $(IMGUI_DIR)/imgui.cpp \
              $(IMGUI_DIR)/imgui_demo.cpp \
              $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp \
              $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

IMGUI_OBJS := $(patsubst $(IMGUI_DIR)/%.cpp,$(BUILD_DIR)/imgui/%.o,$(IMGUI_SRCS))

# --- Module objects ---
MODULE_CPP  := $(REPO_ROOT)/modules/imgui.cpp
MODULE_OBJ  := $(BUILD_DIR)/imgui_module.o

# --- Output: the module .dylib must be named "libimgui.<ext>" so angc can find it ---
MODULE_LIB  := libimgui.$(SO_EXT)

# --- Compiler flags ---
CXX      := clang++
CXXFLAGS := -std=c++23 -fPIC -Wall -Wno-trigraphs -DGL_SILENCE_DEPRECATION=1 \
            -I$(REPO_ROOT)/angc/includes \
            -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends \
            $(GLFW_CFLAGS)

# --- Install location for system-wide module access ---
INSTALL_DIR := /opt/angara/modules

# --- Main targets ---
.PHONY: all build run clean download-imgui module install

all: module build run

module: $(MODULE_LIB)

build: $(BUILD_DIR)/$(NAME)
	@echo ""
	@echo "✅ Build successful: $(BUILD_DIR)/$(NAME)"
	@echo "   Run with: make run"

install: $(MODULE_LIB)
	@mkdir -p $(INSTALL_DIR)
	@cp $(MODULE_LIB) $(INSTALL_DIR)/libimgui.$(SO_EXT)
	@echo "✅ Installed module to $(INSTALL_DIR)/libimgui.$(SO_EXT)"

run: build
	@./$(BUILD_DIR)/$(NAME)

# --- Compile Angara source — angc produces the final binary ---
# angc links against build/modules/imgui.dylib (no "lib" prefix in reference)
$(BUILD_DIR)/$(NAME): $(SRC) $(MODULE_LIB)
	@mkdir -p $(BUILD_DIR)/modules
	@echo "📝 Compiling $(SRC) with angc..."
	@cp $(MODULE_LIB) $(BUILD_DIR)/modules/imgui.$(SO_EXT)
	$(ANGC) $(SRC)
	@mv $(NAME) $(BUILD_DIR)/ 2>/dev/null || true

# --- Compile ImGui sources ---
$(BUILD_DIR)/imgui/%.o: $(IMGUI_DIR)/%.cpp | download-imgui
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Compile the imgui module ---
$(MODULE_OBJ): $(MODULE_CPP) | download-imgui
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Link the imgui module as a shared library (in current dir for angc to find) ---
$(MODULE_LIB): $(MODULE_OBJ) $(IMGUI_OBJS)
	$(CXX) -shared $^ $(GLFW_LIBS) $(FRAMEWORKS) \
		-Wl,-install_name,@rpath/libimgui.$(SO_EXT) \
		-o $@
	@echo "✅ Built module: $(MODULE_LIB)"

# --- Download Dear ImGui if not present ---
download-imgui:
	@if [ ! -d "$(IMGUI_DIR)" ] || [ ! -f "$(IMGUI_DIR)/imgui.cpp" ]; then \
		echo "📥 Downloading Dear ImGui..."; \
		mkdir -p $(REPO_ROOT)/vendor; \
		git clone --depth 1 https://github.com/ocornut/imgui.git $(IMGUI_DIR); \
		echo "✅ Dear ImGui downloaded to $(IMGUI_DIR)"; \
	fi

# --- Clean ---
clean:
	rm -rf $(BUILD_DIR) libimgui.* ang_main.o ang_main.ll