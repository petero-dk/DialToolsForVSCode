// Native Node.js addon — manages radial_hook.dll injection and named pipe IPC.
//
// Architecture:
//   radial_hook.dll  — injected into VS Code's renderer process via SetWindowsHookEx;
//                      creates the RadialController in the correct process context.
//   radial_controller.node — this addon, running in the extension host; creates
//                      named pipe servers, loads and hooks the DLL, and dispatches
//                      WinRT events to JS via Napi::ThreadSafeFunction.
//
// Named pipe protocol (pipe_protocol.h):
//   Events  pipe  \\.\pipe\DialToolsRC-evt-{rendererPid}  DLL writes, addon reads
//   Commands pipe \\.\pipe\DialToolsRC-cmd-{rendererPid}  addon writes, DLL reads
//   Frames: [4-byte Msg type][4-byte dataLen][dataLen bytes]

#include <napi.h>
#include <windows.h>
#include <ole2.h>
#include <tlhelp32.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <string>
#include <map>
#include <set>
#include <sstream>
#include <cwctype>
#include <functional>
#include <memory>

#include "pipe_protocol.h"

#pragma comment(lib, "user32.lib")

using namespace DialTools;

// Forward declaration — used as address hint for GetModuleHandleExW
Napi::Object ModuleInit(Napi::Env env, Napi::Object exports);

// ---------------------------------------------------------------------------
// Helper: map string icon name → UTF-8 icon name (passed through pipe as-is)
// (Icon mapping lives in the DLL; we only need the string here.)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Window-finding helpers — identical to previous version; kept unchanged
// ---------------------------------------------------------------------------
static std::wstring ToLower(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

static std::wstring BasenameOfPath(const std::wstring& fullPath) {
    const size_t pos = fullPath.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? fullPath : fullPath.substr(pos + 1);
}

static bool IsVSCodeLikeExe(const std::wstring& exeNameLower) {
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
    if (QueryFullProcessImageNameW(process, 0, pathBuf, &size))
        result = BasenameOfPath(ToLower(pathBuf));
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
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return nullptr;

    std::map<DWORD, DWORD>        parentOf;
    std::map<DWORD, std::wstring> exeOf;
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
        if (ancestorPids.count(parentPid) && IsVSCodeLikeExe(exeOf[childPid]))
            searchPids.insert(childPid);
    }

    if (searchPids.empty()) return nullptr;

    // First choice: foreground window if it belongs to our process set.
    HWND fg = GetForegroundWindow();
    if (fg && IsWindowVisible(fg) && GetWindow(fg, GW_OWNER) == nullptr) {
        wchar_t cls[64]{};
        GetClassNameW(fg, cls, 64);
        if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) == 0) {
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fg, &fgPid);
            if (searchPids.count(fgPid)) return fg;
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
        c.found    = hwnd;
        c.foundPid = pid;
        c.foundTitle = title;
        if (len > 0) return FALSE; // prefer titled windows
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

// ---------------------------------------------------------------------------
// Pipe write helper
// ---------------------------------------------------------------------------
static bool WriteExact(HANDLE pipe, const void* buf, DWORD len) {
    const char* p   = static_cast<const char*>(buf);
    DWORD       rem = len;
    while (rem > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, p, rem, &written, nullptr) || written == 0)
            return false;
        p   += written;
        rem -= written;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Main addon class
