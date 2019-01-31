#include "prim.h"
#include "value.h"
#include "symbol.h"
#include "expr.h"
#include "parser.h"
#include <iostream>

static bool expect(SymbolType type, JLexer &jlex) {
  if (jlex.next.type != type) {
    std::cerr << "Was expecting a "
      << symbolTable[type] << ", but got a "
      << symbolTable[jlex.next.type] << " at "
      << jlex.next.location << std::endl;
    jlex.fail = true;
    return false;
  }
  return true;
}

static std::unique_ptr<Lambda> eJValue(new Lambda(LOCATION, "_", nullptr));

static std::shared_ptr<Value> getJValue(std::shared_ptr<Value> &&value, int member) {
  auto bind = std::make_shared<Binding>(nullptr, nullptr, eJValue.get(), 1);
  bind->future[0].value = value;
  bind->state = 1;
  return std::make_shared<Data>(&JValue->members[member], std::move(bind));
}

static std::shared_ptr<Value> getJValue(JLexer &jlex, int member) {
  Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
  return getJValue(std::move(lit->value), member);
}

static std::shared_ptr<Value> parse_jelement(JLexer &jlex);

static std::shared_ptr<Value> parse_jlist(JLexer &jlex) {
  jlex.consume();

  bool repeat = true;
  std::vector<std::shared_ptr<Value> > values;

  if (jlex.next.type == SCLOSE) {
    jlex.consume();
    goto done;
  }

  while (repeat) {
    values.emplace_back(parse_jelement(jlex));
    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case SCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        std::cerr << "Was expecting COMMA/SCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

done:
  return getJValue(make_list(std::move(values)), 6);
}

static std::shared_ptr<Value> parse_jobject(JLexer &jlex) {
  jlex.consume();

  bool repeat = true;
  std::vector<std::shared_ptr<Value> > values;

  if (jlex.next.type == BCLOSE) {
    jlex.consume();
    goto done;
  }

  while (repeat) {
    // Extract the JSON key
    std::shared_ptr<Value> key;
    if (expect(STR, jlex)) {
      Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
      key = std::move(lit->value);
    }
    jlex.consume();

    expect(COLON, jlex);
    jlex.consume();

    values.emplace_back(make_tuple(std::move(key), parse_jelement(jlex)));

    switch (jlex.next.type) {
      case COMMA: {
        jlex.consume();
        break;
      }
      case BCLOSE: {
        jlex.consume();
        repeat = false;
        break;
      }
      default: {
        std::cerr << "Was expecting COMMA/BCLOSE, got a "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

done:
  return getJValue(make_list(std::move(values)), 5);
}

static std::shared_ptr<Value> parse_jelement(JLexer &jlex) {
  switch (jlex.next.type) {
    case FLOAT: {
      auto out = getJValue(jlex, 2);
      jlex.consume();
      return out;
    }
    case NULLVAL: {
      jlex.consume();
      return std::make_shared<Data>(&JValue->members[4], nullptr);
    }
    case STR: {
      auto out = getJValue(jlex, 0);
      jlex.consume();
      return out;
    }
    case NUM: {
      auto out = getJValue(jlex, 1);
      jlex.consume();
      return out;
    }
    case TRUE: {
      jlex.consume();
      return getJValue(make_bool(true), 3);
    }
    case FALSE: {
      jlex.consume();
      return getJValue(make_bool(false), 3);
    }
    case SOPEN: {
      return parse_jlist(jlex);
    }
    case BOPEN: {
      return parse_jobject(jlex);
    }
    default: {
      jlex.fail = true;
      std::cerr << "Unexpected symbol "
        << symbolTable[jlex.next.type]
          << " at " << jlex.next.location << std::endl;
      return nullptr;
    }
  }
}

static PRIMTYPE(type_json) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeJValue);
}

static PRIMFN(prim_json) {
  EXPECT(1);
  STRING(file, 0);
  JLexer jlex(file->value.c_str());
  auto out = parse_jelement(jlex);
  expect(END, jlex);
  RETURN(out);
}

void prim_register_json(PrimMap &pmap) {
  pmap.emplace("json", PrimDesc(prim_json, type_json));
}
