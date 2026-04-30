import * as vscode from 'vscode';
import { IDialProvider } from './baseProvider';
import { shiftColor } from './editor/colorShifter';
import { shiftNumber } from './editor/numberShifter';

interface Shifter {
    pattern: RegExp;
    shift: (text: string, direction: 1 | -1) => string | null;
}

const SHIFTERS: Shifter[] = [
    {
        pattern: /#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})\b/g,
        shift: (text, direction) => shiftColor(text, direction)
    },
    {
        pattern: /(-|\b)[0-9]+(\.[0-9]+)?/g,
        shift: (text, direction) => shiftNumber(text, direction)
    }
];

export class EditorProvider implements IDialProvider {
    readonly name = 'Editor';

    canHandleRotate(editor: vscode.TextEditor | undefined): boolean {
        if (!editor) {
            return false;
        }
        return this.findMatch(editor) !== null;
    }

    canHandleClick(editor: vscode.TextEditor | undefined): boolean {
        return editor !== undefined;
    }

    async onRotateLeft(editor: vscode.TextEditor | undefined): Promise<boolean> {
        return this.shiftValue(editor, -1);
    }

    async onRotateRight(editor: vscode.TextEditor | undefined): Promise<boolean> {
        return this.shiftValue(editor, 1);
    }

    async onClick(editor: vscode.TextEditor | undefined): Promise<boolean> {
        if (!editor) {
            return false;
        }
        await vscode.commands.executeCommand('editor.action.triggerSuggest');
        return true;
    }

    private async shiftValue(
        editor: vscode.TextEditor | undefined,
        direction: 1 | -1
    ): Promise<boolean> {
        if (!editor) {
            return false;
        }

        const match = this.findMatch(editor);
        if (!match) {
            return false;
        }

        const { range, text, shifter } = match;
        const newValue = shifter.shift(text, direction);
        if (newValue === null) {
            return false;
        }

        await editor.edit(editBuilder => {
            editBuilder.replace(range, newValue);
        });

        return true;
    }

    private findMatch(
        editor: vscode.TextEditor
    ): { range: vscode.Range; text: string; shifter: Shifter } | null {
        const position = editor.selection.active;
        const line = editor.document.lineAt(position.line);
        const lineText = line.text;
        const offset = position.character;

        for (const shifter of SHIFTERS) {
            shifter.pattern.lastIndex = 0;
            let match: RegExpExecArray | null;
            while ((match = shifter.pattern.exec(lineText)) !== null) {
                const start = match.index;
                const end = start + match[0].length;
                if (start <= offset && offset <= end) {
                    const range = new vscode.Range(
                        new vscode.Position(position.line, start),
                        new vscode.Position(position.line, end)
                    );
                    return { range, text: match[0], shifter };
                }
            }
        }
        return null;
    }
}
