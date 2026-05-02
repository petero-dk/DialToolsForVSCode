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

## Hardware integration (Windows)

The extension includes a native C++/WinRT addon that registers directly with the Windows [`RadialController`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.radialcontroller) API. On startup the extension:

1. Loads `native/build/Release/radial_controller.node`.
2. Creates a `RadialController` bound to a hidden message window using `IRadialControllerInterop::CreateForWindow`.
3. Populates the dial's overlay menu (hold the dial to open it) with one item per enabled mode.
4. Receives rotation and click events directly from the hardware — no keyboard shortcut configuration required.

If the native addon cannot be loaded (non-Windows OS, missing build, or unsupported hardware), the extension falls back silently and continues to work via keyboard shortcuts bound to the registered VS Code commands.

---

## Building the native addon

The addon must be compiled before the extension can use hardware input. You need:

- Windows 10/11
- [Visual Studio](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- Node.js (the same version used by VS Code's extension host)

```
npm install       # also runs the node-gyp VS version patch automatically
npm run build-native
```

The compiled addon is written to `native/build/Release/radial_controller.node`.

> **Note for VS 2026 users:** `node-gyp` does not yet recognise Visual Studio 2026 (version 18) out of the box. The `postinstall` script in this repo patches the local copy of `node-gyp` automatically. If you upgrade `node-gyp` via `npm install`, re-run `node scripts/patch-node-gyp.js` to reapply the patch.

---

## Developing and debugging

### Prerequisites

```
npm install
npm run build-native
npm run compile
```

### Launch the extension host

Press **F5** (or run **Run Extension** from the Run & Debug panel). This opens a new VS Code window with the extension loaded. The **Extension Development Host** window title confirms it is active.

You can set breakpoints in any `.ts` file under `src/` — the launch config maps compiled output back to sources automatically via `outFiles`.

### Diagnose native addon load failures

If the dial menu items do not appear, check whether the native addon loaded successfully:

1. Open **Help → Toggle Developer Tools** in the Extension Development Host window.
2. In the Console tab, look for errors starting with `radial_controller`.
3. Common causes:
   - `radial_controller.node` was not built — run `npm run build-native`.
   - Node.js ABI mismatch — rebuild with `npm run rebuild-native` to target the correct Node version.
   - Running on a non-Windows OS — the extension skips hardware init silently.

### Logging from the native addon

The extension host output is visible in **Output → Log (Extension Host)**. The `loadNativeController` wrapper in `src/nativeRadialController.ts` catches load errors; add a `console.error` there to surface them during development.

