// =============================================================================
// Angara GUI module — Dear ImGui immediate-mode GUI, backed by GLFW + OpenGL.
// =============================================================================
//
// Usage from Angara:
//
//   attach Window, Texture from gui;
//   attach button, text, image from gui;     // module-level widget helpers
//
//   func on_frame(window as Window) -> nil {
//       gui.begin("Controls");
//       if (gui.button("Quit")) { window.close(); }
//       gui.end();
//   }
//
//   let win = Window("Hello", 800, 600);
//   win.run(on_frame);        // blocks, calls on_frame once per frame
//
// Immediate-mode model: the per-frame callback is invoked once every rendered
// frame inside Window.run()'s poll loop (same pattern as eventloop.Loop.run).
// Widgets are plain functions called from within that callback; their return
// values reflect interaction for this frame only.
//
// Threading: OpenGL, GLFW and ImGui all require their calls to originate from a
// single thread — the one that called Window.run(). Keep UI single-threaded.
//
// Build: see Makefile "GUI module" section. ImGui + GLFW are built from vendored
// sources under modules/gui/vendor/; only the system GL driver + window-system
// dev headers are external. The module is optional: if those are absent it is
// skipped and `make modules` still succeeds.
// =============================================================================
#include <GLFW/glfw3.h>

// Our own OpenGL function declarations come from the system GL headers.
#ifdef IMGUI_IMPL_OPENGL_ES3
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Angara.h's own extern "C" block closes at its end, so we reopen one here
// so that every Angara-exported function below gets C linkage.  The compiler
// dlopens this .so and calls the symbols by unmangled name.
#include "Angara.h"

extern "C" {

// Module functions receive the runtime vtable via ANGARA_MODULE_INIT; the
// `ang_api` global is declared in Angara.h (line 287) and set in our init.

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// =============================================================================
// §1  Global ImGui/GLFW state
// =============================================================================
//
// glfwInit + the ImGui context are established once for the process and torn
// down after the last window closes. These guards keep the per-process setup
// idempotent across multiple Window() constructors.
static bool g_glfw_ready = false;
static bool g_imgui_ready = false;

static void glfw_error_callback(int err, const char* desc) {
    std::fprintf(stderr, "[gui] GLFW error %d: %s\n", err, desc);
}

// One-time process-global initialization. Returns false (and throws) on failure.
static bool gui_global_init() {
    if (g_imgui_ready) return true;

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        ang_api->throw_error("gui: glfwInit() failed.");
        return false;
    }
    g_glfw_ready = true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    g_imgui_ready = true;
    return true;
}

// Tear down the global state once no windows remain. Called from finalize_window
// when the last window is destroyed; safe to call repeatedly.
static void gui_global_shutdown() {
    if (!g_imgui_ready) return;
    // Backends must be shut down before the context if still initialized.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    g_imgui_ready = false;
    if (g_glfw_ready) {
        glfwTerminate();
        g_glfw_ready = false;
    }
}

// =============================================================================
// §2  Window class — GLFWwindow + ImGui backend state
// =============================================================================

struct WindowData {
    GLFWwindow* win;
    bool        imgui_init;   // whether the per-window backends were initialized
};

static void finalize_window(void* raw) {
    WindowData* d = (WindowData*)raw;
    if (!d) return;
    if (d->imgui_init) {
        // Make the context current so the backend shutdown touches the right GL.
        if (d->win) glfwMakeContextCurrent(d->win);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        d->imgui_init = false;
    }
    if (d->win) {
        glfwDestroyWindow(d->win);
        d->win = nullptr;
    }
    delete d;
    // The process-global ImGui/GLFW state stays initialized for the lifetime of
    // the program (GLFW 3.4 has no live-window count API to gate shutdown on,
    // and tearing down glfwTerminate while other code may still touch it is
    // risky). It is reclaimed by the OS at exit.
}

