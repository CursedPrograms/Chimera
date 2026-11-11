// LayerTack - Filter > Changeling: finds Changeling Studio, asks its chain.py for the steps, runs them.
#include "changeling.h"
#include "util.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Changeling {

static std::string prefFile, exeFolder, studio;

// ---------- a very small JSON reader (enough for chain.py --steps --json) ----------
struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double n = 0; std::string s;
    std::vector<J> a; std::vector<std::pair<std::string, J>> o;
    const J& operator[](const char* k) const {
        static const J none;
        for (auto& [key, v] : o) if (key == k) return v;
        return none;
    }
};
struct Parser {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                switch (*p) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': break;
                case 'u': {
                    if (e - p < 5) return false;
                    unsigned c = (unsigned)strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16); p += 4;
                    if (c >= 0xD800 && c < 0xDC00 && e - p > 6 && p[1] == '\\' && p[2] == 'u') {   // a pair
                        unsigned lo = (unsigned)strtoul(std::string(p + 3, p + 7).c_str(), nullptr, 16); p += 6;
                        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (c < 0x80) out += (char)c;
                    else if (c < 0x800) { out += (char)(0xC0 | c >> 6); out += (char)(0x80 | (c & 63)); }
                    else if (c < 0x10000) { out += (char)(0xE0 | c >> 12); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
                    else { out += (char)(0xF0 | c >> 18); out += (char)(0x80 | (c >> 12 & 63)); out += (char)(0x80 | (c >> 6 & 63)); out += (char)(0x80 | (c & 63)); }
                    break;
                }
                default: out += *p;
                }
            } else out += *p;
            ++p;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool val(J& v) {
        ws();
        if (p >= e) return false;
        if (*p == '{') {
            v.t = J::OBJ; ++p; ws();
            if (p < e && *p == '}') { ++p; return true; }
            while (true) {
                ws(); std::string k; if (!str(k)) return false;
                ws(); if (p >= e || *p != ':') return false; ++p;
                J x; if (!val(x)) return false; v.o.emplace_back(k, std::move(x));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            v.t = J::ARR; ++p; ws();
            if (p < e && *p == ']') { ++p; return true; }
            while (true) {
                J x; if (!val(x)) return false; v.a.push_back(std::move(x));
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') { v.t = J::STR; return str(v.s); }
        if (!strncmp(p, "true", 4)) { v.t = J::BOOL; v.b = true; p += 4; return true; }
        if (!strncmp(p, "false", 5)) { v.t = J::BOOL; p += 5; return true; }
        if (!strncmp(p, "null", 4)) { p += 4; return true; }
        char* end = nullptr;
        v.n = strtod(p, &end);
        if (end == p) return false;
        v.t = J::NUM; p = end;
        return true;
    }
};

// ---------- the step as chain.py --step wants it ----------
static std::string fmt(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

std::string Step::stepArg() const {
    std::string s = app;
    for (const Option& o : options) {
        std::string v;
        if (o.kind == "choice") { v = o.choices.empty() ? "" : o.choices[o.choice]; for (char& c : v) if (c == ' ') c = '_'; }
        else if (o.kind == "number") v = fmt(o.number);
        else if (o.kind == "check") v = o.check ? "on" : "off";
        else { if (o.file.empty()) continue; v = "\"" + o.file + "\""; }   // a photo, quoted (it may have spaces)
        s += " " + o.key + "=" + v;
    }
    return s;
}

std::string Step::notReady() const {
    if (!missing.empty()) return missing;
    for (const Option& o : options)
        if (o.kind == "file" && o.file.empty()) return "Pick the " + o.label;
    return "";
}

// ---------- where Changeling Studio is ----------
static bool isStudio(const fs::path& d) { std::error_code ec; return fs::exists(d / "chain.py", ec); }

std::string studioDir() { return studio; }

void setStudioDir(const std::string& dir) {
    studio = dir;
    if (FILE* f = openFile(prefFile, "wb")) { fputs(dir.c_str(), f); fclose(f); }
}

static std::string findStudio() {
    // 1. the one picked before
    if (FILE* f = openFile(prefFile, "rb")) {
        char b[4096] = {0}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
        std::string s(b, n);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty() && isStudio(u8path(s))) return s;
    }
    // 2. CHANGELING_STUDIO
    if (const char* e = std::getenv("CHANGELING_STUDIO")) if (isStudio(u8path(e))) return e;
    // 3. next to LayerTack, or next to a folder it's in (D:/cc/LayerTack/cpp/dist/LayerTack -> D:/cc/Changeling-Studio)
    std::error_code ec;
    for (fs::path d = u8path(exeFolder); !d.empty(); d = d.parent_path()) {
        for (const char* name : {"Changeling-Studio", "Changeling Studio"})
            if (isStudio(d / name)) return (d / name).u8string();
        if (d == d.parent_path()) break;
    }
    // 4. the usual places
    std::vector<fs::path> places;
    if (const char* h = std::getenv(
#ifdef _WIN32
            "USERPROFILE"
#else
            "HOME"
#endif
            )) places.push_back(u8path(h));
#ifdef _WIN32
    for (const char* drive : {"C:/", "D:/", "E:/", "F:/"}) { places.push_back(u8path(drive)); places.push_back(u8path(std::string(drive) + "cc")); }
#endif
    for (const fs::path& p : places)
        if (isStudio(p / "Changeling-Studio")) return (p / "Changeling-Studio").u8string();
    return "";
}

void init(const std::string& prefDir, const std::string& exeDir) {
    prefFile = (u8path(prefDir) / "changeling-studio.txt").u8string();
    exeFolder = exeDir;
    studio = findStudio();
}

// A Python to run chain.py with: chain.py needs only the standard library, so any of the Changelings' own
// environments does (the same ones chain.py looks for), else the PC's own Python.
static std::string python() {
#ifdef _WIN32
    const char* inEnv = "Scripts/python.exe";
#else
    const char* inEnv = "bin/python";
#endif
    std::error_code ec;
    const fs::path s = u8path(studio);
    for (auto [app, env] : {std::pair{"Cosmeta", "venv"}, {"Tiresias", "psdenv"}, {"Chronos", "psdenv"}, {"Imago", "psdenv"}, {"Natio", "psdenv"}, {"Echo", "venv"}}) {
        const fs::path py = s / (std::string("Changeling-") + app) / env / inEnv;
        if (fs::exists(py, ec)) return py.u8string();
    }
#ifdef _WIN32
    for (const wchar_t* name : {L"python.exe", L"py.exe"}) {
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, name, nullptr, MAX_PATH, buf, nullptr)) return fs::path(buf).u8string();
    }
    return "";
#else
    return "python3";
#endif
}