// ---------------------------------------------------------------------------
class RadialControllerAddon : public Napi::ObjectWrap<RadialControllerAddon> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "RadialController", {
            InstanceMethod("initialize",        &RadialControllerAddon::Initialize),
            InstanceMethod("addMenuItem",       &RadialControllerAddon::AddMenuItem),
            InstanceMethod("removeMenuItem",    &RadialControllerAddon::RemoveMenuItem),
            InstanceMethod("clearMenuItems",    &RadialControllerAddon::ClearMenuItems),
            InstanceMethod("onDebug",           &RadialControllerAddon::OnDebug),
            InstanceMethod("onRotate",          &RadialControllerAddon::OnRotate),
            InstanceMethod("onClick",           &RadialControllerAddon::OnClick),
            InstanceMethod("onMenuItemSelected",&RadialControllerAddon::OnMenuItemSelected),
            InstanceMethod("onControlAcquired", &RadialControllerAddon::OnControlAcquired),
            InstanceMethod("onControlLost",     &RadialControllerAddon::OnControlLost),
            InstanceMethod("dispose",           &RadialControllerAddon::Dispose),
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
    // Pipe handles
    // -----------------------------------------------------------------------
    HANDLE evtPipeServer_{ INVALID_HANDLE_VALUE }; // addon reads events from DLL
    HANDLE cmdPipeServer_{ INVALID_HANDLE_VALUE }; // addon writes commands to DLL

    std::mutex cmdMutex_; // protects cmdPipeServer_ writes

    // -----------------------------------------------------------------------
    // DLL / hook
    // -----------------------------------------------------------------------
    HMODULE hDll_{ nullptr };
    HHOOK   hook_{ nullptr };

    // -----------------------------------------------------------------------
    // Pipe reader thread
    // -----------------------------------------------------------------------
    std::thread          pipeReaderThread_;
    std::atomic<bool>    pipeStop_{ false };

    // Init-ready event: PipeReaderThread sets this when EvtReady arrives
    HANDLE readyEvent_{ nullptr };

    // -----------------------------------------------------------------------
    // Init state
    // -----------------------------------------------------------------------
    bool initOk_{ false };
    bool tsfnsCreated_{ false };

    // -----------------------------------------------------------------------
    // Thread-safe function handles
    // -----------------------------------------------------------------------
    Napi::ThreadSafeFunction rotateTsfn_;
    Napi::ThreadSafeFunction clickTsfn_;
    Napi::ThreadSafeFunction menuItemSelectedTsfn_;
    Napi::ThreadSafeFunction controlAcquiredTsfn_;
    Napi::ThreadSafeFunction controlLostTsfn_;
    Napi::ThreadSafeFunction debugTsfn_;

    // -----------------------------------------------------------------------
    // JS-callable methods
    // -----------------------------------------------------------------------

    Napi::Value Initialize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        DebugLog("initialize() called");

