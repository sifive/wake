#include "sources.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include <re2/re2.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

bool make_workspace(const std::string &dir) {
  if (chdir(dir.c_str()) != 0) return false;
  int perm = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
  int fd = open("wake.db", O_RDWR|O_CREAT|O_TRUNC, perm);
  if (fd == -1) return false;
  close(fd);
  return true;
}

bool chdir_workspace() {
  int attempts;
  for (attempts = 100; attempts && access("wake.db", W_OK|R_OK) == -1; --attempts) {
    if (chdir("..") == -1) return false;
  }
  return attempts != 0;
}

static std::string slurp(const char *command) {
  std::stringstream str;
  char buf[4096];
  int got;
  FILE *cmd = popen(command, "r");
  while ((got = fread(buf, 1, sizeof(buf), cmd)) > 0) str.write(buf, got);
  pclose(cmd);
  return str.str();
}

static void scan(std::vector<std::shared_ptr<String> > &out, const std::string &path) {
  if (auto dir = opendir(path.c_str())) {
    while (auto f = readdir(dir)) {
      if (f->d_name[0] == '.' && (f->d_name[1] == 0 || (f->d_name[1] == '.' && f->d_name[2] == 0))) continue;
      if (!strcmp(f->d_name, ".git")) {
        std::string files(slurp("git ls-files -z"));
        std::string prefix(path == "." ? "" : (path + "/"));
        const char *tok = files.data();
        const char *end = tok + files.size();
        for (const char *scan = tok; scan != end; ++scan) {
          if (*scan == 0 && scan != tok) {
            out.emplace_back(std::make_shared<String>(prefix + tok));
            tok = scan+1;
          }
        }
      }
      std::string name(path == "." ? f->d_name : (path + "/" + f->d_name));
      if (f->d_type == DT_DIR) scan(out, name);
    }
    closedir(dir);
  }
}

static bool str_lexical(const std::shared_ptr<String> &a, const std::shared_ptr<String> &b) {
  return a->value < b->value;
}

static bool str_equal(const std::shared_ptr<String> &a, const std::shared_ptr<String> &b) {
  return a->value == b->value;
}

static void distinct(std::vector<std::shared_ptr<String> > &sources) {
  std::sort(sources.begin(), sources.end(), str_lexical);
  auto it = std::unique(sources.begin(), sources.end(), str_equal);
  sources.resize(std::distance(sources.begin(), it));
}

std::vector<std::shared_ptr<String> > find_all_sources() {
  std::vector<std::shared_ptr<String> > out;
  scan(out, ".");
  distinct(out);
  return out;
}

// true if possible, false if illegal (ie: . => ., hax/ => hax, foo/.././bar.z => bar.z, foo/../../bar.z => illegal, /foo => illegal)
static bool make_canonical(std::string &x) {
  if (x[0] == '/') return false; // absolute paths forbidden

  std::vector<std::string> tokens;

  size_t tok = 0;
  size_t scan = 0;
  while ((scan = x.find_first_of('/', tok)) != std::string::npos) {
    std::string token(x, tok, scan-tok);
    tok = scan+1;
    if (token == "..") {
      if (tokens.empty()) return false;
      tokens.pop_back();
    } else if (!token.empty() && token != ".") {
      tokens.emplace_back(std::move(token));
    }
  }

  std::string token(x, tok);
  if (token == "..") {
    if (tokens.empty()) return false;
    tokens.pop_back();
  } else if (!token.empty() && token != ".") {
    tokens.emplace_back(std::move(token));
  }

  if (tokens.empty()) {
    x = ".";
  } else {
    std::stringstream str;
    str << tokens.front();
    for (auto i = tokens.begin() + 1; i != tokens.end(); ++i)
      str << "/" << *i;
    x = str.str();
  }

  return true;
}

