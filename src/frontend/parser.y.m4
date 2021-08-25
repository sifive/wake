/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

%stack_size {0}
%extra_argument {ParseInfo pinfo}

%token_prefix TOKEN_
%token_type {TokenInfo}

%default_type {size_t}
%default_destructor { pop($$); }
%destructor error { }

// The lexer also produces these (unused by parser):
%token WS COMMENT P_BOPEN P_BCLOSE P_SOPEN P_SCLOSE.

%include {
// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include "frontend/syntax.h"
#include "frontend/diagnostic.h"
#include "frontend/cst.h"
#include "frontend/file.h"
#include <vector>
#include <sstream>
#include <iostream>

#define add(t, ...) pinfo.cst->addNode((t), __VA_ARGS__)
#define pop(x) pinfo.cst->delNodes(x)

#define fail(sstream, token)                                                           \
  do {                                                                                 \
    std::stringstream sstr;                                                            \
    sstr << "syntax error; " << sstream;                                               \
    Location l = token.location(*pinfo.fcontent);                                      \
    pinfo.reporter->reportError(l, sstr.str());                                        \
  } while (0)
}

start(R) ::= top(T). { R = 1; add(CST_TOP, T); }

top(R) ::= .                 { R = 0;   }
top(R) ::= top(T) topdef(D). { R = T+D; }

id(R) ::= ID(k). { R = 1; add(CST_ID, k); }

topdef(R) ::=        error NL.     { R = 0; }
topdef(R) ::= INDENT error DEDENT. { R = 0; }

topdef(R) ::= KW_PACKAGE          NL(e). { R = 0; fail("keyword 'package' must be followed by a package name", e); }
topdef(R) ::= KW_PACKAGE    error NL.    { R = 0; }
topdef(R) ::= KW_PACKAGE(b) id(I) NL(e). { R = 1; add(CST_PACKAGE, b, I, e); }

topdef(R) ::= KW_FROM                NL(e). { R = 0; fail("keyword 'from' must be followed by a package name", e); }
topdef(R) ::= KW_FROM          error NL.    { R = 0; }
topdef(R) ::= KW_FROM(b) id(I)       NL(e). { R = 1; add(CST_IMPORT, b, I, e); fail("keyword 'from' must be followed by a package name and 'import' or 'export'", e); }
topdef(R) ::= KW_FROM(b) id(I) error NL(e). { R = 1; add(CST_IMPORT, b, I, e); }

topdef(R) ::= KW_FROM(b) id(I) KW_IMPORT P_HOLE                      NL(e).  { R = 1;           add(CST_IMPORT, b, I, e); }
topdef(R) ::= KW_FROM(b) id(I) KW_IMPORT kind(K) arity(A)            NL(e).  { R = 1; pop(K+A); add(CST_IMPORT, b, I, e); fail("package import must be followed by a list of identifiers or operators", e); }
topdef(R) ::= KW_FROM(b) id(I) KW_IMPORT kind(K) arity(A) idopeqs(E) NL(e).  { R = 1; add(CST_IMPORT, b, I+K+A+E, e); }

topdef(R) ::= KW_FROM    id(I) KW_EXPORT kind(K) arity(A)            NL(e).  { R = 0; pop(K+A+I); fail("package export must be followed by a list of identifiers or operators", e); }
topdef(R) ::= KW_FROM(b) id(I) KW_EXPORT kind(K) arity(A) idopeqs(E) NL(e).  { R = 1; add(CST_EXPORT, b, I+K+A+E, e); }

kind(R) ::= KW_DEF(k).   { R = 1; add(CST_KIND, k); }
kind(R) ::= KW_TYPE(k).  { R = 1; add(CST_KIND, k); }
kind(R) ::= KW_TOPIC(k). { R = 1; add(CST_KIND, k); }
kind(R) ::= .            { R = 0; }

arity(R) ::= KW_UNARY(k).  { R = 1; add(CST_ARITY, k); }
arity(R) ::= KW_BINARY(k). { R = 1; add(CST_ARITY, k); }
arity(R) ::= .             { R = 0; }

