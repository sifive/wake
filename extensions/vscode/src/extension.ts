import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
} from 'vscode-languageclient/node';


let client: LanguageClient;

export async function activate(_context: vscode.ExtensionContext) {
  let wakePath: string | undefined | null = vscode.workspace.getConfiguration('wakeLanguageServer').get('path');

  if (wakePath === null || wakePath === undefined || wakePath === '') {
    const selection = await vscode.window.showWarningMessage('The path to the wake binary is not set. Highlighting will work but the LSP will not!', 'Configure', 'Dismiss');

    if (selection === 'Configure') {
      vscode.commands.executeCommand( 'workbench.action.openSettings', 'wakeLanguageServer.path' );
    }

    return;
  }

  const serverOptions: ServerOptions = {
    command: wakePath,
    args: ['--lsp'],
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [
      {
        language: 'wake',
      },
    ]
  };

  client = new LanguageClient('wakeLanguageServer', 'Wake LSP', serverOptions, clientOptions);
  client.clientOptions.errorHandler = client.createDefaultErrorHandler();

  let stdLibPath: string | undefined | null = vscode.workspace.getConfiguration('wakeLanguageServer').get('standardLibrary');
  if (stdLibPath !== null && stdLibPath !== undefined && stdLibPath !== '') {
    // Use the provided standard library instead of the default
    client.clientOptions.initializationOptions = {
      'stdLibPath': stdLibPath
    };
  }

  client.start();
}

export function deactivate() {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
