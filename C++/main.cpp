/*
 * 迅捷搜 - 基于迅捷搜引擎的桌面文件搜索程序
 * 支持通配符/正则/SQL 多模式搜索
 * 功能特性：
 *   - 实时关键词高亮、异步图标加载
 *   - 数据库扫描进度反馈
 *   - 全局热键 Ctrl+Alt+F 呼出/隐藏窗口
 *   - 关闭时最小化到系统托盘，点击托盘图标还原
 *   - SQL 模板菜单，快速查找聊天工具/下载/办公/媒体文件
 */

/* 【配置】是否启用文件大小和修改时间字段
 * 取消注释下一行以启用（会增加内存占用）
*/
//. #define ENABLE_FILE_SIZE_AND_TIME 1


/*
 * xunjieso.h
 * 迅捷搜 API 原生 C/C++ 头文件
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#undef CreateWindowEx
#include <commctrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <atomic>
#include <chrono>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <ole2.h>
#include <shlobj.h>

#include "xunjieso.h"
#include "picojson.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* 控件 ID */
#define ID_SEARCH_EDIT      1001  // 搜索编辑框
#define ID_MODE_COMBO       1002  // 搜索模式下拉框（通配符/正则/SQL）
#define ID_RESULT_LIST      1003  // 结果列表视图（虚拟列表）
#define ID_STATUS_BAR       1004  // 状态栏
#define ID_FILTER_COMBO     1005  // 文件类型筛选下拉框
#define ID_TIMER_UPDATE     1     // 状态更新定时器 ID
#define ID_CTX_OPEN         2001  // 右键菜单：打开文件
#define ID_CTX_OPENDIR      2002  // 右键菜单：打开所在文件夹
#define ID_CTX_COPYPATH     2003  // 右键菜单：复制完整路径
#define ID_CTX_COPYNAME     2004  // 右键菜单：复制文件名
#define ID_CTX_COPYALLPATH  2005  // 右键菜单：复制所有选中路径
#define ID_CTX_COPYALLNAME  2006  // 右键菜单：复制所有选中文件名
#define ID_CTX_PROPERTIES   2007  // 右键菜单：文件属性
#define ID_SQL_TEMPLATE     3000  // SQL 模板菜单项起始 ID
#define ID_HOTKEY_SHOW      1     // 全局热键 ID（Ctrl+Alt+F）
#define ID_TRAY_ICON        2     // 系统托盘图标 ID
#define ID_TRAY_RESTORE     4001  // 托盘菜单：还原窗口
#define ID_TRAY_EXIT        4002  // 托盘菜单：退出程序
#define ID_MENU_HOTKEY      5001  // 菜单：热键设置
#define ID_MENU_REBUILD     5002  // 菜单：重建索引
#define ID_HOTKEY_CTRL      5003  // 热键设置对话框中的热键控件
#define ID_MENU_ABOUT       5004  // 菜单：关于
#define ID_MENU_AUTOSTART   5005  // 菜单：开机自启动
#define ID_ABOUT_LINK_GITHUB 6001 // 关于对话框：GitHub 链接
#define ID_ABOUT_LINK_WEBSITE 6002 // 关于对话框：官网链接

/* 自定义窗口消息，用于异步回调与 UI 线程通信 */
#define WM_SCAN_PROGRESS    (WM_USER + 100)  // 扫描进度更新
#define WM_SCAN_COMPLETE    (WM_USER + 101)  // 磁盘扫描完成
#define WM_SEARCH_COMPLETE  (WM_USER + 102)  // 搜索完成
#define WM_SCAN_DRIVE       (WM_USER + 103)  // 正在扫描某分区
#define WM_ICON_READY       (WM_USER + 104)  // 图标异步加载完成
#define WM_LOAD_COMPLETE    (WM_USER + 105)  // 数据库加载完成
#define WM_TRAY_NOTIFY      (WM_USER + 106)  // 托盘图标事件通知

/* UI 颜色常量 */
#define HIGHLIGHT_COLOR RGB(30, 120, 220)   // 关键词高亮色（蓝色）
#define ALT_ROW_COLOR RGB(245, 248, 252)    // 奇数行背景色（浅灰蓝）

/* 布局常量 */
#define TOOLBAR_H 36    // 工具栏高度
#define TOOLBAR_Y 8     // 工具栏 Y 偏移
#define MARGIN 12       // 控件间距

/* 全局窗口句柄 */
HWND g_hMainWnd = NULL;       // 主窗口
HWND g_hSearchEdit = NULL;    // 搜索编辑框
HWND g_hModeCombo = NULL;     // 搜索模式下拉框
HWND g_hResultList = NULL;    // 结果列表视图
HWND g_hStatusBar = NULL;     // 状态栏
HWND g_hFilterCombo = NULL;   // 文件类型筛选下拉框
HWND g_hSearchIcon = NULL;    // 搜索图标控件

/* 全局字体和 GDI 资源 */
HFONT g_hFont = NULL;             // 主字体（微软雅黑 UI）
HFONT g_hFontBold = NULL;         // 粗体字体（Segoe UI Symbol，用于搜索图标）
ULONG_PTR g_gdiplusToken = 0;     // GDI+ 初始化令牌
WNDPROC g_origEditProc = NULL;    // 搜索编辑框原始窗口过程（用于子类化）
HBRUSH g_hBrushAltRow = NULL;     // 奇数行背景画刷（缓存避免重复创建）
HMENU g_hMenuBar = NULL;          // 菜单栏句柄

/* 迅捷搜引擎核心对象 */
xjs_engine* g_engine = NULL;  // 搜索引擎实例
xjs_result* g_result = NULL;  // 搜索结果实例

/* 全局状态变量 */
std::wstring g_lastSearch;              // 上次搜索关键词（用于去重）
std::atomic<bool> g_isScanning{false};    // 是否正在扫描磁盘（原子类型，防止数据竞争）
bool g_isLoading = false;               // 是否正在加载数据库
int g_fileCount = 0;                    // 数据库中的文件总数
std::atomic<int> g_searchFingerprint{-1};  // 当前搜索指纹（用于判断搜索是否过期，线程安全）
CRITICAL_SECTION g_csIcon;              // 图标缓存临界区（保护 g_iconCache）

/* 排序状态 */
int g_sortColumn = -1;                  // 当前排序列（-1 表示未排序）
bool g_sortAscending = true;            // 是否升序排序

/* 搜索结果变化节流标志 */
std::atomic<bool> g_searchChanged{false};  // 由 SearchChangeCallback 设置，定时器中消费（线程安全）

/* 搜索历史记录时间戳 */
std::chrono::steady_clock::time_point g_lastSearchTime;  // 上次搜索时间
std::wstring g_pendingSearchText;  // 待保存的搜索词

/* 【新增】扫描耗时记录 */
int g_scanElapsedMs = 0;              // 扫描耗时（毫秒）

/* 【新增】抑制搜索标志，防止 SetWindowTextW 触发 CBN_EDITCHANGE */
bool g_suppressSearch = false;

/* 图标缓存（LRU 策略，上限 ICON_CACHE_MAX） */
#define ICON_CACHE_MAX 500
std::unordered_map<int, HICON> g_iconCache;   // itemIndex -> HICON 映射
std::list<int> g_iconCacheOrder;               // LRU 顺序队列（改用 list，O(1) 淘汰）

/* 系统托盘相关 */
NOTIFYICONDATAW g_nid = {0};           // 托盘图标数据
bool g_inTray = false;                  // 窗口是否在托盘中最小化

/* 任务栏进度条 */


/* 全局热键配置（可自定义，保存到 INI 文件） */
UINT g_hotKeyMod = MOD_CONTROL | MOD_ALT;  // 热键修饰符（默认 Ctrl+Alt）
UINT g_hotKeyVk = 'F';                     // 热键虚拟键码（默认 F）

/* 前向声明（解决函数定义顺序依赖） */
std::wstring GetExeDir();
std::wstring GetConfigPath();
void DoSearch();
BOOL SafeDoSearch();
std::wstring HotKeyToString(UINT mod, UINT vk);
std::wstring Utf8ToUtf16(const char* str);
void LoadHotKeyConfig();
void SaveHotKeyConfig();
void LoadWindowConfig(int& x, int& y, int& w, int& h, bool& maximized);
void SaveWindowConfig(int x, int y, int w, int h, bool maximized);
void LoadColumnWidths(int widths[5]);
void SaveColumnWidths(int widths[5]);
void RegisterShowHotKey(HWND hwnd);
void ShowHotKeyDialog(HWND hwnd);
void ShowAboutDialog(HWND hwnd);
void RestoreWindowFromTray(HWND hwnd);
BOOL IsRunningAsAdmin();
void ElevateAndRestart();

/* 【新增】安全设置状态栏文本 */
inline void SafeSetStatusText(const wchar_t* text) {
    if (g_hStatusBar && IsWindow(g_hStatusBar)) {
        SetWindowTextW(g_hStatusBar, text);
    }
}
BOOL IsAutoStartEnabled();
void SetAutoStart(BOOL enable);

struct ScanProgressData { char driveLetter; int enumeratedCount; int totalCount; };
struct ScanCompleteData { int fileCount; int elapsedMs; };
struct SearchCompleteData { int resultCount; int elapsedMs; };
struct IconReadyData { int itemIndex; int searchFingerprint; const void* iconData; int iconLength; };

/* ========== PNG 转 HICON 工具函数 ==========
 * 将迅捷搜返回的 PNG 图标数据转换为 Windows HICON 句柄
 * 使用 GDI+ 解码 PNG，再转换为 HICON 供列表绘制使用
 * 【修复】检查 GDI+ 是否已初始化 */
HICON PngToHICON(const void* pngData, int pngLen) {
    if (!pngData || pngLen <= 0) return NULL;
    /* 【修复】GDI+ 未初始化时直接返回 NULL */
    if (g_gdiplusToken == 0) return NULL;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pngLen);
    if (!hMem) return NULL;
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, pngData, pngLen);
    GlobalUnlock(hMem);
    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK) {
        GlobalFree(hMem);
        return NULL;
    }
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    HICON hIcon = NULL;
    if (bmp && bmp->GetHICON(&hIcon) == Gdiplus::Ok) {
        delete bmp;
        return hIcon;
    }
    delete bmp;
    return NULL;
}

/* ========== 热键配置读写 ========== */

/* 获取配置文件路径（与 exe 同目录的 xjs_config.ini） */
std::wstring GetConfigPath() {
    return GetExeDir() + L"\\xjs_config.ini";
}

/* 将热键转换为可读字符串，例如 "Ctrl+Alt+F" */
std::wstring HotKeyToString(UINT mod, UINT vk) {
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl+";
    if (mod & MOD_ALT) s += L"Alt+";
    if (mod & MOD_SHIFT) s += L"Shift+";
    if (vk >= 'A' && vk <= 'Z') {
        s += (wchar_t)vk;
    } else {
        wchar_t keyName[64] = {0};
        UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (GetKeyNameTextW(scanCode << 16, keyName, 64) > 0)
            s += keyName;
        else {
            wchar_t buf[8]; _snwprintf(buf, 8, L"0x%02X", vk); s += buf;
        }
    }
    return s;
}

/* 从 INI 文件加载热键配置，失败则使用默认值 Ctrl+Alt+F */
void LoadHotKeyConfig() {
    std::wstring iniPath = GetConfigPath();
    g_hotKeyMod = GetPrivateProfileIntW(L"HotKey", L"Mod", MOD_CONTROL | MOD_ALT, iniPath.c_str());
    g_hotKeyVk = (UINT)GetPrivateProfileIntW(L"HotKey", L"Vk", 'F', iniPath.c_str());
}

/* 保存热键配置到 INI 文件 */
void SaveHotKeyConfig() {
    std::wstring iniPath = GetConfigPath();
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%u", g_hotKeyMod);
    WritePrivateProfileStringW(L"HotKey", L"Mod", buf, iniPath.c_str());
    _snwprintf(buf, 16, L"%u", g_hotKeyVk);
    WritePrivateProfileStringW(L"HotKey", L"Vk", buf, iniPath.c_str());
}

/* 从 INI 加载窗口位置和大小，带有效性检查 */
void LoadWindowConfig(int& x, int& y, int& w, int& h, bool& maximized) {
    std::wstring iniPath = GetConfigPath();
    /* 读取原始值 */
    x = GetPrivateProfileIntW(L"Window", L"X", CW_USEDEFAULT, iniPath.c_str());
    y = GetPrivateProfileIntW(L"Window", L"Y", CW_USEDEFAULT, iniPath.c_str());
    w = GetPrivateProfileIntW(L"Window", L"Width", 1000, iniPath.c_str());
    h = GetPrivateProfileIntW(L"Window", L"Height", 700, iniPath.c_str());
    maximized = GetPrivateProfileIntW(L"Window", L"Maximized", 0, iniPath.c_str()) != 0;
    /* 【修复】检查窗口位置是否有效（排除最小化时的负值和超出屏幕范围） */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    /* 如果位置在屏幕外或尺寸异常，使用默认值 */
    if (x < -10000 || y < -10000 || x > screenW || y > screenH || w < 200 || h < 100) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
        w = 1000;
        h = 700;
        maximized = false;
    }
}

