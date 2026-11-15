// Graphics device, image decoding, thumbnails and video playback.

enum { WM_APP_IMG = WM_APP + 1, WM_APP_THUMB, WM_APP_MEDIA, WM_APP_EXPORT, WM_APP_HW };

struct Item { WS path, name; bool video; int rot; FILETIME mtime; };
struct Btn { int id; D2D1_RECT_F r; };
struct ImgEnt { WS path; Com<IWICBitmap> bmp; int exifRot = 0; UINT w = 0, h = 0; std::vector<Com<IWICBitmap>> frames; std::vector<int> delays; };
struct ThumbEnt { Com<ID2D1Bitmap1> bmp; int w = 0, h = 0; double used = 0; bool failed = false; };
enum Mode { M_VIEW, M_GALLERY };

struct App {
    HWND hwnd = nullptr; float dpi = 96; int pw = 0, ph = 0;
    Com<ID3D11Device> d3d; Com<ID3D11DeviceContext> d3dctx;
    Com<ID2D1Factory1> d2dFactory; Com<ID2D1Device> d2dDev; Com<ID2D1DeviceContext> ctx;
    Com<IDXGISwapChain1> swap; Com<ID2D1Bitmap1> target; Com<ID2D1SolidColorBrush> br;
    Com<IDWriteFactory> dw; Com<IDWriteTextFormat> fUI, fBold, fSmall, fTitle, fIcon, fIconS, fIconBig, fCap;
    UINT maxBmp = 8192; bool occluded = false;

    std::vector<Item> items; int cur = -1; WS folder; Mode mode = M_VIEW;

    // current image
    Com<ID2D1Bitmap1> img; int imgW = 0, imgH = 0, baseRot = 0; bool imgIsThumb = false, imgFailed = false, imgLoading = false;
    std::vector<ImgEnt> icache;
    std::vector<Com<IWICBitmap>> animSrc; std::vector<Com<ID2D1Bitmap1>> animBmps; std::vector<int> animDelays; int animTotal = 0; double animT0 = 0;   // animated GIF
    float zoom = 1; D2D1_POINT_2F pan = { 0, 0 };
    double rotT0 = -10; float rotFromDeg = 0, rotFromScale = 1;

    // video
    bool mfStarted = false; Com<IMFDXGIDeviceManager> dxgiMgr; Com<IMFMediaEngine> eng; IMFMediaEngineNotify* notify = nullptr;
    Com<IMFByteStream> vstream; Com<ID3D11Texture2D> vtex; Com<ID2D1Bitmap1> vbmp; int vw = 0, vh = 0; int vgen = 0;
    bool vReady = false, vError = false, vHasFrame = false; double vDur = 0, vWake = 0; double volume = 1; bool muted = false;
    bool seeking = false; double seekPos = 0;

    // thumbs
    std::unordered_map<WS, ThumbEnt> thumbs; std::unordered_set<WS> thumbPending;

    // ui
    std::vector<Btn> btns; int hot = -1, pressed = -1; POINT mouse = { -1, -1 };
    double lastMouse = 0; float uiAlpha = 1; bool fullscreen = false; WINDOWPLACEMENT wpPrev = { sizeof(WINDOWPLACEMENT) };
    bool panning = false; POINT panStart = {}; D2D1_POINT_2F panOrig = {};
    bool cropMode = false; D2D1_RECT_F crop = { 0, 0, 1, 1 }; int cropRatio = 0, cropDrag = -1; D2D1_POINT_2F cropGrab = {}; D2D1_RECT_F cropOrig = {};
    bool exportOpen = false, exporting = false; float progress = 0; int presetImg = 1, presetVid = 0; WS outDir;
    bool depsOpen = false;
    WS toast, toastPath; double toastT = -100;
    float scroll = 0, scrollTarget = 0;
    double lastFrame = 0;
};
static App g;

