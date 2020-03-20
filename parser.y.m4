dnl Left-associative prEfix-heavy (prefix, operand, self, next)
define(`LE',
$1_binary_$3 ::= $1_binary_$3 $2 $1_unary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative, prEfix-heavy (prefix, operand, self, next)
define(`RE',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $2 $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
)

dnl Right-associative pOstfix-heavy
define(`RO',
$1_binary_$3 ::= $1_unary_$3 $2 $1_binary_$3.
$1_binary_$3 ::= $1_unary_$3.
$1_unary_$3 ::= $1_binary_$4.
$1_unary_$3 ::= $1_unary_$3 $2.
)

dnl All operators
define(`OPCHAIN',
RE($1, COMPOSE, compose, app)
LE($1, UNARYFN, unaryfn, compose)
RO($1, EXP,     exp,     unaryfn)
LE($1, MULDIV,  muldiv,  exp)
LE($1, ADDSUB,  addsub,  muldiv)
LE($1, COMPARE, compare, addsub)
RE($1, INEQUAL, inequal, compare)
LE($1, AND,     and,     inequal)
LE($1, OR,      or,      and)
RE($1, DOLLAR,  dollar,  or)
LE($1, LRARROW, lrarrow, dollar)
RE($1, EQARROW, eqarrow, lrarrow)
LE($1, QUANT,   quant,   eqarrow)
LE($1, COLON,   colon,   quant)
RO($1, COMMA,   comma,   colon)
)

%start_symbol top
top ::= block.

OPCHAIN(expression)

expression_term ::= expression_term DOT expression_nodot.
expression_term ::= expression_nodot.

expression_nodot ::= ID.
expression_nodot ::= LITERAL.
expression_nodot ::= HOLE.
expression_nodot ::= POPEN block PCLOSE.

expression_binary_app ::= expression_binary_app expression_term.
expression_binary_app ::= expression_term.
expression_binary_app ::= SUBSCRIBE ID.
expression_binary_app ::= PRIM ID.
expression_binary_app ::= expression_match.

expression_match ::= MATCH.

expression_full ::= expression_binary_comma.
expression_full ::= LAMBDA term_pattern expression_full.
expression_full ::= IF block THEN block ELSE block.

block ::= expression_full.
//block ::= INDENT EOL 
