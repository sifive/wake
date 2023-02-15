#include <iostream>
#include <string>

struct EvictionPolicy {
  virtual void init() = 0;
  virtual void read(int id) = 0;
  virtual void write(int id) = 0;
};

struct NilEvictionPolicy : EvictionPolicy {
  virtual void init() override {}

  virtual void read(int id) override {}

  virtual void write(int id) override {}
};
