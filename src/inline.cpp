/*
 * Copyright 2019 SiFive, Inc.
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

#include "ssa.h"

struct PassInline {
  TermStream stream;
};

void RArg::pass_inline(PassInline &p) {
}

void RLit::pass_inline(PassInline &p) {
}

void RApp::pass_inline(PassInline &p) {
}

void RPrim::pass_inline(PassInline &p) {
}

void RGet::pass_inline(PassInline &p) {
}

void RDes::pass_inline(PassInline &p) {
}

void RCon::pass_inline(PassInline &p) {
}

void RFun::pass_inline(PassInline &p) {
}
