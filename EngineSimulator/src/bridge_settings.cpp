#include "../include/bridge_settings.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
// Themed (v6) common controls; without this the sliders and buttons draw in the pre-XP style
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace {

    enum { IdVolume = 101, IdVolumeLabel, IdIdle, IdIdleLabel, IdLoad, IdEngineLabel, IdTune, IdSimRate, IdSimRateLabel, IdErrorLabel, IdRetune };

    std::atomic<int> s_volumePercent{35};
    std::atomic<int> s_idleRpm{1100};
    std::atomic<bool> s_tune{false};
    std::atomic<int> s_simKHz{10};
    int s_reloadedKHz = 10;
    std::mutex s_pathMutex;
    std::string s_pendingPath, s_currentPath, s_loadError;
    std::atomic<bool> s_retune{false};
    bool s_hasPending = false;
    HWND s_engineLabel, s_errorLabel;

    std::string iniPath() {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p = path;
        return p.substr(0, p.find_last_of("\\/") + 1) + "beam_bridge.ini";
    }

    void save() {
        const std::string ini = iniPath();
        WritePrivateProfileStringA("bridge", "volume_percent", std::to_string(s_volumePercent.load()).c_str(), ini.c_str());
        WritePrivateProfileStringA("bridge", "beam_idle_rpm", std::to_string(s_idleRpm.load()).c_str(), ini.c_str());
        WritePrivateProfileStringA("bridge", "tune_beam_engine", s_tune ? "1" : "0", ini.c_str());
        WritePrivateProfileStringA("bridge", "simulation_khz", std::to_string(s_simKHz.load()).c_str(), ini.c_str());
        std::lock_guard<std::mutex> lock(s_pathMutex);
        WritePrivateProfileStringA("bridge", "engine", s_currentPath.c_str(), ini.c_str());
    }

    void load() {
        const std::string ini = iniPath();
        s_volumePercent = GetPrivateProfileIntA("bridge", "volume_percent", 35, ini.c_str());
        s_idleRpm = GetPrivateProfileIntA("bridge", "beam_idle_rpm", 1100, ini.c_str());
        s_tune = GetPrivateProfileIntA("bridge", "tune_beam_engine", 0, ini.c_str()) != 0;
        s_simKHz = (std::min)((std::max)((int)GetPrivateProfileIntA("bridge", "simulation_khz", 10, ini.c_str()), 10), 20);
        char path[MAX_PATH] = "";
        GetPrivateProfileStringA("bridge", "engine", "", path, MAX_PATH, ini.c_str());
        s_currentPath = path;
    }

    const COLORREF Background = RGB(24, 24, 27), Text = RGB(228, 228, 231), Muted = RGB(140, 140, 150),
        Accent = RGB(255, 140, 50), Error = RGB(255, 99, 88);
    HBRUSH s_background;
    HFONT s_font, s_headerFont, s_titleFont;
    HWND s_volumeValue, s_idleValue, s_simRateValue, s_tuneCheck;
    const int Width = 440, Margin = 18;

    void refreshLabels() {
        SetWindowTextA(s_volumeValue, (std::to_string(s_volumePercent.load()) + " %").c_str());
        SetWindowTextA(s_simRateValue, (std::to_string(s_simKHz.load()) + " kHz").c_str());
        SetWindowTextA(s_idleValue, s_idleRpm.load() == 0 ? "off" : (std::to_string(s_idleRpm.load()) + " rpm").c_str());
        std::lock_guard<std::mutex> lock(s_pathMutex);
        const std::string name = s_currentPath.empty() ? "assets/main.mr" : s_currentPath.substr(s_currentPath.find_last_of("\\/") + 1);
        SetWindowTextA(s_engineLabel, name.c_str());
        SetWindowTextA(s_errorLabel, s_loadError.c_str());
    }

    HWND makeChild(HWND parent, const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
        HWND child = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageA(child, WM_SETFONT, (WPARAM)font, FALSE);
        return child;
    }

    HWND makeLabel(HWND parent, int id, int y, const char *text = "", HFONT font = nullptr, int x = Margin, int w = Width - 2 * Margin, DWORD style = 0) {
        return makeChild(parent, "STATIC", text, style, x, y, w, 20, id, font ? font : s_font);
    }

    HWND makeSliderRow(HWND parent, int y, const char *label, int id, int minValue, int maxValue, int value, HWND *valueLabel) {
        makeLabel(parent, 0, y, label);
        *valueLabel = makeLabel(parent, 0, y, "", nullptr, Width - Margin - 120, 120, SS_RIGHT);
        HWND bar = makeChild(parent, TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | TBS_BOTH, Margin - 6, y + 20, Width - 2 * Margin + 12, 28, id, s_font);
        SendMessageA(bar, TBM_SETRANGEMIN, FALSE, minValue);
        SendMessageA(bar, TBM_SETRANGEMAX, FALSE, maxValue);
        SendMessageA(bar, TBM_SETPOS, TRUE, value);
        return bar;
    }

    HWND makeButton(HWND parent, const char *text, DWORD style, int x, int y, int w, int h, int id) {
        HWND button = makeChild(parent, "BUTTON", text, style, x, y, w, h, id, s_font);
        SetWindowTheme(button, L"DarkMode_Explorer", nullptr);
        return button;
    }

    void pickEngine(HWND owner) {
        char file[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = "Engine Sim script (*.mr)\0*.mr\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        // engine-sim resolves engine_sim.mr and ../assets from the working dir, and
        // GetOpenFileName ignores OFN_NOCHANGEDIR, so put the working dir back ourselves
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        const BOOL picked = GetOpenFileNameA(&ofn);
        SetCurrentDirectoryA(cwd);
        if (!picked) return;
        {
            std::lock_guard<std::mutex> lock(s_pathMutex);
            s_pendingPath = s_currentPath = file;
            s_hasPending = true;
        }
        save();
        refreshLabels();
    }

    enum { IdHeader = 200, IdHint, IdTuneLabel };

    LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const int id = GetDlgCtrlID((HWND)lParam);
            SetTextColor((HDC)wParam, id == IdHeader ? Accent : id == IdHint ? Muted : id == IdErrorLabel ? Error : Text);
            SetBkColor((HDC)wParam, Background);
            return (LRESULT)s_background;
        }
        case WM_HSCROLL: {
            const int pos = (int)SendMessageA((HWND)lParam, TBM_GETPOS, 0, 0);
            const int id = GetDlgCtrlID((HWND)lParam);
            if (id == IdVolume) s_volumePercent = pos;
            else if (id == IdIdle) s_idleRpm = pos * 50;
            else if (id == IdSimRate) s_simKHz = pos;
            refreshLabels();
            if (LOWORD(wParam) == TB_ENDTRACK || LOWORD(wParam) == TB_THUMBPOSITION) {
                save();
                if (id == IdSimRate && s_simKHz != s_reloadedKHz) {
                    s_reloadedKHz = s_simKHz;
                    std::lock_guard<std::mutex> lock(s_pathMutex);
                    s_pendingPath = s_currentPath;
                    s_hasPending = true;
                }
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IdLoad) pickEngine(hwnd);
            else if (LOWORD(wParam) == IdRetune) s_retune = true;
            else if (LOWORD(wParam) == IdTune || LOWORD(wParam) == IdTuneLabel) {
                if (LOWORD(wParam) == IdTuneLabel) {
                    SendMessageA(s_tuneCheck, BM_SETCHECK, s_tune ? BST_UNCHECKED : BST_CHECKED, 0);
                }
                s_tune = SendMessageA(s_tuneCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                save();
            }
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }

    HFONT makeFont(int height, int weight) {
        return CreateFontA(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    }

    void windowThread() {
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        s_background = CreateSolidBrush(Background);
        s_font = makeFont(14, FW_NORMAL);
        s_headerFont = makeFont(12, FW_BOLD);
        s_titleFont = makeFont(17, FW_SEMIBOLD);

        WNDCLASSA wc = {};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hbrBackground = s_background;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = "EngineSimBridgeSettings";
        RegisterClassA(&wc);

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT frame = { 0, 0, Width, 452 };
        AdjustWindowRectEx(&frame, style, FALSE, WS_EX_TOPMOST);
        HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, "EngineSim - BeamNG Bridge", style,
            40, 40, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, wc.hInstance, nullptr);
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

        int y = 14;
        makeLabel(hwnd, IdHeader, y, "ENGINE", s_headerFont);
        s_engineLabel = makeChild(hwnd, "STATIC", "", SS_ENDELLIPSIS, Margin, y + 24, Width - 2 * Margin - 130, 24, IdEngineLabel, s_titleFont);
        makeButton(hwnd, "Load engine...", BS_PUSHBUTTON, Width - Margin - 120, y + 20, 120, 30, IdLoad);
        s_errorLabel = makeChild(hwnd, "STATIC", "", 0, Margin, y + 56, Width - 2 * Margin, 34, IdErrorLabel, s_font);
        y += 96;
        makeSliderRow(hwnd, y, "Simulation rate", IdSimRate, 10, 20, s_simKHz, &s_simRateValue);
        makeLabel(hwnd, IdHint, y + 48, "Higher sounds crisper but costs CPU. Lower it if FPS drops.");
        s_reloadedKHz = s_simKHz;

        y += 88;
        makeLabel(hwnd, IdHeader, y, "BEAMNG", s_headerFont);
        y += 22;
        makeSliderRow(hwnd, y, "Engine volume", IdVolume, 0, 200, s_volumePercent, &s_volumeValue);
        y += 56;
        makeSliderRow(hwnd, y, "BeamNG keeps its own torque below", IdIdle, 0, 60, s_idleRpm / 50, &s_idleValue);
        makeLabel(hwnd, IdHint, y + 48, "Steadies idle. Above it, engine-sim's torque drives the car.");
        y += 80;
        s_tuneCheck = makeButton(hwnd, "", BS_AUTOCHECKBOX, Margin, y + 2, 18, 18, IdTune);
        SendMessageA(s_tuneCheck, BM_SETCHECK, s_tune ? BST_CHECKED : BST_UNCHECKED, 0);
        makeLabel(hwnd, IdTuneLabel, y, "Tune the car's engine to this engine", nullptr, Margin + 26, 300, SS_NOTIFY);
        makeLabel(hwnd, IdHint, y + 20, "Idle, redline, torque curve and inertia from engine-sim", nullptr, Margin + 26, 380);
        makeButton(hwnd, "Retune BeamNG car", BS_PUSHBUTTON, Margin, y + 48, 160, 30, IdRetune);

        refreshLabels();
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        MSG msg;
        while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

}

// Access violations: log exe-relative return addresses to bridge_crash.log (resolve with the PDB)
static LONG CALLBACK logCrash(EXCEPTION_POINTERS *info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    const std::string log = iniPath().substr(0, iniPath().size() - 15) + "bridge_crash.log";
    if (FILE *f = fopen(log.c_str(), "a")) {
        const auto base = (ULONG_PTR)GetModuleHandleA(nullptr);
        void *frames[48];
        const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
        fprintf(f, "AV rip=%llx (exe+%llx)\n", (unsigned long long)info->ContextRecord->Rip, (unsigned long long)(info->ContextRecord->Rip - base));
        for (USHORT i = 0; i < n; ++i) fprintf(f, "  exe+%llx\n", (unsigned long long)((ULONG_PTR)frames[i] - base));
        fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void bridgeSettingsStart(const std::string &initialEnginePath) {
    AddVectoredExceptionHandler(1, logCrash);
    load();
    if (!initialEnginePath.empty()) s_currentPath = initialEnginePath;
    std::thread(windowThread).detach();
}

float bridgeSettingsVolume() { return s_volumePercent / 100.0f; }

float bridgeSettingsBeamIdleRpm() { return (float)s_idleRpm; }

bool bridgeSettingsTuneBeamEngine() { return s_tune; }

double bridgeSettingsSimulationFrequency() { return s_simKHz * 1000.0; }

bool bridgeSettingsTakeEnginePath(std::string *path) {
    std::lock_guard<std::mutex> lock(s_pathMutex);
    if (!s_hasPending) return false;
    *path = s_pendingPath;
    s_hasPending = false;
    return true;
}

void bridgeSettingsSetLoadError(const std::string &message) {
    {
        std::lock_guard<std::mutex> lock(s_pathMutex);
        s_loadError = message.empty() ? "" : message + "  (full text: build/bridge_load_errors.log)";
    }
    refreshLabels();
}

bool bridgeSettingsTakeRetune() { return s_retune.exchange(false); }

void bridgeSettingsSetEnginePath(const std::string &path) {
    {
        std::lock_guard<std::mutex> lock(s_pathMutex);
        s_currentPath = path;
    }
    save();
    refreshLabels();
}

std::string bridgeSettingsLastEnginePath() {
    std::lock_guard<std::mutex> lock(s_pathMutex);
    return s_currentPath;
}
