# DialToolsForVSCode

## Surface Dial Tools for VS Code

A port of [DialToolsForVS](https://github.com/petero-dk/DialToolsForVS/) — adds features to VS Code specific to the [Surface Dial](https://www.microsoft.com/en-us/d/surface-dial/925R551SKTGN).

---

## Features

- **Status bar indicator** – shows the current Dial mode; click to switch modes.
- **Scroll** – scroll documents up and down.
- **Zoom** – zoom the editor font in and out, reset to default.
- **Navigate** – navigate backward and forward in editor history.
- **Debug** – start/stop the debugger, step into / over / out, and navigate breakpoints.
- **Errors** – navigate to the next/previous error, open the Problems panel.
- **Editor** – increment/decrement numbers and lighten/darken hex colors at the cursor.
- **Bookmarks** – toggle, and navigate to the next/previous bookmark. Integrates with the popular [Bookmarks](https://marketplace.visualstudio.com/items?itemName=alefragnani.Bookmarks) extension when installed.
- **Find** – find next/previous match in the editor.
- **Copilot** – navigate and accept GitHub Copilot inline suggestions.
- **UndoRedo** – undo and redo changes in the editor.

---

## Usage

The extension registers VS Code **commands** for each Dial action. Bind your Surface Dial (or any other input device / keyboard shortcut) to these commands via **File → Preferences → Keyboard Shortcuts**.

| Command | Description |
|---|---|
| `dialTools.rotateLeft` | Perform the "rotate left" action for the current mode |
| `dialTools.rotateRight` | Perform the "rotate right" action for the current mode |
| `dialTools.click` | Perform the "click" action for the current mode |
| `dialTools.nextMode` | Switch to the next mode |
| `dialTools.previousMode` | Switch to the previous mode |
| `dialTools.selectMode` | Show a quick-pick list to choose the active mode |
| `dialTools.setMode.scroll` | Activate Scroll mode |
| `dialTools.setMode.zoom` | Activate Zoom mode |
| `dialTools.setMode.navigate` | Activate Navigate mode |
| `dialTools.setMode.debug` | Activate Debug mode |
| `dialTools.setMode.errors` | Activate Errors mode |
| `dialTools.setMode.editor` | Activate Editor mode |
| `dialTools.setMode.bookmarks` | Activate Bookmarks mode |
| `dialTools.setMode.find` | Activate Find mode |
| `dialTools.setMode.copilot` | Activate Copilot mode |
| `dialTools.setMode.undoRedo` | Activate UndoRedo mode |

### Mode behaviours

#### Scroll

| Action | Behaviour |
|---|---|
| Rotate right | Scroll down (`linesToScroll` lines) |
| Rotate left | Scroll up (`linesToScroll` lines) |
| Click | Show the editor context menu |

#### Zoom

| Action | Behaviour |
|---|---|
| Rotate right | Zoom in |
| Rotate left | Zoom out |
| Click | Reset zoom to default |

#### Navigate

| Action | Behaviour |
|---|---|
| Rotate right | Navigate forward |
| Rotate left | Navigate backward |

#### Debug

| Action | Not debugging | Debugging (break mode) |
|---|---|---|
| Click | Start debugger | Step into |
| Rotate right | Go to next breakpoint | Step over |
| Rotate left | Go to previous breakpoint | Step out |

#### Errors

| Action | Behaviour |
|---|---|
| Rotate right | Go to next error |
| Rotate left | Go to previous error |
| Click | Open the Problems panel |

#### Editor (shifters)

Shifting modifies values at the cursor position.

**Numbers** (e.g. `123`, `3.14`, `.5`):
- Rotate right → increase the number
- Rotate left → decrease the number

**Hex colors** (e.g. `#ff0000`):
- Rotate right → lighten the color
- Rotate left → darken the color

#### Bookmarks

| Action | Behaviour |
|---|---|
| Rotate right | Go to next bookmark |
| Rotate left | Go to previous bookmark |
| Click | Toggle bookmark at current line |

If the [Bookmarks](https://marketplace.visualstudio.com/items?itemName=alefragnani.Bookmarks) extension is installed, its bookmark store is used; otherwise a simple built-in bookmark list is maintained per file.

#### Find

| Action | Behaviour |
|---|---|
| Rotate right | Find next match |
| Rotate left | Find previous match |
| Click | Open Find widget |

#### Copilot

| Action | Behaviour |
|---|---|
| Rotate right | Show next inline suggestion |
| Rotate left | Show previous inline suggestion |
| Click | Accept the current inline suggestion |

Requires [GitHub Copilot](https://marketplace.visualstudio.com/items?itemName=GitHub.copilot) to be installed and enabled.

#### UndoRedo

| Action | Behaviour |
|---|---|
| Rotate right | Redo |
| Rotate left | Undo |
| Click | Open Timeline panel |

---

## Settings

| Setting | Type | Default | Description |
|---|---|---|---|
| `dialTools.defaultMode` | string | `"Scroll"` | Mode active when VS Code starts |
| `dialTools.linesToScroll` | number | `3` | Lines scrolled per rotation in Scroll mode |
| `dialTools.enabledModes` | array | all modes | Ordered list of modes included in the rotation cycle |

---

## Architecture (Windows hardware integration)

The WinRT [`RadialController`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontroller) API routes Dial events to the **foreground process**. VS Code's UI is rendered by a separate Chromium renderer process (`Chrome_WidgetWin_*` window class), which is distinct from the extension host process where the `.node` addon runs. Creating a `RadialController` in the extension host would never receive events because that process is never in the foreground.

The extension solves this with a two-component native layer:

```
┌─────────────────────────────────────────────────────────┐
│  Extension Host process  (radial_controller.node)        │
│                                                          │
│  TypeScript                                              │
│    extension.ts → DialController → providers             │
│         ↕                                                │
│  nativeRadialController.ts (JS wrapper)                  │
│         ↕  Node.js addon API (NAPI / ThreadSafeFunction) │
│  radial_controller.node                                  │
│    • FindVSCodeWindow() — locates renderer HWND          │
│    • Creates named pipe servers (keyed to renderer PID)  │
│    • CopyFile → %TEMP%\radial_hook_{pid}.dll             │
│    • LoadLibrary + SetWindowsHookEx(WH_GETMESSAGE)       │
│    • PipeReaderThread — dispatches events to JS          │
│         ↕  Named pipes                                   │
├─────────────────────────────────────────────────────────┤
│       DialToolsRC-evt-{pid}   (DLL → addon)              │
│       DialToolsRC-cmd-{pid}   (addon → DLL)              │
├─────────────────────────────────────────────────────────┤
│  VS Code Renderer process  (radial_hook.dll, injected)   │
│                                                          │
│  InitDll() — run once on first GetMsgProc call           │
│    • Connects to both named pipes                        │
│    • FindRendererHwnd() — finds Chrome_WidgetWin_ HWND   │
│    • Spawns CmdReaderThread (reads addon commands)       │
│    • Runs WinRTThread (STA, owns RadialController)       │
│         ↕  WinRT / IRadialControllerInterop              │
│  RadialController::CreateForWindow(rendererHwnd)         │
│    rotation / click / menu events → pipe → addon → JS   │
└─────────────────────────────────────────────────────────┘
```

### Pipe message protocol

Frames are `[4-byte Msg type][4-byte dataLen][dataLen bytes]`, defined in `native/src/pipe_protocol.h`.

| Direction | Message | Payload |
|---|---|---|
| DLL → addon | `EvtReady` | — |
| DLL → addon | `EvtRotation` | `double` delta in degrees |
| DLL → addon | `EvtButtonClicked` | — |
| DLL → addon | `EvtMenuItemSelected` | UTF-8 item name |
| DLL → addon | `EvtControlAcquired` | — |
| DLL → addon | `EvtControlLost` | — |
| DLL → addon | `EvtShutdownComplete` | — |
| DLL → addon | `EvtDebug` | UTF-8 log string |
| Addon → DLL | `CmdAddMenuItem` | `"name\0iconName"` |
| Addon → DLL | `CmdRemoveMenuItem` | UTF-8 name |
| Addon → DLL | `CmdClearMenuItems` | — |
| Addon → DLL | `CmdShutdown` | — |

### Startup sequence

1. Addon walks the process tree to find the closest VS Code ancestor (`code.exe`) and its sibling renderer processes. Retries up to 5× with 400 ms delay to handle the case where the renderer window is not yet visible on a fast second debug session start.
2. Addon creates the two named pipe servers.
3. Addon copies `radial_hook.dll` to `%TEMP%\radial_hook_{extHostPid}.dll` (so the build output is never file-locked) and calls `LoadLibrary` on the copy.
4. Addon installs a `WH_GETMESSAGE` hook on the renderer thread via `SetWindowsHookEx`, then posts `WM_NULL` to trigger it.
5. DLL's `GetMsgProc` fires in the renderer process. On the first call it increments its own refcount (`GetModuleHandleEx`) and spawns `InitDll` on a detached thread.
6. `InitDll` connects to both pipe clients, finds `Chrome_WidgetWin_*` HWND, spawns `CmdReaderThread`, then calls `WinRTThread`.
7. `WinRTThread` acquires a named mutex (`Local\DialToolsRC-{rendererPid}`) to serialize against any previous session still tearing down. It then calls `IRadialControllerInterop::CreateForWindow`, retrying up to 6× with 500 ms delay.
8. On success: sends `EvtReady` and enters the Win32 message loop to process dial events and menu commands.
9. Addon's `PipeReaderThread` receives `EvtReady`, sets `initOk_ = true`, and signals the ready event to unblock `initialize()`.

### Shutdown sequence

1. Addon sends `CmdShutdown` via the command pipe.
2. `CmdReaderThread` in the DLL receives it, sets `g_shouldStop = true`, and posts `WM_QUIT` to `WinRTThread`.
3. `WinRTThread` exits its message loop, removes all event tokens, clears menu items, sets `controller = nullptr`, then releases the named mutex (so any new session can call `CreateForWindow` immediately).
4. DLL calls `ResetToDefaultMenuItems` to restore the system dial menu.
5. DLL sends `EvtShutdownComplete`, closes both pipe handles, calls `FreeLibraryAndExitThread` — atomically dropping its refcount and exiting without "returning into unmapped code".
6. Addon's `PipeReaderThread` receives `EvtShutdownComplete`, sets `pipeStop_ = true`, exits.
7. Addon joins `PipeReaderThread`, calls `UnhookWindowsHookEx`, `FreeLibrary`, then deletes the temp DLL copy (falls back to `MoveFileEx MOVEFILE_DELAY_UNTIL_REBOOT` if still locked).

### Robustness measures

| Mechanism | Purpose |
|---|---|
| Named mutex `Local\DialToolsRC-{rendererPid}` | Prevents race between a new session's `CreateForWindow` and an old session still tearing down in the same renderer (e.g. Reload Window) |
| DLL `CreateForWindow` retry (6×, 500 ms) | Tolerates transient WinRT state after the previous session's controller is destroyed |
| Addon `FindVSCodeWindow` retry (5×, 400 ms) | Tolerates the renderer window not yet being visible at extension activation time |
| `EvtShutdownComplete` signals `readyEvent_` | `initialize()` returns immediately on DLL init failure instead of waiting the full 10 s timeout |
| Temp DLL copy in `%TEMP%` | Build output `radial_hook.dll` is never file-locked, so `npm run rebuild-native` works at any time |
| 10 s retry loop in `extension.ts` | Reconnects automatically when the Bluetooth dial is turned on after VS Code starts |

---

## Building the native addon

The addon must be compiled before the extension can use hardware input. You need:

- Windows 10/11
- [Visual Studio](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- Node.js (the same version used by VS Code's extension host)

```powershell
npm install          # also applies the node-gyp VS version patch
npm run build-native # initial build (uses system Node headers)
```

The compiled files are written to `native/build/Release/`:

| File | Description |
|---|---|
| `radial_controller.node` | Node.js addon, loaded by the extension host |
| `radial_hook.dll` | Injected into the VS Code renderer process |

> **Note for VS 2026 users:** `node-gyp` does not yet recognise Visual Studio 2026 (version 18) out of the box. The `postinstall` script patches the local copy of `node-gyp` automatically. Re-run `node scripts/patch-node-gyp.js` if you upgrade `node-gyp` via `npm install`.

---

## Developing and debugging

### Prerequisites

```powershell
npm install
npm run build-native
npm run compile
```

### Launch the extension host

Press **F5** (or **Run Extension** from the Run & Debug panel). A new **Extension Development Host** window opens with the extension loaded. The **Surface Dial Tools** output channel in that window shows all extension and native addon logs.

You can set breakpoints in any `.ts` file under `src/` — the launch config maps compiled output back to sources via `outFiles`.

### Rebuild the native addon while VS Code is running

Because `radial_hook.dll` is loaded from a session-unique temp copy in `%TEMP%`, the build output is never locked. You can rebuild at any time without closing VS Code:

```powershell
npm run rebuild-native
```

The rebuild targets VS Code's Electron version and CPU architecture automatically. To target a specific Electron version:

```powershell
$env:VSCODE_ELECTRON_VERSION = "39.8.8"
npm run rebuild-native
```

The required version is printed in the **Surface Dial Tools** output channel on the line:
```
Native RadialController: Node ABI 140, Electron 39.8.8, Node v22.22.1, arch arm64
```

> After rebuilding, reload the Extension Development Host window (**Ctrl+Shift+P → Developer: Reload Window**) for the new binary to take effect.

### Reading extension logs

All native trace messages appear as `Native RadialController[trace]: ...` in the **Surface Dial Tools** output channel (open via **Output → Surface Dial Tools** in the Extension Development Host window).

**Important:** During `initialize()`, the extension host JS thread is blocked on a `WaitForSingleObject` call waiting for the DLL to signal `EvtReady`. Any TSFN callbacks queued during that time are not delivered until after `initialize()` returns. This means trace messages for the startup sequence appear slightly after the `"active"` confirmation line, not in strict chronological order.

### Native diagnostics with DebugView

For failures where `initialize()` returns false (e.g. WinRT init failure, renderer window not found), the TSFN callbacks may not be visible in the output channel. All log messages are also written to `OutputDebugStringW`, which is always visible in **[DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)** (Sysinternals):

1. Download and run **DebugView.exe** as Administrator.
2. Enable **Capture → Capture Win32** and **Capture → Capture Global Win32**.
3. Set a filter: `[DialTools*]` to reduce noise.
4. Press **F5** in the development VS Code to start the debug session.

Key messages to look for:

| Message | Source | Meaning |
|---|---|---|
| `native: initialize(): renderer HWND=0x... PID=... TID=...` | Addon | Which renderer window was targeted |
| `native: hook: WinRTThread started, creating RadialController` | DLL (via pipe) | DLL is running in the renderer |
| `native: hook: RadialController init HRESULT=0x... (attempt N/6, retrying)` | DLL (via pipe) | WinRT init failure with error code |
| `native: hook: EvtReady sent` | DLL (via pipe) | Init succeeded |
| `native: cleanup(): FreeLibrary(hDll_) — releasing DLL from extension host (PID ...)` | Addon | Addon unloads DLL from its process |
| `[DialTools hook] FreeLibraryAndExitThread — unloading DLL from renderer PID ...: ...` | DLL (OutputDebugStringW only) | Renderer unloads DLL; file lock released |
| `native: cleanup(): temp DLL deleted` | Addon | Temp copy cleaned up successfully |
| `native: cleanup(): DeleteFile failed (error=...)` | Addon | Temp copy still locked; scheduled for deletion on reboot |

### Common failure modes

**`initialize() returned false — WinRT init failed`**

The native addon loaded but could not create a `RadialController`. Common causes:

- The renderer window was not yet visible when `initialize()` ran. The addon retries finding the window 5× — if all attempts fail it returns false, and the 10 s extension-level retry will try again.
- The previous session's `RadialController` has not been fully released. The DLL retries `CreateForWindow` 6× with 500 ms delay. Check DebugView for the HRESULT (e.g. `0x80070578 = ERROR_INVALID_WINDOW_HANDLE`).
- The Surface Dial Bluetooth connection dropped. The extension retries `loadNativeController()` every 10 s automatically.

**`Native RadialController: load failed`**

The `.node` addon itself could not be loaded:

- **Node ABI mismatch** — run `npm run rebuild-native` to recompile against VS Code's Electron version.
- **Missing build** — run `npm run build-native`.
- **Architecture mismatch** — the `.node` file was compiled for `x64` but VS Code is running as `arm64` (or vice versa). The rebuild script detects arch from the `Code.exe` PE header automatically.

**Dial events not received after successful init**

- Check the **Surface Dial Tools** output channel for `ControlAcquired` — if it never appears, another application may be holding the RadialController.
- Confirm the dial is paired and connected via **Settings → Bluetooth & devices**.
