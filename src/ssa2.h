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

#ifndef SSA2_H
#define SSA2_H

namespace ssa2 {

struct Term {
};

struct TLit : public Term {
  std::shared_ptr<RootPointer<Value> > value;
};

struct TPrim : public Term {
  std::string name;
  PrimFn fn;
  void *data;
  int pflags;
  std::vector<size_t> args;
};

struct TLoad : public Term {
  size_t stack;
};

struct TClosure : public Term {
  size_t fn; // BB
};

struct TTuple : public Term {
  std::shared_ptr<Constructor> kind;
};

struct TGet : public Term {
  size_t tuple; // term
  size_t index;
};

struct TPut : public Term {
  size_t tuple; // term
  size_t index;
  size_t value; // term
};

struct TFrame : public Term {
  size_t fn; // BB (optional)
};

struct TFrameSet : public Term {
  size_t frame; // term
  size_t index;
  size_t value; // term
};

struct ControlTransfer {
};

struct CNext : public ControlTransfer {
};

struct CReturn : public ControlTransfer {
};

struct CClosureCall : public ControlTransfer {
  size_t frame; // term
  size_t fn; // term
};

struct CDirectCall : public ControlTransfer {
  size_t frame; // term
  size_t fn; // BB
};

struct CSwitch : public ControlTransfer {
  size_t tuple; // term
  std::vector<size_t> cases; // BB
};

struct BasicBlock {
  std::vector<size_t> demands; // outgoing edges
  size_t demanded; // incoming degree
};

struct BFun : public BasicBlock {
  std::vector<std::unique_ptr<BasicBlock> > bbs;
};

struct BArg : public BasicBlock {
};

struct BTerms : public BasicBlock {
  std::vector<size_t> outputs; // terms -> stack frame
  std::vector<std::unique_ptr<Term> > terms;
  std::unique_ptr<ControlTransfer> transfer;
};

// passes:
//   encapsulate and mark-up closures
//   convert to BB (Gets, Literals, and 0-arg Con replicated into BBs)
//   transitive reduction + function demand promotion (args+outer BBs)
//   given elimination (remove demands covered by the function call-site)
//   equal demand merge (no demands can exist between them => safe to merge at later BB)
//     -- redirect all uses of earlier BB to later BB and make later BB demand earlier BB
//     ... don't be fooled if there is already an edge between them
//   singleton-use merge
//   sweep - clear holes from merges
//   sort - move Gets to top of BB
//   emit

};

#endif
