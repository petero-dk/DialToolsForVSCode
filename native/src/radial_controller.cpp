// Native Node.js addon wrapping Windows.UI.Input.RadialController (WinRT).
//
// The RadialController only shows custom menu items when the process that owns
// the HWND passed to CreateForWindow is in the foreground. VS Code's extension
// host is a separate process from the renderer (which owns the visible window),
// so we must find VS Code's renderer window and use its HWND. A hidden message-
// only window is still created on our STA thread to run the message pump that
// delivers WinRT events — but CreateForWindow receives the renderer's HWND.
//
// All WinRT work runs on a dedicated STA thread; JS callbacks are marshalled
// back via Napi::ThreadSafeFunction.

#include <napi.h>
#include <windows.h>
#include <ole2.h>

// C++/WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Storage.Streams.h>

// Win32 interop headers — RadialControllerInterop.h also defines
// IRadialControllerConfigurationInterop in SDK 10.0.26100+
#include <RadialControllerInterop.h>

#include <tlhelp32.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <map>
#include <set>
#include <string>
#include <functional>
#include <memory>

#include <sstream>
#include <cwctype>

#pragma comment(lib, "RuntimeObject.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;

// ---------------------------------------------------------------------------
// Window class name for the hidden message-only window
// ---------------------------------------------------------------------------
static constexpr wchar_t kWndClass[] = L"DialToolsMessageWnd";

// ---------------------------------------------------------------------------
// Helper: map string icon name → RadialControllerMenuKnownIcon
// ---------------------------------------------------------------------------
static RadialControllerMenuKnownIcon IconFromString(const std::string& name) {
    if (name == "zoom")              return RadialControllerMenuKnownIcon::Zoom;
    if (name == "undoRedo")          return RadialControllerMenuKnownIcon::UndoRedo;
    if (name == "volume")            return RadialControllerMenuKnownIcon::Volume;
    if (name == "nextPreviousTrack") return RadialControllerMenuKnownIcon::NextPreviousTrack;
    if (name == "ruler")             return RadialControllerMenuKnownIcon::Ruler;
    if (name == "inkColor")          return RadialControllerMenuKnownIcon::InkColor;
    if (name == "inkThickness")      return RadialControllerMenuKnownIcon::InkThickness;
    if (name == "penType")           return RadialControllerMenuKnownIcon::PenType;
    return RadialControllerMenuKnownIcon::Scroll; // default
}

// ---------------------------------------------------------------------------
// Find a top-level VS Code renderer/main HWND by matching Chromium window class
// plus Code*.exe process image. Falls back to nullptr if nothing is found.
// ---------------------------------------------------------------------------
static std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return s;
}

static std::wstring BasenameOfPath(const std::wstring& fullPath) {
    const size_t pos = fullPath.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? fullPath : fullPath.substr(pos + 1);
}

static bool IsVSCodeLikeExe(const std::wstring& exeNameLower) {
    // Match common channels: Code.exe, Code - Insiders.exe, Code - OSS.exe
    if (exeNameLower == L"code.exe") return true;
    if (exeNameLower.find(L"code - ") == 0 &&
        exeNameLower.size() > 10 &&
        exeNameLower.rfind(L".exe") == exeNameLower.size() - 4) {
        return true;
    }
    return false;
}

static std::wstring GetProcessExeName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return L"";

    wchar_t pathBuf[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, pathBuf, &size)) {
        result = BasenameOfPath(ToLower(pathBuf));
    }
    CloseHandle(process);
    return result;
}

static bool IsVSCodeTopLevelWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;

    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, 64);
    if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) != 0) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    const std::wstring exe = GetProcessExeName(pid);
    return IsVSCodeLikeExe(exe);
}

