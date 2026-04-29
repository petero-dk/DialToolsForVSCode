import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class NavigateProvider implements IDialProvider {
    readonly name = 'Navigate';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return false;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('workbench.action.navigateBack');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('workbench.action.navigateForward');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        return false;
    }
}
