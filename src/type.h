#ifndef TYPE_H
#define TYPE_H

#include <ostream>
struct Location;

#define FN "binary =>"

struct TypeVar {
private:
  mutable TypeVar *parent;

  // Scratch variables useful for tree traversals
  mutable TypeVar *link;
  mutable int epoch;

  // before unification, expr children are YOUNGER than their parent
  // before unification, args are YOUNGER than the constructor
  int dob; // date of birth
  int nargs;
  TypeVar *pargs;
  const char *name;

  bool contains(const TypeVar *other) const;
  void do_sweep() const;
  static void do_clone(TypeVar &out, const TypeVar &x, int dob);
  static int do_format(std::ostream &os, int dob, const TypeVar &value, int tags, int p);
  bool do_unify(TypeVar &other);
  void do_debug(std::ostream &os, TypeVar &other, int who, int p);
  void do_cap(int dob);

  bool isFree() const { return name[0] == 0; }

public:
  TypeVar(const TypeVar& other) = delete;
  TypeVar(TypeVar &&other) = delete;
  TypeVar& operator = (const TypeVar &other) = delete;
  TypeVar& operator = (TypeVar &&other) = delete;

  ~TypeVar();
  TypeVar(); // free type-var
  TypeVar(const char *name_, int nargs_);

  const TypeVar *find() const;
  TypeVar *find();

  const TypeVar & operator[](int i) const { return find()->pargs[i]; }
  TypeVar & operator[](int i) { return find()->pargs[i]; }
  int getDOB() const { return find()->dob; }

  void setDOB();
  bool unify(TypeVar &other, Location *location = 0);
  bool unify(TypeVar &&other, Location *location = 0) { return unify(other, location); }

  void clone(TypeVar &into) const;

friend std::ostream & operator << (std::ostream &os, const TypeVar &value);
};

#endif
