// Extras: selection and batch operations, clipboard and drag-out, frame capture, optional RTX Video Super Resolution.

// ---------------------------------------------------------------- selection (gallery)
static std::vector<int> selIdx() {
    std::vector<int> v;
    for (size_t i = 0; i < g.items.size(); i++) if (g_sel.count(g.items[i].path)) v.push_back((int)i);
    return v;
}
static void clearSel() { g_sel.clear(); g_selMode = false; g_selAnchor = -1; if (g.mode == M_GALLERY && !g.exporting) g.exportOpen = false; }
static std::vector<WS> targetPaths() {   // what a file command applies to: the selection in the gallery, otherwise the current file
    std::vector<WS> v;
    if (g.mode == M_GALLERY && !g_sel.empty()) { for (int i : selIdx()) v.push_back(g.items[i].path); }
    else if (g.cur >= 0 && g.cur < (int)g.items.size()) v.push_back(g.items[g.cur].path);
    return v;
}
static void rescanKeep() {
    WS curPath = (g.cur >= 0 && g.cur < (int)g.items.size()) ? g.items[g.cur].path : L"";
    int old = g.cur;
    scanFolder(g.folder);
    int n = (int)g.items.size(), idx = -1;
    std::unordered_set<WS> keep;
    for (int i = 0; i < n; i++) { if (g.items[i].path == curPath) idx = i; if (g_sel.count(g.items[i].path)) keep.insert(g.items[i].path); }
    g_sel.swap(keep);
    g.cur = n ? (idx >= 0 ? idx : std::max(0, std::min(n - 1, old))) : -1;
    g.scrollTarget = std::max(0.0f, g.scrollTarget);
    updateTitle(); inval();
}
static void galleryClick(int i) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (!ctrl && !shift && !g_selMode) { go(i); return; }
    if (shift && g_selAnchor >= 0 && g_selAnchor < (int)g.items.size()) {
        for (int k = std::min(i, g_selAnchor); k <= std::max(i, g_selAnchor); k++) g_sel.insert(g.items[k].path);
    } else {
        const WS& p = g.items[i].path;
        if (g_sel.count(p)) g_sel.erase(p); else g_sel.insert(p);
        g_selAnchor = i;
    }
    g_selMode = true; g.cur = i; inval();
}
static void batchRotate(int d) {
    std::vector<int> idx = selIdx();
    for (int i : idx) {
        Item& it = g.items[i]; it.rot = (it.rot + d + 4) & 3;
        WS k = lower(it.path); if (it.rot) g_store.rot[k] = it.rot; else g_store.rot.erase(k);
    }
    g_store.saveRot();
    for (int i : idx) queueBake(i, 1.2);
    inval();
}
static void batchDelete() {
    std::vector<int> idx = selIdx(); if (idx.empty()) return;
    WS from;
    for (int i : idx) { from += g.items[i].path; from.push_back(0); }
    from.push_back(0);
    SHFILEOPSTRUCTW op = {}; op.hwnd = g.hwnd; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_ALLOWUNDO;
    SHFileOperationW(&op);
    for (int i : idx) if (!fileExists(g.items[i].path)) g.thumbs.erase(g.items[i].path);
    rescanKeep();
    if (g_sel.empty()) clearSel();
}
static const wchar_t* presetExt(int id) {
    switch (id) { case I_PNG: return L"png"; case I_WEBP: return L"webp"; case I_TIFF: return L"tif"; case V_PHONE: case V_1080: case V_ORIG: case V_HEVC: case V_SIZE: return L"mp4";
        case V_GIF: return L"gif"; case V_MP3: return L"mp3"; case V_COPY: return L""; }
    return L"jpg";
}
static void startBatch() {
    if (g.exporting) return;
    std::vector<int> idx = selIdx(); if (idx.empty()) return;
    bool ff = !findFfmpeg().empty(); int skipped = 0;
    Batch* b = new Batch; std::unordered_set<WS> used;
    for (int i : idx) {
        Item& it = g.items[i]; const Preset& p = it.video ? kVidPresets[g.presetVid] : kImgPresets[g.presetImg];
        if (p.needFF && !ff) { skipped++; continue; }
        WS dir = (!g.outDir.empty() && dirExists(g.outDir)) ? g.outDir : dirOf(it.path);
        WS e = presetExt(p.id); if (e.empty()) { e = extOf(it.path); if (e.empty()) e = L"mp4"; }
        WS stem = stemOf(it.path), dst = uniquePath(dir, stem, e);
        for (int k = 2; used.count(lower(dst)) && k < 10000; k++) dst = uniquePath(dir, stem + fmtW(L" (%d)", k), e);
        used.insert(lower(dst));
        Job* j = new Job; j->src = it.path; j->dst = dst; j->preset = p.id; j->video = it.video; j->rot = it.rot; j->targetMB = g_targetMB;
        b->jobs.push_back(j);
    }
    if (b->jobs.empty()) { delete b; showToast(L"Нужен ffmpeg.exe в папке tools рядом с программой"); return; }
    InterlockedExchange(&g_batchN, (LONG)b->jobs.size()); InterlockedExchange(&g_batchIdx, 0); InterlockedExchange(&g_jobCancel, 0);
    g.exporting = true; g.progress = 0;
    CloseHandle(CreateThread(nullptr, 0, batchThread, b, 0, nullptr));
    g_store.putInt(L"preset_img", g.presetImg); g_store.putInt(L"preset_vid", g.presetVid); g_store.saveSet();
    if (skipped) showToast(fmtW(L"Без ffmpeg пропущено файлов: %d", skipped));
    inval();
}

