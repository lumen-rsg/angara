/// Angara Dear ImGui module — GUI via GLFW + OpenGL3. Links: imgui, glfw, OpenGL.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

struct AngaraImGuiContext {
    GLFWwindow*   window = nullptr;
    ImGuiContext*  imgui_ctx = nullptr;
    bool           initialized = false;
    const char*    glsl_version = "#version 150";
};

static void finalize_context(void* data) {
    auto* ctx = static_cast<AngaraImGuiContext*>(data);
    if (!ctx) return;
    if (ctx->initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(ctx->imgui_ctx);
    }
    if (ctx->window) glfwDestroyWindow(ctx->window);
    delete ctx;
}

static AngaraImGuiContext* get_context(AngaraObject self) {
    if (!ang_is_obj(self)) { ang_api->throw_error("imgui: expected Context"); return nullptr; }
    void* raw = ang_api->native_instance_data(self);
    if (!raw) { ang_api->throw_error("imgui: Context data null"); return nullptr; }
    return static_cast<AngaraImGuiContext*>(raw);
}

AngaraObject Angara_imgui_Context(int arg_count, AngaraObject* args) {
    const char* title = "Angara ImGui";
    int width = 1280, height = 720;
    if (arg_count >= 1 && IS_STR(args[0])) title = ang_api->as_cstr(args[0]);
    if (arg_count >= 2 && ang_is_i64(args[1])) width = (int)ang_as_i64(args[1]);
    if (arg_count >= 3 && ang_is_i64(args[2])) height = (int)ang_as_i64(args[2]);

    fprintf(stderr, "[IMGUI] Context(\"%s\", %d, %d)\n", title, width, height); fflush(stderr);

    if (!glfwInit()) {
        fprintf(stderr, "[IMGUI] ERROR glfwInit failed\n"); fflush(stderr);
        ang_api->throw_error("imgui: glfwInit failed");
        return ang_nil();
    }
    fprintf(stderr, "[IMGUI] glfwInit OK\n"); fflush(stderr);

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    fprintf(stderr, "[IMGUI] Creating window...\n"); fflush(stderr);
    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "[IMGUI] ERROR glfwCreateWindow failed\n"); fflush(stderr);
        glfwTerminate();
        ang_api->throw_error("imgui: glfwCreateWindow failed");
        return ang_nil();
    }
    fprintf(stderr, "[IMGUI] Window created OK\n"); fflush(stderr);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ImGuiContext* imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_ctx);
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    fprintf(stderr, "[IMGUI] All backends init OK\n"); fflush(stderr);

    auto* ctx = new AngaraImGuiContext();
    ctx->window = window;
    ctx->imgui_ctx = imgui_ctx;
    ctx->initialized = true;
    ctx->glsl_version = glsl_version;

    return ang_api->native_instance_new(ctx, finalize_context, "Context");
}

AngaraObject Angara_Context_destroy(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]);
    if (!ctx || !ctx->initialized) return ang_nil();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(ctx->imgui_ctx);
    if (ctx->window) glfwDestroyWindow(ctx->window);
    glfwTerminate();
    ctx->window = nullptr; ctx->imgui_ctx = nullptr; ctx->initialized = false;
    return ang_nil();
}

AngaraObject Angara_Context_new_frame(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    return ang_nil();
}

AngaraObject Angara_Context_render(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(ctx->window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return ang_nil();
}

AngaraObject Angara_Context_swap_buffers(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx || !ctx->window) return ang_nil();
    glfwSwapBuffers(ctx->window); return ang_nil();
}

AngaraObject Angara_Context_poll_events(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    glfwPollEvents(); return ang_nil();
}

AngaraObject Angara_Context_window_should_close(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]);
    if (!ctx || !ctx->window) return ang_bool(true);
    return ang_bool(glfwWindowShouldClose(ctx->window) != 0);
}

AngaraObject Angara_Context_get_display_size(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx || !ctx->window) return ang_nil();
    int w, h; glfwGetFramebufferSize(ctx->window, &w, &h);
    auto rec = ang_api->record_new();
    ang_api->record_set(rec, "width", ang_f64((double)w));
    ang_api->record_set(rec, "height", ang_f64((double)h));
    return rec;
}

AngaraObject Angara_Context_get_fps(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_f64(0.0);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    return ang_f64(ImGui::GetIO().Framerate);
}

