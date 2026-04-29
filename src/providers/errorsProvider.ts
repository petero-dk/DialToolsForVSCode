import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class ErrorsProvider implements IDialProvider {
    readonly name = 'Errors';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.marker.prevInFiles');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.marker.nextInFiles');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('workbench.actions.view.problems');
        return true;
    }
}