idopeqs(R) ::=            error.     { R = 0;   }
idopeqs(R) ::= idopeqs(I) error.     { R = I;   }
idopeqs(R) ::= idopeqs(I) idopeq(E). { R = I+E; }
idopeqs(R) ::=            idopeq(E). { R = E;   }

idopeq(R) ::= idop(A) P_EQUALS idop(B). { R = 1; add(CST_IDEQ, A+B); }
idopeq(R) ::= idop(B).                  { R = 1; add(CST_IDEQ, B); }

idop(R) ::= ID(k).         { R = 1; add(CST_ID, k); }
idop(R) ::= OP_DOT(k).     { R = 1; add(CST_OP, k); }
idop(R) ::= OP_QUANT(k).   { R = 1; add(CST_OP, k); }
idop(R) ::= OP_EXP(k).     { R = 1; add(CST_OP, k); }
idop(R) ::= OP_MULDIV(k).  { R = 1; add(CST_OP, k); }
idop(R) ::= OP_ADDSUB(k).  { R = 1; add(CST_OP, k); }
idop(R) ::= OP_COMPARE(k). { R = 1; add(CST_OP, k); }
idop(R) ::= OP_INEQUAL(k). { R = 1; add(CST_OP, k); }
idop(R) ::= OP_AND(k).     { R = 1; add(CST_OP, k); }
idop(R) ::= OP_OR(k).      { R = 1; add(CST_OP, k); }
idop(R) ::= OP_DOLLAR(k).  { R = 1; add(CST_OP, k); }
idop(R) ::= OP_LRARROW(k). { R = 1; add(CST_OP, k); }
idop(R) ::= OP_EQARROW(k). { R = 1; add(CST_OP, k); }
idop(R) ::= OP_COMMA(k).   { R = 1; add(CST_OP, k); }

global(R) ::= .             { R = 0; }
global(R) ::= KW_GLOBAL(k). { R = 1; add(CST_FLAG_GLOBAL, k); }
export(R) ::= .             { R = 0; }
export(R) ::= KW_EXPORT(k). { R = 1; add(CST_FLAG_EXPORT, k); }
 
topdef(R) ::= global(G) export(E) KW_TOPIC                           NL(e). { R = 0; pop(G+E); fail("keyword 'topic' must be followed by a topic name", e); }
topdef(R) ::= global(G) export(E) KW_TOPIC                     error NL.    { R = 0; pop(G+E); }
topdef(R) ::= global(G) export(E) KW_TOPIC(b) id(I)                  NL(e). { R = 1; add(CST_ERROR, e); add(CST_TOPIC, b, G+E+I+1, e); fail("topics must be followed by an ': type'", e); }
topdef(R) ::= global(G) export(E) KW_TOPIC(b) id(I)            error NL(e). { R = 1; add(CST_ERROR, e); add(CST_TOPIC, b, G+E+I+1, e); }
topdef(R) ::= global(G) export(E) KW_TOPIC(b) id(I) P_COLON          NL(e). { R = 1; add(CST_ERROR, e); add(CST_TOPIC, b, G+E+I+1, e); fail("topics must be followed by an ': type'", e); }
topdef(R) ::= global(G) export(E) KW_TOPIC(b) id(I) P_COLON type(T)  NL(e). { R = 1;                    add(CST_TOPIC, b, G+E+I+T, e); }

topdef(R) ::= KW_PUBLISH                                NL(e). { R = 0; fail("keyword 'publish' must be followed by a topic name", e); }
topdef(R) ::= KW_PUBLISH                          error NL.    { R = 0; }
topdef(R) ::= KW_PUBLISH    id(I)                       NL(e). { R = 0; pop(I); fail("publishes must be followed by an '= expression'", e); }
topdef(R) ::= KW_PUBLISH    id(I)                 error NL.    { R = 0; pop(I); }
topdef(R) ::= KW_PUBLISH    id(I) P_EQUALS              NL(e). { R = 0; pop(I); fail("publishes must be followed by an '= expression'", e); }
topdef(R) ::= KW_PUBLISH(b) id(I) P_EQUALS block_opt(B) NL(e). { R = 1; add(CST_PUBLISH, b, I+B, e); }

