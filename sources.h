#ifndef SOURCES_H
#define SOURCES_H

#include "primfn.h"
#include <memory>
#include <vector>
#include <string>

struct Value;
struct Receiver;
struct String;

bool chdir_workspace();
bool make_workspace(const std::string &dir);

std::vector<std::shared_ptr<String> > find_all_sources();
std::vector<std::shared_ptr<String> > sources(const std::vector<std::shared_ptr<String> > &all, const std::string &regexp);
PRIMFN(prim_sources);

#endif
