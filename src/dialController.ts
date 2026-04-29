import * as vscode from 'vscode';
import { IDialProvider } from './providers/baseProvider';
import { DialStatusBar } from './statusBar';

export class DialController implements vscode.Disposable {
    private providers: IDialProvider[];
    private currentProviderIndex: number;
    private readonly statusBar: DialStatusBar;
    private readonly disposables: vscode.Disposable[] = [];

    constructor(
        providers: IDialProvider[],
        statusBar: DialStatusBar
    ) {
        this.statusBar = statusBar;
        this.providers = this.buildEnabledProviders(providers);
        this.currentProviderIndex = this.resolveDefaultIndex();
        this.updateStatusBar();

        // Rebuild provider list when settings change
        this.disposables.push(
            vscode.workspace.onDidChangeConfiguration(e => {
                if (
                    e.affectsConfiguration('dialTools.enabledModes') ||
                    e.affectsConfiguration('dialTools.defaultMode')
                ) {
                    this.providers = this.buildEnabledProviders(providers);
                    this.currentProviderIndex = this.resolveDefaultIndex();
                    this.updateStatusBar();
                }
            })
        );
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    async rotateLeft(): Promise<void> {
        const provider = this.currentProvider;
        const editor = vscode.window.activeTextEditor;
        if (!provider.canHandleRotate(editor)) {
            return;
        }
        try {
            await provider.onRotateLeft(editor);
        } catch (err) {
            vscode.window.showErrorMessage(
                `Dial (${provider.name}): rotate-left failed – ${err}`
            );
        }
    }

    async rotateRight(): Promise<void> {
        const provider = this.currentProvider;
        const editor = vscode.window.activeTextEditor;
        if (!provider.canHandleRotate(editor)) {
            return;
        }
        try {
            await provider.onRotateRight(editor);
        } catch (err) {
            vscode.window.showErrorMessage(
                `Dial (${provider.name}): rotate-right failed – ${err}`
            );
        }
    }

    async click(): Promise<void> {
        const provider = this.currentProvider;
        const editor = vscode.window.activeTextEditor;
        if (!provider.canHandleClick(editor)) {
            return;
        }
        try {
            await provider.onClick(editor);
        } catch (err) {
            vscode.window.showErrorMessage(
                `Dial (${provider.name}): click failed – ${err}`
            );
        }
    }

    nextMode(): void {
        if (this.providers.length === 0) {
            return;
        }
        this.currentProviderIndex =
            (this.currentProviderIndex + 1) % this.providers.length;
        this.updateStatusBar();
        this.showModeNotification();
    }

    previousMode(): void {
        if (this.providers.length === 0) {
            return;
        }
        this.currentProviderIndex =
            (this.currentProviderIndex - 1 + this.providers.length) %
            this.providers.length;
        this.updateStatusBar();
        this.showModeNotification();
    }

    setModeByName(name: string): boolean {
        const idx = this.providers.findIndex(
            p => p.name.toLowerCase() === name.toLowerCase()
        );
        if (idx < 0) {
            return false;
        }
        this.currentProviderIndex = idx;
        this.updateStatusBar();
        this.showModeNotification();
        return true;
    }

    async selectMode(): Promise<void> {
        const items = this.providers.map((p, i) => ({
            label: p.name,
            description: i === this.currentProviderIndex ? '(current)' : ''
        }));
        const picked = await vscode.window.showQuickPick(items, {
            placeHolder: 'Select Dial mode'
        });
        if (picked) {
            this.setModeByName(picked.label);
        }
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private get currentProvider(): IDialProvider {
        return this.providers[this.currentProviderIndex];
    }

    private buildEnabledProviders(allProviders: IDialProvider[]): IDialProvider[] {
        const cfg = vscode.workspace.getConfiguration('dialTools');
        const enabledNames: string[] = cfg.get('enabledModes', [
            'Scroll', 'Zoom', 'Navigate', 'Debug', 'Errors', 'Editor', 'Bookmarks', 'Find'
        ]);

        const ordered = enabledNames
            .map(name => allProviders.find(p => p.name === name))
            .filter((p): p is IDialProvider => p !== undefined);

        return ordered.length > 0 ? ordered : allProviders;
    }

    private resolveDefaultIndex(): number {
        const cfg = vscode.workspace.getConfiguration('dialTools');
        const defaultMode: string = cfg.get('defaultMode', 'Scroll');
        const idx = this.providers.findIndex(
            p => p.name.toLowerCase() === defaultMode.toLowerCase()
        );
        return idx >= 0 ? idx : 0;
    }

    private updateStatusBar(): void {
        if (this.providers.length === 0) {
            this.statusBar.setInactive();
        } else {
            this.statusBar.setActive(this.currentProvider.name);
        }
    }

    private showModeNotification(): void {
        if (this.providers.length > 0) {
            vscode.window.setStatusBarMessage(
                `Dial mode: ${this.currentProvider.name}`,
                2000
            );
        }
    }

    dispose(): void {
        this.statusBar.dispose();
        this.disposables.forEach(d => d.dispose());
    }
}