topdef(R) ::= global(G) export(E) KW_DATA                              NL(e). { R = 0; pop(G+E); fail("keyword 'data' must be followed by a type expression", e); }
topdef(R) ::= global(G) export(E) KW_DATA                        error NL.    { R = 0; pop(G+E); }
topdef(R) ::= global(G) export(E) KW_DATA(b) type(T)                   NL(e). { R = 1; add(CST_DATA, b, G+E+T, e); fail("data types must be followed by an '= constructor-cases+'", e); }
topdef(R) ::= global(G) export(E) KW_DATA(b) type(T)             error NL(e). { R = 1; add(CST_DATA, b, G+E+T, e); }
topdef(R) ::= global(G) export(E) KW_DATA(b) type(T) P_EQUALS          NL(e). { R = 1; add(CST_DATA, b, G+E+T, e); fail("data types must be followed by an '= constructor-cases+'", e); }
topdef(R) ::= global(G) export(E) KW_DATA(b) type(T) P_EQUALS type(U)  NL(e). { R = 1; add(CST_DATA, b, G+E+T+U, e); }
topdef(R) ::= global(G) export(E) KW_DATA(b) type(T) P_EQUALS INDENT data_elts(D) DEDENT(e). { R = 1; add(CST_DATA, b, G+E+T+D, e); }

data_elts(R) ::= error.                    { R = 0;   }
data_elts(R) ::= type(T).                  { R = T;   }
data_elts(R) ::= data_elts(E) NL error.    { R = E;   }
data_elts(R) ::= data_elts(E) NL type(T).  { R = E+T; }

topdef(R) ::= global(G) export(E) KW_TUPLE                           NL(e). { R = 0; pop(G+E); fail("keyword 'tuple' must be followed by a type expression", e); }
topdef(R) ::= global(G) export(E) KW_TUPLE                     error NL.    { R = 0; pop(G+E); }
topdef(R) ::= global(G) export(E) KW_TUPLE(b) type(T)                NL(e). { R = 1; add(CST_TUPLE, b, G+E+T, e); fail("tuple types must be followed by an '= member-definitions+'", e); }
topdef(R) ::= global(G) export(E) KW_TUPLE(b) type(T)          error NL(e). { R = 1; add(CST_TUPLE, b, G+E+T, e); }
topdef(R) ::= global(G) export(E) KW_TUPLE(b) type(T) P_EQUALS       NL(e). { R = 1; add(CST_TUPLE, b, G+E+T, e); fail("tuple types must be followed by an '= member-definitions+'", e); }
topdef(R) ::= global(G) export(E) KW_TUPLE(b) type(T) P_EQUALS error NL(e). { R = 1; add(CST_TUPLE, b, G+E+T, e); }
topdef(R) ::= global(G) export(E) KW_TUPLE(b) type(T) P_EQUALS INDENT tuple_elts(U) DEDENT(e). { R = 1; add(CST_TUPLE, b, G+E+T+U, e); }

tuple_elts(R) ::= error.                         { R = 0;   }
tuple_elts(R) ::= tuple_elt(T).                  { R = T;   }
tuple_elts(R) ::= tuple_elts(E) NL error.        { R = E;   }
tuple_elts(R) ::= tuple_elts(E) NL tuple_elt(T). { R = E+T; }

tuple_elt(R) ::= global(G) export(E) type(T). { R = 1; add(CST_TUPLE_ELT, G+E+T); }

