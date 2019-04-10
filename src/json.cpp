#include "prim.h"
#include "value.h"
#include "symbol.h"
#include "expr.h"
#include "parser.h"
#include <sstream>

static bool expect(SymbolType type, JLexer &jlex, std::ostream& errs) {
  if (jlex.next.type != type) {
    if (!jlex.fail)
      errs << "Was expecting a "
        << symbolTable[type] << ", but got a "
        << symbolTable[jlex.next.type] << " at "
        << jlex.next.location.text();
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

static std::shared_ptr<Value> parse_jvalue(JLexer &jlex, std::ostream& errs);

// JSON5Array:
//   []
//   [JSON5ElementList ,opt]
// JSON5ElementList:
//   JSON5Value
//   JSON5ElementList , JSON5Value
static std::shared_ptr<Value> parse_jarray(JLexer &jlex, std::ostream& errs) {
  jlex.consume();

  bool repeat = true;
  std::vector<std::shared_ptr<Value> > values;

  while (repeat) {
    if (jlex.next.type == SCLOSE) {
      jlex.consume();
      break;
    }

    values.emplace_back(parse_jvalue(jlex, errs));
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
        if (!jlex.fail)
          errs << "Was expecting COMMA/SCLOSE, got a "
            << symbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

  return getJValue(make_list(std::move(values)), 6);
}

// JSON5Object:
//   {}
//   {JSON5MemberList ,opt}
// JSON5MemberList:
//   JSON5Member
//   JSON5MemberList , JSON5Member
// JSON5Member:
//   JSON5MemberName : JSON5Value
// JSON5MemberName:
//   JSON5Identifier
//   JSON5String
static std::shared_ptr<Value> parse_jobject(JLexer &jlex, std::ostream& errs) {
  jlex.consume();

  bool repeat = true;
  std::vector<std::shared_ptr<Value> > values;

  while (repeat) {
    if (jlex.next.type == BCLOSE) {
      jlex.consume();
      break;
    }

    // Extract the JSON key
    std::shared_ptr<Value> key;
    switch (jlex.next.type) {
      case STR: {
        Literal *lit = reinterpret_cast<Literal*>(jlex.next.expr.get());
        key = std::move(lit->value);
        jlex.consume();
        break;
      }
      case ID: {
        key = std::make_shared<String>(jlex.text());
        jlex.consume();
        break;
      }
      default: {
        if (!jlex.fail)
          errs << "Was expecting ID/STR, got a "
            << symbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }

    expect(COLON, jlex, errs);
    jlex.consume();

    values.emplace_back(make_tuple2(std::move(key), parse_jvalue(jlex, errs)));

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
        if (!jlex.fail)
          errs << "Was expecting COMMA/BCLOSE, got a "
            << symbolTable[jlex.next.type]
            << " at " << jlex.next.location.text();
        jlex.fail = true;
        repeat = false;
        break;
      }
    }
  }

  return getJValue(make_list(std::move(values)), 5);
}

// JSON5Value:
//   JSON5Null
//   JSON5Boolean
//   JSON5String
//   JSON5Number
//   JSON5Object
//   JSON5Array
static std::shared_ptr<Value> parse_jvalue(JLexer &jlex, std::ostream& errs) {
  switch (jlex.next.type) {
    case NULLVAL: {
      jlex.consume();
      return std::make_shared<Data>(&JValue->members[4], nullptr);
    }
    case TRUE: {
      jlex.consume();
      return getJValue(make_bool(true), 3);
    }
    case FALSE: {
      jlex.consume();
      return getJValue(make_bool(false), 3);
    }
    case STR: {
      auto out = getJValue(jlex, 0);
      jlex.consume();
      return out;
    }
    case DOUBLE: {
      auto out = getJValue(jlex, 2);
      jlex.consume();
      return out;
    }
    case NUM: {
      auto out = getJValue(jlex, 1);
      jlex.consume();
      return out;
    }
    case BOPEN: {
      return parse_jobject(jlex, errs);
    }
    case SOPEN: {
      return parse_jarray(jlex, errs);
    }
    default: {
      if (!jlex.fail)
        errs << "Unexpected symbol "
          << symbolTable[jlex.next.type]
          << " at " << jlex.next.location.text();
      jlex.fail = true;
      return nullptr;
    }
  }
}

static PRIMTYPE(type_json) {
  return args.size() == 1 &&
    args[0]->unify(String::typeVar) &&
    out->unify(Data::typeJValue);
}

// JSON5Text:
//   JSON5Value
static PRIMFN(prim_json_file) {
  EXPECT(1);
  STRING(file, 0);
  JLexer jlex(file->value.c_str());
  std::stringstream errs;
  auto out = parse_jvalue(jlex, errs);
  expect(END, jlex, errs);
  if (jlex.fail) RAISE(errs.str());
  RETURN(out);
}

static PRIMFN(prim_json_body) {
  EXPECT(1);
  STRING(body, 0);
  JLexer jlex(body->value, "<input-string>");
  std::stringstream errs;
  auto out = parse_jvalue(jlex, errs);
  expect(END, jlex, errs);
  if (jlex.fail) RAISE(errs.str());
  RETURN(out);
}

void prim_register_json(PrimMap &pmap) {
  prim_register(pmap, "json_file", prim_json_file, type_json, PRIM_SHALLOW);
  prim_register(pmap, "json_body", prim_json_body, type_json, PRIM_SHALLOW|PRIM_PURE);
}
