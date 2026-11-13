// Chimera - an image editor: layers, masks, blend modes, selections, adjustments and filters.
// SDL2 (window, input), OpenGL 3, Dear ImGui (the interface).
//
//   Chimera [picture or project]
//   Chimera --capture out.bmp [picture] [--script demo]    a screenshot from a hidden window, then quit
#include "app.h"
#include "changeling.h"
#include "splash.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static bool fileExists(const char* f) {
    if (FILE* t = fopen(f, "rb")) { fclose(t); return true; }
    return false;
}

// the interface font, plus the splash's: a light one for the name, a small one for the credits
static ImFont* addFont(const char* file, float size) {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    ImFont* font = io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges);
    // Segoe UI has no stars or other symbols (the credit's ☆): Windows' symbol font fills them in
    static const char* symbols = "C:/Windows/Fonts/seguisym.ttf";
    static const ImWchar symbolRanges[] = {0x2190, 0x21FF, 0x2600, 0x26FF, 0};
    if (fileExists(symbols)) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(symbols, size, &merge, symbolRanges);
    }
    return font;
}

static void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/Helvetica.ttc"};
    for (const char* f : fonts) {
        if (!fileExists(f)) continue;
        addFont(f, 16.0f);
        const char* light = fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : f;
        ImFont* title = addFont(light, 50.0f);
        ImFont* small = addFont(f, 13.0f);
        Splash::setFonts(title, small);
        return;
    }
    io.Fonts->AddFontDefault();
}

// ---------- the splash window: shown while Chimera loads ----------
static SDL_Window* splashWin = nullptr;
static Uint32 splashStart = 0;
static bool splashSkip = false, quitEarly = false;   // skip: clicked or Escape; quitEarly: closed (Alt+F4)

static void splashSay(const char* status, float progress) {
    if (!splashWin) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) quitEarly = true;
        if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) splashSkip = true;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, status, progress);
    ImGui::Render();
    int w, h;
    SDL_GL_GetDrawableSize(splashWin, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(splashWin);
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
    // hidden until it's ready: the splash shows meanwhile (--capture never shows it)
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
    SDL_Window* win = SDL_CreateWindow("Chimera", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960, flags);
    if (!win) return 1;
    SDL_SetWindowMinimumSize(win, 900, 560);
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    char* bp = SDL_GetBasePath();
    const std::string base = bp ? bp : "";
    SDL_free(bp);
    SDL_Surface* icon = SDL_LoadBMP((base + "assets/icon.bmp").c_str());
    if (icon) SDL_SetWindowIcon(win, icon);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    loadFont();

    // the splash: a small borderless window sharing the main window's OpenGL context
    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Chimera", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(win, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    splashStart = SDL_GetTicks();
    splashSay("Loading the interface...", 0.1f);

#ifdef _WIN32
    {   // settings and downloaded models from the editor's first folder name move to Chimera's, once
        const char* ad = getenv("APPDATA");
        if (ad) {
            std::error_code ec;
            const std::filesystem::path base = std::filesystem::u8path(ad) / "Cursed Entertainment";
            if (std::filesystem::exists(base / "LayerTack", ec) && !std::filesystem::exists(base / "Chimera", ec))
                std::filesystem::rename(base / "LayerTack", base / "Chimera", ec);
        }
    }
#endif
    char* pp = SDL_GetPrefPath("Cursed Entertainment", "Chimera");
    App::init(win, pp ? pp : "", splashWin ? splashSay : nullptr);
    SDL_free(pp);
    if (!file.empty()) {
        const std::string name = file.substr(file.find_last_of("/\\") + 1);
        splashSay(("Opening " + name + "...").c_str(), 0.8f);
        App::open(file);
    }

    if (splashWin) {
        // Filter > Changeling's list comes from Changeling Studio in the background: give it a moment to arrive,
        // and keep the splash up long enough to be seen. A click or Escape goes straight in.
        for (;;) {
            const Uint32 t = SDL_GetTicks() - splashStart;
            const bool loading = Changeling::stepsLoading();
            if (splashSkip || quitEarly || (t >= 1500 && (!loading || t >= 4000))) break;
            if (loading) splashSay("Asking Changeling Studio for its filters...", 0.6f + 0.35f * (t > 4000 ? 1.0f : t / 4000.0f));
            else splashSay("Ready", 1.0f);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        splashWin = nullptr;
        SDL_GL_MakeCurrent(win, gl);
        ImGui_ImplSDL2_InitForOpenGL(win, gl);
    }
    if (capture.empty()) {
        SDL_ShowWindow(win);
        SDL_MaximizeWindow(win);
        SDL_RaiseWindow(win);
    }
    if (quitEarly) App::requestQuit();
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

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
