import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

export class DebugProvider implements IDialProvider {
    readonly name = 'Debug';

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    private isDebugging(): boolean {
        return vscode.debug.activeDebugSession !== undefined;
    }

    async onRotateLeft(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (this.isDebugging()) {
            await vscode.commands.executeCommand('workbench.action.debug.stepOut');
        } else {
            await this.moveToPreviousBreakpoint();
        }
        return true;
    }

    async onRotateRight(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (this.isDebugging()) {
            await vscode.commands.executeCommand('workbench.action.debug.stepOver');
        } else {
            await this.moveToNextBreakpoint();
        }
        return true;
    }

    async onClick(_editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (this.isDebugging()) {
            await vscode.commands.executeCommand('workbench.action.debug.stepInto');
        } else {
            await vscode.commands.executeCommand('workbench.action.debug.start');
        }
        return true;
    }

    private async moveToNextBreakpoint(): Promise<void> {
        const breakpoints = vscode.debug.breakpoints
            .filter((bp): bp is vscode.SourceBreakpoint => bp instanceof vscode.SourceBreakpoint)
            .sort((a, b) => {
                const fileCompare = a.location.uri.fsPath.localeCompare(b.location.uri.fsPath);
                if (fileCompare !== 0) {
                    return fileCompare;
                }
                return a.location.range.start.line - b.location.range.start.line;
            });

        if (breakpoints.length === 0) {
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            await this.openBreakpoint(breakpoints[0]);
            return;
        }

        const currentFile = editor.document.uri.fsPath;
        const currentLine = editor.selection.active.line;

        const next = breakpoints.find(bp =>
            bp.location.uri.fsPath > currentFile ||
            (bp.location.uri.fsPath === currentFile && bp.location.range.start.line > currentLine)
        ) ?? breakpoints[0];

        await this.openBreakpoint(next);
    }

    private async moveToPreviousBreakpoint(): Promise<void> {
        const breakpoints = vscode.debug.breakpoints
            .filter((bp): bp is vscode.SourceBreakpoint => bp instanceof vscode.SourceBreakpoint)
            .sort((a, b) => {
                const fileCompare = a.location.uri.fsPath.localeCompare(b.location.uri.fsPath);
                if (fileCompare !== 0) {
                    return fileCompare;
                }
                return a.location.range.start.line - b.location.range.start.line;
            });

        if (breakpoints.length === 0) {
            return;
        }

        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            await this.openBreakpoint(breakpoints[breakpoints.length - 1]);
            return;
        }

        const currentFile = editor.document.uri.fsPath;
        const currentLine = editor.selection.active.line;

        const prev = [...breakpoints].reverse().find(bp =>
            bp.location.uri.fsPath < currentFile ||
            (bp.location.uri.fsPath === currentFile && bp.location.range.start.line < currentLine)
        ) ?? breakpoints[breakpoints.length - 1];

        await this.openBreakpoint(prev);
    }

    private async openBreakpoint(bp: vscode.SourceBreakpoint): Promise<void> {
        const doc = await vscode.workspace.openTextDocument(bp.location.uri);
        await vscode.window.showTextDocument(doc, {
            selection: bp.location.range
        });
    }
}