static void inval() { if (g.hwnd) InvalidateRect(g.hwnd, nullptr, FALSE); }
static void showToast(const WS& t, const WS& path = L"") { g.toast = t; g.toastPath = path; g.toastT = nowSec(); inval(); }

// ---------------------------------------------------------------- device
static void makeTarget() {
    g.ctx->SetTarget(nullptr); g.target.reset();
    Com<IDXGISurface> surf;
    if (FAILED(g.swap->GetBuffer(0, IID_PPV_ARGS(&surf)))) return;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), g.dpi, g.dpi);
    g.ctx->CreateBitmapFromDxgiSurface(surf, &bp, &g.target);
    g.ctx->SetTarget(g.target); g.ctx->SetDpi(g.dpi, g.dpi);
}
static IDWriteTextFormat* makeFmt(const wchar_t* fam, float size, DWRITE_FONT_WEIGHT w, Com<IDWriteTextFormat>& out) {
    g.dw->CreateTextFormat(fam, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &out);
    if (out.p) {
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING tr = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        Com<IDWriteInlineObject> ell; g.dw->CreateEllipsisTrimmingSign(out, &ell);
        out->SetTrimming(&tr, ell);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return out;
}
static bool createDevice() {
    static const D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls, 4, D3D11_SDK_VERSION, &g.d3d, nullptr, &g.d3dctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls + 1, 3, D3D11_SDK_VERSION, &g.d3d, nullptr, &g.d3dctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls + 1, 3, D3D11_SDK_VERSION, &g.d3d, nullptr, &g.d3dctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls + 1, 3, D3D11_SDK_VERSION, &g.d3d, nullptr, &g.d3dctx);
    if (FAILED(hr)) return false;
    Com<ID3D10Multithread> mt;
    if (SUCCEEDED(g.d3d->QueryInterface(IID_PPV_ARGS(&mt)))) mt->SetMultithreadProtected(TRUE);
    Com<IDXGIDevice1> dxgi;
    if (FAILED(g.d3d->QueryInterface(IID_PPV_ARGS(&dxgi)))) return false;
    dxgi->SetMaximumFrameLatency(1);
    if (!g.d2dFactory.p) {
        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &fo, (void**)&g.d2dFactory))) return false;
    }
    if (FAILED(g.d2dFactory->CreateDevice(dxgi, &g.d2dDev))) return false;
    if (FAILED(g.d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g.ctx))) return false;
    g.maxBmp = std::min<UINT>(16384, g.ctx->GetMaximumBitmapSize());
    Com<IDXGIAdapter> ad; Com<IDXGIFactory2> fac;
    if (FAILED(dxgi->GetAdapter(&ad)) || FAILED(ad->GetParent(IID_PPV_ARGS(&fac)))) return false;
    DXGI_SWAP_CHAIN_DESC1 d = {};
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; d.Scaling = DXGI_SCALING_NONE; d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = fac->CreateSwapChainForHwnd(g.d3d, g.hwnd, &d, nullptr, nullptr, &g.swap);
    if (FAILED(hr)) { d.Scaling = DXGI_SCALING_STRETCH; hr = fac->CreateSwapChainForHwnd(g.d3d, g.hwnd, &d, nullptr, nullptr, &g.swap); }
    if (FAILED(hr)) return false;
    fac->MakeWindowAssociation(g.hwnd, DXGI_MWA_NO_ALT_ENTER);
    makeTarget();
    g.ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &g.br);
    g.ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (!g.dw.p) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g.dw);
        makeFmt(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, g.fUI);
        makeFmt(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, g.fBold);
        makeFmt(L"Segoe UI", 11.5f, DWRITE_FONT_WEIGHT_NORMAL, g.fSmall);
        makeFmt(L"Segoe UI", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, g.fTitle);
        makeFmt(L"Segoe MDL2 Assets", 16.0f, DWRITE_FONT_WEIGHT_NORMAL, g.fIcon);
        makeFmt(L"Segoe MDL2 Assets", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, g.fIconS);
        makeFmt(L"Segoe MDL2 Assets", 10.0f, DWRITE_FONT_WEIGHT_NORMAL, g.fCap);
        makeFmt(L"Segoe MDL2 Assets", 44.0f, DWRITE_FONT_WEIGHT_NORMAL, g.fIconBig);
    }
    return true;
}
static void resizeSwap() {
    if (!g.swap.p || g.pw <= 0 || g.ph <= 0) return;
    g.ctx->SetTarget(nullptr); g.target.reset();
    g.swap->ResizeBuffers(0, g.pw, g.ph, DXGI_FORMAT_UNKNOWN, 0);
    makeTarget();
}

