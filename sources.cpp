#include "sources.h"
#include "prim.h"
#include "value.h"
#include "heap.h"
#include <re2/re2.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

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
    chdir("..");
  }
  return attempts != 0;
}

static void scan(std::vector<std::shared_ptr<String> > &out, const std::string &path) {
  if (auto dir = opendir(path.c_str())) {
    while (auto f = readdir(dir)) {
      if (f->d_name[0] == '.') continue;
      std::string name(path + "/" + f->d_name);
      if (f->d_type == DT_DIR) scan(out, name);
      if (f->d_type == DT_REG) out.emplace_back(std::make_shared<String>(std::move(name)));
    }
    closedir(dir);
  }
}

std::vector<std::shared_ptr<String> > find_all_sources() {
  std::vector<std::shared_ptr<String> > out;
  /* !!! Plan: find all sources at power-on, capture into set<string> in main
   *  recursively find all .git files
   *  ... then run git ls-files
   */
  scan(out, ".");
  return out;
}

std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &regexp) {
  std::vector<std::shared_ptr<String> > out;
  RE2::Options options;
  options.set_log_errors(false);
  RE2 exp(regexp);
  if (exp.ok())
    for (auto &i : all)
      if (RE2::FullMatch(i->value, exp))
         out.push_back(i);
  return out;
}

void prim_sources(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  STRING(arg0, 0);
  STRING(arg1, 1);

  std::vector<std::shared_ptr<String> > *all = reinterpret_cast<std::vector<std::shared_ptr<String> >*>(data);
  auto match = sources(*all, arg1->value);

  std::vector<std::shared_ptr<Value> > downcast;
  downcast.reserve(match.size());
  for (auto &i : match) downcast.emplace_back(std::move(i));

  auto out = make_list(std::move(downcast));
  RETURN(out);
}
