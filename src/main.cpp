// MediaSense - fast native media viewer for Windows 10/11.
// Win32 + Direct2D/DirectWrite + WIC + Media Foundation. No network code, no runtime dependencies besides Windows itself.
#include "common.h"
#include "media.inl"
#include "export.inl"

using D2D1::Matrix3x2F;
typedef D2D1_RECT_F RF;
typedef D2D1_COLOR_F CF;

enum {
    B_NONE = 0, B_PREV, B_NEXT, B_ROTL, B_ROTR, B_CROP, B_EXPORT, B_GRID, B_INFO, B_FULL, B_PLAY, B_MUTE, B_SEEK, B_OPEN, B_OPENDIR,
    B_CROP_CANCEL, B_CROP_OK, B_PANEL_CLOSE, B_FOLDER, B_FOLDER_RESET, B_SAVE, B_EXP_CANCEL, B_DEPS_COPY, B_DEPS_README, B_DEPS_CLOSE, B_TOAST,
    B_SHOW, B_COPYPATH, B_DELETE, B_FIT, B_100, B_MIN, B_MAX, B_CLOSE, B_STRIP, B_ENH, B_SPEED, B_LOOP, B_FRAME, B_TRIM_A, B_TRIM_B, B_TRIM_X, B_COPYFILE,
    B_SORT, B_FILTER, B_SELMODE, B_SEL_CONVERT, B_SEL_DELETE, B_SEL_CLEAR, B_MOVE_NEW, B_MOVE_LAST, B_UNDO_MOVE, B_NAME_OK, B_NAME_CANCEL,
    B_RATIO0 = 100, B_PRESET0 = 200, B_SIZE0 = 300, B_VPRESET0 = 400, B_STRIP0 = 10000, B_MOVE0 = 5000000,
};
static const float kPanelW = 340, kStripH = 78, kCapW = 138;
static bool g_enh = true;   // video enhancement on/off
static int g_sort = 0, g_filter = 0;   // gallery order (0 name, 1 newest first) and type filter (0 all, 1 photo, 2 video)
static std::unordered_set<WS> g_sel; static bool g_selMode = false; static int g_selAnchor = -1;   // gallery selection
static double g_trimA = -1, g_trimB = -1; static int g_targetMB = 25;   // export fragment (seconds) and target size
static double g_rate = 1.0; static bool g_loop = false;   // playback speed and repeat
static double g_dragEnd = -10; static bool g_dragOut = false, g_dragArm = false; static int g_galDown = -1; static POINT g_downPt = {};
static bool g_strip = false; static float g_stripA = 0, g_stripPos = 0;   // filmstrip: on/off, show animation, scroll position
static RF g_seekRect = {};
static RF g_cropScreen = {};      // crop rectangle on screen (dips), valid in crop mode
static RF g_dispScreen = {};      // displayed media rectangle on screen
static float g_dispW = 0, g_dispH = 0;   // displayed media size in source pixels (after rotation)
static bool g_haveDisp = false;

