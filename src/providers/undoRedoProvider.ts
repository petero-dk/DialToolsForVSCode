import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class UndoRedoProvider implements IDialProvider {
    readonly name = 'UndoRedo';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('undo');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('redo');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('workbench.view.timeline');
        return true;
    }
}
