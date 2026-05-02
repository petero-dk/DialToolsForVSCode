// radial_hook.dll — injected into VS Code's renderer process via SetWindowsHookEx.
//
// The Shell routes RadialController events to the foreground process. VS Code's
// renderer (a Chrome_WidgetWin_* window) is a different process from the extension
// host (where our .node addon runs). By injecting this DLL into the renderer process
// and creating the RadialController from there, we get correct routing without any
// SetForegroundWindow hacks.
//
// Communication with the addon is via two named pipes:
//   Events  pipe (DLL writes, addon reads): \\.\pipe\DialToolsRC-evt-{rendererPid}
//   Commands pipe (DLL reads, addon writes): \\.\pipe\DialToolsRC-cmd-{rendererPid}
//
// Pipe messages are framed: [4-byte Msg type][4-byte dataLen][dataLen bytes data]

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <ole2.h>

// C++/WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Storage.Streams.h>

// Win32 interop headers
#include <RadialControllerInterop.h>

#include <atomic>
#include <mutex>
#include <string>
#include <map>
#include <sstream>
#include <thread>

#include "pipe_protocol.h"

#pragma comment(lib, "RuntimeObject.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace DialTools;

// ---------------------------------------------------------------------------
// Module globals
// ---------------------------------------------------------------------------
static HMODULE        g_hModule      = nullptr;
static std::atomic<bool> g_initDone  { false };   // ensures InitDll runs once
static std::atomic<bool> g_shouldStop{ false };

static HANDLE         g_evtPipe      = INVALID_HANDLE_VALUE;  // DLL writes events
static HANDLE         g_cmdPipe      = INVALID_HANDLE_VALUE;  // DLL reads commands
static std::mutex     g_evtMutex;

static DWORD          g_winrtThreadId = 0;

// WM_USER offsets for inter-thread messaging within this DLL
constexpr UINT WM_ADDMENUITEM    = WM_USER + 1;
constexpr UINT WM_REMOVEMENUITEM = WM_USER + 2;
constexpr UINT WM_CLEARMENU      = WM_USER + 3;

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
// Pipe write helpers — called from any thread; mutex-protected
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

static void SendMsg(Msg type, const void* data = nullptr, uint32_t dataLen = 0) {
    std::lock_guard<std::mutex> lk(g_evtMutex);
    if (g_evtPipe == INVALID_HANDLE_VALUE) return;
    MsgHdr hdr{ type, dataLen };
    if (!WriteExact(g_evtPipe, &hdr, sizeof(hdr))) return;
    if (dataLen && data) WriteExact(g_evtPipe, data, dataLen);
}

static void SendDebug(const std::string& msg) {
    SendMsg(Msg::EvtDebug, msg.data(), static_cast<uint32_t>(msg.size()));
}

// ---------------------------------------------------------------------------
// FindRendererHwnd — find first visible Chrome_WidgetWin_* in current process
// ---------------------------------------------------------------------------
static HWND FindRendererHwnd() {
    struct Ctx { DWORD pid; HWND found; };
    Ctx ctx{ GetCurrentProcessId(), nullptr };

    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto& c = *reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != c.pid) return TRUE;

        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcsncmp(cls, L"Chrome_WidgetWin_", 17) != 0) return TRUE;

        c.found = hwnd;
        return FALSE; // stop on first match
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

