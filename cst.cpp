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

#include <iomanip>

#include "cst.h"
#include "file.h"
#include "syntax.h"
#include "lexer.h"

CSTBuilder::CSTBuilder(const FileContent &fcontent) {
    content.file = &fcontent;
}

CSTNode::CSTNode(uint8_t id_, uint32_t size_, uint32_t begin_, uint32_t end_)
 : id(id_), size(size_), begin(begin_), end(end_)
{
}

void CSTBuilder::addToken(uint8_t id, TokenInfo token) {
    content.token_ids.push_back(id);
    content.token_starts.set(token.start - content.file->start);
}

void CSTBuilder::addNode(uint8_t id, uint32_t children) {
    uint32_t b = 0;
    uint32_t e = content.nodes.back().end;

    int size = 1;
    for (uint32_t i = children; i; --i) {
        if (i == 1) b = content.nodes.end()[-size].begin;
        size += content.nodes.end()[-size].size;
    }

    content.nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, TokenInfo begin, uint32_t children) {
    uint32_t b = 0;
    uint32_t e = content.nodes.back().end;

    int size = 1;
    for (uint32_t i = children; i; --i) {
        if (i == 1) b = content.nodes.end()[-size].begin;
        size += content.nodes.end()[-size].size;
    }

    uint32_t b2 = begin.start - content.file->start;
    if (b2 < b) b = b2;

    content.nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, uint32_t children, TokenInfo end) {
    uint32_t b = 0;
    uint32_t e = content.nodes.back().end;

    int size = 1;
    for (uint32_t i = children; i; --i) {
        if (i == 1) b = content.nodes.end()[-size].begin;
        size += content.nodes.end()[-size].size;
    }

    uint32_t e2 = end.end - content.file->start;
    if (e2 > e) e = e2;

    content.nodes.emplace_back(id, size, b, e);
}

void CSTBuilder::addNode(uint8_t id, TokenInfo begin, uint32_t children, TokenInfo end) {
    uint32_t b2 = 0;

    int size = 1;
    for (uint32_t i = children; i; --i) {
        if (i == 1) b2 = content.nodes.end()[-size].begin;
        size += content.nodes.end()[-size].size;
    }

    uint32_t b = begin.start - content.file->start;
    uint32_t e = end.end - content.file->start;

    if (children) {
        uint32_t e2 = content.nodes.back().end;
        if (b2 < b) b = b2;
        if (e2 > e) e = e2;
    }

    content.nodes.emplace_back(id, size, b, e);
}

struct NodeRange {
    uint32_t parent;
    uint32_t first_child;
    NodeRange(uint32_t parent_, uint32_t first_child_)
     : parent(parent_), first_child(first_child_) { }
};

CST::CST(CSTBuilder &&builder) {
    content.file = builder.content.file;
    content.token_starts = std::move(builder.content.token_starts);
    content.token_ids = std::move(builder.content.token_ids);

    std::vector<uint32_t> stack;
    if (!builder.content.nodes.empty())
        stack.emplace_back(builder.content.nodes.size());

    while (!stack.empty()) {
        uint32_t node = stack.back();
        stack.pop_back();

        uint32_t lim = node - builder.content.nodes[node-1].size;
        for (uint32_t child = node-1; child != lim; child -= builder.content.nodes[child-1].size)
            stack.push_back(child);

        content.nodes.emplace_back(builder.content.nodes[node-1]);
    }

    builder.content.nodes.clear();
}

CSTElement CST::root() const {
    CSTElement out;
    out.cst = this;
    out.node = 0;
    out.limit = content.nodes.size();
    out.token = 0;
    out.end = content.file->end - content.file->start;
    return out;
}

bool CSTElement::empty() const {
    return node == limit && token == end;
}

bool CSTElement::isNode() const {
    return node != limit && token == cst->content.nodes[node].begin;
}

uint8_t CSTElement::id() const {
    if (isNode()) {
        return cst->content.nodes[node].id;
    } else {
        uint32_t rank = cst->content.token_starts.rank(token);
        return cst->content.token_ids[rank];
    }
}

TokenInfo CSTElement::content() const {
    TokenInfo out;
    const uint8_t *start = cst->content.file->start;
    if (isNode()) {
        CSTNode n = cst->content.nodes[node];
        out.start = start + n.begin;
        out.end   = start + n.end;
    } else {
        out.start = start + token;
        out.end   = start + cst->content.token_starts.next(token);
    }
    return out;
}

void CSTElement::nextSibling() {
    if (isNode()) {
        CSTNode n = cst->content.nodes[node];
        node += n.size;
        token = n.end;
    } else {
        token = cst->content.token_starts.next(token);
    }
}

CSTElement CSTElement::firstChild() const {
    CSTElement out;
    out.cst = cst;
    if (isNode()) {
        CSTNode n = cst->content.nodes[node];
        out.node  = node+1;
        out.limit = node + n.size;
        out.token = n.begin;
        out.end   = n.end;
    } else {
        // tokens have no children
        out.node = out.limit = out.token = out.end = 0;
    }
    return out;
}

Location TokenInfo::location(FileContent &fcontent) const {
    return Location(fcontent.filename.c_str(), fcontent.coordinates(start), fcontent.coordinates(end!=start?end-1:end));
}

std::ostream & operator << (std::ostream &os, TokenInfo tinfo) {
    Token token, next;

    os << "'";

    long codepoints = 0;
    for (token.end = tinfo.start; token.end < tinfo.end; token = lex_printable(token.end, tinfo.end))
        ++codepoints;

    // At most 10 chars at start and 10 chars at end
    long skip_start = (codepoints > 20) ? 9 : codepoints;
    long skip_end   = (codepoints > 20) ? codepoints-9 : codepoints;

    long codepoint = 0;
    for (token.end = tinfo.start; token.end < tinfo.end; token = next) {
        next = lex_printable(token.end, tinfo.end);
        if (codepoint < skip_start || codepoint >= skip_end) {
            if (next.ok) {
                os.write(reinterpret_cast<const char*>(token.end), next.end - token.end);
            } else {
                int code;
                switch (next.end - token.end) {
                case 1:
                    code = token.end[0];
                    break;
                case 2:
                    code =
                        ((int)token.end[0] & 0x1f) << 6 |
                        ((int)token.end[1] & 0x3f) << 0 ;
                    break;
                case 3:
                    code =
                        ((int)token.end[0] & 0x0f) << 12 |
                        ((int)token.end[1] & 0x3f) <<  6 |
                        ((int)token.end[2] & 0x3f) <<  0 ;
                    break;
                default:
                    code =
                        ((int)token.end[0] & 0x07) << 18 |
                        ((int)token.end[1] & 0x3f) << 12 |
                        ((int)token.end[2] & 0x3f) <<  6 |
                        ((int)token.end[3] & 0x3f) <<  0 ;
                    break;
                }
                if (code > 0xffff) {
                    os << "\\U" << std::hex << std::setw(8) << std::setfill('0') << code;
                } else if (code > 0xff) {
                    os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << code;
                } else switch (code) {
                    case '\a': os << "\\a"; break;
                    case '\b': os << "\\b"; break;
                    case '\f': os << "\\f"; break;
                    case '\n': os << "\\n"; break;
                    case '\r': os << "\\r"; break;
                    case '\t': os << "\\t"; break;
                    case '\v': os << "\\v"; break;
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
