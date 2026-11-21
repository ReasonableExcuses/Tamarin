import * as path from 'path';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('tamarin');
    let lspPath = config.get<string>('lsp.path') || 'tamarin-lsp';

    // If using default name and workspace has local build binary, prefer it
    if (lspPath === 'tamarin-lsp') {
        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (workspaceFolders && workspaceFolders.length > 0) {
            const root = workspaceFolders[0].uri.fsPath;
            const exeExt = process.platform === 'win32' ? '.exe' : '';
            const candidatePaths = [
                path.join(root, 'build', 'Release', `tamarin-lsp${exeExt}`),
                path.join(root, 'build', `tamarin-lsp${exeExt}`),
                path.join(root, 'bin', `tamarin-lsp${exeExt}`)
            ];
            const fs = require('fs');
            for (const c of candidatePaths) {
                if (fs.existsSync(c)) {
                    lspPath = c;
                    break;
                }
            }
        }
    }

    const serverOptions: ServerOptions = {
        command: lspPath,
        args: ['--stdio'],
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'tamarin' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{tam,tmn}')
        }
    };

    client = new LanguageClient(
        'tamarin-lsp',
        'Tamarin Language Server',
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
