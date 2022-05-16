/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */

import * as vscode from 'vscode';

import {integer, LanguageClient, LanguageClientOptions, ServerOptions, TransportKind} from 'vscode-languageclient/node';
const { spawn } = require('child_process');

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
	const serverModule = context.asAbsolutePath('/lsp-server/lsp-wake.js');

	let stdLibPath: string = vscode.workspace.getConfiguration("wakeLanguageServer").get("pathToWakeStandardLibrary");
	if (stdLibPath === "") {
		stdLibPath = context.asAbsolutePath('/share/wake/lib');
	}

	let serverOptions: ServerOptions = {
		module: serverModule,
		transport: TransportKind.stdio,
		args: [stdLibPath]
	};

	// Options to control the language client
	let clientOptions: LanguageClientOptions = {
		// Register the server for .wake files
		documentSelector: [{ language: 'wake', pattern: '**/*.wake' }],
		synchronize: {
			// Notify the server about file changes to .wake files contained in the workspace
			fileEvents: vscode.workspace.createFileSystemWatcher('**/*.wake')
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


	// timeline setup
	context.subscriptions.push(
		vscode.commands.registerCommand('timeline.create', () => {
			createOrShowPanel();
		})
	);

	context.subscriptions.push(
		vscode.commands.registerCommand('timeline.refresh', () => {
			if (timelinePanel) {
				refreshTimeline();
			}
		})
	);

	if (vscode.window.registerWebviewPanelSerializer) {
		// Make sure we register a serializer in activation event
		vscode.window.registerWebviewPanelSerializer(viewType, {
			async deserializeWebviewPanel(webviewPanel: vscode.WebviewPanel, state: any) {
				updatePanel(webviewPanel);
			}
		});
	}
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}


let timelinePanel: vscode.WebviewPanel;
const viewType = 'timeline';
let wakeBinary: string;
let disposables: vscode.Disposable[] = [];

function createOrShowPanel() {
	const column = vscode.window.activeTextEditor
		? vscode.window.activeTextEditor.viewColumn
		: undefined;

	// If we already have a panel, show it.
	if (timelinePanel) {
		timelinePanel.reveal(column);
		return;
	}

	// Otherwise, create a new panel.
	let panel = vscode.window.createWebviewPanel(
		viewType,
		'Timeline',
		column || vscode.ViewColumn.One,
		getWebviewOptions(),
	);
	updatePanel(panel);
}

function updatePanel(panel: vscode.WebviewPanel) {
	timelinePanel = panel;
	wakeBinary = vscode.workspace.getConfiguration("wakeTimeline").get("pathToWakeBinary");

	// Set the webview's initial html content
	setTimeline();

	// Listen for when the panel is disposed
	// This happens when the user closes the panel or when the panel is closed programmatically
	timelinePanel.onDidDispose(() => dispose(), null, disposables);

	// Handle messages from the webview
	timelinePanel.webview.onDidReceiveMessage(
		message => {
			switch (message.command) {
				case 'alert':
					vscode.window.showErrorMessage(message.text);
					return;
			}
		},
		null,
		disposables
	);
}

function setTimeline() {
	timelinePanel.title = 'Timeline';
	useWake("",
		(stdout: string) => {
			timelinePanel.webview.html = stdout;
		});
	return;
}

function refreshTimeline() {
	useWake("job-reflections", (jobReflections: string) => {
		useWake( "file-accesses", (fileAccesses: string) => {
			let message = {
				jobReflections: JSON.parse(jobReflections),
				fileAccesses: JSON.parse(fileAccesses)
			};
			// Send a message to the webview.
			timelinePanel.webview.postMessage(message);
		});
	});
}

function useWake(option: string, callback: Function) {
	if (wakeBinary == '') {
		vscode.window.showErrorMessage(`Timeline: the path to wake binary is empty. Please provide a valid path in the extension's settings.`);
		return;
	}
	let process;
	// spawn process in directory where the wake executable is and run it with --timeline
	if (option === "") {
		process = spawn(`${wakeBinary}`, [`--timeline`], { cwd: `${wakeBinary.substring(0, wakeBinary.lastIndexOf('/'))}` });
	} else {
		process = spawn(`${wakeBinary}`, [`--timeline`, `${option}`], { cwd: `${wakeBinary.substring(0, wakeBinary.lastIndexOf('/'))}` });
	}

	let err = "";
	let stdout = "";
	let stderr = "";

	process.on('error', (_err) => {
		err += _err;
	});
	process.stdout.on('data', (data) => {
		stdout += data;
	});
	process.stderr.on('data', (data) => {
		stderr += data;
	});

	process.on('close', (code) => {
		if (code === 0) {
			callback(stdout);
		} else {
			vscode.window.showErrorMessage(`Timeline: error using wake binary at ${wakeBinary}. Please provide a valid path in the extension's settings. ${err}. stderr: ${stderr}`);
		}
	});
}

function getWebviewOptions(): vscode.WebviewOptions {
	return {
		// Enable javascript in the webview
		enableScripts: true,
		// And restrict the webview to not loading any content.
		localResourceRoots: []
	};
}

function dispose() {
	// Clean up our resources
	timelinePanel.dispose();

	while (disposables.length) {
		const x = disposables.pop();
		if (x) {
			x.dispose();
		}
	}

	timelinePanel = undefined;
}
