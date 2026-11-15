// Conversion / export. Images go through WIC natively; video goes through ffmpeg.exe with hardware encoders when present.

enum { I_JPEG_MAX, I_JPEG_SHARE, I_PNG, I_WEBP, I_TIFF, V_PHONE, V_1080, V_ORIG, V_HEVC, V_COPY, V_GIF, V_MP3, V_SIZE };
struct Preset { int id; const wchar_t* title; const wchar_t* sub; bool needFF; };
static const Preset kImgPresets[] = {
    { I_JPEG_SHARE, L"JPEG для отправки", L"2048 px по длинной стороне, малый вес", false },
    { I_JPEG_MAX,   L"JPEG, максимум качества", L"исходный размер", false },
    { I_PNG,        L"PNG", L"без потерь, с прозрачностью", false },
    { I_WEBP,       L"WebP", L"компактно, для веба", true },
    { I_TIFF,       L"TIFF", L"без потерь, для печати и архива", false },
};
static const Preset kVidPresets[] = {
    { V_PHONE, L"MP4 для телефона", L"720p, H.264, быстро и мало весит", true },
    { V_SIZE,  L"MP4 под размер", L"уместить в заданное число мегабайт", true },
    { V_1080,  L"MP4 1080p", L"H.264, хорошее качество", true },
    { V_ORIG,  L"MP4 в исходном размере", L"H.264, высокое качество", true },
    { V_HEVC,  L"HEVC (H.265)", L"вдвое компактнее при том же качестве", true },
    { V_COPY,  L"Без перекодирования", L"мгновенно, поворот пишется в файл, кроп не применяется", true },
    { V_GIF,   L"GIF", L"480 px, 12 кадров/с, без звука", true },
    { V_MP3,   L"Только звук, MP3", L"аудиодорожка отдельно", true },
};
static const int kImgPresetN = 5, kVidPresetN = 8;

// ---- hardware encoder probe (runs once per session, in background)
static volatile LONG g_hwState = 0;   // 0 not started, 1 running, 2 done
static WS g_hwH264, g_hwHevc;
static bool ffEncoderWorks(const WS& ff, const wchar_t* enc) {
    std::string out;
    WS cmd = q(ff) + L" -v error -nostdin -f lavfi -i color=c=black:s=640x360:r=30:d=0.3 -frames:v 5 -pix_fmt nv12 -c:v " + enc + L" -f null -";
    return runCapture(cmd, out, 1, 4096) == 0;
}
static DWORD WINAPI hwProbe(void*) {
    WS ff = findFfmpeg();
    if (!ff.empty()) {
        const wchar_t* v[] = { L"nvenc", L"qsv", L"amf" };
        for (auto s : v) {
            WS h = WS(L"h264_") + s;
            if (ffEncoderWorks(ff, h.c_str())) {
                g_hwH264 = h; WS e = WS(L"hevc_") + s;
                if (ffEncoderWorks(ff, e.c_str())) g_hwHevc = e;
                break;
            }
        }
    }
    InterlockedExchange(&g_hwState, 2);
    PostMessageW(g.hwnd, WM_APP_HW, 0, 0);
    return 0;
}
static void startHwProbe() {
    if (InterlockedCompareExchange(&g_hwState, 1, 0) == 0) CloseHandle(CreateThread(nullptr, 0, hwProbe, nullptr, 0, nullptr));
}

// ---- job
struct Job {
    WS src, dst; int preset = 0; bool video = false; int rot = 0, exifRot = 0; bool crop = false; float cx = 0, cy = 0, cw = 1, ch = 1;
    Com<IWICBitmap> bmp; double dur = 0;
    double t0 = -1, t1 = -1;   // fragment to export, seconds (-1 = from start / to end)
    int targetMB = 25;         // for V_SIZE
};
static volatile LONG g_jobCancel = 0; static HANDLE g_jobProc = nullptr; static Lock g_jobLock;

static WS uniquePath(const WS& dir, const WS& stem, const WS& ext) {
    WS p = dir + L"\\" + stem + L"." + ext;
    for (int i = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES && i < 10000; i++) p = dir + L"\\" + stem + fmtW(L" (%d).", i) + ext;
    return p;
}

