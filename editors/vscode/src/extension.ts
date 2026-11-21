import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

interface ServerCommand {
    command: string;
    args: string[];
}

function resolveBinary(): ServerCommand {
    const config = vscode.workspace.getConfiguration('tamarin');
    const customPath = config.get<string>('lsp.path');
    const exeExt = process.platform === 'win32' ? '.exe' : '';

    // 1. Explicit user configuration
    if (customPath && customPath !== 'tamarin-lsp' && customPath.trim() !== '') {
        return { command: customPath, args: ['--stdio'] };
    }

    // 2. Check workspace folders and parent directories (walking up)
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        for (const wf of workspaceFolders) {
            let curr = wf.uri.fsPath;
            while (true) {
                const candidates = [
                    path.join(curr, 'build', 'Release', `tamarin-lsp${exeExt}`),
                    path.join(curr, 'build', `tamarin-lsp${exeExt}`),
                    path.join(curr, 'bin', `tamarin-lsp${exeExt}`),
                    path.join(curr, `tamarin-lsp${exeExt}`),
                    path.join(curr, 'build', 'Release', `tamarin${exeExt}`),
                    path.join(curr, 'bin', `tamarin${exeExt}`),
                    path.join(curr, `tamarin${exeExt}`)
                ];
                for (const c of candidates) {
                    if (fs.existsSync(c)) {
                        if (path.basename(c).toLowerCase().startsWith('tamarin-lsp')) {
                            return { command: c, args: ['--stdio'] };
                        } else {
                            return { command: c, args: ['lsp', '--stdio'] };
                        }
                    }
                }
                const parent = path.dirname(curr);
                if (parent === curr) break;
                curr = parent;
            }
        }
    }

    // 3. Check common user home directory installations
    const homeDir = os.homedir();
    const homeCandidates = [
        path.join(homeDir, '.tamarin', 'bin', `tamarin-lsp${exeExt}`),
        path.join(homeDir, '.tamarin', 'bin', `tamarin${exeExt}`),
        path.join(homeDir, 'AppData', 'Local', 'Programs', 'Tamarin', 'bin', `tamarin-lsp${exeExt}`),
        path.join('C:', 'Program Files', 'Tamarin', 'bin', `tamarin-lsp${exeExt}`),
        path.join('/usr', 'local', 'bin', 'tamarin-lsp'),
        path.join('/usr', 'bin', 'tamarin-lsp')
    ];
    for (const c of homeCandidates) {
        if (fs.existsSync(c)) {
            if (path.basename(c).toLowerCase().startsWith('tamarin-lsp')) {
                return { command: c, args: ['--stdio'] };
            } else {
                return { command: c, args: ['lsp', '--stdio'] };
            }
        }
    }

    // 4. Check if tamarin-lsp or tamarin is on PATH
    const pathEnv = process.env.PATH || '';
    const pathSeparator = process.platform === 'win32' ? ';' : ':';
    const pathDirs = pathEnv.split(pathSeparator);

    for (const dir of pathDirs) {
        const lspCandidate = path.join(dir, `tamarin-lsp${exeExt}`);
        if (fs.existsSync(lspCandidate)) {
            return { command: lspCandidate, args: ['--stdio'] };
        }
    }
    for (const dir of pathDirs) {
        const tamarinCandidate = path.join(dir, `tamarin${exeExt}`);
        if (fs.existsSync(tamarinCandidate)) {
            return { command: tamarinCandidate, args: ['lsp', '--stdio'] };
        }
    }

    // 5. Default fallback
    return { command: `tamarin-lsp${exeExt}`, args: ['--stdio'] };
}

export function activate(context: vscode.ExtensionContext) {
    const serverCmd = resolveBinary();

    const serverOptions: ServerOptions = {
        command: serverCmd.command,
        args: serverCmd.args,
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'tamarin' },
            { scheme: 'untitled', language: 'tamarin' }
        ],
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

    client.start().catch((err: any) => {
        vscode.window.showErrorMessage(
            `Failed to start Tamarin Language Server (${serverCmd.command}): ${err.message || err}. Please ensure tamarin-lsp is built/installed or configure tamarin.lsp.path in settings.`,
            'Open Settings'
        ).then(choice => {
            if (choice === 'Open Settings') {
                vscode.commands.executeCommand('workbench.action.openSettings', 'tamarin.lsp.path');
            }
        });
    });
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