        if (hook_) {
            Napi::TypeError::New(env, "Already initialized").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        // 1. Find VS Code renderer window
        HWND rendererHwnd = FindVSCodeWindow();
        if (!rendererHwnd) {
            DebugLog("initialize(): renderer HWND not found");
            return Napi::Boolean::New(env, false);
        }

        DWORD rendererPid      = 0;
        DWORD rendererThreadId = GetWindowThreadProcessId(rendererHwnd, &rendererPid);

        {
            std::ostringstream ss;
            ss << "initialize(): renderer HWND=0x" << std::hex
               << reinterpret_cast<uintptr_t>(rendererHwnd)
               << " PID=" << std::dec << rendererPid
               << " TID=" << rendererThreadId;
            DebugLog(ss.str());
        }

        // 2. Build pipe names
        std::wstring evtPipeName =
            L"\\\\.\\pipe\\DialToolsRC-evt-" + std::to_wstring(rendererPid);
        std::wstring cmdPipeName =
            L"\\\\.\\pipe\\DialToolsRC-cmd-" + std::to_wstring(rendererPid);

        // 3. Create events pipe server (addon reads — PIPE_ACCESS_INBOUND)
        evtPipeServer_ = CreateNamedPipeW(
            evtPipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 5000, nullptr);
        if (evtPipeServer_ == INVALID_HANDLE_VALUE) {
            DebugLog("initialize(): failed to create events pipe server");
            return Napi::Boolean::New(env, false);
        }

        // 4. Create commands pipe server (addon writes — PIPE_ACCESS_OUTBOUND)
        cmdPipeServer_ = CreateNamedPipeW(
            cmdPipeName.c_str(),
            PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 5000, nullptr);
        if (cmdPipeServer_ == INVALID_HANDLE_VALUE) {
            DebugLog("initialize(): failed to create commands pipe server");
            CloseHandle(evtPipeServer_); evtPipeServer_ = INVALID_HANDLE_VALUE;
            return Napi::Boolean::New(env, false);
        }

        DebugLog("initialize(): pipe servers created");

        // 5. Find DLL path: same directory as this .node addon
        wchar_t selfPath[MAX_PATH]{};
        {
            HMODULE hSelf = nullptr;
            // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS finds the module containing
            // the function pointer we pass — i.e., this .node file.
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ModuleInit),
                &hSelf);
            GetModuleFileNameW(hSelf, selfPath, MAX_PATH);
        }
        // Strip filename, append DLL name
        std::wstring dllPath = selfPath;
        size_t slash = dllPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dllPath.resize(slash + 1);
        dllPath += L"radial_hook.dll";

        {
            std::string dp(dllPath.begin(), dllPath.end());
            DebugLog("initialize(): loading DLL: " + dp);
        }

        // 6. Load the DLL (this registers it; hook will inject it into renderer)
        hDll_ = LoadLibraryW(dllPath.c_str());
        if (!hDll_) {
            std::ostringstream ss;
            ss << "initialize(): LoadLibrary failed, error=" << GetLastError();
            DebugLog(ss.str());
            CloseHandle(evtPipeServer_); evtPipeServer_ = INVALID_HANDLE_VALUE;
            CloseHandle(cmdPipeServer_); cmdPipeServer_ = INVALID_HANDLE_VALUE;
            return Napi::Boolean::New(env, false);
        }

        // 7. Get hook proc and install
        HOOKPROC hookProc = reinterpret_cast<HOOKPROC>(
            GetProcAddress(hDll_, "GetMsgProc"));
        if (!hookProc) {
            DebugLog("initialize(): GetProcAddress(GetMsgProc) failed");
            FreeLibrary(hDll_); hDll_ = nullptr;
            CloseHandle(evtPipeServer_); evtPipeServer_ = INVALID_HANDLE_VALUE;
            CloseHandle(cmdPipeServer_); cmdPipeServer_ = INVALID_HANDLE_VALUE;
            return Napi::Boolean::New(env, false);
        }

        hook_ = SetWindowsHookExW(WH_GETMESSAGE, hookProc, hDll_, rendererThreadId);
        if (!hook_) {
            std::ostringstream ss;
            ss << "initialize(): SetWindowsHookEx failed, error=" << GetLastError();
            DebugLog(ss.str());
            FreeLibrary(hDll_); hDll_ = nullptr;
            CloseHandle(evtPipeServer_); evtPipeServer_ = INVALID_HANDLE_VALUE;
            CloseHandle(cmdPipeServer_); cmdPipeServer_ = INVALID_HANDLE_VALUE;
            return Napi::Boolean::New(env, false);
        }
        DebugLog("initialize(): hook installed");

        // 8. Create the ready event
        readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        // 9. Start the pipe reader thread (it calls ConnectNamedPipe, then reads)
        pipeStop_          = false;
        pipeReaderThread_  = std::thread([this]() { PipeReaderThread(); });

        // 10. Post a dummy message to the renderer thread to trigger the hook
        PostThreadMessageW(rendererThreadId, WM_NULL, 0, 0);

        // 11. Wait for EvtReady (up to 5 s)
        DWORD waitResult = WaitForSingleObject(readyEvent_, 5000);
        if (waitResult != WAIT_OBJECT_0) {
            DebugLog("initialize(): timed out waiting for EvtReady");
            // Cleanup will be done in Dispose/destructor
            initOk_ = false;
        } else {
            DebugLog("initialize(): EvtReady received, init complete");
            initOk_ = true;
        }

        CloseHandle(readyEvent_);
        readyEvent_ = nullptr;

        return Napi::Boolean::New(env, initOk_);
    }

    // addMenuItem(name: string, iconName: string): void
    Napi::Value AddMenuItem(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!initOk_) return env.Undefined();

        std::string name     = info[0].As<Napi::String>().Utf8Value();
        std::string iconName = info.Length() > 1
            ? info[1].As<Napi::String>().Utf8Value()
            : std::string("scroll");

        // Payload: "name\0iconName"
        std::string payload = name + '\0' + iconName;
        WriteCmdMsg(Msg::CmdAddMenuItem, payload.data(),
                    static_cast<uint32_t>(payload.size()));
        DebugLog("addMenuItem: " + name + " (" + iconName + ")");
        return env.Undefined();
    }

    // removeMenuItem(name: string): void
    Napi::Value RemoveMenuItem(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!initOk_) return env.Undefined();

        std::string name = info[0].As<Napi::String>().Utf8Value();
        WriteCmdMsg(Msg::CmdRemoveMenuItem, name.data(),
                    static_cast<uint32_t>(name.size()));
        DebugLog("removeMenuItem: " + name);
        return env.Undefined();
    }

    // clearMenuItems(): void
    Napi::Value ClearMenuItems(const Napi::CallbackInfo& info) {
        if (!initOk_) return info.Env().Undefined();
        WriteCmdMsg(Msg::CmdClearMenuItems);
        DebugLog("clearMenuItems");
        return info.Env().Undefined();
    }

    Napi::Value OnDebug(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        debugTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "debug", 0, 1);
        return env.Undefined();
    }

    Napi::Value OnRotate(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        rotateTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "rotate", 0, 1);
        tsfnsCreated_ = true;
        return env.Undefined();
    }

    Napi::Value OnClick(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        clickTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "click", 0, 1);
        return env.Undefined();
    }

    Napi::Value OnMenuItemSelected(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        menuItemSelectedTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "menuItemSelected", 0, 1);
        return env.Undefined();
    }

    Napi::Value OnControlAcquired(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        controlAcquiredTsfn_ = Napi::ThreadSafeFunction::New(
            env, info[0].As<Napi::Function>(), "controlAcquired", 0, 1);
        return env.Undefined();
    }

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
    // PipeReaderThread
    // -----------------------------------------------------------------------
    void PipeReaderThread() {
        // Wait for DLL to connect to both pipe servers
        if (ConnectNamedPipe(evtPipeServer_, nullptr) == FALSE) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                DebugLog("PipeReaderThread: ConnectNamedPipe(evt) failed");
                return;
            }
        }
        DebugLog("PipeReaderThread: evt pipe connected");

        if (ConnectNamedPipe(cmdPipeServer_, nullptr) == FALSE) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                DebugLog("PipeReaderThread: ConnectNamedPipe(cmd) failed");
                return;
            }
        }
        DebugLog("PipeReaderThread: cmd pipe connected");

        // Read messages until stopped
        while (!pipeStop_) {
            MsgHdr hdr{};
            DWORD  totalRead = 0;
            char*  hdrbuf    = reinterpret_cast<char*>(&hdr);

            while (totalRead < sizeof(hdr)) {
                DWORD read = 0;
                if (!ReadFile(evtPipeServer_, hdrbuf + totalRead,
                              static_cast<DWORD>(sizeof(hdr)) - totalRead,
                              &read, nullptr) || read == 0) {
                    if (!pipeStop_)
                        DebugLog("PipeReaderThread: evt pipe read error (header)");
                    return;
                }
                totalRead += read;
            }

            std::string payload;
            if (hdr.dataLen > 0) {
                payload.resize(hdr.dataLen);
                totalRead = 0;
                while (totalRead < hdr.dataLen) {
                    DWORD read = 0;
                    if (!ReadFile(evtPipeServer_, &payload[totalRead],
                                  hdr.dataLen - totalRead, &read, nullptr)
                        || read == 0) {
                        if (!pipeStop_)
                            DebugLog("PipeReaderThread: evt pipe read error (payload)");
                        return;
                    }
                    totalRead += read;
                }
            }

            DispatchEvent(hdr.type, payload);
        }
    }

    // -----------------------------------------------------------------------
    // Dispatch events received from the DLL to JS callbacks
    // -----------------------------------------------------------------------
    void DispatchEvent(Msg type, const std::string& payload) {
        switch (type) {
        case Msg::EvtReady:
            DebugLog("event: EvtReady");
            if (readyEvent_) SetEvent(readyEvent_);
            break;

        case Msg::EvtRotation: {
            if (payload.size() < sizeof(double)) break;
            double delta = 0.0;
            memcpy(&delta, payload.data(), sizeof(double));
            if (rotateTsfn_) {
                rotateTsfn_.NonBlockingCall(
                    [delta](Napi::Env env, Napi::Function cb) {
                        cb.Call({ Napi::Number::New(env, delta) });
                    });
            }
            break;
        }

        case Msg::EvtButtonClicked:
            if (clickTsfn_) {
                clickTsfn_.NonBlockingCall(
                    [](Napi::Env env, Napi::Function cb) {
                        cb.Call({});
                    });
            }
            break;

        case Msg::EvtControlAcquired:
            DebugLog("event: ControlAcquired");
            if (controlAcquiredTsfn_) {
                controlAcquiredTsfn_.NonBlockingCall(
                    [](Napi::Env env, Napi::Function cb) {
                        cb.Call({});
                    });
            }
            break;

        case Msg::EvtControlLost:
            DebugLog("event: ControlLost");
            if (controlLostTsfn_) {
                controlLostTsfn_.NonBlockingCall(
                    [](Napi::Env env, Napi::Function cb) {
                        cb.Call({});
                    });
            }
            break;

        case Msg::EvtMenuItemSelected:
            if (menuItemSelectedTsfn_) {
                auto name = std::make_shared<std::string>(payload);
                menuItemSelectedTsfn_.NonBlockingCall(
                    [name](Napi::Env env, Napi::Function cb) {
                        cb.Call({ Napi::String::New(env, *name) });
                    });
            }
            break;

        case Msg::EvtDebug:
            DebugLog(payload);
            break;

        default:
            break;
        }
    }

    // -----------------------------------------------------------------------
    // WriteCmdMsg — mutex-protected write to the command pipe
    // -----------------------------------------------------------------------
    bool WriteCmdMsg(Msg type, const void* data = nullptr, uint32_t dataLen = 0) {
        std::lock_guard<std::mutex> lk(cmdMutex_);
        if (cmdPipeServer_ == INVALID_HANDLE_VALUE) return false;
        MsgHdr hdr{ type, dataLen };
        if (!WriteExact(cmdPipeServer_, &hdr, sizeof(hdr))) return false;
        if (dataLen && data) return WriteExact(cmdPipeServer_, data, dataLen);
        return true;
    }

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    void Cleanup() {
        DebugLog("cleanup() called");

        if (initOk_) {
            WriteCmdMsg(Msg::CmdShutdown);
        }

        pipeStop_ = true;

        // Closing the event pipe server will unblock any pending ReadFile in
        // PipeReaderThread, causing it to return.
        if (evtPipeServer_ != INVALID_HANDLE_VALUE) {
            CloseHandle(evtPipeServer_);
            evtPipeServer_ = INVALID_HANDLE_VALUE;
        }

        if (pipeReaderThread_.joinable())
            pipeReaderThread_.join();

        DebugLog("cleanup(): pipe reader thread joined");

        if (hook_) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }

        // Give the DLL a moment to finish teardown before we unload it
        Sleep(500);

        if (cmdPipeServer_ != INVALID_HANDLE_VALUE) {
            CloseHandle(cmdPipeServer_);
            cmdPipeServer_ = INVALID_HANDLE_VALUE;
        }

        if (hDll_) {
            FreeLibrary(hDll_);
            hDll_ = nullptr;
        }

        initOk_       = false;
        tsfnsCreated_ = false;

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

        DebugLog("cleanup(): complete");
    }

    // -----------------------------------------------------------------------
    // DebugLog
    // -----------------------------------------------------------------------
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
// Module entry point — also used as address for GetModuleHandleEx
// ---------------------------------------------------------------------------
Napi::Object ModuleInit(Napi::Env env, Napi::Object exports) {
    RadialControllerAddon::Init(env, exports);
    return exports;
}

NODE_API_MODULE(radial_controller, ModuleInit)
