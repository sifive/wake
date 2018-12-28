#ifndef TYPE_H
#define TYPE_H

#include <vector>
#include <string>

struct TypeVar {
private:
  mutable TypeVar *parent;

  // before unification, expr children are YOUNGER than their parent
  // before unification, args are YOUNGER than the constructor
  int dob; // date of birth
  std::string name;
  std::vector<TypeVar> args;

  const TypeVar *find() const;
  TypeVar *find();

  TypeVar(int, TypeVar &&child);

public:
  TypeVar(); // free type-var
  TypeVar(const std::string &name_, int nargs);
  TypeVar(const TypeVar& other) = default;
  TypeVar(TypeVar &&other) = default;

  const std::string &getName() const { return find()->name; }
  const std::vector<TypeVar> &getArgs() const { return find()->args; }
  std::vector<TypeVar> &getArgs() { return find()->args; }
  int getDOB() const { return find()->dob; }
  bool isFree() const { return getName().size() == 0; }

  void setDOB();
  bool unifyVal(TypeVar &other);
  bool unifyDef(const TypeVar &other, int dob);

  static TypeVar primFn(int nargs);
  TypeVar &getArg(int arg);
};

std::ostream & operator << (std::ostream &os, const TypeVar &value);

#endif
