# --- Configuration & Paths ---
INSTALL_MOD_DIR := /opt/angara/modules
INSTALL_RT_DIR  := /opt/angara/src/runtime
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
    RPATH_FLAG := -Wl,-rpath,$(INSTALL_RT_DIR) -Wl,-rpath,@executable_path/../lib
    RT_LDFLAGS := -dynamiclib -install_name @rpath/libangara_runtime.$(SO_EXT)
else
    SO_EXT   := so
    RPATH_FLAG := -Wl,-rpath=$(INSTALL_RT_DIR) -Wl,-rpath='$$ORIGIN/../lib'
    RT_LDFLAGS := -shared
endif

# --- Toolchain & Flags ---
CC  := clang
CXX := clang++

CFLAGS   := -fPIC -Wall -Iangc/includes
CXXFLAGS := -std=c++23 -fPIC -Wall -Wno-trigraphs -Iangc/includes

LDFLAGS_MOD := -shared -Lbuild -langara_runtime
LDFLAGS_BIN := -Lbuild -langara_runtime $(RPATH_FLAG)

# --- Dependency Resolution (pkg-config) ---
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
LWS_CFLAGS  := $(shell pkg-config --cflags libwebsockets openssl 2>/dev/null)
LWS_LIBS    := $(shell pkg-config --libs libwebsockets openssl 2>/dev/null)
AMQP_CFLAGS := $(shell pkg-config --cflags librabbitmq 2>/dev/null)
AMQP_LIBS   := $(shell pkg-config --libs librabbitmq 2>/dev/null)

ifeq ($(UNAME_S),Darwin)
    ifeq ($(LWS_CFLAGS),)
        LWS_CFLAGS := -I$(BREW_DIR)/opt/libwebsockets/include -I$(BREW_DIR)/include
    endif
endif

# --- File Definitions ---
RT_SRCS := $(wildcard runtime/rt_*.c) runtime/angara_runtime.c
RT_OBJS := $(patsubst %.c,build/obj/%.o,$(RT_SRCS))
RT_OUT  := build/libangara_runtime.$(SO_EXT)

MOD_SRCS := $(wildcard modules/*.c)
MOD_OBJS := $(patsubst %.c,build/obj/%.o,$(MOD_SRCS))
MOD_OUTS := $(patsubst modules/%.c,build/modules/%.$(SO_EXT),$(MOD_SRCS))

JSON_BR_SRC := modules/json_bridge.cpp
JSON_BR_OBJ := build/obj/modules/json_bridge.o

ANGC_SRCS := $(shell find angc -name "*.cpp")
ANGC_OBJS := $(patsubst %.cpp,build/obj/%.o,$(ANGC_SRCS))
ANGC_OUT  := build/angc

# --- Main Targets ---
.PHONY: all logo clean install install_runtime install_libraries install_executables

all: logo $(RT_OUT) $(MOD_OUTS) $(ANGC_OUT) $(ALS_OUT)
	@printf "$(BOLD)$(GREEN)>>> Build Completed Successfully <<<$(RESET)\n"

logo:
	@printf "\n"
	@printf "$(CYAN) █████╗ ███╗   ██╗ ██████╗  █████╗ ██████╗  █████╗  $(RESET)\n"
	@printf "$(CYAN) ██╔══██╗████╗  ██║██╔════╝ ██╔══██╗██╔══██╗██╔══██╗$(RESET)\n"
	@printf "$(CYAN) ███████║██╔██╗ ██║██║  ███╗███████║██████╔╝███████║ $(RESET)\n"
	@printf "$(CYAN) ██╔══██║██║╚██╗██║██║   ██║██╔══██║██╔══██╗██╔══██║ $(RESET)\n"
	@printf "$(CYAN) ██║  ██║██║ ╚████║╚██████╔╝██║  ██║██║  ██║██║  ██║ $(RESET)\n"
	@printf "$(CYAN) ╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝ $(RESET)\n"
	@printf "$(MAGENTA)[MK] Building Angara v2 // cv2 was here$(RESET)\n\n"

# --- Compilation Rules ---
build/obj/%.o: %.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s\n" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/obj/%.o: %.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s\n" "$<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

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

build/obj/modules/json_bridge.o: modules/json_bridge.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (JSON Bridge)\n" "$<"
	@$(CXX) $(CXXFLAGS) -Iangara-ls/vendor -c $< -o $@

# --- Linkage Rules (Runtime) ---
$(RT_OUT): $(RT_OBJS)
	@mkdir -p $(@D)
	@printf "$(BLUE)[LN] $(RESET) %s\n" "$@"
	@$(CC) $(RT_LDFLAGS) $^ -o $@

# --- Linkage Rules (Modules) ---
build/modules/http.$(SO_EXT): build/obj/modules/http.o | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< $(LDFLAGS_MOD) $(CURL_LIBS) -o $@

build/modules/websocket.$(SO_EXT): build/obj/modules/websocket.o | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< $(LDFLAGS_MOD) $(LWS_LIBS) -o $@

build/modules/time.$(SO_EXT): build/obj/modules/time.o | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(UNAME_S),Darwin)
	@$(CC) $< $(LDFLAGS_MOD) -o $@
else
	@$(CC) $< $(LDFLAGS_MOD) -lrt -o $@
endif

build/modules/amqp.$(SO_EXT): build/obj/modules/amqp.o | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< $(LDFLAGS_MOD) $(AMQP_LIBS) -o $@

build/modules/json.$(SO_EXT): build/obj/modules/json.o $(JSON_BR_OBJ) | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CXX) $^ $(LDFLAGS_MOD) -o $@

build/modules/%.$(SO_EXT): build/obj/modules/%.o | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< $(LDFLAGS_MOD) -o $@

# --- Linkage Rules (Executables) ---
$(ANGC_OUT): $(ANGC_OBJS) | $(RT_OUT)
	@mkdir -p $(@D)
	@printf "$(CYAN)[BN] $(RESET) %s\n" "$@"
	@$(CXX) $^ $(LDFLAGS_BIN) -o $@

$(ALS_OUT): $(ALS_OBJS)
	@mkdir -p $(@D)
	@printf "$(CYAN)[BN] $(RESET) %s\n" "$@"
	@$(CXX) $^ -o $@

# --- Installation Rules ---
install: install_runtime install_libraries install_executables
	@printf "$(BOLD)$(GREEN)>>> Full Installation Complete <<<$(RESET)\n"

install_runtime: $(RT_OUT)
	@printf "$(BLUE)[IN] $(RESET) Installing Runtime to %s\n" "$(INSTALL_RT_DIR)"
	@mkdir -p $(INSTALL_RT_DIR)
	@cp $(RT_OUT) $(INSTALL_RT_DIR)/
	@cp runtime/angara_runtime.h $(INSTALL_RT_DIR)/

install_libraries: $(MOD_OUTS)
	@printf "$(MAGENTA)[IN] $(RESET) Installing Modules to %s\n" "$(INSTALL_MOD_DIR)"
	@mkdir -p $(INSTALL_MOD_DIR)
	@cp build/modules/*.$(SO_EXT) $(INSTALL_MOD_DIR)/

install_executables: $(ANGC_OUT) $(ALS_OUT)
	@printf "$(CYAN)[IN] $(RESET) Installing Executables to %s\n" "$(INSTALL_BIN_DIR)"
	@mkdir -p $(INSTALL_BIN_DIR)
	@cp $(ANGC_OUT) $(INSTALL_BIN_DIR)/

# --- Clean ---
clean:
	@printf "$(RED)[CL] $(RESET) Cleaning build directory...\n"
	@rm -rf build