// Window(title, width, height) -> Window
AngaraObject Angara_gui_Window(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !IS_STR(args[0]) || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("gui.Window(title as string, width as i64, height as i64): bad arguments.");
        return ang_nil();
    }
    if (!gui_global_init()) { return ang_nil(); }  // throws on failure

    const char* title = ang_api->as_cstr(args[0]);
    int w = (int)ang_as_i64(args[1]);
    int h = (int)ang_as_i64(args[2]);
    if (w < 1 || h < 1) {
        ang_api->throw_error("gui.Window: width and height must be positive.");
        return ang_nil();
    }

#ifdef IMGUI_IMPL_OPENGL_ES3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!win) {
        ang_api->throw_error("gui.Window: failed to create GLFW window (check GL drivers/display).");
        return ang_nil();
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);  // vsync

#if defined(IMGUI_IMPL_OPENGL_ES3)
    const char* glsl_version = "#version 300 es";
#else
    const char* glsl_version = "#version 330 core";
#endif
    if (!ImGui_ImplGlfw_InitForOpenGL(win, true) ||
        !ImGui_ImplOpenGL3_Init(glsl_version)) {
        glfwDestroyWindow(win);
        ang_api->throw_error("gui.Window: failed to initialize ImGui GL backend.");
        return ang_nil();
    }

    WindowData* d = new WindowData{ win, true };
    return ang_api->native_instance_new(d, finalize_window, "Window");
}

// window.run(frame_fn) — blocks on the render loop, invoking frame_fn once per
// frame. frame_fn receives the window as its argument. Mirrors eventloop.run.
AngaraObject Angara_Window_run(int arg_count, AngaraObject* args) {
    if (arg_count < 2) {
        ang_api->throw_error("window.run(frame_fn): expected a callback.");
        return ang_nil();
    }
    WindowData* d = (WindowData*)ang_api->native_instance_data(args[0]);
    if (!d || !d->win) { ang_api->throw_error("window.run: invalid window."); return ang_nil(); }
    AngaraObject frame_fn = args[1];

    glfwMakeContextCurrent(d->win);
    ang_api->incref(frame_fn);

    while (!glfwWindowShouldClose(d->win)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // User draws UI this frame. The window is passed as the sole argument.
        AngaraObject argv[1] = { args[0] };
        ang_api->call(frame_fn, 1, argv);

        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(d->win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(d->win);
    }

    ang_api->decref(frame_fn);
    return ang_nil();
}

// window.should_close() -> bool
AngaraObject Angara_Window_should_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    WindowData* d = (WindowData*)ang_api->native_instance_data(args[0]);
    return ang_bool(d && d->win ? glfwWindowShouldClose(d->win) : true);
}

// window.close() -> nil  (queues the window for closure at the next poll)
AngaraObject Angara_Window_close(int arg_count, AngaraObject* args) {
    (void)arg_count;
    WindowData* d = (WindowData*)ang_api->native_instance_data(args[0]);
    if (d && d->win) glfwSetWindowShouldClose(d->win, GLFW_TRUE);
    return ang_nil();
}

// =============================================================================
// §3  Texture class — an RGBA8 OpenGL texture for image display
// =============================================================================
//
// Upload is via list<i64> of packed RGBA8 bytes (one byte value 0-255 per list
// element, 4 per pixel). This keeps the module portable across Angara versions;
// a future FFI raw-array upload path can skip the per-element extraction.

struct TextureData {
    unsigned int tex;   // GLuint
    int w;
    int h;
};

static void finalize_texture(void* raw) {
    TextureData* t = (TextureData*)raw;
    if (!t) return;
    if (t->tex) {
        glDeleteTextures(1, &t->tex);
        t->tex = 0;
    }
    delete t;
}

