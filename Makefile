INSTALL_MOD_DIR := /opt/angara/modules
INSTALL_BIN_DIR := /usr/local/bin
INSTALL_DOC_DIR := /opt/angara/docs

ESC     := \033
RESET   := $(ESC)[0m
BOLD    := $(ESC)[1m
RED     := $(ESC)[1;31m
GREEN   := $(ESC)[1;32m
YELLOW  := $(ESC)[1;33m
BLUE    := $(ESC)[1;34m
MAGENTA := $(ESC)[1;35m
CYAN    := $(ESC)[1;36m

UNAME_S := $(shell uname -s)
# ── Windows (MINGW64 / MSYS2) detection ─────────────────────────────────
ifneq ($(findstring MINGW,$(UNAME_S)),)
    WIN32       := 1
    SO_EXT      := dll
    SONAME_FLAG :=
    INSTALL_BIN_DIR := /mingw64/bin
else ifneq ($(findstring MSYS,$(UNAME_S)),)
    WIN32       := 1
    SO_EXT      := dll
    SONAME_FLAG :=
    INSTALL_BIN_DIR := /usr/bin
else ifeq ($(UNAME_S),Darwin)
    SO_EXT      := dylib
    SONAME_FLAG := -Wl,-install_name
    INSTALL_BIN_DIR := /opt/homebrew/bin
    BREW_DIR    := $(shell brew --prefix 2>/dev/null)
    ifneq ($(BREW_DIR),)
        export PKG_CONFIG_PATH := $(BREW_DIR)/lib/pkgconfig:$(PKG_CONFIG_PATH)
    endif
else
    SO_EXT      := so
    SONAME_FLAG := -Wl,-soname
endif

ifneq ($(WIN32),1)
    SONAME_ARG = $(SONAME_FLAG),$(INSTALL_MOD_DIR)/$(@F)
else
    SONAME_ARG :=
endif

LLVM_CONFIG := $(shell ls /usr/bin/llvm-config* 2>/dev/null | sort -t- -k3 -V | tail -1 || which llvm-config 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
    ifeq ($(LLVM_CONFIG),)
        LLVM_CONFIG := $(shell ls $(BREW_DIR)/opt/llvm/bin/llvm-config 2>/dev/null)
    endif
endif
LLVM_CXXFLAGS := $(filter-out -fno-exceptions -fno-rtti -std=%,$(shell $(LLVM_CONFIG) --cxxflags 2>/dev/null))
LLVM_LDFLAGS  := $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)
LLVM_LIBS     := $(shell $(LLVM_CONFIG) --libs core native orcjit 2>/dev/null)
LLVM_SYSTEM_LIBS := $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)

CC  := clang
CXX := clang++

# Build mode: debug (default) or release.
#   make BUILD=release
BUILD ?= debug
ifeq ($(BUILD),release)
    OPT_FLAGS := -O2 -DNDEBUG
    DBG_FLAGS :=
else
    OPT_FLAGS := -O0
    DBG_FLAGS := -g
endif

CFLAGS   := -fPIC -Wall -Wextra $(OPT_FLAGS) $(DBG_FLAGS) -MMD -MP -Iangc/includes
CXXFLAGS := -std=c++23 -fPIC -Wall -Wextra $(OPT_FLAGS) $(DBG_FLAGS) -MMD -MP -Wno-trigraphs -Iangc/includes $(EXTRA_CXXFLAGS)

# Platform-specific SIMD flags (x86_64 gets AVX2+FMA, aarch64 gets baseline NEON).
UNAME_M := $(shell uname -m)
ifneq (,$(findstring x86_64,$(UNAME_M)))
    SIMD_CFLAGS := -mavx2 -mfma
else
    SIMD_CFLAGS :=
endif

# pkg-config helper: warn if a required package is missing.
# Usage: $(call pkg_warn,<pkg-name>)
pkg_warn = $(if $(shell pkg-config --exists $(1) 2>/dev/null || echo missing),$(warning [pkg-config] Package '$(1)' not found — some features may be unavailable))

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
$(call pkg_warn,libcurl)

LDFLAGS_BIN := $(LLVM_LDFLAGS) $(LLVM_LIBS) $(LLVM_SYSTEM_LIBS) $(CURL_LIBS)
LWS_CFLAGS  := $(shell pkg-config --cflags libwebsockets openssl 2>/dev/null)
LWS_LIBS    := $(shell pkg-config --libs libwebsockets openssl 2>/dev/null)
AMQP_CFLAGS := $(shell pkg-config --cflags librabbitmq 2>/dev/null)
AMQP_LIBS   := $(shell pkg-config --libs librabbitmq 2>/dev/null)
MQTT_CFLAGS := $(shell pkg-config --cflags libmosquitto libcjson 2>/dev/null)
MQTT_LIBS   := $(shell pkg-config --libs libmosquitto libcjson 2>/dev/null)
ARCHIVE_CFLAGS := $(shell pkg-config --cflags libarchive zlib 2>/dev/null)
ARCHIVE_LIBS  := $(shell pkg-config --libs libarchive zlib 2>/dev/null)
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   := $(shell pkg-config --libs sqlite3 2>/dev/null)
GMP_CFLAGS    := $(shell pkg-config --cflags gmp 2>/dev/null)
GMP_LIBS      := $(shell pkg-config --libs gmp 2>/dev/null)
PG_CFLAGS := $(shell pkg-config --cflags libpq 2>/dev/null)
PG_LIBS   := $(shell pkg-config --libs libpq 2>/dev/null)

ifeq ($(UNAME_S),Darwin)
    ifeq ($(LWS_CFLAGS),)
        LWS_CFLAGS := -I$(BREW_DIR)/opt/libwebsockets/include -I$(BREW_DIR)/include
    endif
endif

MOD_SRCS := $(wildcard modules/*/*.c)
MOD_OBJS := $(patsubst %.c,build/obj/%.o,$(MOD_SRCS))
MOD_OUTS := $(patsubst modules/%.c,build/modules/%.$(SO_EXT),$(filter modules/%.c,$(MOD_SRCS)))
MOD_OUTS := $(foreach f,$(MOD_SRCS),build/modules/$(notdir $(patsubst %.c,%.$(SO_EXT),$f)))

JSON_BR_SRC := modules/data/json_bridge.cpp
JSON_BR_OBJ := build/obj/modules/data/json_bridge.o

# ── GUI module (Dear ImGui + GLFW + OpenGL), vendored ───────────────────────
# This is an OPTIONAL module. It requires the system's GL driver + window-system
# dev headers (present on any graphical machine; see docs/20-native-modules.md),
# but ImGui and GLFW are built from vendored sources under modules/gui/vendor/.
# If the prereqs are missing the module is silently skipped — `make modules`
# and `make modules-minimal` both succeed without it. Toggle with
#   make GUI=1 modules          # force-enable (errors if prereqs missing)
#   make GUI=0 modules          # force-disable
IMGUI_DIR := modules/gui/vendor/imgui
GLFW_DIR  := modules/gui/vendor/glfw
GLFW_BUILD_DIR := build/third_party/glfw
IMGUI_INC := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(GLFW_DIR)/include

# GL profile: "gl33" (desktop OpenGL 3.3, default) or "gles3" (OpenGL ES 3.0,
# the native fast path on Asahi/Mesa ARM). Override with GUI_GL_PROFILE=gles3.
GUI_GL_PROFILE ?= gl33
ifeq ($(GUI_GL_PROFILE),gles3)
    IMGUI_GL_DEFS := -DIMGUI_IMPL_OPENGL_ES3
    GUI_GL_LIBS   := -lGLESv2 -lEGL
else ifeq ($(UNAME_S),Darwin)
    IMGUI_GL_DEFS :=
    GUI_GL_LIBS   := -framework OpenGL
else ifeq ($(WIN32),1)
    IMGUI_GL_DEFS :=
    GUI_GL_LIBS   := -lopengl32
else
    IMGUI_GL_DEFS :=
    GUI_GL_LIBS   := -lGL
endif

# Source list for ImGui core + the two backends we use.
IMGUI_SRCS := \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMGUI_DIR)/imgui_demo.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS := $(patsubst $(IMGUI_DIR)/%.cpp,build/obj/third_party/imgui/%.o,$(IMGUI_SRCS))

# Detect whether the gui module can build: cmake present + GL runtime + a
# window-system backend (X11 or Wayland). Detection is best-effort: missing
# pieces cause a graceful skip rather than a hard failure, unless GUI=1.
GUI_CAN_BUILD := 0
ifeq ($(GUI),0)
    GUI_CAN_BUILD := 0
else ifeq ($(shell command -v cmake 2>/dev/null),)
    GUI_CAN_BUILD := 0
else ifneq (,$(wildcard /usr/lib64/libGL.so* /usr/lib64/libEGL.so* /usr/lib/libGL.so* /usr/lib/libEGL.so*))
    GUI_CAN_BUILD := 1
endif
# Need at least one window-system backend available for GLFW to target.
ifeq ($(GUI_CAN_BUILD),1)
    ifeq ($(wildcard /usr/include/X11/Xlib.h /usr/include/wayland-client.h),)
        GUI_CAN_BUILD := 0
    endif
endif
ifeq ($(GUI),1)
    ifneq ($(GUI_CAN_BUILD),1)
        $(error GUI=1 but gui prerequisites are missing (need cmake + GL/EGL runtime + X11 or Wayland dev headers))
    endif
endif
ifeq ($(GUI_CAN_BUILD),1)
    MOD_OUTS += build/modules/gui.$(SO_EXT)
endif

ANGC_SRCS := $(shell find angc -name "*.cpp")
ANGC_OBJS := $(patsubst %.cpp,build/obj/%.o,$(ANGC_SRCS))
ANGC_OUT  := build/angc

.PHONY: all logo clean install uninstall lint install_vim uninstall_vim test test-ci test-cpp test-chaperone test-lang test-build test-kernel vendor-gui install_docs uninstall_docs

all: logo $(ANGC_OUT)
	@printf "$(BOLD)$(GREEN)>>> Build Completed Successfully <<<$(RESET)\n"

MINIMAL_MODS := $(filter-out \
	build/modules/websocket.$(SO_EXT) \
	build/modules/amqp.$(SO_EXT) \
	build/modules/mqtt.$(SO_EXT) \
	build/modules/http.$(SO_EXT) \
	build/modules/archive.$(SO_EXT) \
	build/modules/sqlite.$(SO_EXT) \
	build/modules/bigint.$(SO_EXT) \
	build/modules/matter.$(SO_EXT) \
	build/modules/jwt.$(SO_EXT) \
	build/modules/rpc.$(SO_EXT) \
	build/modules/json.$(SO_EXT) \
	build/modules/net.$(SO_EXT) \
	build/modules/process.$(SO_EXT) \
	,$(MOD_OUTS))

modules: logo $(MOD_OUTS)
	@FAILED=0; \
	for mod in $(MOD_OUTS); do \
		if [ ! -f "$$mod" ]; then \
			printf "  $(RED)[FAIL]$(RESET) %s\n" "$$(basename $$mod)"; \
			FAILED=$$((FAILED + 1)); \
		fi; \
	done; \
	if [ $$FAILED -gt 0 ]; then \
		printf "\n$(RED)$(BOLD)>>> $$FAILED module(s) failed to build <<<$(RESET)\n"; \
		exit 1; \
	else \
		printf "$(BOLD)$(GREEN)>>> Modules Built Successfully <<<$(RESET)\n"; \
	fi

modules-minimal: logo $(MINIMAL_MODS)
	@FAILED=0; \
	for mod in $(MINIMAL_MODS); do \
		if [ ! -f "$$mod" ]; then \
			printf "  $(RED)[FAIL]$(RESET) %s\n" "$$(basename $$mod)"; \
			FAILED=$$((FAILED + 1)); \
		fi; \
	done; \
	if [ $$FAILED -gt 0 ]; then \
		printf "\n$(RED)$(BOLD)>>> $$FAILED minimal module(s) failed to build <<<$(RESET)\n"; \
		exit 1; \
	else \
		printf "$(BOLD)$(GREEN)>>> Minimal Modules Built Successfully <<<$(RESET)\n"; \
	fi

logo:
	@printf "\n"
	@printf "$(CYAN) █████╗ ███╗   ██╗ ██████╗  █████╗ ██████╗  █████╗  $(RESET)\n"
	@printf "$(CYAN) ██╔══██╗████╗  ██║██╔════╝ ██╔══██╗██╔══██╗██╔══██╗$(RESET)\n"
	@printf "$(CYAN) ███████║██╔██╗ ██║██║  ███╗███████║██████╔╝███████║ $(RESET)\n"
	@printf "$(CYAN) ██╔══██║██║╚██╗██║██║   ██║██╔══██║██╔══██╗██╔══██║ $(RESET)\n"
	@printf "$(CYAN) ██║  ██║██║ ╚████║╚██████╔╝██║  ██║██║  ██║██║  ██║ $(RESET)\n"
	@printf "$(CYAN) ╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝ $(RESET)\n"
	@printf "$(MAGENTA)[MK] Building Angara v3 (LLVM) // cv2 was here$(RESET)\n\n"

# Fetch the vendored ImGui + GLFW sources (git submodules). Prerequisite for
# the gui module objects. No-op if the sources are already present.
vendor-gui:
ifeq ($(wildcard $(IMGUI_DIR)/imgui.h),)
	@printf "$(CYAN)[SUB]$(RESET) Initializing gui vendor submodules...\n"
	@git submodule update --init --recursive modules/gui/vendor
endif

build/obj/%.o: %.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s\n" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/obj/%.o: %.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s\n" "$<"
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -c $< -o $@

# M17: package manager sources use the vendored nlohmann/json header for strict
# JSON validation (replacing hand-rolled parsers). -Imodules/data resolves it.
build/obj/angc/backend/package/RegistryClient.o: angc/backend/package/RegistryClient.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (JSON)\n" "$<"
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -Imodules/data -c $< -o $@

build/obj/angc/backend/package/Lockfile.o: angc/backend/package/Lockfile.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (JSON)\n" "$<"
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -Imodules/data -c $< -o $@

build/obj/modules/net/http.o: modules/net/http.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (CURL)\n" "$<"
	@$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

build/obj/modules/net/websocket.o: modules/net/websocket.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (LWS)\n" "$<"
	@$(CC) $(CFLAGS) $(LWS_CFLAGS) -c $< -o $@

build/obj/modules/net/amqp.o: modules/net/amqp.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (AMQP)\n" "$<"
	@$(CC) $(CFLAGS) $(AMQP_CFLAGS) -c $< -o $@

build/obj/modules/net/mqtt.o: modules/net/mqtt.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (MQTT)\n" "$<"
	@$(CC) $(CFLAGS) $(MQTT_CFLAGS) -c $< -o $@

build/obj/modules/embedded/matter.o: modules/embedded/matter.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (MATTER/CURL)\n" "$<"
	@$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

build/obj/modules/fs/archive.o: modules/fs/archive.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (ARCHIVE)\n" "$<"
	@$(CC) $(CFLAGS) $(ARCHIVE_CFLAGS) -c $< -o $@

build/obj/modules/data/sqlite.o: modules/data/sqlite.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (SQLITE)\n" "$<"
	@$(CC) $(CFLAGS) $(SQLITE_CFLAGS) -c $< -o $@

build/obj/modules/math/bigint.o: modules/math/bigint.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (GMP)\n" "$<"
	@$(CC) $(CFLAGS) $(GMP_CFLAGS) -c $< -o $@

build/obj/modules/crypto/jwt.o: modules/crypto/jwt.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (JWT)\n" "$<"
	@$(CC) $(CFLAGS) -Imodules/data -c $< -o $@

build/obj/modules/net/net.o: modules/net/net.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (NET)\n" "$<"
	@$(CC) $(CFLAGS) -c $< -o $@

build/obj/modules/net/rpc.o: modules/net/rpc.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (RPC)\n" "$<"
	@$(CC) $(CFLAGS) -Imodules/data -c $< -o $@

build/obj/modules/data/json_bridge.o: modules/data/json_bridge.cpp
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (JSON Bridge)\n" "$<"
	@$(CXX) $(CXXFLAGS) -Iangc-ls/vendor -c $< -o $@

build/obj/modules/data/postgres.o: modules/data/postgres.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (POSTGRES)\n" "$<"
	@$(CC) $(CFLAGS) $(PG_CFLAGS) -c $< -o $@

build/obj/modules/simd/simd.o: modules/simd/simd.c
	@mkdir -p $(@D)
	@printf "$(GREEN)[CC]  $(RESET) %s (SIMD)\n" "$<"
	@$(CC) $(CFLAGS) $(SIMD_CFLAGS) -c $< -o $@

build/modules/http.$(SO_EXT): build/obj/modules/net/http.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(CURL_LIBS) -o $@

build/modules/websocket.$(SO_EXT): build/obj/modules/net/websocket.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(LWS_LIBS) -o $@

build/modules/time.$(SO_EXT): build/obj/modules/system/time.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(WIN32),1)
	@$(CC) $< -shared -o $@
else ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared $(SONAME_ARG) -o $@
else
	@$(CC) $< -shared $(SONAME_ARG) -lrt -o $@
endif

build/modules/amqp.$(SO_EXT): build/obj/modules/net/amqp.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(AMQP_LIBS) -o $@

build/modules/mqtt.$(SO_EXT): build/obj/modules/net/mqtt.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(MQTT_LIBS) -o $@

build/modules/matter.$(SO_EXT): build/obj/modules/embedded/matter.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(CURL_LIBS) -o $@

build/modules/math.$(SO_EXT): build/obj/modules/math/math.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(WIN32),1)
	@$(CC) $< -shared -o $@
else ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared $(SONAME_ARG) -o $@
else
	@$(CC) $< -shared $(SONAME_ARG) -lm -o $@
endif

build/modules/simd.$(SO_EXT): build/obj/modules/simd/simd.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -o $@

build/modules/sys.$(SO_EXT): build/obj/modules/system/sys.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
ifeq ($(WIN32),1)
	@$(CC) $< -shared -o $@
else ifeq ($(UNAME_S),Darwin)
	@$(CC) $< -shared $(SONAME_ARG) -lproc -o $@
else
	@$(CC) $< -shared $(SONAME_ARG) -o $@
endif

build/modules/json.$(SO_EXT): build/obj/modules/data/json.o $(JSON_BR_OBJ)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CXX) $^ -shared $(SONAME_ARG) -o $@

build/modules/rpc.$(SO_EXT): build/obj/modules/net/rpc.o $(JSON_BR_OBJ)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (RPC+JSON)\n" "$@"
	@$(CXX) $^ -shared $(SONAME_ARG) -o $@

build/modules/archive.$(SO_EXT): build/obj/modules/fs/archive.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (ARCHIVE+ZLIB)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(ARCHIVE_LIBS) -o $@

build/modules/sqlite.$(SO_EXT): build/obj/modules/data/sqlite.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (SQLITE3)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(SQLITE_LIBS) -o $@

build/modules/bigint.$(SO_EXT): build/obj/modules/math/bigint.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (GMP)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(GMP_LIBS) -o $@

build/modules/redis.$(SO_EXT): build/obj/modules/data/redis.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (REDIS)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -lhiredis -o $@

build/modules/postgres.$(SO_EXT): build/obj/modules/data/postgres.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (POSTGRES)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) $(PG_LIBS) -o $@

build/modules/mysql.$(SO_EXT): build/obj/modules/data/mysql.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (MYSQL)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -lmariadb -o $@

build/modules/tls.$(SO_EXT): build/obj/modules/net/tls.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (TLS)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -lssl -lcrypto -o $@

build/modules/jwt.$(SO_EXT): build/obj/modules/crypto/jwt.o $(JSON_BR_OBJ)
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (JWT+JSON)\n" "$@"
	@$(CXX) $^ -shared $(SONAME_ARG) -o $@

build/modules/net.$(SO_EXT): build/obj/modules/net/net.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (NET)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -o $@

build/modules/http_server.$(SO_EXT): build/obj/modules/net/http_server.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (HTTP)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -o $@

build/modules/io.$(SO_EXT): build/obj/modules/io/io.o
build/modules/term.$(SO_EXT): build/obj/modules/io/term.o
build/modules/color.$(SO_EXT): build/obj/modules/io/color.o
build/modules/eventloop.$(SO_EXT): build/obj/modules/io/eventloop.o
build/modules/os.$(SO_EXT): build/obj/modules/system/os.o
build/modules/env.$(SO_EXT): build/obj/modules/system/env.o
build/modules/process.$(SO_EXT): build/obj/modules/system/process.o
build/modules/unistd.$(SO_EXT): build/obj/modules/system/unistd.o
build/modules/fs.$(SO_EXT): build/obj/modules/fs/fs.o
build/modules/path.$(SO_EXT): build/obj/modules/fs/path.o
build/modules/watch.$(SO_EXT): build/obj/modules/fs/watch.o

# compress needs -lzstd
build/modules/compress.$(SO_EXT): build/obj/modules/fs/compress.o
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (ZSTD)\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -lzstd -lbz2 -llzma -o $@

build/modules/adv_string.$(SO_EXT): build/obj/modules/text/adv_string.o
build/modules/regex.$(SO_EXT): build/obj/modules/text/regex.o
build/modules/encoding.$(SO_EXT): build/obj/modules/text/encoding.o
build/modules/hash.$(SO_EXT): build/obj/modules/crypto/hash.o
build/modules/uuid.$(SO_EXT): build/obj/modules/crypto/uuid.o
build/modules/random.$(SO_EXT): build/obj/modules/math/random.o
build/modules/csv.$(SO_EXT): build/obj/modules/data/csv.o
build/modules/config.$(SO_EXT): build/obj/modules/data/config.o
build/modules/sort.$(SO_EXT): build/obj/modules/data/sort.o
build/modules/cli.$(SO_EXT): build/obj/modules/data/cli.o
build/modules/yaml.$(SO_EXT): build/obj/modules/data/yaml.o
build/modules/msgpack.$(SO_EXT): build/obj/modules/data/msgpack.o
build/modules/protobuf.$(SO_EXT): build/obj/modules/data/protobuf.o
build/modules/args.$(SO_EXT): build/obj/modules/data/args.o
build/modules/assert.$(SO_EXT): build/obj/modules/testing/assert.o
build/modules/calltest.$(SO_EXT): build/obj/modules/testing/calltest.o

build/modules/io.$(SO_EXT) build/modules/term.$(SO_EXT) build/modules/color.$(SO_EXT) \
build/modules/eventloop.$(SO_EXT) \
build/modules/log.$(SO_EXT) \
build/modules/os.$(SO_EXT) build/modules/env.$(SO_EXT) build/modules/process.$(SO_EXT) \
build/modules/unistd.$(SO_EXT) build/modules/fs.$(SO_EXT) build/modules/path.$(SO_EXT) \
build/modules/watch.$(SO_EXT) \
build/modules/adv_string.$(SO_EXT) build/modules/regex.$(SO_EXT) build/modules/encoding.$(SO_EXT) \
build/modules/hash.$(SO_EXT) build/modules/uuid.$(SO_EXT) build/modules/random.$(SO_EXT) \
build/modules/csv.$(SO_EXT) build/modules/config.$(SO_EXT) build/modules/sort.$(SO_EXT) \
build/modules/args.$(SO_EXT) build/modules/assert.$(SO_EXT) build/modules/calltest.$(SO_EXT) \
build/modules/cli.$(SO_EXT) build/modules/yaml.$(SO_EXT) \
build/modules/msgpack.$(SO_EXT) build/modules/protobuf.$(SO_EXT):
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s\n" "$@"
	@$(CC) $< -shared $(SONAME_ARG) -o $@

$(ANGC_OUT): $(ANGC_OBJS)
	@mkdir -p $(@D)
	@printf "$(CYAN)[BN] $(RESET) %s\n" "$@"
	@$(CXX) $^ $(LDFLAGS_BIN) -o $@

# ── GUI module build rules (ImGui + GLFW + OpenGL) ─────────────────────────
# All rules are gated by GUI_CAN_BUILD so that a missing GL/Wayland/X11 setup
# makes the whole subsystem a silent no-op rather than a build error.

# GLFW as a static library, built once via CMake.
$(GLFW_BUILD_DIR)/src/libglfw3.a: | vendor-gui
	@mkdir -p $(@D)
	@printf "$(CYAN)[CM] $(RESET) Configuring GLFW (static)...\n"
	@cmake -S $(GLFW_DIR) -B $(GLFW_BUILD_DIR) \
		-DBUILD_SHARED_LIBS=OFF \
		-DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
		-DCMAKE_C_COMPILER=$(CC) >/dev/null
	@printf "$(CYAN)[CM] $(RESET) Building GLFW...\n"
	@cmake --build $(GLFW_BUILD_DIR) --parallel >/dev/null

# ImGui core + backends. Each object needs the ImGui and GLFW headers and the
# GL profile defines (for the opengl3 backend's bundled loader).
build/obj/third_party/imgui/%.o: $(IMGUI_DIR)/%.cpp | vendor-gui
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (IMGUI)\n" "$<"
	@$(CXX) $(CXXFLAGS) $(IMGUI_INC) $(IMGUI_GL_DEFS) -c $< -o $@

# The gui module itself (C++).
build/obj/modules/gui/gui.o: modules/gui/gui.cpp | vendor-gui
	@mkdir -p $(@D)
	@printf "$(GREEN)[CX] $(RESET) %s (GUI)\n" "$<"
	@$(CXX) $(CXXFLAGS) $(IMGUI_INC) $(IMGUI_GL_DEFS) -c $< -o $@

ifeq ($(GUI_CAN_BUILD),1)
build/modules/gui.$(SO_EXT): build/obj/modules/gui/gui.o $(IMGUI_OBJS) $(GLFW_BUILD_DIR)/src/libglfw3.a
	@mkdir -p $(@D)
	@printf "$(MAGENTA)[MD] $(RESET) %s (IMGUI+GLFW+GL)\n" "$@"
	@$(CXX) build/obj/modules/gui/gui.o $(IMGUI_OBJS) $(GLFW_BUILD_DIR)/src/libglfw3.a \
		-shared $(SONAME_ARG) $(GUI_GL_LIBS) -o $@
else
# Prereqs missing — emit a no-op stub so the prerequisites list resolves, but
# never actually create the .so. The aggregate `modules` target tolerates the
# absence; GUI=1 is the explicit way to demand it (and errors at parse time).
build/modules/gui.$(SO_EXT):
	@printf "$(YELLOW)[SKIP]$(RESET) gui module (missing GL/Wayland/X11 prereqs; use GUI=1 to require)\n"
endif

install: install_libraries install_executables
	@printf "$(BOLD)$(GREEN)>>> Full Installation Complete <<<$(RESET)\n"

install_libraries: $(MOD_OUTS)
	@printf "$(MAGENTA)[IN] $(RESET) Installing Modules to %s\n" "$(DESTDIR)$(INSTALL_MOD_DIR)"
	@mkdir -p $(DESTDIR)$(INSTALL_MOD_DIR)
	@install -m 755 build/modules/*.$(SO_EXT) $(DESTDIR)$(INSTALL_MOD_DIR)/

install_executables: $(ANGC_OUT)
	@printf "$(CYAN)[IN] $(RESET) Installing Executable to %s\n" "$(DESTDIR)$(INSTALL_BIN_DIR)"
	@mkdir -p $(DESTDIR)$(INSTALL_BIN_DIR)
	@cp $(ANGC_OUT) $(DESTDIR)$(INSTALL_BIN_DIR)/

uninstall:
	@printf "$(RED)[RM] $(RESET) Uninstalling Angara...\n"
	@rm -f $(DESTDIR)$(INSTALL_BIN_DIR)/angc
	@rm -rf $(DESTDIR)$(INSTALL_MOD_DIR)
	@printf "$(BOLD)$(GREEN)>>> Uninstall Complete <<<$(RESET)\n"

install_docs:
	@printf "$(MAGENTA)[IN] $(RESET) Installing Documentation to %s\n" "$(DESTDIR)$(INSTALL_DOC_DIR)"
	@mkdir -p $(DESTDIR)$(INSTALL_DOC_DIR)
	@cp README.md $(DESTDIR)$(INSTALL_DOC_DIR)/
	@cp CHAPERONE.md $(DESTDIR)$(INSTALL_DOC_DIR)/
	@cp SECURITY.md $(DESTDIR)$(INSTALL_DOC_DIR)/
	@cp LICENSE $(DESTDIR)$(INSTALL_DOC_DIR)/
	@cp -r docs/*.md $(DESTDIR)$(INSTALL_DOC_DIR)/
	@printf "$(BOLD)$(GREEN)>>> Docs Installed to $(DESTDIR)$(INSTALL_DOC_DIR) <<<$(RESET)\n"

uninstall_docs:
	@printf "$(RED)[RM] $(RESET) Removing documentation from %s\n" "$(DESTDIR)$(INSTALL_DOC_DIR)"
	@rm -rf $(DESTDIR)$(INSTALL_DOC_DIR)
	@printf "$(BOLD)$(GREEN)>>> Docs Uninstalled <<<$(RESET)\n"

ifeq ($(NVIM_RUNTIME),)
    NVIM_RUNTIME := $(HOME)/.local/share/nvim/site
endif
ifeq ($(VIM_RUNTIME),)
    VIM_RUNTIME := $(HOME)/.vim
endif

install_vim:
	@printf "$(CYAN)[IN] $(RESET) Installing Angara syntax highlight plugin...\n"
ifeq ($(VIM_EDITOR),nvim)
	@mkdir -p $(NVIM_RUNTIME)/pack/angara/start/angara
	@cp -r vim-angara/syntax  $(NVIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/ftdetect $(NVIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/ftplugin $(NVIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/indent  $(NVIM_RUNTIME)/pack/angara/start/angara/
	@printf "$(BOLD)$(GREEN)>>> Installed to $(NVIM_RUNTIME)/pack/angara/start/angara/ <<<$(RESET)\n"
else ifeq ($(VIM_EDITOR),vim)
	@mkdir -p $(VIM_RUNTIME)/pack/angara/start/angara
	@cp -r vim-angara/syntax  $(VIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/ftdetect $(VIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/ftplugin $(VIM_RUNTIME)/pack/angara/start/angara/
	@cp -r vim-angara/indent  $(VIM_RUNTIME)/pack/angara/start/angara/
	@printf "$(BOLD)$(GREEN)>>> Installed to $(VIM_RUNTIME)/pack/angara/start/angara/ <<<$(RESET)\n"
else
	@printf "$(YELLOW)Usage: make install_vim VIM_EDITOR=vim|nvim$(RESET)\n"
	@printf "$(YELLOW)  Optional: VIM_RUNTIME=<path>  (default: ~/.vim or ~/.local/share/nvim)$(RESET)\n"
	@exit 1
endif

uninstall_vim:
ifeq ($(VIM_EDITOR),nvim)
	@rm -rf $(NVIM_RUNTIME)/pack/angara
	@printf "$(RED)[RM] $(RESET) Removed plugin from $(NVIM_RUNTIME)/pack/angara\n"
else ifeq ($(VIM_EDITOR),vim)
	@rm -rf $(VIM_RUNTIME)/pack/angara
	@printf "$(RED)[RM] $(RESET) Removed plugin from $(VIM_RUNTIME)/pack/angara\n"
else
	@printf "$(YELLOW)Usage: make uninstall_vim VIM_EDITOR=vim|nvim$(RESET)\n"
	@exit 1
endif

# C++ unit tests for compiler components (lexer, parser, type checker)
TEST_CPP_SRCS := $(shell find tests/cpp -name "*.cpp")
TEST_CPP_OUT  := build/test-runner

test-cpp: $(ANGC_OBJS) $(TEST_CPP_SRCS)
	@mkdir -p $(@D)
	@printf "$(CYAN)[TS] $(RESET) Building C++ test runner\n"
	@$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) -Iangc/includes \
		$(filter-out build/obj/angc/src/main.o,$(ANGC_OBJS)) \
		$(TEST_CPP_SRCS) $(LDFLAGS_BIN) -o $(TEST_CPP_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running C++ unit tests\n"
	@./$(TEST_CPP_OUT)

# Chaperone memory-safety test suite (positive + negative .an programs).
test-chaperone: $(ANGC_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running Chaperone memory-safety tests\n"
	@./tests/chaperone/run_chaperone_tests.sh ./$(ANGC_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running Chaperone LSP integration test\n"
	@./tests/chaperone/test_lsp_chaperone.sh ./$(ANGC_OUT)

# Language positive/negative suite (compile & run + expected compile errors).
# In CI mode (CI_MODE=1), failures propagate. Otherwise, they're reported but don't halt.
test-lang: $(ANGC_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running language test suite\n"
ifeq ($(CI_MODE),1)
	@./tests/lang/run_tests.sh ./$(ANGC_OUT)
else
	@./tests/lang/run_tests.sh ./$(ANGC_OUT) || true
endif

# .abs build-system suite: native-module linking (Bug 2) + multi-file wrappers
# (Bug 1) through the project build path.
test-build: $(ANGC_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running .abs build-system tests\n"
ifeq ($(CI_MODE),1)
	@./tests/build_system/run_abs_tests.sh ./$(ANGC_OUT)
else
	@./tests/build_system/run_abs_tests.sh ./$(ANGC_OUT) || true
endif

# Kernel-mode gate suite (try/spawn/Mutex/native-attach → E900-E903, plus
# positive: valid --kernel code emits a relocatable object with no entry point).
test-kernel: $(ANGC_OUT)
	@printf "$(CYAN)[TS] $(RESET) Running kernel-mode gate tests\n"
	@./tests/kernel/run_kernel_tests.sh ./$(ANGC_OUT)
	@./tests/kernel/run_alloc_swap_test.sh ./$(ANGC_OUT) clang

# Aggregate: all tests (local use — non-fatal).
test: test-cpp test-chaperone test-lang test-build test-kernel

# Aggregate: all tests (CI use — failures propagate).
test-ci:
	@$(MAKE) test-cpp
	@$(MAKE) test-chaperone
	@$(MAKE) test-lang CI_MODE=1
	@$(MAKE) test-build CI_MODE=1
	@$(MAKE) test-kernel

clean:
	@printf "$(RED)[CL] $(RESET) Cleaning build directory...\n"
	@rm -rf build

lint:
	@printf "$(CYAN)[LT] $(RESET) Running clang-tidy...\n"
	@clang-tidy --config-file=.clang-tidy angc/**/*.cpp -- $(CXXFLAGS) 2>/dev/null || true
	@clang-tidy --config-file=.clang-tidy modules/**/*.c -- $(CFLAGS) 2>/dev/null || true
	@printf "$(BOLD)$(GREEN)>>> Lint Complete <<<$(RESET)\n"

-include $(shell find build/obj -name '*.d' 2>/dev/null)
