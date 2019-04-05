#ifndef TYPE_H
#define TYPE_H

#include <ostream>
struct Location;

#define FN "binary =>"

struct TypeChild;
struct TypeVar {
private:
  mutable TypeVar *parent;

  // Scratch variables useful for tree traversals
  mutable TypeVar *link;
  mutable int epoch;

  // var_dob is unchanging after setDOB
  // free_dob is the DOB of a free variable, unified to the oldest
  int var_dob, free_dob;
  int nargs;
  TypeChild *cargs;
  const char *name;

  bool contains(const TypeVar *other) const;
  void do_sweep() const;
  static void do_clone(TypeVar &out, const TypeVar &x, int dob);
  static int do_format(std::ostream &os, int dob, const TypeVar &value, const char *tag, int tags, int p);
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

  const TypeVar & operator[](int i) const;
  TypeVar & operator[](int i);

  const char *getName() const;
  const char *getTag(int i) const;

  void setDOB();
  void setTag(int i, const char *tag);
  bool unify(TypeVar &other, Location *location = 0);
  bool unify(TypeVar &&other, Location *location = 0) { return unify(other, location); }

  void clone(TypeVar &into) const;
  void format(std::ostream &os, const TypeVar &top) const; // use top's dob

friend std::ostream & operator << (std::ostream &os, const TypeVar &value);
};

struct TypeChild {
  struct TypeVar var;
  const char *tag;
  TypeChild();
};

inline const TypeVar & TypeVar::operator[](int i) const { return find()->cargs[i].var; }
inline       TypeVar & TypeVar::operator[](int i) { return find()->cargs[i].var; }

inline const char *TypeVar::getName() const { return find()->name; }
inline const char *TypeVar::getTag(int i) const { return find()->cargs[i].tag; }

#endif
