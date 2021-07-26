%include {#include <string.h>}
%include {#include "syntax.h"}
%include {#include "reporter.h"}
%include {#include "cst.h"}
%include {#include <vector>}
%include {#include <sstream>}

%token_prefix TOKEN_
%token_type {TokenInfo}
%default_type {size_t}
%extra_argument {ParseInfo pinfo}
%stack_size {0}

// The lexer also produces these (unused by parser):
%token WS COMMENT P_BOPEN P_BCLOSE P_SOPEN P_SCLOSE.

start ::= top(T). { pinfo.cst->addNode(CST_TOP, T); }

top(R) ::= top(T) topdef. { R = T+1; }
top(R) ::= .              { R = 0; }

topdef ::= KW_PACKAGE(b) ID NL(e).                                     { pinfo.cst->addNode(CST_PACKAGE, b, 0, e); }
topdef ::= KW_FROM(b) ID KW_IMPORT P_HOLE NL(e).                       { pinfo.cst->addNode(CST_IMPORT,  b, 0, e); }
topdef ::= KW_FROM(b) ID KW_IMPORT kind(K) arity(A) idopeqs(I) NL(e).  { pinfo.cst->addNode(CST_IMPORT,  b, K+A+I, e); }
topdef ::= KW_FROM(b) ID KW_EXPORT kind(K) arity(A) idopeqs(I) NL(e).  { pinfo.cst->addNode(CST_EXPORT,  b, K+A+I, e); }

kind(R) ::= KW_DEF(k).   { R = 1; pinfo.cst->addNode(CST_KIND, k, 0, k); }
kind(R) ::= KW_TYPE(k).  { R = 1; pinfo.cst->addNode(CST_KIND, k, 0, k); }
kind(R) ::= KW_TOPIC(k). { R = 1; pinfo.cst->addNode(CST_KIND, k, 0, k); }
kind(R) ::= .            { R = 0; }

arity(R) ::= KW_UNARY(k).  { R = 1; pinfo.cst->addNode(CST_ARITY, k, 0, k); }
arity(R) ::= KW_BINARY(k). { R = 1; pinfo.cst->addNode(CST_ARITY, k, 0, k); }
arity(R) ::= .             { R = 0; }

idopeqs(R) ::= idopeqs(I) idopeq. { R = I+1; }
idopeqs(R) ::= idopeq.            { R = 1;   }

idopeq ::= idop P_EQUALS idop. { pinfo.cst->addNode(CST_IDEQ, 2); }
idopeq ::= idop.               { pinfo.cst->addNode(CST_IDEQ, 1); }

idop ::= ID(k).         { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_DOT(k).     { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_QUANT(k).   { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_EXP(k).     { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_MULDIV(k).  { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_ADDSUB(k).  { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_COMPARE(k). { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_INEQUAL(k). { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_AND(k).     { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_OR(k).      { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_DOLLAR(k).  { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_LRARROW(k). { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_EQARROW(k). { pinfo.cst->addNode(CST_ID, k, 0, k); }
idop ::= OP_COMMA(k).   { pinfo.cst->addNode(CST_ID, k, 0, k); }

global(R) ::= .             { R = 0; }
global(R) ::= KW_GLOBAL(k). { R = 1; pinfo.cst->addNode(CST_FLAG_GLOBAL, k, 0, k); }
export(R) ::= .             { R = 0; }
export(R) ::= KW_EXPORT(k). { R = 1; pinfo.cst->addNode(CST_FLAG_EXPORT, k, 0, k); }

topdef ::= global(G) export(E) KW_TOPIC(b) ID P_COLON type NL(e). { pinfo.cst->addNode(CST_TOPIC, b, G+E+1, e); }

topdef ::= KW_PUBLISH(b) ID P_EQUALS block_opt NL(e). { pinfo.cst->addNode(CST_PUBLISH, b, 1, e); }

topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS type NL(e).                       { pinfo.cst->addNode(CST_DATA, b, G+E+1, e); }
topdef ::= global(G) export(E) KW_DATA(b) type P_EQUALS INDENT data_elts(D) DEDENT NL(e). { pinfo.cst->addNode(CST_DATA, b, G+E+D, e); }

data_elts(R) ::= data_elts(E) NL type. { R = E+1; }
data_elts(R) ::= type.                 { R = 1;   }

topdef ::= global(G) export(E) KW_TUPLE(b) type P_EQUALS INDENT tuple_elts(T) DEDENT NL(e). { pinfo.cst->addNode(CST_TUPLE, b, G+E+1+T, e); }

tuple_elts(R) ::= tuple_elt.                  { R = 1; }
tuple_elts(R) ::= tuple_elts NL tuple_elt(E). { R = E+1; }

tuple_elt ::= global(G) export(E) type. { pinfo.cst->addNode(CST_TUPLE_ELT, G+E+1); }

topdef ::= global(G) export(E) KW_DEF(b)    pattern P_EQUALS block_opt NL(e). { pinfo.cst->addNode(CST_DEF,    b, G+E+2, e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern P_EQUALS block_opt NL(e). { pinfo.cst->addNode(CST_TARGET, b, G+E+2, e); }
topdef ::= global(G) export(E) KW_TARGET(b) pattern P_BSLASH pattern_terms(T) P_EQUALS block_opt NL(e). { pinfo.cst->addNode(CST_TARGET, b, G+E+1+T+1, e); }

dnl Left-associative prEfix-heavy; 1 + + 4 + + 5 = (1 + (+4)) + (+5)
define(`LE',
$1_binary_$3 ::= $1_binary_$3 $2 $1_unary_$3. { pinfo.cst->addNode(CST_BINARY, 2); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.               { pinfo.cst->addNode(CST_UNARY,  1); }
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative, prEfix-heavy; 1 $ $ 4 $ $ 5 = 1 $ (($4) $ ($5))
define(`RE',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3. { pinfo.cst->addNode(CST_BINARY, 2); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.               { pinfo.cst->addNode(CST_UNARY,  1); }
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative pOstfix-heavy; 1 , , 4 , , 5 = (1,) , ((4,) , 5)
define(`RO',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3. { pinfo.cst->addNode(CST_BINARY, 2); }
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
$1_unary_$3 ::= $1_unary_$3 $2.               { pinfo.cst->addNode(CST_UNARY,  1); }
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

expression_term ::= expression_term OP_DOT expression_nodot. { pinfo.cst->addNode(CST_BINARY, 2); }
expression_term ::= expression_nodot.

expression_nodot ::= ID(i).                            { pinfo.cst->addNode(CST_ID,    i, 0, i); }
expression_nodot ::= P_POPEN(b) block_opt P_PCLOSE(e). { pinfo.cst->addNode(CST_PAREN, b, 1, e); }

expression_nodot ::= P_HOLE(h).     { pinfo.cst->addNode(CST_HOLE,    h, 0, h); }
expression_nodot ::= STR_RAW(s).    { pinfo.cst->addNode(CST_LITERAL, s, 0, s); }
expression_nodot ::= STR_SINGLE(s). { pinfo.cst->addNode(CST_LITERAL, s, 0, s); }
expression_nodot ::= str_open interpolated_string(N) str_close. { pinfo.cst->addNode(CST_INTERPOLATE, 2+N); }

interpolated_string(R) ::= expression. { R = 1; }
interpolated_string(R) ::= interpolated_string(O) str_mid expression. { R = O+2; }

str_mid   ::= STR_MID(s).   { pinfo.cst->addNode(CST_LITERAL, s, 0, s); }
str_open  ::= STR_OPEN(s).  { pinfo.cst->addNode(CST_LITERAL, s, 0, s); }
str_close ::= STR_CLOSE(s). { pinfo.cst->addNode(CST_LITERAL, s, 0, s); }

expression_nodot ::= REG_SINGLE(r). { pinfo.cst->addNode(CST_LITERAL, r, 0, r); }
expression_nodot ::= reg_open interpolated_regexp(N) reg_close. { pinfo.cst->addNode(CST_INTERPOLATE, 2+N); }

interpolated_regexp(R) ::= expression. { R = 1; }
interpolated_regexp(R) ::= interpolated_regexp(O) reg_mid expression. { R = O+2; }

reg_mid   ::= REG_MID(r).   { pinfo.cst->addNode(CST_LITERAL, r, 0, r); }
reg_open  ::= REG_OPEN(r).  { pinfo.cst->addNode(CST_LITERAL, r, 0, r); }
reg_close ::= REG_CLOSE(r). { pinfo.cst->addNode(CST_LITERAL, r, 0, r); }

expression_nodot ::= DOUBLE(d).  { pinfo.cst->addNode(CST_LITERAL, d, 0, d); }
expression_nodot ::= INTEGER(i). { pinfo.cst->addNode(CST_LITERAL, i, 0, i); }
expression_nodot ::= KW_HERE(h). { pinfo.cst->addNode(CST_LITERAL, h, 0, h); }

expression_binary_app ::= expression_binary_app expression_term. { pinfo.cst->addNode(CST_APP, 2); }
expression_binary_app ::= expression_term.

expression_binary_app ::= KW_SUBSCRIBE(b) ID(e).    { pinfo.cst->addNode(CST_SUBSCRIBE, b, 0, e); }
expression_binary_app ::= KW_PRIM(b) STR_SINGLE(e). { pinfo.cst->addNode(CST_PRIM,      b, 0, e); }

expression_binary_app ::= KW_MATCH(b) expression_term INDENT match1_cases(C) DEDENT(e). { pinfo.cst->addNode(CST_MATCH, b, 1+C, e); }
expression_binary_app ::= KW_MATCH(b) match_terms(T)  INDENT matchx_cases(C) DEDENT(e). { pinfo.cst->addNode(CST_MATCH, b, T+C, e); }

match1_cases(R) ::= match1_cases(C) NL match1_case.  { R = C+1; }
match1_cases(R) ::= match1_case.                     { R = 1;   }
match1_case ::= pattern guard(G) P_EQUALS block_opt. { pinfo.cst->addNode(CST_CASE, 2+G); }

match_terms(R) ::= expression_term expression_term. { R = 2; }
match_terms(R) ::= match_terms(T) expression_term.  { R = 1+T; }

matchx_cases(R) ::= matchx_cases(C) NL matchx_case.           { R = C+1; }
matchx_cases(R) ::= matchx_case.                              { R = 1; }
matchx_case ::= pattern_terms(T) guard(G) P_EQUALS block_opt. { pinfo.cst->addNode(CST_CASE, T+G+1); }

pattern_terms(R) ::= pattern_terms(T) pattern_term. { R = 1+T; }
pattern_terms(R) ::= pattern_term.                  { R = 1; }

guard(R) ::= .                    { R = 0; }
guard(R) ::= KW_IF(b) expression. { R = 1; pinfo.cst->addNode(CST_GUARD, b, 1); }

expression ::= expression_binary_comma.
expression ::= P_BSLASH(b) pattern_term expression.                    { pinfo.cst->addNode(CST_LAMBDA, b, 2); }
expression ::= KW_IF(b) block_opt KW_THEN block_opt KW_ELSE block_opt. { pinfo.cst->addNode(CST_IF,     b, 3); }

pattern ::= expression.
type ::= expression.
pattern_term ::= expression_term.

block_opt ::= expression.
block_opt ::= INDENT(b) block DEDENT(e). { pinfo.cst->addNode(CST_PAREN, b, 1, e); }
block ::= blockdefs(N) body.             { pinfo.cst->addNode(CST_BLOCK, N+1); }
block ::= body.

body ::= expression.
body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt NL                      block. { pinfo.cst->addNode(CST_REQUIRE, b, 2); }
body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt NL KW_ELSE block_opt NL block. { pinfo.cst->addNode(CST_REQUIRE, b, 3); }
body ::= KW_REQUIRE(b) pattern P_EQUALS block_opt    KW_ELSE block_opt NL block. { pinfo.cst->addNode(CST_REQUIRE, b, 3); }

blockdefs(R) ::= blockdef.              { R = 1;   }
blockdefs(R) ::= blockdefs(D) blockdef. { R = 1+D; }

blockdef ::= KW_DEF(b) pattern P_EQUALS block_opt NL(e).                { pinfo.cst->addNode(CST_DEF,    b, 2, e); }
blockdef ::= KW_FROM(b) ID KW_IMPORT P_HOLE NL(e).                      { pinfo.cst->addNode(CST_IMPORT, b, 0, e); }
blockdef ::= KW_FROM(b) ID KW_IMPORT kind(K) arity(A) idopeqs(I) NL(e). { pinfo.cst->addNode(CST_IMPORT, b, K+A+I, e); }

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
  std::stringstream ss;
  ss << "syntax error; found ";
  if (token_len > 0) ss << yyminor << ", ";
  bool same =
    token_len == example_len &&
    memcmp(yyminor.start, example, example_len) == 0;
  if (!same || token_len == 0) ss << "a " << example << ", ";
  ss << "but was expecting one of:\n";
  for (int i = 1; i < YYNTOKEN; ++i) {
    int a = yy_find_shift_action((YYCODETYPE)i, yypParser->yytos->stateno);
    if (a < YYNSTATE + YYNRULE) {
      ss << "    " << symbolExample(i);
    }
  }
  pinfo.reporter->report(REPORT_ERROR, yyminor.location(*pinfo.fcontent), ss.str());
}
