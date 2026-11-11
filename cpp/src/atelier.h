// LayerTack - finding the other Atelier apps it uses as plugins (Bathos, Mimic): a command-line tool of theirs, looked
// for next to LayerTack, in its plugins\<App> folder, where the app is installed (%LOCALAPPDATA%\Programs\<App>, as
// the Atelier launcher and the apps' own installers put them), or in an <App> folder near LayerTack; or the one the
// user picked (remembered in LayerTack's settings folder).
#pragma once
#include <string>

namespace Atelier {
// app: "Bathos"; exe: "bathos-cli" (".exe" added on Windows); pref: the file that remembers the user's pick
std::string findTool(const std::string& app, const std::string& exe, const std::string& prefFile, const std::string& exeFolder);
void remember(const std::string& prefFile, const std::string& path);
}