static bool wicSave(IWICImagingFactory* f, IWICBitmapSource* src, UINT w, UINT h, const WS& dst, REFGUID container, WICPixelFormatGUID fmt, float quality) {
    Com<IWICStream> st; Com<IWICBitmapEncoder> enc; Com<IWICBitmapFrameEncode> fr; Com<IPropertyBag2> props;
    if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromFilename(dst.c_str(), GENERIC_WRITE))) return false;
    if (FAILED(f->CreateEncoder(container, nullptr, &enc)) || FAILED(enc->Initialize(st, WICBitmapEncoderNoCache))) return false;
    if (FAILED(enc->CreateNewFrame(&fr, &props))) return false;
    if (quality > 0 && props.p) {
        PROPBAG2 o = {}; o.pstrName = (LPOLESTR)L"ImageQuality"; VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = quality;
        props->Write(1, &o, &v);
    }
    if (FAILED(fr->Initialize(props))) return false;
    fr->SetSize(w, h);
    WICPixelFormatGUID want = fmt; fr->SetPixelFormat(&want);
    Com<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(src, want, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    if (FAILED(fr->WriteSource(conv, nullptr))) return false;
    if (FAILED(fr->Commit()) || FAILED(enc->Commit())) return false;
    return true;
}

static bool exportImage(Job* j, WS& err) {
    Com<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) { err = L"WIC недоступен"; return false; }
    Com<IWICBitmap> bmp = j->bmp; int exif = j->exifRot;
    if (!bmp.p) {
        ImgRes r;
        if (!decodeFile(f, j->src, &r)) { err = L"Не удалось прочитать исходный файл"; return false; }
        bmp = r.bmp; exif = r.exifRot;
    }
    UINT W = 0, H = 0; bmp->GetSize(&W, &H);
    int rot = (exif + j->rot) & 3;
    UINT RW = (rot & 1) ? H : W, RH = (rot & 1) ? W : H;
    UINT x0 = 0, y0 = 0, cw = RW, ch = RH;
    if (j->crop) {
        x0 = std::min(RW - 1, (UINT)(j->cx * RW + 0.5f)); y0 = std::min(RH - 1, (UINT)(j->cy * RH + 0.5f));
        cw = std::max(1u, std::min(RW - x0, (UINT)(j->cw * RW + 0.5f))); ch = std::max(1u, std::min(RH - y0, (UINT)(j->ch * RH + 0.5f)));
    }
    std::vector<UINT32> out((size_t)cw * ch);
    UINT32 andA = 0xFF000000;
    {
        WICRect rc = { 0, 0, (INT)W, (INT)H }; Com<IWICBitmapLock> lk;
        if (FAILED(bmp->Lock(&rc, WICBitmapLockRead, &lk))) { err = L"Нет доступа к пикселям"; return false; }
        UINT stride = 0, sz = 0; BYTE* p = nullptr; lk->GetStride(&stride); lk->GetDataPointer(&sz, &p);
        for (UINT y = 0; y < ch; y++) {
            UINT ry = y0 + y; UINT32* d = &out[(size_t)y * cw];
            if (rot == 0) {
                memcpy(d, p + (size_t)ry * stride + (size_t)x0 * 4, (size_t)cw * 4);
                for (UINT x = 0; x < cw; x++) andA &= d[x];
            } else if (rot == 1) {
                for (UINT x = 0; x < cw; x++) { UINT rx = x0 + x; UINT32 v = *(UINT32*)(p + (size_t)(H - 1 - rx) * stride + (size_t)ry * 4); d[x] = v; andA &= v; }
            } else if (rot == 2) {
                const UINT32* s = (const UINT32*)(p + (size_t)(H - 1 - ry) * stride);
                for (UINT x = 0; x < cw; x++) { UINT32 v = s[W - 1 - (x0 + x)]; d[x] = v; andA &= v; }
            } else {
                for (UINT x = 0; x < cw; x++) { UINT rx = x0 + x; UINT32 v = *(UINT32*)(p + (size_t)rx * stride + (size_t)(W - 1 - ry) * 4); d[x] = v; andA &= v; }
            }
            if ((y & 255) == 0) { if (g_jobCancel) return false; PostMessageW(g.hwnd, WM_APP_EXPORT, 0, (LPARAM)(y * 500 / ch)); }
        }
    }
    bool opaque = (andA == 0xFF000000);
    Com<IWICBitmap> b2;
    if (FAILED(f->CreateBitmapFromMemory(cw, ch, GUID_WICPixelFormat32bppPBGRA, cw * 4, (UINT)(out.size() * 4), (BYTE*)out.data(), &b2))) { err = L"Недостаточно памяти"; return false; }
    Com<IWICBitmapSource> src((IWICBitmapSource*)b2.p);
    UINT ow = cw, oh = ch;
    if (j->preset == I_JPEG_SHARE && std::max(cw, ch) > 2048) {
        double sc = 2048.0 / std::max(cw, ch); ow = std::max(1u, (UINT)(cw * sc + 0.5)); oh = std::max(1u, (UINT)(ch * sc + 0.5));
        Com<IWICBitmapScaler> s;
        if (SUCCEEDED(f->CreateBitmapScaler(&s)) && SUCCEEDED(s->Initialize(src, ow, oh, WICBitmapInterpolationModeFant))) src = Com<IWICBitmapSource>((IWICBitmapSource*)s.p);
        else { ow = cw; oh = ch; }
    }
    PostMessageW(g.hwnd, WM_APP_EXPORT, 0, 600);
    WICPixelFormatGUID rgb = GUID_WICPixelFormat24bppBGR, rgba = GUID_WICPixelFormat32bppBGRA;
    bool ok = false;
    switch (j->preset) {
    case I_JPEG_MAX:   ok = wicSave(f, src, ow, oh, j->dst, GUID_ContainerFormatJpeg, rgb, 0.95f); break;
    case I_JPEG_SHARE: ok = wicSave(f, src, ow, oh, j->dst, GUID_ContainerFormatJpeg, rgb, 0.82f); break;
    case I_PNG:        ok = wicSave(f, src, ow, oh, j->dst, GUID_ContainerFormatPng, opaque ? rgb : rgba, 0); break;
    case I_TIFF:       ok = wicSave(f, src, ow, oh, j->dst, GUID_ContainerFormatTiff, opaque ? rgb : rgba, 0); break;
    case I_WEBP: {
        WS ff = findFfmpeg();
        if (ff.empty()) { err = L"Для WebP нужен ffmpeg.exe рядом с программой"; return false; }
        wchar_t tp[MAX_PATH]; GetTempPathW(MAX_PATH, tp);
        WS tmp = WS(tp) + fmtW(L"mediasense_%u.png", GetTickCount());
        if (wicSave(f, src, ow, oh, tmp, GUID_ContainerFormatPng, opaque ? rgb : rgba, 0)) {
            std::string o;
            ok = runCapture(q(ff) + L" -y -v error -nostdin -i " + q(tmp) + L" -c:v libwebp -quality 88 " + q(j->dst), o, 1, 4096) == 0;
            if (!ok) err = L"ffmpeg: " + wide(o.substr(0, 200));
        }
        DeleteFileW(tmp.c_str());
        break; }
    }
    if (!ok && err.empty()) err = L"Не удалось записать файл. Проверьте папку назначения.";
    return ok;
}

