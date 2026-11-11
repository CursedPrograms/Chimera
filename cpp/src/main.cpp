// LayerTack - an image editor: layers, masks, blend modes, selections, adjustments and filters.
// SDL2 (window, input), OpenGL 3, Dear ImGui (the interface).
//
//   LayerTack [picture or project]
//   LayerTack --capture out.bmp [picture] [--script demo]    a screenshot from a hidden window, then quit
#include "app.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/Helvetica.ttc"};
    for (const char* f : fonts) {
        if (FILE* t = fopen(f, "rb")) {
            fclose(t);
            static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
            io.Fonts->AddFontFromFileTTF(f, 16.0f, nullptr, ranges);
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

int main(int argc, char** argv) {
    std::string file, capture, scriptName;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (a == "--script" && i + 1 < argc) scriptName = argv[++i];
        else file = a;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (!capture.empty()) flags |= SDL_WINDOW_HIDDEN;
    else flags |= SDL_WINDOW_MAXIMIZED;
    SDL_Window* win = SDL_CreateWindow("LayerTack", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960, flags);
    if (!win) return 1;
    SDL_SetWindowMinimumSize(win, 900, 560);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    char* bp = SDL_GetBasePath();
    const std::string base = bp ? bp : "";
    SDL_free(bp);
    if (SDL_Surface* icon = SDL_LoadBMP((base + "assets/icon.bmp").c_str())) { SDL_SetWindowIcon(win, icon); SDL_FreeSurface(icon); }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    loadFont();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    char* pp = SDL_GetPrefPath("Cursed Entertainment", "LayerTack");
    App::init(win, pp ? pp : "");
    SDL_free(pp);
    if (!file.empty()) App::open(file);

    int frames = 0, quiet = 0;                   // quiet: frames since a Changeling finished
    while (!App::wantsQuit()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) App::requestQuit();
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(win)) App::requestQuit();
            if (e.type == SDL_DROPFILE) { App::dropFile(e.drop.file); SDL_free(e.drop.file); }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        App::frame();
        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (!capture.empty()) {
            if (frames == 2 && !scriptName.empty()) App::script(scriptName);
            quiet = App::busy() ? 0 : quiet + 1;
            if (frames >= 8 && quiet >= 4) {
                // the frame, read back and saved as a BMP (SDL writes those itself)
                std::vector<unsigned char> px((size_t)w * h * 4);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                std::vector<unsigned char> flip(px.size());
                for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
                SDL_SaveBMP(s, capture.c_str());
                SDL_FreeSurface(s);
                break;
            }
        }
        SDL_GL_SwapWindow(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