/* 保存窗口位置和大小到 INI */
void SaveWindowConfig(int x, int y, int w, int h, bool maximized) {
    std::wstring iniPath = GetConfigPath();
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%d", x);
    WritePrivateProfileStringW(L"Window", L"X", buf, iniPath.c_str());
    _snwprintf(buf, 16, L"%d", y);
    WritePrivateProfileStringW(L"Window", L"Y", buf, iniPath.c_str());
    _snwprintf(buf, 16, L"%d", w);
    WritePrivateProfileStringW(L"Window", L"Width", buf, iniPath.c_str());
    _snwprintf(buf, 16, L"%d", h);
    WritePrivateProfileStringW(L"Window", L"Height", buf, iniPath.c_str());
    WritePrivateProfileStringW(L"Window", L"Maximized", maximized ? L"1" : L"0", iniPath.c_str());
}

/* 列宽配置键名和默认值（循环化配置读写） */
static const wchar_t* g_colKeys[] = { L"Name", L"Path", L"Size", L"Time", L"Type" };
static const int g_colDefWidths[] = { 300, 250, 80, 120, 80 };

/* 搜索历史最大数量 */
#define MAX_SEARCH_HISTORY 20

/* 历史记录数据（改用 vector 存储，便于管理） */
std::vector<std::wstring> g_searchHistory;

/* 从 INI 加载搜索历史到 vector */
void LoadSearchHistory(HWND hCombo) {
    g_searchHistory.clear();
    std::wstring iniPath = GetConfigPath();
    wchar_t buf[1024];
    for (int i = 0; i < MAX_SEARCH_HISTORY; i++) {
        wchar_t key[16];
        _snwprintf(key, 16, L"History%d", i);
        GetPrivateProfileStringW(L"SearchHistory", key, L"", buf, 1024, iniPath.c_str());
        if (buf[0] != L'\0') {
            g_searchHistory.push_back(buf);
        }
    }
}

/* 保存搜索历史到 INI */
void SaveSearchHistory() {
    std::wstring iniPath = GetConfigPath();
    int count = (int)g_searchHistory.size();
    if (count > MAX_SEARCH_HISTORY) count = MAX_SEARCH_HISTORY;
    for (int i = 0; i < count; i++) {
        wchar_t key[16];
        _snwprintf(key, 16, L"History%d", i);
        WritePrivateProfileStringW(L"SearchHistory", key, g_searchHistory[i].c_str(), iniPath.c_str());
    }
    /* 清空多余的条目 */
    for (int i = count; i < MAX_SEARCH_HISTORY; i++) {
        wchar_t key[16];
        _snwprintf(key, 16, L"History%d", i);
        WritePrivateProfileStringW(L"SearchHistory", key, L"", iniPath.c_str());
    }
}

/* 添加搜索词到历史记录（去重，置顶） */
void AddSearchToHistory(const wchar_t* text) {
    if (!text || text[0] == L'\0') return;
    /* 查找是否已存在 */
    auto it = std::find(g_searchHistory.begin(), g_searchHistory.end(), text);
    if (it != g_searchHistory.end()) {
        /* 已存在，先删除 */
        g_searchHistory.erase(it);
    }
    /* 插入到顶部 */
    g_searchHistory.insert(g_searchHistory.begin(), text);
    /* 限制数量 */
    while (g_searchHistory.size() > MAX_SEARCH_HISTORY) {
        g_searchHistory.pop_back();
    }
}

/* 从 INI 加载列宽配置 */
void LoadColumnWidths(int widths[5]) {
    std::wstring iniPath = GetConfigPath();
    for (int i = 0; i < 5; i++)
        widths[i] = GetPrivateProfileIntW(L"Columns", g_colKeys[i], g_colDefWidths[i], iniPath.c_str());
}

/* 保存列宽配置到 INI */
void SaveColumnWidths(int widths[5]) {
    std::wstring iniPath = GetConfigPath();
    wchar_t buf[16];
    for (int i = 0; i < 5; i++) {
        _snwprintf(buf, 16, L"%d", widths[i]);
        WritePrivateProfileStringW(L"Columns", g_colKeys[i], buf, iniPath.c_str());
    }
}

/* 注册全局热键（先注销旧的再注册新的） */
void RegisterShowHotKey(HWND hwnd) {
    UnregisterHotKey(hwnd, ID_HOTKEY_SHOW);
    RegisterHotKey(hwnd, ID_HOTKEY_SHOW, g_hotKeyMod, g_hotKeyVk);
}

/* ========== 热键设置对话框 ========== */

