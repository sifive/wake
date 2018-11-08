#include "symbol.h"
#include "value.h"
#include "expr.h"
#include "parser.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>

const char *symbolTable[] = {
  "ERROR", "ID", "OPERATOR", "LITERAL", "DEF", "GLOBAL", "PUBLISH", "SUBSCRIBE", "PRIM", "LAMBDA",
  "EQUALS", "POPEN", "PCLOSE", "BOPEN", "BCLOSE", "IF", "THEN", "ELSE", "HERE", "MEMOIZE", "END",
  "EOL", "INDENT", "DEDENT"
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

  input_t(const char *fn, FILE *f, int start = SIZE, int end = SIZE)
   : buf(), lim(buf + end), cur(buf + start), mar(buf + start), tok(buf + start), sol(buf + start),
     row(1), eof(false), filename(fn), file(f) { }
  bool fill(size_t need);
};

#define SYM_LOCATION Location(in.filename, start, Coordinates(in.row, in.cur - in.sol))

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

static uint32_t lex_oct(const char *s, const char *e)
{
  uint32_t u = 0;
  for (++s; s < e; ++s) u = u*8 + *s - '0';
  return u;
}

static uint32_t lex_hex(const char *s, const char *e)
{
  uint32_t u = 0;
  for (s += 2; s < e;) {
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYCURSOR = s;
      *     { u = u*16 + s[-1] - '0' +  0; continue; }
      [a-f] { u = u*16 + s[-1] - 'a' + 10; continue; }
      [A-F] { u = u*16 + s[-1] - 'A' + 10; continue; }
  */
  }
  return u;
}

enum
{
        Bit1    = 7,
        Bitx    = 6,
        Bit2    = 5,
        Bit3    = 4,
        Bit4    = 3,
        Bit5    = 2,

        T1      = ((1<<(Bit1+1))-1) ^ 0xFF,     /* 0000 0000 */
        Tx      = ((1<<(Bitx+1))-1) ^ 0xFF,     /* 1000 0000 */
        T2      = ((1<<(Bit2+1))-1) ^ 0xFF,     /* 1100 0000 */
        T3      = ((1<<(Bit3+1))-1) ^ 0xFF,     /* 1110 0000 */
        T4      = ((1<<(Bit4+1))-1) ^ 0xFF,     /* 1111 0000 */
        T5      = ((1<<(Bit5+1))-1) ^ 0xFF,     /* 1111 1000 */

        Rune1   = (1<<(Bit1+0*Bitx))-1,         /*                     0111 1111 */
        Rune2   = (1<<(Bit2+1*Bitx))-1,         /*                0111 1111 1111 */
        Rune3   = (1<<(Bit3+2*Bitx))-1,         /*           1111 1111 1111 1111 */
        Rune4   = (1<<(Bit4+3*Bitx))-1,         /* 0001 1111 1111 1111 1111 1111 */

        Maskx   = (1<<Bitx)-1,                  /* 0011 1111 */
        Testx   = Maskx ^ 0xFF                  /* 1100 0000 */
};

static char push_utf8(std::string &result, uint32_t c)
{
  if (c <= Rune1) {
    return static_cast<char>(c);
  } else if (c <= Rune2) {
    result.push_back(T2 | static_cast<char>(c >> 1*Bitx));
    return Tx | (c & Maskx);
  } else if (c <= Rune3) {
    result.push_back(T3 | static_cast<char>(c >> 2*Bitx));
    result.push_back(Tx | ((c >> 1*Bitx) & Maskx));
    return Tx | (c & Maskx);
  } else if (c <= Rune4) {
    result.push_back(T4 | static_cast<char>(c >> 3*Bitx));
    result.push_back(Tx | ((c >> 2*Bitx) & Maskx));
    result.push_back(Tx | ((c >> 1*Bitx) & Maskx));
    return  Tx | (c & Maskx);
  } else {
    return static_cast<char>(0xff);
  }
}

