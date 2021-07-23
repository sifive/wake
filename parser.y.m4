%include {#include <string.h>}
%include {#include "syntax.h"}
%include {#include "reporter.h"}
%include {#include <vector>}
%include {#include <sstream>}

%token_prefix TOKEN_
%token_type {TokenInfo}
%extra_argument {ParseInfo pinfo}

// The lexer also produces these (unused by parser):
%token WS COMMENT P_BOPEN P_BCLOSE P_SOPEN P_SCLOSE.

start ::= top.

top ::= top topdef.
top ::= .

topdef ::= KW_PACKAGE ID NL.
topdef ::= KW_FROM ID KW_IMPORT P_HOLE NL.
topdef ::= KW_FROM ID KW_EXPORT P_HOLE NL.
topdef ::= KW_FROM ID KW_IMPORT kind arity idopeqs NL.
topdef ::= KW_FROM ID KW_EXPORT kind arity idopeqs NL.

kind ::= KW_DEF.
kind ::= KW_TYPE.
kind ::= KW_TOPIC.
kind ::= .

arity ::= KW_UNARY.
arity ::= KW_BINARY.
arity ::= .

idopeqs ::= idopeqs idopeq.
idopeqs ::= idopeq.

idopeq ::= idop P_EQUALS idop.
idopeq ::= idop.

idop ::= ID.
idop ::= OP_DOT.
idop ::= OP_QUANT.
idop ::= OP_EXP.
idop ::= OP_MULDIV.
idop ::= OP_ADDSUB.
idop ::= OP_COMPARE.
idop ::= OP_INEQUAL.
idop ::= OP_AND.
idop ::= OP_OR.
idop ::= OP_DOLLAR.
idop ::= OP_LRARROW.
idop ::= OP_EQARROW.
idop ::= OP_COMMA.

flags ::= KW_GLOBAL KW_EXPORT.
flags ::= KW_GLOBAL.
flags ::= KW_EXPORT.
flags ::= .

topdef ::= flags KW_TOPIC ID P_COLON type NL.
topdef ::= KW_PUBLISH ID P_EQUALS block.

topdef ::= data.
data ::= flags KW_DATA type P_EQUALS type NL.
data ::= flags KW_DATA type P_EQUALS INDENT data_elts DEDENT.

data_elts ::= data_elts NL type.
data_elts ::= type.

topdef ::= tuple.
tuple ::= flags KW_TUPLE type P_EQUALS INDENT tuple_elts DEDENT.

tuple_elts ::= tuple_elt.
tuple_elts ::= tuple_elts NL tuple_elt.

tuple_elt ::= flags type.

topdef ::= flags KW_DEF pattern P_EQUALS block.
topdef ::= flags KW_TARGET pattern P_EQUALS block. // !!! slash

dnl Left-associative prEfix-heavy; 1 + + 4 + + 5 = (1 + (+4)) + (+5)
define(`LE',
$1_binary_$3 ::= $1_binary_$3 $2 $1_unary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative, prEfix-heavy; 1 $ $ 4 $ $ 5 = 1 $ (($4) $ ($5))
define(`RE',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative pOstfix-heavy; 1 , , 4 , , 5 = (1,) , ((4,) , 5)
define(`RO',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
$1_unary_$3 ::= $1_unary_$3 $2.
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

expression_term ::= expression_term OP_DOT expression_nodot.
expression_term ::= expression_nodot.

expression_nodot ::= ID.
expression_nodot ::= P_POPEN block_opt P_PCLOSE.

expression_nodot ::= P_HOLE.
expression_nodot ::= STR_RAW.
expression_nodot ::= STR_SINGLE.
expression_nodot ::= STR_OPEN interpolated_string STR_CLOSE.
interpolated_string ::= expression.
interpolated_string ::= interpolated_string STR_MID expression.

expression_nodot ::= REG_SINGLE.
expression_nodot ::= REG_OPEN interpolated_regexp REG_CLOSE.
interpolated_regexp ::= expression.
interpolated_regexp ::= interpolated_regexp REG_MID expression.

expression_nodot ::= DOUBLE.
expression_nodot ::= INTEGER.
expression_nodot ::= KW_HERE.

expression_binary_app ::= expression_binary_app expression_term.
expression_binary_app ::= expression_term.

expression_binary_app ::= KW_SUBSCRIBE ID.
expression_binary_app ::= KW_PRIM STR_SINGLE.
expression_binary_app ::= expression_match.

expression_match ::= KW_MATCH expression_term INDENT match_cases DEDENT.
//expression_match ::= KW_MATCH match_terms INDENT DEDENT.
//match_terms ::= expression_term expression_term. !!!
//match_terms ::= match_terms expression_term.

match_cases ::= match_cases NL match_case.
match_cases ::= match_case.
match_case ::= pattern guard P_EQUALS expression.
match_case ::= pattern guard P_EQUALS INDENT blockdefs expression DEDENT.
guard ::= .
guard ::= KW_IF expression.

expression ::= expression_binary_comma.
expression ::= P_BSLASH pattern_term expression.
expression ::= KW_IF block_opt KW_THEN block_opt KW_ELSE block_opt.

pattern ::= expression.
type ::= expression.
pattern_term ::= expression_term.

block_opt ::= expression.
block_opt ::= INDENT blockdefs expression DEDENT.

block ::= expression NL.
block ::= INDENT blockdefs expression DEDENT.

blockdefs ::= .
blockdefs ::= blockdefs blockdef.
blockdef ::= KW_FROM ID KW_IMPORT P_HOLE NL.
blockdef ::= KW_FROM ID KW_IMPORT kind arity idopeqs NL.
blockdef ::= KW_REQUIRE pattern P_EQUALS block.
blockdef ::= KW_REQUIRE pattern P_EQUALS block_opt KW_ELSE block.
blockdef ::= KW_DEF pattern P_EQUALS block.

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