topdef(R) ::= global(G) export(E) KW_DEF                                     NL(e). { R = 0; pop(G+E); fail("keyword 'def' must be followed by a pattern", e); }
topdef(R) ::= global(G) export(E) KW_DEF                               error NL.    { R = 0; pop(G+E); }
topdef(R) ::= global(G) export(E) KW_DEF(b) pattern(P)                       NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, G+E+P+1, e); fail("definitions must be followed by an '= expression'", e); }
topdef(R) ::= global(G) export(E) KW_DEF(b) pattern(P)                 error NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, G+E+P+1, e); }
topdef(R) ::= global(G) export(E) KW_DEF(b) pattern(P) P_EQUALS              NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, G+E+P+1, e); fail("definitions must be followed by an '= expression'", e); }
topdef(R) ::= global(G) export(E) KW_DEF(b) pattern(P) P_EQUALS block_opt(B) NL(e). { R = 1;                    add(CST_DEF, b, G+E+P+B, e); }

topdef(R) ::= global(G) export(E) KW_TARGET                                            NL(e). { R = 0; pop(G+E); fail("keyword 'target' must be followed by a pattern", e); }
topdef(R) ::= global(G) export(E) KW_TARGET                                      error NL.    { R = 0; pop(G+E); }
topdef(R) ::= global(G) export(E) KW_TARGET(b) target_pattern(P)                       NL(e). { R = 1; add(CST_ERROR, e); add(CST_TARGET, b, G+E+P+1, e); fail("targets must be followed by an '= expression'", e); }
topdef(R) ::= global(G) export(E) KW_TARGET(b) target_pattern(P)                 error NL(e). { R = 1; add(CST_ERROR, e); add(CST_TARGET, b, G+E+P+1, e); }
topdef(R) ::= global(G) export(E) KW_TARGET(b) target_pattern(P) P_EQUALS              NL(e). { R = 1; add(CST_ERROR, e); add(CST_TARGET, b, G+E+P+1, e); fail("targets must be followed by an '= expression'", e); }
topdef(R) ::= global(G) export(E) KW_TARGET(b) target_pattern(P) P_EQUALS block_opt(B) NL(e). { R = 1; add(CST_TARGET, b, G+E+P+B, e); }

target_pattern(R) ::= pattern(P).                          { R = P;   }
target_pattern(R) ::= pattern(P) P_BSLASH target_args(G). { R = P+G; }

target_args(R) ::= pattern_terms(T). { R = 1; add(CST_TARGET_ARGS, T); }

dnl Left-associative prEfix-heavy; 1 + + 4 + + 5 = (1 + (+4)) + (+5)
define(`LE',
$1_op_$3(R) ::= $2(k).                                          { R = 1; add(CST_OP, k); }
$1_binary_$3(R) ::= $1_binary_$3(A) $1_op_$3(O) $1_unary_$3(B). { R = 1; add(CST_BINARY, A+O+B); }
$1_binary_$3(R) ::= $1_unary_$3(U).                             { R = U; }
$1_unary_$3(R) ::= $1_op_$3(O) $1_unary_$3(B).                  { R = 1; add(CST_UNARY,  O+B); }
$1_unary_$3(R) ::= $1_binary_$4(B).                             { R = B; }
)

dnl Right-associative, prEfix-heavy; 1 $ $ 4 $ $ 5 = 1 $ (($4) $ ($5))
define(`RE',
$1_op_$3(R) ::= $2(k).                                          { R = 1; add(CST_OP, k); }
$1_binary_$3(R) ::= $1_unary_$3(A) $1_op_$3(O) $1_binary_$3(B). { R = 1; add(CST_BINARY, A+O+B); }
$1_binary_$3(R) ::= $1_unary_$3(U).                             { R = U; }
$1_unary_$3(R) ::= $1_op_$3(O) $1_unary_$3(B).                  { R = 1; add(CST_UNARY,  O+B); }
$1_unary_$3(R) ::= $1_binary_$4(B).                             { R = B; }
)

