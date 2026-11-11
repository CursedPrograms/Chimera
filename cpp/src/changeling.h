// LayerTack - Filter > Changeling: the Changelings (Imago, Tiresias, Chronos, Natio, Cosmeta, Tonsor) as filters.
// LayerTack doesn't carry them: it finds Changeling Studio on this PC and runs its chain.py, which runs each
// Changeling in its own Python environment. The steps and their options come from `chain.py --steps --json`, so a
// new Changeling or option shows up here without changing LayerTack.
#pragma once
#include <string>
#include <vector>

namespace Changeling {

struct Option {
    std::string key, kind, label;                // kind: choice, number, file (required), file? (optional), check
    std::vector<std::string> choices;
    float min = 0, max = 1;
    // the value
    int choice = 0; float number = 0; bool check = false; std::string file;
};

struct Step {
    std::string app, about, missing;             // missing: why it can't run on this PC ("" = ready)
    std::vector<Option> options;
    std::string stepArg() const;                 // for chain.py --step: "Chronos age=0.50 keep_gender=auto ..."
    std::string notReady() const;                // a required photo not picked yet, or missing
};

// Changeling Studio's folder (the one with chain.py): remembered, CHANGELING_STUDIO, or found near LayerTack.
std::string studioDir();
void setStudioDir(const std::string& dir);      // remembered in LayerTack's settings folder
void init(const std::string& prefDir, const std::string& exeDir);

// A program run in the background: its output comes in line by line, Stop ends it and everything it started.
struct Job {
    enum State { IDLE, RUNNING, DONE, FAILED };
    ~Job();
    bool start(const std::vector<std::string>& argv, const std::string& cwd);
    void stop();
    State state();
    std::vector<std::string> lines();            // everything it has said so far
    std::string output();                        // the same, joined
    struct Impl; Impl* impl = nullptr;
};

// the steps, asked of chain.py in the background: call loadSteps() once, then poll stepsLoading() each frame
void loadSteps();
bool stepsLoading();
std::vector<Step>& steps();
const std::string& stepsError();                 // why there are none

// one step on a picture: in.png -> out.png
bool run(Job& job, const Step& step, const std::string& inPng, const std::string& outPng);
std::string workDir();                           // where the pictures go (a temp folder)
}
