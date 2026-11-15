// MediaSense - native Windows 10/11 media viewer. Common definitions and helpers.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define INITGUID
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#undef GetCurrentTime
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfmediaengine.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

typedef std::wstring WS;

template <class T> struct Com {
    T* p = nullptr;
    Com() {}
    Com(T* raw) : p(raw) { if (p) p->AddRef(); }
    Com(const Com& o) : p(o.p) { if (p) p->AddRef(); }
    Com(Com&& o) : p(o.p) { o.p = nullptr; }
    ~Com() { reset(); }
    Com& operator=(const Com& o) { if (o.p) o.p->AddRef(); reset(); p = o.p; return *this; }
    Com& operator=(Com&& o) { if (p != o.p) { reset(); p = o.p; o.p = nullptr; } return *this; }
    void reset() { if (p) { T* t = p; p = nullptr; t->Release(); } }
    T** operator&() { reset(); return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

struct Lock {
    SRWLOCK l = SRWLOCK_INIT;
    void lock() { AcquireSRWLockExclusive(&l); }
    void unlock() { ReleaseSRWLockExclusive(&l); }
};
struct Guard { Lock& l; Guard(Lock& x) : l(x) { l.lock(); } ~Guard() { l.unlock(); } };

static double nowSec() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static WS lower(WS s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
static WS extOf(const WS& p) {
    size_t d = p.find_last_of(L".\\/");
    if (d == WS::npos || p[d] != L'.') return L"";
    return lower(p.substr(d + 1));
}
static WS dirOf(const WS& p) { size_t s = p.find_last_of(L"\\/"); return s == WS::npos ? L"" : p.substr(0, s); }
static WS nameOf(const WS& p) { size_t s = p.find_last_of(L"\\/"); return s == WS::npos ? p : p.substr(s + 1); }
static WS stemOf(const WS& p) { WS n = nameOf(p); size_t d = n.find_last_of(L'.'); return d == WS::npos ? n : n.substr(0, d); }
static bool fileExists(const WS& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static bool dirExists(const WS& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }

static std::string utf8(const WS& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static WS wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    WS w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
static bool readFile(const WS& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0; BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h); out.resize(rd); return ok != 0;
}
static bool writeFile(const WS& path, const std::string& data) {
    WS tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0; BOOL ok = data.empty() ? TRUE : WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
static WS exeDir() {
    wchar_t b[MAX_PATH * 4]; DWORD n = GetModuleFileNameW(nullptr, b, MAX_PATH * 4);
    return dirOf(WS(b, n));
}
static WS appDataDir() {
    static WS d;
    if (d.empty()) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) { d = WS(p) + L"\\MediaSense"; CoTaskMemFree(p); }
        else d = exeDir();
        CreateDirectoryW(d.c_str(), nullptr);
    }
    return d;
}
static WS fmtW(const wchar_t* f, ...) {
    wchar_t b[1024]; va_list a; va_start(a, f); _vsnwprintf(b, 1023, f, a); va_end(a); b[1023] = 0; return b;
}

// ---------- media kinds ----------
static const wchar_t* kImgExt[] = { L"jpg", L"jpeg", L"jpe", L"jfif", L"png", L"bmp", L"dib", L"gif", L"tif", L"tiff", L"ico", L"webp",
    L"heic", L"heif", L"hif", L"avif", L"jxr", L"wdp", L"hdp", L"dds", L"jxl",
    L"cr2", L"cr3", L"crw", L"nef", L"nrw", L"arw", L"srf", L"sr2", L"dng", L"orf", L"rw2", L"raf", L"pef", L"srw", L"erf", L"kdc", L"mrw", L"3fr", L"x3f",
    L"tga", L"psd", L"exr", L"hdr", L"pcx", L"ppm", L"pgm", L"pbm", L"pnm", L"jp2", L"j2k", L"qoi", L"sgi", L"xbm", nullptr };
static const wchar_t* kVidExt[] = { L"mp4", L"m4v", L"mov", L"mkv", L"avi", L"wmv", L"asf", L"webm", L"3gp", L"3g2", L"mts", L"m2ts", L"ts",
    L"mpg", L"mpeg", L"vob", L"flv", L"ogv", L"divx", L"f4v", L"mxf", L"m2v", nullptr };
static bool inList(const wchar_t** l, const WS& e) { for (; *l; ++l) if (e == *l) return true; return false; }
static bool isImageExt(const WS& e) { return inList(kImgExt, e); }
static bool isVideoExt(const WS& e) { return inList(kVidExt, e); }

// ---------- persistent store: rotations + settings ----------
struct Store {
    std::unordered_map<WS, int> rot;
    std::unordered_map<WS, WS> set;
    void load() {
        std::string s;
        if (readFile(appDataDir() + L"\\rotations.txt", s)) {
            size_t i = 0;
            while (i < s.size()) {
                size_t e = s.find('\n', i); if (e == std::string::npos) e = s.size();
                std::string ln = s.substr(i, e - i); i = e + 1;
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                if (ln.size() > 2 && ln[1] == '|') { int r = ln[0] - '0'; if (r > 0 && r < 4) rot[wide(ln.substr(2))] = r; }
            }
        }
        if (readFile(appDataDir() + L"\\settings.txt", s)) {
            size_t i = 0;
            while (i < s.size()) {
                size_t e = s.find('\n', i); if (e == std::string::npos) e = s.size();
                std::string ln = s.substr(i, e - i); i = e + 1;
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                size_t q = ln.find('='); if (q != std::string::npos) set[wide(ln.substr(0, q))] = wide(ln.substr(q + 1));
            }
        }
    }
    void saveRot() {
        std::string s;
        for (auto& kv : rot) if (kv.second) { s += (char)('0' + kv.second); s += '|'; s += utf8(kv.first); s += "\r\n"; }
        writeFile(appDataDir() + L"\\rotations.txt", s);
    }
    void saveSet() {
        std::string s;
        for (auto& kv : set) { s += utf8(kv.first); s += '='; s += utf8(kv.second); s += "\r\n"; }
        writeFile(appDataDir() + L"\\settings.txt", s);
    }
    int getRot(const WS& path) { auto it = rot.find(lower(path)); return it == rot.end() ? 0 : it->second; }
    void setRot(const WS& path, int r) { WS k = lower(path); if (r) rot[k] = r; else rot.erase(k); saveRot(); }
    WS get(const wchar_t* k, const wchar_t* def = L"") { auto it = set.find(k); return it == set.end() ? def : it->second; }
    int getInt(const wchar_t* k, int def) { auto it = set.find(k); return it == set.end() ? def : _wtoi(it->second.c_str()); }
    void put(const wchar_t* k, const WS& v) { set[k] = v; }
    void putInt(const wchar_t* k, int v) { set[k] = std::to_wstring(v); }
};
static Store g_store;

// ---------- ffmpeg helper (optional external tool, used for conversion and exotic formats) ----------
static WS g_ffmpeg; static bool g_ffmpegChecked = false; static Lock g_ffLock;
static WS findFfmpeg() {
    Guard gd(g_ffLock);
    if (g_ffmpegChecked) return g_ffmpeg;
    g_ffmpegChecked = true;
    WS d = exeDir();
    const WS cands[] = { d + L"\\ffmpeg.exe", d + L"\\tools\\ffmpeg.exe", d + L"\\ffmpeg\\ffmpeg.exe", d + L"\\ffmpeg\\bin\\ffmpeg.exe" };
    for (auto& c : cands) if (fileExists(c)) { g_ffmpeg = c; return g_ffmpeg; }
    wchar_t b[MAX_PATH * 2];
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH * 2, b, nullptr)) g_ffmpeg = b;
    return g_ffmpeg;
}
static WS q(const WS& s) { return L"\"" + s + L"\""; }

static Lock g_spawnLock;
// Starts a hidden child process. mode: 0 = capture stdout, 1 = capture stdout+stderr, 2 = capture stderr only.
static HANDLE spawnProc(const WS& cmd, HANDLE* readEnd, int mode) {
    Guard gd(g_spawnLock);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) return nullptr;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = (mode == 2) ? nul : wr;
    si.hStdError = (mode == 0) ? nul : wr;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); return nullptr; }
    CloseHandle(pi.hThread);
    *readEnd = rd;
    return pi.hProcess;
}
// Runs a process to completion, returns exit code (or -1), collects output.
static int runCapture(const WS& cmd, std::string& out, int mode, size_t limit = (size_t)1 << 30) {
    HANDLE rd = nullptr; HANDLE hp = spawnProc(cmd, &rd, mode);
    if (!hp) return -1;
    char buf[65536]; DWORD n;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) { if (out.size() < limit) out.append(buf, n); }
    CloseHandle(rd);
    WaitForSingleObject(hp, INFINITE);
    DWORD code = 1; GetExitCodeProcess(hp, &code); CloseHandle(hp);
    return (int)code;
}