AngaraObject Angara_Context_set_clear_color(int ac, AngaraObject* args) {
    if (ac < 5) return ang_nil();
    float r = ang_is_f64(args[1]) ? (float)ang_as_f64(args[1]) : (float)ang_as_i64(args[1]);
    float g = ang_is_f64(args[2]) ? (float)ang_as_f64(args[2]) : (float)ang_as_i64(args[2]);
    float b = ang_is_f64(args[3]) ? (float)ang_as_f64(args[3]) : (float)ang_as_i64(args[3]);
    float a = ang_is_f64(args[4]) ? (float)ang_as_f64(args[4]) : (float)ang_as_i64(args[4]);
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    glClearColor(r, g, b, a); return ang_nil();
}

AngaraObject Angara_Context_set_window_title(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx || !ctx->window) return ang_nil();
    if (ac < 2 || !IS_STR(args[1])) return ang_nil();
    glfwSetWindowTitle(ctx->window, ang_api->as_cstr(args[1])); return ang_nil();
}

AngaraObject Angara_Context_begin(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_bool(false);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    if (ac < 2 || !IS_STR(args[1])) return ang_bool(false);
    return ang_bool(ImGui::Begin(ang_api->as_cstr(args[1])));
}

AngaraObject Angara_Context_end(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::End(); return ang_nil();
}

AngaraObject Angara_Context_text(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    if (ac < 2 || !IS_STR(args[1])) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    ImGui::Text("%s", ang_api->as_cstr(args[1])); return ang_nil();
}

AngaraObject Angara_Context_text_colored(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx || ac < 6) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* s = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "";
    float r = ang_is_f64(args[2]) ? (float)ang_as_f64(args[2]) : (float)ang_as_i64(args[2]);
    float g = ang_is_f64(args[3]) ? (float)ang_as_f64(args[3]) : (float)ang_as_i64(args[3]);
    float b = ang_is_f64(args[4]) ? (float)ang_as_f64(args[4]) : (float)ang_as_i64(args[4]);
    float a = ang_is_f64(args[5]) ? (float)ang_as_f64(args[5]) : (float)ang_as_i64(args[5]);
    ImGui::TextColored(ImVec4(r,g,b,a), "%s", s); return ang_nil();
}

AngaraObject Angara_Context_button(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_bool(false);
    if (ac < 2 || !IS_STR(args[1])) return ang_bool(false);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    return ang_bool(ImGui::Button(ang_api->as_cstr(args[1])));
}

AngaraObject Angara_Context_checkbox(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_bool(false);
    if (ac < 3) return ang_bool(false);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* label = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "##cb";
    bool val = ang_is_bool(args[2]) ? ang_as_bool(args[2]) : (ang_as_i64(args[2]) != 0);
    ImGui::Checkbox(label, &val); return ang_bool(val);
}

AngaraObject Angara_Context_slider_float(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_f64(0.0);
    if (ac < 5) return ang_f64(0.0);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* label = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "##sl";
    float val = ang_is_f64(args[2]) ? (float)ang_as_f64(args[2]) : (float)ang_as_i64(args[2]);
    float mn = ang_is_f64(args[3]) ? (float)ang_as_f64(args[3]) : (float)ang_as_i64(args[3]);
    float mx = ang_is_f64(args[4]) ? (float)ang_as_f64(args[4]) : (float)ang_as_i64(args[4]);
    ImGui::SliderFloat(label, &val, mn, mx); return ang_f64((double)val);
}

AngaraObject Angara_Context_slider_int(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_i64(0);
    if (ac < 5) return ang_i64(0);
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* label = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "##sli";
    int val = (int)(ang_is_i64(args[2]) ? ang_as_i64(args[2]) : (int64_t)ang_as_f64(args[2]));
    int mn = (int)(ang_is_i64(args[3]) ? ang_as_i64(args[3]) : (int64_t)ang_as_f64(args[3]));
    int mx = (int)(ang_is_i64(args[4]) ? ang_as_i64(args[4]) : (int64_t)ang_as_f64(args[4]));
    ImGui::SliderInt(label, &val, mn, mx); return ang_i64((int64_t)val);
}