// ---------------------------------------------------------------- clipboard and drag-out
static void copyFiles(const std::vector<WS>& paths) {
    if (paths.empty()) return;
    size_t chars = 1; for (auto& p : paths) chars += p.size() + 1;
    HGLOBAL h = GlobalAlloc(GHND, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!h) return;
    DROPFILES* df = (DROPFILES*)GlobalLock(h); df->pFiles = sizeof(DROPFILES); df->fWide = TRUE;
    wchar_t* w = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
    for (auto& p : paths) { memcpy(w, p.c_str(), p.size() * sizeof(wchar_t)); w += p.size() + 1; }
    GlobalUnlock(h);
    if (OpenClipboard(g.hwnd)) { EmptyClipboard(); if (!SetClipboardData(CF_HDROP, h)) GlobalFree(h); CloseClipboard(); showToast(paths.size() == 1 ? WS(L"Файл скопирован") : fmtW(L"Скопировано файлов: %d", (int)paths.size())); }
    else GlobalFree(h);
}
static const GUID kBHID_DataObject = { 0xb8c0bd9f, 0xed24, 0x455c, { 0x83, 0xe6, 0xd5, 0x39, 0x0c, 0x4f, 0xe8, 0xc4 } };
static void dragOutFiles(const std::vector<WS>& paths) {   // drag the file(s) into another program: Explorer, a messenger, an editor
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (auto& p : paths) { PIDLIST_ABSOLUTE id = ILCreateFromPathW(p.c_str()); if (id) ids.push_back(id); }
    if (!ids.empty()) {
        Com<IShellItemArray> arr; Com<IDataObject> dobj;
        if (SUCCEEDED(SHCreateShellItemArrayFromIDLists((UINT)ids.size(), (PCIDLIST_ABSOLUTE_ARRAY)ids.data(), &arr)) && SUCCEEDED(arr->BindToHandler(nullptr, kBHID_DataObject, IID_PPV_ARGS(&dobj)))) {
            DWORD eff = 0; g_dragOut = true;
            SHDoDragDrop(g.hwnd, dobj, nullptr, DROPEFFECT_COPY, &eff);
            g_dragOut = false; g_dragEnd = nowSec();
        }
    }
    for (auto id : ids) ILFree(id);
    g.pressed = -1; g.panning = false; g.lastMouse = nowSec(); inval();
}

