%include {#include <string.h>}
%include {#include "syntax.h"}
%include {#include "reporter.h"}
%include {#include "cst.h"}
%include {#include "file.h"}
%include {#include <vector>}
%include {#include <sstream>}
%include {#include <iostream>}

%token_prefix TOKEN_
%token_type {TokenInfo}
%default_type {size_t}
%extra_argument {ParseInfo pinfo}
%stack_size {0}
%default_destructor { (void)pinfo; }

// These are to disambiguate syntactically invalid subscribe/prim
%right ID STR_SINGLE.

// The lexer also produces these (unused by parser):
%token WS COMMENT P_BOPEN P_BCLOSE P_SOPEN P_SCLOSE.

%include {
#define fault(sstream, ...)                                                            \
  do {                                                                                 \
    pinfo.cst->addNode(CST_ERROR, __VA_ARGS__);                                        \
    std::stringstream sstr;                                                            \
    sstr << "syntax error; " << sstream;                                               \
    Location l = pinfo.cst->lastNode().location(*pinfo.fcontent);                      \
    pinfo.reporter->report(REPORT_ERROR, l, sstr.str());                               \
  } while (0)

#define add(t, ...)         pinfo.cst->addNode((t), __VA_ARGS__)
}

start ::= top(T). { add(CST_TOP, T); }

top(R) ::= top(T) topdef. { R = T+1; }
top(R) ::= .              { R = 0; }

id ::= ID(k). { add(CST_ID, k, 0, k); }

topdef ::= KW_PACKAGE(b) NL(e).     { fault("keyword 'package' must be followed by a package name", b, 0, e); }
topdef ::= KW_PACKAGE(b) id NL(e).  { add(CST_PACKAGE, b, 1, e); }

topdef ::= KW_FROM(b) NL(e).                               { fault("keyword 'from' must be followed by a package name", b, 0, e); }
topdef ::= KW_FROM(b) id NL(e).                            { fault("keyword 'from' must be followed by a package name and 'import' or 'export'", b, 1, e); }
topdef ::= KW_FROM(b) id KW_IMPORT kind(K) arity(A) NL(e). { fault("package import must be followed by a list of identifiers or operators", b, 1+K+A, e); }
topdef ::= KW_FROM(b) id KW_EXPORT kind(K) arity(A) NL(e). { fault("package export must be followed by a list of identifiers or operators", b, 1+K+A, e); }

topdef ::= KW_FROM(b) id KW_IMPORT P_HOLE NL(e).                       { add(CST_IMPORT,  b, 1, e); }
topdef ::= KW_FROM(b) id KW_IMPORT kind(K) arity(A) idopeqs(I) NL(e).  { add(CST_IMPORT,  b, 1+K+A+I, e); }
topdef ::= KW_FROM(b) id KW_EXPORT kind(K) arity(A) idopeqs(I) NL(e).  { add(CST_EXPORT,  b, 1+K+A+I, e); }

kind(R) ::= KW_DEF(k).   { R = 1; add(CST_KIND, k, 0, k); }
kind(R) ::= KW_TYPE(k).  { R = 1; add(CST_KIND, k, 0, k); }
kind(R) ::= KW_TOPIC(k). { R = 1; add(CST_KIND, k, 0, k); }
kind(R) ::= .            { R = 0; }

arity(R) ::= KW_UNARY(k).  { R = 1; add(CST_ARITY, k, 0, k); }
arity(R) ::= KW_BINARY(k). { R = 1; add(CST_ARITY, k, 0, k); }
arity(R) ::= .             { R = 0; }

idopeqs(R) ::= idopeqs(I) idopeq. { R = I+1; }
idopeqs(R) ::= idopeq.            { R = 1;   }

idopeq ::= idop P_EQUALS idop. { add(CST_IDEQ, 2); }
idopeq ::= idop.               { add(CST_IDEQ, 1); }

idop ::= ID(k).         { add(CST_ID, k, 0, k); }
idop ::= OP_DOT(k).     { add(CST_OP, k, 0, k); }
idop ::= OP_QUANT(k).   { add(CST_OP, k, 0, k); }
idop ::= OP_EXP(k).     { add(CST_OP, k, 0, k); }
idop ::= OP_MULDIV(k).  { add(CST_OP, k, 0, k); }
idop ::= OP_ADDSUB(k).  { add(CST_OP, k, 0, k); }
idop ::= OP_COMPARE(k). { add(CST_OP, k, 0, k); }
idop ::= OP_INEQUAL(k). { add(CST_OP, k, 0, k); }
idop ::= OP_AND(k).     { add(CST_OP, k, 0, k); }
idop ::= OP_OR(k).      { add(CST_OP, k, 0, k); }
idop ::= OP_DOLLAR(k).  { add(CST_OP, k, 0, k); }
idop ::= OP_LRARROW(k). { add(CST_OP, k, 0, k); }
idop ::= OP_EQARROW(k). { add(CST_OP, k, 0, k); }
idop ::= OP_COMMA(k).   { add(CST_OP, k, 0, k); }

global(R) ::= .             { R = 0; }
global(R) ::= KW_GLOBAL(k). { R = 1; add(CST_FLAG_GLOBAL, k, 0, k); }
export(R) ::= .             { R = 0; }
export(R) ::= KW_EXPORT(k). { R = 1; add(CST_FLAG_EXPORT, k, 0, k); }
 
topdef ::= global(G) export(E) KW_TOPIC(b) NL(e).                 { fault("keyword 'topic' must be followed by a topic name", b, G+E, e); }
topdef ::= global(G) export(E) KW_TOPIC(b) id NL(e).              { fault("topic names must be followed by ': type'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_TOPIC(b) id P_COLON NL(e).      { fault("topic names must be followed by ': type'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_TOPIC(b) id P_COLON type NL(e). { add(CST_TOPIC, b, G+E+2, e); }

topdef ::= KW_PUBLISH(b) NL(e).             { fault("keyword 'publish' must be followed by a topic name", b, 0, e); }
topdef ::= KW_PUBLISH(b) id NL(e).          { fault("publishes must be followed by an '= expression'",    b, 1, e); }
topdef ::= KW_PUBLISH(b) id P_EQUALS NL(e). { fault("publishes must be followed by an '= expression'",    b, 1, e); }
topdef ::= KW_PUBLISH(b) id P_EQUALS block_opt NL(e). { add(CST_PUBLISH, b, 2, e); }

topdef ::= global(G) export(E) KW_DATA(b) NL(e).               { fault("keyword 'data' must be followed by a type expression", b, G+E, e); }
topdef ::= global(G) export(E) KW_DATA(b) type NL(e).          { fault("data types must be followed by an '= constructor-cases*'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS NL(e). { fault("data types must be followed by an '= constructor-cases*'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS INDENT DEDENT NL(e). { fault("data types must be followed by an '= constructor-cases*'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS INDENT data_elts(D) DEDENT NL(e). { add(CST_DATA, b, G+E+1+D, e); }
topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS type NL(e).                       { add(CST_DATA, b, G+E+2,   e); }

data_elts(R) ::= type.                  { R = 1;   }
data_elts(R) ::= data_elts(E) NL type.  { R = E+1; }
data_elts(R) ::= data_elts(E) NL.       { R = E;   } // happens when the last constructor is an error

topdef ::= global(G) export(E) KW_TUPLE(b) NL(e).               { fault("keyword 'tuple' must be followed by a type expression", b, G+E, e); }
topdef ::= global(G) export(E) KW_TUPLE(b) type NL(e).          { fault("tuple types definitions must be followed by an '='", b, 1+G+E, e); }
topdef ::= global(G) export(E) KW_TUPLE(b) type P_EQUALS NL(e). { fault("tuple type definitions must be followed by indented element definitions", b, 1+G+E, e); }
topdef ::= global(G) export(E) KW_TUPLE(b) type P_EQUALS INDENT DEDENT NL(e). { fault("tuple type definitions must be followed by indented element definitions", b, 1+G+E, e); }
topdef ::= global(G) export(E) KW_TUPLE(b) type P_EQUALS INDENT tuple_elts(T) DEDENT NL(e). { add(CST_TUPLE, b, G+E+1+T, e); }

tuple_elts(R) ::= tuple_elt.                  { R = 1;   }
tuple_elts(R) ::= tuple_elts(E) NL tuple_elt. { R = E+1; }
// If the last tuple element starts with something illegal, it breaks the following line. :-(

tuple_elt ::= global(G) export(E) type. { add(CST_TUPLE_ELT, G+E+1); }

topdef ::= global(G) export(E) KW_DEF(b) NL(e).                  { fault("keyword 'def' must be followed by a pattern", b, G+E,   e); }
topdef ::= global(G) export(E) KW_DEF(b) pattern NL(e).          { fault("definitions must be followed by an '= expression'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DEF(b) pattern P_EQUALS NL(e). { fault("definitions must be followed by an '= expression'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DEF(b) pattern P_EQUALS block_opt NL(e). { add(CST_DEF, b, G+E+2, e); }

topdef ::= global(G) export(E) KW_TARGET(b) NL(e).                  { fault("keyword 'target' must be followed by a pattern", b, G+E,   e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern NL(e).          { fault("targets must be followed by an '= expression'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern P_EQUALS NL(e). { fault("targets must be followed by an '= expression'", b, G+E+1, e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern P_EQUALS block_opt NL(e). { add(CST_TARGET, b, G+E+2, e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern P_BSLASH pattern_terms(T) P_EQUALS block_opt NL(e). { add(CST_TARGET, b, G+E+1+T+1, e); }

dnl Left-associative prEfix-heavy; 1 + + 4 + + 5 = (1 + (+4)) + (+5)
define(`LE',
$1_op_$3 ::= $2(k).                                 { add(CST_OP, k, 0, k); }
$1_binary_$3 ::= $1_binary_$3 $1_op_$3 $1_unary_$3. { add(CST_BINARY, 3); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_op_$3 $1_unary_$3.               { add(CST_UNARY,  2); }
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative, prEfix-heavy; 1 $ $ 4 $ $ 5 = 1 $ (($4) $ ($5))
define(`RE',
$1_op_$3 ::= $2(k).                                 { add(CST_OP, k, 0, k); }
$1_binary_$3 ::= $1_unary_$3 $1_op_$3 $1_binary_$3. { add(CST_BINARY, 3); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_op_$3 $1_unary_$3.               { add(CST_UNARY,  2); }
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative pOstfix-heavy; 1 , , 4 , , 5 = (1,) , ((4,) , 5)
define(`RO',
$1_op_$3 ::= $2(k).                                 { add(CST_OP, k, 0, k); }
$1_binary_$3 ::= $1_unary_$3 $1_op_$3 $1_binary_$3. { add(CST_BINARY, 3); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
$1_unary_$3 ::= $1_unary_$3 $1_op_$3.               { add(CST_UNARY,  2); }
)

dnl All operators
define(`OPCHAIN',
LE($1, OP_QUANT,   quant,   app)         dnl x ∑ ∑ y ∑ ∑ z       x ∑ ((∑y) ∑ (∑z))
RE($1, OP_EXP,     exp,     quant)       dnl x ^ ^ y ^ ^ z       x ^ ((^y) ^ (^z))
LE($1, OP_MULDIV,  muldiv,  exp)         dnl x * * y * * z       (x * (*y)) * (*z)
LE($1, OP_ADDSUB,  addsub,  muldiv)      dnl 4 + - 5 - - 7       (4 + (-5)) - (-7)
LE($1, OP_COMPARE, compare, addsub)      dnl 4 < < 5 < < 7       (4 < (<5)) < (<7)
LE($1, OP_INEQUAL, inequal, compare)     dnl x != !y != !z       (x != (!y)) != (!z)
LE($1, OP_AND,     and,     inequal)     dnl x & & y & & z       (x & (&y)) & (&z)
LE($1, OP_OR,      or,      and)         dnl x | | y | | z       (x | (|y)) | (|z)
RO($1, OP_DOLLAR,  dollar,  or)          dnl x $ $ y $ $ z       x $ (($y) $ ($z))
RE($1, P_COLON,    colon,   dollar)      dnl x : : y : : z       (x:): ((y:): z)
LE($1, OP_LRARROW, lrarrow, colon)       dnl as compare
LE($1, OP_EQARROW, eqarrow, lrarrow)     dnl as inequal
RO($1, OP_COMMA,   comma,   eqarrow)     dnl x , , y , , z       (x,), ((y,), z)
)

OPCHAIN(expression)

expression_term ::= expression_term OP_DOT expression_nodot. { add(CST_BINARY, 2); }
expression_term ::= expression_nodot.

expression_nodot ::= ID(i).                            { add(CST_ID,    i, 0, i); }
expression_nodot ::= P_POPEN(b) block_opt P_PCLOSE(e). { add(CST_PAREN, b, 1, e); }

expression_nodot ::= P_HOLE(h).     { add(CST_HOLE,    h, 0, h); }
expression_nodot ::= STR_RAW(s).    { add(CST_LITERAL, s, 0, s); }
expression_nodot ::= STR_SINGLE(s). { add(CST_LITERAL, s, 0, s); }
expression_nodot ::= str_open interpolated_string(N) str_close. { add(CST_INTERPOLATE, 2+N); }
expression_nodot ::= str_open str_close. { fault("interpolated string is missing {expression}", 2); }

interpolated_string(R) ::= block_opt. { R = 1; }
interpolated_string(R) ::= interpolated_string(O) str_mid. { R = O; fault("interpolated string is missing {expression}", 1); }
interpolated_string(R) ::= interpolated_string(O) str_mid block_opt. { R = O+2; }

str_mid   ::= STR_MID(s).   { add(CST_LITERAL, s, 0, s); }
str_open  ::= STR_OPEN(s).  { add(CST_LITERAL, s, 0, s); }
str_close ::= STR_CLOSE(s). { add(CST_LITERAL, s, 0, s); }

expression_nodot ::= REG_SINGLE(r). { add(CST_LITERAL, r, 0, r); }
expression_nodot ::= reg_open interpolated_regexp(N) reg_close. { add(CST_INTERPOLATE, 2+N); }
expression_nodot ::= reg_open reg_close. { fault("interpolated regular expression is missing {expression}", 2); }

interpolated_regexp(R) ::= block_opt. { R = 1; }
interpolated_regexp(R) ::= interpolated_regexp(O) reg_mid. { R = O; fault("interpolated regular expression is missing ${expression}", 1); }
interpolated_regexp(R) ::= interpolated_regexp(O) reg_mid block_opt. { R = O+2; }

reg_mid   ::= REG_MID(r).   { add(CST_LITERAL, r, 0, r); }
reg_open  ::= REG_OPEN(r).  { add(CST_LITERAL, r, 0, r); }
reg_close ::= REG_CLOSE(r). { add(CST_LITERAL, r, 0, r); }

expression_nodot ::= DOUBLE(d).  { add(CST_LITERAL, d, 0, d); }
expression_nodot ::= INTEGER(i). { add(CST_LITERAL, i, 0, i); }
expression_nodot ::= KW_HERE(h). { add(CST_LITERAL, h, 0, h); }

expression_binary_app ::= expression_binary_app expression_term. { add(CST_APP, 2); }
expression_binary_app ::= expression_term.

expression_binary_app ::= KW_SUBSCRIBE(b) id. [ID] { add(CST_SUBSCRIBE, b, 1); }
expression_binary_app ::= KW_SUBSCRIBE(b).    [ID] { fault("keyword 'subscribe' must be followed by an identifier", b, 0, b); }

expression_binary_app ::= KW_PRIM(b) prim_literal. [STR_SINGLE] { add(CST_PRIM, b, 1); }
expression_binary_app ::= KW_PRIM(b).              [STR_SINGLE] { fault("keyword 'prim' must be followed by a \"string\"", b, 0, b); }

prim_literal ::= STR_SINGLE(s). { add(CST_LITERAL, s, 0, s); }

// !!!
expression_binary_app ::= KW_MATCH(b) expression_term INDENT match1_cases(C) DEDENT. { add(CST_MATCH, b, 1+C); }
expression_binary_app ::= KW_MATCH(b) match_terms(T)  INDENT matchx_cases(C) DEDENT. { add(CST_MATCH, b, T+C); }

match1_cases(R) ::= match1_cases(C) NL match1_case.  { R = C+1; }
match1_cases(R) ::= match1_case.                     { R = 1;   }
match1_case ::= pattern guard(G) P_EQUALS block_opt. { add(CST_CASE, 2+G); }

match_terms(R) ::= expression_term expression_term. { R = 2; }
match_terms(R) ::= match_terms(T) expression_term.  { R = 1+T; }

matchx_cases(R) ::= matchx_cases(C) NL matchx_case.           { R = C+1; }
matchx_cases(R) ::= matchx_case.                              { R = 1; }
matchx_case ::= pattern_terms(T) guard(G) P_EQUALS block_opt. { add(CST_CASE, T+G+1); }

pattern_terms(R) ::= pattern_terms(T) pattern_term. { R = 1+T; }
pattern_terms(R) ::= pattern_term.                  { R = 1; }

guard(R) ::= .                    { R = 0; }
guard(R) ::= KW_IF(b) expression. { R = 1; add(CST_GUARD, b, 1); }

expression ::= expression_binary_comma.

//!!!
expression ::= P_BSLASH(b) pattern_term expression.                    { add(CST_LAMBDA, b, 2); }
//!!!
expression ::= KW_IF(b) block_opt KW_THEN block_opt KW_ELSE block_opt. { add(CST_IF,     b, 3); }

pattern ::= expression.
type ::= expression.
pattern_term ::= expression_term.

block_opt ::= expression.
block_opt ::= INDENT block DEDENT.
block_opt ::= INDENT(b) DEDENT(e). { fault("illegal block expression", b, 0, e); }
block_opt ::= INDENT(b) blockdefs(N) DEDENT(e). { fault("illegal block expression", b, N, e); }

block ::= blockdefs(N) body. { add(CST_BLOCK, N+1); }
block ::= body.

body ::= expression.

body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt NL                      block. { add(CST_REQUIRE, b, 3); }
body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt NL KW_ELSE block_opt NL block. { add(CST_REQUIRE, b, 4); }
body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt    KW_ELSE block_opt NL block. { add(CST_REQUIRE, b, 4); }

blockdefs(R) ::= blockdef.              { R = 1;   }
blockdefs(R) ::= blockdefs(D) blockdef. { R = 1+D; }

blockdef ::= KW_DEF(b) NL(e).                  { fault("keyword 'def' must be followed by a pattern", b, 0, e); }
blockdef ::= KW_DEF(b) pattern NL(e).          { fault("definitions must be followed by an '= expression'", b, 1, e); }
blockdef ::= KW_DEF(b) pattern P_EQUALS NL(e). { fault("definitions must be followed by an '= expression'", b, 1, e); }
blockdef ::= KW_DEF(b) pattern P_EQUALS block_opt NL(e).                { add(CST_DEF,    b, 2, e); }

blockdef ::= KW_FROM(b) NL(e).                               { fault("keyword 'from' must be followed by a package name", b, 0, e); }
blockdef ::= KW_FROM(b) id NL(e).                            { fault("keyword 'from' must be followed by a package name and 'import'", b, 1, e); }
blockdef ::= KW_FROM(b) id KW_IMPORT kind(K) arity(A) NL(e). { fault("package import must be followed by a list of identifiers or operators", b, 1+K+A, e); }
blockdef ::= KW_FROM(b) id KW_IMPORT P_HOLE NL(e).                      { add(CST_IMPORT, b, 1, e); }
blockdef ::= KW_FROM(b) id KW_IMPORT kind(K) arity(A) idopeqs(I) NL(e). { add(CST_IMPORT, b, 1+K+A+I, e); }

%code {
bool ParseShifts(void *p, int yymajor) {
  yyParser *pParser = (yyParser*)p;
  YYACTIONTYPE yyact = pParser->yytos->stateno;
  std::vector<YYACTIONTYPE> speculation;
  size_t offset = pParser->yytos - pParser->yystack;
  size_t end = offset + 1;

  while (1) {
    // printf("STATE %d => ", yyact);
    yyact = yy_find_shift_action((YYCODETYPE)yymajor, yyact);
    // printf("ACTION %d => ", yyact);
    if (yyact >= YY_MIN_REDUCE) {
      int yyruleno = yyact - YY_MIN_REDUCE;
      int yygoto = yyRuleInfoLhs[yyruleno];
      int yysize = yyRuleInfoNRhs[yyruleno];
      // printf("REDUCE %d by %d => ", yyruleno, yysize);
      YYACTIONTYPE stateno;
      offset += yysize; // remove tokens
      if (offset < end) {
        stateno = pParser->yystack[offset].stateno;
        end = ++offset;
        speculation.clear();
      } else {
      	stateno = speculation[offset-end];
        speculation.resize(++offset-end);
      }
      // printf("FIND %d %d => ", stateno, yygoto);
      yyact = yy_find_reduce_action(stateno, (YYCODETYPE)yygoto);
      speculation.push_back(yyact);
      // printf("%d\n", yyact);
    } else if (yyact <= YY_MAX_SHIFTREDUCE) {
      // printf("shift\n");
      return true;
    } else if (yyact == YY_ACCEPT_ACTION) {
      // printf("accept\n");
      return true;
    } else {
      // printf("reject\n");
      return false;
    }
  }
}
}

%syntax_error { 
  const char *example = symbolExample(yymajor);
  int example_len = strlen(example);
  int token_len = yyminor.size();
  std::stringstream ss, tokens;
  ss << "syntax error; found ";
  if (token_len > 0) ss << yyminor << ", ";
  bool same =
    token_len == example_len &&
    memcmp(yyminor.start, example, example_len) == 0;
  if (!same || token_len == 0) ss << "a " << example << ", ";
  tokens << "but was expecting one of:\n";
  int num = 0;
  for (int i = 1; i < YYNTOKEN; ++i) {
    int a = yy_find_shift_action((YYCODETYPE)i, yypParser->yytos->stateno);
    if (a < YYNSTATE + YYNRULE) {
      ++num;
      tokens << "    " << symbolExample(i);
    }
  }
  if (num) {
    ss << tokens.rdbuf();
  } else {
    ss << "which is inappropriate here";
  }
  pinfo.reporter->report(REPORT_ERROR, yyminor.location(*pinfo.fcontent), ss.str());
}

%parse_failure {
  TokenInfo ti;
  ti.start = pinfo.fcontent->start;
  ti.end = pinfo.fcontent->end;
  pinfo.reporter->report(REPORT_ERROR, ti.location(*pinfo.fcontent), "Parser was unable to proceed");
}