static int runFf(const WS& cmd, Job* j, std::string& errText) {
    HANDLE rd = nullptr; HANDLE hp = spawnProc(cmd, &rd, 1);
    if (!hp) return -1;
    g_jobLock.lock(); g_jobProc = hp; g_jobLock.unlock();
    std::string line; char buf[8192]; DWORD n;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) {
        for (DWORD i = 0; i < n; i++) {
            char c = buf[i];
            if (c != '\n' && c != '\r') { line += c; continue; }
            if (line.empty()) continue;
            size_t eq = line.find('='); size_t sp = line.find(' ');
            if (eq != std::string::npos && (sp == std::string::npos || sp > eq)) {
                if (!line.compare(0, 12, "out_time_us=") || !line.compare(0, 12, "out_time_ms=")) {
                    double t = atof(line.c_str() + 12) / 1e6;
                    if (j->dur > 0.01) { int pm = (int)(t / j->dur * 1000); PostMessageW(g.hwnd, WM_APP_EXPORT, 0, std::max(0, std::min(999, pm))); }
                }
            } else if (errText.size() < 600) { errText += line; errText += ' '; }
            line.clear();
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(hp, INFINITE);
    DWORD code = 1; GetExitCodeProcess(hp, &code);
    g_jobLock.lock(); g_jobProc = nullptr; g_jobLock.unlock();
    CloseHandle(hp);
    return (int)code;
}
static WS vfChain(Job* j, int shortSide) {
    WS f;
    auto add = [&](const WS& s) { if (!f.empty()) f += L","; f += s; };
    if (j->rot == 1) add(L"transpose=1"); else if (j->rot == 2) add(L"hflip,vflip"); else if (j->rot == 3) add(L"transpose=2");
    if (j->crop) add(fmtW(L"crop=iw*%.5f:ih*%.5f:iw*%.5f:ih*%.5f", j->cw, j->ch, j->cx, j->cy));
    if (shortSide) add(fmtW(L"scale='if(gt(iw,ih),-2,min(%d,iw))':'if(gt(iw,ih),min(%d,ih),-2)'", shortSide, shortSide));
    add(L"scale=trunc(iw/2)*2:trunc(ih/2)*2");
    return f;
}
static WS videoCmd(Job* j, const WS& ff, bool hw) {
    WS base = q(ff) + L" -hide_banner -nostdin -y -v error -progress pipe:1 -nostats ";
    WS in;
    if (j->t0 > 0) in += fmtW(L"-ss %.3f ", j->t0);
    if (j->t1 > 0) in += fmtW(L"-to %.3f ", j->t1);
    in += L"-i " + q(j->src) + L" ";
    WS h264 = hw ? g_hwH264 : L"", hevc = hw ? g_hwHevc : L"";
    auto enc264 = [&](int crf, int kbps) {
        if (!h264.empty()) return fmtW(L"-c:v %s -b:v %dk -maxrate %dk -bufsize %dk -pix_fmt nv12 ", h264.c_str(), kbps, kbps * 3 / 2, kbps * 2);
        return fmtW(L"-c:v libx264 -preset veryfast -crf %d -pix_fmt yuv420p ", crf);
    };
    WS accel = hw ? L"-hwaccel auto " : L"";
    switch (j->preset) {
    case V_PHONE: return base + accel + in + L"-vf \"" + vfChain(j, 720) + L"\" " + enc264(26, 3000) + L"-c:a aac -b:a 128k -ac 2 -movflags +faststart " + q(j->dst);
    case V_SIZE: {   // bitrate from the target size and duration; resolution steps down when the budget is small
        double dur = std::max(1.0, j->dur);
        int total = (int)(j->targetMB * 8.0 * 1024.0 * 0.94 / dur), ab = total < 260 ? 64 : 96, vb = std::max(100, total - ab);
        int side = vb < 600 ? 480 : vb < 1500 ? 720 : 1080;
        WS e = !h264.empty() ? fmtW(L"-c:v %s -b:v %dk -maxrate %dk -bufsize %dk -pix_fmt nv12 ", h264.c_str(), vb, vb, vb * 2)
                             : fmtW(L"-c:v libx264 -preset veryfast -b:v %dk -maxrate %dk -bufsize %dk -pix_fmt yuv420p ", vb, vb * 13 / 10, vb * 2);
        return base + accel + in + L"-vf \"" + vfChain(j, side) + L"\" " + e + fmtW(L"-c:a aac -b:a %dk -ac 2 -movflags +faststart ", ab) + q(j->dst); }
    case V_1080:  return base + accel + in + L"-vf \"" + vfChain(j, 1080) + L"\" " + enc264(23, 6500) + L"-c:a aac -b:a 192k -movflags +faststart " + q(j->dst);
    case V_ORIG:  return base + accel + in + L"-vf \"" + vfChain(j, 0) + L"\" " + enc264(19, 14000) + L"-c:a aac -b:a 192k -movflags +faststart " + q(j->dst);
    case V_HEVC: {
        WS e = !hevc.empty() ? fmtW(L"-c:v %s -b:v 6000k -maxrate 9000k -bufsize 12000k -pix_fmt nv12 -tag:v hvc1 ", hevc.c_str())
                             : WS(L"-c:v libx265 -preset fast -crf 26 -pix_fmt yuv420p -tag:v hvc1 ");
        return base + accel + in + L"-vf \"" + vfChain(j, 0) + L"\" " + e + L"-c:a aac -b:a 192k -movflags +faststart " + q(j->dst); }
    case V_GIF:   return base + in + L"-vf \"" + vfChain(j, 0) + L",fps=12,scale='min(480,iw)':-2:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse\" -an " + q(j->dst);
    case V_MP3:   return base + in + L"-vn -c:a libmp3lame -q:a 2 " + q(j->dst);
    case V_COPY: {
        WS rotOpt;
        if (j->rot) {
            std::string info; runCapture(q(ff) + L" -hide_banner -nostdin -i " + q(j->src), info, 2, 1 << 16);
            double old = 0; size_t p = info.find("rotation of ");
            if (p != std::string::npos) old = atof(info.c_str() + p + 12);
            int nr = (int)lround(old) - j->rot * 90;
            while (nr > 180) nr -= 360; while (nr <= -180) nr += 360;
            rotOpt = fmtW(L"-display_rotation:v:0 %d ", nr);
        }
        WS e = extOf(j->dst); WS mov = (e == L"mp4" || e == L"m4v" || e == L"mov") ? L"-movflags +faststart " : L"";
        return base + rotOpt + in + L"-c copy " + mov + q(j->dst); }
    }
    return L"";
}
static bool exportVideo(Job* j, WS& err) {
    WS ff = findFfmpeg();
    if (ff.empty()) { err = L"Для конвертации видео нужен ffmpeg.exe рядом с программой"; return false; }
    bool reenc = j->preset == V_PHONE || j->preset == V_1080 || j->preset == V_ORIG || j->preset == V_HEVC || j->preset == V_SIZE;
    if (j->dur <= 0) {   // batch jobs come without a known duration: read it from the file (needed for progress and for V_SIZE)
        std::string info; runCapture(q(ff) + L" -hide_banner -nostdin -i " + q(j->src), info, 2, 1 << 16);
        size_t p = info.find("Duration: "); int hh = 0, mm = 0; double ss = 0;
        if (p != std::string::npos && sscanf(info.c_str() + p + 10, "%d:%d:%lf", &hh, &mm, &ss) == 3) j->dur = hh * 3600 + mm * 60 + ss;
    }
    if (reenc) { startHwProbe(); while (g_hwState != 2 && !g_jobCancel) Sleep(30); }
    bool hw = reenc && ((j->preset == V_HEVC) ? !g_hwHevc.empty() : !g_hwH264.empty());
    std::string e1;
    int code = runFf(videoCmd(j, ff, hw), j, e1);
    if (code != 0 && hw && !g_jobCancel) {   // hardware path failed: retry fully in software
        DeleteFileW(j->dst.c_str()); e1.clear();
        code = runFf(videoCmd(j, ff, false), j, e1);
    }
    if (code == 0) return true;
    err = e1.empty() ? WS(L"ffmpeg завершился с ошибкой") : wide(e1.substr(0, 260));
    return false;
}
static DWORD WINAPI jobThread(void* p) {
    Job* j = (Job*)p;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    WS err; bool ok = j->video ? exportVideo(j, err) : exportImage(j, err);
    if (g_jobCancel) { DeleteFileW(j->dst.c_str()); PostMessageW(g.hwnd, WM_APP_EXPORT, 3, 0); }
    else if (ok) PostMessageW(g.hwnd, WM_APP_EXPORT, 1, (LPARAM) new WS(j->dst));
    else { DeleteFileW(j->dst.c_str()); PostMessageW(g.hwnd, WM_APP_EXPORT, 2, (LPARAM) new WS(err)); }
    delete j;
    CoUninitialize();
    return 0;
}
// several files in a row (gallery selection)
struct Batch { std::vector<Job*> jobs; };
static volatile LONG g_batchIdx = 0, g_batchN = 0;
static DWORD WINAPI batchThread(void* p) {
    Batch* b = (Batch*)p;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int ok = 0, n = (int)b->jobs.size();
    for (int i = 0; i < n && !g_jobCancel; i++) {
        InterlockedExchange(&g_batchIdx, i);
        PostMessageW(g.hwnd, WM_APP_EXPORT, 0, 0);
        Job* j = b->jobs[i]; WS err;
        bool r = j->video ? exportVideo(j, err) : exportImage(j, err);
        if (r && !g_jobCancel) ok++; else DeleteFileW(j->dst.c_str());
    }
    for (auto j : b->jobs) delete j;
    delete b;
    PostMessageW(g.hwnd, WM_APP_EXPORT, g_jobCancel ? 3 : 4, MAKELPARAM(ok, n));
    CoUninitialize();
    return 0;
}
static void cancelExport() {
    InterlockedExchange(&g_jobCancel, 1);
    Guard gd(g_jobLock);
    if (g_jobProc) TerminateProcess(g_jobProc, 1);
}