// ---------------------------------------------------------------- save the current video frame as a photo
static void saveFrame() {
    if (!g.vtex.p || !g.vHasFrame || g.cur < 0 || !g.items[g.cur].video) return;
    Item& it = g.items[g.cur];
    D3D11_TEXTURE2D_DESC d; g.vtex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    Com<ID3D11Texture2D> stg; if (FAILED(g.d3d->CreateTexture2D(&d, nullptr, &stg))) return;
    g.d3dctx->CopyResource(stg, g.vtex);
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g.d3dctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) return;
    UINT W = d.Width, H = d.Height, rot = it.rot & 3, ow = (rot & 1) ? H : W, oh = (rot & 1) ? W : H;
    std::vector<UINT32> out((size_t)ow * oh); const BYTE* p = (const BYTE*)m.pData;
    for (UINT y = 0; y < oh; y++) for (UINT x = 0; x < ow; x++) {
        UINT sx, sy;
        if (rot == 0) { sx = x; sy = y; } else if (rot == 1) { sx = y; sy = H - 1 - x; } else if (rot == 2) { sx = W - 1 - x; sy = H - 1 - y; } else { sx = W - 1 - y; sy = x; }
        out[(size_t)y * ow + x] = *(const UINT32*)(p + (size_t)sy * m.RowPitch + (size_t)sx * 4) | 0xFF000000;
    }
    g.d3dctx->Unmap(stg, 0);
    int t = (int)videoTime();
    WS dir = (!g.outDir.empty() && dirExists(g.outDir)) ? g.outDir : dirOf(it.path);
    WS dst = uniquePath(dir, stemOf(it.path) + fmtW(L" %d-%02d", t / 60, t % 60), L"jpg");
    Com<IWICImagingFactory> f; Com<IWICBitmap> b;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateBitmapFromMemory(ow, oh, GUID_WICPixelFormat32bppBGR, ow * 4, (UINT)(out.size() * 4), (BYTE*)out.data(), &b)) &&
        wicSave(f, b, ow, oh, dst, GUID_ContainerFormatJpeg, GUID_WICPixelFormat24bppBGR, 0.93f)) showToast(L"Кадр сохранён: " + nameOf(dst), dst);
    else showToast(L"Не удалось сохранить кадр");
}