// ---------------------------------------------------------------- image decoding (worker threads)
struct ImgRes { WS path; Com<IWICBitmap> bmp; int exifRot = 0; UINT w = 0, h = 0; std::vector<Com<IWICBitmap>> frames; std::vector<int> delays; };
static Lock g_imgLock; static CONDITION_VARIABLE g_imgCv = CONDITION_VARIABLE_INIT;
static std::deque<WS> g_imgQ; static std::unordered_set<WS> g_imgBusy;

static int exifToRot(int o) { switch (o) { case 3: case 4: return 2; case 5: case 6: return 1; case 7: case 8: return 3; } return 0; }

static bool wicDecode(IWICImagingFactory* f, IWICBitmapDecoder* dec, ImgRes* r) {
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    GUID cf = {}; dec->GetContainerFormat(&cf);
    if (cf == GUID_ContainerFormatJpeg || cf == GUID_ContainerFormatTiff) {
        Com<IWICMetadataQueryReader> mq;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&mq))) {
            const wchar_t* names[] = { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}" };
            for (auto n : names) {
                PROPVARIANT v; PropVariantInit(&v);
                if (SUCCEEDED(mq->GetMetadataByName(n, &v))) {
                    if (v.vt == VT_UI2) { r->exifRot = exifToRot(v.uiVal); PropVariantClear(&v); break; }
                    PropVariantClear(&v);
                }
            }
        }
    }
    UINT w = 0, h = 0; if (FAILED(frame->GetSize(&w, &h)) || !w || !h) return false;
    Com<IWICBitmapSource> src((IWICBitmapSource*)frame.p);
    UINT cap = g.maxBmp; double sc = 1.0;
    if (w > cap || h > cap) sc = std::min((double)cap / w, (double)cap / h);
    double px = (double)w * h * sc * sc; if (px > 160e6) sc *= sqrt(160e6 / px);
    if (sc < 1.0) {
        Com<IWICBitmapScaler> s;
        if (SUCCEEDED(f->CreateBitmapScaler(&s)) && SUCCEEDED(s->Initialize(src, std::max(1u, (UINT)(w * sc)), std::max(1u, (UINT)(h * sc)), WICBitmapInterpolationModeFant))) {
            w = std::max(1u, (UINT)(w * sc)); h = std::max(1u, (UINT)(h * sc)); src = Com<IWICBitmapSource>((IWICBitmapSource*)s.p);
        }
    }
    Com<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    if (FAILED(f->CreateBitmapFromSource(conv, WICBitmapCacheOnLoad, &r->bmp))) return false;
    r->w = w; r->h = h;
    return true;
}
static UINT metaUInt(IWICMetadataQueryReader* q, const wchar_t* name) {
    PROPVARIANT v; PropVariantInit(&v); UINT r = 0;
    if (SUCCEEDED(q->GetMetadataByName(name, &v))) { if (v.vt == VT_UI2) r = v.uiVal; else if (v.vt == VT_UI1) r = v.bVal; else if (v.vt == VT_UI4) r = v.ulVal; PropVariantClear(&v); }
    return r;
}
// Animated GIF: every frame is composed onto the full canvas (offsets and disposal modes applied), so playback is just a bitmap swap.
static bool decodeGif(IWICImagingFactory* f, IWICBitmapDecoder* dec, ImgRes* r) {
    UINT n = 0; dec->GetFrameCount(&n); if (n < 2) return false;
    UINT W = 0, H = 0; Com<IWICMetadataQueryReader> gq;
    if (SUCCEEDED(dec->GetMetadataQueryReader(&gq))) { W = metaUInt(gq, L"/logscr/Width"); H = metaUInt(gq, L"/logscr/Height"); }
    if (!W || !H) { Com<IWICBitmapFrameDecode> f0; if (FAILED(dec->GetFrame(0, &f0)) || FAILED(f0->GetSize(&W, &H))) return false; }
    if (!W || !H || W > 4096 || H > 4096) return false;
    size_t per = (size_t)W * H * 4;
    UINT maxN = (UINT)std::min<size_t>(std::min<UINT>(n, 1200), (size_t)300000000 / per);
    if (maxN < 2) return false;
    std::vector<UINT32> canvas((size_t)W * H, 0), saved;
    UINT pl = 0, pt = 0, pw = 0, ph = 0, pdisp = 0;
    for (UINT i = 0; i < maxN; i++) {
        Com<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(i, &fr))) break;
        UINT l = 0, t = 0, delay = 0, disp = 0; Com<IWICMetadataQueryReader> mq;
        if (SUCCEEDED(fr->GetMetadataQueryReader(&mq))) { l = metaUInt(mq, L"/imgdesc/Left"); t = metaUInt(mq, L"/imgdesc/Top"); delay = metaUInt(mq, L"/grctlext/Delay"); disp = metaUInt(mq, L"/grctlext/Disposal"); }
        if (i > 0) {
            if (pdisp == 2) { for (UINT y = pt; y < std::min(H, pt + ph); y++) for (UINT x = pl; x < std::min(W, pl + pw); x++) canvas[(size_t)y * W + x] = 0; }
            else if (pdisp == 3 && !saved.empty()) canvas = saved;
        }
        if (disp == 3) saved = canvas;
        UINT fw = 0, fh = 0; if (FAILED(fr->GetSize(&fw, &fh)) || !fw || !fh) break;
        Com<IWICFormatConverter> conv;
        if (FAILED(f->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) break;
        std::vector<UINT32> px((size_t)fw * fh);
        if (FAILED(conv->CopyPixels(nullptr, fw * 4, (UINT)(px.size() * 4), (BYTE*)px.data()))) break;
        for (UINT y = 0; y < fh && t + y < H; y++) { const UINT32* sp = &px[(size_t)y * fw]; UINT32* dp = &canvas[(size_t)(t + y) * W]; for (UINT x = 0; x < fw && l + x < W; x++) if (sp[x] >> 24) dp[l + x] = sp[x]; }
        Com<IWICBitmap> b;
        if (FAILED(f->CreateBitmapFromMemory(W, H, GUID_WICPixelFormat32bppPBGRA, W * 4, (UINT)per, (BYTE*)canvas.data(), &b))) break;
        r->frames.push_back(b); r->delays.push_back(delay < 2 ? 100 : (int)delay * 10);
        pl = l; pt = t; pw = fw; ph = fh; pdisp = disp;
    }
    if (r->frames.size() < 2) { r->frames.clear(); r->delays.clear(); return false; }
    r->bmp = r->frames[0]; r->w = W; r->h = H;
    return true;
}
static bool decodeFile(IWICImagingFactory* f, const WS& path, ImgRes* r) {
    Com<IWICBitmapDecoder> dec;
    if (SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        GUID cf = {}; dec->GetContainerFormat(&cf);
        if (cf == GUID_ContainerFormatGif && decodeGif(f, dec, r)) return true;
        r->frames.clear(); r->delays.clear();
        if (wicDecode(f, dec, r)) return true;
    }
    r->bmp.reset(); r->exifRot = 0;
    WS ff = findFfmpeg();
    if (ff.empty()) return false;
    std::string data;
    WS cmd = q(ff) + L" -v error -nostdin -i " + q(path) + L" -frames:v 1 -an -f image2pipe -c:v bmp -";
    runCapture(cmd, data, 0);
    if (data.size() < 64) return false;
    Com<IWICStream> st;
    if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size()))) return false;
    Com<IWICBitmapDecoder> d2;
    if (FAILED(f->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &d2))) return false;
    return wicDecode(f, d2, r);
}
static DWORD WINAPI imgWorker(void*) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    for (;;) {
        WS path;
        g_imgLock.lock();
        while (g_imgQ.empty()) SleepConditionVariableSRW(&g_imgCv, &g_imgLock.l, INFINITE, 0);
        path = g_imgQ.front(); g_imgQ.pop_front(); g_imgBusy.insert(path);
        g_imgLock.unlock();
        ImgRes* r = new ImgRes; r->path = path;
        if (f.p) decodeFile(f, path, r);
        g_imgLock.lock(); g_imgBusy.erase(path); g_imgLock.unlock();
        if (!PostMessageW(g.hwnd, WM_APP_IMG, 0, (LPARAM)r)) delete r;
    }
    return 0;
}
static ImgEnt* cachedImage(const WS& path) {
    for (auto& e : g.icache) if (e.path == path) return &e;
    return nullptr;
}
static void requestImage(const WS& path, bool front) {
    if (cachedImage(path)) return;
    Guard gd(g_imgLock);
    if (g_imgBusy.count(path)) return;
    auto it = std::find(g_imgQ.begin(), g_imgQ.end(), path);
    if (it != g_imgQ.end()) { if (!front) return; g_imgQ.erase(it); }
    if (front) g_imgQ.push_front(path); else g_imgQ.push_back(path);
    WakeConditionVariable(&g_imgCv);
}
static void clearImageQueue() { Guard gd(g_imgLock); g_imgQ.clear(); }

