// LayerTack - the app: the document being edited, the tools and the windows round it.
#pragma once
#include "doc.h"
#include "ops.h"
#include <string>

struct SDL_Window;

namespace App {
void init(SDL_Window* win, const std::string& prefDir);
bool open(const std::string& path);               // a picture or a .ltk project
void frame();                                    // draws the whole window (ImGui)
bool wantsQuit();
void requestQuit();
void dropFile(const std::string& path);          // a file dropped on the window
// for --capture: run a little scripted session (open, select subject, mask...) before the screenshot
void script(const std::string& what);
bool busy();                                    // a Changeling still running (--capture waits for it)
}
