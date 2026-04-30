import * as vscode from 'vscode';
import { DialController } from './dialController';
import { DialStatusBar } from './statusBar';
import { ScrollProvider } from './providers/scrollProvider';
import { ZoomProvider } from './providers/zoomProvider';
import { NavigateProvider } from './providers/navigateProvider';
import { DebugProvider } from './providers/debugProvider';
import { ErrorsProvider } from './providers/errorsProvider';
import { EditorProvider } from './providers/editorProvider';
import { BookmarksProvider } from './providers/bookmarksProvider';
import { FindProvider } from './providers/findProvider';

export function activate(context: vscode.ExtensionContext): void {
    const statusBar = new DialStatusBar('dialTools.selectMode');

    const providers = [
        new ScrollProvider(),
        new ZoomProvider(),
        new NavigateProvider(),
        new DebugProvider(),
        new ErrorsProvider(),
        new EditorProvider(),
        new BookmarksProvider(),
        new FindProvider()
    ];

    const controller = new DialController(providers, statusBar);

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
        ['dialTools.setMode.find', () => controller.setModeByName('Find')]
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