// ---------------------------------------------------------------- thumbnails (shell cache, worker threads)
struct ThumbRes { WS path; int w = 0, h = 0; std::vector<BYTE> px; bool dropped = false; };
static Lock g_thLock; static CONDITION_VARIABLE g_thCv = CONDITION_VARIABLE_INIT; static std::vector<WS> g_thQ;
static const int kThumbPx = 256;   // matches the size Explorer already keeps in its thumbnail cache

static DWORD WINAPI thumbWorker(void*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        WS path, drop;
        g_thLock.lock();
        while (g_thQ.empty()) SleepConditionVariableSRW(&g_thCv, &g_thLock.l, INFINITE, 0);
        path = g_thQ.back(); g_thQ.pop_back();
        if (g_thQ.size() > 400) { drop = g_thQ.front(); g_thQ.erase(g_thQ.begin()); }
        g_thLock.unlock();
        if (!drop.empty()) { ThumbRes* d = new ThumbRes; d->path = drop; d->dropped = true; if (!PostMessageW(g.hwnd, WM_APP_THUMB, 0, (LPARAM)d)) delete d; }
        ThumbRes* r = new ThumbRes; r->path = path;
        Com<IShellItemImageFactory> fac;
        if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&fac)))) {
            SIZE sz = { kThumbPx, kThumbPx }; HBITMAP hb = nullptr;
            if (SUCCEEDED(fac->GetImage(sz, SIIGBF_THUMBNAILONLY, &hb)) && hb) {
                BITMAP bm = {}; GetObjectW(hb, sizeof(bm), &bm);
                if (bm.bmWidth > 0 && bm.bmHeight > 0) {
                    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = bm.bmWidth; bi.bmiHeader.biHeight = -bm.bmHeight;
                    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
                    r->px.resize((size_t)bm.bmWidth * bm.bmHeight * 4);
                    HDC dc = GetDC(nullptr);
                    if (GetDIBits(dc, hb, 0, bm.bmHeight, r->px.data(), &bi, DIB_RGB_COLORS)) {
                        r->w = bm.bmWidth; r->h = bm.bmHeight;
                        bool anyA = false; size_t n = r->px.size();
                        for (size_t i = 3; i < n; i += 4) if (r->px[i]) { anyA = true; break; }
                        if (!anyA) for (size_t i = 3; i < n; i += 4) r->px[i] = 255;
                    }
                    ReleaseDC(nullptr, dc);
                }
                DeleteObject(hb);
            }
        }
        if (!PostMessageW(g.hwnd, WM_APP_THUMB, 0, (LPARAM)r)) delete r;
    }
    return 0;
}
static ThumbEnt* getThumb(const WS& path, bool request) {
    auto it = g.thumbs.find(path);
    if (it != g.thumbs.end()) { it->second.used = g.lastFrame; return &it->second; }
    if (request && !g.thumbPending.count(path)) {
        g.thumbPending.insert(path);
        Guard gd(g_thLock); g_thQ.push_back(path); WakeConditionVariable(&g_thCv);
    }
    return nullptr;
}
static void onThumb(ThumbRes* r) {
    g.thumbPending.erase(r->path);
    if (!r->dropped) {
        ThumbEnt e; e.used = nowSec();
        if (r->w > 0 && g.ctx.p) {
            D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
            g.ctx->CreateBitmap(D2D1::SizeU(r->w, r->h), r->px.data(), r->w * 4, &bp, &e.bmp);
            e.w = r->w; e.h = r->h;
        }
        e.failed = !e.bmp.p;
        if (g.thumbs.size() > 700) {   // drop the least recently used third
            std::vector<std::pair<double, WS>> v; v.reserve(g.thumbs.size());
            for (auto& kv : g.thumbs) v.push_back({ kv.second.used, kv.first });
            std::sort(v.begin(), v.end());
            for (size_t i = 0; i < v.size() / 3; i++) g.thumbs.erase(v[i].second);
        }
        g.thumbs[r->path] = std::move(e);
    }
    delete r;
}