dnl Right-associative pOstfix-heavy; 1 , , 4 , , 5 = (1,) , ((4,) , 5)
define(`RO',
$1_op_$3(R) ::= $2(k).                                          { R = 1; add(CST_OP, k); }
$1_binary_$3(R) ::= $1_unary_$3(A) $1_op_$3(O) $1_binary_$3(B). { R = 1; add(CST_BINARY, A+O+B); }
$1_binary_$3(R) ::= $1_unary_$3(U).                             { R = U; }
$1_unary_$3(R) ::= $1_binary_$4(B).                             { R = B; }
$1_unary_$3(R) ::= $1_unary_$3(A) $1_op_$3(O).                  { R = 1; add(CST_UNARY,  A+O); }
)

dnl All operators
define(`OPCHAIN',
LE($1, OP_QUANT,   quant,   app)         dnl x ∑ ∑ y ∑ ∑ z       x ∑ ((∑y) ∑ (∑z))
RE($1, OP_EXP,     exp,     quant)       dnl x ^ ^ y ^ ^ z       x ^ ((^y) ^ (^z))
LE($1, OP_MULDIV,  muldiv,  exp)         dnl x * * y * * z       (x * (*y)) * (*z)
LE($1, OP_ADDSUB,  addsub,  muldiv)      dnl 4 + - 5 - - 7       (4 + (-5)) - (-7)
LE($1, OP_COMPARE, compare, addsub)      dnl 4 < < 5 < < 7       (4 < (<5)) < (<7)
RE($1, OP_INEQUAL, inequal, compare)     dnl x => !y => !z       x => ((!y) => (!z))
LE($1, OP_AND,     and,     inequal)     dnl x & & y & & z       (x & (&y)) & (&z)
LE($1, OP_OR,      or,      and)         dnl x | | y | | z       (x | (|y)) | (|z)
RE($1, OP_DOLLAR,  dollar,  or)          dnl x $ $ y $ $ z       x $ (($y) $ ($z))
RO($1, P_COLON,    colon,   dollar)      dnl x : : y : : z       (x:): ((y:): z)
LE($1, OP_LRARROW, lrarrow, colon)       dnl as compare
RE($1, OP_EQARROW, eqarrow, lrarrow)     dnl as inequal
RO($1, OP_COMMA,   comma,   eqarrow)     dnl x , , y , , z       (x,), ((y,), z)
)

OPCHAIN(expression)

op_dot(R) ::= OP_DOT(k). { R = 1; add(CST_OP, k); }

expression_term(R) ::= expression_term(A) op_dot(O) expression_nodot(B). { R = 1; add(CST_BINARY, A+O+B); }
expression_term(R) ::= expression_nodot(N). { R = N; }

expression_nodot(R) ::= ID(i). { R = 1; add(CST_ID, i); }

expression_nodot(R) ::= P_POPEN(b) block_opt(B) P_PCLOSE(e). { R = 1; add(CST_PAREN, b, B, e); }

expression_nodot(R) ::= P_HOLE(h).     { R = 1; add(CST_HOLE, h); }
expression_nodot(R) ::= STR_RAW(s).    { R = 1; add(CST_LITERAL, s); }
expression_nodot(R) ::= STR_SINGLE(s). { R = 1; add(CST_LITERAL, s); }

expression_nodot(R) ::= str_open(A) interpolated_string(I) str_close(B). { R = 1; add(CST_INTERPOLATE, A+I+B); }

interpolated_string(R) ::= expression(B). { R = B; }
interpolated_string(R) ::= interpolated_string(O) str_mid(M) expression(B). { R = O+M+B; }

str_mid(R)   ::= STR_MID(s).   { R = 1; add(CST_LITERAL, s); }
str_open(R)  ::= STR_OPEN(s).  { R = 1; add(CST_LITERAL, s); }
str_close(R) ::= STR_CLOSE(s). { R = 1; add(CST_LITERAL, s); }

expression_nodot(R) ::= mstr_single(S). { R = S; }
expression_nodot(R) ::= mstr_open(A) interpolated_mstring(I) mstr_close(B). { R = 1; add(CST_INTERPOLATE, A+I+B); }