// ---------------------------------------------------------------- RTX Video Super Resolution (only on NVIDIA RTX adapters; any failure switches it off for the session)
static const GUID kNvidiaPPE = { 0xd43ce1b3, 0x1f4b, 0x48ac, { 0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7 } };
static int g_vsrCap = -1; static bool g_vsrFailed = false, g_vsrReady = false;
static Com<ID3D11VideoDevice> g_vdev; static Com<ID3D11VideoContext> g_vctx; static Com<ID3D11VideoProcessorEnumerator> g_vpe; static Com<ID3D11VideoProcessor> g_vp;
static Com<ID3D11VideoProcessorInputView> g_vin; static Com<ID3D11VideoProcessorOutputView> g_vout; static Com<ID3D11Texture2D> g_vsrTex; static Com<ID2D1Bitmap1> g_vsrBmp;
static int g_vsrW = 0, g_vsrH = 0; static ID3D11Texture2D* g_vsrSrc = nullptr;
static void vsrReset() {
    g_vsrReady = false; g_vsrBmp.reset(); g_vout.reset(); g_vin.reset(); g_vp.reset(); g_vpe.reset(); g_vsrTex.reset(); g_vctx.reset(); g_vdev.reset();
    g_vsrW = g_vsrH = 0; g_vsrSrc = nullptr;
}
static bool vsrCreate(int ow, int oh) {
    vsrReset();
    if (FAILED(g.d3d->QueryInterface(IID_PPV_ARGS(&g_vdev))) || FAILED(g.d3dctx->QueryInterface(IID_PPV_ARGS(&g_vctx)))) return false;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd = {};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE; cd.InputWidth = g.vw; cd.InputHeight = g.vh; cd.OutputWidth = ow; cd.OutputHeight = oh;
    cd.InputFrameRate.Numerator = 60; cd.InputFrameRate.Denominator = 1; cd.OutputFrameRate = cd.InputFrameRate; cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (FAILED(g_vdev->CreateVideoProcessorEnumerator(&cd, &g_vpe)) || FAILED(g_vdev->CreateVideoProcessor(g_vpe, 0, &g_vp))) return false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = ow; td.Height = oh; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g.d3d->CreateTexture2D(&td, nullptr, &g_vsrTex))) return false;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od = {}; od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC id = {}; id.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    if (FAILED(g_vdev->CreateVideoProcessorOutputView(g_vsrTex, g_vpe, &od, &g_vout)) || FAILED(g_vdev->CreateVideoProcessorInputView(g.vtex, g_vpe, &id, &g_vin))) return false;
    struct { UINT version, method, enable; } ext = { 1, 2, 1 };   // NVIDIA stream extension v1, method 2 = RTX VSR
    if (FAILED(g_vctx->VideoProcessorSetStreamExtension(g_vp, 0, &kNvidiaPPE, sizeof(ext), &ext))) return false;
    g_vctx->VideoProcessorSetStreamFrameFormat(g_vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    Com<IDXGISurface> surf;
    if (FAILED(g_vsrTex->QueryInterface(IID_PPV_ARGS(&surf)))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
    if (FAILED(g.ctx->CreateBitmapFromDxgiSurface(surf, &bp, &g_vsrBmp))) return false;
    g_vsrW = ow; g_vsrH = oh; g_vsrSrc = g.vtex.p;
    return true;
}
static void vsrPrepare() {   // before drawing: upscale the current frame to its on-screen size through the driver
    g_vsrReady = false;
    if (!g_enh || g_vsrFailed || g.mode != M_VIEW || !g.vtex.p || !g.vHasFrame) return;
    if (g_vsrCap < 0) {
        g_vsrCap = 0;
        Com<IDXGIDevice> dx; Com<IDXGIAdapter> ad; DXGI_ADAPTER_DESC ds = {};
        if (SUCCEEDED(g.d3d->QueryInterface(IID_PPV_ARGS(&dx))) && SUCCEEDED(dx->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&ds)) && ds.VendorId == 0x10DE && wcsstr(ds.Description, L"RTX")) g_vsrCap = 1;
    }
    if (g_vsrCap != 1) return;
    Cur c; if (!getCur(c) || c.bmp != g.vbmp.p) return;
    ML m; layoutMedia(c, m, false);
    float up = m.s * g.dpi / 96.0f; if (up < 1.1f) return;
    up = std::min(up, std::min(3840.0f / g.vw, 2160.0f / g.vh)); if (up < 1.1f) return;
    int ow = ((int)(g.vw * up + 0.5f)) & ~1, oh = ((int)(g.vh * up + 0.5f)) & ~1;
    if (!g_vp.p || g_vsrSrc != g.vtex.p || abs(ow - g_vsrW) > 1 || abs(oh - g_vsrH) > 1) { if (!vsrCreate(ow, oh)) { g_vsrFailed = true; vsrReset(); return; } }
    D3D11_VIDEO_PROCESSOR_STREAM st = {}; st.Enable = TRUE; st.pInputSurface = g_vin;
    if (FAILED(g_vctx->VideoProcessorBlt(g_vp, g_vout, 0, 1, &st))) { g_vsrFailed = true; vsrReset(); return; }
    g_vsrReady = true;
}