// Texture(width, height) -> Texture  (zero-initialized RGBA8)
AngaraObject Angara_gui_Texture(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_i64(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("gui.Texture(width as i64, height as i64): bad arguments.");
        return ang_nil();
    }
    int w = (int)ang_as_i64(args[0]);
    int h = (int)ang_as_i64(args[1]);
    if (w < 1 || h < 1) {
        ang_api->throw_error("gui.Texture: width and height must be positive.");
        return ang_nil();
    }

    TextureData* t = new TextureData{ 0, w, h };
    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Allocate zero-initialized storage so the texture is usable before upload.
    size_t bytes = (size_t)w * (size_t)h * 4;
    unsigned char* zero = (unsigned char*)std::calloc(bytes, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
    std::free(zero);
    glBindTexture(GL_TEXTURE_2D, 0);

    return ang_api->native_instance_new(t, finalize_texture, "Texture");
}

// texture.upload(bytes as list<i64>) -> nil
// bytes must contain width*height*4 entries, each a byte value 0-255 (RGBA8).
AngaraObject Angara_Texture_upload(int arg_count, AngaraObject* args) {
    if (arg_count < 2) {
        ang_api->throw_error("texture.upload(bytes): expected a byte list.");
        return ang_nil();
    }
    TextureData* t = (TextureData*)ang_api->native_instance_data(args[0]);
    if (!t || !t->tex) { ang_api->throw_error("texture.upload: invalid texture."); return ang_nil(); }
    if (ang_api->obj_type(args[1]) != ANG_OBJ_LIST) {
        ang_api->throw_error("texture.upload: expected a list<i64> of RGBA8 bytes.");
        return ang_nil();
    }

    size_t need = (size_t)t->w * (size_t)t->h * 4;
    size_t got = ang_api->list_len(args[1]);
    if (got < need) {
        ang_api->throw_error("texture.upload: byte list too short (need w*h*4).");
        return ang_nil();
    }

    unsigned char* buf = (unsigned char*)std::malloc(need);
    if (!buf) { ang_api->throw_error("texture.upload: out of memory."); return ang_nil(); }
    for (size_t i = 0; i < need; i++) {
        int64_t b = ang_as_i64(ang_api->list_get(args[1], (int64_t)i));
        buf[i] = (unsigned char)(b & 0xFF);
    }

    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glBindTexture(GL_TEXTURE_2D, 0);
    std::free(buf);
    return ang_nil();
}

// texture.upload_packed(pixels as list<i64>) -> nil
// Each i64 is a packed RGBA8 pixel: (a<<24)|(b<<16)|(g<<8)|r.
// The list must have at least width*height entries (1 element per pixel).
AngaraObject Angara_Texture_upload_packed(int arg_count, AngaraObject* args) {
    if (arg_count < 2) {
        ang_api->throw_error("texture.upload_packed(pixels): expected a packed-pixel list.");
        return ang_nil();
    }
    TextureData* t = (TextureData*)ang_api->native_instance_data(args[0]);
    if (!t || !t->tex) { ang_api->throw_error("texture.upload_packed: invalid texture."); return ang_nil(); }
    if (ang_api->obj_type(args[1]) != ANG_OBJ_LIST) {
        ang_api->throw_error("texture.upload_packed: expected a list<i64> of packed pixels.");
        return ang_nil();
    }

    size_t pixel_count = (size_t)t->w * (size_t)t->h;
    size_t got = ang_api->list_len(args[1]);
    if (got < pixel_count) {
        ang_api->throw_error("texture.upload_packed: pixel list too short (need w*h).");
        return ang_nil();
    }

    size_t byte_count = pixel_count * 4;
    unsigned char* buf = (unsigned char*)std::malloc(byte_count);
    if (!buf) { ang_api->throw_error("texture.upload_packed: out of memory."); return ang_nil(); }
    for (size_t i = 0; i < pixel_count; i++) {
        int64_t packed = ang_as_i64(ang_api->list_get(args[1], (int64_t)i));
        size_t off = i * 4;
        buf[off + 0] = (unsigned char)( packed        & 0xFF);  // R
        buf[off + 1] = (unsigned char)((packed >> 8)  & 0xFF);  // G
        buf[off + 2] = (unsigned char)((packed >> 16) & 0xFF);  // B
        buf[off + 3] = (unsigned char)((packed >> 24) & 0xFF);  // A
    }

    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glBindTexture(GL_TEXTURE_2D, 0);
    std::free(buf);
    return ang_nil();
}

// texture.width() -> i64 / texture.height() -> i64
AngaraObject Angara_Texture_width(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TextureData* t = (TextureData*)ang_api->native_instance_data(args[0]);
    return ang_i64(t ? t->w : 0);
}
AngaraObject Angara_Texture_height(int arg_count, AngaraObject* args) {
    (void)arg_count;
    TextureData* t = (TextureData*)ang_api->native_instance_data(args[0]);
    return ang_i64(t ? t->h : 0);
}

// =============================================================================
// §4  Immediate-mode widget helpers (module-level functions)
// =============================================================================
//
// These are intended to be called from within the per-frame callback passed to
// Window.run(). They reflect interaction for the current frame only.

// gui.text(label as string) -> nil
AngaraObject Angara_gui_text(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("gui.text(label as string): bad argument.");
        return ang_nil();
    }
    ImGui::TextUnformatted(ang_api->as_cstr(args[0]));
    return ang_nil();
}