std::string workDir() {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) / "LayerTack-Changeling";
    fs::create_directories(d, ec);
    return d.u8string();
}

// ---------- running a program ----------
struct Job::Impl {
    std::thread reader;
    std::mutex m;
    std::vector<std::string> lines;
    std::atomic<int> state{IDLE};
    std::atomic<bool> stopped{false};
#ifdef _WIN32
    HANDLE job = nullptr, proc = nullptr;
#else
    pid_t pid = -1;
#endif
    void add(std::string& line) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (!line.empty()) { std::lock_guard<std::mutex> g(m); lines.push_back(line); if (lines.size() > 400) lines.erase(lines.begin()); }
        line.clear();
    }
};

#ifdef _WIN32
static std::wstring widen(const std::string& s) { return u8path(s).wstring(); }
// one argument on a Windows command line (the rules CommandLineToArgvW and the C runtime read it back with)
static std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < a.size() && a[i] == L'\\') { ++slashes; ++i; }
        if (i == a.size()) { q.append(slashes * 2, L'\\'); break; }
        if (a[i] == L'"') q.append(slashes * 2 + 1, L'\\');
        else q.append(slashes, L'\\');
        q += a[i];
    }
    return q + L"\"";
}
#endif

Job::~Job() { stop(); if (impl && impl->reader.joinable()) impl->reader.join(); delete impl; }