static HWND FindVSCodeWindow() {
    // VS Code's extension host can be either:
    //   (a) a direct child of the renderer process, OR
    //   (b) a sibling of the renderer (both children of the main process).
    //
    // Strategy: snapshot all processes, walk up 2-3 levels from our PID
    // collecting Code.exe ancestors, then also include their Code.exe children.
    // Search for a Chrome_WidgetWin_* window owned by any of those PIDs.

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;

    std::map<DWORD, DWORD>        parentOf;
    std::map<DWORD, std::wstring> exeOf;   // base exe name, lower-case
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            parentOf[pe.th32ProcessID] = pe.th32ParentProcessID;
            exeOf[pe.th32ProcessID]    = ToLower(BasenameOfPath(pe.szExeFile));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    const DWORD myPid = GetCurrentProcessId();

    // Collect Code.exe ancestors (up to 3 levels up).
    std::set<DWORD> ancestorPids;
    DWORD cur = myPid;
    for (int i = 0; i < 3; ++i) {
        auto it = parentOf.find(cur);
        if (it == parentOf.end() || it->second == 0) break;
        cur = it->second;
        if (IsVSCodeLikeExe(exeOf[cur])) ancestorPids.insert(cur);
    }

    // Also include Code.exe children of those ancestors (renderer siblings).
    std::set<DWORD> searchPids = ancestorPids;
    for (auto& [childPid, parentPid] : parentOf) {
        if (ancestorPids.count(parentPid) && IsVSCodeLikeExe(exeOf[childPid])) {
            searchPids.insert(childPid);
        }
    }

    if (searchPids.empty()) return nullptr;

    // First choice: current foreground window if it belongs to our process set
    // and is a Chromium top-level VS Code window.
    HWND fg = GetForegroundWindow();
    if (fg && IsWindowVisible(fg) && GetWindow(fg, GW_OWNER) == nullptr) {
        wchar_t cls[64]{};
        GetClassNameW(fg, cls, 64);
        if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) == 0) {
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fg, &fgPid);
            if (searchPids.count(fgPid)) {
                return fg;
            }
        }
    }

    struct Ctx {
        const std::set<DWORD>& searchPids;
        HWND         found{ nullptr };
        DWORD        foundPid{ 0 };
        std::wstring foundTitle;
    };
    Ctx ctx{ searchPids };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) != 0) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!c.searchPids.count(pid)) return TRUE;

        wchar_t title[256]{};
        int len = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
        c.found      = hwnd;
        c.foundPid   = pid;
        c.foundTitle = title;
        if (len > 0) return FALSE; // prefer titled windows; stop on first
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