// ---------------------------------------------------------------- sorting into subfolders: move the file(s) into a folder inside the current one
static std::vector<WS> g_moveDirs; static WS g_lastMoveDir; static std::vector<std::pair<WS, WS>> g_lastMoves;   // (where it is now, where it was)
static bool g_nameOpen = false; static WS g_nameText; static std::vector<WS> g_namePaths;
static void scanSubdirs() {
    g_moveDirs.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((g.folder + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        g_moveDirs.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd) && g_moveDirs.size() < 400);
    FindClose(h);
    std::sort(g_moveDirs.begin(), g_moveDirs.end(), [](const WS& a, const WS& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
}
static bool moveOne(const WS& src, const WS& dir, WS& dst) {
    dst = dir + L"\\" + nameOf(src);
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) { WS e = extOf(src); dst = uniquePath(dir, stemOf(src), e.empty() ? WS(L"bin") : e); }   // never overwrite
    for (int i = 0; i < 12; i++) {   // a video may stay locked for a moment after the player lets go of it
        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED)) return true;
        DWORD er = GetLastError(); if (er != ERROR_SHARING_VIOLATION && er != ERROR_ACCESS_DENIED && er != ERROR_LOCK_VIOLATION) return false;
        Sleep(150);
    }
    return false;
}
static void forgetPath(const WS& p) {
    g.thumbs.erase(p);
    for (size_t i = 0; i < g.icache.size(); i++) if (g.icache[i].path == p) { g.icache.erase(g.icache.begin() + i); break; }
}
static void moveTo(const std::vector<WS>& paths, const WS& dir) {
    if (paths.empty()) return;
    if (!dirExists(dir)) { showToast(L"Папка не найдена"); return; }
    bool view = g.mode == M_VIEW; int keep = g.cur;
    if (view) { closeVideo(); g.img.reset(); g.animSrc.clear(); g.animBmps.clear(); g.animDelays.clear(); g.animTotal = 0; }
    std::vector<std::pair<WS, WS>> done; bool rotChanged = false;
    for (auto& p : paths) {
        WS dst;
        if (!moveOne(p, dir, dst)) continue;
        done.push_back({ dst, p });
        WS k = lower(p); auto it = g_store.rot.find(k);   // an in-app rotation follows the file
        if (it != g_store.rot.end()) { g_store.rot[lower(dst)] = it->second; g_store.rot.erase(k); rotChanged = true; }
        forgetPath(p);
    }
    if (rotChanged) g_store.saveRot();
    if (!done.empty()) { g_lastMoves = done; g_lastMoveDir = dir; }
    if (view) {
        for (auto& d : done) for (size_t i = 0; i < g.items.size(); i++) if (g.items[i].path == d.second) { g.items.erase(g.items.begin() + i); break; }
        g.cur = -1;
        if (g.items.empty()) { updateTitle(); inval(); } else go(std::max(0, std::min((int)g.items.size() - 1, keep)));
    } else { rescanKeep(); clearSel(); }
    if (done.empty()) showToast(L"Не удалось переместить: файл занят или нет доступа");
    else if (done.size() == 1) showToast(L"Перемещено в «" + nameOf(dir) + L"». Вернуть: Ctrl+Z", done[0].first);
    else showToast(fmtW(L"Перемещено файлов: %d из %d. Вернуть: Ctrl+Z", (int)done.size(), (int)paths.size()), dir);
}
static void undoMove() {
    if (g_lastMoves.empty()) { showToast(L"Нечего возвращать"); return; }
    int ok = 0; WS first; bool rotChanged = false;
    for (auto& m : g_lastMoves) {
        if (GetFileAttributesW(m.second.c_str()) != INVALID_FILE_ATTRIBUTES || !MoveFileExW(m.first.c_str(), m.second.c_str(), MOVEFILE_COPY_ALLOWED)) continue;
        ok++; if (first.empty()) first = m.second;
        WS k = lower(m.first); auto it = g_store.rot.find(k);
        if (it != g_store.rot.end()) { g_store.rot[lower(m.second)] = it->second; g_store.rot.erase(k); rotChanged = true; }
        forgetPath(m.first);
    }
    if (rotChanged) g_store.saveRot();
    g_lastMoves.clear();
    if (!ok) { showToast(L"Не удалось вернуть файлы"); return; }
    if (g.mode == M_VIEW && !first.empty() && !lstrcmpiW(dirOf(first).c_str(), g.folder.c_str())) openPath(first); else rescanKeep();
    showToast(fmtW(L"Возвращено файлов: %d", ok));
}
static void nameConfirm() {
    WS name = g_nameText;
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(name.begin());
    if (name.empty()) return;
    WS dir = g.folder + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { showToast(L"Не удалось создать папку с таким именем"); return; }
    g_nameOpen = false;
    std::vector<WS> paths = g_namePaths; g_namePaths.clear();
    moveTo(paths, dir);
}
static void nameChar(wchar_t c) {
    if (c == L'\r') { nameConfirm(); return; }
    if (c == 27) { g_nameOpen = false; return; }
    if (c == L'\b') { if (!g_nameText.empty()) g_nameText.pop_back(); return; }
    if (c < 32 || wcschr(L"\\/:*?\"<>|", c) || g_nameText.size() >= 120) return;
    g_nameText.push_back(c);
}
