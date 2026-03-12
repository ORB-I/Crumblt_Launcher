#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <algorithm>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

// ---- Config ----
#define LATEST_URL   "https://github.com/ORB-I/Crumblt_Client/releases/latest/download/Crumblt.zip"
#define VERSION_URL  "https://github.com/ORB-I/Crumblt_Client/releases/latest/download/version.txt"
#define CLIENT_EXE   "CrumbltClient.exe"
#define WIN_W        520
#define WIN_H        280

// ---- Colors ----
#define CLR_BG       RGB(10,  12,  20)
#define CLR_PANEL    RGB(16,  19,  32)
#define CLR_ACCENT   RGB(99,  179, 237)
#define CLR_ACCENT2  RGB(159, 122, 234)
#define CLR_TEXT     RGB(220, 230, 255)
#define CLR_SUBTEXT  RGB(100, 115, 150)
#define CLR_BAR_BG   RGB(25,  30,  50)
#define CLR_BAR_FG   RGB(99,  179, 237)

namespace fs = std::filesystem;

// ---- State ----
static HWND      g_hwnd        = nullptr;
static HFONT     g_fontTitle   = nullptr;
static HFONT     g_fontSub     = nullptr;
static HFONT     g_fontStatus  = nullptr;
static std::atomic<float> g_progress{0.0f};
static std::atomic<bool>  g_done{false};
static std::atomic<bool>  g_error{false};
static std::wstring       g_statusText = L"Checking for updates...";
static std::wstring       g_gameArgs;
static CRITICAL_SECTION   g_cs;

