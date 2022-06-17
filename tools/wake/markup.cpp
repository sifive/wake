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

#include "markup.h"

#include <assert.h>

#include <fstream>
#include <set>
#include <vector>

#include "dst/expr.h"
#include "json/json5.h"
#include "util/execpath.h"

struct ParanOrder {
  bool operator()(Expr *a, Expr *b) const {
    FileFragment fa = a->fragment;
    FileFragment fb = b->fragment;
    int cmp = strcmp(fa.filename(), fb.filename());
    if (cmp < 0) return true;
    if (cmp > 0) return false;
    if (fa.startByte() < fb.startByte()) return true;
    if (fa.startByte() > fb.startByte()) return false;
    return fa.endByte() > fb.endByte();
  }
};

struct JSONRender {
  typedef std::set<Expr *, ParanOrder> ESet;
  std::string libdir;
  std::vector<std::unique_ptr<Expr> > defs;
  std::ostream &os;
  ESet eset;
  ESet::iterator it;

  JSONRender(const std::string &libdir_, std::ostream &os_) : libdir(libdir_), os(os_) {}

  std::string filename(const FileFragment &frag) const;
  void explore(Expr *expr);
  void dump();
  void render(Expr *root);
};

std::string JSONRender::filename(const FileFragment &frag) const {
  const char *filename = frag.filename();
  if (libdir.compare(0, libdir.size(), filename, libdir.size()) == 0 &&
      filename[libdir.size()] == '/') {
    return json_escape(std::string("<stdlib>") + (filename + libdir.size()));
  } else {
    return json_escape(frag.filename());
  }
}

void JSONRender::explore(Expr *expr) {
  if (!expr->fragment.empty() && (expr->flags & FLAG_AST) != 0) eset.insert(expr);

  if (expr->type == &App::type) {
    App *app = static_cast<App *>(expr);
    explore(app->val.get());
    explore(app->fn.get());
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda *>(expr);
    if (!lambda->token.empty()) {
      auto foo = new VarArg(lambda->token);
      foo->typeVar.setDOB(lambda->typeVar[0]);
      lambda->typeVar[0].unify(foo->typeVar);
      defs.emplace_back(foo);
      eset.insert(foo);
    }
    explore(lambda->body.get());
  } else if (expr->type == &Ascribe::type) {
    Ascribe *ascribe = static_cast<Ascribe *>(expr);
    explore(ascribe->body.get());
  } else if (expr->type == &DefBinding::type) {
    DefBinding *defbinding = static_cast<DefBinding *>(expr);
    for (auto &i : defbinding->val) explore(i.get());
    for (auto &i : defbinding->fun) explore(i.get());
    for (auto &i : defbinding->order) {
      if (!i.second.fragment.empty()) {
        int val = i.second.index;
        int fun = val - defbinding->val.size();
        Expr *expr = (fun >= 0) ? defbinding->fun[fun].get() : defbinding->val[val].get();
        if (i.first.compare(0, 6, "topic ") == 0) {
          // expr = VarRef(Nil) | App(App(VarRef(++),Ascribe(VarRef(pub))), expr)
          while (expr->type == &App::type) {
            App *app1 = static_cast<App *>(expr);
            App *app2 = static_cast<App *>(app1->fn.get());
            Ascribe *asc = static_cast<Ascribe *>(app2->val.get());
            VarRef *pub = static_cast<VarRef *>(asc->body.get());
            auto ref = new VarRef(pub->target, i.first);
            ref->target = i.second.fragment;
            ref->typeVar.setDOB(expr->typeVar);
            expr->typeVar.unify(ref->typeVar);
            eset.insert(ref);
            expr = app1->val.get();
          }
        }
        if (i.first.compare(0, 8, "publish ") != 0) {
          auto def = new VarDef(i.second.fragment);
          def->typeVar.setDOB(expr->typeVar);
          expr->typeVar.unify(def->typeVar);
          defs.emplace_back(def);
          eset.insert(def);
        }
      }
    }
    explore(defbinding->body.get());
  }
}

