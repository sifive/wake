/* --------------------------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License. See License.txt in the project root for license information.
 * ------------------------------------------------------------------------------------------ */

import * as vscode from 'vscode';

import {integer, LanguageClient, LanguageClientOptions, ServerOptions, TransportKind} from 'vscode-languageclient/node';
import fs = require('fs');
import sqlite3 = require('sqlite3');

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


	context.subscriptions.push(
		vscode.commands.registerCommand('timeline.create', () => {
			TimelinePanel.createOrShow();
		})
	);

	context.subscriptions.push(
		vscode.commands.registerCommand('timeline.refresh', () => {
			if (TimelinePanel.currentPanel) {
				TimelinePanel.currentPanel.refresh();
			}
		})
	);

	if (vscode.window.registerWebviewPanelSerializer) {
		// Make sure we register a serializer in activation event
		vscode.window.registerWebviewPanelSerializer(TimelinePanel.viewType, {
			async deserializeWebviewPanel(webviewPanel: vscode.WebviewPanel, state: any) {
				TimelinePanel.revive(webviewPanel);
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

function getWebviewOptions(): vscode.WebviewOptions {
	return {
		// Enable javascript in the webview
		enableScripts: true,
		// And restrict the webview to not loading any content.
		localResourceRoots: []
	};
}

/**
 * Manages timeline webview panels
 */
class TimelinePanel {
	/**
	 * Track the currently panel. Only allow a single panel to exist at a time.
	 */
	public static currentPanel: TimelinePanel | undefined;

	public static readonly viewType = 'timeline';

	private readonly _panel: vscode.WebviewPanel;
	private _disposables: vscode.Disposable[] = [];

	public static createOrShow() {
		const column = vscode.window.activeTextEditor
			? vscode.window.activeTextEditor.viewColumn
			: undefined;

		// If we already have a panel, show it.
		if (TimelinePanel.currentPanel) {
			TimelinePanel.currentPanel._panel.reveal(column);
			return;
		}

		// Otherwise, create a new panel.
		const panel = vscode.window.createWebviewPanel(
			TimelinePanel.viewType,
			'Timeline',
			column || vscode.ViewColumn.One,
			getWebviewOptions(),
		);

		TimelinePanel.currentPanel = new TimelinePanel(panel);
	}

	public static revive(panel: vscode.WebviewPanel) {
		TimelinePanel.currentPanel = new TimelinePanel(panel);
	}

	private constructor(panel: vscode.WebviewPanel) {
		this._panel = panel;

		// Set the webview's initial html content
		this._update();

		// Listen for when the panel is disposed
		// This happens when the user closes the panel or when the panel is closed programmatically
		this._panel.onDidDispose(() => this.dispose(), null, this._disposables);

		// Update the content based on view changes
		this._panel.onDidChangeViewState(
			e => {
				if (this._panel.visible) {
					this._update();
				}
			},
			null,
			this._disposables
		);

		// Handle messages from the webview
		this._panel.webview.onDidReceiveMessage(
			message => {
				switch (message.command) {
					case 'alert':
						vscode.window.showErrorMessage(message.text);
						return;
				}
			},
			null,
			this._disposables
		);
	}

	private _getDB() {
		return new sqlite3.Database(__dirname + '/../../../../wake.db', (err) => {
			if (err) {
				return console.error(err.message);
			}
		});
	}

	public refresh() {
		let db = this._getDB();
		this._queryDB(db, (json: JobReflection[], accessesJson: { type: number, job: number }[]) => {
			let message = {
				jobReflections: json,
				fileAccesses: accessesJson
			}
			// Send a message to the webview webview.
			this._panel.webview.postMessage(message);
		});
	}

	public dispose() {
		TimelinePanel.currentPanel = undefined;

		// Clean up our resources
		this._panel.dispose();

		while (this._disposables.length) {
			const x = this._disposables.pop();
			if (x) {
				x.dispose();
			}
		}
	}

	private _update() {
		this._panel.title = 'Timeline';

		let db = this._getDB();
		try {
			let data = fs.readFileSync(__dirname + '/../../../../share/wake/html/timeline.html', { encoding: 'utf8' }).toString();
			this._queryDB(db, (json: JobReflection[], accessesJson: { type: number, job: number }[]) => {
				this._panel.webview.html = this._formatString(data, JSON.stringify(json), JSON.stringify(accessesJson));
			});			
		} catch (err) { }
		return;
	}

	private _formatString(s: string, ...args: string[]): string {
		return s.replace(/{(\d+)}/g, function (match, number) {
			return typeof args[number] != 'undefined'
				? args[number] : match;
		});
	}

	private _queryDB(db: sqlite3.Database, callback: Function) {
		let json: JobReflection[] = [];
			let accessesJson: { type: number, job: number }[] = [];
			let sql = 
			`select j.job_id, j.label, j.directory, j.commandline, j.environment, j.stack, j.stdin, j.starttime, j.endtime, j.stale, r.time, r.cmdline, s.status, s.runtime, s.cputime, s.membytes, s.ibytes, s.obytes
			 from  jobs j left join stats s on j.stat_id=s.stat_id join runs r on j.run_id=r.run_id
			 where substr(cast(j.commandline as varchar), 1, 8) != '<source>'
			 and substr(cast(j.commandline as varchar), 1, 7) != '<mkdir>'
			 and substr(cast(j.commandline as varchar), 1, 7) != '<write>'
			 and substr(cast(j.commandline as varchar), 1, 6) != '<hash>'`;

			db.each(sql, (err, row) => {
				const reflection: JobReflection = {
					job: row.job_id,
					label: row.label,
					stale: row.stale,
					directory: row.directory,
					commandline: row.commandline.toString().replaceAll('\0', ' '),
					environment: row.environment.toString().replaceAll('\0', ' '),
					stack: JSON.stringify(row.stack), //!
					stdin_file: row.stdin.length != 0 ? row.stdin : '/dev/null',
					starttime: row.starttime,
					endtime: row.endtime,
					wake_start: row.time,
					wake_cmdline: row.cmdline,
					stdout_payload: row.stdout_payload,
					stderr_payload: row.stderr_payload,
					usage: 'status: ' + row.status + '<br>runtime: ' + row.runtime + '<br>cputime: ' + row.cputime + '<br>membytes: ' + row.membytes + 'br>ibytes: ' + row.ibytes + '<br>obytes: ' + row.obytes,
					visible: '',
					inputs: '',
					outputs: '',
					tags: ''
				}
				json.push(reflection);
			}, (err, count) => {
								let sql2 =
					`select access, job_id
				from filetree
				where access != 1
				order by file_id, access desc, job_id`;

				db.each(sql2, (err, row) => {
					const fileAccess = {
						type: row.access,
						job: row.job_id
					}
					accessesJson.push(fileAccess);
				},   (err, count) => {
						callback(json, accessesJson);
					});
			});
	}
}

class JobReflection {
	job: number = 0;
	stale: boolean = false;
	label: string = '';
	directory: string = '';
	commandline: string = '';
	environment: string = '';
	stack: string = '';
	stdin_file: string = '';
	starttime: number = 0;
	endtime: number = 0;
	wake_start: number = 0;
	wake_cmdline: string = '';
	stdout_payload: string = '';
	stderr_payload: string = '';
	usage: string = '';
	visible: string = '';
	inputs: string = '';
	outputs: string = '';
	tags: string = '';
}