// gui.button(label as string) -> bool  (true the frame it's clicked)
AngaraObject Angara_gui_button(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("gui.button(label as string): bad argument.");
        return ang_nil();
    }
    return ang_bool(ImGui::Button(ang_api->as_cstr(args[0])));
}

// gui.checkbox(label as string, current as bool) -> bool  (new value)
AngaraObject Angara_gui_checkbox(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !ang_is_bool(args[1])) {
        ang_api->throw_error("gui.checkbox(label as string, current as bool): bad arguments.");
        return ang_nil();
    }
    bool v = ang_as_bool(args[1]);
    ImGui::Checkbox(ang_api->as_cstr(args[0]), &v);
    return ang_bool(v);
}

// gui.slider_float(label, current, min, max) -> float  (new value)
AngaraObject Angara_gui_slider_float(int arg_count, AngaraObject* args) {
    if (arg_count < 4 || !IS_STR(args[0]) || !ang_is_f64(args[1]) ||
        !ang_is_f64(args[2]) || !ang_is_f64(args[3])) {
        ang_api->throw_error("gui.slider_float(label, current, min, max): bad arguments.");
        return ang_nil();
    }
    float v = (float)ang_as_f64(args[1]);
    float lo = (float)ang_as_f64(args[2]);
    float hi = (float)ang_as_f64(args[3]);
    ImGui::SliderFloat(ang_api->as_cstr(args[0]), &v, lo, hi);
    return ang_f64((double)v);
}

// gui.slider_int(label, current, min, max) -> i64  (new value)
AngaraObject Angara_gui_slider_int(int arg_count, AngaraObject* args) {
    if (arg_count < 4 || !IS_STR(args[0]) || !ang_is_i64(args[1]) ||
        !ang_is_i64(args[2]) || !ang_is_i64(args[3])) {
        ang_api->throw_error("gui.slider_int(label, current, min, max): bad arguments.");
        return ang_nil();
    }
    int v = (int)ang_as_i64(args[1]);
    int lo = (int)ang_as_i64(args[2]);
    int hi = (int)ang_as_i64(args[3]);
    ImGui::SliderInt(ang_api->as_cstr(args[0]), &v, lo, hi);
    return ang_i64((int64_t)v);
}

// texture.draw(width, height) -> nil  — draw the texture as an ImGui image
AngaraObject Angara_Texture_draw(int arg_count, AngaraObject* args) {
    if (arg_count < 3) {
        ang_api->throw_error("texture.draw(width, height): bad arguments.");
        return ang_nil();
    }
    TextureData* t = (TextureData*)ang_api->native_instance_data(args[0]);
    if (!t || !t->tex) { ang_api->throw_error("texture.draw: invalid texture."); return ang_nil(); }
    if (!ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("texture.draw: width and height must be i64.");
        return ang_nil();
    }
    float w = (float)ang_as_i64(args[1]);
    float h = (float)ang_as_i64(args[2]);
    ImGui::Image(ImTextureRef((ImTextureID)(uint64_t)t->tex), ImVec2(w, h));
    return ang_nil();
}