// ---------------------------------------------------------------- video (Media Foundation Media Engine, frames into a D3D texture)
struct EngNotify : IMFMediaEngineNotify {
    LONG rc = 1; int gen;
    EngNotify(int g_) : gen(g_) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) { *ppv = (IMFMediaEngineNotify*)this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&rc); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&rc); if (!r) delete this; return r; }
    STDMETHODIMP EventNotify(DWORD ev, DWORD_PTR, DWORD) override { PostMessageW(g.hwnd, WM_APP_MEDIA, ev, gen); return S_OK; }
};
static const GUID kIID_MediaEngineEx = { 0x83015ead, 0xb1e6, 0x40d0, { 0xa9, 0x8a, 0x37, 0x14, 0x5f, 0xfe, 0x1a, 0xd1 } };
struct IMFMediaEngineExLite : IMFMediaEngine {   // first method of IMFMediaEngineEx (not declared in the mingw headers)
    virtual HRESULT STDMETHODCALLTYPE SetSourceFromByteStream(IMFByteStream* stream, BSTR url) = 0;
};
static void closeVideo() {
    g.vgen++;
    if (g.eng.p) { g.eng->Shutdown(); g.eng.reset(); }
    g.vstream.reset();
    g.vbmp.reset(); g.vtex.reset(); g.vw = g.vh = 0;
    g.vReady = g.vError = g.vHasFrame = false; g.vDur = 0; g.seeking = false;
}
static bool openVideo(const WS& path) {
    closeVideo();
    if (!g.mfStarted) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) { g.vError = true; return false; }
        g.mfStarted = true;
    }
    if (!g.dxgiMgr.p) {
        UINT tok = 0;
        if (SUCCEEDED(MFCreateDXGIDeviceManager(&tok, &g.dxgiMgr))) g.dxgiMgr->ResetDevice(g.d3d, tok);
    }
    Com<IMFMediaEngineClassFactory> cf; Com<IMFAttributes> at;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cf))) || FAILED(MFCreateAttributes(&at, 3))) { g.vError = true; return false; }
    EngNotify* n = new EngNotify(g.vgen);
    at->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, n);
    if (g.dxgiMgr.p) at->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, g.dxgiMgr);
    at->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    HRESULT hr = cf->CreateInstance(0, at, &g.eng);
    n->Release();
    if (FAILED(hr)) { g.vError = true; return false; }
    BSTR b = SysAllocString(path.c_str());
    g.eng->SetVolume(g.volume); g.eng->SetMuted(g.muted);
    // Preferred: hand the engine an open byte stream, so odd characters in the path are never parsed as a URL.
    hr = E_FAIL;
    IMFMediaEngineExLite* ex = nullptr;
    if (SUCCEEDED(g.eng->QueryInterface(kIID_MediaEngineEx, (void**)&ex)) && ex) {
        if (SUCCEEDED(MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, path.c_str(), &g.vstream)))
            hr = ex->SetSourceFromByteStream(g.vstream, b);
        ex->Release();
    }
    if (FAILED(hr)) { g.vstream.reset(); hr = g.eng->SetSource(b); }
    SysFreeString(b);
    if (FAILED(hr)) { g.vError = true; return false; }
    g.eng->Play();
    g.vWake = nowSec() + 2.0;
    return true;
}
static void videoMakeTexture() {
    DWORD cx = 0, cy = 0;
    if (!g.eng.p || FAILED(g.eng->GetNativeVideoSize(&cx, &cy)) || !cx || !cy) return;
    if ((int)cx == g.vw && (int)cy == g.vh && g.vtex.p) return;
    g.vbmp.reset(); g.vtex.reset(); g.vHasFrame = false;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = cx; td.Height = cy; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g.d3d->CreateTexture2D(&td, nullptr, &g.vtex))) return;
    Com<IDXGISurface> surf;
    if (FAILED(g.vtex->QueryInterface(IID_PPV_ARGS(&surf)))) { g.vtex.reset(); return; }
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
    if (FAILED(g.ctx->CreateBitmapFromDxgiSurface(surf, &bp, &g.vbmp))) { g.vtex.reset(); return; }
    g.vw = (int)cx; g.vh = (int)cy;
}
static void videoPump() {   // called before each frame: pulls a new video frame when one is due
    if (!g.eng.p || !g.vtex.p) return;
    LONGLONG pts = 0;
    if (g.eng->OnVideoStreamTick(&pts) == S_OK) {
        RECT rc = { 0, 0, g.vw, g.vh }; MFVideoNormalizedRect nr = { 0, 0, 1, 1 }; MFARGB bk = { 0, 0, 0, 255 };
        if (SUCCEEDED(g.eng->TransferVideoFrame(g.vtex, &nr, &rc, &bk))) g.vHasFrame = true;
    }
}
static bool videoPlaying() { return g.eng.p && g.vReady && !g.eng->IsPaused() && !g.eng->IsEnded(); }
static bool videoBusy() { return g.eng.p && (videoPlaying() || g.eng->IsSeeking() || nowSec() < g.vWake); }
static double videoTime() { return g.eng.p ? g.eng->GetCurrentTime() : 0; }
static void videoSeek(double t) {
    if (!g.eng.p || !g.vReady) return;
    t = std::max(0.0, std::min(t, std::max(0.0, g.vDur - 0.05)));
    g.eng->SetCurrentTime(t); g.vWake = nowSec() + 1.0;
}
static void videoToggle() {
    if (!g.eng.p || !g.vReady) return;
    if (g.eng->IsEnded()) { g.eng->SetCurrentTime(0); g.eng->Play(); }
    else if (g.eng->IsPaused()) g.eng->Play(); else g.eng->Pause();
    g.vWake = nowSec() + 0.5; inval();
}
