import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';

/**
 * Bookmarks provider.
 *
 * Integrates with the popular "Bookmarks" VS Code extension by alefragnani
 * (extension id: alefragnani.Bookmarks) if it is installed. Falls back to a
 * simple built-in bookmark list when that extension is not available.
 */
export class BookmarksProvider implements IDialProvider {
    readonly name = 'Bookmarks';

    /** Simple fallback bookmark store: map of file URI -> sorted line numbers. */
    private readonly bookmarks = new Map<string, number[]>();

    canHandleRotate(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    canHandleClick(_editor: vscode.TextEditor | undefined): boolean {
        return true;
    }

    async onRotateLeft(editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (await this.tryExternalCommand('bookmarks.jumpToPrevious')) {
            return true;
        }
        await this.jumpToPreviousBuiltin(editor);
        return true;
    }

    async onRotateRight(editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (await this.tryExternalCommand('bookmarks.jumpToNext')) {
            return true;
        }
        await this.jumpToNextBuiltin(editor);
        return true;
    }

    async onClick(editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (await this.tryExternalCommand('bookmarks.toggle')) {
            return true;
        }
        this.toggleBuiltin(editor);
        return true;
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /** Try to execute a command from an external extension; return false if not available. */
    private async tryExternalCommand(command: string): Promise<boolean> {
        try {
            const allCommands = await vscode.commands.getCommands(true);
            if (!allCommands.includes(command)) {
                return false;
            }
            await vscode.commands.executeCommand(command);
            return true;
        } catch {
            return false;
        }
    }

    private toggleBuiltin(editor: vscode.TextEditor | undefined): void {
        if (!editor) {
            return;
        }
        const uri = editor.document.uri.toString();
        const line = editor.selection.active.line;
        const lines = this.bookmarks.get(uri) ?? [];
        const idx = lines.indexOf(line);
        if (idx >= 0) {
            lines.splice(idx, 1);
            vscode.window.setStatusBarMessage(`Bookmark removed at line ${line + 1}`, 2000);
        } else {
            lines.push(line);
            lines.sort((a, b) => a - b);
            vscode.window.setStatusBarMessage(`Bookmark added at line ${line + 1}`, 2000);
        }
        this.bookmarks.set(uri, lines);
    }

    private async jumpToNextBuiltin(
        editor: vscode.TextEditor | undefined
    ): Promise<void> {
        if (!editor) {
            return;
        }
        const uri = editor.document.uri.toString();
        const lines = this.bookmarks.get(uri) ?? [];
        if (lines.length === 0) {
            return;
        }
        const currentLine = editor.selection.active.line;
        const next = lines.find(l => l > currentLine) ?? lines[0];
        const pos = new vscode.Position(next, 0);
        editor.selection = new vscode.Selection(pos, pos);
        editor.revealRange(new vscode.Range(pos, pos));
    }

    private async jumpToPreviousBuiltin(
        editor: vscode.TextEditor | undefined
    ): Promise<void> {
        if (!editor) {
            return;
        }
        const uri = editor.document.uri.toString();
        const lines = this.bookmarks.get(uri) ?? [];
        if (lines.length === 0) {
            return;
        }
        const currentLine = editor.selection.active.line;
        const prev =
            [...lines].reverse().find(l => l < currentLine) ??
            lines[lines.length - 1];
        const pos = new vscode.Position(prev, 0);
        editor.selection = new vscode.Selection(pos, pos);
        editor.revealRange(new vscode.Range(pos, pos));
    }
}
