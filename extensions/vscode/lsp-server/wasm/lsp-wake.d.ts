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

/// <reference types="emscripten" />
/** Above will import declarations from @types/emscripten, including Module etc. */

// Declare wasm functions so that we can use them in typescript
export interface WakeLspModule extends EmscriptenModule {
    _instantiateServer(): void;
    processRequest(request: string): Promise<number>;

    toString(pointer: number): string;
    _free(pointer: number): void;
}

export default function wakeLspModule(module?: any): WakeLspModule;
