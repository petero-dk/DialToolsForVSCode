import * as vscode from 'vscode';

export interface IDialProvider {
    readonly name: string;
    canHandleRotate(editor: vscode.TextEditor | undefined): boolean;
    canHandleClick(editor: vscode.TextEditor | undefined): boolean;
    onRotateLeft(editor: vscode.TextEditor | undefined): Promise<boolean>;
    onRotateRight(editor: vscode.TextEditor | undefined): Promise<boolean>;
    onClick(editor: vscode.TextEditor | undefined): Promise<boolean>;
}