interpolated_mstring(R) ::= expression(B). { R = B; }
interpolated_mstring(R) ::= interpolated_mstring(O) mstr_mid(M) expression(B). { R = O+M+B; }

mstr_cont(R)  ::= .                        { R = 0; }
mstr_cont(R)  ::= mstr_cont MSTR_CONTINUE. { R = 0; }
mstr_cont(R)  ::= mstr_cont NL.            { R = 0; }

mstr_single(R) ::= MSTR_BEGIN(b)  mstr_cont MSTR_END(e).   { R = 1; add(CST_LITERAL, b, 0, e); }
mstr_open(R)   ::= MSTR_BEGIN(b)  mstr_cont MSTR_PAUSE(e). { R = 1; add(CST_LITERAL, b, 0, e); }
mstr_mid(R)    ::= MSTR_MID(b).                            { R = 1; add(CST_LITERAL, b);       }
mstr_mid(R)    ::= MSTR_RESUME(b) mstr_cont MSTR_PAUSE(e). { R = 1; add(CST_LITERAL, b, 0, e); }
mstr_close(R)  ::= MSTR_RESUME(b) mstr_cont MSTR_END(e).   { R = 1; add(CST_LITERAL, b, 0, e); }

expression_nodot(R) ::= lstr_single(S). { R = S; }
expression_nodot(R) ::= lstr_open(A) interpolated_lstring(I) lstr_close(B). { R = 1; add(CST_INTERPOLATE, A+I+B); }

interpolated_lstring(R) ::= expression(B). { R = B; }
interpolated_lstring(R) ::= interpolated_lstring(O) lstr_mid(M) expression(B). { R = O+M+B; }

lstr_cont(R)  ::= .                        { R = 0; }
lstr_cont(R)  ::= lstr_cont LSTR_CONTINUE. { R = 0; }
lstr_cont(R)  ::= lstr_cont NL.            { R = 0; }

lstr_single(R) ::= LSTR_BEGIN(b)  lstr_cont LSTR_END(e).   { R = 1; add(CST_LITERAL, b, 0, e); }
lstr_open(R)   ::= LSTR_BEGIN(b)  lstr_cont LSTR_PAUSE(e). { R = 1; add(CST_LITERAL, b, 0, e); }
lstr_mid(R)    ::= LSTR_MID(b).                            { R = 1; add(CST_LITERAL, b);       }
lstr_mid(R)    ::= LSTR_RESUME(b) lstr_cont LSTR_PAUSE(e). { R = 1; add(CST_LITERAL, b, 0, e); }
lstr_close(R)  ::= LSTR_RESUME(b) lstr_cont LSTR_END(e).   { R = 1; add(CST_LITERAL, b, 0, e); }

expression_nodot(R) ::= REG_SINGLE(r). { R = 1; add(CST_LITERAL, r); }
expression_nodot(R) ::= reg_open(A) interpolated_regexp(I) reg_close(B). { R = 1; add(CST_INTERPOLATE, A+I+B); }

interpolated_regexp(R) ::= expression(B). { R = B; }
interpolated_regexp(R) ::= interpolated_regexp(O) reg_mid(M) expression(B). { R = O+M+B; }

reg_mid(R)   ::= REG_MID(r).   { R = 1; add(CST_LITERAL, r); }
reg_open(R)  ::= REG_OPEN(r).  { R = 1; add(CST_LITERAL, r); }
reg_close(R) ::= REG_CLOSE(r). { R = 1; add(CST_LITERAL, r); }

expression_nodot(R) ::= DOUBLE(d).  { R = 1; add(CST_LITERAL, d); }
expression_nodot(R) ::= INTEGER(i). { R = 1; add(CST_LITERAL, i); }
expression_nodot(R) ::= KW_HERE(h). { R = 1; add(CST_LITERAL, h); }

expression_binary_app(R) ::= expression_binary_app(A) expression_term(B). { R = 1; add(CST_APP, A+B); }
expression_binary_app(R) ::= expression_term(A). { R = A; }