// gui.begin(name as string) -> bool / gui.end() -> nil  (a child window/region)
AngaraObject Angara_gui_begin(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("gui.begin(name as string): bad argument.");
        return ang_nil();
    }
    return ang_bool(ImGui::Begin(ang_api->as_cstr(args[0])));
}
AngaraObject Angara_gui_end(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    ImGui::End();
    return ang_nil();
}

// gui.same_line() -> nil  (place next widget on the same line)
AngaraObject Angara_gui_same_line(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    ImGui::SameLine();
    return ang_nil();
}

// gui.fps() -> float  (current frame rate as reported by ImGui)
AngaraObject Angara_gui_fps(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetIO().Framerate);
}

// gui.rect(x, y, w, h, r, g, b, a) -> nil  — filled rectangle via ImDrawList
AngaraObject Angara_gui_rect(int arg_count, AngaraObject* args) {
    if (arg_count < 8) {
        ang_api->throw_error("gui.rect(x,y,w,h,r,g,b,a): expected 8 i64 arguments.");
        return ang_nil();
    }
    float x = (float)ang_as_i64(args[0]), y = (float)ang_as_i64(args[1]);
    float w = (float)ang_as_i64(args[2]), h = (float)ang_as_i64(args[3]);
    int r = (int)ang_as_i64(args[4]), g = (int)ang_as_i64(args[5]);
    int b = (int)ang_as_i64(args[6]), a = (int)ang_as_i64(args[7]);
    ImU32 col = IM_COL32(r, g, b, a);
    ImVec2 p_min(x, y);
    ImVec2 p_max(x + w, y + h);
    ImGui::GetBackgroundDrawList()->AddRectFilled(p_min, p_max, col);
    return ang_nil();
}

AngaraObject Angara_gui_byte_at(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("gui.byte_at(str, idx): bad arguments.");
        return ang_nil();
    }
    const char* s = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    int64_t idx = ang_as_i64(args[1]);
    if (idx < 0 || (size_t)idx >= len) return ang_i64(0);
    return ang_i64((int64_t)(unsigned char)s[idx]);
}

// gui.input_text(label as string, current as string, max_len as i64) -> string
// Renders a text-input field. Returns the (possibly edited) string each frame.
AngaraObject Angara_gui_input_text(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !IS_STR(args[0]) || !IS_STR(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("gui.input_text(label, current, max_len): bad arguments.");
        return ang_nil();
    }
    const char* label   = ang_api->as_cstr(args[0]);
    const char* current = ang_api->as_cstr(args[1]);
    size_t cur_len      = ang_api->str_len(args[1]);
    int64_t max_len     = ang_as_i64(args[2]);

    if (max_len < 1 || max_len > 65536) {
        ang_api->throw_error("gui.input_text: max_len must be 1..65536.");
        return ang_nil();
    }

    size_t buf_size = (size_t)max_len;
    char* buf = (char*)std::malloc(buf_size);
    if (!buf) { ang_api->throw_error("gui.input_text: out of memory."); return ang_nil(); }

    size_t copy_len = cur_len < buf_size - 1 ? cur_len : buf_size - 1;
    std::memcpy(buf, current, copy_len);
    buf[copy_len] = '\0';

    ImGui::InputText(label, buf, buf_size);

    AngaraObject result = ang_api->string(buf);
    std::free(buf);
    return result;
}

