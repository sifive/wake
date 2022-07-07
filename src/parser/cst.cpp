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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "cst.h"

#include <iomanip>

#include "lexer.h"
#include "syntax.h"
#include "util/file.h"

CSTBuilder::CSTBuilder(const FileContent &fcontent) { file = &fcontent; }

CSTNode::CSTNode(uint8_t id_, uint32_t size_, uint32_t begin_, uint32_t end_)
    : id(id_), size(size_), begin(begin_), end(end_) {}

void CSTBuilder::addToken(uint8_t id, StringSegment token) {
  token_ids.push_back(id);
  token_starts.set(token.start - file->segment().start);
}

void CSTBuilder::addNode(uint8_t id, StringSegment begin) {
  uint32_t b = begin.start - file->segment().start;
  uint32_t e = begin.end - file->segment().start;
  nodes.emplace_back(id, 1, b, e);
}

void CSTBuilder::addNode(uint8_t id, uint32_t children) {
  uint32_t b = 0;
  uint32_t e = nodes.empty() ? 0 : nodes.back().end;

  int size = 1;
  for (uint32_t i = children; i; --i) {
    if (i == 1) b = nodes.end()[-size].begin;
    size += nodes.end()[-size].size;
  }

  nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, StringSegment begin, uint32_t children) {
  uint32_t b = 0;
  uint32_t e = nodes.empty() ? 0 : nodes.back().end;

  int size = 1;
  for (uint32_t i = children; i; --i) {
    if (i == 1) b = nodes.end()[-size].begin;
    size += nodes.end()[-size].size;
  }

  uint32_t b2 = begin.start - file->segment().start;
  if (b2 < b) b = b2;

  nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, uint32_t children, StringSegment end) {
  uint32_t b = 0;
  uint32_t e = nodes.empty() ? 0 : nodes.back().end;

  int size = 1;
  for (uint32_t i = children; i; --i) {
    if (i == 1) b = nodes.end()[-size].begin;
    size += nodes.end()[-size].size;
  }

  uint32_t e2 = end.end - file->segment().start;
  if (e2 > e) e = e2;

  nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, StringSegment begin, uint32_t children, StringSegment end) {
  uint32_t b2 = 0;

  int size = 1;
  for (uint32_t i = children; i; --i) {
    if (i == 1) b2 = nodes.end()[-size].begin;
    size += nodes.end()[-size].size;
  }

  uint32_t b = begin.start - file->segment().start;
  uint32_t e = end.end - file->segment().start;

  if (children) {
    uint32_t e2 = nodes.back().end;
    if (b2 < b) b = b2;
    if (e2 > e) e = e2;
  }

  nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::delNodes(size_t num) {
  int size = 1;
  while (num--) size += nodes.end()[-size].size;
  nodes.erase(nodes.end() - size + 1, nodes.end());
}

struct NodeRange {
  uint32_t parent;
  uint32_t first_child;
  NodeRange(uint32_t parent_, uint32_t first_child_) : parent(parent_), first_child(first_child_) {}
};

CSTBuilder CST::build(FileContent &fcontent, DiagnosticReporter &reporter) {
  CSTBuilder builder(fcontent);
  parseWake(ParseInfo(&fcontent, &builder, &reporter));
  return builder;
}

CST::CST(CSTBuilder &&builder) : token_starts(builder.token_starts) {
  file = builder.file;
  token_ids = std::move(builder.token_ids);

  std::vector<uint32_t> stack;
  if (!builder.nodes.empty()) stack.emplace_back(builder.nodes.size());

  while (!stack.empty()) {
    uint32_t node = stack.back();
    stack.pop_back();

    uint32_t lim = node - builder.nodes[node - 1].size;
    for (uint32_t child = node - 1; child != lim; child -= builder.nodes[child - 1].size)
      stack.push_back(child);

    nodes.emplace_back(builder.nodes[node - 1]);
  }

  // parseWake filters out COMMENT and WS tokens from the stream passed to the lexer
  // This means that any leading or trailing comments in the file are orphaned by the lexer parse tree.
  // This expands the range of the top node to include those tokens.
  if (!nodes.empty()) {
    nodes.front().begin = 0;
    nodes.front().end = file->segment().size();
  }

  builder.nodes.clear();
}

CSTElement CST::root() const {
  CSTElement out;
  out.cst = this;
  out.node = 0;
  out.limit = nodes.size();
  out.token = 0;
  out.end = file->segment().size();
  return out;
}

bool CSTElement::empty() const { return node == limit && token >= end; }

bool CSTElement::isNode() const { return node != limit && token >= cst->nodes[node].begin; }

uint8_t CSTElement::id() const {
  if (isNode()) {
    return cst->nodes[node].id;
  } else {
    uint32_t rank = cst->token_starts.rank1(token);
    return cst->token_ids[rank];
  }
}

FileFragment CSTElement::fragment() const {
  uint32_t start, end;
  if (isNode()) {
    CSTNode n = cst->nodes[node];
    start = n.begin;
    end = n.end;
  } else {
    start = token;
    end = cst->token_starts.next1(token + 1);
  }

  return FileFragment(cst->file, start, end);
}

void CSTElement::nextSiblingElement() {
  if (isNode()) {
    CSTNode n = cst->nodes[node];
    node += n.size;
    token = n.end;
  } else {
    token = cst->token_starts.next1(token + 1);
  }
}

void CSTElement::nextSiblingNode() {
  if (isNode()) {
    node += cst->nodes[node].size;
    if (node == limit) {
      token = end;
    } else {
      token = cst->nodes[node].begin;
    }
  } else {
    if (node == limit) {
      token = end;
    } else {
      token = cst->nodes[node].begin;
    }
  }
}

CSTElement CSTElement::firstChildElement() const {
  CSTElement out;
  out.cst = cst;
  if (isNode()) {
    CSTNode n = cst->nodes[node];
    out.node = node + 1;
    out.limit = node + n.size;
    out.token = n.begin;
    out.end = n.end;
  } else {
    // tokens have no children
    out.node = out.limit = out.token = out.end = 0;
  }
  return out;
}

CSTElement CSTElement::firstChildNode() const {
  CSTElement out;
  out.cst = cst;
  if (isNode()) {
    CSTNode n = cst->nodes[node];
    if (n.size == 1) {
      // no child nodes
      out.node = out.limit = out.token = out.end = 0;
    } else {
      out.node = node + 1;
      out.limit = node + n.size;
      out.token = cst->nodes[node + 1].begin;
      out.end = n.end;
    }
  } else {
    // tokens have no children
    out.node = out.limit = out.token = out.end = 0;
  }
  return out;
}

#define MAX_SNIPPET 30
#define MAX_SNIPPET_HALF ((MAX_SNIPPET / 2) - 1)

std::ostream &operator<<(std::ostream &os, StringSegment tinfo) {
  LexerOutput token, next;

  os << "'";

  long codepoints = 0;
  for (token.end = tinfo.start; token.end < tinfo.end; token = lex_printable(token.end, tinfo.end))
    ++codepoints;

  // At most 10 chars at start and 10 chars at end
  long skip_start = (codepoints > MAX_SNIPPET) ? MAX_SNIPPET_HALF : codepoints;
  long skip_end = (codepoints > MAX_SNIPPET) ? codepoints - MAX_SNIPPET_HALF : codepoints;

  long codepoint = 0;
  for (token.end = tinfo.start; token.end < tinfo.end; token = next) {
    next = lex_printable(token.end, tinfo.end);
    if (codepoint < skip_start || codepoint >= skip_end) {
      if (next.ok) {
        os.write(reinterpret_cast<const char *>(token.end), next.end - token.end);
      } else {
        int code;
        switch (next.end - token.end) {
          case 1:
            code = token.end[0];
            break;
          case 2:
            code = ((int)token.end[0] & 0x1f) << 6 | ((int)token.end[1] & 0x3f) << 0;
            break;
          case 3:
            code = ((int)token.end[0] & 0x0f) << 12 | ((int)token.end[1] & 0x3f) << 6 |
                   ((int)token.end[2] & 0x3f) << 0;
            break;
          default:
            code = ((int)token.end[0] & 0x07) << 18 | ((int)token.end[1] & 0x3f) << 12 |
                   ((int)token.end[2] & 0x3f) << 6 | ((int)token.end[3] & 0x3f) << 0;
            break;
        }
        if (code > 0xffff) {
          os << "\\U" << std::hex << std::setw(8) << std::setfill('0') << code;
        } else if (code > 0xff) {
          os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << code;
        } else
          switch (code) {
            case '\a':
              os << "\\a";
              break;
            case '\b':
              os << "\\b";
              break;
            case '\f':
              os << "\\f";
              break;
            case '\n':
              os << "\\n";
              break;
            case '\r':
              os << "\\r";
              break;
            case '\t':
              os << "\\t";
              break;
            case '\v':
              os << "\\v";
              break;
            default:
              os << "\\x" << std::hex << std::setw(2) << std::setfill('0') << code;
          }
      }
    } else if (codepoint == skip_start) {
      os << "..";
    }
    ++codepoint;
  }

  os << "'";

  return os;
}