expression_binary_app(R) ::= KW_SUBSCRIBE(b) id(I). { R = 1; add(CST_SUBSCRIBE, b, I); }

expression_binary_app(R) ::= KW_PRIM(b) prim_literal(L). { R = 1; add(CST_PRIM, b, L); }

prim_literal(R) ::= STR_SINGLE(s). { R = 1; add(CST_LITERAL, s); }

expression_binary_app(R) ::= KW_MATCH(b) expression_term(T) INDENT match1_cases(C) DEDENT. { R = 1; add(CST_MATCH, b, T+C); }
expression_binary_app(R) ::= KW_MATCH(b) match_terms(T)     INDENT matchx_cases(C) DEDENT. { R = 1; add(CST_MATCH, b, T+C); }

match1_cases(R) ::= match1_cases(C) NL match1_case(B). { R = C+B; }
match1_cases(R) ::= match1_case(B).                    { R = B;   }
match1_case(R) ::= pattern(P) guard(G) block_opt(B).   { R = 1; add(CST_CASE, P+G+B); }

match_terms(R) ::= expression_term(E) expression_term(F). { R = E+F; }
match_terms(R) ::= match_terms(T) expression_term(E).     { R = T+E; }

matchx_cases(R) ::= matchx_cases(C) NL matchx_case(B). { R = C+B; }
matchx_cases(R) ::= matchx_case(B).                    { R = B; }
matchx_case(R) ::= pattern_terms(T) guard(G) block_opt(B). { R = 1; add(CST_CASE, T+G+B); }

pattern_terms(R) ::= pattern_terms(T) pattern_term(B). { R = T+B; }
pattern_terms(R) ::= pattern_term(B).                  { R = B; }

guard(R) ::=                        P_EQUALS(e). { R = 1; add(CST_GUARD, e); }
guard(R) ::= KW_IF(b) expression(E) P_EQUALS(e). { R = 1; add(CST_GUARD, b, E, e); }

expression(R) ::= expression_binary_comma(B). { R = B; }

expression(R) ::= P_BSLASH(b) pattern_term(P) expression(E). { R = 1; add(CST_LAMBDA, b, P+E); }

expression(R) ::= KW_IF(b) block_opt(I) KW_THEN block_opt(T) KW_ELSE block_opt(E). { R = 1; add(CST_IF, b, I+T+E); }

pattern(R)      ::= expression(E).      { R = E; }
type(R)         ::= expression(E).      { R = E; }
pattern_term(R) ::= expression_term(E). { R = E; }

block_opt(R) ::= expression(E).   { R = E; }
block_opt(R) ::= INDENT block(B). { R = B; } // we defer the DEDENT to the body which needs a location

block(R) ::=              body(B). { R = B; }
block(R) ::= blockdefs(N) body(B). { R = 1; add(CST_BLOCK, N+B); }

body(R) ::= error         DEDENT(e). { R = 1; add(CST_ERROR, e); }
body(R) ::= expression(E) DEDENT.    { R = E; }

body(R) ::= KW_REQUIRE    reqbad                           block(B). { R = B; }
body(R) ::= KW_REQUIRE(b) pattern(P) reqbody(Q) reqelse(E) block(B). { R = 1; add(CST_REQUIRE, b, P+Q+E+B); }

// This ensures we pop reqelse before taking the following block
reqbad(R) ::=       reqelse. { R = 0; fail("keyword 'require' must be followed by a pattern", yyLookaheadToken); }
reqbad(R) ::= error reqelse. { R = 0; }

reqbody(R) ::= .                      { R = 1; add(CST_ERROR, yyLookaheadToken); fail("requirements must be followed by an '= expression", yyLookaheadToken); }
reqbody(R) ::= error.                 { R = 1; add(CST_ERROR, yyLookaheadToken); }
reqbody(R) ::= P_EQUALS.              { R = 1; add(CST_ERROR, yyLookaheadToken); fail("requirements must be followed by an '= expression", yyLookaheadToken); }
reqbody(R) ::= P_EQUALS block_opt(B). { R = B; }

