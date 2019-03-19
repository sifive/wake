#ifndef SOURCES_H
#define SOURCES_H

#include "primfn.h"
#include <memory>
#include <vector>
#include <string>

struct Value;
struct Receiver;
struct String;

bool chdir_workspace(std::string &prefix);
bool make_workspace(const std::string &dir);
std::string make_canonical(const std::string &x);

std::string find_execpath();
std::string get_cwd();
std::string get_workspace();

std::vector<std::shared_ptr<String> > find_all_sources(bool &ok);
std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &base, const std::string &regexp);

#endif
