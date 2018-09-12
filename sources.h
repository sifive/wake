#ifndef SOURCES_H
#define SOURCES_H

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
void prim_sources(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion);

#endif