// ---------------------------------------------------------------------------
// Main addon class
// ---------------------------------------------------------------------------
class RadialControllerAddon : public Napi::ObjectWrap<RadialControllerAddon> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "RadialController", {
            InstanceMethod("initialize",       &RadialControllerAddon::Initialize),
            InstanceMethod("addMenuItem",      &RadialControllerAddon::AddMenuItem),
            InstanceMethod("removeMenuItem",   &RadialControllerAddon::RemoveMenuItem),
            InstanceMethod("clearMenuItems",   &RadialControllerAddon::ClearMenuItems),
            InstanceMethod("onDebug",          &RadialControllerAddon::OnDebug),
            InstanceMethod("onRotate",         &RadialControllerAddon::OnRotate),
            InstanceMethod("onClick",          &RadialControllerAddon::OnClick),
            InstanceMethod("onMenuItemSelected",&RadialControllerAddon::OnMenuItemSelected),
            InstanceMethod("onControlAcquired",&RadialControllerAddon::OnControlAcquired),
            InstanceMethod("onControlLost",    &RadialControllerAddon::OnControlLost),
            InstanceMethod("dispose",          &RadialControllerAddon::Dispose),
        });

        auto* ctor = new Napi::FunctionReference();
        *ctor = Napi::Persistent(func);
        env.SetInstanceData(ctor);
        exports.Set("RadialController", func);
        return exports;
    }

    explicit RadialControllerAddon(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<RadialControllerAddon>(info) {}

    ~RadialControllerAddon() { Cleanup(); }

private:
    // -----------------------------------------------------------------------
    // State shared between JS thread and WinRT thread
    // -----------------------------------------------------------------------
    std::thread           winrtThread_;
    DWORD                 winrtThreadId_{ 0 };
    std::atomic<bool>     shouldStop_{ false };

    // One-time init sync
    std::mutex              initMutex_;
    std::condition_variable initCv_;
    bool                    initComplete_{ false };
    bool                    initOk_{ false };

    // WinRT objects (touch only from winrtThread_)
    RadialController            controller_{ nullptr };
    RadialControllerConfiguration config_{ nullptr };
    HWND                        hwnd_{ nullptr };

    struct MenuItemEntry {
        RadialControllerMenuItem item{ nullptr };
        winrt::event_token       invokedToken{};
    };
    std::map<std::wstring, MenuItemEntry> menuItems_;

    winrt::event_token rotationToken_{};
    winrt::event_token clickToken_{};
    winrt::event_token acquiredToken_{};
    winrt::event_token lostToken_{};

    // Thread-safe function handles (valid after initialize, released in Cleanup)
    Napi::ThreadSafeFunction rotateTsfn_;
    Napi::ThreadSafeFunction clickTsfn_;
    Napi::ThreadSafeFunction menuItemSelectedTsfn_;
    Napi::ThreadSafeFunction controlAcquiredTsfn_;
    Napi::ThreadSafeFunction controlLostTsfn_;
    Napi::ThreadSafeFunction debugTsfn_;

    bool tsfnsCreated_{ false };

    // -----------------------------------------------------------------------
    // JS-callable methods
    // -----------------------------------------------------------------------

    Napi::Value Initialize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        DebugLog("initialize() called");
        if (winrtThread_.joinable()) {
            DebugLog("initialize() rejected: already initialized");
            Napi::TypeError::New(env, "Already initialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // Spawn the WinRT STA thread
        DebugLog("spawning WinRT thread");
        winrtThread_ = std::thread([this]() { WinRTThreadProc(); });

        // Wait for init to complete (max 5 s)
        std::unique_lock<std::mutex> lock(initMutex_);
        const bool done = initCv_.wait_for(lock, std::chrono::seconds(5),
                                           [this]{ return initComplete_; });
        if (!done) {
            DebugLog("initialize() timed out waiting for WinRT thread (5s)");
        }
        DebugLog(std::string("initialize() completed: initOk=") + (initOk_ ? "true" : "false"));

        return Napi::Boolean::New(env, initOk_);
    }

    // addMenuItem(name: string, iconName: string): void
    Napi::Value AddMenuItem(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!initOk_) return env.Undefined();

        std::string name     = info[0].As<Napi::String>();
        std::string iconName = info.Length() > 1 ? info[1].As<Napi::String>().Utf8Value()
                                                 : std::string("scroll");

        std::wstring wname(name.begin(), name.end());
        auto icon = IconFromString(iconName);

        // Post work to WinRT thread via PostThreadMessage
        // We heap-allocate a small payload and free it in the thread.
        struct Payload { std::wstring name; RadialControllerMenuKnownIcon icon; };
        auto* p = new Payload{ wname, icon };
        if (!PostThreadMessageW(winrtThreadId_, WM_USER + 1,
                                reinterpret_cast<WPARAM>(p), 0)) {
            DebugLog("addMenuItem failed to post thread message");
            delete p;
        } else {
            DebugLog(std::string("addMenuItem queued: ") + name + " (" + iconName + ")");
        }
        return env.Undefined();
    }

    // removeMenuItem(name: string): void
    Napi::Value RemoveMenuItem(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!initOk_) return env.Undefined();

        std::string name = info[0].As<Napi::String>();
        std::wstring wname(name.begin(), name.end());

        auto* p = new std::wstring(wname);
        if (!PostThreadMessageW(winrtThreadId_, WM_USER + 2,
                                reinterpret_cast<WPARAM>(p), 0)) {
            DebugLog("removeMenuItem failed to post thread message");
            delete p;
        } else {
            DebugLog(std::string("removeMenuItem queued: ") + name);
        }
        return env.Undefined();
    }

    // clearMenuItems(): void
    Napi::Value ClearMenuItems(const Napi::CallbackInfo& info) {
        if (!initOk_) return info.Env().Undefined();
        if (!PostThreadMessageW(winrtThreadId_, WM_USER + 3, 0, 0)) {
            DebugLog("clearMenuItems failed to post thread message");
        } else {
            DebugLog("clearMenuItems queued");
        }
        return info.Env().Undefined();
    }

    // onDebug(cb: (message: string) => void): void
    Napi::Value OnDebug(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        debugTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "debug", 0, 1);
        return env.Undefined();
    }

    // onRotate(cb: (delta: number) => void): void
    Napi::Value OnRotate(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        rotateTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "rotate", 0, 1);
        tsfnsCreated_ = true;
        return env.Undefined();
    }

    // onClick(cb: () => void): void
    Napi::Value OnClick(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        clickTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "click", 0, 1);
        return env.Undefined();
    }

    // onMenuItemSelected(cb: (name: string) => void): void
    Napi::Value OnMenuItemSelected(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        menuItemSelectedTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "menuItemSelected", 0, 1);
        return env.Undefined();
    }

    // onControlAcquired(cb: () => void): void
    Napi::Value OnControlAcquired(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        controlAcquiredTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "controlAcquired", 0, 1);
        return env.Undefined();
    }

    // onControlLost(cb: () => void): void
    Napi::Value OnControlLost(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        controlLostTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "controlLost", 0, 1);
        return env.Undefined();
    }

    Napi::Value Dispose(const Napi::CallbackInfo& info) {
        Cleanup();
        return info.Env().Undefined();
    }

    // -----------------------------------------------------------------------
    // WinRT worker thread
    // -----------------------------------------------------------------------
    void WinRTThreadProc() {
        // STA — required for WinRT
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            DebugLog("WinRT thread: CoInitializeEx failed");
            SignalInit(false);
            return;
        }
        DebugLog("WinRT thread: CoInitializeEx succeeded");

        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrtThreadId_ = GetCurrentThreadId();
        DebugLog("WinRT thread: apartment initialized");

        // Register hidden message-only window class
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWndClass;
        RegisterClassExW(&wc); // ok if already registered

        hwnd_ = CreateWindowExW(0, kWndClass, L"DialTools",
                                0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_) {
            DebugLog("WinRT thread: CreateWindowExW failed");
            winrt::uninit_apartment();
            CoUninitialize();
            SignalInit(false);
            return;
        }
        DebugLog("WinRT thread: message-only HWND created");

        // Use VS Code's renderer window so the custom menu appears when VS Code
        // is in the foreground. Fall back to our message window if not found.
        HWND appHwnd = FindVSCodeWindow();

        if (appHwnd) {
            DWORD pid = 0;
            GetWindowThreadProcessId(appHwnd, &pid);
            wchar_t title[256]{};
            GetWindowTextW(appHwnd, title, static_cast<int>(std::size(title)));
            std::ostringstream ds;
            ds << "WinRT thread: found renderer HWND=0x"
               << std::hex << reinterpret_cast<uintptr_t>(appHwnd)
               << " PID=" << std::dec << pid
               << " title=\"" << std::string(title, title + wcslen(title)) << "\"";
            DebugLog(ds.str());
        } else {
            // Log process ancestry so we can diagnose detection failures.
            HANDLE dbgSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (dbgSnap != INVALID_HANDLE_VALUE) {
                std::map<DWORD, DWORD> dbgParentOf;
                std::map<DWORD, std::wstring> dbgExeOf;
                PROCESSENTRY32W dbgPe{ sizeof(dbgPe) };
                if (Process32FirstW(dbgSnap, &dbgPe)) {
                    do {
                        dbgParentOf[dbgPe.th32ProcessID] = dbgPe.th32ParentProcessID;
                        dbgExeOf[dbgPe.th32ProcessID]    = ToLower(BasenameOfPath(dbgPe.szExeFile));
                    } while (Process32NextW(dbgSnap, &dbgPe));
                }
                CloseHandle(dbgSnap);
                std::ostringstream chain;
                chain << "WinRT thread: renderer not found. PID chain:";
                DWORD p = GetCurrentProcessId();
                for (int i = 0; i < 5; ++i) {
                    chain << " " << p << "(" << std::string(dbgExeOf[p].begin(), dbgExeOf[p].end()) << ")";
                    auto it = dbgParentOf.find(p);
                    if (it == dbgParentOf.end() || it->second == 0) break;
                    p = it->second;
                }
                DebugLog(chain.str());
            }
        }

        if (!appHwnd) { appHwnd = hwnd_; }

        if (appHwnd == hwnd_) {
            DebugLog("WinRT thread: sibling renderer not found; using message-window fallback (custom menu will NOT show)");
        } else {
            DebugLog("WinRT thread: using sibling renderer HWND for CreateForWindow");
        }

        auto logInitFailure = [](HWND attemptedHwnd, HRESULT hresult, const wchar_t* stage) {
            std::wstringstream ss;
            ss << L"[DialTools] RadialController init failed at " << stage
               << L" for HWND=" << attemptedHwnd
               << L" HRESULT=0x" << std::hex << static_cast<unsigned long>(hresult)
               << L"\n";
            OutputDebugStringW(ss.str().c_str());
        };

        auto tryInitForWindow = [&](HWND target) -> bool {
            if (!target || !IsWindow(target)) return false;
            try {
                auto controllerInterop =
                    winrt::get_activation_factory<RadialController,
                                                  IRadialControllerInterop>();
                winrt::check_hresult(
                    controllerInterop->CreateForWindow(
                        target,
                        winrt::guid_of<RadialController>(),
                        winrt::put_abi(controller_)));

                auto configInterop =
                    winrt::get_activation_factory<RadialControllerConfiguration,
                                                  IRadialControllerConfigurationInterop>();
                winrt::check_hresult(
                    configInterop->GetForWindow(
                        target,
                        winrt::guid_of<RadialControllerConfiguration>(),
                        winrt::put_abi(config_)));

                config_.SetDefaultMenuItems(
                    winrt::single_threaded_vector<RadialControllerSystemMenuItemKind>());
                return true;
            } catch (const winrt::hresult_error& e) {
                controller_ = nullptr;
                config_ = nullptr;
                logInitFailure(target, e.code(), L"CreateForWindow/GetForWindow");
                return false;
            } catch (...) {
                controller_ = nullptr;
                config_ = nullptr;
                logInitFailure(target, E_FAIL, L"Unknown");
                return false;
            }
        };

        // Try renderer HWND first; if interop rejects it, fall back to the
        // message window so initialize() still succeeds.
        bool initialized = false;
        if (appHwnd != hwnd_) {
            initialized = tryInitForWindow(appHwnd);
        }
        if (!initialized) {
            initialized = tryInitForWindow(hwnd_);
        }

        if (!initialized) {
            DebugLog("WinRT thread: failed to initialize controller/config for all HWND candidates");
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            winrt::uninit_apartment();
            CoUninitialize();
            SignalInit(false);
            return;
        }
        DebugLog("WinRT thread: controller/config initialized");

        // Ensure the thread message queue exists before JS starts posting
        // WM_USER work items (menu add/remove/clear).
        MSG queued{};
        PeekMessageW(&queued, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        DebugLog("WinRT thread: thread message queue ready");

        // Wire up controller events
        rotationToken_ = controller_.RotationChanged(
            [this](RadialController const&,
                   RadialControllerRotationChangedEventArgs const& args) {
                double delta = args.RotationDeltaInDegrees();
                if (rotateTsfn_) {
                        DebugLog("event: rotation changed");
                    rotateTsfn_.NonBlockingCall(
                        [delta](Napi::Env env, Napi::Function cb) {
                            cb.Call({ Napi::Number::New(env, delta) });
                        });
                }
            });

        clickToken_ = controller_.ButtonClicked(
            [this](RadialController const&,
                   RadialControllerButtonClickedEventArgs const&) {
                if (clickTsfn_) {
                    DebugLog("event: button clicked");
                    clickTsfn_.NonBlockingCall(
                        [](Napi::Env env, Napi::Function cb) {
                            cb.Call({});
                        });
                }
            });

        acquiredToken_ = controller_.ControlAcquired(
            [this](RadialController const&, winrt::Windows::Foundation::IInspectable const&) {
                if (controlAcquiredTsfn_) {
                    DebugLog("event: control acquired");
                    controlAcquiredTsfn_.NonBlockingCall(
                        [](Napi::Env env, Napi::Function cb) {
                            cb.Call({});
                        });
                }
            });

        lostToken_ = controller_.ControlLost(
            [this](RadialController const&, winrt::Windows::Foundation::IInspectable const&) {
                if (controlLostTsfn_) {
                    DebugLog("event: control lost");
                    controlLostTsfn_.NonBlockingCall(
                        [](Napi::Env env, Napi::Function cb) {
                            cb.Call({});
                        });
                }
            });

        DebugLog("WinRT thread: signaling init success");
        SignalInit(true);

        // Message loop — delivers WinRT events on this STA thread
        // and receives PostThreadMessage work items from JS thread
        MSG msg;
        while (!shouldStop_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.hwnd == nullptr) {
                // Thread message (from PostThreadMessageW)
                HandleThreadMessage(msg);
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        // Cleanup WinRT
        if (controller_) {
            DebugLog("WinRT thread: tearing down controller event handlers");
            controller_.RotationChanged(rotationToken_);
            controller_.ButtonClicked(clickToken_);
            controller_.ControlAcquired(acquiredToken_);
            controller_.ControlLost(lostToken_);
            controller_ = nullptr;
        }
        DebugLog("WinRT thread: cleanup complete");
        config_ = nullptr;
        if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
        winrt::uninit_apartment();
        CoUninitialize();
    }

    // -----------------------------------------------------------------------
    // Handle WM_USER messages posted by JS-side methods
    // -----------------------------------------------------------------------
    void HandleThreadMessage(const MSG& msg) {
        switch (msg.message) {
        case WM_USER + 1: { // addMenuItem
            using Payload = struct { std::wstring name; RadialControllerMenuKnownIcon icon; };
            auto* p = reinterpret_cast<Payload*>(msg.wParam);
            try {
                if (menuItems_.count(p->name) == 0) {
                    auto item = RadialControllerMenuItem::CreateFromKnownIcon(
                        winrt::hstring(p->name), p->icon);

                    std::wstring capturedName = p->name;
                    auto token = item.Invoked(
                        [this, capturedName](RadialControllerMenuItem const&,
                                             winrt::Windows::Foundation::IInspectable const&) {
                            if (menuItemSelectedTsfn_) {
                                std::string narrow(capturedName.begin(),
                                                   capturedName.end());
                                menuItemSelectedTsfn_.NonBlockingCall(
                                    [narrow](Napi::Env env, Napi::Function cb) {
                                        cb.Call({ Napi::String::New(env, narrow) });
                                    });
                            }
                        });

                    controller_.Menu().Items().Append(item);
                    menuItems_[p->name] = { item, token };
                    if (menuItems_.size() == 1) {
                        try {
                            controller_.Menu().SelectMenuItem(item);
                            DebugLog("selected first custom menu item");
                        } catch (...) {
                            DebugLog("failed to select first custom menu item");
                        }
                    }
                    DebugLog(std::string("menu item added: ") + std::string(p->name.begin(), p->name.end()));
                } else {
                    DebugLog(std::string("menu item already exists: ") + std::string(p->name.begin(), p->name.end()));
                }
            } catch (...) {
                DebugLog("menu item add failed in WinRT thread");
            }
            delete p;
            break;
        }
        case WM_USER + 2: { // removeMenuItem
            auto* p = reinterpret_cast<std::wstring*>(msg.wParam);
            RemoveMenuItemByName(*p);
            DebugLog(std::string("menu item remove requested: ") + std::string(p->begin(), p->end()));
            delete p;
            break;
        }
        case WM_USER + 3: { // clearMenuItems
            auto& items = controller_.Menu().Items();
            for (auto& [k, v] : menuItems_) {
                v.item.Invoked(v.invokedToken);
                uint32_t idx{};
                if (items.IndexOf(v.item, idx)) items.RemoveAt(idx);
            }
            menuItems_.clear();
            DebugLog("menu items cleared");
            break;
        }
        default: break;
        }
    }

    void RemoveMenuItemByName(const std::wstring& name) {
        auto it = menuItems_.find(name);
        if (it == menuItems_.end()) {
            DebugLog(std::string("remove skipped; menu item not found: ") + std::string(name.begin(), name.end()));
            return;
        }
        it->second.item.Invoked(it->second.invokedToken);
        auto& items = controller_.Menu().Items();
        uint32_t idx{};
        if (items.IndexOf(it->second.item, idx)) items.RemoveAt(idx);
        menuItems_.erase(it);
        DebugLog(std::string("menu item removed: ") + std::string(name.begin(), name.end()));
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    void SignalInit(bool ok) {
        std::lock_guard<std::mutex> lock(initMutex_);
        initOk_      = ok;
        initComplete_ = true;
        initCv_.notify_all();
    }

    void Cleanup() {
        DebugLog("cleanup() called");
        if (winrtThread_.joinable()) {
            shouldStop_ = true;
            if (winrtThreadId_) PostThreadMessageW(winrtThreadId_, WM_QUIT, 0, 0);
            winrtThread_.join();
            DebugLog("cleanup(): WinRT thread joined");
        }
        // Release TSFNs
        auto releaseTsfn = [](Napi::ThreadSafeFunction& fn) {
            if (fn) { fn.Release(); fn = Napi::ThreadSafeFunction{}; }
        };
        releaseTsfn(rotateTsfn_);
        releaseTsfn(clickTsfn_);
        releaseTsfn(menuItemSelectedTsfn_);
        releaseTsfn(controlAcquiredTsfn_);
        releaseTsfn(controlLostTsfn_);
        releaseTsfn(debugTsfn_);
    }

    void DebugLog(const std::string& msg) {
        std::string prefixed = std::string("native: ") + msg;
        if (debugTsfn_) {
            auto payload = std::make_shared<std::string>(prefixed);
            debugTsfn_.NonBlockingCall([payload](Napi::Env env, Napi::Function cb) {
                cb.Call({ Napi::String::New(env, *payload) });
            });
        }
        std::wstring w(prefixed.begin(), prefixed.end());
        w += L"\n";
        OutputDebugStringW(w.c_str());
    }
};

// ---------------------------------------------------------------------------
// Module entry point
// ---------------------------------------------------------------------------
Napi::Object ModuleInit(Napi::Env env, Napi::Object exports) {
    RadialControllerAddon::Init(env, exports);
    return exports;
}

NODE_API_MODULE(radial_controller, ModuleInit)