/* 热键设置对话框的窗口过程，动态创建控件 */
LRESULT CALLBACK HotKeyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND s_hHotKey = NULL;
    static HWND s_hHint = NULL;
    switch (msg) {
        case WM_CREATE: {
            RECT rcOwner;
            GetWindowRect(GetParent(hDlg), &rcOwner);
            int w = 290, h = 175;
            int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
            int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
            SetWindowPos(hDlg, NULL, x, y, w, h, SWP_NOZORDER);
            HWND hLabel = CreateWindowExW(0, L"STATIC", L"请按下新的热键组合：",
                WS_VISIBLE | WS_CHILD | SS_LEFT, 15, 15, 250, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            s_hHotKey = CreateWindowExW(0, HOTKEY_CLASS, L"",
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP,
                15, 42, 250, 24, hDlg, (HMENU)ID_HOTKEY_CTRL, NULL, NULL);
            SendMessageW(s_hHotKey, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessageW(s_hHotKey, HKM_SETRULES, HKCOMB_NONE | HKCOMB_S, 0);
            WORD hkMod = 0;
            if (g_hotKeyMod & MOD_CONTROL) hkMod |= HOTKEYF_CONTROL;
            if (g_hotKeyMod & MOD_ALT) hkMod |= HOTKEYF_ALT;
            if (g_hotKeyMod & MOD_SHIFT) hkMod |= HOTKEYF_SHIFT;
            SendMessageW(s_hHotKey, HKM_SETHOTKEY, MAKEWORD(g_hotKeyVk, hkMod), 0);
            std::wstring hint = L"当前热键: " + HotKeyToString(g_hotKeyMod, g_hotKeyVk);
            s_hHint = CreateWindowExW(0, L"STATIC", hint.c_str(),
                WS_VISIBLE | WS_CHILD | SS_LEFT, 15, 74, 250, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(s_hHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            HWND hBtnOK = CreateWindowExW(0, L"BUTTON", L"确定",
                WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP,
                80, 104, 80, 28, hDlg, (HMENU)IDOK, NULL, NULL);
            SendMessageW(hBtnOK, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"取消",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                180, 104, 80, 28, hDlg, (HMENU)IDCANCEL, NULL, NULL);
            SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                DWORD hk = (DWORD)SendMessageW(s_hHotKey, HKM_GETHOTKEY, 0, 0);
                BYTE vk = LOBYTE(LOWORD(hk));
                BYTE modFlags = HIBYTE(LOWORD(hk));
                UINT newMod = 0;
                if (modFlags & HOTKEYF_CONTROL) newMod |= MOD_CONTROL;
                if (modFlags & HOTKEYF_ALT) newMod |= MOD_ALT;
                if (modFlags & HOTKEYF_SHIFT) newMod |= MOD_SHIFT;
                if (newMod == 0 && vk == 0) {
                    MessageBoxW(hDlg, L"请输入一个有效的热键组合", L"提示", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                g_hotKeyMod = newMod;
                g_hotKeyVk = vk;
                RegisterShowHotKey(g_hMainWnd);
                SaveHotKeyConfig();
                std::wstring tip = L"热键已设置为: " + HotKeyToString(g_hotKeyMod, g_hotKeyVk);
                SetWindowTextW(g_hStatusBar, tip.c_str());
                DestroyWindow(hDlg);
                return 0;
            } else if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hDlg);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

/* 显示热键设置对话框（动态创建模态对话框） */
void ShowHotKeyDialog(HWND hwnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HotKeyDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"HotKeyDlgClass";
        if (!RegisterClassExW(&wc)) {
            MessageBoxW(hwnd, L"注册热键对话框类失败", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
        registered = true;
    }
    RECT rcOwner;
    GetWindowRect(hwnd, &rcOwner);
    int w = 290, h = 175;
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"HotKeyDlgClass", L"热键设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        hwnd, NULL, hInst, NULL);
    if (!hDlg) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        _snwprintf(msg, 256, L"创建热键对话框失败，错误码: %lu", err);
        MessageBoxW(hwnd, msg, L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    EnableWindow(hwnd, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    MSG msg;
    while (IsWindow(hDlg)) {
        /* 【修复】正确处理 GetMessageW 返回值 */
        BOOL ret = GetMessageW(&msg, NULL, 0, 0);
        if (ret == 0 || ret == -1) break;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hwnd, TRUE);
    SetForegroundWindow(hwnd);
}

/* ========== 关于对话框 ========== */

/* 关于对话框的窗口过程 */
LRESULT CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* 【修复】使用静态变量保存字体句柄，在 WM_DESTROY 中释放防止 GDI 泄漏 */
    static HFONT s_hTitleFont = NULL;
    static HFONT s_hNormFont = NULL;
    switch (msg) {
        case WM_CREATE: {
            RECT rcOwner;
            GetWindowRect(GetParent(hDlg), &rcOwner);
            int w = 360, h = 340;  /* 【调整】增加高度以容纳演示程序说明 */
            int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
            int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
            SetWindowPos(hDlg, NULL, x, y, w, h, SWP_NOZORDER);
            s_hTitleFont = CreateFontW(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑 UI");
            HWND hTitle = CreateWindowExW(0, L"STATIC", L"迅捷搜",
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 20, 300, 30,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)s_hTitleFont, TRUE);
            s_hNormFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑 UI");
            HWND hVer = CreateWindowExW(0, L"STATIC", L"基于迅捷搜引擎的桌面文件搜索程序",
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 55, 300, 22,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hVer, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hGithub = CreateWindowExW(0, WC_LINK,
                L"<A href=\"https://github.com/Winlpl/xunjieso\">GitHub: github.com/Winlpl/xunjieso</A>",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP,
                30, 85, 300, 22, hDlg, (HMENU)ID_ABOUT_LINK_GITHUB, NULL, NULL);
            SendMessageW(hGithub, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hWebsite = CreateWindowExW(0, WC_LINK,
                L"<A href=\"https://www.xunjieso.com/\">官网: www.xunjieso.com</A>",
                WS_VISIBLE | WS_CHILD | WS_TABSTOP,
                30, 112, 300, 22, hDlg, (HMENU)ID_ABOUT_LINK_WEBSITE, NULL, NULL);
            SendMessageW(hWebsite, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            const char* ver = xjs_GetVersion();
            std::wstring verStr = L"引擎版本: " + (ver ? Utf8ToUtf16(ver) : L"未知");
            HWND hEngVer = CreateWindowExW(0, L"STATIC", verStr.c_str(),
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 142, 300, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hEngVer, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hAI = CreateWindowExW(0, L"STATIC", L"本程序使用 GLM5.1/Kimi2.5 AI 开发",
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 165, 300, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hAI, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hLib = CreateWindowExW(0, L"STATIC", L"使用了: picojson.h",
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 188, 300, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hLib, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hDemo = CreateWindowExW(0, L"STATIC", L"本程序为 xunjieso 引擎演示程序(已开源)",
                WS_VISIBLE | WS_CHILD | SS_CENTER, 30, 211, 300, 20,
                hDlg, NULL, NULL, NULL);
            SendMessageW(hDemo, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            HWND hBtnOK = CreateWindowExW(0, L"BUTTON", L"确定",
                WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_TABSTOP,
                140, 243, 80, 28, hDlg, (HMENU)IDOK, NULL, NULL);
            SendMessageW(hBtnOK, WM_SETFONT, (WPARAM)s_hNormFont, TRUE);
            return 0;
        }
        case WM_DESTROY:
            /* 【修复】释放 GDI 字体对象，防止内存泄漏 */
            if (s_hTitleFont) { DeleteObject(s_hTitleFont); s_hTitleFont = NULL; }
            if (s_hNormFont) { DeleteObject(s_hNormFont); s_hNormFont = NULL; }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hDlg);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            NMHDR* pnm = (NMHDR*)lParam;
            if (pnm->code == NM_CLICK || pnm->code == NM_RETURN) {
                NMLINK* pLink = (NMLINK*)lParam;
                ShellExecuteW(NULL, L"open", pLink->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        }
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

/* 显示关于对话框 */
void ShowAboutDialog(HWND hwnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = AboutDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"AboutDlgClass";
        if (!RegisterClassExW(&wc)) {
            MessageBoxW(hwnd, L"注册关于对话框类失败", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
        registered = true;
    }
    RECT rcOwner;
    GetWindowRect(hwnd, &rcOwner);
    int w = 360, h = 340;  /* 【调整】增加高度以容纳演示程序说明 */
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"AboutDlgClass", L"关于 迅捷搜",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h,
        hwnd, NULL, hInst, NULL);
    if (!hDlg) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        _snwprintf(msg, 256, L"创建关于对话框失败，错误码: %lu", err);
        MessageBoxW(hwnd, msg, L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    EnableWindow(hwnd, FALSE);
    ShowWindow(hDlg, SW_SHOW);
    MSG msg;
    while (IsWindow(hDlg)) {
        /* 【修复】正确处理 GetMessageW 返回值 */
        BOOL ret = GetMessageW(&msg, NULL, 0, 0);
        if (ret == 0 || ret == -1) break;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(hwnd, TRUE);
    SetForegroundWindow(hwnd);
}

/* ========== 管理员权限检查与提权 ========== */
BOOL IsRunningAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            fIsRunAsAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return fIsRunAsAdmin;
}

/* 提权并重启程序，如果用户拒绝 UAC 则提示 */
void ElevateAndRestart() {
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, MAX_PATH)) {
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = szPath;
        sei.nShow = SW_NORMAL;
        if (!ShellExecuteExW(&sei)) {
            MessageBoxW(NULL, L"此程序需要管理员权限才能扫描所有文件。", L"权限不足", MB_OK | MB_ICONERROR);
        }
        ExitProcess(0);
    }
}

/* ========== 开机自启动设置 ========== */

/* 检查是否已设置开机自启动
 * 通过检查注册表 Run 键是否存在本程序条目 */
BOOL IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                      0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH];
        DWORD bufSize = sizeof(buf);
        DWORD type;
        LONG result = RegQueryValueExW(hKey, L"XunJieSo", NULL, &type, (LPBYTE)buf, &bufSize);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return FALSE;
}

/* 设置或取消开机自启动
 * enable: TRUE=启用自启动, FALSE=禁用自启动
 * 将程序路径写入或删除注册表 Run 键 */
void SetAutoStart(BOOL enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            RegSetValueExW(hKey, L"XunJieSo", 0, REG_SZ, (LPBYTE)exePath, 
                          (DWORD)(wcslen(exePath) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"XunJieSo");
        }
        RegCloseKey(hKey);
    }
}

/* ========== 编码转换工具函数 ========== */

/* UTF-8 转 UTF-16（宽字符），用于将迅捷搜返回的 UTF-8 字符串转为 Windows 宽字符 */
std::wstring Utf8ToUtf16(const char* str) {
    if (!str) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], len);
    return result;
}

/* UTF-16 转 UTF-8，用于将 Windows 宽字符转为迅捷搜所需的 UTF-8 字符串 */
std::string Utf16ToUtf8(const wchar_t* str) {
    if (!str) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, str, -1, &result[0], len, NULL, NULL);
    return result;
}

/* 获取窗口文本的便捷封装 */
std::wstring GetWindowTextStr(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len == 0) return L"";
    std::wstring text(len + 1, 0);
    GetWindowTextW(hwnd, &text[0], len + 1);
    text.resize(len);
    return text;
}

/* 获取可执行文件所在目录路径 */
std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\');
    if (p) *p = L'\0';
    return path;
}

/* ========== 迅捷搜 SDK 回调函数定义 ==========
 * 所有回调均在 SDK 内部线程触发，必须通过 PostMessage 转发到 UI 线程处理
 * 回调中禁止对数据库进行写操作 */

/* 根据平台架构确定回调调用约定（x64 下为空，x86 下为 __stdcall） */
#if defined(_WIN64) || defined(__x86_64__)
    #define XJS_CALLBACK
#else
    #define XJS_CALLBACK __stdcall
#endif

/* 回调类型 4：枚举进度回调，每隔 50ms 触发一次
 * 返回 0 继续遍历，非 0 中止 */
INT XJS_CALLBACK EnumProgressCallback(void* userData, xjs_engine* engine, const char* driveLetter, int enumeratedCount, int totalCount) {
    if (g_hMainWnd) {
        ScanProgressData* data = new ScanProgressData();
        data->driveLetter = driveLetter[0];
        data->enumeratedCount = enumeratedCount;
        data->totalCount = totalCount;
        PostMessage(g_hMainWnd, WM_SCAN_PROGRESS, (WPARAM)data, 0);
    }
    return 0;
}

/* 回调类型 2：数据库加载完成回调 */
void XJS_CALLBACK LoadCompleteCallback(void* userData, xjs_engine* engine, int fileCount) {
    if (g_hMainWnd) {
        PostMessage(g_hMainWnd, WM_LOAD_COMPLETE, (WPARAM)fileCount, 0);
    }
}

/* 回调类型 3：枚举分区回调，开始扫描某分区时触发
 * 注意：使用 new char[2] 分配字符串，在 WM_SCAN_DRIVE 中用 delete[] 释放 */
void XJS_CALLBACK EnumPartitionCallback(void* userData, xjs_engine* engine, const char* driveLetter) {
    if (g_hMainWnd) {
        char* drive = new char[2];
        drive[0] = driveLetter[0];
        drive[1] = '\0';
        PostMessage(g_hMainWnd, WM_SCAN_DRIVE, (WPARAM)drive, 0);
    }
}

/* 回调类型 5：所有分区枚举完成回调 */
void XJS_CALLBACK EnumCompleteCallback(void* userData, xjs_engine* engine, int elapsedMs) {
    g_isScanning = false;
    if (g_hMainWnd) {
        ScanCompleteData* data = new ScanCompleteData();
        data->fileCount = xjs_db_GetFileCount(engine);
        data->elapsedMs = elapsedMs;
        PostMessage(g_hMainWnd, WM_SCAN_COMPLETE, (WPARAM)data, 0);
    }
}

/* 回调类型 4：搜索完成回调（result 对象的回调）
 * discarded=TRUE 表示该搜索已被新搜索取代，应忽略 */
int XJS_CALLBACK SearchCompleteCallback(void* userData, xjs_engine* engine, xjs_result* result, int searchFingerprint, const char* keyword, BOOL discarded) {
    if (g_hMainWnd && !discarded) {
        SearchCompleteData* data = new SearchCompleteData();
        data->resultCount = xjs_result_GetCount(result);
        data->elapsedMs = xjs_result_GetElapsed(result);
        PostMessage(g_hMainWnd, WM_SEARCH_COMPLETE, (WPARAM)data, 0);
    }
    return 0;
}

/* 回调类型 10：搜索结果变化回调，异步线程触发
 * 采用节流策略：仅设置标志位，由定时器统一更新 UI，避免频繁刷新 */
int XJS_CALLBACK SearchChangeCallback(void* userData, xjs_engine* engine, xjs_result* result, BOOL resetCount) {
    g_searchChanged = true;
    return 0;
}

/* 回调类型 11：图标绘制回调，异步线程触发
 * 当图标首次获取时，SDK 先返回临时图标，然后异步触发此回调提供最终图标
 * 注意：iconData 需要深拷贝，因为回调返回后 SDK 可能释放该内存 */
void XJS_CALLBACK DrawIconCallback(void* userData, xjs_engine* engine, xjs_result* result, int searchFingerprint, int id, int itemIndex, const void* iconData, int iconLength) {
    if (g_hMainWnd && iconData && iconLength > 0) {
        IconReadyData* data = new IconReadyData();
        data->itemIndex = itemIndex;
        data->searchFingerprint = searchFingerprint;
        data->iconLength = iconLength;
        data->iconData = new char[iconLength];
        memcpy((void*)data->iconData, iconData, iconLength);
        PostMessage(g_hMainWnd, WM_ICON_READY, (WPARAM)data, 0);
    }
}

/* ========== 搜索核心逻辑 ========== */

/* 【封装】安全执行搜索：自动创建结果对象（如果为NULL），检查扫描状态
 * 查询完成后清空旧的图标缓存，防止新旧结果图标错位
 * 返回值：TRUE=搜索已触发，FALSE=未搜索（正在扫描或无法创建结果对象） */
BOOL SafeDoSearch() {
    /* 检查是否在扫描状态 */
    /* 【修复】检查引擎是否为空 */
    if (!g_engine) {
        return FALSE;
    }
    
    if (g_isScanning) {
        return FALSE;
    }
    
    /* 检查结果对象，如果为NULL则创建 */
    if (!g_result) {
        g_result = xjs_result_Create(g_engine);
        if (g_result) {
            xjs_result_SetCallback(g_result, 4, (const void*)SearchCompleteCallback, NULL);
            xjs_result_SetCallback(g_result, 10, (const void*)SearchChangeCallback, NULL);
            xjs_result_SetCallback(g_result, 11, (const void*)DrawIconCallback, NULL);
        }
    }
    
    if (!g_result) {
        return FALSE;
    }
    
    std::wstring searchText = GetWindowTextStr(g_hSearchEdit);
    std::string query = Utf16ToUtf8(searchText.c_str());
    int mode = (int)SendMessageW(g_hModeCombo, CB_GETCURSEL, 0, 0);
    
    int selFilter = (int)SendMessageW(g_hFilterCombo, CB_GETCURSEL, 0, 0);
    if (selFilter != CB_ERR) {
        /* 【修复】动态分配缓冲区防止溢出 */
        int len = (int)SendMessageW(g_hFilterCombo, CB_GETLBTEXTLEN, selFilter, 0);
        if (len > 0 && len < 1024) {  /* 限制最大长度 */
            std::wstring buf(len + 1, L'\0');
            SendMessageW(g_hFilterCombo, CB_GETLBTEXT, selFilter, (LPARAM)buf.data());
            std::string filterName = Utf16ToUtf8(buf.c_str());
            xjs_result_SetSelectedFilter(g_result, filterName.c_str());
        }
    }

    int newFingerprint = xjs_result_Query(g_result, query.c_str(), mode, FALSE);
    if (newFingerprint != -1) {
        g_searchFingerprint = newFingerprint;
        // 【修复】指纹更新，清空旧的 HICON 绘制缓存，防止张冠李戴
        // 将 DestroyIcon 移到临界区外，避免死锁
        std::vector<HICON> iconsToDestroy;
        EnterCriticalSection(&g_csIcon);
        for (auto& pair : g_iconCache) iconsToDestroy.push_back(pair.second);
        g_iconCache.clear();
        g_iconCacheOrder.clear();
        LeaveCriticalSection(&g_csIcon);
        for (HICON h : iconsToDestroy) DestroyIcon(h);
    }
    return TRUE;
}

/* 执行搜索（兼容旧代码，直接调用 SafeDoSearch） */
void DoSearch() {
    SafeDoSearch();
}

/* ========== 状态栏更新 ========== */

/* 根据引擎状态更新状态栏文本
 * 状态码：0=就绪, 1=加载中, 2=保存中, 3=扫描中
 * 【修复】扫描期间不更新状态栏，避免与 WM_SCAN_PROGRESS 冲突导致闪烁
 * 【新增】显示数据库内存占用，使用 SDK 自带的 xjs_util_FormatFileSize */
void UpdateStatus() {
    if (!g_engine) return;
    int state = xjs_db_GetEngineState(g_engine);
    g_fileCount = xjs_db_GetFileCount(g_engine);
    long long memSize = xjs_db_GetMemorySize(g_engine);
    wchar_t status[256];
    switch (state) {
        case 0:
            if (g_isScanning || g_isLoading) {
                g_isScanning = false;
                g_isLoading = false;
                std::wstring memStr = Utf8ToUtf16(xjs_util_FormatFileSize(memSize));
                _snwprintf(status, 256, L"就绪 - %d 个文件 | 内存: %s", g_fileCount, memStr.c_str());
                SafeSetStatusText(status);
            }
            break;
        case 1:
            g_isLoading = true;
            SafeSetStatusText(L"正在加载数据库...");
            break;
        case 2: SafeSetStatusText(L"正在保存数据库..."); break;
        case 3:
            g_isScanning = true;
            /* 扫描期间由 WM_SCAN_PROGRESS 更新状态栏，这里不更新避免闪烁 */
            break;
        case 4: break;
        case 5: break;
    }
}

/* ========== 字体创建 ========== */

/* 创建应用主字体（微软雅黑 UI） */
HFONT CreateAppFont(HWND hwnd, int size) {
    HDC hdc = GetDC(hwnd);
    int height = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, hdc);
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

/* 创建粗体字体（Segoe UI Symbol，用于搜索图标 ⌕ 的渲染） */
HFONT CreateBoldFont(HWND hwnd, int size) {
    HDC hdc = GetDC(hwnd);
    int height = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, hdc);
    return CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
}

/* ========== 关键词高亮相关 ========== */

/* 解析迅捷搜返回的匹配关键词 JSON 数组
 * 格式示例：["关键词1","关键词2"] */
std::vector<std::wstring> GetMatchKeywords(const char* jsonStr) {
    std::vector<std::wstring> keywords;
    if (!jsonStr || strlen(jsonStr) < 3) return keywords;
    std::string jsonCopy = jsonStr; // 安全拷贝
    picojson::value v;
    std::string err = picojson::parse(v, jsonCopy);
    if (err.empty() && v.is<picojson::array>()) {
        picojson::array& arr = v.get<picojson::array>();
        for (size_t i = 0; i < arr.size(); i++) {
            if (arr[i].is<std::string>()) {
                keywords.push_back(Utf8ToUtf16(arr[i].get<std::string>().c_str()));
            }
        }
    }
    return keywords;
}

/* 高亮文本段，标记哪些部分是关键词 */
struct HighlightSegment {
    std::wstring text;     // 文本内容
    bool isKeyword;        // 是否为关键词（需要高亮显示）
};

/* 构建高亮文本段列表：将文本按关键词位置切分为多个段
 * 每段标记是否为关键词，用于 OnCustomDraw 中分色绘制 */
std::vector<HighlightSegment> BuildHighlightSegments(const std::wstring& text, const std::vector<std::wstring>& keywords) {
    std::vector<HighlightSegment> segments;
    if (text.empty()) return segments;
    if (keywords.empty()) {
        segments.push_back({text, false});
        return segments;
    }
    std::vector<bool> mask(text.size(), false);
    for (const auto& kw : keywords) {
        if (kw.empty()) continue;
        size_t pos = 0;
        while ((pos = text.find(kw, pos)) != std::wstring::npos) {
            for (size_t j = 0; j < kw.size() && pos + j < mask.size(); j++)
                mask[pos + j] = true;
            pos += kw.size();
        }
    }
    std::wstring cur;
    bool curIsKey = mask[0];
    for (size_t i = 0; i < text.size(); i++) {
        if (mask[i] != curIsKey) {
            if (!cur.empty()) segments.push_back({cur, curIsKey});
            cur = text[i];
            curIsKey = mask[i];
        } else {
            cur += text[i];
        }
    }
    if (!cur.empty()) segments.push_back({cur, curIsKey});
    return segments;
}

/* ========== 列排序相关 ========== */

/* 排序字段名，与 ListView 列索引一一对应 */
static const char* g_sortFields[] = {"文件名", "文件夹", "文件大小", "修改时间", "文件类型"};

/* 列头点击排序处理：点击同一列切换升降序，点击不同列默认升序
 * 【修复】扫描期间禁止排序，避免数据不一致 */
void OnColumnClick(LPNMLISTVIEW pLV) {
    /* 【修复】检查引擎和结果对象是否为空 */
    if (!g_engine || !g_result) {
        return;
    }
    /* 扫描期间禁止排序 */
    if (g_isScanning) {
        MessageBoxW(g_hMainWnd, L"正在扫描磁盘，请稍后再试。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int col = pLV->iSubItem;
    if (col < 0 || col > 4) return;
    if (g_sortColumn == col) {
        g_sortAscending = !g_sortAscending;
    } else {
        g_sortColumn = col;
        g_sortAscending = true;
    }
    xjs_result_SetSortField(g_result, g_sortFields[col], g_sortAscending ? TRUE : FALSE);
    DoSearch();
    HWND hdr = ListView_GetHeader(g_hResultList);
    for (int i = 0; i < 5; i++) {
        HDITEMW hdi = {0};
        hdi.mask = HDI_FORMAT;
        Header_GetItem(hdr, i, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == col) {
            hdi.fmt |= g_sortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(hdr, i, &hdi);
    }
}

/* ========== 筛选器下拉框初始化 ========== */

/* 从引擎获取所有文件类型筛选分类，填充到下拉框 */
void InitFilterCombo() {
    SendMessageW(g_hFilterCombo, CB_RESETCONTENT, 0, 0);
    /* 【修复】检查引擎和结果对象是否为空 */
    if (!g_engine || !g_result) return;
    
    std::string filterJson = xjs_result_GetAllFilter(g_result); // 安全拷贝
    if (filterJson.empty() || filterJson.length() < 3) return;
    
    picojson::value v;
    std::string err = picojson::parse(v, filterJson);
    if (!err.empty() || !v.is<picojson::array>()) return;
    
    std::string keyName = Utf16ToUtf8(L"名称"); // 防止源码 GBK 编码导致匹配不到 JSON 键

    picojson::array& arr = v.get<picojson::array>();
    for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i].is<picojson::object>()) {
            picojson::object& obj = arr[i].get<picojson::object>();
            if (obj.count(keyName) && obj[keyName].is<std::string>()) {
                std::wstring name = Utf8ToUtf16(obj[keyName].get<std::string>().c_str());
                SendMessageW(g_hFilterCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            }
        }
    }
    
    std::string currentFilter = xjs_result_GetSelectedFilter(g_result); // 安全拷贝
    std::wstring wCurrentFilter = Utf8ToUtf16(currentFilter.c_str());
    
    int idx = (int)SendMessageW(g_hFilterCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)wCurrentFilter.c_str());
    if (idx != CB_ERR) {
        SendMessageW(g_hFilterCombo, CB_SETCURSEL, idx, 0);
    } else if (SendMessageW(g_hFilterCombo, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(g_hFilterCombo, CB_SETCURSEL, 0, 0);
    }
}

/* ========== 系统托盘功能 ========== */

/* 添加系统托盘图标 */
void TrayAdd(HWND hwnd) {
    /* 【修复】先销毁旧图标防止资源泄漏 */
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_NOTIFY;
    /* 从 xunjieso.ico 加载托盘图标 */
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    HICON hIcon = (HICON)LoadImageW(hInst, L"xunjieso.ico", IMAGE_ICON, 
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    g_nid.hIcon = hIcon ? hIcon : LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"迅捷搜 - 点击还原窗口");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_inTray = true;
}

/* 移除系统托盘图标 */
void TrayRemove() {
    if (g_inTray) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_inTray = false;
    }
    /* 【修复】销毁托盘图标防止资源泄漏 */
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }
}

/* 【新增】从托盘还原窗口的统一处理函数（DRY原则） */
void RestoreWindowFromTray(HWND hwnd) {
    if (!g_result && g_engine) {
        g_result = xjs_result_Create(g_engine);
        /* 【修复】遍历阶段创建结果对象可能返回 nullptr，需要检查 */
        if (!g_result) {
            /* 正在遍历中，无法创建结果对象，仅还原窗口不执行搜索 */
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            SetFocus(g_hSearchEdit);
            return;
        }
        xjs_result_SetCallback(g_result, 4, (const void*)SearchCompleteCallback, NULL);
        xjs_result_SetCallback(g_result, 10, (const void*)SearchChangeCallback, NULL);
        xjs_result_SetCallback(g_result, 11, (const void*)DrawIconCallback, NULL);
        SetWindowTextW(g_hSearchEdit, g_lastSearch.c_str());
        DoSearch();
    }
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    SetFocus(g_hSearchEdit);
}

/* 显示托盘右键菜单 */
void TrayShowMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE, L"还原窗口(&R)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序(&X)");
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == ID_TRAY_RESTORE) {
        /* 【优化】使用统一函数处理托盘还原 */
        RestoreWindowFromTray(hwnd);
    } else if (cmd == ID_TRAY_EXIT) {
        TrayRemove();
        DestroyWindow(hwnd);
    }
}

/* ========== 搜索图标控件窗口过程 ========== */

/* 自绘搜索图标 ⌕ (U+2315) */
LRESULT CALLBACK SearchIconWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(100, 100, 100));
        HFONT oldFont = (HFONT)SelectObject(hdc, g_hFontBold);
        wchar_t ch = L'\u2315';
        SIZE sz;
        GetTextExtentPoint32W(hdc, &ch, 1, &sz);
        TextOutW(hdc, (rc.right - sz.cx) / 2, (rc.bottom - sz.cy) / 2, &ch, 1);
        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ========== 搜索编辑框子类化 ========== */

/* 拦截 ESC（清空搜索框）、Enter（立即搜索）按键 */
LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            /* 【修复】只有有内容时才清空并搜索，避免空搜索导致卡顿 */
            if (GetWindowTextLengthW(hwnd) > 0) {
                SetWindowTextW(hwnd, L"");
            }
            return 0;
        }
        if (wParam == VK_RETURN) {
            g_lastSearch = GetWindowTextStr(hwnd);
            DoSearch();
            return 0;
        }
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

/* ========== 工具栏布局计算 ========== */

/* 根据窗口宽度动态调整搜索框、模式下拉框、筛选下拉框的位置和大小 */
void LayoutToolbar(int clientWidth) {
    int y = TOOLBAR_Y;
    int h = TOOLBAR_H;
    int iconW = 36;
    int comboModeW = 90;
    int comboFilterW = 140;
    int rightMargin = MARGIN;
    int x = MARGIN;
    SetWindowPos(g_hSearchIcon, NULL, x, y, iconW, h, SWP_NOZORDER);
    x += iconW + 2;
    int comboRight = rightMargin + comboFilterW + comboModeW + 8;
    int editW = clientWidth - x - comboRight;
    if (editW < 100) editW = 100;
    SetWindowPos(g_hSearchEdit, NULL, x, y + 2, editW, h - 4, SWP_NOZORDER);
    x += editW + 6;
    SetWindowPos(g_hModeCombo, NULL, x, y, comboModeW, 200, SWP_NOZORDER);
    x += comboModeW + 4;
    SetWindowPos(g_hFilterCombo, NULL, x, y, comboFilterW, 400, SWP_NOZORDER);
}

/* ========== SQL 模板菜单 ========== */

/* SQL 模板数据结构 */
struct SqlTemplate {
    std::wstring title;   // 菜单显示标题
    std::string sql;      // SQL 查询语句（UTF-8）
};

std::vector<SqlTemplate> g_sqlTemplates;       // 所有 SQL 模板

/* 构建 SQL 模板菜单：创建菜单栏，添加4个分类子菜单
 * 分类：聊天工具(12项)、下载(7项)、办公文档(6项)、视频图片(5项)
 * 点击模板时自动填入搜索框并切换到 SQL 模式 */
void BuildSqlTemplates() {
    struct Group { const wchar_t* name; SqlTemplate* items; int count; };
    SqlTemplate tChat[] = {
        {L"微信收发文件", "SELECT * FROM alltable WHERE ParentName LIKE '20%' AND ParentPath LIKE '%\\wxid_%\\Msg\\File\\%' ORDER BY ModTime DESC;"},
        {L"微信图片视频", "SELECT * FROM alltable WHERE IsDir=0 AND (FileType='图片' OR FileType='视频') AND ParentPath LIKE '%\\wxid_%\\Msg\\%' ORDER BY ModTime DESC;"},
        {L"QQ接收文件", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\Tencent Files%\\FileRecv\\%' ORDER BY ModTime DESC;"},
        {L"QQ图片视频", "SELECT * FROM alltable WHERE IsDir=0 AND (FileType='图片' OR FileType='视频') AND ParentPath LIKE '%\\Tencent Files%' ORDER BY ModTime DESC;"},
        {L"TIM接收文件", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\TIM\\FileRecv\\%' ORDER BY ModTime DESC;"},
        {L"企业微信文件", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\WXWork%\\Msg\\File\\%' ORDER BY ModTime DESC;"},
        {L"旺旺/千牛文件", "SELECT * FROM alltable WHERE IsDir=0 AND (ParentPath LIKE '%\\WangWang\\%' OR ParentPath LIKE '%\\AliWorkbench\\%') AND (ParentPath LIKE '%\\FileRecv\\%' OR ParentPath LIKE '%\\File\\%') ORDER BY ModTime DESC;"},
        {L"钉钉下载文件", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\DingTalk\\download\\%' ORDER BY ModTime DESC;"},
        {L"飞书下载文件", "SELECT * FROM alltable WHERE IsDir=0 AND (ParentPath LIKE '%\\Lark\\download\\%' OR ParentPath LIKE '%\\Feishu\\download\\%') ORDER BY ModTime DESC;"},
        {L"Telegram下载", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\Telegram\\download\\%' ORDER BY ModTime DESC;"},
        {L"Zoom录制视频", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\Zoom\\recordings\\%' ORDER BY ModTime DESC;"},
        {L"Teams下载文件", "SELECT * FROM alltable WHERE IsDir=0 AND ParentPath LIKE '%\\Microsoft\\Teams\\Downloads\\%' ORDER BY ModTime DESC;"}
    };
    SqlTemplate tDownload[] = {
        {L"系统下载目录", "SELECT * FROM alltable WHERE ParentName='Downloads' AND ParentPath LIKE '_:\\Users\\%' ORDER BY ModTime DESC;"},
        {L"迅雷下载", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\Thunder\\download\\%' AND Ext NOT IN ('td','cfg') ORDER BY ModTime DESC;"},
        {L"百度网盘下载", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\BaiduNetdiskDownload\\%' ORDER BY ModTime DESC;"},
        {L"阿里云盘下载", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\AliDrive\\Download\\%' ORDER BY ModTime DESC;"},
        {L"夸克网盘下载", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\Quark\\Download\\%' ORDER BY ModTime DESC;"},
        {L"OneDrive文件", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\OneDrive\\%' ORDER BY ModTime DESC;"},
        {L"坚果云文件", "SELECT * FROM alltable WHERE ParentPath LIKE '%\\Nutstore\\%' ORDER BY ModTime DESC;"}
    };
    SqlTemplate tOffice[] = {
        {L"最近的表格", "SELECT * FROM alltable WHERE IsDir=0 AND Ext IN ('xlsx', 'xls', 'csv', 'et') ORDER BY ModTime DESC;"},
        {L"最近的文档", "SELECT * FROM alltable WHERE IsDir=0 AND Ext IN ('docx', 'doc', 'wps', 'rtf') ORDER BY ModTime DESC;"},
        {L"最近的PPT", "SELECT * FROM alltable WHERE IsDir=0 AND Ext IN ('pptx', 'ppt', 'dps') ORDER BY ModTime DESC;"},
        {L"最近的PDF", "SELECT * FROM alltable WHERE IsDir=0 AND Ext='pdf' ORDER BY ModTime DESC;"},
        {L"压缩包文件", "SELECT * FROM alltable WHERE IsDir=0 AND Ext IN ('zip', 'rar', '7z') ORDER BY ModTime DESC;"},
        {L"大压缩包", "SELECT * FROM alltable WHERE IsDir=0 AND Ext IN ('zip', 'rar', '7z') AND Size > '50M' ORDER BY Size DESC;"}
    };
    SqlTemplate tMedia[] = {
        {L"截图录屏", "SELECT * FROM alltable WHERE (FileType='图片' OR FileType='视频') AND (ParentPath LIKE '%\\AppData\\Local\\Packages\\MicrosoftWindows.Client.CBS_%\\TempState\\ScreenSketch\\%' OR ParentPath LIKE '%\\AppData\\Local\\Packages\\Microsoft.ScreenSketch_%\\TempState\\%' OR ParentName = 'Snipaste' OR ParentName = 'Bandicam' OR ParentName = 'JianyingPro' OR ParentName = 'EVs' OR ParentName = 'oCam' OR ParentName = 'KK' OR ParentName = 'ScreenToGif' OR ParentName = 'picgo' OR ParentPath LIKE '%\\ShareX\\Screenshots\\%' OR ParentPath LIKE '%\\FastStone\\FSCapture\\%' OR ParentPath LIKE '%\\Steam\\userdata\\%\\screenshots\\%' OR ParentPath LIKE '%\\Videos\\Captures\\%' OR ParentPath LIKE '%\\Videos\\NVIDIA\\%' OR ParentPath LIKE '%\\Videos\\OBS\\%' OR ParentPath LIKE '%\\Zoom\\recordings\\%' OR ParentPath LIKE '%\\TencentMeeting\\recordings\\%') ORDER BY ModTime DESC;"},
        {L"视频目录汇总", "SELECT ParentPath, COUNT(*) AS cnt FROM alltable WHERE FileType='视频' GROUP BY ParentPath ORDER BY cnt DESC;"},
        {L"最近的图片", "SELECT * FROM alltable WHERE IsDir=0 AND FileType='图片' ORDER BY ModTime DESC;"},
        {L"最近的视频", "SELECT * FROM alltable WHERE IsDir=0 AND FileType='视频' ORDER BY ModTime DESC;"},
        {L"大于1G的视频", "SELECT * FROM alltable WHERE IsDir=0 AND FileType='视频' AND Size > '1G' ORDER BY Size DESC;"}
    };
    Group groups[] = {
        {L"聊天工具", tChat, _countof(tChat)},
        {L"下载", tDownload, _countof(tDownload)},
        {L"办公文档", tOffice, _countof(tOffice)},
        {L"视频图片", tMedia, _countof(tMedia)}
    };
    g_sqlTemplates.clear();
    g_hMenuBar = CreateMenu();
    HMENU hSqlMenu = CreatePopupMenu();
    for (int g = 0; g < _countof(groups); g++) {
        if (g > 0) AppendMenuW(hSqlMenu, MF_SEPARATOR, 0, NULL);
        HMENU hSub = CreatePopupMenu();
        for (int i = 0; i < groups[g].count; i++) {
            int id = ID_SQL_TEMPLATE + (int)g_sqlTemplates.size();
            g_sqlTemplates.push_back(groups[g].items[i]);
            AppendMenuW(hSub, MF_STRING, id, groups[g].items[i].title.c_str());
        }
        AppendMenuW(hSqlMenu, MF_POPUP, (UINT_PTR)hSub, groups[g].name);
    }
    AppendMenuW(g_hMenuBar, MF_POPUP, (UINT_PTR)hSqlMenu, L"SQL模板");
    /* 添加"设置"菜单，包含热键设置、重建索引、开机自启动、关于选项 */
    HMENU hSettingsMenu = CreatePopupMenu();
    AppendMenuW(hSettingsMenu, MF_STRING, ID_MENU_HOTKEY, L"热键设置(&K)...");
    AppendMenuW(hSettingsMenu, MF_STRING, ID_MENU_REBUILD, L"重建索引(&R)");
    /* 【新增】开机自启动选项，带勾选标记 */
    UINT autoStartFlags = MF_STRING;
    if (IsAutoStartEnabled()) {
        autoStartFlags |= MF_CHECKED;
    }
    AppendMenuW(hSettingsMenu, autoStartFlags, ID_MENU_AUTOSTART, L"开机自启动(&S)");
    AppendMenuW(hSettingsMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hSettingsMenu, MF_STRING, ID_MENU_ABOUT, L"关于(&A)...");
    AppendMenuW(g_hMenuBar, MF_POPUP, (UINT_PTR)hSettingsMenu, L"设置");
    SetMenu(g_hMainWnd, g_hMenuBar);
}

/* ========== UI 初始化 ========== */

/* 创建所有子控件：搜索图标、编辑框、模式下拉框、筛选下拉框、列表视图、状态栏
 * 并构建 SQL 模板菜单 */
void InitUI(HWND hwnd) {
    g_hFont = CreateAppFont(hwnd, 9);
    g_hFontBold = CreateBoldFont(hwnd, 11);
    g_hBrushAltRow = CreateSolidBrush(ALT_ROW_COLOR);

    WNDCLASSEXW wci = {0};
    wci.cbSize = sizeof(wci);
    wci.lpfnWndProc = SearchIconWndProc;
    wci.hInstance = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    wci.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wci.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wci.lpszClassName = L"SearchIconCls";
    RegisterClassExW(&wci);
    int y = TOOLBAR_Y, h = TOOLBAR_H;
    g_hSearchIcon = CreateWindowExW(0, L"SearchIconCls", L"",
        WS_VISIBLE | WS_CHILD, MARGIN, y, 36, h, hwnd, NULL, wci.hInstance, NULL);
    /* 【新增】搜索框改为组合下拉框，支持搜索历史 */
    g_hSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 50, y + 2, 420, h + 200, hwnd, (HMENU)ID_SEARCH_EDIT, NULL, NULL);
    SendMessageW(g_hSearchEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hSearchEdit, CB_SETCUEBANNER, 0, (LPARAM)L"输入关键词搜索文件... (Shift+Del 删除历史)");
    /* 加载搜索历史到 vector 和组合框 */
    LoadSearchHistory(g_hSearchEdit);
    for (const auto& item : g_searchHistory) {
        SendMessageW(g_hSearchEdit, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(g_hSearchEdit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
    g_hModeCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 480, y, 90, 200, hwnd, (HMENU)ID_MODE_COMBO, NULL, NULL);
    SendMessageW(g_hModeCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    /* 【优化】移除 SQL 选项，引擎会自动检测 SQL 语句 */
    SendMessageW(g_hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"通配符");
    SendMessageW(g_hModeCombo, CB_ADDSTRING, 0, (LPARAM)L"正则");
    SendMessageW(g_hModeCombo, CB_SETCURSEL, 0, 0);
    g_hFilterCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 578, y, 140, 400, hwnd, (HMENU)ID_FILTER_COMBO, NULL, NULL);
    SendMessageW(g_hFilterCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    g_hResultList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
        WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_OWNERDATA | WS_VSCROLL,
        MARGIN, y + h + 6, 800, 450, hwnd, (HMENU)ID_RESULT_LIST, NULL, NULL);
    ListView_SetExtendedListViewStyle(g_hResultList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    SendMessageW(g_hResultList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    /* 加载保存的列宽配置 */
    int colWidths[5];
    LoadColumnWidths(colWidths);
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    /* 【屏蔽】文件大小和修改时间列，节约内存
     * 如需启用，取消定义 ENABLE_FILE_SIZE_AND_TIME
     */
    #ifdef ENABLE_FILE_SIZE_AND_TIME
    const wchar_t* colNames[] = { L"文件名", L"路径", L"大小", L"修改时间", L"类型" };
    int colCount = 5;
    #else
    const wchar_t* colNames[] = { L"文件名", L"路径", L"类型" };
    int colCount = 3;
    #endif
    for (int i = 0; i < colCount; i++) {
        lvc.pszText = (LPWSTR)colNames[i];
        lvc.cx = colWidths[i] > 0 ? colWidths[i] : g_colDefWidths[i]; /* 使用统一的默认值 */
        lvc.iSubItem = i;
        ListView_InsertColumn(g_hResultList, i, &lvc);
    }
    g_hStatusBar = CreateWindowExW(0, STATUSCLASSNAME, L"初始化中...",
        WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, (HMENU)ID_STATUS_BAR, NULL, NULL);
    SendMessageW(g_hStatusBar, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    RECT rc;
    GetClientRect(hwnd, &rc);
    LayoutToolbar(rc.right);
    BuildSqlTemplates();
}

/* ========== 虚拟列表数据回调 ========== */

/* LVN_GETDISPINFO 处理：为虚拟列表提供按需数据
 * 【修复】使用 ListView 提供的 pszText 缓冲区（cchTextMax 指定大小），避免 static buffer 的并发问题
 * 注意：ListView 为每个子项分配了独立的缓冲区，直接写入即可 */
void OnGetDispInfo(NMLVDISPINFOW* pDispInfo) {
    /* 【修复】检查引擎和结果对象是否为空 */
    if (!g_engine || !g_result || g_searchFingerprint < 0) return;
    int index = pDispInfo->item.iItem;
    if (index < 0) return;
    
    /* 【优化】第0列完全由 OnCustomDraw 自绘，无需在此提供文本，减少跨引擎调用 */
    if (pDispInfo->item.iSubItem == 0) return;
    
    int fileId = xjs_result_GetFileId(g_result, index);
    if (fileId < 0) return;
    
    if ((pDispInfo->item.mask & LVIF_TEXT) && pDispInfo->item.pszText && pDispInfo->item.cchTextMax > 0) {
        std::wstring text;
        switch (pDispInfo->item.iSubItem) {
            case 1:
                text = Utf8ToUtf16(xjs_db_GetParentDirectory(g_engine, fileId));
                break;
            /* 【屏蔽】文件大小和修改时间显示，节约内存
             * 如需启用，取消定义 ENABLE_FILE_SIZE_AND_TIME
             */
            #ifdef ENABLE_FILE_SIZE_AND_TIME
            case 2:
                text = Utf8ToUtf16(xjs_util_FormatFileSize(xjs_db_GetFileSize(g_engine, fileId)));
                break;
            case 3:
                text = Utf8ToUtf16(xjs_util_FormatTimestamp(xjs_db_GetModifyTime(g_engine, fileId)));
                break;
            case 4:
                text = Utf8ToUtf16(xjs_db_GetFileTypeStr(g_engine, fileId));
                break;
            #else
            case 2:
                text = Utf8ToUtf16(xjs_db_GetFileTypeStr(g_engine, fileId));
                break;
            #endif
        }
        /* 安全复制到 ListView 提供的缓冲区 */
        wcsncpy_s(pDispInfo->item.pszText, pDispInfo->item.cchTextMax, text.c_str(), _TRUNCATE);
    }
}

/* ========== 自定义绘制：关键词高亮 + 图标 ========== */

/* NM_CUSTOMDRAW 处理：
 * - CDDS_PREPAINT: 请求行级通知
 * - CDDS_ITEMPREPAINT: 设置行背景色（奇偶行交替）
 * - CDDS_SUBITEM|CDDS_ITEMPREPAINT: 仅对第0列（文件名）进行自定义绘制
 *   包括：背景填充 → 图标绘制 → 关键词高亮文字绘制 */
LRESULT OnCustomDraw(LPNMLVCUSTOMDRAW pcd) {
    switch (pcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            int item = (int)pcd->nmcd.dwItemSpec;
            UINT state = ListView_GetItemState(g_hResultList, item, LVIS_SELECTED | LVIS_FOCUSED);
            if (state & LVIS_SELECTED) {
                pcd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                pcd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
            } else {
                pcd->clrText = GetSysColor(COLOR_WINDOWTEXT);
                pcd->clrTextBk = (item % 2 != 0) ? ALT_ROW_COLOR : GetSysColor(COLOR_WINDOW);
            }
            return CDRF_NEWFONT | CDRF_NOTIFYSUBITEMDRAW;
        }
        case CDDS_SUBITEM | CDDS_ITEMPREPAINT: {
            /* 【修复】检查引擎和结果对象是否为空 */
            if (pcd->iSubItem != 0 || !g_engine || !g_result || g_searchFingerprint < 0)
                return CDRF_DODEFAULT;
            int item = (int)pcd->nmcd.dwItemSpec;
            
            int fileId = xjs_result_GetFileId(g_result, item);
            if (fileId < 0) return CDRF_DODEFAULT;
            
            std::string name = xjs_db_GetName(g_engine, fileId); // 安全拷贝
            std::wstring wname = Utf8ToUtf16(name.c_str());
            std::string matchJson = xjs_result_GetMatchKeywords(g_result, name.c_str()); // 安全拷贝
            std::vector<std::wstring> keywords = GetMatchKeywords(matchJson.c_str());
            
            int iconLen = 0;
            const void* iconData = xjs_result_GetFileIco(g_result, fileId, item, 16, &iconLen);

            HICON hIcon = NULL;
            bool hasIcon = false;
            EnterCriticalSection(&g_csIcon);
            auto it = g_iconCache.find(item);
            if (it != g_iconCache.end()) {
                hIcon = it->second;
                hasIcon = (hIcon != NULL);
                LeaveCriticalSection(&g_csIcon);
            } else {
                LeaveCriticalSection(&g_csIcon);
                if (iconData && iconLen > 0) {
                    hIcon = PngToHICON(iconData, iconLen);
                    if (hIcon) {
                        std::vector<HICON> iconsToDestroy; // 【优化】收集待销毁图标，临界区外销毁
                        EnterCriticalSection(&g_csIcon);
                        if (g_iconCache.size() >= ICON_CACHE_MAX) {
                            for (int i = 0; i < ICON_CACHE_MAX / 4 && !g_iconCacheOrder.empty(); i++) {
                                int old = g_iconCacheOrder.front();
                                g_iconCacheOrder.pop_front(); // O(1) 淘汰
                                auto itOld = g_iconCache.find(old);
                                if (itOld != g_iconCache.end()) {
                                    iconsToDestroy.push_back(itOld->second);
                                    g_iconCache.erase(itOld);
                                }
                            }
                        }
                        g_iconCache[item] = hIcon;
                        g_iconCacheOrder.push_back(item);
                        LeaveCriticalSection(&g_csIcon);
                        // 【关键】在临界区外安全销毁 GDI 对象
                        for (HICON h : iconsToDestroy) {
                            DestroyIcon(h);
                        }
                        hasIcon = true;
                    }
                }
            }
            std::vector<HighlightSegment> segments = BuildHighlightSegments(wname, keywords);
            RECT rc;
            ListView_GetSubItemRect(g_hResultList, item, 0, LVIR_LABEL, &rc);
            HDC hdc = pcd->nmcd.hdc;
            UINT itemState = ListView_GetItemState(g_hResultList, item, LVIS_SELECTED | LVIS_FOCUSED);
            bool selected = (itemState & LVIS_SELECTED) != 0;
            bool oddRow = (item % 2 != 0);
            
            HBRUSH bgBrush;
            if (selected)
                bgBrush = (HBRUSH)(COLOR_HIGHLIGHT + 1);
            else if (oddRow)
                bgBrush = g_hBrushAltRow; 
            else
                bgBrush = (HBRUSH)(COLOR_WINDOW + 1);

            FillRect(hdc, &rc, bgBrush);

            SetBkMode(hdc, TRANSPARENT);
            HFONT oldFont = (HFONT)SelectObject(hdc, g_hFont);
            TEXTMETRICW tm;
            GetTextMetricsW(hdc, &tm);
            int yText = rc.top + (rc.bottom - rc.top - tm.tmHeight) / 2;
            int x = rc.left + 4;
            if (hasIcon) {
                if (hIcon) {
                    int iconY = rc.top + (rc.bottom - rc.top - 16) / 2;
                    DrawIconEx(hdc, x, iconY, hIcon, 16, 16, 0, NULL, DI_NORMAL);
                }
                x += 20;
            }
            COLORREF normalColor = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
            COLORREF hlColor = selected ? RGB(255, 220, 100) : HIGHLIGHT_COLOR;
            SIZE sz;
            for (const auto& seg : segments) {
                SetTextColor(hdc, seg.isKeyword ? hlColor : normalColor);
                GetTextExtentPoint32W(hdc, seg.text.c_str(), (int)seg.text.length(), &sz);
                ExtTextOutW(hdc, x, yText, ETO_CLIPPED, &rc, seg.text.c_str(), (UINT)seg.text.length(), NULL);
                x += sz.cx;
            }
            SelectObject(hdc, oldFont);
            if (itemState & LVIS_FOCUSED) DrawFocusRect(hdc, &rc);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

/* ========== 文件操作辅助函数 ========== */

/* 获取指定列表项的完整文件路径 */
std::wstring GetItemFilePath(int item) {
    /* 【修复】检查引擎和结果对象是否为空 */
    if (!g_engine || !g_result || item < 0) return L"";
    int fileId = xjs_result_GetFileId(g_result, item);
    if (fileId < 0) return L"";
    std::string path = xjs_db_GetPath(g_engine, fileId); // 安全拷贝
    return Utf8ToUtf16(path.c_str());
}

/* 获取指定列表项的文件名 */
std::wstring GetItemName(int item) {
    /* 【修复】检查引擎和结果对象是否为空 */
    if (!g_engine || !g_result || item < 0) return L"";
    int fileId = xjs_result_GetFileId(g_result, item);
    if (fileId < 0) return L"";
    std::string name = xjs_db_GetName(g_engine, fileId); // 安全拷贝
    return Utf8ToUtf16(name.c_str());
}

/* 将文本复制到剪贴板（Unicode 格式） */
void CopyToClipboard(const std::wstring& text) {
    if (OpenClipboard(g_hMainWnd)) {
        EmptyClipboard();
        size_t sz = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
        if (hMem) {
            memcpy(GlobalLock(hMem), text.c_str(), sz);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

/* 使用 Shell 关联程序打开文件 */
void OpenFile(const std::wstring& path) {
    if (path.empty()) return;
    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

/* 在资源管理器中打开并选中文件 */
void OpenFolderAndSelect(const std::wstring& path) {
    if (path.empty()) return;
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (pidl) {
        SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
        ILFree(pidl);
    } else {
        std::wstring dir = path.substr(0, path.rfind(L'\\'));
        ShellExecuteW(NULL, L"explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

/* 显示右键上下文菜单：单选时显示打开/打开文件夹/复制路径/复制文件名
 * 多选时显示复制所有路径/复制所有文件名 */
void ShowContextMenu(HWND hwnd, POINT pt, int clickedItem) {
    int selCount = ListView_GetSelectedCount(g_hResultList);
    if (selCount == 0 && clickedItem < 0) return;
    if (clickedItem >= 0 && selCount == 0) {
        ListView_SetItemState(g_hResultList, clickedItem, LVIS_SELECTED, LVIS_SELECTED);
        selCount = 1;
    }
    HMENU hMenu = CreatePopupMenu();
    if (selCount == 1) {
        AppendMenuW(hMenu, MF_STRING, ID_CTX_OPEN, L"打开(&O)");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_OPENDIR, L"打开所在文件夹(&F)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPYPATH, L"复制完整路径(&P)");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPYNAME, L"复制文件名(&N)");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_CTX_PROPERTIES, L"属性(&R)");
    } else {
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPYALLPATH, L"复制所有选中路径(&P)");
        AppendMenuW(hMenu, MF_STRING, ID_CTX_COPYALLNAME, L"复制所有选中文件名(&N)");
    }
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == 0) return;
    /* 【优化】提取公共的 item 获取逻辑 */
    int item = (clickedItem >= 0) ? clickedItem : ListView_GetNextItem(g_hResultList, -1, LVNI_SELECTED);
    switch (cmd) {
        case ID_CTX_OPEN:
            if (item >= 0) OpenFile(GetItemFilePath(item));
            break;
        case ID_CTX_OPENDIR:
            if (item >= 0) OpenFolderAndSelect(GetItemFilePath(item));
            break;
        case ID_CTX_COPYPATH:
            if (item >= 0) CopyToClipboard(GetItemFilePath(item));
            break;
        case ID_CTX_COPYNAME:
            if (item >= 0) CopyToClipboard(GetItemName(item));
            break;
        case ID_CTX_COPYALLPATH: {
            std::wstring allPaths;
            int item = -1;
            while ((item = ListView_GetNextItem(g_hResultList, item, LVNI_SELECTED)) >= 0) {
                if (!allPaths.empty()) allPaths += L"\r\n";
                allPaths += GetItemFilePath(item);
            }
            if (!allPaths.empty()) CopyToClipboard(allPaths);
            break;
        }
        case ID_CTX_COPYALLNAME: {
            std::wstring allNames;
            int item = -1;
            while ((item = ListView_GetNextItem(g_hResultList, item, LVNI_SELECTED)) >= 0) {
                if (!allNames.empty()) allNames += L"\r\n";
                allNames += GetItemName(item);
            }
            if (!allNames.empty()) CopyToClipboard(allNames);
            break;
        }
        case ID_CTX_PROPERTIES:
            /* 显示文件属性对话框 */
            if (item >= 0) {
                std::wstring filePath = GetItemFilePath(item);
                SHELLEXECUTEINFOW sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_INVOKEIDLIST;
                sei.lpVerb = L"properties";
                sei.lpFile = filePath.c_str();
                sei.nShow = SW_SHOWNORMAL;
                ShellExecuteExW(&sei);
            }
            break;
    }
}

/* ========== 主窗口过程 ========== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        /* 窗口创建：初始化临界区、UI、COM、GDI+、搜索引擎、回调、数据库、热键 */
        case WM_CREATE: {
            g_hMainWnd = hwnd;
            InitializeCriticalSection(&g_csIcon);
            InitUI(hwnd);
            /* 先初始化 COM，再创建任务栏接口 */
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            Gdiplus::GdiplusStartupInput si;
            Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, NULL);
            xjs_EnableException(FALSE);// 必须为FALSE,不然可能会捕获到输入法的异常/驱动的异常..
            g_engine = xjs_Create(NULL, NULL);
            if (!g_engine) {
                MessageBoxW(hwnd, L"创建搜索引擎失败", L"错误", MB_OK | MB_ICONERROR);
                PostQuitMessage(0);
                return 0;
            }
            xjs_SetDefaultEngine(g_engine);
            xjs_SetCallback(g_engine, 2, (const void*)LoadCompleteCallback, NULL);
            xjs_SetCallback(g_engine, 3, (const void*)EnumPartitionCallback, NULL);
            xjs_SetCallback(g_engine, 4, (const void*)EnumProgressCallback, NULL);
            xjs_SetCallback(g_engine, 5, (const void*)EnumCompleteCallback, NULL);
            g_result = xjs_result_Create(g_engine);
            xjs_result_SetCallback(g_result, 4, (const void*)SearchCompleteCallback, NULL);
            xjs_result_SetCallback(g_result, 10, (const void*)SearchChangeCallback, NULL);
            xjs_result_SetCallback(g_result, 11, (const void*)DrawIconCallback, NULL);
            
            InitFilterCombo();

            std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
            std::string dbPathUtf8 = Utf16ToUtf8(dbPath.c_str());
            /* 【屏蔽】文件大小和修改时间字段，节约内存
             * 如需启用，取消定义 ENABLE_FILE_SIZE_AND_TIME
             */
            #ifdef ENABLE_FILE_SIZE_AND_TIME
            xjs_db_AddField(g_engine, "文件大小", NULL);
            xjs_db_AddField(g_engine, "修改时间", NULL);
            #endif
            BOOL loadOk = xjs_db_Load(g_engine, dbPathUtf8.c_str(), TRUE);
            if (!loadOk) {
                xjs_db_ScanPath(g_engine, NULL, TRUE);
                SetWindowTextW(g_hStatusBar, L"正在扫描磁盘...");
            } else {
                SetWindowTextW(g_hStatusBar, L"正在加载数据库...");
            }
            
            SetTimer(hwnd, ID_TIMER_UPDATE, 500, NULL);
            /* 注册全局热键（从配置文件加载，默认 Ctrl+Alt+F） */
            LoadHotKeyConfig();
            RegisterShowHotKey(hwnd);
            /* 【优化】程序启动后立即添加托盘图标 */
            TrayAdd(hwnd);
            SetFocus(g_hSearchEdit);
            break;
        }
        /* 窗口大小变化：重新布局工具栏和列表视图 */
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) break;
            RECT rc;
            GetClientRect(hwnd, &rc);
            SendMessageW(g_hStatusBar, WM_SIZE, 0, 0);
            RECT statusRc;
            GetWindowRect(g_hStatusBar, &statusRc);
            int statusHeight = statusRc.bottom - statusRc.top;
            LayoutToolbar(rc.right);
            int listY = TOOLBAR_Y + TOOLBAR_H + 6;
            SetWindowPos(g_hResultList, NULL, MARGIN, listY,
                rc.right - MARGIN * 2, rc.bottom - listY - statusHeight - MARGIN, SWP_NOZORDER);
            break;
        }
        /* 定时器：500ms 周期更新状态栏，并消费搜索变化标志 */
        case WM_TIMER: {
            if (wParam == ID_TIMER_UPDATE) {
                UpdateStatus();
                if (g_searchChanged) {
                    g_searchChanged = false;
                    if (g_result) {
                        int cnt = xjs_result_GetCount(g_result);
                        /* 【修复】搜索结果变化时清空图标缓存，防止行号变化导致图标错位 */
                        /* 【修复】将 DestroyIcon 移到临界区外，避免死锁 */
                        std::vector<HICON> iconsToDestroy;
                        EnterCriticalSection(&g_csIcon);
                        for (auto& pair : g_iconCache) iconsToDestroy.push_back(pair.second);
                        g_iconCache.clear();
                        g_iconCacheOrder.clear();
                        LeaveCriticalSection(&g_csIcon);
                        for (HICON h : iconsToDestroy) DestroyIcon(h);
                        ListView_SetItemCount(g_hResultList, cnt);
                        ListView_RedrawItems(g_hResultList, 0, cnt);
                        wchar_t status[256];
                        _snwprintf(status, 256, L"找到 %d 个结果", cnt);
                        SafeSetStatusText(status);
                    }
                }
                /* 【新增】检查是否需要保存搜索历史（停止输入超过1秒） */
                if (!g_pendingSearchText.empty()) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastSearchTime).count();
                    if (elapsed >= 1000) {
                        /* 【修复】保存当前编辑框文本和光标位置，防止被 CB_INSERTSTRING 干扰 */
                        std::wstring currentText = GetWindowTextStr(g_hSearchEdit);
                        DWORD selStart = 0, selEnd = 0;
                        SendMessageW(g_hSearchEdit, CB_GETEDITSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                        /* 先检查是否已存在于组合框中 */
                        int existing = (int)SendMessageW(g_hSearchEdit, CB_FINDSTRINGEXACT, -1, (LPARAM)g_pendingSearchText.c_str());
                        if (existing != CB_ERR) {
                            /* 已存在，先删除 */
                            SendMessageW(g_hSearchEdit, CB_DELETESTRING, existing, 0);
                        }
                        /* 插入到组合框顶部 */
                        SendMessageW(g_hSearchEdit, CB_INSERTSTRING, 0, (LPARAM)g_pendingSearchText.c_str());
                        /* 限制组合框数量 */
                        int count = (int)SendMessageW(g_hSearchEdit, CB_GETCOUNT, 0, 0);
                        while (count > MAX_SEARCH_HISTORY) {
                            SendMessageW(g_hSearchEdit, CB_DELETESTRING, count - 1, 0);
                            count--;
                        }
                        /* 【修复】恢复编辑框文本和光标位置 */
                        /* 【修复】设置抑制标志，防止触发 CBN_EDITCHANGE */
                        g_suppressSearch = true;
                        SetWindowTextW(g_hSearchEdit, currentText.c_str());
                        SendMessageW(g_hSearchEdit, CB_SETEDITSEL, 0, MAKELPARAM(selStart, selEnd));
                        g_suppressSearch = false;
                        /* 添加到 vector */
                        AddSearchToHistory(g_pendingSearchText.c_str());
                        g_pendingSearchText.clear();
                    }
                }
            }
            break;
        }
        /* 全局热键 Ctrl+Alt+F：切换窗口显示/隐藏 */
        case WM_HOTKEY: {
            if (wParam == ID_HOTKEY_SHOW) {
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                    /* 窗口可见且未最小化 → 隐藏到托盘 */
                    /* 【优化】隐藏时释放搜索结果对象，节约内存 */
                    /* 【修复】遍历阶段不能销毁结果对象，避免崩溃 */
                    if (g_result && !g_isScanning) {
                        xjs_result_Destroy(g_result);
                        g_result = NULL;
                    }
                    ListView_SetItemCount(g_hResultList, 0);
                    ShowWindow(hwnd, SW_HIDE);
                    TrayAdd(hwnd);
                } else {
                    /* 窗口隐藏或最小化 → 还原显示 */
                    /* 【优化】使用统一函数处理托盘还原 */
                    RestoreWindowFromTray(hwnd);
                }
            }
            break;
        }
        /* 系统托盘事件通知 */
        case WM_TRAY_NOTIFY: {
            if (wParam == ID_TRAY_ICON) {
                if (lParam == WM_LBUTTONUP) {
                    /* 左键点击：还原窗口 */
                    /* 【优化】使用统一函数处理托盘还原 */
                    RestoreWindowFromTray(hwnd);
                } else if (lParam == WM_RBUTTONUP) {
                    /* 右键点击：弹出托盘菜单 */
                    TrayShowMenu(hwnd);
                }
            }
            break;
        }
        /* 命令消息：处理下拉框变化、编辑框内容变化、SQL模板菜单、托盘菜单 */
        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                if (LOWORD(wParam) == ID_MODE_COMBO || LOWORD(wParam) == ID_FILTER_COMBO) {
                    DoSearch();
                } else if (LOWORD(wParam) == ID_SEARCH_EDIT) {
                    /* 【新增】选择历史记录时自动搜索 */
                    int sel = (int)SendMessageW(g_hSearchEdit, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR) {
                        wchar_t buf[256];
                        SendMessageW(g_hSearchEdit, CB_GETLBTEXT, sel, (LPARAM)buf);
                        SetWindowTextW(g_hSearchEdit, buf);  /* 确保编辑框显示选中的文本 */
                        g_lastSearch = buf;
                        /* 【修复】清除待保存的搜索词，防止定时器干扰 */
                        g_pendingSearchText.clear();
                        DoSearch();
                    }
                }
            } else if ((HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_EDITCHANGE) && LOWORD(wParam) == ID_SEARCH_EDIT) {
                /* 【修复】处理组合框的编辑框内容变化 */
                /* 【修复】如果设置了抑制标志，不执行搜索 */
                if (g_suppressSearch) break;
                g_lastSearch = GetWindowTextStr(g_hSearchEdit);
                DoSearch();
            } else if (LOWORD(wParam) == ID_MENU_HOTKEY) {
                /* 设置菜单：热键设置对话框 */
                ShowHotKeyDialog(hwnd);
            } else if (LOWORD(wParam) == ID_MENU_REBUILD) {
                /* 设置菜单：重建索引 - 删除数据库并重新扫描 */
                /* 【修复】扫描期间禁止重复重建 */
                if (g_isScanning) {
                    MessageBoxW(hwnd, L"正在扫描磁盘，请等待完成后再重建索引。", L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                if (MessageBoxW(hwnd, L"确定要重建索引吗？\n这将删除现有数据库并重新扫描所有磁盘。", L"重建索引", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    /* 停止同步（等待队列完成） */
                    if (g_engine) xjs_sync_AllStop(g_engine, FALSE);
                    /* 删除数据库文件 */
                    std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
                    DeleteFileW(dbPath.c_str());
                    /* 清空当前结果 */
                    if (g_result) {
                        xjs_result_Destroy(g_result);
                        g_result = NULL;
                    }
                    ListView_SetItemCount(g_hResultList, 0);
                    /* 清空引擎 */
                    if (g_engine) {
                        xjs_db_Clear(g_engine);
                        /* 【屏蔽】文件大小和修改时间字段，节约内存
                         * 如需启用，取消定义 ENABLE_FILE_SIZE_AND_TIME
                         */
                        #ifdef ENABLE_FILE_SIZE_AND_TIME
                        xjs_db_AddField(g_engine, "文件大小", NULL);
                        xjs_db_AddField(g_engine, "修改时间", NULL);
                        #endif
                    }
                    /* 重新扫描 */
                    SetWindowTextW(g_hStatusBar, L"正在重建索引，扫描磁盘...");
                    xjs_db_ScanPath(g_engine, NULL, TRUE);
                }
            } else if (LOWORD(wParam) == ID_MENU_AUTOSTART) {
                /* 设置菜单：切换开机自启动状态 */
                BOOL currentlyEnabled = IsAutoStartEnabled();
                SetAutoStart(!currentlyEnabled);
                /* 更新菜单勾选状态 */
                HMENU hMenu = GetMenu(hwnd);
                HMENU hSettingsMenu = GetSubMenu(hMenu, 1);  /* 假设"设置"是第2个菜单 */
                CheckMenuItem(hSettingsMenu, ID_MENU_AUTOSTART, 
                             !currentlyEnabled ? MF_CHECKED : MF_UNCHECKED);
                /* 显示提示 */
                MessageBoxW(hwnd, 
                           !currentlyEnabled ? L"已启用开机自启动" : L"已禁用开机自启动",
                           L"提示", MB_OK | MB_ICONINFORMATION);
            } else if (LOWORD(wParam) == ID_MENU_ABOUT) {
                /* 设置菜单：关于对话框 */
                ShowAboutDialog(hwnd);
            } else if (LOWORD(wParam) >= ID_SQL_TEMPLATE && LOWORD(wParam) < ID_SQL_TEMPLATE + (int)g_sqlTemplates.size()) {
                /* SQL 模板菜单：填入 SQL 语句并自动搜索（引擎会自动检测 SQL） */
                int idx = LOWORD(wParam) - ID_SQL_TEMPLATE;
                std::wstring sqlW = Utf8ToUtf16(g_sqlTemplates[idx].sql.c_str());
                SetWindowTextW(g_hSearchEdit, sqlW.c_str());
                /* 【优化】不再切换到 SQL 模式，引擎会自动检测 SQL 语句 */
                g_lastSearch = sqlW;
                DoSearch();
            }
            break;
        /* 图标异步加载完成：将 PNG 转为 HICON 并更新缓存，刷新对应行 */
        case WM_ICON_READY: {
            IconReadyData* data = (IconReadyData*)wParam;
            // 【关键修复】判断搜索指纹是否已过期，过期则直接丢弃，不绘制
            if (data->searchFingerprint == g_searchFingerprint) {
                HICON hIcon = PngToHICON(data->iconData, data->iconLength);
                if (hIcon) {
                    // 【修复】将 DestroyIcon 移到临界区外，避免死锁
                    HICON oldIconToDestroy = NULL;
                    EnterCriticalSection(&g_csIcon);
                    auto it = g_iconCache.find(data->itemIndex);
                    if (it != g_iconCache.end()) {
                        oldIconToDestroy = it->second; // 先取出旧图标
                        it->second = hIcon;
                    } else {
                        g_iconCache[data->itemIndex] = hIcon;
                    }
                    LeaveCriticalSection(&g_csIcon);
                    if (oldIconToDestroy) DestroyIcon(oldIconToDestroy); // 在外面销毁
                    // 【修复】检查 itemIndex 是否在有效范围内再刷新
                    int itemCount = ListView_GetItemCount(g_hResultList);
                    if (data->itemIndex < itemCount) {
                        ListView_RedrawItems(g_hResultList, data->itemIndex, data->itemIndex);
                    }
                }
            }
            delete[](char*)data->iconData;
            delete data;
            break;
        }
        /* 数据库加载完成：触发首次搜索 */
        case WM_LOAD_COMPLETE: {
            g_isLoading = false;
            int fileCount = (int)wParam;
            wchar_t status[256];
            _snwprintf(status, 256, L"数据库加载完成，%d 个文件，正在搜索...", fileCount);
            SetWindowTextW(g_hStatusBar, status);
            /* 【调试】检查搜索触发情况 */
            BOOL searchOk = SafeDoSearch();
            if (!searchOk) {
                MessageBoxW(hwnd, L"SafeDoSearch 返回 FALSE", L"调试", MB_OK);
            }
            break;
        }
        /* 正在扫描某分区 */
        case WM_SCAN_DRIVE: {
            char* drive = (char*)wParam;
            wchar_t status[256];
            _snwprintf(status, 256, L"正在扫描 %C: ...", drive[0]);
            SetWindowTextW(g_hStatusBar, status);
            delete[] drive;
            break;
        }
        /* 扫描进度更新 */
        case WM_SCAN_PROGRESS: {
            ScanProgressData* data = (ScanProgressData*)wParam;
            wchar_t status[256];
            /* 计算进度百分比并绘制文本进度条 */
            int percent = 0;
            if (data->totalCount > 0) {
                percent = (int)((double)data->enumeratedCount * 100 / data->totalCount);
                if (percent > 100) percent = 100;
            }
            /* 文本进度条：用 20 个方块表示进度 */
            int filled = percent / 5;  // 每 5% 一个方块
            wchar_t progressBar[21] = {0};
            for (int i = 0; i < 20; i++) {
                progressBar[i] = (i < filled) ? L'█' : L'░';
            }
            _snwprintf(status, 256, L"扫描 %C: %s %d%% (%d/%d)", 
                data->driveLetter, progressBar, percent, data->enumeratedCount, data->totalCount);
            SetWindowTextW(g_hStatusBar, status);
            delete data;
            break;
        }
        /* 磁盘扫描完成：保存耗时并触发搜索 */
        case WM_SCAN_COMPLETE: {
            ScanCompleteData* data = (ScanCompleteData*)wParam;
            g_fileCount = data->fileCount;
            g_scanElapsedMs = data->elapsedMs;  /* 【新增】保存扫描耗时 */
            /* 【优化】使用 SafeDoSearch，自动创建结果对象并检查扫描状态 */
            SafeDoSearch();
            delete data;
            break;
        }
        /* 搜索完成：更新列表项数和状态栏 */
        case WM_SEARCH_COMPLETE: {
            SearchCompleteData* data = (SearchCompleteData*)wParam;
            ListView_SetItemCount(g_hResultList, data->resultCount);
            wchar_t status[256];
            /* 【新增】显示内存占用，使用 SDK 自带的 xjs_util_FormatFileSize */
            long long memSize = g_engine ? xjs_db_GetMemorySize(g_engine) : 0;
            std::wstring memStr = Utf8ToUtf16(xjs_util_FormatFileSize(memSize));
            /* 【修改】如果有扫描耗时，显示扫描+搜索总耗时 */
            if (g_scanElapsedMs > 0) {
                int totalMs = g_scanElapsedMs + data->elapsedMs;
                _snwprintf(status, 256, L"找到 %d 个结果 (扫描%dms+搜索%dms=%dms) | 内存: %s", 
                          data->resultCount, g_scanElapsedMs, data->elapsedMs, totalMs, memStr.c_str());
                g_scanElapsedMs = 0;  /* 重置扫描耗时 */
            } else {
                _snwprintf(status, 256, L"找到 %d 个结果 (%d ms) | 内存: %s", 
                          data->resultCount, data->elapsedMs, memStr.c_str());
            }
            SetWindowTextW(g_hStatusBar, status);
            /* 【优化】搜索完成后记录时间和待保存的搜索词（延迟1秒保存） */
            std::wstring searchText = GetWindowTextStr(g_hSearchEdit);
            if (!searchText.empty() && data->resultCount > 0) {
                g_pendingSearchText = searchText;
                g_lastSearchTime = std::chrono::steady_clock::now();
            }
            delete data;
            break;
        }
        /* 通知消息：处理列表视图的各种事件 */
        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == ID_RESULT_LIST) {
                if (pnmh->code == LVN_GETDISPINFO) {
                    OnGetDispInfo((NMLVDISPINFOW*)pnmh);
                } else if (pnmh->code == NM_CUSTOMDRAW) {
                    return OnCustomDraw((LPNMLVCUSTOMDRAW)pnmh);
                } else if (pnmh->code == LVN_COLUMNCLICK) {
                    OnColumnClick((LPNMLISTVIEW)lParam);
                } else if (pnmh->code == NM_DBLCLK) {
                    LPNMITEMACTIVATE pnmia = (LPNMITEMACTIVATE)lParam;
                    if (pnmia->iItem >= 0) {
                        std::wstring path = GetItemFilePath(pnmia->iItem);
                        if (!path.empty()) OpenFile(path);
                    }
                } else if (pnmh->code == NM_RCLICK) {
                    LPNMITEMACTIVATE pnmia = (LPNMITEMACTIVATE)lParam;
                    POINT pt;
                    GetCursorPos(&pt);
                    ShowContextMenu(hwnd, pt, pnmia->iItem);
                } else if (pnmh->code == LVN_KEYDOWN) {
                    /* 【优化】支持键盘 Menu 键呼出右键菜单 */
                    LPNMLVKEYDOWN pLVKeyDown = (LPNMLVKEYDOWN)lParam;
                    if (pLVKeyDown->wVKey == VK_APPS) { /* 键盘 Menu 键 */
                        int selItem = ListView_GetNextItem(g_hResultList, -1, LVNI_FOCUSED);
                        if (selItem >= 0) {
                            RECT rc;
                            ListView_GetItemRect(g_hResultList, selItem, &rc, LVIR_BOUNDS);
                            POINT pt = { rc.left, rc.bottom };
                            ClientToScreen(g_hResultList, &pt);
                            ShowContextMenu(hwnd, pt, selItem);
                        }
                    }
                }
            }
            break;
        }
        /* 关闭时最小化到托盘而非退出；托盘菜单选"退出"才真正销毁窗口 */
        case WM_CLOSE: {
            /* 【优化】最小化到托盘时释放搜索结果对象，节约内存 */
            /* 【修复】遍历阶段不能销毁结果对象，会导致崩溃 */
            if (g_result && !g_isScanning) {
                xjs_result_Destroy(g_result);
                g_result = NULL;
            }
            ListView_SetItemCount(g_hResultList, 0);
            ShowWindow(hwnd, SW_HIDE);
            TrayAdd(hwnd);
            return 0;
        }
        /* 系统关机/重启查询：允许关闭，但先保存数据库 */
        case WM_QUERYENDSESSION: {
            /* 返回 TRUE 允许系统关闭，然后在 WM_ENDSESSION 中保存 */
            return TRUE;
        }
        /* 系统正在关机/重启：保存数据库 */
        case WM_ENDSESSION: {
            if (wParam) {  /* wParam 为 TRUE 表示确实要关闭 */
                if (g_engine && !g_isScanning) {
                    /* 正常保存数据库 */
                    xjs_sync_AllStop(g_engine, TRUE);
                    std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
                    std::string dbPathUtf8 = Utf16ToUtf8(dbPath.c_str());
                    xjs_db_Save(g_engine, dbPathUtf8.c_str());
                } else if (g_engine && g_isScanning) {
                    /* 扫描期间：停止扫描并删除不完整数据库 */
                    xjs_db_StopScan(g_engine);
                    xjs_sync_AllStop(g_engine, FALSE);
                    std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
                    DeleteFileW(dbPath.c_str());
                }
            }
            return 0;
        }
        /* 窗口销毁：保存数据库、释放所有资源、注销热键、移除托盘图标 */
        case WM_DESTROY: {
            KillTimer(hwnd, ID_TIMER_UPDATE);
            UnregisterHotKey(hwnd, ID_HOTKEY_SHOW);
            TrayRemove();
            if (g_engine) {
                /* 【修复】如果正在扫描，先停止扫描，禁止保存不完整的数据库 */
                if (g_isScanning) {
                    /* 停止扫描和文件同步 */
                    xjs_db_StopScan(g_engine);
                    xjs_sync_AllStop(g_engine, FALSE);
                    /* 删除不完整的数据库文件，下次启动重新扫描 */
                    std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
                    DeleteFileW(dbPath.c_str());
                } else {
                    /* 正常保存数据库 */
                    xjs_sync_AllStop(g_engine, TRUE);
                    std::wstring dbPath = GetExeDir() + L"\\xjs_db.dat";
                    std::string dbPathUtf8 = Utf16ToUtf8(dbPath.c_str());
                    xjs_db_Save(g_engine, dbPathUtf8.c_str());
                }
                /* 【修复】销毁结果对象，确保回调不再访问引擎 */
                if (g_result) {
                    xjs_result_Destroy(g_result);
                    g_result = NULL;
                }
                /* 【修复】等待所有回调完成后再销毁引擎 */
                xjs_sync_AllStop(g_engine, TRUE);
            }
            /* 【修复】销毁图标缓存时加锁保护 */
            EnterCriticalSection(&g_csIcon);
            for (auto& pair : g_iconCache) DestroyIcon(pair.second);
            g_iconCache.clear();
            g_iconCacheOrder.clear();
            LeaveCriticalSection(&g_csIcon);
            
            /* 【修复】释放 GDI 对象，防止内存泄漏 */
            if (g_hBrushAltRow) { DeleteObject(g_hBrushAltRow); g_hBrushAltRow = NULL; }
            if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
            if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
            
            /* 保存窗口位置和列宽配置 */
            WINDOWPLACEMENT wp = {0};
            wp.length = sizeof(wp);
            GetWindowPlacement(hwnd, &wp);
            bool maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            SaveWindowConfig(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, maximized);
            int colWidths[5];
            for (int i = 0; i < 5; i++) {
                colWidths[i] = ListView_GetColumnWidth(g_hResultList, i);
            }
            SaveColumnWidths(colWidths);
            /* 【新增】保存搜索历史 */
            SaveSearchHistory();
            
            DeleteCriticalSection(&g_csIcon);
            if (g_result) xjs_result_Destroy(g_result);
            if (g_engine) xjs_Destroy(g_engine);

            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            CoUninitialize();
            PostQuitMessage(0);
            break;
        }
        default:
            /* 最小化时也隐藏到托盘（可选行为） */
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* ========== 程序入口 ========== */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    /* 检查管理员权限，不足则提权重启 */
    if (!IsRunningAsAdmin()) { ElevateAndRestart(); return 0; }
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_HOTKEY_CLASS | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    /* 从 xunjieso.ico 加载程序图标 */
    HICON hAppIcon = (HICON)LoadImageW(hInstance, L"xunjieso.ico", IMAGE_ICON, 
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    HICON hAppIconSm = (HICON)LoadImageW(hInstance, L"xunjieso.ico", IMAGE_ICON, 
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    wc.hIcon = hAppIcon ? hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XunJieSuoWndClass";
    wc.hIconSm = hAppIconSm ? hAppIconSm : LoadIconW(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    /* 加载窗口配置 */
    int winX, winY, winW, winH;
    bool maximized;
    LoadWindowConfig(winX, winY, winW, winH, maximized);
    /* 【修复】检查窗口位置是否在有效屏幕范围内，防止拔掉显示器后窗口"隐身" */
    if (winX != CW_USEDEFAULT && winY != CW_USEDEFAULT) {
        HMONITOR hMonitor = MonitorFromPoint({winX, winY}, MONITOR_DEFAULTTONULL);
        if (!hMonitor) {
            /* 如果点不在任何显示器上，重置为默认 */
            winX = CW_USEDEFAULT;
            winY = CW_USEDEFAULT;
        }
    }
    HWND hwnd = CreateWindowExW(0, L"XunJieSuoWndClass", L"迅捷搜",
        WS_OVERLAPPEDWINDOW, winX, winY, winW, winH,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    if (maximized) nCmdShow = SW_SHOWMAXIMIZED;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
