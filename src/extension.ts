import * as vscode from 'vscode';
import { DialController } from './dialController';
import { DialStatusBar } from './statusBar';
import { loadNativeController } from './nativeRadialController';
import { ScrollProvider } from './providers/scrollProvider';
import { ZoomProvider } from './providers/zoomProvider';
import { NavigateProvider } from './providers/navigateProvider';
import { DebugProvider } from './providers/debugProvider';
import { ErrorsProvider } from './providers/errorsProvider';
import { EditorProvider } from './providers/editorProvider';
import { BookmarksProvider } from './providers/bookmarksProvider';
import { FindProvider } from './providers/findProvider';
import { CopilotProvider } from './providers/copilotProvider';
import { UndoRedoProvider } from './providers/undoRedoProvider';

export function activate(context: vscode.ExtensionContext): void {
    const output = vscode.window.createOutputChannel('Surface Dial Tools');
    context.subscriptions.push(output);
    const log = (msg: string) => {
        output.appendLine(msg);
        console.log('[DialTools]', msg);
    };
    output.show(true); // reveal without stealing focus

    const statusBar = new DialStatusBar('dialTools.selectMode');

    const providers = [
        new ScrollProvider(),
        new ZoomProvider(),
        new NavigateProvider(),
        new DebugProvider(),
        new ErrorsProvider(),
        new EditorProvider(),
        new BookmarksProvider(),
        new FindProvider(),
        new CopilotProvider(),
        new UndoRedoProvider()
    ];

    const hardware   = loadNativeController(log);
    const controller = new DialController(providers, statusBar, hardware, log);

    const cmds: [string, () => void | Promise<void>][] = [
        ['dialTools.rotateLeft', () => controller.rotateLeft()],
        ['dialTools.rotateRight', () => controller.rotateRight()],
        ['dialTools.click', () => controller.click()],
        ['dialTools.nextMode', () => controller.nextMode()],
        ['dialTools.previousMode', () => controller.previousMode()],
        ['dialTools.selectMode', () => controller.selectMode()],
        ['dialTools.setMode.scroll', () => controller.setModeByName('Scroll')],
        ['dialTools.setMode.zoom', () => controller.setModeByName('Zoom')],
        ['dialTools.setMode.navigate', () => controller.setModeByName('Navigate')],
        ['dialTools.setMode.debug', () => controller.setModeByName('Debug')],
        ['dialTools.setMode.errors', () => controller.setModeByName('Errors')],
        ['dialTools.setMode.editor', () => controller.setModeByName('Editor')],
        ['dialTools.setMode.bookmarks', () => controller.setModeByName('Bookmarks')],
        ['dialTools.setMode.find', () => controller.setModeByName('Find')],
        ['dialTools.setMode.copilot', () => controller.setModeByName('Copilot')],
        ['dialTools.setMode.undoRedo', () => controller.setModeByName('UndoRedo')]
    ];

    for (const [id, handler] of cmds) {
        context.subscriptions.push(
            vscode.commands.registerCommand(id, handler)
        );
    }

    context.subscriptions.push(controller);
}

export function deactivate(): void {
    // Cleanup is handled via context.subscriptions
}