// Even though NL is legal to discard everywhere, we need to explicitly allow it here.
// Otherwise, the NL might be taken to be the end of require, prohibiting else.
reqelse(R) ::= NL KW_ELSE reqelsebody(B). { R = 1; add(CST_REQ_ELSE, B); }
reqelse(R) ::=    KW_ELSE reqelsebody(B). { R = 1; add(CST_REQ_ELSE, B); }
reqelse(R) ::= NL.                        { R = 0; }

reqelsebody(R) ::=              NL(e). { R = 1; add(CST_ERROR, e); fail("keyword 'else' must be followed by an expression", e); }
reqelsebody(R) ::= error        NL(e). { R = 1; add(CST_ERROR, e); }
reqelsebody(R) ::= block_opt(B) NL.    { R = B; }

blockdefs(R) ::= blockdef(B).              { R = B;   }
blockdefs(R) ::= blockdefs(D) blockdef(B). { R = D+B; }

blockdef(R) ::=        error NL.     { R = 0; }
blockdef(R) ::= INDENT error DEDENT. { R = 0; }

blockdef(R) ::= KW_DEF                                     NL(e). { R = 0; fail("keyword 'def' must be followed by a pattern", e); }
blockdef(R) ::= KW_DEF                               error NL.    { R = 0; }
blockdef(R) ::= KW_DEF(b) pattern(P)                       NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, P+1, e); fail("definitions must be followed by an '= expression'", e); }
blockdef(R) ::= KW_DEF(b) pattern(P)                 error NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, P+1, e); }
blockdef(R) ::= KW_DEF(b) pattern(P) P_EQUALS              NL(e). { R = 1; add(CST_ERROR, e); add(CST_DEF, b, P+1, e); fail("definitions must be followed by an '= expression'", e); }
blockdef(R) ::= KW_DEF(b) pattern(P) P_EQUALS block_opt(B) NL(e). { R = 1;                    add(CST_DEF, b, P+B, e); }

blockdef(R) ::= KW_FROM                                                NL(e). { R = 0; fail("keyword 'from' must be followed by a package name", e); }
blockdef(R) ::= KW_FROM                                          error NL.    { R = 0; }
blockdef(R) ::= KW_FROM(b) id(I)                                       NL(e). { R = 1;           add(CST_IMPORT, b, I, e); fail("keyword 'from' must be followed by a package name and 'import'", e); }
blockdef(R) ::= KW_FROM(b) id(I)                                 error NL(e). { R = 1;           add(CST_IMPORT, b, I, e); }
blockdef(R) ::= KW_FROM(b) id(I) KW_IMPORT P_HOLE                      NL(e). { R = 1;           add(CST_IMPORT, b, I, e); }
blockdef(R) ::= KW_FROM(b) id(I) KW_IMPORT kind(K) arity(A)            NL(e). { R = 1; pop(K+A); add(CST_IMPORT, b, I, e); fail("package import must be followed by a list of identifiers or operators", e); }
blockdef(R) ::= KW_FROM(b) id(I) KW_IMPORT kind(K) arity(A) idopeqs(E) NL(e). { R = 1; add(CST_IMPORT, b, I+K+A+E, e); }

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
    if (ParseShifts(yypParser, i)) {
      ++num;
      tokens << "    " << symbolExample(i);
    }
  }
  if (num) {
    ss << tokens.rdbuf();
  } else {
    ss << "which is inappropriate here";
  }
  pinfo.reporter->reportError(yyminor.location(*pinfo.fcontent), ss.str());
}

%parse_failure {
  TokenInfo ti;
  ti.start = pinfo.fcontent->start;
  ti.end = pinfo.fcontent->end;
  pinfo.reporter->reportError(ti.location(*pinfo.fcontent), "Parser was unable to proceed");
}