static int utf8_size(const char *str)
{
  uint8_t c = str[0];
  if (c < Tx) return 1;
  if (c < T2) return -1;
  if ((static_cast<uint8_t>(str[1]) & Testx) != Tx) return -1;
  if (c < T3) return 2;
  if ((static_cast<uint8_t>(str[2]) & Testx) != Tx) return -1;
  if (c < T4) return 3;
  if ((static_cast<uint8_t>(str[3]) & Testx) != Tx) return -1;
  if (c < T5) return 4;
  return -1;
}

static bool is_unicode(const std::string &str)
{
  int code;
  for (const char *c = str.c_str(); *c; c += code)
    if ((code = utf8_size(c)) == -1)
      return false;
  return true;
}

static bool lex_sstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start(in.row, in.cur - in.sol);
  std::string slice;

  for (char u = 0;; slice.push_back(u)) {
    if (u == static_cast<char>(0xff)) return false;
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;
        *                    { return false; }
        "'"                  { break; }
        [^]                  { u = in.tok[0]; continue; }
    */
  }

  bool ok = is_unicode(slice);
  std::shared_ptr<String> str = std::make_shared<String>(std::move(slice));
  out = new Literal(SYM_LOCATION, std::move(str));
  return ok;
}

static bool lex_dstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start(in.row, in.cur - in.sol);
  std::vector<Expr*> exprs;
  std::string slice;
  bool ok = true;

  for (char u = 0;; slice.push_back(u)) {
    if (u == static_cast<char>(0xff)) return false;
top:
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;

        * { return false; }
        [{] {
          ok &= is_unicode(slice);
          std::shared_ptr<String> str = std::make_shared<String>(std::move(slice));
          exprs.push_back(new Literal(SYM_LOCATION, std::move(str)));
          lex.consume();
          exprs.push_back(parse_block(lex));
          ok &= expect(BCLOSE, lex);
          start.row = in.row;
          start.column = in.cur - in.sol;
          goto top;
        }

        ["]                  { break; }
        [^\n\\]              { u = in.tok[0]; continue; }
        "\\{"                { u = '{';  continue; }
        "\\}"                { u = '}';  continue; }
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
        "\\"  [0-7]{1,3}     { u = push_utf8(slice, lex_oct(in.tok, in.cur)); continue; }
        "\\x" [0-9a-fA-F]{2} { u = push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\u" [0-9a-fA-F]{4} { u = push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\U" [0-9a-fA-F]{8} { u = push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
    */
  }

  ok &= is_unicode(slice);
  std::shared_ptr<String> str = std::make_shared<String>(std::move(slice));
  exprs.push_back(new Literal(SYM_LOCATION, std::move(str)));

  if (exprs.size() == 1) {
    out = exprs.front();
  } else {
    DefMap *map = new DefMap(LOCATION);
    map->map["_catopen"]  = std::unique_ptr<Expr>(new Prim(LOCATION, "catopen"));
    map->map["_catadd"]   = std::unique_ptr<Expr>(new Lambda(LOCATION, "_", new Lambda(LOCATION, "_", new Prim(LOCATION, "catadd"))));
    map->map["_catclose"] = std::unique_ptr<Expr>(new Lambda(LOCATION, "_", new Prim(LOCATION, "catclose")));
    Expr *body = new VarRef(LOCATION, "_catopen");
    for (auto expr : exprs)
      body = new App(LOCATION, new App(LOCATION, new VarRef(LOCATION, "_catadd"), body), expr);
    map->body = std::unique_ptr<Expr>(new App(LOCATION, new VarRef(LOCATION, "_catclose"), body));
    out = map;
  }

  return ok;
}

#define mkSym2(x, v) Symbol(x, SYM_LOCATION, v)
#define mkSym(x) Symbol(x, SYM_LOCATION)

static Symbol lex_top(Lexer &lex) {
  input_t &in = *lex.engine.get();
  Coordinates start;
top:
  start.row = in.row;
  start.column = in.cur - in.sol + 1;
  in.tok = in.cur;
  char *YYCTXMARKER;
  (void)YYCTXMARKER;

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
        Expr *out;
        bool ok = in.cur[-1] == '"' ? lex_dstr(lex, out) : lex_sstr(lex, out);
        return mkSym2(ok ? LITERAL : ERROR, out);
      }

      // integer literals
      dec = [0-9][0-9_]*;
      hex = '0x' [0-9a-fA-F_]+;
      bin = '0b' [01_]+;
      (dec | hex | bin) {
        std::string integer(in.tok, in.cur);
        std::replace(integer.begin(), integer.end(), '_', ' ');
        std::shared_ptr<Integer> value = std::make_shared<Integer>(integer.c_str());
        return mkSym2(LITERAL, new Literal(SYM_LOCATION, std::move(value)));
      }

      // keywords
      "def"       { return mkSym(DEF);       }
      "global"    { return mkSym(GLOBAL);    }
      "publish"   { return mkSym(PUBLISH);   }
      "subscribe" { return mkSym(SUBSCRIBE); }
      "prim"      { return mkSym(PRIM);      }
      "if"        { return mkSym(IF);        }
      "then"      { return mkSym(THEN);      }
      "else"      { return mkSym(ELSE);      }
      "here"      { return mkSym(HERE);      }
      "memoize"   { return mkSym(MEMOIZE);   }
      "\\"        { return mkSym(LAMBDA);    }
      "="         { return mkSym(EQUALS);    }
      "("         { return mkSym(POPEN);     }
      ")"         { return mkSym(PCLOSE);    }
      "{"         { return mkSym(BOPEN);     }
      "}"         { return mkSym(BCLOSE);    }

      // identifiers
      op = [.$^*/%\-+~<>=!&|,]+;
      id = [a-z][a-zA-Z0-9_]* | "_";

      id { return mkSym(ID); }
      op { return mkSym(OPERATOR); }
   */

   // reserved punctuation: `@[]:;
   // reserved id space: capitalized
}

struct state_t {
  std::string location;
  std::vector<int> tabs;
  int indent;
  bool eol;

  state_t() : location(), tabs(), indent(0), eol(false) {
    tabs.push_back(0);
  }
};

Lexer::Lexer(const char *file)
 : engine(new input_t(file, fopen(file, "r"))), state(new state_t), next(ERROR, Location(file, Coordinates(), Coordinates())), fail(false)
{
  if (engine->file) consume();
}

Lexer::Lexer(const std::string &cmdline, const char *target)
  : engine(new input_t(target, 0, 0, cmdline.size())), state(new state_t), next(ERROR, LOCATION, 0), fail(false)
{
  if (cmdline.size() >= SIZE) {
    fail = true;
  } else {
    memcpy(&engine->buf[0], cmdline.c_str(), cmdline.size());
    memset(&engine->buf[cmdline.size()], 0, YYMAXFILL);
    engine->eof = true;
    engine->lim += YYMAXFILL;
    consume();
  }
}

Lexer::~Lexer() {
  if (engine->file) fclose(engine->file);
}

std::string Lexer::text() {
  return std::string(engine->tok, engine->cur);
}

void Lexer::consume() {
  if (state->eol) {
    if (state->indent < state->tabs.back()) {
      state->tabs.pop_back();
      next.type = DEDENT;
    } else {
      next.type = EOL;
      state->eol = false;
    }
  } else if (state->indent > state->tabs.back()) {
    state->tabs.push_back(state->indent);
    next.type = INDENT;
  } else {
    next = lex_top(*this);
    if (next.type == EOL) {
      state->indent = (engine->cur - engine->tok) - 1;
      state->eol = true;
      consume();
    }
  }
}
