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

import {
    CancellationToken,
    DefinitionLink,
    DefinitionParams,
    DidChangeTextDocumentParams,
    DidChangeWatchedFilesParams,
    DidCloseTextDocumentParams,
    DidOpenTextDocumentParams,
    DidSaveTextDocumentParams,
    DocumentHighlight,
    DocumentHighlightParams,
    DocumentSymbolParams,
    Hover,
    HoverParams,
    InitializedParams,
    InitializeError,
    InitializeResult,
    Location,
    ReferenceParams,
    RenameParams,
    ResponseError,
    SymbolInformation,
    WorkspaceEdit,
    WorkspaceSymbolParams,
} from 'vscode-languageserver-protocol';
import { Connection, InitializeParams } from 'vscode-languageserver';
import wakeLspModule , { WakeLspModule } from '../wasm/lsp-wake';
import Mutex = require('ts-mutex');

let lspModule: WakeLspModule | null;
const lock = new Mutex();

function createLspJson(method: string, params: any): string {
    return JSON.stringify({
        'id': 0,
        'method': method,
        'params': params
    });
}

function createLspJsonNoParams(method: string): string {
    return JSON.stringify({
        'id': 0,
        'method': method
    });
}

// Defined by JSON RPC
const InternalError  = -32603;

export function prepareConnection(connection: Connection, isWeb: Boolean) {
    const getResponse = async <T, E>(request: string): Promise<T | ResponseError<E>> => {
        let c_str = await lock.use(async () => { // synchronise requests to wasm
            return await lspModule?.processRequest(request) ?? null;
        });

        if (lspModule === null) {
            return new ResponseError(InternalError, 'Failed to load wake lsp module.');
        }

        if (c_str === null) {
            return new ResponseError(InternalError, 'Failed to receive response from lsp module.');
        }

        let response = lspModule.toString(c_str);
        lspModule._free(c_str); // free the string mallocced by c++

        let result = JSON.parse(response);

        if (result.response.hasOwnProperty('error')) {
            return new ResponseError(result.response.error.code, result.response.error.message); // request resulted in an error
        }

        if (result.notification.hasOwnProperty('method')) { // send notification, if one was received
            await connection.sendNotification(result.notification.method, result.notification.params);
        }

        for (let fileDiagnostics of result.diagnostics) { // send diagnostics, if any were received
            await connection.sendDiagnostics(fileDiagnostics.params);
        }

        return result.response.result;
    }

    // Create lsp handlers
    connection.onInitialize(async (params: InitializeParams): Promise<InitializeResult | ResponseError<InitializeError>> => {
        lspModule = await wakeLspModule(connection);
        lspModule?._instantiateServer();

        let response: InitializeResult | ResponseError<InitializeError> = await getResponse(createLspJson('initialize', params));
        // TODO: impl & test invalid stdlib
        return response;
    });

    connection.onInitialized(async (params: InitializedParams): Promise<void> => {
        await getResponse(createLspJson('initialized', params));
    });

    connection.onDidOpenTextDocument(async (params: DidOpenTextDocumentParams): Promise<void> => {
        await getResponse(createLspJson('textDocument/didOpen', params));
    });

    connection.onDidChangeTextDocument(async (params: DidChangeTextDocumentParams): Promise<void> => {
        await getResponse(createLspJson('textDocument/didChange', params));
    });

    connection.onDidSaveTextDocument(async (params: DidSaveTextDocumentParams): Promise<void> => {
        await getResponse(createLspJson('textDocument/didSave', params));
    });

    connection.onDidCloseTextDocument(async (params: DidCloseTextDocumentParams): Promise<void> => {
        await getResponse(createLspJson('textDocument/didClose', params));
    });

    connection.onDidChangeWatchedFiles(async (params: DidChangeWatchedFilesParams): Promise<void> => {
        await getResponse(createLspJson('workspace/didChangeWatchedFiles', params));
    });

    connection.onShutdown(async (_: CancellationToken): Promise<void> => {
        await getResponse(createLspJsonNoParams('shutdown'));
    });

    connection.onExit(async (): Promise<void> => {
        await getResponse(createLspJsonNoParams('exit'));
    });

    connection.onDefinition(async (params: DefinitionParams): Promise<DefinitionLink[] | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/definition', params));
    });

    connection.onReferences(async (params: ReferenceParams): Promise<Location[] | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/references', params));
    });

    connection.onDocumentHighlight(async (params: DocumentHighlightParams): Promise<DocumentHighlight[] | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/documentHighlight', params));
    });

    connection.onHover(async (params: HoverParams): Promise<Hover | null | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/hover', params));
    });

    connection.onDocumentSymbol(async (params: DocumentSymbolParams): Promise<SymbolInformation[] | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/documentSymbol', params));
    });

    connection.onWorkspaceSymbol(async (params: WorkspaceSymbolParams): Promise<SymbolInformation[] | ResponseError<void>> => {
        return await getResponse(createLspJson('workspace/symbol', params));
    });

    connection.onRenameRequest(async (params: RenameParams): Promise<WorkspaceEdit | ResponseError<void>> => {
        return await getResponse(createLspJson('textDocument/rename', params));
    });

    // Listen on the connection
    connection.listen();
}