AngaraObject Angara_Context_input_text(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_api->string("");
    if (ac < 3) return ang_api->string("");
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* label = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "##inp";
    const char* cur = IS_STR(args[2]) ? ang_api->as_cstr(args[2]) : "";
    static char buf[1024];
    strncpy(buf, cur, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    ImGui::InputText(label, buf, sizeof(buf));
    return ang_api->string(buf);
}

AngaraObject Angara_Context_color_edit(int ac, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    if (ac < 5) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx);
    const char* label = IS_STR(args[1]) ? ang_api->as_cstr(args[1]) : "##clr";
    float r = ang_is_f64(args[2]) ? (float)ang_as_f64(args[2]) : (float)ang_as_i64(args[2]);
    float g = ang_is_f64(args[3]) ? (float)ang_as_f64(args[3]) : (float)ang_as_i64(args[3]);
    float b = ang_is_f64(args[4]) ? (float)ang_as_f64(args[4]) : (float)ang_as_i64(args[4]);
    float color[3] = { r, g, b };
    ImGui::ColorEdit3(label, color);
    auto rec = ang_api->record_new();
    ang_api->record_set(rec, "r", ang_f64((double)color[0]));
    ang_api->record_set(rec, "g", ang_f64((double)color[1]));
    ang_api->record_set(rec, "b", ang_f64((double)color[2]));
    return rec;
}

AngaraObject Angara_Context_same_line(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::SameLine(); return ang_nil();
}

AngaraObject Angara_Context_separator(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::Separator(); return ang_nil();
}

AngaraObject Angara_Context_spacing(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::Spacing(); return ang_nil();
}

AngaraObject Angara_Context_indent(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::Indent(); return ang_nil();
}

AngaraObject Angara_Context_unindent(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::Unindent(); return ang_nil();
}

AngaraObject Angara_Context_newline(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::NewLine(); return ang_nil();
}

AngaraObject Angara_Context_style_colors_dark(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::StyleColorsDark(); return ang_nil();
}

AngaraObject Angara_Context_style_colors_light(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::StyleColorsLight(); return ang_nil();
}

AngaraObject Angara_Context_style_colors_classic(int, AngaraObject* args) {
    auto* ctx = get_context(args[0]); if (!ctx) return ang_nil();
    ImGui::SetCurrentContext(ctx->imgui_ctx); ImGui::StyleColorsClassic(); return ang_nil();
}

static const AngaraMethodDef CONTEXT_METHODS[] = {
    {"destroy",             Angara_Context_destroy,             "->n"},
    {"new_frame",           Angara_Context_new_frame,           "->n"},
    {"render",              Angara_Context_render,              "->n"},
    {"swap_buffers",        Angara_Context_swap_buffers,        "->n"},
    {"poll_events",         Angara_Context_poll_events,         "->n"},
    {"window_should_close", Angara_Context_window_should_close, "->b"},
    {"get_display_size",    Angara_Context_get_display_size,    "->{}"},
    {"get_fps",             Angara_Context_get_fps,             "->d"},
    {"set_clear_color",     Angara_Context_set_clear_color,     "dddd->n"},
    {"set_window_title",    Angara_Context_set_window_title,    "s->n"},
    {"begin",               Angara_Context_begin,               "s->b"},
    {"end",                 Angara_Context_end,                 "->n"},
    {"text",                Angara_Context_text,                "s->n"},
    {"text_colored",        Angara_Context_text_colored,        "sdddd->n"},
    {"button",              Angara_Context_button,              "s->b"},
    {"checkbox",            Angara_Context_checkbox,            "sb->b"},
    {"slider_float",        Angara_Context_slider_float,        "sddd->d"},
    {"slider_int",          Angara_Context_slider_int,          "siii->i"},
    {"input_text",          Angara_Context_input_text,          "ss->s"},
    {"color_edit",          Angara_Context_color_edit,          "sddd->{}"},
    {"same_line",           Angara_Context_same_line,           "->n"},
    {"separator",           Angara_Context_separator,           "->n"},
    {"spacing",             Angara_Context_spacing,             "->n"},
    {"indent",              Angara_Context_indent,              "->n"},
    {"unindent",            Angara_Context_unindent,            "->n"},
    {"newline",             Angara_Context_newline,             "->n"},
    {"style_colors_dark",   Angara_Context_style_colors_dark,   "->n"},
    {"style_colors_light",  Angara_Context_style_colors_light,  "->n"},
    {"style_colors_classic",Angara_Context_style_colors_classic,"->n"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef CONTEXT_CLASS_DEF = {
    "Context", NULL, CONTEXT_METHODS
};

static const AngaraFuncDef IMGUI_EXPORTS[] = {
    {"Context", Angara_imgui_Context, "si?i?->Context", &CONTEXT_CLASS_DEF},
    ANGARA_FUNC_END
};

extern "C" {

ANGARA_MODULE_INIT(imgui) {
    ang_api = api;
    *def_count = (sizeof(IMGUI_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return IMGUI_EXPORTS;
}

}