static std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const RE2 &exp) {
  std::vector<std::shared_ptr<String> > out;

  if (base == ".") {
    for (auto &i : all) if (RE2::FullMatch(i->value, exp)) out.push_back(i);
  } else {
    long skip = base.size() + 1;
    auto prefixL = std::make_shared<String>(base + "/");
    auto prefixH = std::make_shared<String>(base + "0"); // '/' + 1 = '0'
    auto low  = std::lower_bound(all.begin(), all.end(), prefixL, str_lexical);
    auto high = std::lower_bound(all.begin(), all.end(), prefixH, str_lexical);
    for (auto i = low; i != high; ++i) {
      re2::StringPiece piece((*i)->value.data() + skip, (*i)->value.size() - skip);
      if (RE2::FullMatch(piece, exp)) out.push_back(*i);
    }
  }

  return out;
}

std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const std::string &regexp) {
  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  options.set_dot_nl(true);
  RE2 exp(regexp, options);
  return sources(all, base, exp);
}

static PRIMFN(prim_sources) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);

  std::string root(arg0->value);
  REQUIRE(make_canonical(root), "base directory cannot be made canonical (too many ..s?)");

  RE2::Options options;
  options.set_log_errors(false);
  options.set_one_line(true);
  options.set_dot_nl(true);
  RE2 exp(arg1->value, options);
  if (!exp.ok()) {
    auto fail = std::make_shared<Exception>(exp.error(), binding);
    RETURN(fail);
  }

  std::vector<std::shared_ptr<String> > *all = reinterpret_cast<std::vector<std::shared_ptr<String> >*>(data);
  auto match = sources(*all, root, exp);

  std::vector<std::shared_ptr<Value> > downcast;
  downcast.reserve(match.size());
  for (auto &i : match) downcast.emplace_back(std::move(i));

  auto out = make_list(std::move(downcast));
  RETURN(out);
}

static PRIMFN(prim_add_sources) {
  EXPECT(1);
  STRING(arg0, 0);

  std::vector<std::shared_ptr<String> > *all = reinterpret_cast<std::vector<std::shared_ptr<String> >*>(data);

  const char *tok = arg0->value.data();
  const char *end = tok + arg0->value.size();
  for (const char *scan = tok; scan != end; ++scan) {
    if (*scan == 0 && scan != tok) {
      std::string name(tok);
      if (make_canonical(name)) {
        all->emplace_back(std::make_shared<String>(std::move(name)));
      } else {
        std::cerr << "Warning: Published source '" << name << "' escapes wake workspace; skipped" << std::endl;
      }
      tok = scan+1;
    }
  }

  distinct(*all);
  auto out = make_true();
  RETURN(out);
}

static PRIMFN(prim_simplify) {
  EXPECT(1);
  STRING(arg0, 0);

  auto out = std::make_shared<String>(arg0->value);
  bool ok = make_canonical(out->value);
  REQUIRE(ok, "path escapes wake workspace");
  RETURN(out);
}

static PRIMFN(prim_relative) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);

  bool ok;

  std::string dir(arg0->value);
  ok = make_canonical(dir);
  REQUIRE(ok, "dir escapes wake workspace");

  std::string path(arg1->value);
  ok = make_canonical(path);
  REQUIRE(ok, "path escapes wake workspace");

  if (dir == ".") dir = ""; else dir += '/';
  path += '/';

  size_t skip = 0, end = std::min(path.size(), dir.size());
  for (size_t i = 0; i < end; ++i) {
    if (dir[i] != path[i])
      break;
    if (dir[i] == '/') skip = i+1;
  }

  std::stringstream str;
  for (size_t i = skip; i < dir.size(); ++i)
    if (dir[i] == '/')
      str << "../";

  std::string x;
  std::string last(path, skip, path.size()-skip-1);
  if (last.empty() || last == ".") {
    // remove trailing '/'
    x = str.str();
    if (x.empty()) x = ".";
    else x.resize(x.size()-1);
  } else {
    str << last;
    x = str.str();
  }

  auto out = std::make_shared<String>(x.empty() ? "." : std::move(x));
  RETURN(out);
}

void prim_register_sources(std::vector<std::shared_ptr<String> > *sources, PrimMap &pmap) {
  pmap["sources"].first = prim_sources;
  pmap["sources"].second = sources;
  pmap["add_sources"].first = prim_add_sources;
  pmap["add_sources"].second = sources;
  pmap["simplify"].first = prim_simplify;
  pmap["relative"].first = prim_relative;
}
