import * as vscode from 'vscode';

export class DialStatusBar {
    private readonly item: vscode.StatusBarItem;

    constructor(onClickCommand: string) {
        this.item = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            100
        );
        this.item.command = onClickCommand;
        this.item.tooltip = 'Surface Dial – click to select mode';
        this.setInactive();
        this.item.show();
    }

    setActive(modeName: string): void {
        this.item.text = `$(circle-filled) Dial: ${modeName}`;
        this.item.color = new vscode.ThemeColor('statusBarItem.prominentForeground');
    }

    setInactive(): void {
        this.item.text = '$(circle-outline) Dial';
        this.item.color = undefined;
    }

    dispose(): void {
        this.item.dispose();
    }
}