// gui.combo(label as string, current_idx as i64, items as string) -> i64
// items is a single string with entries separated by \0 (null bytes),
// terminated by \0\0 (double null). Returns the newly selected index.
AngaraObject Angara_gui_combo(int arg_count, AngaraObject* args) {
    if (arg_count < 3 || !IS_STR(args[0]) || !ang_is_i64(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("gui.combo(label, current_idx, items): bad arguments.");
        return ang_nil();
    }
    const char* label  = ang_api->as_cstr(args[0]);
    int current        = (int)ang_as_i64(args[1]);
    size_t items_len   = ang_api->str_len(args[2]);
    const char* items_buf = ang_api->as_cstr(args[2]);

    // Split the null-separated items string into an array of char* pointers.
    std::vector<const char*> item_ptrs;
    const char* p   = items_buf;
    const char* end = items_buf + items_len;
    while (p < end) {
        item_ptrs.push_back(p);
        while (p < end && *p != '\0') p++;
        if (p < end && *p == '\0') p++;
    }
    while (!item_ptrs.empty() && item_ptrs.back()[0] == '\0')
        item_ptrs.pop_back();

    if (!item_ptrs.empty()) {
        ImGui::Combo(label, &current, item_ptrs.data(), (int)item_ptrs.size());
    }
    return ang_i64((int64_t)current);
}

// =============================================================================
// §4b  Mouse / keyboard input helpers
// =============================================================================

// gui.is_item_hovered() -> bool
AngaraObject Angara_gui_is_item_hovered(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_bool(ImGui::IsItemHovered());
}

// gui.mouse_clicked(button as i64) -> bool
AngaraObject Angara_gui_mouse_clicked(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("gui.mouse_clicked(button): expected i64.");
        return ang_nil();
    }
    int btn = (int)ang_as_i64(args[0]);
    return ang_bool(ImGui::IsMouseClicked(btn));
}

// gui.mouse_down(button as i64) -> bool
AngaraObject Angara_gui_mouse_down(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("gui.mouse_down(button): expected i64.");
        return ang_nil();
    }
    int btn = (int)ang_as_i64(args[0]);
    return ang_bool(ImGui::IsMouseDown(btn));
}

// gui.mouse_drag_x(button as i64) -> f64
AngaraObject Angara_gui_mouse_drag_x(int arg_count, AngaraObject* args) {
    int btn = 0;
    if (arg_count >= 1 && ang_is_i64(args[0])) btn = (int)ang_as_i64(args[0]);
    return ang_f64((double)ImGui::GetMouseDragDelta(btn).x);
}

// gui.mouse_drag_y(button as i64) -> f64
AngaraObject Angara_gui_mouse_drag_y(int arg_count, AngaraObject* args) {
    int btn = 0;
    if (arg_count >= 1 && ang_is_i64(args[0])) btn = (int)ang_as_i64(args[0]);
    return ang_f64((double)ImGui::GetMouseDragDelta(btn).y);
}

// gui.mouse_wheel() -> f64
AngaraObject Angara_gui_mouse_wheel(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetIO().MouseWheel);
}

// gui.is_mouse_hovering_rect(x, y, w, h) -> bool
AngaraObject Angara_gui_is_mouse_hovering_rect(int arg_count, AngaraObject* args) {
    if (arg_count < 4 || !ang_is_i64(args[0]) || !ang_is_i64(args[1]) ||
        !ang_is_i64(args[2]) || !ang_is_i64(args[3])) {
        ang_api->throw_error("gui.is_mouse_hovering_rect(x,y,w,h): expected 4 i64.");
        return ang_nil();
    }
    float x = (float)ang_as_i64(args[0]);
    float y = (float)ang_as_i64(args[1]);
    float w = (float)ang_as_i64(args[2]);
    float h = (float)ang_as_i64(args[3]);
    return ang_bool(ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h)));
}

// gui.mouse_x() -> f64  /  gui.mouse_y() -> f64
// Mouse position in absolute screen coordinates.
AngaraObject Angara_gui_mouse_x(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetMousePos().x);
}
AngaraObject Angara_gui_mouse_y(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetMousePos().y);
}

// gui.cursor_screen_x() -> f64  /  gui.cursor_screen_y() -> f64
// Top-left of the window content area in absolute screen coordinates.
// Used to convert mouse screen coords to draw-list coords.
AngaraObject Angara_gui_cursor_screen_x(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetCursorScreenPos().x);
}
AngaraObject Angara_gui_cursor_screen_y(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_f64((double)ImGui::GetCursorScreenPos().y);
}

