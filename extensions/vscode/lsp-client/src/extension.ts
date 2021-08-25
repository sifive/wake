/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */

import { workspace } from 'vscode';

import {
	integer,
	LanguageClient,
	LanguageClientOptions,
	ServerOptions
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate() {
	let serverOptions: ServerOptions = {
		run: {
			command: __dirname + "/../../../../lib/wake/lsp-wake.native-cpp11-release"
		},
		debug: {
			command: __dirname + "/../../../../lib/wake/lsp-wake.native-cpp11-debug"
		}
	};

	// Options to control the language client
	let clientOptions: LanguageClientOptions = {
		// Register the server for .wake files
		documentSelector: [{ language: 'wake', pattern: '**/*.wake' }],
		synchronize: {
			// Notify the server about file changes to '.wake files contained in the workspace
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
