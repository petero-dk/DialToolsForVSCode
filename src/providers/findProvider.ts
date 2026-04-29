import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class FindProvider implements IDialProvider {
    readonly name = 'Find';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.previousMatchFindAction');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.nextMatchFindAction');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('actions.find');
        return true;
    }
}