// gui.nil_texture() -> Texture?  — returns nil typed as optional Texture.
// Workaround: the constructors occupy the export names, so `let t as Texture?`
// resolves Texture to the function, not the class.  This helper returns a
// properly-typed nil so the caller can write `let t = gui.nil_texture();`
// and get type-inferred Optional<Texture>.
AngaraObject Angara_gui_nil_texture(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_nil();
}

// =============================================================================
// §5  Export tables + module entry point
// =============================================================================

static const AngaraMethodDef WINDOW_METHODS[] = {
    {"run",          (AngaraMethodFn)Angara_Window_run,          "a->n"},
    {"should_close", (AngaraMethodFn)Angara_Window_should_close, "->b"},
    {"close",        (AngaraMethodFn)Angara_Window_close,        "->n"},
    {NULL, NULL, NULL}
};
static const AngaraClassDef WINDOW_CLASS = { "Window", NULL, WINDOW_METHODS };

static const AngaraMethodDef TEXTURE_METHODS[] = {
    {"upload",        (AngaraMethodFn)Angara_Texture_upload,        "l<i>->n"},
    {"upload_packed", (AngaraMethodFn)Angara_Texture_upload_packed, "l<i>->n"},
    {"draw",          (AngaraMethodFn)Angara_Texture_draw,          "ii->n"},
    {"width",         (AngaraMethodFn)Angara_Texture_width,         "->i"},
    {"height",        (AngaraMethodFn)Angara_Texture_height,        "->i"},
    {NULL, NULL, NULL}
};
static const AngaraClassDef TEXTURE_CLASS = { "Texture", NULL, TEXTURE_METHODS };

static const AngaraFuncDef GUI_EXPORTS[] = {
    // Constructors
    {"Window",  Angara_gui_Window,  "sii->Window",  &WINDOW_CLASS},
    {"Texture", Angara_gui_Texture, "ii->Texture",  &TEXTURE_CLASS},

    // Immediate-mode widgets (called from inside Window.run's frame callback)
    {"text",        Angara_gui_text,        "s->n"},
    {"button",      Angara_gui_button,      "s->b"},
    {"checkbox",    Angara_gui_checkbox,    "sb->b"},
    {"slider_float", Angara_gui_slider_float, "sddd->d"},
    {"slider_int",  Angara_gui_slider_int,  "siii->i"},
    {"begin",       Angara_gui_begin,       "s->b"},
    {"end",         Angara_gui_end,         "->n"},
    {"same_line",   Angara_gui_same_line,   "->n"},
    {"fps",         Angara_gui_fps,         "->d"},
    {"byte_at",     Angara_gui_byte_at,     "si->i"},
    {"rect",        Angara_gui_rect,        "iiiiiiii->n"},
    {"input_text",  Angara_gui_input_text,  "ssi->s"},
    {"combo",       Angara_gui_combo,       "sis->i"},
    {"nil_texture", Angara_gui_nil_texture, "->Texture?"},

    // Mouse / keyboard input
    {"is_item_hovered", Angara_gui_is_item_hovered, "->b"},
    {"mouse_clicked",   Angara_gui_mouse_clicked,   "i->b"},
    {"mouse_down",      Angara_gui_mouse_down,      "i->b"},
    {"mouse_drag_x",    Angara_gui_mouse_drag_x,    "i->d"},
    {"mouse_drag_y",    Angara_gui_mouse_drag_y,    "i->d"},
    {"mouse_wheel",     Angara_gui_mouse_wheel,     "->d"},
    {"is_mouse_hovering_rect", Angara_gui_is_mouse_hovering_rect, "iiii->b"},
    {"mouse_x",       Angara_gui_mouse_x,       "->d"},
    {"mouse_y",       Angara_gui_mouse_y,       "->d"},
    {"cursor_screen_x", Angara_gui_cursor_screen_x, "->d"},
    {"cursor_screen_y", Angara_gui_cursor_screen_y, "->d"},

    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(gui) {
    ang_api = api;
    *def_count = (sizeof(GUI_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return GUI_EXPORTS;
}

} // extern "C"
