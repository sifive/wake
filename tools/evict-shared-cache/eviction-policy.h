#include <iostream>
#include <string>

struct EvictionPolicy {
  virtual std::string init() = 0;
  virtual std::string read(int id) = 0;
  virtual std::string write(int id) = 0;
};

struct NilEvictionPolicy : EvictionPolicy {
  virtual std::string init() override {
    std::cerr << "init" << std::endl;
    return "";
  }

  virtual std::string read(int id) override {
    std::cerr << "read" << std::endl;
    return "";
  }

  virtual std::string write(int id) override {
    std::cerr << "write" << std::endl;
    return "";
  }
};
