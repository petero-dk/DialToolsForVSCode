import * as path from 'path';

// Known icon names accepted by the C++ addon
export type KnownIcon =
    | 'scroll' | 'zoom' | 'undoRedo' | 'volume'
    | 'nextPreviousTrack' | 'ruler' | 'inkColor'
    | 'inkThickness' | 'penType';

interface NativeAddon {
    RadialController: new () => NativeController;
}

interface NativeController {
    initialize(): boolean;
    addMenuItem(name: string, icon: KnownIcon): void;
    removeMenuItem(name: string): void;
    clearMenuItems(): void;
    onDebug(cb: (message: string) => void): void;
    onRotate(cb: (delta: number) => void): void;
    onClick(cb: () => void): void;
    onMenuItemSelected(cb: (name: string) => void): void;
    onControlAcquired(cb: () => void): void;
    onControlLost(cb: () => void): void;
    dispose(): void;
}

export interface DialHardwareEvents {
    onRotate(cb: (delta: number) => void): void;
    onButtonClick(cb: () => void): void;
    onMenuItemSelected(cb: (name: string) => void): void;
    onControlAcquired(cb: () => void): void;
    onControlLost(cb: () => void): void;
    addMenuItem(name: string, icon: KnownIcon): void;
    removeMenuItem(name: string): void;
    clearMenuItems(): void;
    dispose(): void;
}

export function loadNativeController(
    log: (msg: string) => void = () => {}
): DialHardwareEvents | null {
    if (process.platform !== 'win32') {
        log('Native RadialController: skipped (non-Windows)');
        return null;
    }

    const electronVer = process.versions.electron ?? 'n/a';
    log(`Native RadialController: Node ABI ${process.versions.modules}, ` +
        `Electron ${electronVer}, Node ${process.version}, arch ${process.arch}`);

    const addonPath = path.join(
        __dirname, '..', 'native', 'build', 'Release', 'radial_controller.node'
    );
    log(`Native RadialController: loading ${addonPath}`);

    let addon: NativeAddon;
    try {
        // eslint-disable-next-line @typescript-eslint/no-var-requires
        addon = require(addonPath) as NativeAddon;
    } catch (err) {
        log(`Native RadialController: load failed — ${err}`);
        if (electronVer !== 'n/a') {
            log(`Hint: run: VSCODE_ELECTRON_VERSION=${electronVer} npm run rebuild-native`);
        } else {
            log('Hint: run "npm run rebuild-native" to recompile against VS Code\'s Node ABI.');
        }
        return null;
    }

    const ctrl = new addon.RadialController();
    ctrl.onDebug((message: string) => {
        log(`Native RadialController[trace]: ${message}`);
    });

    const ok = ctrl.initialize();
    if (!ok) {
        log('Native RadialController: initialize() returned false — WinRT init failed.');
        ctrl.dispose();
        return null;
    }

    log('Native RadialController: active.');
    return {
        onRotate:            cb  => ctrl.onRotate(cb),
        onButtonClick:       cb  => ctrl.onClick(cb),
        onMenuItemSelected:  cb  => ctrl.onMenuItemSelected(cb),
        onControlAcquired:   cb  => ctrl.onControlAcquired(cb),
        onControlLost:       cb  => ctrl.onControlLost(cb),
        addMenuItem:    (n, i)   => ctrl.addMenuItem(n, i),
        removeMenuItem:      n   => ctrl.removeMenuItem(n),
        clearMenuItems:      ()  => ctrl.clearMenuItems(),
        dispose:             ()  => ctrl.dispose(),
    };
}
