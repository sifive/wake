/* -----------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See LICENSE.MIT in this folder for license information.
 * ----------------------------------------------------------------------------------------*/

import * as vscode from 'vscode';
import { LanguageClient, ServerOptions, TransportKind } from 'vscode-languageclient/node';
import { clientOptions, registerFsMethods } from '../common';
import { registerTimelineCommands} from '../timelineNode';

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext): void {
	let stdLibPath: string | undefined = vscode.workspace.getConfiguration('wakeLanguageServer')
		.get('pathToWakeStandardLibrary');
	if (stdLibPath === undefined || stdLibPath === '') {
		stdLibPath = context.asAbsolutePath('/share/wake/lib');
	}

	const serverOptions: ServerOptions = {
		module: context.asAbsolutePath('/lsp-server/out/serverNode.js'),
		transport: TransportKind.ipc
	};

	client = new LanguageClient(
		'wakeLanguageServer',
		'Wake Node Language Server',
		serverOptions,
		clientOptions
	);
	client.clientOptions.errorHandler = client.createDefaultErrorHandler(Number.MAX_SAFE_INTEGER);

	// Start the client. This will also launch the server
	client.start();
	registerFsMethods(client, stdLibPath);
	registerTimelineCommands(context);
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