bool Job::start(const std::vector<std::string>& argv, const std::string& cwd) {
    if (impl && impl->reader.joinable()) impl->reader.join();
    delete impl;
    impl = new Impl();
    Impl* I = impl;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd;
    for (const auto& a : argv) { if (!cmd.empty()) cmd += L' '; cmd += quoteArg(widen(a)); }
    STARTUPINFOW si{}; si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    // the Changeling's environment: unbuffered, UTF-8 (the same chain.py gives its own steps)
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    std::wstring wcwd = widen(cwd);
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    // in a job: Stop (or LayerTack closing) ends chain.py and the Changeling it started
    I->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(I->job, JobObjectExtendedLimitInformation, &li, sizeof li);
    AssignProcessToJobObject(I->job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    I->proc = pi.hProcess;
    I->state = RUNNING;
    I->reader = std::thread([I, rd] {
        char buf[4096]; DWORD n = 0; std::string line;
        while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) {
            for (DWORD i = 0; i < n; ++i) { if (buf[i] == '\n' || buf[i] == '\r') I->add(line); else line += buf[i]; }
        }
        I->add(line);
        CloseHandle(rd);
        WaitForSingleObject(I->proc, INFINITE);
        DWORD code = 1; GetExitCodeProcess(I->proc, &code);
        I->state = (code == 0 && !I->stopped) ? DONE : FAILED;
    });
#else
    int fd[2];
    if (pipe(fd)) return false;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return false; }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]);
        if (!cwd.empty() && chdir(cwd.c_str())) _exit(127);
        setenv("PYTHONUNBUFFERED", "1", 1); setenv("PYTHONIOENCODING", "utf-8", 1);
        std::vector<char*> av; for (auto& a : argv) av.push_back(const_cast<char*>(a.c_str())); av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }
    close(fd[1]);
    I->pid = pid;
    I->state = RUNNING;
    I->reader = std::thread([I, rfd = fd[0]] {
        char buf[4096]; ssize_t n; std::string line;
        while ((n = read(rfd, buf, sizeof buf)) > 0)
            for (ssize_t i = 0; i < n; ++i) { if (buf[i] == '\n' || buf[i] == '\r') I->add(line); else line += buf[i]; }
        I->add(line);
        close(rfd);
        int st = 0; waitpid(I->pid, &st, 0);
        I->state = (WIFEXITED(st) && WEXITSTATUS(st) == 0 && !I->stopped) ? DONE : FAILED;
    });
#endif
    return true;
}

void Job::stop() {
    if (!impl || impl->state != RUNNING) return;
    impl->stopped = true;
#ifdef _WIN32
    if (impl->job) TerminateJobObject(impl->job, 1);
#else
    if (impl->pid > 0) kill(-impl->pid, SIGKILL);
#endif
}

Job::State Job::state() { return impl ? (State)impl->state.load() : IDLE; }
std::vector<std::string> Job::lines() { if (!impl) return {}; std::lock_guard<std::mutex> g(impl->m); return impl->lines; }
std::string Job::output() { std::string s; for (auto& l : lines()) s += l + "\n"; return s; }

// ---------- the steps ----------
static Job stepsJob;
static std::vector<Step> stepList;
static std::string stepsErr;
static bool loading = false;

void loadSteps() {
    stepList.clear(); stepsErr.clear(); loading = false;
    if (studio.empty()) { stepsErr = "Changeling Studio isn't on this PC (or LayerTack can't find it)"; return; }
    const std::string py = python();
    if (py.empty()) { stepsErr = "No Python to run Changeling Studio with (set up a Changeling first)"; return; }
    if (!stepsJob.start({py, "-u", "chain.py", "--steps", "--json"}, studio)) { stepsErr = "Couldn't start " + py; return; }
    loading = true;
}

bool stepsLoading() {
    if (!loading) return false;
    const Job::State s = stepsJob.state();
    if (s == Job::RUNNING) return true;
    loading = false;
    std::string out;
    for (auto& l : stepsJob.lines()) if (!l.empty() && l[0] == '{') out = l;
    J root; Parser ps{out.data(), out.data() + out.size()};
    if (s != Job::DONE || out.empty() || !ps.val(root)) {
        auto ls = stepsJob.lines();
        stepsErr = "chain.py --steps --json didn't answer" + (ls.empty() ? std::string() : ": " + ls.back());
        return false;
    }
    for (const J& js : root["steps"].a) {
        Step st; st.app = js["app"].s; st.about = js["about"].s; st.missing = js["missing"].s;
        for (const J& jo : js["options"].a) {
            Option o; o.key = jo["key"].s; o.kind = jo["kind"].s; o.label = jo["label"].s;
            for (const J& c : jo["choices"].a) o.choices.push_back(c.s);
            o.min = (float)jo["min"].n; o.max = (float)jo["max"].n;
            const J& d = jo["default"];
            if (o.kind == "number") o.number = (float)d.n;
            else if (o.kind == "check") o.check = d.b;
            else if (o.kind == "choice") { for (size_t i = 0; i < o.choices.size(); ++i) if (o.choices[i] == d.s) o.choice = (int)i; }
            else o.file = d.s;
            st.options.push_back(std::move(o));
        }
        stepList.push_back(std::move(st));
    }
    if (stepList.empty()) stepsErr = "Changeling Studio has no steps";
    return false;
}

std::vector<Step>& steps() { return stepList; }
const std::string& stepsError() { return stepsErr; }

bool run(Job& job, const Step& step, const std::string& inPng, const std::string& outPng) {
    const std::string py = python();
    if (py.empty() || studio.empty()) return false;
    return job.start({py, "-u", "chain.py", inPng, "-o", outPng, "--step", step.stepArg()}, studio);
}

}  // namespace Changeling
