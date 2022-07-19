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

import { LanguageClientOptions } from 'vscode-languageclient/node';
import { CommonLanguageClient } from 'vscode-languageclient';
import { workspace, Uri, FileType } from 'vscode';

export const clientOptions: LanguageClientOptions = {
    // Register the server for .wake files
    documentSelector: [{ language: 'wake', pattern: '**/*.wake' }],
    synchronize: {
        // Notify the server about file changes to .wake files contained in the workspace
        fileEvents: workspace.createFileSystemWatcher('**/*.wake')
    }
};

export function registerFsMethods(client: CommonLanguageClient, stdLib: string): void {
    client.onReady().then(() => {
        client.onRequest('getStdLib', async (): Promise<string> => {
            return stdLib;
        });

        const decoder = new TextDecoder();

        client.onRequest('isReadable', async (path: string): Promise<number> => {
            try {
                let uri = Uri.parse(path);
                await workspace.fs.stat(uri);
                return 1;
            } catch {
                return 0;
            }
        });

        client.onRequest('readFile', async (path: string): Promise<string | any> => {
            try {
                let uri = Uri.parse(path);
                const content = await workspace.fs.readFile(uri);
                return decoder.decode(content);
            } catch (err) {
                return { message: err.toString() };
            }
        });

        client.onRequest('readDir', async (path: string): Promise<[string, boolean][] | any> => {
            try {
                let uri = Uri.parse(path);
                const files = await workspace.fs.readDirectory(uri);
                let strippedFiles: [string, boolean][] = []; // [fileName, isDirectory]
                for (const file of files) {
                    strippedFiles.push([ file[0], !!(file[1] & FileType.Directory) ]);
                }
                return strippedFiles;
            } catch (err) {
                return { message: err.toString() };
            }
        });
    });
}