static void SetStatus(const std::wstring& s) {
    EnterCriticalSection(&g_cs);
    g_statusText = s;
    LeaveCriticalSection(&g_cs);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static std::wstring GetStatus() {
    EnterCriticalSection(&g_cs);
    auto s = g_statusText;
    LeaveCriticalSection(&g_cs);
    return s;
}

// ---- Paths (AppData version - WORKING) ----
static fs::path GetInstallDir() {
    wchar_t buf[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf);
    return fs::path(buf) / L"Crumblt";
}

static fs::path GetVersionFile() { return GetInstallDir() / L"version.txt"; }
static fs::path GetClientExe()   { return GetInstallDir() / CLIENT_EXE; }

// ---- HTTP helpers ----
static std::string HttpGet(const std::string& url) {
    HINTERNET hNet = InternetOpenA("CrumbltLauncher/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hNet) return "";

    DWORD timeout = 5000;
    InternetSetOptionA(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hNet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hUrl = InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    std::string result;
    if (hUrl) {
        char buf[4096]; DWORD read = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
            result.append(buf, read);
        InternetCloseHandle(hUrl);
    }
    InternetCloseHandle(hNet);
    return result;
}

static bool HttpDownload(const std::string& url, const fs::path& dest,
                          std::function<void(float)> onProgress) {
    HINTERNET hNet = InternetOpenA("CrumbltLauncher/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hNet) return false;

    DWORD timeout = 5000;
    InternetSetOptionA(hNet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hNet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hNet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hUrl = InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    if (!hUrl) { InternetCloseHandle(hNet); return false; }

    DWORD contentLen = 0, lenSize = sizeof(contentLen);
    HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                   &contentLen, &lenSize, nullptr);

    std::ofstream f(dest, std::ios::binary);
    if (!f) { InternetCloseHandle(hUrl); InternetCloseHandle(hNet); return false; }

    char buf[65536]; DWORD read = 0; DWORD total = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0) {
        f.write(buf, read);
        total += read;
        if (contentLen > 0 && onProgress)
            onProgress((float)total / (float)contentLen);
    }
    f.close();
    InternetCloseHandle(hUrl); InternetCloseHandle(hNet);
    return total > 0;
}

// ---- Zip extraction via PowerShell ----
static bool ExtractZip(const fs::path& zip, const fs::path& dest) {
    std::wstring cmd = L"powershell -NoProfile -Command \"Expand-Archive -Force -Path '"
        + zip.wstring() + L"' -DestinationPath '" + dest.wstring() + L"'\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exit = 1; GetExitCodeProcess(pi.hProcess, &exit);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exit == 0;
}

// ---- Launch client ----
static void LaunchClient() {
    fs::path exe = GetClientExe();
    if (!fs::exists(exe)) {
        SetStatus(L"Client executable not found!");
        g_error = true;
        return;
    }

    std::wstring args = L"\"" + exe.wstring() + L"\"";
    if (!g_gameArgs.empty()) args += L" " + g_gameArgs;

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.wstring().c_str(), args.data(),
                       nullptr, nullptr, FALSE, 0, nullptr,
                       exe.parent_path().wstring().c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        SetStatus(L"Failed to launch client!");
        g_error = true;
    }
}

// ---- Update thread ----
static void UpdateThread() {
    fs::path installDir = GetInstallDir();
    fs::create_directories(installDir);

    // Fetch latest version tag
    SetStatus(L"Checking for updates...");
    std::string latestVer = HttpGet(VERSION_URL);

    // Trim whitespace properly
    latestVer.erase(std::remove_if(latestVer.begin(), latestVer.end(), ::isspace), latestVer.end());

    std::string localVer;
    if (fs::exists(GetVersionFile())) {
        std::ifstream f(GetVersionFile());
        std::getline(f, localVer);
        localVer.erase(std::remove_if(localVer.begin(), localVer.end(), ::isspace), localVer.end());
    }

    bool needsUpdate = latestVer.empty() || localVer != latestVer || !fs::exists(GetClientExe());

    if (!needsUpdate) {
        SetStatus(L"Up to date! Launching...");
        g_progress = 1.0f;
        Sleep(600);
        LaunchClient();
        g_done = true;
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    // Download
    std::wstring verW(latestVer.begin(), latestVer.end());
    SetStatus(L"Downloading " + verW + L"...");

    fs::path zipPath = installDir / L"update.zip";
    bool ok = HttpDownload(LATEST_URL, zipPath, [](float p) {
        g_progress = p * 0.85f;
        InvalidateRect(g_hwnd, nullptr, FALSE);
    });

    if (!ok) {
        g_error = true;
        SetStatus(L"Download failed. Check your connection.");
        return;
    }

    // Extract
    SetStatus(L"Installing...");
    g_progress = 0.90f;
    InvalidateRect(g_hwnd, nullptr, FALSE);

    if (!ExtractZip(zipPath, installDir)) {
        g_error = true;
        SetStatus(L"Extraction failed.");
        return;
    }

    fs::remove(zipPath);

    // Save version
    { std::ofstream f(GetVersionFile()); f << latestVer; }

    g_progress = 1.0f;
    SetStatus(L"Launching Crumblt...");
    InvalidateRect(g_hwnd, nullptr, FALSE);
    Sleep(500);

    LaunchClient();
    g_done = true;
    PostMessage(g_hwnd, WM_CLOSE, 0, 0);
}

// ---- Painting ----
static void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bgBrush = CreateSolidBrush(CLR_BG);
    FillRect(mem, &rc, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH accentBrush = CreateSolidBrush(CLR_ACCENT);
    RECT topLine = {0, 0, W/2, 3};
    FillRect(mem, &topLine, accentBrush);
    DeleteObject(accentBrush);
    HBRUSH accent2Brush = CreateSolidBrush(CLR_ACCENT2);
    RECT topLine2 = {W/2, 0, W, 3};
    FillRect(mem, &topLine2, accent2Brush);
    DeleteObject(accent2Brush);

    HBRUSH panelBrush = CreateSolidBrush(CLR_PANEL);
    RECT panel = {30, 25, W-30, H-30};
    FillRect(mem, &panel, panelBrush);
    DeleteObject(panelBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(40, 50, 80));
    HPEN oldPen = (HPEN)SelectObject(mem, borderPen);
    HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(mem, nullBrush);
    Rectangle(mem, 30, 25, W-30, H-30);
    SelectObject(mem, oldPen); SelectObject(mem, oldBrush);
    DeleteObject(borderPen);

    HBRUSH dot1 = CreateSolidBrush(CLR_ACCENT);
    RECT dotR = {52, 45, 62, 55};
    FillRect(mem, &dotR, dot1);
    DeleteObject(dot1);
    HBRUSH dot2 = CreateSolidBrush(CLR_ACCENT2);
    RECT dotR2 = {63, 45, 73, 55};
    FillRect(mem, &dotR2, dot2);
    DeleteObject(dot2);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, CLR_TEXT);
    SelectObject(mem, g_fontTitle);
    RECT titleR = {80, 40, W-50, 75};
    DrawTextW(mem, L"Crumblt", -1, &titleR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(mem, CLR_SUBTEXT);
    SelectObject(mem, g_fontSub);
    RECT subR = {80, 68, W-50, 88};
    DrawTextW(mem, L"Game Platform", -1, &subR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HPEN divPen = CreatePen(PS_SOLID, 1, RGB(35, 42, 70));
    SelectObject(mem, divPen);
    MoveToEx(mem, 52, 100, nullptr);
    LineTo(mem, W-52, 100);
    SelectObject(mem, (HPEN)GetStockObject(NULL_PEN));
    DeleteObject(divPen);

    std::wstring status = GetStatus();
    SetTextColor(mem, g_error ? RGB(255,100,100) : CLR_TEXT);
    SelectObject(mem, g_fontStatus);
    RECT statusR = {52, 115, W-52, 145};
    DrawTextW(mem, status.c_str(), -1, &statusR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT barBg = {52, 155, W-52, 175};
    HBRUSH barBgBrush = CreateSolidBrush(CLR_BAR_BG);
    FillRect(mem, &barBg, barBgBrush);
    DeleteObject(barBgBrush);

    float p = g_progress.load();
    if (p > 0.0f) {
        int fillW = (int)((W - 104) * p);
        if (fillW > 0) {
            int splitX = 52 + fillW/2;
            HBRUSH fb1 = CreateSolidBrush(CLR_ACCENT);
            RECT fill1 = {52, 156, (std::min)(splitX, 52 + fillW), 174};
            FillRect(mem, &fill1, fb1);
            DeleteObject(fb1);
            if (splitX < 52 + fillW) {
                HBRUSH fb2 = CreateSolidBrush(CLR_ACCENT2);
                RECT fill2 = {splitX, 156, 52 + fillW, 174};
                FillRect(mem, &fill2, fb2);
                DeleteObject(fb2);
            }
        }
    }

    HPEN barPen = CreatePen(PS_SOLID, 1, RGB(50, 60, 100));
    SelectObject(mem, barPen);
    SelectObject(mem, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(mem, 52, 155, W-52, 175);
    SelectObject(mem, (HPEN)GetStockObject(NULL_PEN));
    DeleteObject(barPen);

    if (p > 0.0f && p < 1.0f) {
        wchar_t pct[16]; swprintf(pct, 16, L"%d%%", (int)(p*100));
        SetTextColor(mem, CLR_SUBTEXT);
        SelectObject(mem, g_fontStatus);
        RECT pctR = {52, 178, W-52, 198};
        DrawTextW(mem, pct, -1, &pctR, DT_RIGHT | DT_SINGLELINE);
    }

    SetTextColor(mem, RGB(50, 60, 90));
    SelectObject(mem, g_fontStatus);
    RECT footR = {52, H-55, W-52, H-38};
    DrawTextW(mem, L"crumblt.com", -1, &footR, DT_RIGHT | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---- Window Proc ----
static POINT g_dragStart;
static bool  g_dragging = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        g_dragging = true;
        g_dragStart = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_dragging = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) {
            RECT wr; GetWindowRect(hwnd, &wr);
            int dx = GET_X_LPARAM(lp) - g_dragStart.x;
            int dy = GET_Y_LPARAM(lp) - g_dragStart.y;
            SetWindowPos(hwnd, nullptr, wr.left+dx, wr.top+dy, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureProtocolRegistration() {
    // Check if protocol is already registered
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\crumblt", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return; // Already registered
    }

    // Get launcher path
    wchar_t launcherPath[MAX_PATH];
    GetModuleFileNameW(nullptr, launcherPath, MAX_PATH);

    // Register protocol
    std::wstring key = L"Software\\Classes\\crumblt";
    RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"Crumblt Game Launcher", sizeof(L"Crumblt Game Launcher"));
    RegSetValueExW(hKey, L"URL Protocol", 0, REG_SZ, (BYTE*)L"", sizeof(L""));
    RegCloseKey(hKey);

    // DefaultIcon
    key = L"Software\\Classes\\crumblt\\DefaultIcon";
    RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)launcherPath, (wcslen(launcherPath) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);

    // Shell command
    key = L"Software\\Classes\\crumblt\\shell\\open\\command";
    RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    std::wstring command = std::wstring(L"\"") + launcherPath + L"\" \"%1\"";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)command.c_str(), (command.length() + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
}

// ---- WinMain ----
int WINAPI WinMainW(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    InitializeCriticalSection(&g_cs);

    EnsureProtocolRegistration();

    if (lpCmdLine && wcslen(lpCmdLine) > 0)
        g_gameArgs = lpCmdLine;

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);

    g_fontTitle  = CreateFontW(28, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontSub    = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontStatus = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = L"CrumbltLauncher";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - WIN_W) / 2, wy = (sh - WIN_H) / 2;

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"CrumbltLauncher", L"Crumblt",
        WS_POPUP | WS_VISIBLE,
        wx, wy, WIN_W, WIN_H,
        nullptr, nullptr, hInst, nullptr
    );

    DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(g_hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    MARGINS margins = {1,1,1,1};
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    std::thread(UpdateThread).detach();
    SetTimer(g_hwnd, 1, 50, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_TIMER) InvalidateRect(g_hwnd, nullptr, FALSE);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(g_fontTitle);
    DeleteObject(g_fontSub);
    DeleteObject(g_fontStatus);
    Gdiplus::GdiplusShutdown(gdipToken);
    DeleteCriticalSection(&g_cs);
    return 0;
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE p, LPWSTR cmd, int show) {
    return WinMainW(h, p, cmd, show);
}
