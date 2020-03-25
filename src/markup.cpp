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

#include "markup.h"
#include "expr.h"
#include "execpath.h"
#include "json5.h"
#include <set>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <assert.h>

struct ParanOrder {
  bool operator () (Expr *a, Expr *b) const {
    int cmp = strcmp(a->location.filename, b->location.filename);
    if (cmp < 0) return true;
    if (cmp > 0) return false;
    if (a->location.start < b->location.start) return true;
    if (a->location.start > b->location.start) return false;
    return a->location.end > b->location.end;
  }
};

struct JSONRender {
  typedef std::set<Expr*, ParanOrder> ESet;
  std::vector<std::unique_ptr<Expr> > defs;
  std::ostream &os;
  ESet eset;
  ESet::iterator it;

  JSONRender(std::ostream &os_) : os(os_) { }

  void explore(Expr *expr);
  void dump();
  void render(Expr *root);
};

void JSONRender::explore(Expr *expr) {
  if (expr->location.start.bytes >= 0 && (expr->flags & FLAG_AST) != 0)
    eset.insert(expr);

  if (expr->type == &App::type) {
    App *app = static_cast<App*>(expr);
    explore(app->val.get());
    explore(app->fn.get());
  } else if (expr->type == &Lambda::type) {
    Lambda *lambda = static_cast<Lambda*>(expr);
    if (lambda->token.start.bytes >= 0) {
      auto foo = new VarArg(lambda->token);
      foo->typeVar.setDOB(lambda->typeVar[0]);
      lambda->typeVar[0].unify(foo->typeVar);
      defs.emplace_back(foo);
      eset.insert(foo);
    }
    explore(lambda->body.get());
  } else if (expr->type == &DefBinding::type) {
    DefBinding *defbinding = static_cast<DefBinding*>(expr);
    for (auto &i : defbinding->val) explore(i.get());
    for (auto &i : defbinding->fun) explore(i.get());
    for (auto &i : defbinding->order) {
      if (i.second.location.start.bytes >= 0) {
        int val = i.second.index;
        int fun = val - defbinding->val.size();
        Expr *expr = (fun >= 0) ? defbinding->fun[fun].get() : defbinding->val[val].get();
        auto def = new VarDef(i.second.location);
        if (i.first.compare(0, 8, "publish ") == 0) {
          assert (expr->type == &App::type);
          App *app = static_cast<App*>(expr);
          assert (app->val->type == &VarRef::type);
          VarRef *ref = static_cast<VarRef*>(app->val.get());
          def->target = ref->target;
        }
        def->typeVar.setDOB(expr->typeVar);
        expr->typeVar.unify(def->typeVar);
        defs.emplace_back(def);
        eset.insert(def);
      }
    }
    explore(defbinding->body.get());
  }
}

void JSONRender::dump() {
  Location self = (*it)->location;

  os
    << "{\"type\":\"" << (*it)->type->name
    << "\",\"range\":[" << self.start.bytes
    << "," << (self.end.bytes+1)
    << "],\"sourceType\":\"";
  (*it)->typeVar.format(os, (*it)->typeVar);
  os << "\"";

  Location target = LOCATION;

  if ((*it)->type == &VarRef::type) {
    VarRef *ref = static_cast<VarRef*>(*it);
    target = ref->target;
  }

  if ((*it)->type == &VarDef::type) {
    VarDef *def = static_cast<VarDef*>(*it);
    target = def->target;
  }

  if (target.start.bytes >= 0) {
    os
      << ",\"target\":{\"filename\":\"" << json_escape(target.filename)
      << "\",\"range\":[" << target.start.bytes
      << "," << (target.end.bytes+1)
      << "]}";
  }

  ++it;

  bool body = false;
  while (it != eset.end()) {
    Location child = (*it)->location;
    if (child.filename != self.filename) break;
    if (child.start > self.end) break;
    if (body) os << ","; else os << ",\"body\":[";
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
    const char *filename = (*it)->location.filename;
    std::ifstream ifs(filename);
    std::string content(
      (std::istreambuf_iterator<char>(ifs)),
      (std::istreambuf_iterator<char>()));
    if (comma) os << ",";
    comma = true;
    os
      << "{\"type\":\"Program\",\"filename\":\"" << json_escape(filename)
      << "\",\"range\":[0," << content.size()
      << "],\"source\":\"" << json_escape(content)
      << "\",\"body\":[";
    bool comma = false;
    while (it != eset.end() && (*it)->location.filename == filename) {
      if (comma) os << ",";
      comma = true;
      dump();
    }
    os << "]}";
  }
  os << "]}";
}
void markup_json(std::ostream &os, Expr *root) {
  JSONRender(os).render(root);
}

void markup_html(std::ostream &os, Expr *root) {
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
  JSONRender(os).render(root);
  os << "</script>" << std::endl;
}

void markup_ctags(std::ostream &os, Expr *root, const std::vector<std::string>& globals) {
  for (auto &g : globals) {
    Expr *e = root;
    while (e && e->type == &DefBinding::type) {
      DefBinding *d = static_cast<DefBinding*>(e);
      e = d->body.get();
      auto i = d->order.find(g);
      if (i != d->order.end()) {
        int idx = i->second.index;
        Expr *v = idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx-d->val.size()].get();

        os << g << "\t" << v->location.filename << "\t" << v->location.start.row << std::endl;
      }
    }
  }
}

void markup_etags(std::ostream &os, Expr *root, const std::vector<std::string>& globals) {

  typedef std::vector<std::pair<std::string, Location>> Symbols;
  std::map<std::string, Symbols> files;

  for (auto &g : globals) {
    Expr *e = root;
    while (e && e->type == &DefBinding::type) {
      DefBinding *d = static_cast<DefBinding*>(e);
      e = d->body.get();
      auto i = d->order.find(g);
      if (i != d->order.end()) {
        int idx = i->second.index;
        Expr *v = idx < (int)d->val.size() ? d->val[idx].get() : d->fun[idx-d->val.size()].get();
        std::string filename = v->location.filename;
        std::map<std::string, Symbols>::iterator it;
        if ((it = files.find(filename)) != files.end()) {
          (*it).second.push_back(std::make_pair(g, v->location));
        }
        else {
          Symbols symbols;
          symbols.push_back(std::make_pair(g, v->location));
          files.insert(std::make_pair(filename,symbols));
        }
      }
    }
  }

  for(auto file : files) {
    std::ifstream ifs(file.first.c_str(), std::ios_base::in);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line)) {
      lines.push_back(line);
    }

    std::stringbuf buffer;
    std::ostream bufos (&buffer);
    for(auto symbol : file.second) {
      auto row = symbol.second.start.row;
      auto bytes = symbol.second.start.bytes;
      assert(unsigned(row) <= lines.size());
      bufos << lines[row-1] << char(0x7f) << symbol.first<< char(0x01) << row << "," << bytes << std::endl;
    }

    os << char(0x0c) << std::endl << file.first << "," << buffer.str().size() << std::endl;
    os << buffer.str();
  }
}
