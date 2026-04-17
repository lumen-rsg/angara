# --- Configuration & Paths ---
INSTALL_MOD_DIR := /opt/angara/modules
INSTALL_BIN_DIR := /opt/homebrew/bin

# --- Colors & Formatting ---
ESC     := \033
RESET   := $(ESC)[0m
BOLD    := $(ESC)[1m
RED     := $(ESC)[1;31m
GREEN   := $(ESC)[1;32m
YELLOW  := $(ESC)[1;33m
BLUE    := $(ESC)[1;34m
MAGENTA := $(ESC)[1;35m
CYAN    := $(ESC)[1;36m

# --- OS Detection ---
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SO_EXT   := dylib
    BREW_DIR := $(shell brew --prefix 2>/dev/null)
    ifneq ($(BREW_DIR),)
        export PKG_CONFIG_PATH := $(BREW_DIR)/lib/pkgconfig:$(PKG_CONFIG_PATH)
    endif
else
    SO_EXT   := so
endif

# --- LLVM Configuration ---
LLVM_CONFIG := $(shell which llvm-config 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/llvm-config)
LLVM_CXXFLAGS := $(filter-out -fno-exceptions -fno-rtti -std=%,$(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null))
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)
LLVM_LIBS     := $(shell $(LLVM_CONFIG) --libs core native 2>/dev/null)
LLVM_SYSTEM_LIBS := $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)

# --- Toolchain & Flags ---
CC  := clang
CXX := clang++

CFLAGS   := -fPIC -Wall -Iangc/includes
CXXFLAGS := -std=c++23 -fPIC -Wall -Wno-trigraphs -Iangc/includes

LDFLAGS_BIN := $(LLVM_LDFLAGS) $(LLVM_LIBS) $(LLVM_SYSTEM_LIBS)

# --- Dependency Resolution (pkg-config) ---
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
LWS_CFLAGS  := $(shell pkg-config --cflags libwebsockets openssl 2>/dev/null)
LWS_LIBS    := $(shell pkg-config --libs libwebsockets openssl 2>/dev/null)
AMQP_CFLAGS := $(shell pkg-config --cflags librabbitmq 2>/dev/null)
AMQP_LIBS   := $(shell pkg-config --libs librabbitmq 2>/dev/null)
MQTT_CFLAGS := $(shell pkg-config --cflags libmosquitto libcjson 2>/dev/null)
MQTT_LIBS   := $(shell pkg-config --libs libmosquitto libcjson 2>/dev/null)

IMGUI_DIR   := vendor/imgui
IMGUI_SRCS  := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS  := $(patsubst $(IMGUI_DIR)/%.cpp,build/obj/imgui/%.o,$(IMGUI_SRCS))
GLFW_CFLAGS := $(shell pkg-config --cflags glfw3 2>/dev/null)
GLFW_LIBS   := $(shell pkg-config --libs glfw3 2>/dev/null)
IMGUI_FRAMEWORKS :=
ifeq ($(UNAME_S),Darwin)
    IMGUI_FRAMEWORKS := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
endif

ifeq ($(UNAME_S),Darwin)
    ifeq ($(LWS_CFLAGS),)
        LWS_CFLAGS := -I$(BREW_DIR)/opt/libwebsockets/include -I$(BREW_DIR)/include
    endif
endif

# --- File Definitions ---
MOD_SRCS := $(wildcard modules/*.c)
MOD_OBJS := $(patsubst %.c,build/obj/%.o,$(MOD_SRCS))
MOD_OUTS := $(patsubst modules/%.c,build/modules/%.$(SO_EXT),$(MOD_SRCS))

JSON_BR_SRC := modules/json_bridge.cpp
JSON_BR_OBJ := build/obj/modules/json_bridge.o

ANGC_SRCS := $(shell find angc -name "*.cpp")
ANGC_OBJS := $(patsubst %.cpp,build/obj/%.o,$(ANGC_SRCS))
ANGC_OUT  := build/angc

# --- Main Targets ---
.PHONY: all logo clean install

all: logo $(ANGC_OUT)
	@printf "$(BOLD)$(GREEN)>>> Build Completed Successfully <<<$(RESET)\n"

modules: logo $(MOD_OUTS) build/modules/imgui.$(SO_EXT)
	@printf "$(BOLD)$(GREEN)>>> Modules Built Successfully <<<$(RESET)\n"

imgui: logo build/modules/imgui.$(SO_EXT)
	@printf "$(BOLD)$(GREEN)>>> ImGui Module Built Successfully <<<$(RESET)\n"

logo:
	@printf "\n"
	@printf "$(CYAN) █████╗ ███╗   ██╗ ██████╗  █████╗ ██████╗  █████╗  $(RESET)\n"
	@printf "$(CYAN) ██╔══██╗████╗  ██║██╔════╝ ██╔══██╗██╔══██╗██╔══██╗$(RESET)\n"
	@printf "$(CYAN) ███████║██╔██╗ ██║██║  ███╗███████║██████╔╝███████║ $(RESET)\n"
	@printf "$(CYAN) ██╔══██║██║╚██╗██║██║   ██║██╔══██║██╔══██╗██╔══██║ $(RESET)\n"
	@printf "$(CYAN) ██║  ██║██║ ╚████║╚██████╔╝██║  ██║██║  ██║██║  ██║ $(RESET)\n"
	@printf "$(CYAN) ╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝ $(RESET)\n"
	@printf "$(MAGENTA)[MK] Building Angara v3 (LLVM) // cv2 was here$(RESET)\n\n"

# --- Compilation Rules ---
build/obj/%.o: %.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s\n" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/obj/%.o: %.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s\n" "$<"
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

build/obj/modules/http.o: modules/http.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (CURL)\n" "$<"
	@$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

build/obj/modules/websocket.o: modules/websocket.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (LWS)\n" "$<"
	@$(CC) $(CFLAGS) $(LWS_CFLAGS) -c $< -o $@

build/obj/modules/amqp.o: modules/amqp.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (AMQP)\n" "$<"
	@$(CC) $(CFLAGS) $(AMQP_CFLAGS) -c $< -o $@

build/obj/modules/mqtt.o: modules/mqtt.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (MQTT)\n" "$<"
	@$(CC) $(CFLAGS) $(MQTT_CFLAGS) -c $< -o $@

build/obj/modules/matter.o: modules/matter.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (MATTER/CURL)\n" "$<"
	@$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

build/obj/modules/rpc.o: modules/rpc.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (RPC)\n" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/obj/modules/json_bridge.o: modules/json_bridge.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (JSON Bridge)\n" "$<"
	@$(CXX) $(CXXFLAGS) -Iangc-ls/vendor -c $< -o $@

build/obj/modules/imgui.o: modules/imgui.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (ImGui Module)\n" "$<"
	@$(CXX) $(CXXFLAGS) -DGL_SILENCE_DEPRECATION=1 \
		-I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(GLFW_CFLAGS) -c $< -o $@

build/obj/imgui/%.o: $(IMGUI_DIR)/%.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (Dear ImGui)\n" "$<"
	@$(CXX) $(CXXFLAGS) -DGL_SILENCE_DEPRECATION=1 \
		-I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(GLFW_CFLAGS) -c $< -o $@

# --- Linkage Rules (Modules) ---
build/modules/http.$(SO_EXT): build/obj/modules/http.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(CURL_LIBS) -o $@

build/modules/websocket.$(SO_EXT): build/obj/modules/websocket.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(LWS_LIBS) -o $@

build/modules/time.$(SO_EXT): build/obj/modules/time.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared -o $@
else
	@$(CC) $< -shared -lrt -o $@
endif

build/modules/amqp.$(SO_EXT): build/obj/modules/amqp.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(AMQP_LIBS) -o $@

build/modules/mqtt.$(SO_EXT): build/obj/modules/mqtt.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(MQTT_LIBS) -o $@

build/modules/matter.$(SO_EXT): build/obj/modules/matter.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(CURL_LIBS) -o $@

build/modules/math.$(SO_EXT): build/obj/modules/math.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared -o $@
else
	@$(CC) $< -shared -lm -o $@
endif

build/modules/sys.$(SO_EXT): build/obj/modules/sys.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared -lproc -o $@
else
	@$(CC) $< -shared -o $@
endif

build/modules/json.$(SO_EXT): build/obj/modules/json.o $(JSON_BR_OBJ)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CXX) $^ -shared -o $@

build/modules/rpc.$(SO_EXT): build/obj/modules/rpc.o $(JSON_BR_OBJ)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (RPC+JSON)\n" "$@"
	@$(CXX) $^ -shared -o $@

build/modules/imgui.$(SO_EXT): build/obj/modules/imgui.o $(IMGUI_OBJS)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (ImGui+GLFW+OpenGL)\n" "$@"
	@$(CXX) -shared $^ $(GLFW_LIBS) $(IMGUI_FRAMEWORKS) \
		-Wl,-install_name,@rpath/libimgui.$(SO_EXT) \
		-o $@

build/modules/%.$(SO_EXT): build/obj/modules/%.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared -o $@

# --- Linkage Rules (Compiler) ---
$(ANGC_OUT): $(ANGC_OBJS)
	@mkdir -p $(@D)
	@printf "$(CYAN)[BN] $(RESET) %s\n" "$@"
	@$(CXX) $^ $(LDFLAGS_BIN) -o $@

# --- Installation Rules ---
install: install_libraries install_executables
	@printf "$(BOLD)$(GREEN)>>> Full Installation Complete <<<$(RESET)\n"

install_libraries: $(MOD_OUTS)
	@printf "$(MAGENTA)[IN] $(RESET) Installing Modules to %s\n" "$(INSTALL_MOD_DIR)"
	@mkdir -p $(INSTALL_MOD_DIR)
	@cp build/modules/*.$(SO_EXT) $(INSTALL_MOD_DIR)/

install_executables: $(ANGC_OUT)
	@printf "$(CYAN)[IN] $(RESET) Installing Executable to %s\n" "$(INSTALL_BIN_DIR)"
	@mkdir -p $(INSTALL_BIN_DIR)
	@cp $(ANGC_OUT) $(INSTALL_BIN_DIR)/

# --- Clean ---
clean:
	@printf "$(RED)[CL] $(RESET) Cleaning build directory...\n"
	@rm -rf build