#include "prim.h"
#include "value.h"
#include "heap.h"
#include <re2/re2.h>

struct RegExp : public Value {
  RE2 exp;
  static const char *type;
  RegExp(const std::string &regexp, const RE2::Options &opts) : Value(type), exp(re2::StringPiece(regexp), opts) { }
};
const char *RegExp::type = "RegExp";

static std::unique_ptr<Receiver> cast_regexp(std::unique_ptr<Receiver> completion, const std::shared_ptr<Value> &value, RegExp **reg) {
  if (value->type != RegExp::type) {
    resume(std::move(completion), std::make_shared<Exception>(value->to_str() + " is not a RegExp"));
    return std::unique_ptr<Receiver>();
  } else {
    *reg = reinterpret_cast<RegExp*>(value.get());
    return completion;
  }
}
#define REGEXP(arg, i)	 							\
  RegExp *arg;									\
  do {										\
    completion = cast_regexp(std::move(completion), args[i], &arg);		\
    if (!completion) return;							\
  } while(0)


static void prim_re2(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  STRING(arg0, 0);
  RE2::Options options;
  options.set_log_errors(false);
  auto out = std::make_shared<RegExp>(arg0->value, options);
  if (out->exp.ok()) {
    RETURN(out);
  } else {
    auto exp = std::make_shared<Exception>(out->exp.error());
    RETURN(exp);
  }
}

static void prim_quote(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(1);
  STRING(arg0, 0);
  auto out = std::make_shared<String>(RE2::QuoteMeta(arg0->value));
  RETURN(out);
}

static void prim_match(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  auto out = RE2::FullMatch(arg1->value, arg0->exp) ? make_true() : make_false();
  RETURN(out);
}

static void prim_extract(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);

  int matches = arg0->exp.NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> submatch(matches, nullptr);
  re2::StringPiece input(arg1->value);
  if (!arg0->exp.Match(input, 0, arg1->value.size(), RE2::ANCHOR_BOTH, submatch.data(), matches)) {
    auto exp = std::make_shared<Exception>("No match");
    RETURN(exp);
  }

  std::vector<std::shared_ptr<Value> > strings;
  strings.reserve(matches);
  for (int i = 1; i < matches; ++i) strings.emplace_back(std::make_shared<String>(submatch[i].as_string()));
  auto out = make_list(std::move(strings));
  RETURN(out);
}

static void prim_replace(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(3);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  STRING(arg2, 2);

  auto out = std::make_shared<String>(arg2->value);
  RE2::GlobalReplace(&out->value, arg0->exp, arg1->value);
  RETURN(out);
}

static void prim_tokenize(void *data, std::vector<std::shared_ptr<Value> > &&args, std::unique_ptr<Receiver> completion) {
  EXPECT(2);
  REGEXP(arg0, 0);
  STRING(arg1, 1);
  re2::StringPiece input(arg1->value);
  re2::StringPiece hit;
  std::vector<std::shared_ptr<Value> > tokens;
  while (arg0->exp.Match(input, 0, input.size(), RE2::UNANCHORED, &hit, 1)) {
    if (hit.empty()) break;
    re2::StringPiece token(input.data(), hit.data() - input.data());
    tokens.emplace_back(std::make_shared<String>(token.as_string()));
    input.remove_prefix(token.size() + hit.size());
  }
  if (!input.empty()) tokens.emplace_back(std::make_shared<String>(input.as_string()));
  auto out = make_list(std::move(tokens));
  RETURN(out);
}

void prim_register_regexp(PrimMap &pmap) {
  pmap["re2"     ].first = prim_re2;
  pmap["quote"   ].first = prim_quote;
  pmap["match"   ].first = prim_match;
  pmap["extract" ].first = prim_extract;
  pmap["replace" ].first = prim_replace;
  pmap["tokenize"].first = prim_tokenize;
}
