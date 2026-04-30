import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class CopilotProvider implements IDialProvider {
    readonly name = 'Copilot';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.inlineSuggest.showPrevious');
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.inlineSuggest.showNext');
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        await vscode.commands.executeCommand('editor.action.inlineSuggest.commit');
        return true;
    }
}
