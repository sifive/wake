#include "symbol.h"
#include "value.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <string>
#include <list>

const char *symbolTable[] = {
  "ERROR", "ID", "OPERATOR", "LITERAL", "DEF", "LAMBDA", "EQUALS", "POPEN", "PCLOSE", "END", "EOL", "INDENT", "DEDENT"
};

/*!max:re2c*/
static const size_t SIZE = 64 * 1024;

struct input_t {
  char buf[SIZE + YYMAXFILL];
  char *lim;
  char *cur;
  char *mar;
  char *tok;
  char *sol;
  int  row;
  bool eof;

  const char *filename;
  FILE *const file;

  input_t(const char *fn, FILE *f) : buf(), lim(buf + SIZE), cur(lim), mar(lim), tok(lim), sol(lim), row(1), eof(false), filename(fn), file(f) { }
  bool fill(size_t need);
};

bool input_t::fill(size_t need) {
  if (eof) {
    return false;
  }
  const size_t free = tok - buf;
  if (free < need) {
    return false;
  }
  memmove(buf, tok, lim - tok);
  lim -= free;
  cur -= free;
  mar -= free;
  tok -= free;
  sol -= free;
  lim += fread(lim, 1, free, file);
  if (lim < buf + SIZE) {
    eof = true;
    memset(lim, 0, YYMAXFILL);
    lim += YYMAXFILL;
  }
  return true;
}

/*!re2c re2c:define:YYCTYPE = "char"; */

template<int base>
static bool adddgt(unsigned long &u, unsigned long d)
{
  if (u > (ULONG_MAX - d) / base) {
      return false;
  }
  u = u * base + d;
  return true;
}

static bool lex_oct(const char *s, const char *e, unsigned long &u)
{
  for (u = 0, ++s; s < e; ++s) {
    if (!adddgt<8>(u, (unsigned)*s - 0x30u)) {
      return false;
    }
  }
  return true;
}

static bool lex_dec(const char *s, const char *e, unsigned long &u)
{
  for (u = 0; s < e; ++s) {
    if (!adddgt<10>(u, (unsigned)*s - 0x30u)) {
      return false;
    }
  }
  return true;
}

static bool lex_hex(const char *s, const char *e, unsigned long &u)
{
  for (u = 0, s += 2; s < e;) {
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYCURSOR = s;
      *     { if (!adddgt<16>(u, (unsigned)s[-1] - 0x30u))      return false; continue; }
      [a-f] { if (!adddgt<16>(u, (unsigned)s[-1] - 0x61u + 10)) return false; continue; }
      [A-F] { if (!adddgt<16>(u, (unsigned)s[-1] - 0x41u + 10)) return false; continue; }
  */
  }
  return true;
}

static bool lex_str(input_t &in, unsigned char q, std::string& result)
{
  for (unsigned long u = q;; result.push_back((char)u)) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;
        *                    { return false; }
        [^\n\\]              { u = in.tok[0]; if (u == q) break; continue; }
        "\\a"                { u = '\a'; continue; }
        "\\b"                { u = '\b'; continue; }
        "\\f"                { u = '\f'; continue; }
        "\\n"                { u = '\n'; continue; }
        "\\r"                { u = '\r'; continue; }
        "\\t"                { u = '\t'; continue; }
        "\\v"                { u = '\v'; continue; }
        "\\\\"               { u = '\\'; continue; }
        "\\'"                { u = '\''; continue; }
        "\\\""               { u = '"';  continue; }
        "\\?"                { u = '?';  continue; }
        "\\" [0-7]{1,3}      { if (!lex_oct(in.tok, in.cur, u)) return false; continue; }
        "\\u" [0-9a-fA-F]{4} { if (!lex_hex(in.tok, in.cur, u)) return false; continue; }
        "\\U" [0-9a-fA-F]{8} { if (!lex_hex(in.tok, in.cur, u)) return false; continue; }
        "\\x" [0-9a-fA-F]+   { if (!lex_hex(in.tok, in.cur, u)) return false; continue; }
    */
  }
  return true;
}

#define mkSym2(x, v) Symbol(x, Location(in.filename, start, Coordinates(in.row, in.cur - in.sol)), v)
#define mkSym(x) mkSym2(x, 0)

static Symbol lex_top(input_t &in) {
  Coordinates start;
top:
  start.row = in.row;
  start.column = in.cur - in.sol + 1;
  in.tok = in.cur;

  /*!re2c
      re2c:define:YYCURSOR = in.cur;
      re2c:define:YYMARKER = in.mar;
      re2c:define:YYLIMIT = in.lim;
      re2c:yyfill:enable = 1;
      re2c:define:YYFILL = "if (!in.fill(@@)) return mkSym(ERROR);";
      re2c:define:YYFILL:naked = 1;

      end = "\x00";

      *   { return mkSym(ERROR); }
      end { return mkSym((in.lim - in.tok == YYMAXFILL) ? END : ERROR); }

      // whitespace
      [ ]+              { goto top; }
      "#" [^\n]*        { goto top; }
      "\n" [ ]* / [#\n] { ++in.row; in.sol = in.tok+1; goto top; }
      "\n" [ ]*         { ++in.row; in.sol = in.tok+1; return mkSym(EOL); }

      // character and string literals
      ['"] {
        std::string out;
        bool ok = lex_str(in, in.cur[-1], out);
        return mkSym2(ok ? LITERAL : ERROR, new String(out));
      }

      // keywords
      "def" { return mkSym(DEF);    }
      "\\"  { return mkSym(LAMBDA); }
      "="   { return mkSym(EQUALS); }
      "("   { return mkSym(POPEN);  }
      ")"   { return mkSym(PCLOSE); }
      "_"   { return mkSym(DROP);   }

      [a-z][a-zA-Z0-9_]* { return mkSym(ID); }
      [$~]               { return mkSym(ID); }
      [.^*/%\-+<>=!&|,]+ { return mkSym(OPERATOR); }
   */

   // reserved punctuation: `@{}[]:;
   // reserved id space: capitalized
}

struct state_t {
  std::string filename;
  std::string location;
  std::list<int> tabs;
  int indent;

  state_t(const char *file) : filename(file), location(), tabs(), indent(0) {
    tabs.push_back(0);
  }
};

Lexer::Lexer(const char *file) : engine(new input_t(file, fopen(file, "r"))), state(new state_t(file)), next(), fail(false)  {
  if (engine->file) consume();
}

Lexer::~Lexer() {
  if (engine->file) fclose(engine->file);
}

std::string Lexer::text() {
  return std::string(engine->tok, engine->cur);
}

void Lexer::consume() {
  if (state->indent != state->tabs.back()) {
    if (state->indent > state->tabs.back()) {
      state->tabs.push_back(state->indent);
      next.type = INDENT;
    } else {
      state->tabs.pop_back();
      next.type = DEDENT;
    }
  } else {
    next = lex_top(*engine.get());
    if (next.type == EOL) state->indent = (engine->cur - engine->tok) - 1;
  }
}
