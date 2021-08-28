/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */

import { workspace, ExtensionContext } from 'vscode';

import {
	integer,
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
	TransportKind
} from 'vscode-languageclient/node';


let client: LanguageClient;

export function activate(context: ExtensionContext) {
	const serverModule = context.asAbsolutePath('/lsp-server/lsp-wake.js');

	// If the extension is launched in debug mode then the debug server options are used
	// Otherwise the run options are used
	let serverOptions: ServerOptions = {
		module: serverModule,
		transport: TransportKind.stdio
	};

	// Options to control the language client
	let clientOptions: LanguageClientOptions = {
		// Register the server for .wake files
		documentSelector: [{ language: 'wake', pattern: '**/*.wake' }],
		synchronize: {
			// Notify the server about file changes to '.clientrc files contained in the workspace
			fileEvents: workspace.createFileSystemWatcher('**/*.wake')
		}
	};

	// Create the language client and start the client.
	client = new LanguageClient(
		'wakeLanguageServer',
		'Wake Language Server',
		serverOptions,
		clientOptions
	);
	client.clientOptions.errorHandler = client.createDefaultErrorHandler(integer.MAX_VALUE);

	// Start the client. This will also launch the server
	client.start();
}


export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