// ---------------------------------------------------------------------------
// CmdReaderThread — reads commands from the addon and posts to WinRT thread
// ---------------------------------------------------------------------------
static void CmdReaderThread() {
    while (!g_shouldStop) {
        MsgHdr hdr{};
        DWORD  read = 0;

        // Read header
        DWORD totalRead = 0;
        const char* hdrbuf = reinterpret_cast<char*>(&hdr);
        while (totalRead < sizeof(hdr)) {
            if (!ReadFile(g_cmdPipe, (void*)(hdrbuf + totalRead),
                          static_cast<DWORD>(sizeof(hdr)) - totalRead, &read, nullptr)
                || read == 0) {
                // pipe broken or shutdown
                g_shouldStop = true;
                if (g_winrtThreadId)
                    PostThreadMessageW(g_winrtThreadId, WM_QUIT, 0, 0);
                return;
            }
            totalRead += read;
        }

        // Read payload
        std::string payload;
        if (hdr.dataLen > 0) {
            payload.resize(hdr.dataLen);
            totalRead = 0;
            while (totalRead < hdr.dataLen) {
                if (!ReadFile(g_cmdPipe, &payload[totalRead],
                              hdr.dataLen - totalRead, &read, nullptr)
                    || read == 0) {
                    g_shouldStop = true;
                    if (g_winrtThreadId)
                        PostThreadMessageW(g_winrtThreadId, WM_QUIT, 0, 0);
                    return;
                }
                totalRead += read;
            }
        }

        switch (hdr.type) {
        case Msg::CmdShutdown:
            SendDebug("hook: received CmdShutdown");
            g_shouldStop = true;
            if (g_winrtThreadId)
                PostThreadMessageW(g_winrtThreadId, WM_QUIT, 0, 0);
            return;

        case Msg::CmdAddMenuItem: {
            // payload: "name\0iconName" — two null-terminated UTF-8 strings
            // Heap-allocate a payload struct and post to WinRT thread.
            struct AddPayload { std::wstring name; RadialControllerMenuKnownIcon icon; };

            // Find the split point
            const char* p   = payload.data();
            size_t      len = payload.size();
            size_t      nameEnd = 0;
            while (nameEnd < len && p[nameEnd] != '\0') ++nameEnd;
            std::string nameUtf8(p, nameEnd);
            std::string iconUtf8;
            if (nameEnd + 1 < len)
                iconUtf8 = std::string(p + nameEnd + 1, len - nameEnd - 1);
            // Strip trailing null from iconUtf8 if present
            while (!iconUtf8.empty() && iconUtf8.back() == '\0')
                iconUtf8.pop_back();

            std::wstring wname(nameUtf8.begin(), nameUtf8.end());
            auto* ap = new AddPayload{ wname, IconFromString(iconUtf8) };
            if (g_winrtThreadId)
                PostThreadMessageW(g_winrtThreadId, WM_ADDMENUITEM,
                                   reinterpret_cast<WPARAM>(ap), 0);
            else
                delete ap;
            break;
        }

        case Msg::CmdRemoveMenuItem: {
            // payload: UTF-8 name
            std::wstring* wname = new std::wstring(payload.begin(), payload.end());
            if (g_winrtThreadId)
                PostThreadMessageW(g_winrtThreadId, WM_REMOVEMENUITEM,
                                   reinterpret_cast<WPARAM>(wname), 0);
            else
                delete wname;
            break;
        }

        case Msg::CmdClearMenuItems:
            if (g_winrtThreadId)
                PostThreadMessageW(g_winrtThreadId, WM_CLEARMENU, 0, 0);
            break;

        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// WinRTThread — STA thread that owns the RadialController
// ---------------------------------------------------------------------------
static void WinRTThread(HWND rendererHwnd) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        SendDebug("hook: CoInitializeEx failed");
        return;
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    g_winrtThreadId = GetCurrentThreadId();

    // Prime the thread message queue so PostThreadMessage works before
    // GetMessage is called.
    {
        MSG primeMsg{};
        PeekMessageW(&primeMsg, nullptr, WM_USER, WM_USER + 10, PM_NOREMOVE);
    }

    SendDebug("hook: WinRTThread started, creating RadialController");

    RadialController            controller{ nullptr };
    RadialControllerConfiguration config{ nullptr };

    struct MenuItemEntry {
        RadialControllerMenuItem item{ nullptr };
        winrt::event_token       invokedToken{};
    };
    std::map<std::wstring, MenuItemEntry> menuItems;

    winrt::event_token rotationToken{};
    winrt::event_token clickToken{};
    winrt::event_token acquiredToken{};
    winrt::event_token lostToken{};

    // Create RadialController for the renderer window
    try {
        auto controllerInterop =
            winrt::get_activation_factory<RadialController,
                                          IRadialControllerInterop>();
        winrt::check_hresult(
            controllerInterop->CreateForWindow(
                rendererHwnd,
                winrt::guid_of<RadialController>(),
                winrt::put_abi(controller)));

        auto configInterop =
            winrt::get_activation_factory<RadialControllerConfiguration,
                                          IRadialControllerConfigurationInterop>();
        winrt::check_hresult(
            configInterop->GetForWindow(
                rendererHwnd,
                winrt::guid_of<RadialControllerConfiguration>(),
                winrt::put_abi(config)));

        config.SetDefaultMenuItems(
            winrt::single_threaded_vector<RadialControllerSystemMenuItemKind>());
        SendDebug("hook: RadialController created, system menu items cleared");
    } catch (const winrt::hresult_error& e) {
        std::ostringstream ss;
        ss << "hook: RadialController init failed HRESULT=0x"
           << std::hex << static_cast<unsigned long>(e.code());
        SendDebug(ss.str());
        winrt::uninit_apartment();
        CoUninitialize();
        return;
    } catch (...) {
        SendDebug("hook: RadialController init failed (unknown exception)");
        winrt::uninit_apartment();
        CoUninitialize();
        return;
    }

    // Helper: send a named pipe event with a string payload
    auto sendStrEvent = [](Msg msg, const std::string& s) {
        SendMsg(msg, s.data(), static_cast<uint32_t>(s.size()));
    };

    // Wire up events
    rotationToken = controller.RotationChanged(
        [&](RadialController const&,
            RadialControllerRotationChangedEventArgs const& args) {
            double delta = args.RotationDeltaInDegrees();
            SendMsg(Msg::EvtRotation, &delta, sizeof(delta));
        });

    clickToken = controller.ButtonClicked(
        [&](RadialController const&,
            RadialControllerButtonClickedEventArgs const&) {
            SendMsg(Msg::EvtButtonClicked);
        });

    acquiredToken = controller.ControlAcquired(
        [&](RadialController const&, auto const&) {
            SendDebug("hook: ControlAcquired");
            SendMsg(Msg::EvtControlAcquired);
        });

    lostToken = controller.ControlLost(
        [&](RadialController const&, auto const&) {
            SendDebug("hook: ControlLost");
            SendMsg(Msg::EvtControlLost);
        });

    // Signal ready
    SendMsg(Msg::EvtReady);
    SendDebug("hook: EvtReady sent, entering message loop");

    // Menu management helpers (called from message loop, same thread as controller)
    auto doAddMenuItem = [&](std::wstring wname, RadialControllerMenuKnownIcon icon) {
        try {
            if (menuItems.count(wname) == 0) {
                auto item = RadialControllerMenuItem::CreateFromKnownIcon(
                    winrt::hstring(wname), icon);

                auto token = item.Invoked(
                    [&sendStrEvent, wname](RadialControllerMenuItem const&,
                                          auto const&) {
                        std::string narrow(wname.begin(), wname.end());
                        sendStrEvent(Msg::EvtMenuItemSelected, narrow);
                    });

                controller.Menu().Items().Append(item);
                menuItems[wname] = { item, token };

                if (menuItems.size() == 1) {
                    try {
                        controller.Menu().SelectMenuItem(item);
                    } catch (...) {
                        SendDebug("hook: failed to select first menu item");
                    }
                }
                std::string nm(wname.begin(), wname.end());
                SendDebug("hook: menu item added: " + nm);
            }
        } catch (...) {
            SendDebug("hook: addMenuItem failed");
        }
    };

    auto doRemoveMenuItem = [&](const std::wstring& wname) {
        auto it = menuItems.find(wname);
        if (it == menuItems.end()) return;
        it->second.item.Invoked(it->second.invokedToken);
        auto& items = controller.Menu().Items();
        uint32_t idx{};
        if (items.IndexOf(it->second.item, idx)) items.RemoveAt(idx);
        menuItems.erase(it);
        std::string nm(wname.begin(), wname.end());
        SendDebug("hook: menu item removed: " + nm);
    };

    auto doClearMenuItems = [&]() {
        auto& items = controller.Menu().Items();
        for (auto& [k, v] : menuItems) {
            v.item.Invoked(v.invokedToken);
            uint32_t idx{};
            if (items.IndexOf(v.item, idx)) items.RemoveAt(idx);
        }
        menuItems.clear();
        SendDebug("hook: menu items cleared");
    };

    // Message loop
    MSG msg;
    while (!g_shouldStop && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr) {
            // Thread message
            switch (msg.message) {
            case WM_ADDMENUITEM: {
                struct AddPayload { std::wstring name; RadialControllerMenuKnownIcon icon; };
                auto* p = reinterpret_cast<AddPayload*>(msg.wParam);
                doAddMenuItem(p->name, p->icon);
                delete p;
                break;
            }
            case WM_REMOVEMENUITEM: {
                auto* p = reinterpret_cast<std::wstring*>(msg.wParam);
                doRemoveMenuItem(*p);
                delete p;
                break;
            }
            case WM_CLEARMENU:
                doClearMenuItems();
                break;
            default:
                break;
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    SendDebug("hook: WinRTThread exiting, tearing down");

    // Teardown
    if (controller) {
        controller.RotationChanged(rotationToken);
        controller.ButtonClicked(clickToken);
        controller.ControlAcquired(acquiredToken);
        controller.ControlLost(lostToken);
        // Remove remaining menu items
        doClearMenuItems();
        controller = nullptr;
    }
    config = nullptr;

    winrt::uninit_apartment();
    CoUninitialize();
    g_winrtThreadId = 0;
}

// ---------------------------------------------------------------------------
// InitDll — called once from a detached thread spawned by GetMsgProc
// ---------------------------------------------------------------------------
static void InitDll() {
    DWORD pid = GetCurrentProcessId();

    // Build pipe names
    std::wstring evtPipeName = L"\\\\.\\pipe\\DialToolsRC-evt-" + std::to_wstring(pid);
    std::wstring cmdPipeName = L"\\\\.\\pipe\\DialToolsRC-cmd-" + std::to_wstring(pid);

    // Connect to events pipe (addon is server, PIPE_ACCESS_INBOUND — we write GENERIC_WRITE)
    for (int attempt = 0; attempt < 20; ++attempt) {
        g_evtPipe = CreateFileW(
            evtPipeName.c_str(),
            GENERIC_WRITE,
            0, nullptr,
            OPEN_EXISTING,
            0, nullptr);
        if (g_evtPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(evtPipeName.c_str(), 1000);
        else
            Sleep(200);
    }
    if (g_evtPipe == INVALID_HANDLE_VALUE) {
        // Nothing we can do — output to debugger
        OutputDebugStringW(L"[DialTools hook] Failed to connect events pipe\n");
        return;
    }

    // Connect to commands pipe (addon is server, PIPE_ACCESS_OUTBOUND — we read GENERIC_READ)
    for (int attempt = 0; attempt < 20; ++attempt) {
        g_cmdPipe = CreateFileW(
            cmdPipeName.c_str(),
            GENERIC_READ,
            0, nullptr,
            OPEN_EXISTING,
            0, nullptr);
        if (g_cmdPipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(cmdPipeName.c_str(), 1000);
        else
            Sleep(200);
    }
    if (g_cmdPipe == INVALID_HANDLE_VALUE) {
        SendDebug("hook: Failed to connect commands pipe");
        CloseHandle(g_evtPipe);
        g_evtPipe = INVALID_HANDLE_VALUE;
        return;
    }

    SendDebug("hook: both pipes connected");

    // Find the renderer HWND
    HWND rendererHwnd = FindRendererHwnd();
    if (!rendererHwnd) {
        SendDebug("hook: renderer HWND not found, aborting");
        CloseHandle(g_evtPipe); g_evtPipe = INVALID_HANDLE_VALUE;
        CloseHandle(g_cmdPipe); g_cmdPipe = INVALID_HANDLE_VALUE;
        return;
    }

    {
        std::ostringstream ss;
        ss << "hook: renderer HWND=0x" << std::hex
           << reinterpret_cast<uintptr_t>(rendererHwnd);
        SendDebug(ss.str());
    }

    // Start command reader thread (detached; it will post to the WinRT thread)
    std::thread(CmdReaderThread).detach();

    // Run the WinRT thread (blocks until shutdown)
    WinRTThread(rendererHwnd);

    // Cleanup
    CloseHandle(g_evtPipe); g_evtPipe = INVALID_HANDLE_VALUE;
    CloseHandle(g_cmdPipe); g_cmdPipe = INVALID_HANDLE_VALUE;
}

// ---------------------------------------------------------------------------
// Exported hook procedure — installed via SetWindowsHookEx(WH_GETMESSAGE)
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport)
LRESULT CALLBACK GetMsgProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // On first invocation, spawn the init thread
    bool expected = false;
    if (g_initDone.compare_exchange_strong(expected, true)) {
        std::thread(InitDll).detach();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        // Best-effort cleanup; avoid complex operations here
        g_shouldStop = true;
        break;
    }
    return TRUE;
}