void JSONRender::dump() {
  FileFragment self = (*it)->fragment;

  os << "{\"type\":\"" << (*it)->type->name << "\",\"range\":[" << self.startByte() << ","
     << self.endByte() << "],\"sourceType\":\"";
  (*it)->typeVar.format(os, (*it)->typeVar);
  os << "\"";

  if ((*it)->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef *>(*it);
    FileFragment target = ref->target;
    if (!target.empty()) {
      os << ",\"target\":{\"filename\":\"" << filename(target) << "\",\"range\":["
         << target.startByte() << "," << target.endByte() << "]}";
    }
  }

  ++it;

  bool body = false;
  while (it != eset.end()) {
    FileFragment child = (*it)->fragment;
    if (child.filename() != self.filename()) break;
    if (child.startByte() >= self.endByte()) break;
    if (body)
      os << ",";
    else
      os << ",\"body\":[";
    body = true;
    dump();
  }

  if (body) os << "]";
  os << "}";
}

void JSONRender::render(Expr *root) {
  explore(root);
  it = eset.begin();

  os << "{\"type\":\"Workspace\",\"body\":[";
  bool comma = false;
  while (it != eset.end()) {
    FileFragment fragment = (*it)->fragment;
    StringSegment content = fragment.fcontent()->segment();
    if (comma) os << ",";
    comma = true;
    os << "{\"type\":\"Program\",\"filename\":\"" << filename(fragment) << "\",\"range\":[0,"
       << content.size() << "],\"source\":\"" << json_escape(content.str()) << "\",\"body\":[";
    bool comma = false;
    while (it != eset.end() && (*it)->fragment.filename() == fragment.filename()) {
      if (comma) os << ",";
      comma = true;
      dump();
    }
    os << "]}";
  }
  os << "]}";
}

void markup_json(const std::string &libdir, std::ostream &os, Expr *root) {
  JSONRender(libdir, os).render(root);
}

void markup_html(const std::string &libdir, std::ostream &os, Expr *root) {
  std::ifstream style(find_execpath() + "/../share/wake/html/style.css");
  std::ifstream utf8(find_execpath() + "/../share/wake/html/utf8.js");
  std::ifstream main(find_execpath() + "/../share/wake/html/main.js");
  os << "<meta charset=\"UTF-8\">" << std::endl;
  os << "<style type=\"text/css\">" << std::endl;
  os << style.rdbuf();
  os << "</style>" << std::endl;
  os << "<script type=\"text/javascript\">" << std::endl;
  os << utf8.rdbuf();
  os << "</script>" << std::endl;
  os << "<script type=\"text/javascript\">" << std::endl;
  os << main.rdbuf();
  os << "</script>" << std::endl;
  os << "<script type=\"wake\">";
  JSONRender(libdir, os).render(root);
  os << "</script>" << std::endl;
}

void format_reexports(std::ostream &os, const char *package, const char *kind,
                      const std::vector<std::string> &mixed) {
  std::set<std::string> id, binary, unary;
  for (auto &t : mixed) {
    if (t.compare(0, 7, "binary ") == 0) {
      binary.insert(t.substr(7));
    } else if (t.compare(0, 6, "unary ") == 0) {
      unary.insert(t.substr(6));
    } else {
      id.insert(t);
    }
  }
  if (!id.empty()) {
    os << "from " << package << " export " << kind;
    for (auto &i : id) os << " " << i;
    os << std::endl;
  }
  if (!unary.empty()) {
    os << "from " << package << " export " << kind << " unary";
    for (auto &i : unary) os << " " << i;
    os << std::endl;
  }
  if (!binary.empty()) {
    os << "from " << package << " export " << kind << " binary";
    for (auto &i : binary) os << " " << i;
    os << std::endl;
  }
}
