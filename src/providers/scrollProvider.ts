import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class ScrollProvider implements IDialProvider {
    readonly name = 'Scroll';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(editor: vscode.TextEditor | undefined): Promise<boolean> {
        const linesToScroll = vscode.workspace
            .getConfiguration('dialTools')
            .get<number>('linesToScroll', 3);

        if (editor) {
            for (let i = 0; i < linesToScroll; i++) {
                await vscode.commands.executeCommand('scrollLineUp');
            }
        } else {
            await vscode.commands.executeCommand('workbench.action.scrollPanelUp');
        }
        return true;
    }

    async onRotateRight(editor: vscode.TextEditor | undefined): Promise<boolean> {
        const linesToScroll = vscode.workspace
            .getConfiguration('dialTools')
            .get<number>('linesToScroll', 3);

        if (editor) {
            for (let i = 0; i < linesToScroll; i++) {
                await vscode.commands.executeCommand('scrollLineDown');
            }
        } else {
            await vscode.commands.executeCommand('workbench.action.scrollPanelDown');
        }
        return true;
    }

    async onClick(editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (editor) {
            await vscode.commands.executeCommand('editor.action.showContextMenu');
        } else {
            await vscode.commands.executeCommand('list.select');
        }
        return true;
    }
}
