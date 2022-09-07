/*-----------------------------------------------------------------------------------------
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Licensed under the MIT License. See LICENSE.MIT in this folder for license information.
 *----------------------------------------------------------------------------------------*/

import { ExtensionContext, Uri } from 'vscode';
import { LanguageClientOptions } from 'vscode-languageclient';
import { LanguageClient } from 'vscode-languageclient/browser';
import { clientOptions, registerFsMethods } from '../common';

export function activate(context: ExtensionContext): void {
	const client = createWorkerLanguageClient(context, clientOptions);
	client.clientOptions.errorHandler = client.createDefaultErrorHandler(Number.MAX_SAFE_INTEGER);

	// Start the client. This will also launch the server
	const disposable = client.start();
	context.subscriptions.push(disposable);
	registerFsMethods(client, context.asAbsolutePath('/share/wake/lib'));
}

function createWorkerLanguageClient(context: ExtensionContext, clientOptions: LanguageClientOptions): LanguageClient {
	// Create a worker. The worker main file implements the language server.
	const server = Uri.joinPath(context.extensionUri, 'lsp-server/out/serverWeb.js');
	const worker = new Worker(server.toString());

	// create the language server client to communicate with the server running in the worker
	return new LanguageClient('wakeLanguageServer', 'Wake Web Language Server', clientOptions, worker);
}
