/* Copyright 2022 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import * as vscode from 'vscode';
import { spawn } from 'child_process';

export function registerTimelineCommands(context: vscode.ExtensionContext): void {
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

let timelinePanel: vscode.WebviewPanel | null = null;
const viewType = 'timeline';
let wakeBinary: string = '';
const disposables: vscode.Disposable[] = [];

function createOrShowPanel(): void {
    const column = vscode.window.activeTextEditor?.viewColumn;

    // If we already have a panel, show it.
    if (timelinePanel) {
        timelinePanel.reveal(column);
        return;
    }

    // Otherwise, create a new panel.
    const panel = vscode.window.createWebviewPanel(
        viewType,
        'Timeline',
        column || vscode.ViewColumn.One,
        getWebviewOptions(),
    );
    updatePanel(panel);
}

function updatePanel(panel: vscode.WebviewPanel): void {
    timelinePanel = panel;
    wakeBinary = vscode.workspace.getConfiguration("wakeTimeline").get("pathToWakeBinary");

    // Set the webview's initial html content
    setTimeline();

    // Listen for when the panel is disposed
    // This happens when the user closes the panel or when the panel is closed programmatically
    timelinePanel.onDidDispose(() => dispose(), null, disposables);

    // when timelinePanel is not visible, the updates from refresh() are lost => must update the html itself
    timelinePanel.onDidChangeViewState(
        e => {
            if (timelinePanel.visible) {
                setTimeline();
            }
        },
        null,
        disposables
    );

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

function setTimeline(): void {
    timelinePanel.title = 'Timeline';
    useWake("",
        (stdout: string) => {
            timelinePanel.webview.html = stdout;
        });
}

function refreshTimeline(): void {
    useWake("job-reflections", (jobReflections: string) => {
        useWake( "file-accesses", (fileAccesses: string) => {
            const message = {
                jobReflections: JSON.parse(jobReflections),
                fileAccesses: JSON.parse(fileAccesses)
            };
            // Send a message to the webview.
            timelinePanel.webview.postMessage(message);
        });
    });
}

function useWake(option: string, callback: (stdout: string) => void): void {
    if (wakeBinary == '') {
        vscode.window.showErrorMessage(`Timeline: the path to wake binary is empty. Please provide a valid path in the extension's settings.`);
        return;
    }

    const extraArgs = option === "" ? [] : [option];
    // spawn process in directory where the wake executable is and run it with --timeline
    const process = spawn(wakeBinary, [`--timeline`, ...extraArgs], { cwd: `${wakeBinary.substring(0, wakeBinary.lastIndexOf('/'))}` });

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

function dispose(): void {
    // Clean up our resources
    timelinePanel.dispose();

    for (const disposable of disposables) {
        disposable.dispose();
    }
    disposables.length = 0;

    timelinePanel = null;
}
