import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class ZoomProvider implements IDialProvider {
    readonly name = 'Zoom';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.fontZoomOut');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.fontZoomIn');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.fontZoomReset');
        return true;
    }
}