static float Wd() { return g.pw * 96.0f / g.dpi; }
static float Hd() { return g.ph * 96.0f / g.dpi; }
static CF C(float r, float gg, float b, float a = 1) { CF c = { r, gg, b, a }; return c; }
static CF Wh(float a) { return C(1, 1, 1, a); }
static RF R(float l, float t, float r, float b) { RF x = { l, t, r, b }; return x; }
static bool inR(const RF& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

static void fillR(const RF& r, CF c) { g.br->SetColor(&c); g.ctx->FillRectangle(&r, g.br); }
static void fillRR(const RF& r, float rad, CF c) { g.br->SetColor(&c); D2D1_ROUNDED_RECT rr = { r, rad, rad }; g.ctx->FillRoundedRectangle(&rr, g.br); }
static void strokeRR(const RF& r, float rad, CF c, float w = 1) { g.br->SetColor(&c); D2D1_ROUNDED_RECT rr = { r, rad, rad }; g.ctx->DrawRoundedRectangle(&rr, g.br, w, nullptr); }
static void txt(const WS& s, const RF& r, IDWriteTextFormat* f, CF c, DWRITE_TEXT_ALIGNMENT al = DWRITE_TEXT_ALIGNMENT_LEADING) {
    if (s.empty() || !f) return;
    f->SetTextAlignment(al); g.br->SetColor(&c);
    g.ctx->DrawText(s.c_str(), (UINT32)s.size(), f, &r, g.br, D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}
static float txtW(const WS& s, IDWriteTextFormat* f) {
    Com<IDWriteTextLayout> l;
    if (FAILED(g.dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 4000, 100, &l))) return 0;
    DWRITE_TEXT_METRICS m = {}; l->GetMetrics(&m); return m.widthIncludingTrailingWhitespace;
}
static void ident() { Matrix3x2F m = Matrix3x2F::Identity(); g.ctx->SetTransform(&m); }
static void addBtn(int id, const RF& r) { Btn b = { id, r }; g.btns.push_back(b); }
static int hitBtn() {
    float x = g.mouse.x * 96.0f / g.dpi, y = g.mouse.y * 96.0f / g.dpi;
    for (int i = (int)g.btns.size() - 1; i >= 0; i--) if (inR(g.btns[i].r, x, y)) return g.btns[i].id;
    return -1;
}
// icon button; returns nothing, registers the hit rect
static void iconBtn(int id, const RF& r, const wchar_t* glyph, float a, bool mirror = false, bool on = false, IDWriteTextFormat* f = nullptr) {
    addBtn(id, r);
    bool hot = g.hot == id, down = g.pressed == id && hot;
    if (hot || on) fillRR(R(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), 9, Wh((down ? 0.2f : on && !hot ? 0.12f : 0.11f) * a));
    if (mirror) { Matrix3x2F m = Matrix3x2F::Scale(-1, 1, D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2)); g.ctx->SetTransform(&m); }
    txt(glyph, r, f ? f : g.fIcon.p, Wh((hot ? 1.0f : 0.86f) * a), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (mirror) ident();
}
static void textBtn(int id, const RF& r, const WS& label, bool primary, float a = 1, bool on = false) {
    addBtn(id, r);
    bool hot = g.hot == id, down = g.pressed == id && hot;
    float rad = (r.bottom - r.top) / 2; if (rad > 10) rad = 10;
    if (primary) { fillRR(r, rad, Wh((down ? 0.8f : hot ? 1.0f : 0.92f) * a)); txt(label, r, g.fBold, C(0.04f, 0.04f, 0.05f, a), DWRITE_TEXT_ALIGNMENT_CENTER); }
    else {
        if (on) fillRR(r, rad, Wh(0.92f * a)); else if (hot) fillRR(r, rad, Wh((down ? 0.2f : 0.11f) * a));
        txt(label, r, on ? g.fBold.p : g.fUI.p, on ? C(0.04f, 0.04f, 0.05f, a) : Wh((hot ? 1.0f : 0.86f) * a), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
static void pill(const RF& r, float a) {
    float rad = (r.bottom - r.top) / 2; if (rad > 16) rad = 16;
    fillRR(r, rad, C(0.105f, 0.105f, 0.115f, 0.86f * a));
    strokeRR(R(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), rad, Wh(0.09f * a));
}
static WS fmtTime(double t) {
    if (!(t >= 0) || t > 1e7) t = 0;
    int s = (int)(t + 0.5); int h = s / 3600, m = (s / 60) % 60; s %= 60;
    return h ? fmtW(L"%d:%02d:%02d", h, m, s) : fmtW(L"%d:%02d", m, s);
}

// ---------------------------------------------------------------- current media and its placement
struct Cur { ID2D1Bitmap1* bmp; float w, h; int rot; bool allowUp; bool video; };
static bool getCur(Cur& c) {
    if (g.cur < 0 || g.cur >= (int)g.items.size()) return false;
    Item& it = g.items[g.cur];
    if (it.video) { if (g.vbmp.p && g.vHasFrame) { c = { g.vbmp.p, (float)g.vw, (float)g.vh, it.rot, true, true }; return true; } }
    else if (g.img.p) { c = { g.img.p, (float)g.imgW, (float)g.imgH, (g.baseRot + it.rot) & 3, false, false }; return true; }
    if ((it.video && !g.vError) || (!it.video && !g.imgFailed)) {
        ThumbEnt* th = getThumb(it.path, true);
        if (th && th->bmp.p) { c = { th->bmp.p, (float)th->w, (float)th->h, it.rot, true, it.video }; return true; }
    }
    return false;
}
static RF mediaArea() {
    RF r = R(0, 0, Wd() - (g.exportOpen ? kPanelW : 0), Hd());
    r.bottom -= kStripH * g_stripA;
    if (g.cropMode) { r.left += 36; r.top += 36; r.right -= 36; r.bottom -= 112; }
    if (r.right - r.left < 50) r.right = r.left + 50;
    if (r.bottom - r.top < 50) r.bottom = r.top + 50;
    return r;
}
struct ML { float cx, cy, s, deg, dw, dh, fit; RF disp; };
static void layoutMedia(const Cur& c, ML& m, bool anim = true) {
    RF a = mediaArea();
    m.dw = (c.rot & 1) ? c.h : c.w; m.dh = (c.rot & 1) ? c.w : c.h;
    float fit = std::min((a.right - a.left) / m.dw, (a.bottom - a.top) / m.dh);
    float one = 96.0f / g.dpi;
    if (!c.allowUp && fit > one) fit = one;
    m.fit = fit; m.s = fit * g.zoom;
    float mx = std::max(0.0f, (m.dw * m.s - (a.right - a.left)) / 2), my = std::max(0.0f, (m.dh * m.s - (a.bottom - a.top)) / 2);
    g.pan.x = std::max(-mx, std::min(mx, g.pan.x)); g.pan.y = std::max(-my, std::min(my, g.pan.y));
    m.cx = (a.left + a.right) / 2 + g.pan.x; m.cy = (a.top + a.bottom) / 2 + g.pan.y;
    m.deg = c.rot * 90.0f;
    m.disp = R(m.cx - m.dw * m.s / 2, m.cy - m.dh * m.s / 2, m.cx + m.dw * m.s / 2, m.cy + m.dh * m.s / 2);
    if (anim) {
        float t = (float)((nowSec() - g.rotT0) / 0.16);
        if (t >= 0 && t < 1) { float e = 1 - (1 - t) * (1 - t) * (1 - t); m.deg += g.rotFromDeg * (1 - e); m.s = g.rotFromScale + (m.s - g.rotFromScale) * e; }
    }
}


// ---------------------------------------------------------------- writing the rotation into the file itself
// Video (MP4/MOV/M4V/3GP): lossless remux with a new display matrix. JPEG: EXIF orientation tag. Other formats keep the in-app rotation only.
enum { WM_APP_BAKE = WM_APP + 20 };
struct Bake { WS path; int rot = 0; bool video = false; int vw = 0, vh = 0; bool engIgnores = false; bool ok = false; int codedLandscape = -1; int turns = 0; };
struct BakeReq { WS path; double due; int vw, vh; };
static std::unordered_set<WS> g_bakeBusy; static int g_bakeActive = 0; static bool g_closing = false, g_noBake = false;
static std::vector<BakeReq> g_bakePending;

static bool bakeable(const WS& path, bool video) {
    WS e = extOf(path);
    if (video) return e == L"mp4" || e == L"m4v" || e == L"mov" || e == L"3gp" || e == L"3g2";
    return e == L"jpg" || e == L"jpeg" || e == L"jpe" || e == L"jfif";
}
static bool replaceFile(const WS& orig, const WS& tmp) {
    for (int i = 0; i < 20; i++) {   // the player may hold the file for a moment after closing
        if (ReplaceFileW(orig.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
        Sleep(200);
    }
    return false;
}
static LONGLONG fileSize(const WS& p) { WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1; return ((LONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow; }

static bool bakeJpeg(const WS& path, int rot) {
    std::string d;
    if (!readFile(path, d) || d.size() < 4 || (BYTE)d[0] != 0xFF || (BYTE)d[1] != 0xD8) return false;
    auto B = [&](size_t i) { return (DWORD)(BYTE)d[i]; };
    static const int val[4] = { 1, 6, 3, 8 };
    size_t n = d.size(), pos = 2, insertAt = 2;
    while (pos + 4 <= n && B(pos) == 0xFF) {
        DWORD m = B(pos + 1); if (m == 0xDA || m == 0xD9) break;
        size_t len = (B(pos + 2) << 8) | B(pos + 3); if (len < 2 || pos + 2 + len > n) return false;
        if (m == 0xE0 && pos == 2) insertAt = pos + 2 + len;
        if (m == 0xE1 && len >= 16 && !memcmp(&d[pos + 4], "Exif\0\0", 6)) {
            size_t t = pos + 10, end = pos + 2 + len; bool le = B(t) == 'I';
            auto u16 = [&](size_t i) { return le ? (B(i) | (B(i + 1) << 8)) : ((B(i) << 8) | B(i + 1)); };
            auto u32 = [&](size_t i) { return le ? (B(i) | (B(i + 1) << 8) | (B(i + 2) << 16) | (B(i + 3) << 24)) : ((B(i) << 24) | (B(i + 1) << 16) | (B(i + 2) << 8) | B(i + 3)); };
            if (t + 8 > end) return false;
            size_t ifd = t + u32(t + 4); if (ifd + 2 > end) return false;
            DWORD cnt = u16(ifd);
            for (DWORD k = 0; k < cnt; k++) {
                size_t e = ifd + 2 + (size_t)k * 12; if (e + 12 > end) return false;
                if (u16(e) != 0x0112) continue;
                if (u16(e + 2) != 3) return false;
                int old = (int)u16(e + 8); if (old == 2 || old == 4 || old == 5 || old == 7) return false;
                int v = val[(exifToRot(old) + rot) & 3];
                BYTE b2[2]; if (le) { b2[0] = (BYTE)v; b2[1] = 0; } else { b2[0] = 0; b2[1] = (BYTE)v; }
                HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h == INVALID_HANDLE_VALUE) return false;
                LARGE_INTEGER off; off.QuadPart = (LONGLONG)(e + 8); DWORD wr = 0;
                bool ok = SetFilePointerEx(h, off, nullptr, FILE_BEGIN) && WriteFile(h, b2, 2, &wr, nullptr) && wr == 2;
                CloseHandle(h); return ok;
            }
            return false;   // EXIF present but without an orientation tag: leave the file alone
        }
        pos += 2 + len;
    }
    // no EXIF block at all: insert a minimal one carrying only the orientation
    const unsigned char seg[36] = { 0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 0x2A, 0, 0, 0, 8, 0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, (unsigned char)val[rot & 3], 0, 0, 0, 0, 0, 0 };
    std::string out = d.substr(0, insertAt) + std::string((const char*)seg, 36) + d.substr(insertAt);
    WS tmp = path + L".msrot";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0; BOOL ok = WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr) && wr == out.size(); CloseHandle(h);
    if (!ok || !replaceFile(path, tmp)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
static bool bakeVideo(Bake* b) {
    WS ff = findFfmpeg(); if (ff.empty()) return false;
    std::string info; runCapture(q(ff) + L" -hide_banner -nostdin -i " + q(b->path), info, 2, 1 << 16);
    double old = 0; size_t p = info.find("rotation of "); if (p != std::string::npos) old = atof(info.c_str() + p + 12);
    size_t v = info.find("Video:");
    if (v != std::string::npos) {   // coded frame size, e.g. ", 1920x1080"
        for (size_t i = v; i + 3 < info.size() && info[i] != '\n'; i++) {
            if (info[i] == 'x' && isdigit((unsigned char)info[i - 1]) && isdigit((unsigned char)info[i + 1])) {
                size_t a = i; while (a > v && isdigit((unsigned char)info[a - 1])) a--;
                int cw = atoi(info.c_str() + a), ch = atoi(info.c_str() + i + 1);
                if (cw >= 16 && ch >= 16 && i - a >= 2) { b->codedLandscape = cw > ch ? 1 : 0; break; }
            }
        }
    }
    int nr = (b->engIgnores ? 0 : (int)lround(old)) - b->rot * 90;
    while (nr > 180) nr -= 360; while (nr <= -180) nr += 360;
    b->turns = ((-nr / 90) % 4 + 4) % 4;
    WS e = extOf(b->path), tmp = b->path.substr(0, b->path.size() - e.size() - 1) + L".msrot." + e;
    LONGLONG osz = fileSize(b->path);
    WS head = q(ff) + fmtW(L" -hide_banner -nostdin -y -v error -display_rotation:v:0 %d -i ", nr) + q(b->path);
    std::string o1;
    int code = runCapture(head + L" -map 0 -c copy -ignore_unknown " + q(tmp), o1, 1, 4096);
    if (code != 0 || fileSize(tmp) < osz * 8 / 10) {
        DeleteFileW(tmp.c_str()); o1.clear();
        code = runCapture(head + L" -map 0:v -map 0:a? -c copy " + q(tmp), o1, 1, 4096);
        if (code != 0 || fileSize(tmp) < osz / 2) { DeleteFileW(tmp.c_str()); return false; }
    }
    if (!replaceFile(b->path, tmp)) { DeleteFileW(tmp.c_str()); return false; }
    return true;
}
static DWORD WINAPI bakeThread(void* p) {
    Bake* b = (Bake*)p;
    b->ok = b->video ? bakeVideo(b) : bakeJpeg(b->path, b->rot);
    if (!PostMessageW(g.hwnd, WM_APP_BAKE, 0, (LPARAM)b)) delete b;
    return 0;
}
static void pumpBakes(bool force) {
    double t = nowSec();
    for (size_t i = 0; i < g_bakePending.size();) {
        BakeReq r = g_bakePending[i];
        bool viewing = g.mode == M_VIEW && g.cur >= 0 && g.cur < (int)g.items.size() && g.items[g.cur].path == r.path && !g_closing;
        if ((!force && r.due > t) || g_bakeBusy.count(r.path) || viewing || g_bakeActive >= 2) { i++; continue; }
        g_bakePending.erase(g_bakePending.begin() + i);
        int rot = g_store.getRot(r.path);
        if (!rot || !fileExists(r.path)) continue;
        Bake* b = new Bake; b->path = r.path; b->rot = rot; b->video = isVideoExt(extOf(r.path)); b->vw = r.vw; b->vh = r.vh;
        b->engIgnores = g_store.getInt(L"eng_ignores_rot", 0) != 0;
        g_bakeBusy.insert(r.path); g_bakeActive++;
        CloseHandle(CreateThread(nullptr, 0, bakeThread, b, 0, nullptr));
    }
}
static void queueBake(int idx, double delay) {
    if (g_noBake || idx < 0 || idx >= (int)g.items.size()) return;
    Item& it = g.items[idx];
    if (!it.rot || !bakeable(it.path, it.video)) return;
    for (auto& r : g_bakePending) if (r.path == it.path) { r.due = nowSec() + delay; return; }
    BakeReq r = { it.path, nowSec() + delay, 0, 0 }; g_bakePending.push_back(r);
}
static void go(int i);
static void onBakeDone(Bake* b) {
    g_bakeBusy.erase(b->path); g_bakeActive--;
    if (b->ok) {
        int cur = g_store.getRot(b->path);
        int nr = b->engIgnores ? cur : ((cur - b->rot) & 3);   // normally the file now carries the rotation, so the in-app one is cleared
        g_store.setRot(b->path, nr);
        if (b->video && !b->engIgnores && (b->turns & 1) && b->codedLandscape >= 0) { g_store.put((L"bk:" + lower(b->path)).c_str(), fmtW(L"%d,%d", b->codedLandscape, b->turns)); g_store.saveSet(); }
        g.thumbs.erase(b->path);
        for (size_t i = 0; i < g.icache.size(); i++) if (g.icache[i].path == b->path) { g.icache.erase(g.icache.begin() + i); break; }
        for (size_t i = 0; i < g.items.size(); i++) if (g.items[i].path == b->path) {
            g.items[i].rot = nr;
            if (g.mode == M_VIEW && (int)i == g.cur && !g_closing) go(g.cur);
            break;
        }
        if (!g_closing) showToast(L"Поворот записан в файл: " + nameOf(b->path));
    } else if (!g_closing && b->video && findFfmpeg().empty()) showToast(L"Поворот запомнен только в программе: нет ffmpeg.exe");
    delete b;
    if (g_closing) { pumpBakes(true); if (g_bakeActive == 0) DestroyWindow(g.hwnd); }
    inval();
}

// ---------------------------------------------------------------- navigation
static void updateTitle() {
    WS t = L"MediaSense";
    if (g.cur >= 0 && g.cur < (int)g.items.size() && g.mode == M_VIEW) t = g.items[g.cur].name + L" - MediaSense";
    else if (!g.folder.empty()) t = nameOf(g.folder) + L" - MediaSense";
    SetWindowTextW(g.hwnd, t.c_str());
}
static void setImage(ImgEnt* e) {
    g.img.reset(); g.imgLoading = false;
    g.animSrc.clear(); g.animBmps.clear(); g.animDelays.clear(); g.animTotal = 0;
    if (!e->bmp.p) { g.imgFailed = true; return; }
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (SUCCEEDED(g.ctx->CreateBitmapFromWicBitmap(e->bmp, &bp, &g.img))) {
        g.imgW = e->w; g.imgH = e->h; g.baseRot = e->exifRot; g.imgFailed = false;
        if (e->frames.size() > 1 && e->frames.size() == e->delays.size()) {   // animated GIF: frames are uploaded to the GPU as they are first shown
            g.animSrc = e->frames; g.animDelays = e->delays; g.animBmps.assign(e->frames.size(), Com<ID2D1Bitmap1>()); g.animBmps[0] = g.img;
            for (int d : g.animDelays) g.animTotal += d;
            g.animT0 = nowSec();
        }
    }
    else g.imgFailed = true;
}
static void go(int i) {
    if (g.items.empty()) { g.cur = -1; return; }
    i = std::max(0, std::min((int)g.items.size() - 1, i));
    if (g.mode == M_VIEW && g.cur != i) queueBake(g.cur, 0);
    g.cur = i; g.mode = M_VIEW;
    Item& it = g.items[i];
    closeVideo(); g.img.reset(); g.imgFailed = false; g.imgLoading = false; g.baseRot = 0;
    g.animSrc.clear(); g.animBmps.clear(); g.animDelays.clear(); g.animTotal = 0; g_trimA = g_trimB = -1; g_dragArm = false;
    g.zoom = 1; g.pan = D2D1::Point2F(0, 0); g.cropMode = false; g.crop = R(0, 0, 1, 1); g.cropRatio = 0; g.rotT0 = -10; g.panning = false;
    clearImageQueue();
    if (it.video) openVideo(it.path);
    else {
        ImgEnt* e = cachedImage(it.path);
        if (e) setImage(e); else { g.imgLoading = true; requestImage(it.path, true); }
    }
    for (int d = 1; d >= -1; d -= 2) { int n = i + d; if (n >= 0 && n < (int)g.items.size() && !g.items[n].video) requestImage(g.items[n].path, false); }
    pumpBakes(false);
    updateTitle(); inval();
}
static void scanFolder(const WS& dir) {
    g.items.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WS name = fd.cFileName, e = extOf(name);
        bool v = isVideoExt(e);
        if (!v && !isImageExt(e)) continue;
        if ((g_filter == 1 && v) || (g_filter == 2 && !v)) continue;
        Item it; it.name = name; it.path = dir + L"\\" + name; it.video = v; it.rot = g_store.getRot(it.path); it.mtime = fd.ftLastWriteTime;
        g.items.push_back(std::move(it));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (g_sort == 1) std::sort(g.items.begin(), g.items.end(), [](const Item& a, const Item& b) { int c = CompareFileTime(&a.mtime, &b.mtime); return c ? c > 0 : StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });
    else std::sort(g.items.begin(), g.items.end(), [](const Item& a, const Item& b) { return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0; });
}
static void openPath(const WS& raw) {
    wchar_t full[4096]; DWORD n = GetFullPathNameW(raw.c_str(), 4096, full, nullptr);
    WS path = (n && n < 4096) ? WS(full, n) : raw;
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    if (g.mode == M_VIEW) queueBake(g.cur, 0);
    closeVideo(); g.img.reset(); g.thumbPending.clear(); g_sel.clear(); g_selMode = false; g_selAnchor = -1;
    if (dirExists(path)) {
        g.folder = path; scanFolder(path);
        if (g.items.empty() && g_filter) { g_filter = 0; g_store.putInt(L"filter", 0); scanFolder(path); }
        g.cur = g.items.empty() ? -1 : 0; g.mode = M_GALLERY; g.scroll = g.scrollTarget = 0;
        if (g.items.empty()) showToast(L"В этой папке нет фото и видео");
        updateTitle(); inval(); return;
    }
    if (!fileExists(path)) { showToast(L"Файл не найден"); return; }
    g.folder = dirOf(path); scanFolder(g.folder);
    int idx = -1;
    for (size_t i = 0; i < g.items.size(); i++) if (!lstrcmpiW(g.items[i].path.c_str(), path.c_str())) { idx = (int)i; break; }
    if (idx < 0) { Item it; it.mtime = FILETIME(); it.path = path; it.name = nameOf(path); it.video = isVideoExt(extOf(path)); it.rot = g_store.getRot(path); g.items.push_back(it); idx = (int)g.items.size() - 1; }
    go(idx);
}
static void batchRotate(int d);
static void rotateCur(int d) {
    if (g.cur < 0) return;
    if (g.mode == M_GALLERY && !g_sel.empty()) { batchRotate(d); return; }
    Item& it = g.items[g.cur];
    Cur c; ML m; bool had = g.mode == M_VIEW && getCur(c);
    if (had) layoutMedia(c, m, false);
    it.rot = (it.rot + d + 4) & 3;
    g_store.setRot(it.path, it.rot);
    g.zoom = 1; g.pan = D2D1::Point2F(0, 0); g.crop = R(0, 0, 1, 1); g.cropRatio = 0;
    if (had) { g.rotFromDeg = -90.0f * d; g.rotFromScale = m.s; g.rotT0 = nowSec(); }
    if (g.mode == M_GALLERY) queueBake(g.cur, 1.2);   // in the viewer the file is written when you leave it or close the app
    inval();
}
static void zoomAt(float factor, float px, float py) {
    Cur c; if (!getCur(c)) return;
    ML m; layoutMedia(c, m, false);
    RF a = mediaArea();
    float maxZ = std::max(1.0f, 16.0f * (96.0f / g.dpi) / m.fit);
    float nz = std::max(1.0f, std::min(maxZ, g.zoom * factor));
    float ox = px - (a.left + a.right) / 2, oy = py - (a.top + a.bottom) / 2;
    float k = nz / g.zoom;
    g.pan.x = ox - (ox - g.pan.x) * k; g.pan.y = oy - (oy - g.pan.y) * k;
    g.zoom = nz; if (nz <= 1.0001f) g.pan = D2D1::Point2F(0, 0);
    inval();
}
static void toggleFullscreen() {
    DWORD st = GetWindowLongW(g.hwnd, GWL_STYLE);
    if (!g.fullscreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(g.hwnd, &g.wpPrev) && GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            SetWindowLongW(g.hwnd, GWL_STYLE, st & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(g.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g.fullscreen = true;
        }
    } else {
        SetWindowLongW(g.hwnd, GWL_STYLE, st | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g.hwnd, &g.wpPrev);
        SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g.fullscreen = false;
    }
    inval();
}
static void setVolume(double v) {
    g.volume = std::max(0.0, std::min(1.0, v)); g.muted = false;
    if (g.eng.p) { g.eng->SetVolume(g.volume); g.eng->SetMuted(FALSE); }
    g_store.putInt(L"volume", (int)(g.volume * 100 + 0.5)); g_store.putInt(L"muted", 0);
    showToast(fmtW(L"Громкость %d%%", (int)(g.volume * 100 + 0.5)));
}
static void copyText(const WS& s) {
    if (!OpenClipboard(g.hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard();
}
static void showInExplorer(const WS& path) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (pidl) { SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); ILFree(pidl); }
}
static WS pickPath(bool folder) {
    WS res; Com<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return res;
    DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    if (SUCCEEDED(d->Show(g.hwnd))) {
        Com<IShellItem> it;
        if (SUCCEEDED(d->GetResult(&it))) { PWSTR p = nullptr; if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { res = p; CoTaskMemFree(p); } }
    }
    return res;
}
static void deleteCurrent() {
    if (g.cur < 0 || g.mode != M_VIEW) return;
    WS path = g.items[g.cur].path;
    closeVideo(); g.img.reset();
    WS from = path; from.push_back(0); from.push_back(0);
    SHFILEOPSTRUCTW op = {}; op.hwnd = g.hwnd; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_ALLOWUNDO;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted && !fileExists(path)) {
        g.thumbs.erase(path);
        for (size_t i = 0; i < g.icache.size(); i++) if (g.icache[i].path == path) { g.icache.erase(g.icache.begin() + i); break; }
        g.items.erase(g.items.begin() + g.cur);
        if (g.items.empty()) { g.cur = -1; updateTitle(); inval(); return; }
    }
    go(g.cur);
}

// ---------------------------------------------------------------- dependency check
struct Dep { WS name; int state; WS hint; };   // state: 1 ok, 2 via ffmpeg, 0 missing
static std::vector<Dep> g_deps;
static bool mfHasDecoder(const GUID& sub) {
    MFT_REGISTER_TYPE_INFO in = { MFMediaType_Video, sub };
    IMFActivate** acts = nullptr; UINT32 n = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &in, nullptr, &acts, &n);
    if (FAILED(hr)) return false;
    for (UINT32 i = 0; i < n; i++) acts[i]->Release();
    CoTaskMemFree(acts);
    return n > 0;
}
static GUID fccGuid(DWORD f) { GUID x = { f, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } }; return x; }
static void checkDeps() {
    g_deps.clear();
    std::unordered_set<WS> exts;
    Com<IWICImagingFactory> f; Com<IEnumUnknown> en;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && SUCCEEDED(f->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault, &en))) {
        IUnknown* u = nullptr; ULONG got = 0;
        while (en->Next(1, &u, &got) == S_OK && got) {
            Com<IWICBitmapDecoderInfo> di;
            if (SUCCEEDED(u->QueryInterface(IID_PPV_ARGS(&di)))) {
                UINT len = 0; di->GetFileExtensions(0, nullptr, &len);
                if (len) { std::vector<wchar_t> b(len + 1); di->GetFileExtensions(len, b.data(), &len); WS s = lower(b.data()); size_t i = 0;
                    while (i < s.size()) { size_t e = s.find(L',', i); if (e == WS::npos) e = s.size(); exts.insert(s.substr(i, e - i)); i = e + 1; } }
            }
            u->Release();
        }
    }
    bool ff = !findFfmpeg().empty();
    auto img = [&](const wchar_t* name, const wchar_t* ext, const wchar_t* hint) {
        Dep d; d.name = name; d.state = exts.count(ext) ? 1 : (ff ? 2 : 0); d.hint = hint; g_deps.push_back(d);
    };
    img(L"HEIC / HEIF (фото с iPhone)", L".heic", L"Microsoft Store: HEIF Image Extensions (бесплатно) + HEVC Video Extensions");
    img(L"WebP", L".webp", L"Microsoft Store: Webp Image Extensions (бесплатно)");
    img(L"AVIF", L".avif", L"Microsoft Store: AV1 Video Extension (бесплатно)");
    img(L"RAW с камер (CR2, NEF, ARW, DNG)", L".cr2", L"Microsoft Store: Raw Image Extension (бесплатно)");
    if (!g.mfStarted && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) g.mfStarted = true;
    auto vid = [&](const wchar_t* name, const GUID& sub, const wchar_t* hint) {
        Dep d; d.name = name; d.state = mfHasDecoder(sub) ? 1 : 0; d.hint = hint; g_deps.push_back(d);
    };
    static const GUID mpeg2 = { 0xe06d8026, 0xdb46, 0x11cf, { 0xb4, 0xd1, 0x00, 0x80, 0x5f, 0x6c, 0xbb, 0xea } };
    vid(L"Видео HEVC / H.265", fccGuid(0x43564548), L"Microsoft Store: HEVC Video Extensions");
    vid(L"Видео AV1", fccGuid(0x31305641), L"Microsoft Store: AV1 Video Extension (бесплатно)");
    vid(L"Видео VP9 (WebM)", fccGuid(0x30395056), L"Microsoft Store: VP9 Video Extensions (бесплатно)");
    vid(L"Видео MPEG-2 (VOB, MPG)", mpeg2, L"Microsoft Store: MPEG-2 Video Extension (бесплатно)");
    Dep d; d.name = L"FFmpeg: конвертация и редкие форматы"; d.state = ff ? 1 : 0;
    d.hint = L"Положите ffmpeg.exe в папку tools рядом с MediaSense.exe"; g_deps.push_back(d);
}
static WS agentInstructions() {
    WS s = L"Задача для ИИ-агента: довести Windows до полной поддержки форматов в MediaSense.\r\n"
           L"Программа сама в сеть не выходит, установку выполняет пользователь или агент.\r\n\r\nТекущее состояние:\r\n";
    for (auto& d : g_deps) s += L"- " + d.name + L": " + (d.state == 1 ? L"OK" : d.state == 2 ? L"встроенного кодека нет, работает через ffmpeg (медленнее)" : L"НЕ УСТАНОВЛЕНО") + L"\r\n";
    s += L"\r\nКоманды (PowerShell, нужен winget и вход в Microsoft Store):\r\n"
         L"winget install --id 9PMMSR1CGPWG --source msstore --accept-package-agreements --accept-source-agreements   # HEIF Image Extensions\r\n"
         L"winget install --id 9N4WGH0Z6VHQ --source msstore --accept-package-agreements --accept-source-agreements   # HEVC Video Extensions from Device Manufacturer\r\n"
         L"winget install --id 9PG2DK419DRG --source msstore --accept-package-agreements --accept-source-agreements   # Webp Image Extensions\r\n"
         L"winget install --id 9MVZQVXJBQ9V --source msstore --accept-package-agreements --accept-source-agreements   # AV1 Video Extension\r\n"
         L"winget install --id 9N4D0MSMP0PT --source msstore --accept-package-agreements --accept-source-agreements   # VP9 Video Extensions\r\n"
         L"winget install --id 9NCTDW2W1BH8 --source msstore --accept-package-agreements --accept-source-agreements   # Raw Image Extension\r\n"
         L"winget install --id 9N95Q1ZZPMH4 --source msstore --accept-package-agreements --accept-source-agreements   # MPEG-2 Video Extension\r\n"
         L"Если бесплатный пакет HEVC недоступен, поставить платный: winget install --id 9NMZLZ57R3T7 --source msstore\r\n\r\n"
         L"FFmpeg: скачать сборку win64 (gyan.dev или github.com/BtbN/FFmpeg-Builds), положить ffmpeg.exe в папку tools рядом с MediaSense.exe:\r\n  "
         + exeDir() + L"\\tools\\ffmpeg.exe\r\n"
         L"После установки перезапустить MediaSense и открыть окно «Компоненты» (клавиша I): все строки должны быть со статусом OK.\r\n";
    return s;
}

// ---------------------------------------------------------------- export glue
static const Preset& curPreset() { bool v = g.cur >= 0 && g.items[g.cur].video; return v ? kVidPresets[g.presetVid] : kImgPresets[g.presetImg]; }
static bool cropActive() { return g.cropMode && (g.crop.left > 0.001f || g.crop.top > 0.001f || g.crop.right < 0.999f || g.crop.bottom < 0.999f); }
static void openExport() {
    if (g.cur < 0) return;
    if (g.mode != M_VIEW) go(g.cur);
    g.exportOpen = true; g.zoom = 1; g.pan = D2D1::Point2F(0, 0);
    if (g.items[g.cur].video) startHwProbe();
    inval();
}
static void startExport() {
    if (g.exporting || g.cur < 0) return;
    Item& it = g.items[g.cur]; const Preset& p = curPreset();
    if (p.needFF && findFfmpeg().empty()) { showToast(L"Нужен ffmpeg.exe в папке tools рядом с программой"); return; }
    WS dir = (!g.outDir.empty() && dirExists(g.outDir)) ? g.outDir : dirOf(it.path);
    const wchar_t* ext = L"jpg";
    switch (p.id) { case I_PNG: ext = L"png"; break; case I_WEBP: ext = L"webp"; break; case I_TIFF: ext = L"tif"; break;
        case V_PHONE: case V_1080: case V_ORIG: case V_HEVC: case V_SIZE: ext = L"mp4"; break; case V_GIF: ext = L"gif"; break; case V_MP3: ext = L"mp3"; break; default: break; }
    WS e = ext; if (p.id == V_COPY) { e = extOf(it.path); if (e.empty()) e = L"mp4"; }
    Job* j = new Job; j->src = it.path; j->dst = uniquePath(dir, stemOf(it.path), e); j->preset = p.id; j->video = it.video; j->rot = it.rot;
    if (!it.video) { ImgEnt* c = cachedImage(it.path); if (c && c->bmp.p) { j->bmp = c->bmp; j->exifRot = c->exifRot; } }
    if (cropActive()) { j->crop = true; j->cx = g.crop.left; j->cy = g.crop.top; j->cw = g.crop.right - g.crop.left; j->ch = g.crop.bottom - g.crop.top; }
    j->dur = g.vDur;
    if (it.video) {
        double a = g_trimA, b = g_trimB; if (a >= 0 && b >= 0 && b < a) std::swap(a, b);
        if (a > 0.05) j->t0 = a;
        if (b > 0 && b < g.vDur - 0.05) j->t1 = b;
        j->dur = (j->t1 > 0 ? j->t1 : g.vDur) - (j->t0 > 0 ? j->t0 : 0); j->targetMB = g_targetMB;
    }
    if (it.video && g.eng.p && g.vReady && !g.eng->IsPaused()) g.eng->Pause();
    InterlockedExchange(&g_jobCancel, 0); InterlockedExchange(&g_batchN, 0); g.exporting = true; g.progress = 0;
    CloseHandle(CreateThread(nullptr, 0, jobThread, j, 0, nullptr));
    g_store.putInt(L"preset_img", g.presetImg); g_store.putInt(L"preset_vid", g.presetVid); g_store.saveSet();
    inval();
}
static void setRatio(int r) {
    g.cropRatio = r;
    static const float ars[] = { 0, 1.0f, 4.0f / 3, 3.0f / 2, 16.0f / 9, 9.0f / 16 };
    if (r <= 0 || !g_haveDisp || g_dispH <= 0) { if (r == 0) g.crop = R(0, 0, 1, 1); return; }
    float A = g_dispW / g_dispH, ar = ars[r], cw, ch;
    if (ar > A) { cw = 1; ch = A / ar; } else { ch = 1; cw = ar / A; }
    g.crop = R((1 - cw) / 2, (1 - ch) / 2, (1 + cw) / 2, (1 + ch) / 2);
}

#include "extras.inl"

// ---------------------------------------------------------------- actions
static void act(int id) {
    int n = (int)g.items.size();
    if (id == B_EXPORT && g.mode == M_GALLERY) {   // in the gallery E opens the batch panel for the selection (or for the highlighted file)
        if (g.cur < 0) return;
        if (g_sel.empty()) { g_sel.insert(g.items[g.cur].path); g_selAnchor = g.cur; }
        g_selMode = true; if (!g.exporting) g.exportOpen = !g.exportOpen;
        startHwProbe(); inval(); return;
    }
    switch (id) {
    case B_PREV: if (g.cur > 0) go(g.cur - 1); break;
    case B_NEXT: if (g.cur >= 0 && g.cur < n - 1) go(g.cur + 1); break;
    case B_ROTL: rotateCur(-1); break;
    case B_ROTR: rotateCur(1); break;
    case B_CROP: if (g.cur >= 0 && g.mode == M_VIEW) { g.cropMode = !g.cropMode; g.crop = R(0, 0, 1, 1); g.cropRatio = 0; g.zoom = 1; g.pan = D2D1::Point2F(0, 0); if (!g.cropMode && !g.exporting) g.exportOpen = false; } break;
    case B_CROP_CANCEL: g.cropMode = false; g.crop = R(0, 0, 1, 1); break;
    case B_CROP_OK: case B_EXPORT: if (g.exportOpen && id == B_EXPORT) { if (!g.exporting) g.exportOpen = false; } else openExport(); break;
    case B_GRID:
        if (n) {
            if (g.mode == M_VIEW) { queueBake(g.cur, 0); closeVideo(); g.img.reset(); g.mode = M_GALLERY; g.cropMode = false; if (!g.exporting) g.exportOpen = false; g.scroll = g.scrollTarget = -1; updateTitle(); pumpBakes(false); }
            else go(g.cur);
        } break;
    case B_INFO: g.depsOpen = !g.depsOpen; if (g.depsOpen) checkDeps(); break;
    case B_FULL: toggleFullscreen(); break;
    case B_PLAY: videoToggle(); break;
    case B_MUTE: g.muted = !g.muted; if (g.eng.p) g.eng->SetMuted(g.muted); g_store.putInt(L"muted", g.muted); break;
    case B_OPEN: { WS p = pickPath(false); if (!p.empty()) openPath(p); } break;
    case B_OPENDIR: { WS p = pickPath(true); if (!p.empty()) openPath(p); } break;
    case B_PANEL_CLOSE: if (!g.exporting) g.exportOpen = false; break;
    case B_FOLDER: { WS p = pickPath(true); if (!p.empty()) { g.outDir = p; g_store.put(L"outdir", p); g_store.saveSet(); } } break;
    case B_FOLDER_RESET: g.outDir.clear(); g_store.put(L"outdir", L""); g_store.saveSet(); break;
    case B_SAVE: startExport(); break;
    case B_EXP_CANCEL: cancelExport(); break;
    case B_DEPS_COPY: copyText(agentInstructions()); showToast(L"Инструкция для ИИ-агента скопирована"); break;
    case B_DEPS_README: ShellExecuteW(g.hwnd, L"open", (exeDir() + L"\\README.txt").c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case B_DEPS_CLOSE: g.depsOpen = false; break;
    case B_TOAST: if (!g.toastPath.empty()) showInExplorer(g.toastPath); g.toastT = -100; break;
    case B_SHOW: if (g.cur >= 0) showInExplorer(g.items[g.cur].path); break;
    case B_COPYPATH: if (g.cur >= 0) { copyText(g.items[g.cur].path); showToast(L"Путь скопирован"); } break;
    case B_DELETE: deleteCurrent(); break;
    case B_MIN: ShowWindow(g.hwnd, SW_MINIMIZE); break;
    case B_MAX: if (g.fullscreen) toggleFullscreen(); else ShowWindow(g.hwnd, IsZoomed(g.hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case B_CLOSE: PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
    case B_STRIP: g_strip = !g_strip; g_stripPos = (float)g.cur; g_store.putInt(L"strip", g_strip); g_store.saveSet(); break;
    case B_ENH: g_enh = !g_enh; g_store.putInt(L"enhance", g_enh); g_store.saveSet(); showToast(g_enh ? L"Улучшение видео включено" : L"Улучшение видео выключено"); break;
    case B_SPEED: {
        static const double rs[] = { 1.0, 1.25, 1.5, 2.0, 0.5 }; int k = 0; for (int i = 0; i < 5; i++) if (fabs(rs[i] - g_rate) < 0.01) k = i;
        g_rate = (GetKeyState(VK_SHIFT) < 0) ? 1.0 : rs[(k + 1) % 5];
        if (g.eng.p) { g.eng->SetDefaultPlaybackRate(g_rate); g.eng->SetPlaybackRate(g_rate); }
        showToast(fmtW(L"Скорость %gx", g_rate)); break; }
    case B_LOOP: g_loop = !g_loop; if (g.eng.p) g.eng->SetLoop(g_loop); showToast(g_loop ? L"Повтор включён" : L"Повтор выключен"); break;
    case B_FRAME: saveFrame(); break;
    case B_TRIM_A: if (g.eng.p && g.vReady) { g_trimA = videoTime(); if (g_trimB >= 0 && g_trimB <= g_trimA) g_trimB = -1; showToast(L"Начало фрагмента: " + fmtTime(g_trimA)); } break;
    case B_TRIM_B: if (g.eng.p && g.vReady) { g_trimB = videoTime(); if (g_trimA >= g_trimB) g_trimA = -1; showToast(L"Конец фрагмента: " + fmtTime(g_trimB)); } break;
    case B_TRIM_X: g_trimA = g_trimB = -1; break;
    case B_COPYFILE: copyFiles(targetPaths()); break;
    case B_SORT: g_sort = !g_sort; g_store.putInt(L"sort", g_sort); g_store.saveSet(); rescanKeep(); g.scrollTarget = -1; break;
    case B_FILTER:
        for (int k = 0; k < 3; k++) {   // skip filters that would leave the folder empty
            g_filter = (g_filter + 1) % 3; rescanKeep();
            if (!g.items.empty() || g_filter == 0) break;
        }
        g_store.putInt(L"filter", g_filter); g_store.saveSet(); g.scrollTarget = -1; break;
    case B_SELMODE: if (g_selMode) clearSel(); else g_selMode = true; break;
    case B_SEL_CLEAR: clearSel(); break;
    case B_SEL_DELETE: if (g.mode == M_GALLERY && !g_sel.empty()) batchDelete(); break;
    case B_SEL_CONVERT: startBatch(); break;
    case B_MOVE_NEW: g_namePaths = targetPaths(); if (!g_namePaths.empty()) { g_nameText.clear(); g_nameOpen = true; } break;
    case B_MOVE_LAST: if (!g_lastMoveDir.empty() && dirExists(g_lastMoveDir)) moveTo(targetPaths(), g_lastMoveDir); else showToast(L"Сначала выберите папку: правая кнопка, \u00ABОтправить в папку\u00BB"); break;
    case B_UNDO_MOVE: undoMove(); break;
    case B_NAME_OK: nameConfirm(); break;
    case B_NAME_CANCEL: g_nameOpen = false; break;
    case B_FIT: g.zoom = 1; g.pan = D2D1::Point2F(0, 0); break;
    case B_100: { Cur c; if (getCur(c)) { ML m; layoutMedia(c, m, false); g.zoom = std::max(1.0f, (96.0f / g.dpi) / m.fit); g.pan = D2D1::Point2F(0, 0); } } break;
    default:
        if (id >= B_MOVE0) { if (id - B_MOVE0 < (int)g_moveDirs.size()) moveTo(targetPaths(), g.folder + L"\\" + g_moveDirs[id - B_MOVE0]); }
        else if (id >= B_STRIP0) go(id - B_STRIP0);
        else if (id >= B_RATIO0 && id < B_RATIO0 + 6) setRatio(id - B_RATIO0);
        else if (id >= B_VPRESET0 && id < B_VPRESET0 + kVidPresetN && !g.exporting) g.presetVid = id - B_VPRESET0;
        else if (id >= B_SIZE0 && id < B_SIZE0 + 4) { static const int mbs[4] = { 10, 25, 50, 100 }; g_targetMB = mbs[id - B_SIZE0]; g_store.putInt(L"target_mb", g_targetMB); g_store.saveSet(); }
        else if (id >= B_PRESET0 && id < B_PRESET0 + 16 && !g.exporting && g.cur >= 0) { if (g.mode != M_GALLERY && g.items[g.cur].video) g.presetVid = id - B_PRESET0; else g.presetImg = id - B_PRESET0; }
    }
    inval();
}
static const wchar_t* tipFor(int id) {
    switch (id) {
    case B_PREV: return L"Назад  ←"; case B_NEXT: return L"Вперёд  →"; case B_ROTL: return L"Повернуть влево  L"; case B_ROTR: return L"Повернуть вправо  R";
    case B_CROP: return L"Обрезать  C"; case B_EXPORT: return L"Конвертировать и сохранить  E"; case B_GRID: return L"Галерея  G"; case B_INFO: return L"Компоненты  I";
    case B_FULL: return L"Во весь экран  F11"; case B_STRIP: return L"Плёнка  F"; case B_OPEN: return L"Открыть файл  Ctrl+O"; case B_OPENDIR: return L"Открыть папку";
    }
    return nullptr;
}

// ---------------------------------------------------------------- drawing
static void drawMessage(const wchar_t* glyph, const WS& l1, const WS& l2) {
    RF a = mediaArea(); float cx = (a.left + a.right) / 2, cy = (a.top + a.bottom) / 2;
    txt(glyph, R(cx - 60, cy - 90, cx + 60, cy - 20), g.fIconBig, Wh(0.35f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(l1, R(a.left + 20, cy - 8, a.right - 20, cy + 22), g.fBold, Wh(0.9f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(l2, R(a.left + 20, cy + 22, a.right - 20, cy + 46), g.fUI, Wh(0.5f), DWRITE_TEXT_ALIGNMENT_CENTER);
}
static void drawCropOverlay(const ML& m) {
    RF d = m.disp; float w = d.right - d.left, h = d.bottom - d.top;
    RF c = R(d.left + g.crop.left * w, d.top + g.crop.top * h, d.left + g.crop.right * w, d.top + g.crop.bottom * h);
    g_cropScreen = c;
    CF dim = C(0, 0, 0, 0.62f);
    fillR(R(d.left, d.top, d.right, c.top), dim); fillR(R(d.left, c.bottom, d.right, d.bottom), dim);
    fillR(R(d.left, c.top, c.left, c.bottom), dim); fillR(R(c.right, c.top, d.right, c.bottom), dim);
    g.br->SetColor(Wh(0.28f));
    for (int i = 1; i < 3; i++) {
        float x = c.left + (c.right - c.left) * i / 3, y = c.top + (c.bottom - c.top) * i / 3;
        g.ctx->DrawLine(D2D1::Point2F(x, c.top), D2D1::Point2F(x, c.bottom), g.br, 1, nullptr);
        g.ctx->DrawLine(D2D1::Point2F(c.left, y), D2D1::Point2F(c.right, y), g.br, 1, nullptr);
    }
    g.br->SetColor(Wh(0.95f)); g.ctx->DrawRectangle(&c, g.br, 1.2f, nullptr);
    float L = std::min(22.0f, std::min(c.right - c.left, c.bottom - c.top) / 3), T = 3.5f;
    CF wc = Wh(1);
    fillR(R(c.left - T, c.top - T, c.left + L, c.top), wc); fillR(R(c.left - T, c.top - T, c.left, c.top + L), wc);
    fillR(R(c.right - L, c.top - T, c.right + T, c.top), wc); fillR(R(c.right, c.top - T, c.right + T, c.top + L), wc);
    fillR(R(c.left - T, c.bottom, c.left + L, c.bottom + T), wc); fillR(R(c.left - T, c.bottom - L, c.left, c.bottom + T), wc);
    fillR(R(c.right - L, c.bottom, c.right + T, c.bottom + T), wc); fillR(R(c.right, c.bottom - L, c.right + T, c.bottom + T), wc);
    WS dims = fmtW(L"%d × %d", (int)((g.crop.right - g.crop.left) * m.dw + 0.5f), (int)((g.crop.bottom - g.crop.top) * m.dh + 0.5f));
    float tw = txtW(dims, g.fSmall) + 16;
    RF chip = R((c.left + c.right - tw) / 2, c.top + 8, (c.left + c.right + tw) / 2, c.top + 30);
    if (c.bottom - c.top > 60 && c.right - c.left > tw + 20) { fillRR(chip, 11, C(0, 0, 0, 0.55f)); txt(dims, chip, g.fSmall, Wh(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER); }
}
static void drawTip(const RF& anchor, const wchar_t* tip) {
    if (!tip) return;
    float w = txtW(tip, g.fSmall) + 20, cx = (anchor.left + anchor.right) / 2;
    float l = std::max(8.0f, std::min(Wd() - w - 8, cx - w / 2));
    RF r = R(l, anchor.top - 36, l + w, anchor.top - 10);
    if (r.top < 4) { r.top = anchor.bottom + 8; r.bottom = r.top + 26; }
    fillRR(r, 8, C(0.16f, 0.16f, 0.18f, 0.98f)); strokeRR(r, 8, Wh(0.1f));
    txt(tip, r, g.fSmall, Wh(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
}
static void drawStrip(const RF& va) {
    float a = g_stripA; if (a < 0.01f) return;
    int n = (int)g.items.size(); float top = Hd() - kStripH * a;
    RF bar = R(va.left, top, va.right, top + kStripH);
    fillR(bar, C(0.03f, 0.03f, 0.035f, 0.94f)); addBtn(B_NONE, bar);
    g.ctx->PushAxisAlignedClip(&bar, D2D1_ANTIALIAS_MODE_ALIASED);
    const float cs = 58, step = 62; float cx = (va.left + va.right) / 2, cy = top + kStripH / 2;
    int half = (int)((va.right - va.left) / 2 / step) + 2, c = (int)lroundf(g_stripPos);
    for (int i = std::max(0, c - half); i <= std::min(n - 1, c + half); i++) {
        Item& it = g.items[i]; bool isCur = i == g.cur; float s = isCur ? cs + 8 : cs, x = cx + (i - g_stripPos) * step;
        RF cell = R(x - s / 2, cy - s / 2, x + s / 2, cy + s / 2);
        fillR(cell, C(0.1f, 0.1f, 0.11f));
        ThumbEnt* th = getThumb(it.path, true);
        if (th && th->bmp.p) {
            g.ctx->PushAxisAlignedClip(&cell, D2D1_ANTIALIAS_MODE_ALIASED);
            float tw = (float)th->w, thh = (float)th->h, dw = (it.rot & 1) ? thh : tw, dh = (it.rot & 1) ? tw : thh, k = std::max(s / dw, s / dh);
            Matrix3x2F tr = Matrix3x2F::Translation(-tw / 2, -thh / 2) * Matrix3x2F::Rotation(it.rot * 90.0f) * Matrix3x2F::Scale(k, k) * Matrix3x2F::Translation(x, cy);
            g.ctx->SetTransform(&tr); RF src = R(0, 0, tw, thh);
            g.ctx->DrawBitmap(th->bmp, &src, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
            ident(); g.ctx->PopAxisAlignedClip();
        }
        if (it.video) txt(L"", R(cell.left + 2, cell.bottom - 18, cell.left + 18, cell.bottom - 2), g.fIconS, Wh(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (isCur) { g.br->SetColor(Wh(0.97f)); RF rr = R(cell.left + 1, cell.top + 1, cell.right - 1, cell.bottom - 1); g.ctx->DrawRectangle(&rr, g.br, 2, nullptr); }
        else if (g.hot != B_STRIP0 + i) fillR(cell, C(0, 0, 0, 0.38f));
        addBtn(B_STRIP0 + i, cell);
    }
    g.ctx->PopAxisAlignedClip();
}
// own window buttons: the system title bar is removed, these fade in with the rest of the interface
static void drawCaption(float a) {
    if (a < 0.01f) return;
    float w = 46, h = 32, x = Wd() - 3 * w;
    struct CB { int id; const wchar_t* gl; };
    const CB cb[3] = { { B_MIN, L"" }, { B_MAX, (g.fullscreen || IsZoomed(g.hwnd)) ? L"" : L"" }, { B_CLOSE, L"" } };
    for (int i = 0; i < 3; i++) {
        RF r = R(x + i * w, 0, x + (i + 1) * w, h); addBtn(cb[i].id, r);
        bool hot = g.hot == cb[i].id;
        if (hot) fillR(r, cb[i].id == B_CLOSE ? C(0.77f, 0.17f, 0.11f, a) : Wh(0.12f * a));
        txt(cb[i].gl, r, g.fCap, Wh((hot ? 1.0f : 0.8f) * a), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
// ---------------------------------------------------------------- video enhancement (GPU, per frame)
// High-quality cubic scaling to the exact on-screen size + unsharp mask. Low-resolution or strongly upscaled video
// is first smoothed slightly at source resolution so compression blocks do not get sharpened along with the detail.
static Com<ID2D1Effect> g_fxPre, g_fxScale, g_fxBlur, g_fxComp;
template <class T> static void fxSet(ID2D1Effect* e, UINT32 idx, const T& v) { e->SetValue(idx, D2D1_PROPERTY_TYPE_UNKNOWN, (const BYTE*)&v, sizeof(T)); }
static bool ensureFx() {
    if (g_fxComp.p) return true;
    if (FAILED(g.ctx->CreateEffect(CLSID_D2D1GaussianBlur, &g_fxPre)) || FAILED(g.ctx->CreateEffect(CLSID_D2D1Scale, &g_fxScale)) ||
        FAILED(g.ctx->CreateEffect(CLSID_D2D1GaussianBlur, &g_fxBlur)) || FAILED(g.ctx->CreateEffect(CLSID_D2D1ArithmeticComposite, &g_fxComp))) {
        g_fxPre.reset(); g_fxScale.reset(); g_fxBlur.reset(); g_fxComp.reset(); return false;
    }
    fxSet(g_fxPre.p, D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, (UINT32)1); fxSet(g_fxBlur.p, D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, (UINT32)1);   // hard borders: no dark edges
    fxSet(g_fxScale.p, 2, (UINT32)5);   // D2D1_SCALE_PROP_INTERPOLATION_MODE = HIGH_QUALITY_CUBIC
    fxSet(g_fxScale.p, 3, (UINT32)1);   // D2D1_SCALE_PROP_BORDER_MODE = HARD
    fxSet(g_fxComp.p, D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, (BOOL)TRUE);
    return true;
}
static void drawEnhanced(const Cur& c, const ML& m) {
    float up = m.s * g.dpi / 96.0f;                              // screen pixels per source pixel
    bool low = std::min(c.w, c.h) <= 600 || up > 1.6f;           // SD material or heavy upscale
    float px = 96.0f / g.dpi;                                    // one screen pixel in dips
    if (low) { fxSet(g_fxPre.p, D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 0.75f); g_fxPre->SetInput(0, c.bmp); g_fxScale->SetInputEffect(0, g_fxPre); }
    else g_fxScale->SetInput(0, c.bmp);
    D2D1_VECTOR_2F sc = { m.s, m.s }; fxSet(g_fxScale.p, 0, sc);
    fxSet(g_fxBlur.p, D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, (low ? 1.7f : 1.1f) * px);
    g_fxBlur->SetInputEffect(0, g_fxScale);
    float k = low ? 0.95f : (up < 0.9f ? 0.35f : 0.6f);          // sharpening amount: result = (1+k)*image - k*blurred
    D2D1_VECTOR_4F co = { 0, 1 + k, -k, 0 }; fxSet(g_fxComp.p, D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, co);
    g_fxComp->SetInputEffect(0, g_fxScale); g_fxComp->SetInputEffect(1, g_fxBlur);
    Matrix3x2F tr = Matrix3x2F::Translation(-c.w * m.s / 2, -c.h * m.s / 2) * Matrix3x2F::Rotation(m.deg) * Matrix3x2F::Translation(m.cx, m.cy);
    g.ctx->SetTransform(&tr);
    g.ctx->DrawImage(g_fxComp.p, nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    ident();
}
static void drawViewer() {
    Item& it = g.items[g.cur];
    Cur c; ML m = {}; bool have = getCur(c);
    g_haveDisp = false;
    if (have) {
        layoutMedia(c, m);
        g_haveDisp = true; g_dispScreen = m.disp; g_dispW = m.dw; g_dispH = m.dh;
        if (g_vsrReady && c.video && c.bmp == g.vbmp.p && g_vsrBmp.p) {
            float k = m.s * g.vw / (float)g_vsrW;
            Matrix3x2F tr = Matrix3x2F::Translation(-g_vsrW / 2.0f, -g_vsrH / 2.0f) * Matrix3x2F::Rotation(m.deg) * Matrix3x2F::Scale(k, k) * Matrix3x2F::Translation(m.cx, m.cy);
            g.ctx->SetTransform(&tr); RF src = R(0, 0, (float)g_vsrW, (float)g_vsrH);
            g.ctx->DrawBitmap(g_vsrBmp, &src, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr); ident();
        }
        else if (g_enh && c.video && c.bmp == g.vbmp.p && ensureFx()) drawEnhanced(c, m);
        else {
            Matrix3x2F tr = Matrix3x2F::Translation(-c.w / 2, -c.h / 2) * Matrix3x2F::Rotation(m.deg) * Matrix3x2F::Scale(m.s, m.s) * Matrix3x2F::Translation(m.cx, m.cy);
            g.ctx->SetTransform(&tr);
            RF src = R(0, 0, c.w, c.h);
            bool down = m.s * g.dpi / 96.0f < 0.98f && !c.video;
            g.ctx->DrawBitmap(c.bmp, &src, 1.0f, down ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
            ident();
        }
        if (g.cropMode) drawCropOverlay(m);
    }
    if (it.video && g.vError) drawMessage(L"", L"Windows не может воспроизвести это видео", L"Нажмите E, чтобы конвертировать в MP4, или I, чтобы проверить кодеки");
    else if (!it.video && g.imgFailed) drawMessage(L"", L"Не удалось открыть изображение", L"Нажмите I, чтобы проверить установленные кодеки");

    float a = g.uiAlpha;
    RF va = R(0, 0, Wd() - (g.exportOpen ? kPanelW : 0), Hd());
    if (a < 0.01f) { drawStrip(va); return; }
    float vw = va.right - va.left;
    // file name and counter
    WS label = it.name + fmtW(L"    %d / %d", g.cur + 1, (int)g.items.size());
    RF lr = R(18, 10, vw - 18, 36);
    txt(label, R(lr.left + 1, lr.top + 1, lr.right + 1, lr.bottom + 1), g.fUI, C(0, 0, 0, 0.7f * a));
    txt(label, lr, g.fUI, Wh(0.8f * a));

    drawStrip(va);
    float bottom = Hd() - 16 - kStripH * g_stripA;
    const wchar_t* tip = nullptr; RF tipR = {};
    if (g.cropMode) {
        static const wchar_t* names[] = { L"Свободно", L"1:1", L"4:3", L"3:2", L"16:9", L"9:16" };
        float ws[6], total = 12;
        for (int i = 0; i < 6; i++) { ws[i] = txtW(names[i], g.fUI) + 24; total += ws[i] + 2; }
        float wCancel = 86, wOk = 124; total += 14 + wCancel + 6 + wOk;
        float x = va.left + (vw - total) / 2; if (x < 8) x = 8;
        RF p = R(x, bottom - 48, x + total, bottom); pill(p, 1); addBtn(B_NONE, p);
        x += 6;
        for (int i = 0; i < 6; i++) { textBtn(B_RATIO0 + i, R(x, p.top + 7, x + ws[i], p.bottom - 7), names[i], false, 1, g.cropRatio == i); x += ws[i] + 2; }
        x += 6; fillR(R(x, p.top + 14, x + 1, p.bottom - 14), Wh(0.14f)); x += 7;
        textBtn(B_CROP_CANCEL, R(x, p.top + 7, x + wCancel, p.bottom - 7), L"Отмена", false); x += wCancel + 6;
        textBtn(B_CROP_OK, R(x, p.top + 7, x + wOk, p.bottom - 7), L"Сохранить…", true);
        return;
    }
    // main pill
    struct PB { int id; const wchar_t* gl; bool mirror; bool sepAfter; };
    const PB pbs[] = { { B_PREV, L"", false, false }, { B_NEXT, L"", false, true }, { B_ROTL, L"", true, false }, { B_ROTR, L"", false, true },
        { B_CROP, L"", false, false }, { B_EXPORT, L"", false, true }, { B_GRID, L"", false, false }, { B_STRIP, L"", false, false }, { B_INFO, L"", false, false },
        { B_FULL, g.fullscreen ? L"" : L"", false, false } };
    const int nb = 10; float bw = 44, sep = 13;
    float total = nb * bw + 3 * sep + 12;
    if (total > vw - 16) { bw = std::max(26.0f, (vw - 16 - 3 * sep - 12) / nb); total = nb * bw + 3 * sep + 12; }
    float x = va.left + (vw - total) / 2;
    RF p = R(x, bottom - 48, x + total, bottom); pill(p, a); addBtn(B_NONE, p);
    x += 6;
    for (int i = 0; i < nb; i++) {
        RF br = R(x, p.top + 2, x + bw, p.bottom - 2);
        iconBtn(pbs[i].id, br, pbs[i].gl, a, pbs[i].mirror, (pbs[i].id == B_EXPORT && g.exportOpen) || (pbs[i].id == B_STRIP && g_strip));
        if (pbs[i].id == B_STRIP) {   // filmstrip glyph drawn by hand: a frame between two smaller ones
            float mx = (br.left + br.right) / 2, my = (br.top + br.bottom) / 2; CF ic = Wh((g.hot == B_STRIP ? 1.0f : 0.86f) * a);
            fillRR(R(mx - 3.5f, my - 5.5f, mx + 3.5f, my + 5.5f), 1.5f, ic);
            fillRR(R(mx - 9.5f, my - 4, mx - 5.5f, my + 4), 1.2f, C(1, 1, 1, ic.a * 0.6f)); fillRR(R(mx + 5.5f, my - 4, mx + 9.5f, my + 4), 1.2f, C(1, 1, 1, ic.a * 0.6f));
        }
        if (g.hot == pbs[i].id) { tip = tipFor(pbs[i].id); tipR = br; }
        x += bw;
        if (pbs[i].sepAfter) { fillR(R(x + 6, p.top + 15, x + 7, p.bottom - 15), Wh(0.14f * a)); x += sep; }
    }
    // video transport
    if (it.video && g.vReady) {
        float bwid = std::min(vw - 32, 700.0f); float bx = va.left + (vw - bwid) / 2;
        RF vb = R(bx, p.top - 8 - 44, bx + bwid, p.top - 8); pill(vb, a); addBtn(B_NONE, vb);
        bool playing = videoPlaying();
        iconBtn(B_PLAY, R(vb.left + 4, vb.top + 2, vb.left + 46, vb.bottom - 2), playing ? L"" : L"", a);
        iconBtn(B_MUTE, R(vb.right - 46, vb.top + 2, vb.right - 4, vb.bottom - 2), g.muted || g.volume <= 0 ? L"" : L"", a);
        double pos = g.seeking ? g.seekPos : videoTime();
        txt(fmtTime(pos), R(vb.left + 48, vb.top, vb.left + 104, vb.bottom), g.fSmall, Wh(0.85f * a), DWRITE_TEXT_ALIGNMENT_CENTER);
        RF eb = R(vb.right - 92, vb.top + 2, vb.right - 48, vb.bottom - 2);
        addBtn(B_ENH, eb);
        if (g.hot == B_ENH) { fillRR(R(eb.left + 2, eb.top + 2, eb.right - 2, eb.bottom - 2), 9, Wh(0.11f * a)); tip = L"Улучшение видео  H"; tipR = eb; }
        RF chip = R(eb.left + 7, eb.top + 11, eb.right - 7, eb.bottom - 11);
        if (g_enh) { fillRR(chip, 4, Wh(0.92f * a)); txt(L"HD+", chip, g.fSmall, C(0.04f, 0.04f, 0.05f, a), DWRITE_TEXT_ALIGNMENT_CENTER); }
        else { strokeRR(chip, 4, Wh(0.4f * a)); txt(L"HD+", chip, g.fSmall, Wh(0.5f * a), DWRITE_TEXT_ALIGNMENT_CENTER); }
        RF sb = R(vb.right - 136, vb.top + 2, vb.right - 94, vb.bottom - 2);
        addBtn(B_SPEED, sb);
        if (g.hot == B_SPEED) { fillRR(R(sb.left + 2, sb.top + 2, sb.right - 2, sb.bottom - 2), 9, Wh(0.11f * a)); tip = L"Скорость  S"; tipR = sb; }
        txt(fmtW(L"%gx", g_rate), sb, g.fSmall, Wh((fabs(g_rate - 1.0) > 0.01 ? 1.0f : 0.7f) * a), DWRITE_TEXT_ALIGNMENT_CENTER);
        txt(fmtTime(g.vDur), R(vb.right - 192, vb.top, vb.right - 138, vb.bottom), g.fSmall, Wh(0.6f * a), DWRITE_TEXT_ALIGNMENT_CENTER);
        RF tr = R(vb.left + 110, vb.top, vb.right - 198, vb.bottom);
        if (tr.right - tr.left > 30) {
            g_seekRect = tr; addBtn(B_SEEK, tr);
            float cy = (tr.top + tr.bottom) / 2; float k = g.vDur > 0 ? (float)std::max(0.0, std::min(1.0, pos / g.vDur)) : 0;
            bool hot = g.hot == B_SEEK || g.seeking; float th = hot ? 2.5f : 1.5f;
            fillRR(R(tr.left, cy - th, tr.right, cy + th), th, Wh(0.22f * a));
            fillRR(R(tr.left, cy - th, tr.left + (tr.right - tr.left) * k, cy + th), th, Wh(0.95f * a));
            if (g.vDur > 0 && (g_trimA >= 0 || g_trimB >= 0)) {   // export fragment markers
                float tw = tr.right - tr.left, xa = tr.left + tw * (float)(std::max(0.0, g_trimA) / g.vDur), xb = tr.left + tw * (float)((g_trimB >= 0 ? g_trimB : g.vDur) / g.vDur);
                CF mc = C(1.0f, 0.78f, 0.25f, a);
                fillR(R(xa, cy + 5, xb, cy + 7), mc); fillR(R(xa - 1, cy - 7, xa + 1, cy + 7), mc); fillR(R(xb - 1, cy - 7, xb + 1, cy + 7), mc);
            }
            if (hot) { float kx = tr.left + (tr.right - tr.left) * k; D2D1_ELLIPSE el = { { kx, cy }, 6.5f, 6.5f }; g.br->SetColor(Wh(a)); g.ctx->FillEllipse(&el, g.br); }
        }
    }
    drawTip(tipR, tip);
}
static void drawEmpty() {
    float cx = Wd() / 2, cy = Hd() / 2;
    txt(L"", R(cx - 60, cy - 110, cx + 60, cy - 40), g.fIconBig, Wh(0.3f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(L"Перетащите сюда фото, видео или папку", R(20, cy - 28, Wd() - 20, cy + 4), g.fTitle, Wh(0.92f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(L"Стрелки листают, L и R поворачивают, C обрезает, E конвертирует, G открывает галерею", R(20, cy + 8, Wd() - 20, cy + 32), g.fUI, Wh(0.45f), DWRITE_TEXT_ALIGNMENT_CENTER);
    textBtn(B_OPEN, R(cx - 166, cy + 52, cx - 6, cy + 90), L"Открыть файл", true);
    addBtn(B_OPENDIR, R(cx + 6, cy + 52, cx + 166, cy + 90));
    strokeRR(R(cx + 6.5f, cy + 52.5f, cx + 165.5f, cy + 89.5f), 10, Wh(0.2f));
    textBtn(B_OPENDIR, R(cx + 6, cy + 52, cx + 166, cy + 90), L"Открыть папку", false);
    iconBtn(B_INFO, R(Wd() - 52, 40, Wd() - 8, 84), L"", 0.8f);
}
static const float kGalTop = 56, kGalPad = 16, kGalGap = 4;
static void galleryMetrics(int& cols, float& cs) {
    float w = Wd() - 2 * kGalPad;
    cols = std::max(2, (int)((w + kGalGap) / (186 + kGalGap)));
    cs = (w - (cols - 1) * kGalGap) / cols;
}
static float galleryMaxScroll() {
    int cols; float cs; galleryMetrics(cols, cs);
    int rows = ((int)g.items.size() + cols - 1) / cols;
    return std::max(0.0f, rows * (cs + kGalGap) + kGalPad - (Hd() - kGalTop));
}
static int galleryHit(float x, float y) {
    if (y < kGalTop) return -1;
    int cols; float cs; galleryMetrics(cols, cs);
    float gx = x - kGalPad, gy = y - kGalTop + g.scroll;
    if (gx < 0 || gy < 0) return -1;
    int c = (int)(gx / (cs + kGalGap)), r = (int)(gy / (cs + kGalGap));
    if (c >= cols || gx - c * (cs + kGalGap) > cs || gy - r * (cs + kGalGap) > cs) return -1;
    int i = r * cols + c;
    return i < (int)g.items.size() ? i : -1;
}
static void galleryReveal() {
    int cols; float cs; galleryMetrics(cols, cs);
    if (g.cur < 0) return;
    float y = (g.cur / cols) * (cs + kGalGap), vh = Hd() - kGalTop;
    if (y < g.scrollTarget) g.scrollTarget = y;
    else if (y + cs > g.scrollTarget + vh) g.scrollTarget = y + cs + kGalPad - vh;
    g.scrollTarget = std::max(0.0f, std::min(galleryMaxScroll(), g.scrollTarget));
}
static void drawGallery() {
    int cols; float cs; galleryMetrics(cols, cs);
    if (g.scrollTarget < 0) {   // first frame after entering: center current item
        float y = (g.cur / cols) * (cs + kGalGap);
        g.scroll = g.scrollTarget = std::max(0.0f, std::min(galleryMaxScroll(), y - (Hd() - kGalTop - cs) / 2));
    }
    g.scrollTarget = std::max(0.0f, std::min(galleryMaxScroll(), g.scrollTarget));
    g.scroll = std::max(0.0f, std::min(galleryMaxScroll(), g.scroll));
    int n = (int)g.items.size();
    float step = cs + kGalGap, vh = Hd() - kGalTop;
    int r0 = std::max(0, (int)(g.scroll / step)), r1 = (int)((g.scroll + vh) / step);
    int iFirst = r0 * cols, iLast = std::min(n - 1, (r1 + 1) * cols - 1), iAhead = std::min(n - 1, (r1 + 4) * cols - 1);
    for (int i = iAhead; i >= iFirst; i--) getThumb(g.items[i].path, true);   // queue is LIFO: top-left gets decoded first
    float mx = g.mouse.x * 96.0f / g.dpi, my = g.mouse.y * 96.0f / g.dpi;
    int hover = (g.depsOpen || my < kGalTop) ? -1 : galleryHit(mx, my);
    RF clip = R(0, kGalTop, Wd(), Hd());
    g.ctx->PushAxisAlignedClip(&clip, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int i = iFirst; i <= iLast; i++) {
        Item& it = g.items[i];
        float x = kGalPad + (i % cols) * step, y = kGalTop + (i / cols) * step - g.scroll;
        RF cell = R(x, y, x + cs, y + cs);
        fillR(cell, C(0.085f, 0.085f, 0.095f));
        ThumbEnt* th = getThumb(it.path, false);
        if (th && th->bmp.p) {
            g.ctx->PushAxisAlignedClip(&cell, D2D1_ANTIALIAS_MODE_ALIASED);
            float tw = (float)th->w, thh = (float)th->h, dw = (it.rot & 1) ? thh : tw, dh = (it.rot & 1) ? tw : thh;
            float s = std::max(cs / dw, cs / dh);
            Matrix3x2F tr = Matrix3x2F::Translation(-tw / 2, -thh / 2) * Matrix3x2F::Rotation(it.rot * 90.0f) * Matrix3x2F::Scale(s, s) * Matrix3x2F::Translation(x + cs / 2, y + cs / 2);
            g.ctx->SetTransform(&tr);
            RF src = R(0, 0, tw, thh);
            g.ctx->DrawBitmap(th->bmp, &src, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
            ident();
            g.ctx->PopAxisAlignedClip();
        } else if (th && th->failed) txt(it.video ? L"" : L"", cell, g.fIcon, Wh(0.25f), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (it.video) {
            D2D1_ELLIPSE el = { { x + 20, y + cs - 20 }, 12, 12 }; g.br->SetColor(C(0, 0, 0, 0.55f)); g.ctx->FillEllipse(&el, g.br);
            txt(L"", R(x + 8, y + cs - 32, x + 33, y + cs - 8), g.fIconS, Wh(0.95f), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        if (i == hover) fillR(cell, Wh(0.08f));
        if (i == g.cur) { g.br->SetColor(Wh(0.95f)); RF rr = R(x + 1, y + 1, x + cs - 1, y + cs - 1); g.ctx->DrawRectangle(&rr, g.br, 2, nullptr); }
        if (g_selMode) {
            bool sel = g_sel.count(it.path) > 0; D2D1_ELLIPSE el = { { x + cs - 18, y + 18 }, 10, 10 };
            if (sel) {
                g.br->SetColor(Wh(1)); RF rr = R(x + 1.5f, y + 1.5f, x + cs - 1.5f, y + cs - 1.5f); g.ctx->DrawRectangle(&rr, g.br, 3, nullptr);
                g.ctx->FillEllipse(&el, g.br); txt(L"\uE73E", R(x + cs - 28, y + 8, x + cs - 8, y + 28), g.fCap, C(0.04f, 0.04f, 0.05f), DWRITE_TEXT_ALIGNMENT_CENTER);
            } else { fillR(cell, C(0, 0, 0, 0.3f)); g.br->SetColor(C(0, 0, 0, 0.35f)); g.ctx->FillEllipse(&el, g.br); g.br->SetColor(Wh(0.85f)); g.ctx->DrawEllipse(&el, g.br, 1.5f, nullptr); }
        }
    }
    g.ctx->PopAxisAlignedClip();
    // scrollbar
    float maxS = galleryMaxScroll();
    if (maxS > 0) {
        float th = std::max(30.0f, vh * vh / (vh + maxS)), ty = kGalTop + (vh - th) * (g.scroll / maxS);
        fillRR(R(Wd() - 7, ty + 2, Wd() - 3, ty + th - 2), 2, Wh(0.22f));
    }
    // header
    txt(nameOf(g.folder), R(kGalPad + 2, 8, Wd() - 520 - kCapW, 48), g.fTitle, Wh(0.95f));
    float nx = kGalPad + 2 + std::max(0.0f, std::min(txtW(nameOf(g.folder), g.fTitle), Wd() - 520 - kCapW - kGalPad)) + 14;
    txt(fmtW(L"%d", n), R(nx, 12, nx + 120, 48), g.fUI, Wh(0.4f));
    float x = Wd() - 12 - 44 - kCapW;
    const wchar_t* tip = nullptr; RF tipR = {};
    struct HB { int id; const wchar_t* gl; };
    const HB hb[] = { { B_FULL, g.fullscreen ? L"" : L"" }, { B_INFO, L"" }, { B_OPENDIR, L"" }, { B_ROTR, L"" }, { B_ROTL, L"" } };
    for (int i = 0; i < 5; i++) {
        RF br = R(x, 6, x + 44, 50);
        iconBtn(hb[i].id, br, hb[i].gl, 1, hb[i].id == B_ROTL);
        if (g.hot == hb[i].id) { tip = tipFor(hb[i].id); tipR = br; }
        x -= 44;
    }
    {   // order, type filter, selection mode
        static const wchar_t* fl[3] = { L"Все", L"Фото", L"Видео" };
        struct CH { int id; const wchar_t* label; bool on; };
        const CH ch[3] = { { B_SELMODE, L"Выбрать", g_selMode }, { B_FILTER, fl[g_filter], g_filter != 0 }, { B_SORT, g_sort ? L"По дате" : L"По имени", false } };
        float cr = x + 44 - 8;
        for (int i = 0; i < 3; i++) { float w = txtW(ch[i].label, g.fUI) + 24; textBtn(ch[i].id, R(cr - w, 13, cr, 43), ch[i].label, false, 1, ch[i].on); cr -= w + 4; }
    }
    if (g_selMode) {   // actions for the selection
        WS cnt = fmtW(L"Выбрано: %d", (int)g_sel.size());
        float wl = txtW(cnt, g.fUI) + 28, total = 12 + wl + 44 * 2 + 8 + 158 + 8 + 44 * 2;
        float bx = std::max(8.0f, (Wd() - (g.exportOpen ? kPanelW : 0) - total) / 2);
        RF p = R(bx, Hd() - 16 - 48, bx + total, Hd() - 16); pill(p, 1); addBtn(B_NONE, p);
        float px = p.left + 6; bool any = !g_sel.empty();
        txt(cnt, R(px, p.top, px + wl, p.bottom), g.fUI, Wh(0.9f), DWRITE_TEXT_ALIGNMENT_CENTER); px += wl;
        iconBtn(B_ROTL, R(px, p.top + 2, px + 44, p.bottom - 2), L"\uE7AD", any ? 1 : 0.35f, true); px += 44;
        iconBtn(B_ROTR, R(px, p.top + 2, px + 44, p.bottom - 2), L"\uE7AD", any ? 1 : 0.35f); px += 52;
        textBtn(B_EXPORT, R(px, p.top + 7, px + 158, p.bottom - 7), L"Конвертировать\u2026", true, any ? 1 : 0.4f); px += 166;
        iconBtn(B_SEL_DELETE, R(px, p.top + 2, px + 44, p.bottom - 2), L"\uE74D", any ? 1 : 0.35f); px += 44;
        iconBtn(B_SEL_CLEAR, R(px, p.top + 2, px + 44, p.bottom - 2), L"\uE711", 1, false, false, g.fIconS);
    }
    drawTip(tipR, tip);
}
static void drawBatchPanel() {   // export settings for the gallery selection
    float x0 = Wd() - kPanelW, H = Hd();
    RF p = R(x0, 0, Wd(), H);
    fillR(p, C(0.075f, 0.075f, 0.085f, 0.985f)); fillR(R(x0, 0, x0 + 1, H), Wh(0.08f)); addBtn(B_NONE, p);
    float l = x0 + 20, r = Wd() - 20, y = 38;
    txt(L"Экспорт выбранного", R(l, y, r - 40, y + 36), g.fTitle, Wh(0.96f));
    iconBtn(B_PANEL_CLOSE, R(Wd() - 52, 34, Wd() - 10, 76), L"\uE711", g.exporting ? 0.3f : 1, false, false, g.fIconS);
    y += 40;
    int ni = 0, nv = 0; for (auto& it : g.items) if (g_sel.count(it.path)) { if (it.video) nv++; else ni++; }
    txt(fmtW(L"Фото: %d    Видео: %d", ni, nv), R(l, y, r, y + 20), g.fUI, Wh(0.6f)); y += 30;
    bool ff = !findFfmpeg().empty();
    float rowH = std::max(24.0f, std::min(32.0f, (H - y - 260) / (float)((ni ? kImgPresetN : 0) + (nv ? kVidPresetN : 0) + 1)));
    auto list = [&](const wchar_t* cap, const Preset* ps, int np, int sel, int base) {
        txt(cap, R(l, y, r, y + 18), g.fSmall, Wh(0.38f)); y += 20;
        for (int i = 0; i < np; i++) {
            RF rr = R(x0 + 12, y, Wd() - 12, y + rowH - 2); bool dis = ps[i].needFF && !ff;
            addBtn(base + i, rr);
            if (i == sel) { fillRR(rr, 7, Wh(0.1f)); strokeRR(R(rr.left + 0.5f, rr.top + 0.5f, rr.right - 0.5f, rr.bottom - 0.5f), 7, Wh(0.38f)); }
            else if (g.hot == base + i && !g.exporting) fillRR(rr, 7, Wh(0.06f));
            txt(ps[i].title, R(rr.left + 12, rr.top, rr.right - 10, rr.bottom), i == sel ? g.fBold.p : g.fUI.p, Wh(dis ? 0.35f : 0.92f));
            y += rowH;
        }
        y += 8;
    };
    if (ni) list(L"ФОТО", kImgPresets, kImgPresetN, std::min(g.presetImg, kImgPresetN - 1), B_PRESET0);
    if (nv) {
        list(L"ВИДЕО", kVidPresets, kVidPresetN, std::min(g.presetVid, kVidPresetN - 1), B_VPRESET0);
        if (kVidPresets[std::min(g.presetVid, kVidPresetN - 1)].id == V_SIZE) {
            static const int mbs[4] = { 10, 25, 50, 100 };
            for (int i = 0; i < 4; i++) textBtn(B_SIZE0 + i, R(x0 + 12 + i * 79, y - 4, x0 + 12 + i * 79 + 75, y + 24), fmtW(L"%d МБ", mbs[i]), false, 1, g_targetMB == mbs[i]);
            y += 32;
        }
    }
    txt(L"ПАПКА", R(l, y, r, y + 18), g.fSmall, Wh(0.38f)); y += 22;
    bool custom = !g.outDir.empty() && dirExists(g.outDir);
    RF fr = R(x0 + 12, y, Wd() - 12 - (custom ? 40 : 0), y + 40);
    addBtn(B_FOLDER, fr); fillRR(fr, 9, Wh(g.hot == B_FOLDER ? 0.1f : 0.055f));
    txt(L"\uE8B7", R(fr.left + 6, fr.top, fr.left + 38, fr.bottom), g.fIcon, Wh(0.7f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(custom ? g.outDir : WS(L"Рядом с исходными файлами"), R(fr.left + 40, fr.top, fr.right - 10, fr.bottom), g.fUI, Wh(0.85f));
    if (custom) iconBtn(B_FOLDER_RESET, R(Wd() - 50, y, Wd() - 12, y + 40), L"\uE711", 0.7f, false, false, g.fIconS);
    float by = H - 20 - 44;
    if (g.exporting) {
        RF bar = R(x0 + 20, by - 26, Wd() - 20, by - 22);
        fillRR(bar, 2, Wh(0.14f)); fillRR(R(bar.left, bar.top, bar.left + (bar.right - bar.left) * g.progress, bar.bottom), 2, Wh(0.95f));
        txt(g_batchN > 0 ? fmtW(L"Файл %d из %d", (int)g_batchIdx + 1, (int)g_batchN) : fmtW(L"Сохраняю\u2026 %d%%", (int)(g.progress * 100)), R(l, by - 54, r, by - 30), g.fSmall, Wh(0.6f));
        RF cb = R(x0 + 12, by, Wd() - 12, by + 44);
        strokeRR(R(cb.left + 0.5f, cb.top + 0.5f, cb.right - 0.5f, cb.bottom - 0.5f), 10, Wh(0.2f));
        textBtn(B_EXP_CANCEL, cb, L"Отмена", false);
    } else textBtn(B_SEL_CONVERT, R(x0 + 12, by, Wd() - 12, by + 44), fmtW(L"Конвертировать: %d", ni + nv), true, (ni + nv) ? 1 : 0.4f);
}
static void drawExportPanel() {
    Item& it = g.items[g.cur];
    float x0 = Wd() - kPanelW, H = Hd();
    RF p = R(x0, 0, Wd(), H);
    fillR(p, C(0.075f, 0.075f, 0.085f, 0.985f)); fillR(R(x0, 0, x0 + 1, H), Wh(0.08f)); addBtn(B_NONE, p);
    float l = x0 + 20, r = Wd() - 20, y = 38;
    txt(L"Экспорт", R(l, y, r - 40, y + 36), g.fTitle, Wh(0.96f));
    iconBtn(B_PANEL_CLOSE, R(Wd() - 52, 34, Wd() - 10, 76), L"", g.exporting ? 0.3f : 1, false, false, g.fIconS);
    y += 40;
    txt(it.name, R(l, y, r, y + 20), g.fUI, Wh(0.6f)); y += 20;
    WS tr;
    if (it.rot) tr = fmtW(L"Поворот %d°", it.rot * 90);
    if (cropActive() && g_haveDisp) { if (!tr.empty()) tr += L"  ·  "; tr += fmtW(L"Кроп %d × %d", (int)((g.crop.right - g.crop.left) * g_dispW + 0.5f), (int)((g.crop.bottom - g.crop.top) * g_dispH + 0.5f)); }
    if (tr.empty()) tr = L"Кадр без изменений";
    txt(tr, R(l, y, r, y + 20), g.fSmall, Wh(0.42f)); y += 32;
    txt(L"ФОРМАТ", R(l, y, r, y + 18), g.fSmall, Wh(0.38f)); y += 22;
    const Preset* ps = it.video ? kVidPresets : kImgPresets; int np = it.video ? kVidPresetN : kImgPresetN; int sel = it.video ? g.presetVid : g.presetImg;
    if (sel >= np) sel = 0;
    bool ff = !findFfmpeg().empty();
    float rowH = std::max(30.0f, std::min(54.0f, (H - y - (it.video ? 310 : 210)) / np));
    for (int i = 0; i < np; i++) {
        RF rr = R(x0 + 12, y, Wd() - 12, y + rowH - 4);
        bool dis = ps[i].needFF && !ff; bool hot = g.hot == B_PRESET0 + i && !g.exporting;
        addBtn(B_PRESET0 + i, rr);
        if (i == sel) { fillRR(rr, 9, Wh(0.1f)); strokeRR(R(rr.left + 0.5f, rr.top + 0.5f, rr.right - 0.5f, rr.bottom - 0.5f), 9, Wh(0.38f)); }
        else if (hot) fillRR(rr, 9, Wh(0.06f));
        float ta = dis ? 0.35f : 0.95f;
        if (rowH >= 46) {
            txt(ps[i].title, R(rr.left + 12, rr.top + 4, rr.right - 10, rr.top + (rowH - 4) / 2 + 3), g.fBold, Wh(ta));
            txt(dis ? L"нужен ffmpeg.exe в папке tools" : ps[i].sub, R(rr.left + 12, rr.top + (rowH - 4) / 2 - 1, rr.right - 10, rr.bottom - 4), g.fSmall, Wh(dis ? 0.3f : 0.45f));
        } else txt(ps[i].title, R(rr.left + 12, rr.top, rr.right - 10, rr.bottom), g.fBold, Wh(ta));
        y += rowH;
    }
    if (it.video) {
        if (ps[sel].id == V_SIZE) {
            static const int mbs[4] = { 10, 25, 50, 100 };
            for (int i = 0; i < 4; i++) textBtn(B_SIZE0 + i, R(x0 + 12 + i * 79, y + 2, x0 + 12 + i * 79 + 75, y + 30), fmtW(L"%d МБ", mbs[i]), false, 1, g_targetMB == mbs[i]);
            y += 36;
        }
        y += 8; txt(L"ФРАГМЕНТ  (A начало, D конец)", R(l, y, r, y + 18), g.fSmall, Wh(0.38f)); y += 20;
        double ta = std::max(0.0, g_trimA), tb = g_trimB >= 0 ? g_trimB : g.vDur;
        txt((g_trimA < 0 && g_trimB < 0) ? WS(L"Весь ролик") : fmtTime(ta) + L" – " + fmtTime(tb), R(l, y, r - 180, y + 30), g.fUI, Wh(0.85f));
        textBtn(B_TRIM_A, R(r - 180, y, r - 122, y + 30), L"Начало", false); textBtn(B_TRIM_B, R(r - 120, y, r - 62, y + 30), L"Конец", false); textBtn(B_TRIM_X, R(r - 60, y, r, y + 30), L"Сброс", false);
        y += 30;
    }
    y += 12;
    txt(L"ПАПКА", R(l, y, r, y + 18), g.fSmall, Wh(0.38f)); y += 22;
    bool custom = !g.outDir.empty() && dirExists(g.outDir);
    RF fr = R(x0 + 12, y, Wd() - 12 - (custom ? 40 : 0), y + 40);
    addBtn(B_FOLDER, fr);
    fillRR(fr, 9, Wh(g.hot == B_FOLDER ? 0.1f : 0.055f));
    txt(L"", R(fr.left + 6, fr.top, fr.left + 38, fr.bottom), g.fIcon, Wh(0.7f), DWRITE_TEXT_ALIGNMENT_CENTER);
    txt(custom ? g.outDir : WS(L"Рядом с исходным файлом"), R(fr.left + 40, fr.top, fr.right - 10, fr.bottom), g.fUI, Wh(0.85f));
    if (custom) iconBtn(B_FOLDER_RESET, R(Wd() - 50, y, Wd() - 12, y + 40), L"", 0.7f, false, false, g.fIconS);
    // bottom
    float by = H - 20 - 44;
    if (g.exporting) {
        RF bar = R(x0 + 20, by - 26, Wd() - 20, by - 22);
        fillRR(bar, 2, Wh(0.14f)); fillRR(R(bar.left, bar.top, bar.left + (bar.right - bar.left) * g.progress, bar.bottom), 2, Wh(0.95f));
        txt(fmtW(L"Сохраняю… %d%%", (int)(g.progress * 100)), R(l, by - 54, r, by - 30), g.fSmall, Wh(0.6f));
        RF cb = R(x0 + 12, by, Wd() - 12, by + 44);
        strokeRR(R(cb.left + 0.5f, cb.top + 0.5f, cb.right - 0.5f, cb.bottom - 0.5f), 10, Wh(0.2f));
        textBtn(B_EXP_CANCEL, cb, L"Отмена", false);
    } else {
        if (it.video) {
            WS enc = !ff ? L"" : g_hwState != 2 ? L"Проверяю аппаратный кодировщик…" :
                g_hwH264.empty() ? L"Кодировщик: программный x264" :
                g_hwH264 == L"h264_nvenc" ? L"Кодировщик: видеокарта NVIDIA (NVENC)" : g_hwH264 == L"h264_qsv" ? L"Кодировщик: Intel Quick Sync" : L"Кодировщик: видеокарта AMD (AMF)";
            txt(enc, R(l, by - 30, r, by - 8), g.fSmall, Wh(0.4f));
        }
        textBtn(B_SAVE, R(x0 + 12, by, Wd() - 12, by + 44), L"Сохранить", true);
    }
}
static void drawDeps() {
    fillR(R(0, 0, Wd(), Hd()), C(0, 0, 0, 0.6f)); addBtn(B_NONE, R(0, 0, Wd(), Hd()));
    float rowH = 50, w = std::min(580.0f, Wd() - 24), h = 76 + g_deps.size() * rowH + 76;
    if (h > Hd() - 16) { rowH = std::max(30.0f, (Hd() - 16 - 152) / std::max<size_t>(1, g_deps.size())); h = 152 + g_deps.size() * rowH; }
    float x = (Wd() - w) / 2, y = std::max(8.0f, (Hd() - h) / 2);
    RF card = R(x, y, x + w, y + h);
    fillRR(card, 14, C(0.095f, 0.095f, 0.105f)); strokeRR(R(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), 14, Wh(0.1f));
    txt(L"Компоненты", R(x + 24, y + 16, x + w - 60, y + 50), g.fTitle, Wh(0.96f));
    txt(L"Что установлено в системе для полной поддержки форматов", R(x + 24, y + 46, x + w - 24, y + 66), g.fSmall, Wh(0.45f));
    iconBtn(B_DEPS_CLOSE, R(x + w - 52, y + 10, x + w - 10, y + 52), L"", 1, false, false, g.fIconS);
    float ry = y + 76;
    for (auto& d : g_deps) {
        CF col = d.state == 1 ? C(0.45f, 0.85f, 0.55f) : d.state == 2 ? C(0.95f, 0.8f, 0.4f) : C(1.0f, 0.5f, 0.45f);
        txt(d.state == 1 ? L"" : d.state == 2 ? L"" : L"", R(x + 20, ry, x + 48, ry + rowH), g.fIconS, col, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (d.state == 1 || rowH < 44) txt(d.name, R(x + 56, ry, x + w - 24, ry + rowH), g.fUI, Wh(0.92f));
        else {
            txt(d.name, R(x + 56, ry + 4, x + w - 24, ry + rowH / 2 + 3), g.fUI, Wh(0.92f));
            txt(d.state == 2 ? WS(L"Открывается через ffmpeg (медленнее). ") + d.hint : d.hint, R(x + 56, ry + rowH / 2 - 2, x + w - 24, ry + rowH - 4), g.fSmall, Wh(0.45f));
        }
        ry += rowH;
    }
    float by = y + h - 60;
    textBtn(B_DEPS_COPY, R(x + 20, by, x + w - 150, by + 40), L"Скопировать инструкцию для ИИ-агента", true);
    strokeRR(R(x + w - 139.5f, by + 0.5f, x + w - 20.5f, by + 39.5f), 10, Wh(0.2f));
    textBtn(B_DEPS_README, R(x + w - 140, by, x + w - 20, by + 40), L"README", false);
}
static void drawNameCard() {
    fillR(R(0, 0, Wd(), Hd()), C(0, 0, 0, 0.6f)); addBtn(B_NONE, R(0, 0, Wd(), Hd()));
    float w = std::min(460.0f, Wd() - 24), h = 196, x = (Wd() - w) / 2, y = std::max(8.0f, (Hd() - h) / 2);
    RF card = R(x, y, x + w, y + h);
    fillRR(card, 14, C(0.095f, 0.095f, 0.105f)); strokeRR(R(card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f), 14, Wh(0.1f));
    txt(L"Новая папка", R(x + 24, y + 16, x + w - 24, y + 50), g.fTitle, Wh(0.96f));
    txt(L"Создастся в «" + nameOf(g.folder) + fmtW(L"», файлов для переноса: %d", (int)g_namePaths.size()), R(x + 24, y + 48, x + w - 24, y + 68), g.fSmall, Wh(0.45f));
    RF fld = R(x + 24, y + 80, x + w - 24, y + 120);
    fillRR(fld, 8, Wh(0.06f)); strokeRR(R(fld.left + 0.5f, fld.top + 0.5f, fld.right - 0.5f, fld.bottom - 0.5f), 8, Wh(0.35f));
    RF tr = R(fld.left + 12, fld.top, fld.right - 12, fld.bottom);
    if (g_nameText.empty()) txt(L"Название папки", tr, g.fUI, Wh(0.3f)); else txt(g_nameText, tr, g.fUI, Wh(0.95f));
    if (fmod(nowSec(), 1.0) < 0.55) { float cx = std::min(tr.right, tr.left + txtW(g_nameText, g.fUI) + 1); fillR(R(cx, fld.top + 10, cx + 1.2f, fld.bottom - 10), Wh(0.9f)); }
    float by = y + h - 56;
    textBtn(B_NAME_OK, R(x + 24, by, x + w - 150, by + 40), L"Создать и переместить", true, g_nameText.empty() ? 0.4f : 1);
    strokeRR(R(x + w - 139.5f, by + 0.5f, x + w - 24.5f, by + 39.5f), 10, Wh(0.2f));
    textBtn(B_NAME_CANCEL, R(x + w - 140, by, x + w - 24, by + 40), L"Отмена", false);
}
static void drawToast() {
    double age = nowSec() - g.toastT; double life = g.toastPath.empty() ? 2.6 : 6.0;
    if (g.toast.empty() || age > life + 0.3) return;
    float a = age > life ? (float)(1 - (age - life) / 0.3) : 1.0f;
    WS s = g.toast; if (!g.toastPath.empty()) s += L"    Показать в папке";
    float w = std::min(Wd() - 24, txtW(s, g.fUI) + 36), x = (Wd() - (g.exportOpen ? kPanelW : 0) - w) / 2;
    RF r = R(x, 52, x + w, 52 + 38);
    fillRR(r, 10, C(0.16f, 0.16f, 0.18f, 0.97f * a)); strokeRR(R(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 10, Wh(0.12f * a));
    txt(s, R(r.left + 14, r.top, r.right - 14, r.bottom), g.fUI, Wh(0.95f * a), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!g.toastPath.empty()) addBtn(B_TOAST, r);
}

static float uiTarget() {
    if (g.items.empty() || g.mode != M_VIEW || g.cropMode || g.exportOpen || g.depsOpen || g_nameOpen) return 1;
    if ((g.hot > 0 && g.hot < B_STRIP0) || g.seeking) return 1;
    return nowSec() - g.lastMouse < 2.2 ? 1.0f : 0.0f;
}
static bool needsAnim() {
    if (!g.hwnd || IsIconic(g.hwnd)) return false;
    double t = nowSec();
    if (g.mode == M_VIEW && videoBusy()) return true;
    if (fabsf(g.uiAlpha - uiTarget()) > 0.004f) return true;
    if (t - g.rotT0 < 0.2) return true;
    if (g.mode == M_VIEW && g.animTotal > 0) return true;
    if (g.mode == M_GALLERY && fabsf(g.scroll - g.scrollTarget) > 0.4f) return true;
    if (fabsf(g_stripA - (g_strip && g.mode == M_VIEW && !g.items.empty() ? 1.0f : 0.0f)) > 0.004f) return true;
    if (g_strip && g.mode == M_VIEW && fabsf(g_stripPos - g.cur) > 0.01f) return true;
    double life = g.toastPath.empty() ? 2.6 : 6.0;
    if (!g.toast.empty() && t - g.toastT > life && t - g.toastT < life + 0.35) return true;
    return false;
}
static void recreateDevice();
static void saveShot(const WS& path);
extern WS g_shotPath;
static void render() {
    if (!g.ctx.p || !g.target.p) return;
    double t = nowSec(); float dt = (float)std::min(0.1, t - g.lastFrame); g.lastFrame = t;
    float ut = uiTarget();
    float prevA = g.uiAlpha;
    if (g.uiAlpha < ut) g.uiAlpha = std::min(ut, g.uiAlpha + dt / 0.12f); else if (g.uiAlpha > ut) g.uiAlpha = std::max(ut, g.uiAlpha - dt / 0.3f);
    if (prevA > 0 && g.uiAlpha == 0 && g.mode == M_VIEW && !g.items.empty()) { POINT cp; if (GetCursorPos(&cp) && WindowFromPoint(cp) == g.hwnd) SetCursor(nullptr); }   // idle: hide the pointer together with the interface
    if (g.mode == M_GALLERY && g.scrollTarget >= 0) {
        float d = g.scrollTarget - g.scroll;
        if (fabsf(d) < 0.4f) g.scroll = g.scrollTarget; else g.scroll += d * std::min(1.0f, dt * 18);
    }
    {
        float st = (g_strip && g.mode == M_VIEW && !g.items.empty()) ? 1.0f : 0.0f;
        if (g_stripA < st) g_stripA = std::min(st, g_stripA + dt / 0.16f); else if (g_stripA > st) g_stripA = std::max(st, g_stripA - dt / 0.16f);
        float d = g.cur - g_stripPos;
        if (fabsf(d) > 8) { g_stripPos = g.cur - (d > 0 ? 8.0f : -8.0f); d = g.cur - g_stripPos; }
        if (fabsf(d) < 0.01f) g_stripPos = (float)g.cur; else g_stripPos += d * std::min(1.0f, dt * 16);
    }
    if (g.mode == M_VIEW) { videoPump(); vsrPrepare(); }
    if (g.mode == M_VIEW && g.animTotal > 0 && !g.animSrc.empty() && g.cur >= 0 && !g.items[g.cur].video) {   // animated GIF: pick the frame for this moment
        int tms = (int)fmod((t - g.animT0) * 1000.0, (double)g.animTotal); size_t idx = 0;
        for (size_t i = 0; i < g.animDelays.size(); i++) { if (tms < g.animDelays[i]) { idx = i; break; } tms -= g.animDelays[i]; }
        if (!g.animBmps[idx].p) {
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            g.ctx->CreateBitmapFromWicBitmap(g.animSrc[idx], &bp, &g.animBmps[idx]);
        }
        if (g.animBmps[idx].p) g.img = g.animBmps[idx];
    }
    g.ctx->BeginDraw();
    ident();
    CF bg = C(0.035f, 0.035f, 0.04f); g.ctx->Clear(&bg);
    g.btns.clear(); g_seekRect = R(0, 0, 0, 0);
    if (g.items.empty() || g.cur < 0) drawEmpty();
    else if (g.mode == M_GALLERY) { drawGallery(); if (g.exportOpen) drawBatchPanel(); }
    else { drawViewer(); if (g.exportOpen) drawExportPanel(); }
    if (g.depsOpen) drawDeps();
    drawToast();
    if (g_nameOpen) drawNameCard();
    drawCaption((g.mode == M_VIEW && !g.items.empty() && g.cur >= 0 && !g.depsOpen) ? g.uiAlpha : 1.0f);
    HRESULT hr = g.ctx->EndDraw();
    if (!g_shotPath.empty()) { saveShot(g_shotPath); g_shotPath.clear(); }
    HRESULT hp = g.swap->Present(1, 0);
    g.occluded = (hp == DXGI_STATUS_OCCLUDED);
    if (hr == D2DERR_RECREATE_TARGET || hp == DXGI_ERROR_DEVICE_REMOVED || hp == DXGI_ERROR_DEVICE_RESET) { recreateDevice(); return; }
    int h = hitBtn();
    if (h != g.hot && g.pressed < 0) { g.hot = h; inval(); }
}
static void recreateDevice() {
    closeVideo(); g.dxgiMgr.reset(); vsrReset(); g_vsrCap = -1; g.animBmps.clear(); g.animSrc.clear(); g.animTotal = 0; g_fxPre.reset(); g_fxScale.reset(); g_fxBlur.reset(); g_fxComp.reset();
    g.img.reset(); g.thumbs.clear(); g.thumbPending.clear(); g.br.reset(); g.target.reset(); g.swap.reset(); g.ctx.reset(); g.d2dDev.reset(); g.d3dctx.reset(); g.d3d.reset();
    if (!createDevice()) { MessageBoxW(g.hwnd, L"Не удалось инициализировать Direct3D.", L"MediaSense", MB_ICONERROR); PostQuitMessage(1); return; }
    if (g.mode == M_VIEW && g.cur >= 0) go(g.cur);
    inval();
}


// ---------------------------------------------------------------- self-test (MediaSense.exe --selftest <folder>): scripted run that writes screenshots and a log
WS g_shotPath; static int st_step = -1; static double st_t = 0; static WS st_dir, st_prevOut; static std::string st_log;
static void saveShot(const WS& path) {
    Com<ID3D11Texture2D> bb, stg; Com<IWICImagingFactory> f; Com<IWICBitmap> b;
    if (FAILED(g.swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) return;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    if (FAILED(g.d3d->CreateTexture2D(&d, nullptr, &stg))) return;
    g.d3dctx->CopyResource(stg, bb);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g.d3dctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) return;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateBitmapFromMemory(d.Width, d.Height, GUID_WICPixelFormat32bppBGR, m.RowPitch, m.RowPitch * d.Height, (BYTE*)m.pData, &b)))
        wicSave(f, b, d.Width, d.Height, path, GUID_ContainerFormatJpeg, GUID_WICPixelFormat24bppBGR, 0.8f);
    g.d3dctx->Unmap(stg, 0);
}
static void stLog(const WS& s) { st_log += utf8(s) + "\r\n"; writeFile(st_dir + L"\\selftest.log", st_log); }
static void stShot(const wchar_t* name) { g_shotPath = st_dir + L"\\shot_" + name + L".jpg"; inval(); }
static int stFind(const wchar_t* name) { for (size_t i = 0; i < g.items.size(); i++) if (!lstrcmpiW(g.items[i].name.c_str(), name)) return (int)i; return 0; }
static void stVideo(const wchar_t* tag) {
    stLog(fmtW(L"%s: ready=%d error=%d frame=%d native=%dx%d dur=%.2f time=%.2f rot=%d", tag, g.vReady, g.vError, g.vHasFrame, g.vw, g.vh, g.vDur, videoTime(), g.cur >= 0 ? g.items[g.cur].rot : -1));
}
static void selftestTick() {
    double t = nowSec(); g.lastMouse = t;
    if (t < st_t || g.exporting || !g_shotPath.empty()) return;
    switch (++st_step) {
    case 0: stLog(L"MediaSense selftest start"); st_prevOut = g.outDir; openPath(st_dir); st_t = t + 3; break;
    case 1: stLog(fmtW(L"gallery: items=%d thumbs=%d pending=%d dpi=%.0f client=%dx%d maxBmp=%u", (int)g.items.size(), (int)g.thumbs.size(), (int)g.thumbPending.size(), g.dpi, g.pw, g.ph, g.maxBmp)); stShot(L"1_gallery"); break;
    case 2: go(stFind(L"photo1.jpg")); st_t = t + 2; break;
    case 3: stLog(fmtW(L"photo1: bmp=%d size=%dx%d exifRot=%d failed=%d loading=%d", g.img.p ? 1 : 0, g.imgW, g.imgH, g.baseRot, g.imgFailed, g.imgLoading)); stShot(L"2_photo"); break;
    case 4: rotateCur(1); st_t = t + 0.8; break;
    case 5: stShot(L"3_photo_rotated"); break;
    case 6: act(B_CROP); st_t = t + 0.3; break;
    case 7: setRatio(1); openExport(); g.presetImg = 0; st_t = t + 0.5; break;
    case 8: stShot(L"4_crop_export"); break;
    case 9: g.outDir = st_dir + L"\\out"; CreateDirectoryW(g.outDir.c_str(), nullptr); startExport(); st_t = t + 0.5; break;
    case 10: stLog(L"image export: " + g.toast + L" -> " + g.toastPath); g_store.setRot(g.items[g.cur].path, 0); g.items[g.cur].rot = 0; go(stFind(L"clip_phone_rotated.mp4")); st_t = t + 3; break;
    case 11: stVideo(L"phone clip"); stShot(L"5_video_phone"); break;
    case 12: rotateCur(1); st_t = t + 1; break;
    case 13: stVideo(L"phone clip rotated"); stShot(L"6_video_rotated"); break;
    case 14: openExport(); g.presetVid = 0; st_t = t + 0.3; break;
    case 15: if (g_hwState != 2) { st_step--; st_t = t + 0.3; break; } stLog(L"hw encoders: h264=" + g_hwH264 + L" hevc=" + g_hwHevc + L" ffmpeg=" + findFfmpeg()); stShot(L"7_video_export"); break;
    case 16: startExport(); st_t = t + 0.5; break;
    case 17: stLog(L"video export: " + g.toast + L" -> " + g.toastPath); g.presetVid = 4; g.exportOpen = true; startExport(); st_t = t + 0.5; break;
    case 18: stLog(L"video copy export: " + g.toast + L" -> " + g.toastPath); g_store.setRot(g.items[g.cur].path, 0); g.items[g.cur].rot = 0; g.exportOpen = false; go(stFind(L"clip_landscape.mp4")); st_t = t + 2.5; break;
    case 19: stVideo(L"landscape clip"); stShot(L"8_video_landscape"); break;
    case 20: act(B_INFO); st_t = t + 0.5; break;
    case 21: for (auto& d : g_deps) stLog(fmtW(L"dep %d: ", d.state) + d.name); stShot(L"9_components"); break;
    case 22: g.outDir = st_prevOut; g_store.put(L"outdir", st_prevOut); stLog(L"selftest done"); PostMessageW(g.hwnd, WM_CLOSE, 0, 0); break;
    }
}

// ---------------------------------------------------------------- input
static int cropHit(float x, float y) {
    RF c = g_cropScreen; const float t = 16;
    if (x < c.left - t || x > c.right + t || y < c.top - t || y > c.bottom + t) return -1;
    int m = 0;
    if (fabsf(x - c.left) <= t) m |= 1; else if (fabsf(x - c.right) <= t) m |= 4;
    if (fabsf(y - c.top) <= t) m |= 2; else if (fabsf(y - c.bottom) <= t) m |= 8;
    return m ? m : 16;
}
static void cropDragTo(float x, float y) {
    RF d = g_dispScreen; float w = d.right - d.left, h = d.bottom - d.top;
    if (w < 1 || h < 1) return;
    float nx = (x - d.left) / w, ny = (y - d.top) / h;
    RF c = g.cropOrig; int m = g.cropDrag; const float mn = 0.03f;
    if (m == 16) {
        float dx = nx - g.cropGrab.x, dy = ny - g.cropGrab.y;
        dx = std::max(-c.left, std::min(1 - c.right, dx)); dy = std::max(-c.top, std::min(1 - c.bottom, dy));
        g.crop = R(c.left + dx, c.top + dy, c.right + dx, c.bottom + dy); return;
    }
    nx = std::max(0.0f, std::min(1.0f, nx)); ny = std::max(0.0f, std::min(1.0f, ny));
    if (m & 1) c.left = std::min(nx, c.right - mn);
    if (m & 4) c.right = std::max(nx, c.left + mn);
    if (m & 2) c.top = std::min(ny, c.bottom - mn);
    if (m & 8) c.bottom = std::max(ny, c.top + mn);
    if (g.cropRatio > 0 && g_dispH > 0) {
        static const float ars[] = { 0, 1.0f, 4.0f / 3, 3.0f / 2, 16.0f / 9, 9.0f / 16 };
        float k = ars[g.cropRatio] * g_dispH / g_dispW;   // normalized width / normalized height
        if (m & 5) { float ch = (c.right - c.left) / k; if (m & 2) c.top = c.bottom - ch; else if (m & 8) c.bottom = c.top + ch; else { float cy = (c.top + c.bottom) / 2; c.top = cy - ch / 2; c.bottom = cy + ch / 2; } }
        else { float cw = (c.bottom - c.top) * k; float cx = (c.left + c.right) / 2; c.left = cx - cw / 2; c.right = cx + cw / 2; }
        if (c.left < -0.0005f || c.top < -0.0005f || c.right > 1.0005f || c.bottom > 1.0005f || c.right - c.left < mn || c.bottom - c.top < mn) return;
    }
    g.crop = c;
}
static double seekFromX(float x) {
    float w = g_seekRect.right - g_seekRect.left; if (w < 1) return 0;
    return std::max(0.0f, std::min(1.0f, (x - g_seekRect.left) / w)) * g.vDur;
}
static void showMenu(POINT scr) {
    HMENU m = CreatePopupMenu();
    bool has = g.cur >= 0 && !g.items.empty(), view = has && g.mode == M_VIEW;
    {   // sorting: move into a subfolder of the current folder
        scanSubdirs();
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, B_MOVE_NEW, L"Новая папка\u2026");
        if (!g_moveDirs.empty()) AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < g_moveDirs.size(); i++) {
            WS nm = g_moveDirs[i]; for (size_t k = 0; k < nm.size(); k++) if (nm[k] == L'&') { nm.insert(k, 1, L'&'); k++; }
            AppendMenuW(sub, MF_STRING | ((i && i % 30 == 0) ? MF_MENUBARBREAK : 0), B_MOVE0 + (int)i, nm.c_str());
        }
        AppendMenuW(m, MF_POPUP | (has ? 0 : MF_GRAYED), (UINT_PTR)sub, L"Отправить в папку");
        if (has && !g_lastMoveDir.empty() && dirExists(g_lastMoveDir)) {
            WS nm = nameOf(g_lastMoveDir); for (size_t k = 0; k < nm.size(); k++) if (nm[k] == L'&') { nm.insert(k, 1, L'&'); k++; }
            AppendMenuW(m, MF_STRING, B_MOVE_LAST, (L"Отправить в \u00AB" + nm + ((view && g.items[g.cur].video) ? L"\u00BB\tCtrl+M" : L"\u00BB\tM")).c_str());
        }
        if (!g_lastMoves.empty()) AppendMenuW(m, MF_STRING, B_UNDO_MOVE, L"Вернуть перемещённое\tCtrl+Z");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(m, MF_STRING, B_OPEN, L"Открыть файл…\tCtrl+O");
    AppendMenuW(m, MF_STRING, B_OPENDIR, L"Открыть папку…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_ROTL, L"Повернуть влево\tL");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_ROTR, L"Повернуть вправо\tR");
    AppendMenuW(m, MF_STRING | (view ? 0 : MF_GRAYED), B_CROP, L"Обрезать\tC");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_EXPORT, L"Конвертировать и сохранить…\tE");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (view ? 0 : MF_GRAYED), B_FIT, L"Вписать в окно\t0");
    AppendMenuW(m, MF_STRING | (view ? 0 : MF_GRAYED), B_100, L"Реальный размер\t1");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_GRID, g.mode == M_GALLERY ? L"Открыть выбранное\tEnter" : L"Галерея\tG");
    bool vid = view && g.items[g.cur].video;
    AppendMenuW(m, MF_STRING | (vid ? 0 : MF_GRAYED), B_FRAME, L"Сохранить кадр как фото\tP");
    AppendMenuW(m, MF_STRING | (g_loop ? MF_CHECKED : 0), B_LOOP, L"Повтор видео\tO");
    AppendMenuW(m, MF_STRING | (g_enh ? MF_CHECKED : 0), B_ENH, L"Улучшение видео\tH");
    AppendMenuW(m, MF_STRING | (view ? 0 : MF_GRAYED), B_STRIP, L"Плёнка\tF");
    AppendMenuW(m, MF_STRING, B_FULL, L"Во весь экран\tF11");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_SHOW, L"Показать в проводнике");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_COPYFILE, L"Копировать файл\tCtrl+C");
    AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), B_COPYPATH, L"Копировать путь");
    AppendMenuW(m, MF_STRING | (view ? 0 : MF_GRAYED), B_DELETE, L"Удалить в корзину\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, B_INFO, L"Компоненты и кодеки\tI");
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, scr.x, scr.y, 0, g.hwnd, nullptr);
    DestroyMenu(m);
    g.lastMouse = nowSec();
    if (id > 0) act(id);
}
static void onKey(WPARAM vk) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    int n = (int)g.items.size();
    if (vk == VK_ESCAPE) {
        if (g.depsOpen) g.depsOpen = false;
        else if (g.exportOpen && !g.exporting) g.exportOpen = false;
        else if (g.exportOpen) {}
        else if (g.mode == M_GALLERY && g_selMode) clearSel();
        else if (g.cropMode) g.cropMode = false;
        else if (g.fullscreen) toggleFullscreen();
        else if (g.mode == M_VIEW && g.zoom > 1.001f) { g.zoom = 1; g.pan = D2D1::Point2F(0, 0); }
        else PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
        inval(); return;
    }
    if (g.depsOpen) return;
    if (ctrl && vk == 'O') { act(B_OPEN); return; }
    if (ctrl && vk == 'Z') { act(B_UNDO_MOVE); return; }
    if (vk == 'M' && n && g.cur >= 0 && (g.mode == M_GALLERY || ctrl || !g.items[g.cur].video)) { act(B_MOVE_LAST); return; }   // for video plain M stays mute
    if (vk == VK_F11) { toggleFullscreen(); return; }
    if (vk == 'I' || vk == VK_F1) { act(B_INFO); return; }
    if (!n || g.cur < 0) return;
    if (vk == 'L' || vk == VK_OEM_4) { rotateCur(-1); return; }
    if (vk == 'R' || vk == VK_OEM_6) { rotateCur(1); return; }
    if (vk == 'E' || (ctrl && vk == 'S')) { act(B_EXPORT); return; }
    if (g.mode == M_GALLERY) {
        int cols; float cs; galleryMetrics(cols, cs); int c = g.cur;
        switch (vk) {
        case VK_LEFT: c--; break; case VK_RIGHT: c++; break; case VK_UP: c -= cols; break; case VK_DOWN: c += cols; break;
        case VK_HOME: c = 0; break; case VK_END: c = n - 1; break;
        case VK_PRIOR: c -= cols * std::max(1, (int)((Hd() - kGalTop) / (cs + kGalGap))); break;
        case VK_NEXT: c += cols * std::max(1, (int)((Hd() - kGalTop) / (cs + kGalGap))); break;
        case VK_RETURN: case 'G': case VK_SPACE: go(g.cur); return;
        case 'A': if (ctrl) { for (auto& it : g.items) g_sel.insert(it.path); g_selMode = true; inval(); } return;
        case 'C': if (ctrl) act(B_COPYFILE); return;
        case 'S': act(B_SORT); return;
        case 'T': act(B_FILTER); return;
        case VK_DELETE: act(B_SEL_DELETE); return;
        default: return;
        }
        g.cur = std::max(0, std::min(n - 1, c)); galleryReveal(); inval(); return;
    }
    bool video = g.items[g.cur].video;
    if (vk == 'F') { act(B_STRIP); return; }
    if (ctrl && video && (vk == VK_LEFT || vk == VK_RIGHT)) { videoSeek(videoTime() + (vk == VK_LEFT ? -30 : 30)); return; }
    switch (vk) {
    case VK_LEFT: case VK_PRIOR: case VK_BACK: act(B_PREV); break;
    case VK_RIGHT: case VK_NEXT: act(B_NEXT); break;
    case VK_HOME: go(0); break; case VK_END: go(n - 1); break;
    case VK_SPACE: if (video) videoToggle(); else act(B_NEXT); break;
    case VK_UP: if (video) setVolume(g.volume + 0.1); break;
    case VK_DOWN: if (video) setVolume(g.volume - 0.1); break;
    case 'M': act(B_MUTE); break;
    case 'H': if (video) act(B_ENH); break;
    case 'C': if (ctrl) act(B_COPYFILE); else act(B_CROP); break;
    case 'S': if (video) act(B_SPEED); break;
    case 'O': if (video) act(B_LOOP); break;
    case 'P': if (video) act(B_FRAME); break;
    case 'A': if (video) act(B_TRIM_A); break;
    case 'D': if (video) act(B_TRIM_B); break;
    case 'X': if (video) act(B_TRIM_X); break;
    case VK_OEM_COMMA: case VK_OEM_PERIOD: if (video && g.eng.p && g.vReady) { if (!g.eng->IsPaused()) g.eng->Pause(); videoSeek(videoTime() + (vk == VK_OEM_PERIOD ? 0.04 : -0.04)); } break;
    case 'G': act(B_GRID); break;
    case VK_RETURN: if (g.cropMode) act(B_CROP_OK); else if (g.exportOpen && !g.exporting) startExport(); else toggleFullscreen(); break;
    case VK_DELETE: deleteCurrent(); break;
    case '0': case VK_NUMPAD0: act(B_FIT); break;
    case '1': case VK_NUMPAD1: act(B_100); break;
    case VK_ADD: case VK_OEM_PLUS: zoomAt(1.25f, (mediaArea().left + mediaArea().right) / 2, (mediaArea().top + mediaArea().bottom) / 2); break;
    case VK_SUBTRACT: case VK_OEM_MINUS: zoomAt(0.8f, (mediaArea().left + mediaArea().right) / 2, (mediaArea().top + mediaArea().bottom) / 2); break;
    case 'J': if (video) videoSeek(videoTime() - 5); break;
    case 'K': if (video) videoSeek(videoTime() + 5); break;
    }
}

static LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE:   // no system title bar or borders: the whole window is ours
        if (wp) {
            if (IsZoomed(hw) && !g.fullscreen) {
                NCCALCSIZE_PARAMS* np = (NCCALCSIZE_PARAMS*)lp; UINT dpi = (UINT)g.dpi;
                int f = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                np->rgrc[0].left += f; np->rgrc[0].top += f; np->rgrc[0].right -= f; np->rgrc[0].bottom -= f;
            }
            return 0;
        }
        break;
    case WM_NCACTIVATE: return DefWindowProcW(hw, msg, wp, -1);
    case WM_NCHITTEST: {
        if (g.fullscreen) return HTCLIENT;
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(hw, &p);
        RECT rc; GetClientRect(hw, &rc);
        if (!IsZoomed(hw)) {
            int b = (int)(6 * g.dpi / 96); bool l = p.x < b, r = p.x >= rc.right - b, t = p.y < b, bt = p.y >= rc.bottom - b;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (bt && l) return HTBOTTOMLEFT;
            if (bt && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (bt) return HTBOTTOM;
        }
        float x = p.x * 96.0f / g.dpi, y = p.y * 96.0f / g.dpi;
        if (y < 34) {   // top strip drags the window unless one of our buttons is there
            for (auto& bn : g.btns) if (inR(bn.r, x, y)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT; }
    case WM_NCMOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(hw, &p);
        if (p.x != g.mouse.x || p.y != g.mouse.y) { g.mouse = p; g.lastMouse = nowSec(); inval(); }
        break; }
    case WM_PAINT: render(); ValidateRect(hw, nullptr); return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) { g.pw = LOWORD(lp); g.ph = HIWORD(lp); resizeSwap(); if (g.hwnd) render(); }
        return 0;
    case WM_DPICHANGED: {
        g.dpi = (float)HIWORD(wp); RECT* r = (RECT*)lp;
        SetWindowPos(hw, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (g.ctx.p) { makeTarget(); inval(); }
        return 0; }
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)lp; mm->ptMinTrackSize.x = (LONG)(520 * g.dpi / 96); mm->ptMinTrackSize.y = (LONG)(400 * g.dpi / 96); return 0; }
    case WM_TIMER: {
        if (wp == 2) { selftestTick(); return 0; }
        double t = nowSec();
        if (!g_bakePending.empty()) pumpBakes(false);
        if (g.uiAlpha != uiTarget()) inval();
        if (!g.toast.empty() && t - g.toastT > 2.5 && t - g.toastT < 7) inval();
        if (g_nameOpen) inval();
        if (g.mode == M_VIEW && g.cur >= 0 && g.items[g.cur].video && g.uiAlpha > 0) inval();
        return 0; }
    case WM_MOUSEMOVE: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (p.x == g.mouse.x && p.y == g.mouse.y) return 0;
        g.mouse = p; g.lastMouse = nowSec();
        TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, hw, 0 }; TrackMouseEvent(&te);
        float x = p.x * 96.0f / g.dpi, y = p.y * 96.0f / g.dpi;
        if (g.cropDrag >= 0) cropDragTo(x, y);
        else if (g.seeking) { g.seekPos = seekFromX(x); if (g.eng.p && !g.eng->IsSeeking()) videoSeek(g.seekPos); }
        else if (g.panning) { g.pan.x = g.panOrig.x + (p.x - g.panStart.x) * 96.0f / g.dpi; g.pan.y = g.panOrig.y + (p.y - g.panStart.y) * 96.0f / g.dpi; }
        else if ((g_galDown >= 0 || g_dragArm) && (abs(p.x - g_downPt.x) > 8 * g.dpi / 96 || abs(p.y - g_downPt.y) > 8 * g.dpi / 96)) {
            std::vector<WS> paths;   // drag the file(s) out of the window
            if (g_galDown >= 0 && g_galDown < (int)g.items.size()) { if (g_sel.count(g.items[g_galDown].path)) paths = targetPaths(); else paths.push_back(g.items[g_galDown].path); }
            else if (g.cur >= 0 && g.cur < (int)g.items.size()) paths.push_back(g.items[g.cur].path);
            g_galDown = -1; g_dragArm = false; ReleaseCapture();
            if (!paths.empty()) dragOutFiles(paths);
        }
        else { int h = hitBtn(); if (g.pressed < 0) g.hot = h; else g.hot = (h == g.pressed) ? h : -1; }
        inval(); return 0; }
    case WM_MOUSELEAVE: g.mouse.x = g.mouse.y = -1; if (g.pressed < 0) g.hot = -1; inval(); return 0;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        g.mouse.x = GET_X_LPARAM(lp); g.mouse.y = GET_Y_LPARAM(lp); g.lastMouse = nowSec();
        float x = g.mouse.x * 96.0f / g.dpi, y = g.mouse.y * 96.0f / g.dpi;
        int h = hitBtn();
        if (h < 0 && msg == WM_LBUTTONDBLCLK && g.mode == M_VIEW && !g.cropMode && !g.items.empty()) { toggleFullscreen(); return 0; }
        SetCapture(hw);
        if (h > 0) {
            g.pressed = g.hot = h;
            if (h == B_SEEK) { g.seeking = true; g.seekPos = seekFromX(x); videoSeek(g.seekPos); }
        } else if (h < 0 && g.mode == M_VIEW && !g.items.empty()) {
            if (g.cropMode && g_haveDisp) {
                int m = cropHit(x, y);
                if (m >= 0) { g.cropDrag = m; g.cropOrig = g.crop; RF d = g_dispScreen; g.cropGrab = D2D1::Point2F((x - d.left) / (d.right - d.left), (y - d.top) / (d.bottom - d.top)); }
            } else if (g.zoom > 1.001f) { g.panning = true; g.panStart = g.mouse; g.panOrig = g.pan; }
            else if (g_haveDisp && inR(g_dispScreen, x, y) && msg == WM_LBUTTONDOWN) { g_dragArm = true; g_downPt = g.mouse; }
        } else if (h < 0 && g.mode == M_GALLERY) {
            int i = galleryHit(x, y);
            if (i >= 0) { g_galDown = i; g_downPt = g.mouse; }
        }
        inval(); return 0; }
    case WM_LBUTTONUP: {
        g.mouse.x = GET_X_LPARAM(lp); g.mouse.y = GET_Y_LPARAM(lp);
        ReleaseCapture();
        int pr = g.pressed; g.pressed = -1;
        if (g.seeking) { g.seeking = false; videoSeek(g.seekPos); }
        g.cropDrag = -1; g.panning = false; g_dragArm = false;
        if (g_galDown >= 0) {
            int gi = g_galDown; g_galDown = -1;
            if (g.mode == M_GALLERY && hitBtn() < 0 && galleryHit(g.mouse.x * 96.0f / g.dpi, g.mouse.y * 96.0f / g.dpi) == gi) { galleryClick(gi); return 0; }
        }
        int h = hitBtn(); g.hot = h;
        if (pr > 0 && h == pr && pr != B_SEEK) act(pr);
        inval(); return 0; }
    case WM_RBUTTONUP: { POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g.mode == M_GALLERY && !g.depsOpen && !g_nameOpen) { g.mouse = p; if (hitBtn() < 0) { int gi = galleryHit(p.x * 96.0f / g.dpi, p.y * 96.0f / g.dpi); if (gi >= 0 && !g_sel.count(g.items[gi].path)) { if (!g_sel.empty()) clearSel(); g.cur = gi; } } } ClientToScreen(hw, &p); if (!g.depsOpen && !g_nameOpen) showMenu(p); return 0; }
    case WM_XBUTTONUP: if (g.mode == M_VIEW) act(HIWORD(wp) == XBUTTON1 ? B_PREV : B_NEXT); return TRUE;
    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(hw, &p);
        float d = GET_WHEEL_DELTA_WPARAM(wp) / 120.0f; g.lastMouse = nowSec();
        if (g.depsOpen || g_nameOpen || g.items.empty()) return 0;
        if (g.mode == M_GALLERY) { g.scrollTarget = std::max(0.0f, std::min(galleryMaxScroll(), g.scrollTarget - d * 130)); inval(); }
        else if (g_stripA > 0.5f && p.y * 96.0f / g.dpi > Hd() - kStripH) { act(d > 0 ? B_PREV : B_NEXT); }
        else if (!g.cropMode) {
            float x = p.x * 96.0f / g.dpi, y = p.y * 96.0f / g.dpi;
            if (g.exportOpen && x > Wd() - kPanelW) return 0;
            zoomAt(powf(1.2f, d), x, y);
        }
        return 0; }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            if (g.uiAlpha < 0.02f && g.mode == M_VIEW && !g.items.empty()) { SetCursor(nullptr); return TRUE; }
            if (g.cropMode && g_haveDisp && hitBtn() < 0) {
                int m = cropHit(g.mouse.x * 96.0f / g.dpi, g.mouse.y * 96.0f / g.dpi);
                LPCWSTR c = IDC_ARROW;
                if (m == 16) c = IDC_SIZEALL; else if (m == 3 || m == 12) c = IDC_SIZENWSE; else if (m == 6 || m == 9) c = IDC_SIZENESW;
                else if (m == 1 || m == 4) c = IDC_SIZEWE; else if (m == 2 || m == 8) c = IDC_SIZENS;
                SetCursor(LoadCursorW(nullptr, c)); return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, (g.hot > 0) ? IDC_HAND : IDC_ARROW)); return TRUE;
        }
        break;
    case WM_KEYDOWN: if (g_nameOpen) { inval(); return 0; } onKey(wp); return 0;
    case WM_CHAR: if (g_nameOpen) { nameChar((wchar_t)wp); inval(); } return 0;
    case WM_DROPFILES: {
        HDROP h = (HDROP)wp; wchar_t b[4096];
        if (!g_dragOut && nowSec() - g_dragEnd > 0.6 && DragQueryFileW(h, 0, b, 4096)) { g.depsOpen = false; if (!g.exporting) g.exportOpen = false; openPath(b); }
        DragFinish(h); SetForegroundWindow(hw); return 0; }
    case WM_APP_IMG: {
        ImgRes* r = (ImgRes*)lp;
        ImgEnt e; e.path = r->path; e.bmp = r->bmp; e.exifRot = r->exifRot; e.w = r->w; e.h = r->h; e.frames = r->frames; e.delays = r->delays;
        for (size_t i = 0; i < g.icache.size(); i++) if (g.icache[i].path == e.path) { g.icache.erase(g.icache.begin() + i); break; }
        g.icache.push_back(e);
        for (;;) {
            double bytes = 0; for (auto& c : g.icache) bytes += (double)c.w * c.h * 4 * std::max<size_t>(1, c.frames.size());
            if (g.icache.size() > 7 || (g.icache.size() > 3 && bytes > 700e6)) g.icache.erase(g.icache.begin()); else break;
        }
        if (g.mode == M_VIEW && g.cur >= 0 && !g.items[g.cur].video && g.items[g.cur].path == r->path && !g.img.p) { ImgEnt* c = cachedImage(r->path); if (c) setImage(c); inval(); }
        delete r; return 0; }
    case WM_APP_THUMB: onThumb((ThumbRes*)lp); inval(); return 0;
    case WM_APP_MEDIA:
        if ((int)lp != g.vgen || !g.eng.p) return 0;
        switch (wp) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA: {
            g.vReady = true; double d = g.eng->GetDuration(); g.vDur = (d == d && d < 1e9 && d > 0) ? d : 0;
            videoMakeTexture();
            if (fabs(g_rate - 1.0) > 0.01) { g.eng->SetDefaultPlaybackRate(g_rate); g.eng->SetPlaybackRate(g_rate); }
            if (g_loop) g.eng->SetLoop(TRUE);
            if (g.cur >= 0 && g.vw > 0) {   // first open after a rotation was written: confirm the system player honours it, otherwise keep rotating in-app
                Item& it = g.items[g.cur]; WS key = L"bk:" + lower(it.path); auto f = g_store.set.find(key);
                if (f != g_store.set.end()) {
                    int land = 0, turns = 0;
                    if (swscanf(f->second.c_str(), L"%d,%d", &land, &turns) == 2 && (turns & 1) && ((g.vw > g.vh) ? 1 : 0) == land) {
                        it.rot = (it.rot + turns) & 3; g_store.setRot(it.path, it.rot); g_store.putInt(L"eng_ignores_rot", 1);
                    }
                    g_store.set.erase(f); g_store.saveSet();
                }
            }
            break; }
        case MF_MEDIA_ENGINE_EVENT_FORMATCHANGE: videoMakeTexture(); break;
        case MF_MEDIA_ENGINE_EVENT_ERROR: g.vError = true; break;
        }
        g.vWake = std::max(g.vWake, nowSec() + 0.4); inval(); return 0;
    case WM_APP_EXPORT:
        if (wp == 0) g.progress = g_batchN > 0 ? ((float)g_batchIdx + lp / 1000.0f) / (float)g_batchN : lp / 1000.0f;
        else {
            g.exporting = false; g.progress = 0; if (wp != 4) InterlockedExchange(&g_batchN, 0);
            if (wp == 1) { WS* s = (WS*)lp; showToast(L"Сохранено: " + nameOf(*s), *s); delete s; g.cropMode = false; g.exportOpen = false; }
            else if (wp == 2) { WS* s = (WS*)lp; showToast(L"Ошибка: " + *s); delete s; }
            else if (wp == 4) {
                WS dir = (!g.outDir.empty() && dirExists(g.outDir)) ? g.outDir : g.folder;
                showToast(fmtW(L"Готово: %d из %d", (int)LOWORD(lp), (int)HIWORD(lp)), dir);
                InterlockedExchange(&g_batchN, 0); g.exportOpen = false; clearSel();
            }
            else showToast(L"Отменено");
        }
        inval(); return 0;
    case WM_APP_HW: inval(); return 0;
    case WM_COPYDATA: {   // a second launch hands its file over to this window
        COPYDATASTRUCT* cd = (COPYDATASTRUCT*)lp;
        if (g_closing || !cd || cd->dwData != 0x4D53) return 0;
        WS path; if (cd->lpData && cd->cbData >= sizeof(wchar_t)) path.assign((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t)); while (!path.empty() && path.back() == 0) path.pop_back();
        if (IsIconic(hw)) ShowWindow(hw, SW_RESTORE);
        SetForegroundWindow(hw);
        if (!path.empty()) { g.depsOpen = false; if (!g.exporting) g.exportOpen = false; openPath(path); }
        return 1; }
    case WM_APP_BAKE: onBakeDone((Bake*)lp); return 0;
    case WM_CLOSE:
        if (g_closing) return 0;
        if (g.exporting) cancelExport();
        if (g.mode == M_VIEW) queueBake(g.cur, 0);
        closeVideo();
        g_closing = true; pumpBakes(true);
        if (g_bakeActive > 0) { ShowWindow(hw, SW_HIDE); return 0; }   // finish writing rotations in the background, then exit
        DestroyWindow(hw); return 0;
    case WM_DESTROY: {
        WINDOWPLACEMENT wpl = { sizeof(wpl) };
        if (g.fullscreen) wpl = g.wpPrev; else GetWindowPlacement(hw, &wpl);
        RECT r = wpl.rcNormalPosition;
        g_store.put(L"win", fmtW(L"%d,%d,%d,%d,%d", (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top), wpl.showCmd == SW_SHOWMAXIMIZED ? 1 : 0));
        g_store.saveSet();
        closeVideo();
        PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    {   // one window: if MediaSense is already running, pass the file to it and exit
        int ac = 0; LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &ac);
        bool selftest = av && ac > 1 && !lstrcmpiW(av[1], L"--selftest");
        HWND other = selftest ? nullptr : FindWindowW(L"MediaSenseWnd", nullptr);
        if (other) {
            WS path; if (av && ac > 1) { wchar_t full[4096]; DWORD n = GetFullPathNameW(av[1], 4096, full, nullptr); path = (n && n < 4096) ? WS(full, n) : WS(av[1]); }
            COPYDATASTRUCT cd = { 0x4D53, (DWORD)((path.size() + 1) * sizeof(wchar_t)), (PVOID)path.c_str() };
            DWORD pid = 0; GetWindowThreadProcessId(other, &pid); AllowSetForegroundWindow(pid);
            DWORD_PTR res = 0;
            if (SendMessageTimeoutW(other, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 3000, &res) && res == 1) return 0;
        }
        if (av) LocalFree(av);
    }
    OleInitialize(nullptr);   // COM apartment + drag-and-drop
    g_store.load();
    g.volume = g_store.getInt(L"volume", 100) / 100.0; g.muted = g_store.getInt(L"muted", 0) != 0;
    g.outDir = g_store.get(L"outdir"); g.presetImg = std::max(0, std::min(kImgPresetN - 1, g_store.getInt(L"preset_img", 0))); g.presetVid = std::max(0, std::min(kVidPresetN - 1, g_store.getInt(L"preset_vid", 0)));

    // dark context menus on Windows 10 1903+ / 11 (uxtheme ordinals 135/136); silently skipped if absent
    if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        typedef int (WINAPI* SetMode)(int); typedef void (WINAPI* Flush)();
        SetMode sm = (SetMode)GetProcAddress(ux, MAKEINTRESOURCEA(135)); Flush fl = (Flush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (sm && fl) { sm(2); fl(); }
    }
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = wndProc; wc.hInstance = hi; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = L"MediaSenseWnd";
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1280, h = 820, mx = 0;
    WS win = g_store.get(L"win"); bool saved = swscanf(win.c_str(), L"%d,%d,%d,%d,%d", &x, &y, &w, &h, &mx) == 5 && w > 300 && h > 200;
    if (saved) { RECT r = { x, y, x + w, y + h }; if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) saved = false; }
    if (!saved) { x = y = CW_USEDEFAULT; w = 1280; h = 820; mx = 0; }
    HWND hw = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"MediaSense", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hi, nullptr);
    if (!hw) return 1;
    g.hwnd = hw; g.dpi = (float)GetDpiForWindow(hw);
    if (!saved) { SetWindowPos(hw, nullptr, 0, 0, (int)(1280 * g.dpi / 96), (int)(820 * g.dpi / 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); }
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hw, 20, &dark, sizeof(dark)))) DwmSetWindowAttribute(hw, 19, &dark, sizeof(dark));
    COLORREF cap = RGB(9, 9, 10); DwmSetWindowAttribute(hw, 35, &cap, sizeof(cap));
    RECT cr; GetClientRect(hw, &cr); g.pw = cr.right; g.ph = cr.bottom;
    if (!createDevice()) { MessageBoxW(nullptr, L"Не удалось инициализировать Direct3D 11. Обновите драйвер видеокарты.", L"MediaSense", MB_ICONERROR); return 1; }
    g.lastMouse = -100; g_strip = g_store.getInt(L"strip", 0) != 0; g_enh = g_store.getInt(L"enhance", 1) != 0;
    g_sort = g_store.getInt(L"sort", 0) ? 1 : 0; g_filter = std::max(0, std::min(2, g_store.getInt(L"filter", 0))); g_targetMB = std::max(1, g_store.getInt(L"target_mb", 25));
    SetWindowPos(hw, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    GetClientRect(hw, &cr); g.pw = cr.right; g.ph = cr.bottom; resizeSwap();
    for (int i = 0; i < 2; i++) CloseHandle(CreateThread(nullptr, 0, imgWorker, nullptr, 0, nullptr));
    for (int i = 0; i < 3; i++) CloseHandle(CreateThread(nullptr, 0, thumbWorker, nullptr, 0, nullptr));

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 2 && !lstrcmpiW(argv[1], L"--selftest")) { st_dir = argv[2]; g_noBake = true; SetTimer(hw, 2, 100, nullptr); }
    else if (argv && argc > 1) openPath(argv[1]);
    if (argv) LocalFree(argv);
    ShowWindow(hw, mx ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hw);
    SetTimer(hw, 1, 250, nullptr);

    MSG m;
    for (;;) {
        if (needsAnim()) {
            bool quit = false;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) { quit = true; break; } TranslateMessage(&m); DispatchMessageW(&m); }
            if (quit) break;
            render();
            if (g.occluded) Sleep(30);
        } else {
            BOOL r = GetMessageW(&m, nullptr, 0, 0);
            if (r <= 0) break;
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    if (g.mfStarted) MFShutdown();
    ExitProcess(0);   // worker threads are parked on condition variables; nothing to flush
    return 0;
}
