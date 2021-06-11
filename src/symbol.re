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

// Open Group Base Specifications Issue 7
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <utf8proc.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>

#include "symbol.h"
#include "value.h"
#include "expr.h"
#include "parser.h"
#include "utf8.h"
#include "lexint.h"
#include "gc.h"

const char *symbolTable[] = {
  "ERROR", "ID", "OPERATOR", "LITERAL", "DEF", "VAL", "GLOBAL", "PUBLISH", "SUBSCRIBE", "PRIM", "LAMBDA",
  "DATA", "EQUALS", "POPEN", "PCLOSE", "BOPEN", "BCLOSE", "IF", "THEN", "ELSE", "HERE", "END",
  "MATCH", "EOL", "INDENT", "DEDENT", "COLON", "TARGET", "PACKAGE", "IMPORT", "EXPORT", "FROM",
  "TYPE", "TOPIC", "UNARY", "BINARY", "REQUIRE"
};

/*!re2c
  re2c:flags:tags = 1;
  re2c:tags:expression = "in.@@";
  re2c:define:YYCTYPE = "unsigned char";
  re2c:flags:8 = 1;
*/

/*!max:re2c*/
static const size_t SIZE = 4 * 1024;

/*!include:re2c "unicode_categories.re" */

/*!re2c
Sk_notick = Sk \ [`];
modifier = Lm|M;
upper = Lt|Lu;

// Sm categorized by operator precedence
Sm_id     = [϶∂∅∆∇∞∿⋮⋯⋰⋱▷◁◸◹◺◻◼◽◾◿⟀⟁⦰⦱⦲⦳⦴⦵⦽⧄⧅⧈⧉⧊⧋⧌⧍⧖⧗⧝⧞⧠⧨⧩⧪⧫⧬⧭⧮⧯⧰⧱⧲⧳];
Sm_nfkc   = [⁄⁺⁻⁼₊₋₌⅀−∕∖∣∤∬∭∯∰∶∼≁⨌⩴⩵⩶﬩﹢﹤﹥﹦＋＜＝＞｜～￢￩￪￫￬];
Sm_norm   = [؆؇⁒℘⅁⅂⅃⅄⅋∊∍∗∽∾⊝⋴⋷⋼⋾⟂⟋⟍⟘⟙⟝⟞⦀⦂⧵⧸⧹⨟⨾⫞⫟⫠];
Sm_unop   = [√∛∜];
Sm_comp   = [∘⊚⋆⦾⧇];
Sm_produ  = [∏⋂⨀⨂⨅⨉];
Sm_prodb  = [×∙∩≀⊓⊗⊙⊛⊠⊡⋄⋅⋇⋈⋉⋊⋋⋌⋒⟐⟕⟖⟗⟡⦁⦻⦿⧆⧑⧒⧓⧔⧕⧢⨝⨯⨰⨱⨲⨳⨴⨵⨶⨷⨻⨼⨽⩀⩃⩄⩋⩍⩎];
Sm_divu   = [∐];
Sm_divb   = [÷⊘⟌⦸⦼⧶⧷⨸⫻⫽];
Sm_sumu   = [∑∫∮∱∲∳⋃⨁⨃⨄⨆⨊⨋⨍⨎⨏⨐⨑⨒⨓⨔⨕⨖⨗⨘⨙⨚⨛⨜⫿];
Sm_sumb   = [+~¬±∓∔∪∸∸∹∺∻≂⊌⊍⊎⊔⊕⊖⊞⊟⊹⊻⋓⧺⧻⧾⧿⨢⨣⨤⨥⨦⨧⨨⨩⨪⨫⨬⨭⨮⨹⨺⨿⩁⩂⩅⩊⩌⩏⩐⩪⩫⫬⫭⫾];
Sm_lt     = [<≤≦≨≪≮≰≲≴≶≸≺≼≾⊀⊂⊄⊆⊈⊊⊏⊑⊰⊲⊴⊷⋐⋖⋘⋚⋜⋞⋠⋢⋤⋦⋨⋪⋬⟃⟈⧀⧏⧡⩹⩻⩽⩿⪁⪃⪅⪇⪉⪋⪍⪏⪑⪓⪕⪗⪙⪛⪝⪟⪡⪣⪦⪨⪪⪬⪯⪱⪳⪵⪷⪹⪻⪽⪿⫁⫃⫅⫇⫉⫋⫍⫏⫑⫓⫕⫷⫹];
Sm_gt     = [>≥≧≩≫≯≱≳≵≷≹≻≽≿⊁⊃⊅⊇⊉⊋⊐⊒⊱⊳⊵⊶⋑⋗⋙⋛⋝⋟⋡⋣⋥⋧⋩⋫⋭⟄⟉⧁⧐⩺⩼⩾⪀⪂⪄⪆⪈⪊⪌⪎⪐⪒⪔⪖⪘⪚⪜⪞⪠⪢⪧⪩⪫⪭⪰⪲⪴⪶⪸⪺⪼⪾⫀⫂⫄⫆⫈⫊⫌⫎⫐⫒⫔⫖⫸⫺];
Sm_eq     = [=≃≄≅≆≇≈≉≊≋≌≍≎≏≐≑≒≓≔≕≖≗≘≙≚≛≜≝≞≟≠≡≢≣≭⊜⋍⋕⧂⧃⧎⧣⧤⧥⧦⧧⩆⩇⩈⩉⩙⩦⩧⩨⩩⩬⩭⩮⩯⩰⩱⩲⩳⩷⩸⪤⪥⪮⫗⫘];
Sm_test   = [∈∉∋∌∝∟∠∡∢∥∦≬⊾⊿⋔⋲⋳⋵⋶⋸⋹⋺⋻⋽⋿⍼⟊⟒⦛⦜⦝⦞⦟⦠⦡⦢⦣⦤⦥⦦⦧⦨⦩⦪⦫⦬⦭⦮⦯⦶⦷⦹⦺⩤⩥⫙⫚⫛⫝̸⫝⫡⫮⫲⫳⫴⫵⫶⫼];
Sm_andu   = [⋀];
Sm_andb   = [∧⊼⋏⟎⟑⨇⩑⩓⩕⩘⩚⩜⩞⩞⩟⩟⩠⩠];
Sm_oru    = [⋁];
Sm_orb    = [|∨⊽⋎⟇⟏⨈⩒⩔⩖⩗⩛⩝⩡⩢⩣];
Sm_Sc     = [♯];
Sm_larrow = [←↑↚⇷⇺⇽⊣⊥⟣⟥⟰⟲⟵⟸⟻⟽⤂⤆⤉⤊⤌⤎⤒⤙⤛⤝⤟⤣⤦⤧⤪⤱⤲⤴⤶⤺⤽⤾⥀⥃⥄⥆⥉⥒⥔⥖⥘⥚⥜⥞⥠⥢⥣⥪⥫⥳⥶⥷⥺⥻⥼⥾⫣⫤⫥⫨⫫⬰⬱⬲⬳⬴⬵⬶⬷⬸⬹⬺⬻⬼⬽⬾⬿⭀⭁⭂⭉⭊⭋];
Sm_rarrow = [→↓↛↠↣↦⇏⇒⇴⇶⇸⇻⇾⊢⊤⊦⊧⊨⊩⊪⊫⊬⊭⊮⊯⊺⟢⟤⟱⟳⟴⟶⟹⟼⟾⟿⤀⤁⤃⤅⤇⤈⤋⤍⤏⤐⤑⤓⤔⤕⤖⤗⤘⤚⤜⤞⤠⤤⤥⤨⤩⤭⤮⤯⤰⤳⤵⤷⤸⤹⤻⤼⤿⥁⥂⥅⥇⥓⥕⥗⥙⥛⥝⥟⥡⥤⥥⥬⥭⥰⥱⥲⥴⥵⥸⥹⥽⥿⧴⫢⫦⫧⫪⭃⭄⭇⭈⭌];
Sm_earrow = [↔↮⇎⇔⇵⇹⇼⇿⟚⟛⟠⟷⟺⤄⤡⤢⤫⤬⥈⥊⥋⥌⥍⥎⥏⥐⥑⥦⥧⥨⥩⥮⥯⫩];
Sm_quant  = [∀∁∃∄∎∴∵∷];
Sm_wtf    = [؈⊸⟓⟔⟜⟟⦙⦚⧜⧟⨞⨠⨡⫯⫰⫱];
Sm_multi  = [⌠⌡⍼⎛⎜⎝⎞⎟⎠⎡⎢⎣⎤⎥⎦⎧⎨⎩⎪⎫⎬⎭⎮⎯⎰⎱⎲⎳⏜⏝⏞⏟⏠⏡];
Sm_op = Sm_nfkc | Sm_norm | Sm_unop | Sm_comp | Sm_produ | Sm_prodb | Sm_divu | Sm_divb | Sm_sumu | Sm_sumb | Sm_lt | Sm_gt | Sm_eq | Sm_test | Sm_andu | Sm_andb | Sm_oru | Sm_orb | Sm_Sc | Sm_larrow | Sm_rarrow | Sm_earrow | Sm_quant;

nlc = [\n\v\f\r\x85\u2028\u2029];
nl = nlc | "\r\n";
notnl = [^] \ nlc;
lws = [\t \xa0\u1680\u2000-\u200A\u202F\u205F\u3000];
*/

bool Lexer::isLower(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;

top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:eof = -1;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *           { return true; }
      "unary "    { return false; }
      "binary "   { return false; }
      modifier    { goto top; }
      "_\x00"     { return false; }
      upper       { return false; }
  */
}

bool Lexer::isUpper(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:eof = -1;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *           { return false; }
      modifier    { goto top; }
      upper       { return true; }
  */
}

bool Lexer::isOperator(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:eof = -1;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *           { return false; }
      "unary "    { return true; }
      "binary "   { return true; }
  */
}

op_type op_precedence(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:eof = -1;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *                          { return op_type(-1, -1);}
      "."                        { return op_type(22, 1); }
      [smpa]                     { return op_type(APP_PRECEDENCE, 1); } // SUBSCRIBE/PRIM/APP
      Sm_comp                    { return op_type(20, 0); }
      Sm_unop                    { return op_type(19, 0); }
      "^"                        { return op_type(18, 0); }
      Sm_produ | Sm_divu         { return op_type(17, 0); }
      [*/%] | Sm_prodb | Sm_divb { return op_type(16, 1); }
      Sm_sumu                    { return op_type(15, 0); }
      [\-] | Sm_sumb             { return op_type(14, 1); }
      Sm_test | Sm_lt | Sm_gt    { return op_type(13, 1); }
      "!" | Sm_eq                { return op_type(12, 0); }
      Sm_andu                    { return op_type(11, 0); }
      "&" | Sm_andb              { return op_type(10, 1);  }
      Sm_oru                     { return op_type(9, 0);  }
      "|" | Sm_orb               { return op_type(8, 1);  }
      Sm_Sc | Sc                 { return op_type(7, 0);  }
      Sm_larrow | Sm_rarrow      { return op_type(6, 1);  }
      Sm_earrow                  { return op_type(5, 0);  }
      Sm_quant                   { return op_type(4, 0);  }
      ":"                        { return op_type(3, 0);  }
      ","                        { return op_type(2, 0);  }
      ";"                        { return op_type(1, 0);  }
      [i\\]                      { return op_type(0, 0);  } // IF and LAMBDA
      Sk                         { goto top; }
  */
}

struct state_t {
  std::vector<int> tabs;
  std::string indent;
  bool eol;

  state_t() : tabs(), indent(), eol(false) {
    tabs.push_back(0);
  }
};

struct input_t {
  unsigned char buf[SIZE + 1];
  const unsigned char *lim;
  const unsigned char *cur;
  const unsigned char *mar;
  const unsigned char *tok;
  const unsigned char *sol;
  /*!stags:re2c format = "const unsigned char *@@;"; */
  long offset;
  int  row;
  bool eof;

  const char *filename;
  FILE *const file;

  input_t(const char *fn, FILE *f, int start = SIZE, int end = SIZE)
   : buf(), lim(buf + end), cur(buf + start), mar(buf + start), tok(buf + start), sol(buf + start),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(-start), row(1), eof(false), filename(fn), file(f) { }
  input_t(const char *fn, const unsigned char *buf_, int end)
   : buf(), lim(buf_ + end), cur(buf_), mar(buf_), tok(buf_), sol(buf_),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(0), row(1), eof(false), filename(fn), file(0) { }

  int __attribute__ ((noinline)) fill();

  Coordinates coord() const { return Coordinates(row, 1 + cur - sol, offset + cur - &buf[0]); }
};

#define SYM_LOCATION Location(in.filename, start, in.coord()-1)
#define mkSym2(x, v) Symbol(x, SYM_LOCATION, v)
#define mkSym(x) Symbol(x, SYM_LOCATION)

int input_t::fill() {
  if (eof) return 1;

  const size_t used = lim - tok;
  const size_t free = SIZE - used;

  memmove(buf, tok, used);
  const unsigned char *newlim = buf + used;
  offset += free;

  cur = newlim - (lim - cur);
  mar = newlim - (lim - mar);
  tok = newlim - (lim - tok);
  sol = newlim - (lim - sol);
  /*!stags:re2c format = "if (@@) @@ = newlim - (lim - @@);"; */

  lim = newlim;
  if (file) lim += fread(buf + (lim - buf), 1, free, file);

  eof = lim < buf + SIZE;
  buf[lim - buf] = 0;

  return 0;
}

static ssize_t unicode_escape(const unsigned char *s, const unsigned char *e, char **out, bool compat) {
  utf8proc_uint8_t *dst;
  ssize_t len;

  utf8proc_option_t oCanon = static_cast<utf8proc_option_t>(
      UTF8PROC_COMPOSE   |
      UTF8PROC_IGNORE    |
      UTF8PROC_LUMP      |
      UTF8PROC_REJECTNA);
  utf8proc_option_t oCompat = static_cast<utf8proc_option_t>(
      UTF8PROC_COMPAT    |
      oCanon);

  len = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>(s),
    e - s,
    &dst,
    compat?oCompat:oCanon);

  *out = reinterpret_cast<char*>(dst);
  return len;
}

static std::string unicode_escape_canon(std::string &&str) {
  char *cleaned;
  const unsigned char *data = reinterpret_cast<const unsigned char *>(str.data());
  ssize_t len = unicode_escape(data, data + str.size(), &cleaned, false);
  if (len < 0) return std::move(str);
  std::string out(cleaned, len);
  free(cleaned);
  return out;
}

static bool lex_rstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  Coordinates first = start;
  std::vector<Expr*> exprs;
  std::string check;
  std::string slice;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "in.fill";
        re2c:eof = 0;

        *                    { return false; }
        $                    { return false; }
        "\\\\"               { slice.append("\\\\"); continue; }
        "\\`"                { slice.append("\\`");  continue; }
        "`"                  { break; }
        "\\\$"               { slice.append("\\$");  continue; }
        "\${"		     {
          Coordinates expr = in.coord();
          std::string str = unicode_escape_canon(std::move(slice));
          check.append(str);
          exprs.push_back(new Literal(SYM_LOCATION, String::literal(lex.heap, std::move(str)), &String::typeVar));
          exprs.back()->flags |= FLAG_AST;
          lex.consume();
          exprs.push_back(parse_expr(lex));
          if (lex.next.type == EOL) lex.consume();
          expect(BCLOSE, lex);
          check.append(2 + in.coord().bytes - expr.bytes, '.');
          start = in.coord() - 1;
          slice.clear();
          continue;
        }
        notnl                { slice.append(in.tok, in.cur); continue; }
    */
  }

  std::shared_ptr<RE2> exp;

  if (exprs.empty()) {
    RootPointer<RegExp> val = RegExp::literal(lex.heap, unicode_escape_canon(std::move(slice)));
    exp = val->exp;
    out = new Literal(SYM_LOCATION, std::move(val), &RegExp::typeVar);
  } else {
    std::string str = unicode_escape_canon(std::move(slice));
    check.append(str);
    exprs.push_back(new Literal(SYM_LOCATION, String::literal(lex.heap, std::move(str)), &String::typeVar));
    exprs.back()->flags |= FLAG_AST;

    // Not actually used beyond confirming this is a valid regular expression
    exp = std::make_shared<RE2>(re2::StringPiece(check));

    Expr *cat = new Prim(LOCATION, "rcat");
    for (size_t i = 0; i < exprs.size(); ++i) cat = new Lambda(LOCATION, "_", cat, i?"":" ");
    Location catloc = Location(in.filename, first, in.coord() - 1);
    for (auto expr : exprs) cat = new App(catloc, cat, expr);
    cat->location = catloc;
    cat->flags |= FLAG_AST;
    out = cat;
  }

  if (!exp->ok()) {
    lex.fail = true;
    std::cerr << "Invalid regular expression at "
      << SYM_LOCATION.file() << "; "
      << exp->error() << std::endl;
  }

  return true;
}

static bool lex_sstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  std::string slice;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "in.fill";
        re2c:eof = 0;

        *                    { slice.push_back(*in.tok); continue; }
        $                    { return false; }
        "'"                  { break; }
        nl                   { ++in.row; in.sol = in.tok+1; slice.append(in.tok, in.cur); continue; }
    */
  }

  // NOTE: unicode_escape NOT invoked; '' is raw "" is cleaned
  out = new Literal(SYM_LOCATION, String::literal(lex.heap, slice), &String::typeVar);
  return true;
}

static bool is_escape(Lexer &lex, std::string &slice, const std::string &prefix) {
  if (prefix.empty()) {
    return true;
  } else if (slice.size() >= prefix.size() &&
      std::equal(slice.end() - prefix.size(), slice.end(), prefix.begin())) {
    slice.resize(slice.size() - prefix.size());
    return true;
  } else {
    input_t &in = *lex.engine.get();
    slice.append(in.tok, in.cur);
    return false;
  }
}

static bool lex_dstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  Coordinates first = start;
  std::vector<Expr*> exprs;
  std::string slice, prefix, indent;
  const unsigned char *nl, *lws, *body;
  bool ok = true, multiline = false;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "in.fill";
        re2c:eof = 0;

        * { return false; }
        $ { return false; }

        @nl nl @lws lws* @body {
          ++in.row;
          in.sol = in.tok+1;
          Location nll(in.filename, Coordinates(in.row, 1), in.coord()-1);
          if (!multiline) {
            multiline = true;
            indent.assign(lws, body);
            prefix = slice;
            slice.clear();
            std::string &lindent = lex.state->indent;
            if (!exprs.empty()) {
              std::cerr << "Multiline string prefix cannot include interpolation at " << nll.file() << std::endl;
              lex.fail = true;
            } else if (static_cast<size_t>(body - lws) <= lex.state->indent.size()) {
              std::cerr << "Insufficient whitespace indentation at " << nll.file() << std::endl;
              lex.fail = true;
            } else if (!std::equal(lindent.begin(), lindent.end(), lws)) {
              std::cerr << "Whitespace indentation does not match previous line at " << nll.file() << std::endl;
              lex.fail = true;
            }
          } else {
            slice.append(nl, lws);
            if (static_cast<size_t>(body - lws) < indent.size()) {
              std::cerr << "Insufficient whitespace indentation at " << nll.file() << std::endl;
              lex.fail = true;
            } else if (!std::equal(indent.begin(), indent.end(), lws)) {
              std::cerr << "Whitespace indentation does not match previous line at " << nll.file() << std::endl;
              lex.fail = true;
            } else {
              slice.append(lws+indent.size(), body);
            }
          }
          continue;
        }

        [{] {
          if (is_escape(lex, slice, prefix)) {
            exprs.push_back(new Literal(SYM_LOCATION, String::literal(lex.heap, unicode_escape_canon(std::move(slice))), &String::typeVar));
            exprs.back()->flags |= FLAG_AST;
            lex.consume();
            exprs.push_back(parse_expr(lex));
            if (lex.next.type == EOL) lex.consume();
            expect(BCLOSE, lex);
            start = in.coord() - 1;
            slice.clear();
          }
          continue;
        }

        "\\a"                { if (is_escape(lex, slice, prefix)) slice.push_back('\a'); continue; }
        "\\b"                { if (is_escape(lex, slice, prefix)) slice.push_back('\b'); continue; }
        "\\f"                { if (is_escape(lex, slice, prefix)) slice.push_back('\f'); continue; }
        "\\n"                { if (is_escape(lex, slice, prefix)) slice.push_back('\n'); continue; }
        "\\r"                { if (is_escape(lex, slice, prefix)) slice.push_back('\r'); continue; }
        "\\t"                { if (is_escape(lex, slice, prefix)) slice.push_back('\t'); continue; }
        "\\v"                { if (is_escape(lex, slice, prefix)) slice.push_back('\v'); continue; }
        "\\" (lws|[{}\\'"?]) { if (is_escape(lex, slice, prefix)) slice.append(in.tok+1, in.cur); continue; }
        "\\"  [0-7]{1,3}     { if (is_escape(lex, slice, prefix)) ok &= push_utf8(slice, lex_oct(in.tok, in.cur)); continue; }
        "\\x" [0-9a-fA-F]{2} { if (is_escape(lex, slice, prefix)) ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\u" [0-9a-fA-F]{4} { if (is_escape(lex, slice, prefix)) ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\U" [0-9a-fA-F]{8} { if (is_escape(lex, slice, prefix)) ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }

        ["]       { if (is_escape(lex, slice, prefix)) break; else continue; }
        [^]       { slice.append(in.tok, in.cur); continue;  }
    */
  }

  RootPointer<String> str = String::literal(lex.heap, unicode_escape_canon(std::move(slice)));
  exprs.push_back(new Literal(SYM_LOCATION, std::move(str), &String::typeVar));
  exprs.back()->flags |= FLAG_AST;

  if (exprs.size() == 1) {
    out = exprs.front();
  } else {
    Expr *cat = new Prim(LOCATION, "vcat");
    Location full(in.filename, first, in.coord() - 1);
    for (size_t i = 0; i < exprs.size(); ++i) cat = new Lambda(full, "_", cat, i?"":" ");
    for (auto expr : exprs) cat = new App(full, cat, expr);
    cat->flags |= FLAG_AST;
    out = cat;
  }

  return ok;
}

static Symbol lex_top(Lexer &lex) {
  input_t &in = *lex.engine.get();
  Coordinates start;
top:
  start = in.coord();
  in.tok = in.cur;

  /*!re2c
      re2c:define:YYCURSOR = in.cur;
      re2c:define:YYMARKER = in.mar;
      re2c:define:YYLIMIT = in.lim;
      re2c:yyfill:enable = 1;
      re2c:define:YYFILL = "in.fill";
      re2c:eof = 0;

      * { return mkSym(ERROR); }
      $ { return mkSym(END); }

      // whitespace
      lws+               { goto top; }
      "#" notnl*         { goto top; }
      nl lws* / ("#"|nl) { ++in.row; in.sol = in.tok+1; goto top; }
      nl lws*            { ++in.row; in.sol = in.tok+1; return mkSym(EOL); }

      // character and string literals
      [`] { Expr *out = 0; bool ok = lex_rstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }
      ['] { Expr *out = 0; bool ok = lex_sstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }
      ["] { Expr *out = 0; bool ok = lex_dstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }

      // double literals
      dec = [1-9][0-9_]*;
      double10  = (dec|"0") "." [0-9_]+ ([eE] [+-]? [0-9_]+)?;
      double10e = (dec|"0") [eE] [+-]? [0-9_]+;
      double16  = "0x" [0-9a-fA-F_]+ "." [0-9a-fA-F_]+ ([pP] [+-]? [0-9a-fA-F_]+)?;
      double16e = "0x" [0-9a-fA-F_]+ [pP] [+-]? [0-9a-fA-F_]+;
      (double10 | double10e | double16 | double16e) {
        std::string x(in.tok, in.cur);
        x.resize(std::remove(x.begin(), x.end(), '_') - x.begin());
        return mkSym2(LITERAL, new Literal(SYM_LOCATION, Double::literal(lex.heap, x.c_str()), &Double::typeVar));
      }

      // integer literals
      oct = '0'[0-7_]*;
      hex = '0x' [0-9a-fA-F_]+;
      bin = '0b' [01_]+;
      (dec | oct | hex | bin) {
        std::string integer(in.tok, in.cur);
        integer.resize(std::remove(integer.begin(), integer.end(), '_') - integer.begin());
        return mkSym2(LITERAL, new Literal(SYM_LOCATION, Integer::literal(lex.heap, integer), &Integer::typeVar));
      }

      // keywords
      "def"       { return mkSym(DEF);       }
      "tuple"     { return mkSym(TUPLE);     }
      "data"      { return mkSym(DATA);      }
      "global"    { return mkSym(GLOBAL);    }
      "target"    { return mkSym(TARGET);    }
      "publish"   { return mkSym(PUBLISH);   }
      "subscribe" { return mkSym(SUBSCRIBE); }
      "prim"      { return mkSym(PRIM);      }
      "if"        { return mkSym(IF);        }
      "then"      { return mkSym(THEN);      }
      "else"      { return mkSym(ELSE);      }
      "here"      { return mkSym(HERE);      }
      "match"     { return mkSym(MATCH);     }
      "require"   { return mkSym(REQUIRE);   }
      "package"   { return mkSym(PACKAGE);   }
      "import"    { return mkSym(IMPORT);    }
      "export"    { return mkSym(EXPORT);    }
      "from"      { return mkSym(FROM);      }
      "type"      { return mkSym(TYPE);      }
      "topic"     { return mkSym(TOPIC);     }
      "unary"     { return mkSym(UNARY);     }
      "binary"    { return mkSym(BINARY);    }
      "\\"        { return mkSym(LAMBDA);    }
      "="         { return mkSym(EQUALS);    }
      ":"         { return mkSym(COLON);     }
      "("         { return mkSym(POPEN);     }
      ")"         { return mkSym(PCLOSE);    }
      "{"         { return mkSym(BOPEN);     }
      "}"         { return mkSym(BCLOSE);    }

      // operators
      Po_reserved = [?@];
      Po_special  = ["#'\\];
      Po_op       = [!%&*,./:;];
      // !!! TODO: Po, Pd(without -)
      op = (Sk_notick|Sc|Sm_op|Po_op|"-")+; // [^] is Sk

      // identifiers
      start = L|So|Sm_id|Nl|"_";
      body = L|So|Sm_id|N|Pc|Lm|M;
      id = modifier* start body*;

      id { return mkSym(ID); }
      op { return mkSym(OPERATOR); }
   */
}

Lexer::Lexer(Heap &heap_, const char *file)
 : heap(heap_), engine(new input_t(file, fopen(file, "r"))), state(new state_t), next(ERROR, Location(file, Coordinates(), Coordinates())), fail(false)
{
  if (engine->file) consume();
}

Lexer::Lexer(Heap &heap_, const std::string &cmdline, const char *target)
  : heap(heap_), engine(new input_t(target, reinterpret_cast<const unsigned char *>(cmdline.c_str()), cmdline.size())), state(new state_t), next(ERROR, LOCATION, 0), fail(false)
{
  consume();
}

Lexer::~Lexer() {
  if (engine->file) fclose(engine->file);
}

static std::string op_escape(const char *str, size_t len) {
  std::string out;
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *limit = s + len;
  const unsigned char *ignore;
  (void)ignore;

  while (true) {
    const unsigned char *start = s;
    /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYLIMIT  = limit;
      re2c:define:YYCURSOR = s;
      re2c:eof = 0;

      *                { break; }
      $                { break; }

      // Two surrogates => one character in json identifiers
      "\\u" [dD] [89abAB] [0-9a-fA-F]{2} "\\u" [dD] [c-fC-F] [0-9a-fA-F]{2} {
        uint32_t lo = lex_hex(start,   start+6);
        uint32_t hi = lex_hex(start+6, start+12);
        uint32_t x = ((lo & 0x3ff) << 10) + (hi & 0x3ff) + 0x10000;
        push_utf8(out, x);
        continue;
      }
      "\\u" [0-9a-fA-F]{4} { push_utf8(out, lex_hex(start, s)); continue; }

      [؆] { out.append("∛"); continue; }
      [؇] { out.append("∜"); continue; }
      [⁒] { out.append("%"); continue; }
      [℘] { out.append("P"); continue; }
      [⅁] { out.append("G"); continue; }
      [⅂] { out.append("L"); continue; }
      [⅃] { out.append("L"); continue; }
      [⅄] { out.append("Y"); continue; }
      [⅋] { out.append("&"); continue; }
      [∊] { out.append("∈"); continue; }
      [∍] { out.append("∋"); continue; }
      [∗] { out.append("*"); continue; }
      [∽] { out.append("~"); continue; }
      [∾] { out.append("~"); continue; }
      [⊝] { out.append("⊖"); continue; }
      [⋴] { out.append("⋳"); continue; }
      [⋷] { out.append("⋶"); continue; }
      [⋼] { out.append("⋻"); continue; }
      [⋾] { out.append("⋽"); continue; }
      [⟂] { out.append("⊥"); continue; }
      [⟋] { out.append("/"); continue; }
      [⟍] { out.append("\\");continue; }
      [⟘] { out.append("⊥"); continue; }
      [⟙] { out.append("⊤"); continue; }
      [⟝] { out.append("⊢"); continue; }
      [⟞] { out.append("⊣"); continue; }
      [⦀] { out.append("⫴"); continue; }
      [⦂] { out.append(":"); continue; }
      [⧵] { out.append("\\");continue; }
      [⧸] { out.append("/"); continue; }
      [⧹] { out.append("\\");continue; }
      [⨟] { out.append(";"); continue; }
      [⨾] { out.append("l"); continue; }
      [⫞] { out.append("⋽"); continue; }
      [⫟] { out.append("⊤"); continue; }
      [⫠] { out.append("⊥"); continue; }
      [^] { out.append(reinterpret_cast<const char*>(start), reinterpret_cast<const char*>(s)); continue; }
  */}

  return out;
}

std::string Lexer::id() const {
  std::string out;
  char *dst;
  ssize_t len;

  len = unicode_escape(engine->tok, engine->cur, &dst, true); // compat
  if (len >= 0) {
    out = op_escape(dst, len);
    free(dst);
  } else {
    out.assign(engine->tok, engine->cur);
  }

  return out;
}

void Lexer::consume() {
  if (state->eol) {
    if ((int)state->indent.size() < state->tabs.back()) {
      state->tabs.pop_back();
      next.type = DEDENT;
    } else if ((int)state->indent.size() > state->tabs.back()) {
      state->tabs.push_back(state->indent.size());
      next.type = INDENT;
    } else {
      next.type = EOL;
      state->eol = false;
    }
  } else {
    next = lex_top(*this);
    if (next.type == EOL) {
      std::string newindent(engine->tok+1, engine->cur);
      size_t check = std::min(newindent.size(), state->indent.size());
      if (!std::equal(newindent.begin(), newindent.begin()+check, state->indent.begin())) {
        std::cerr << "Whitespace is neither a prefix nor a suffix of the previous line at " << next.location.file() << std::endl;
        fail = true;
      }
      std::swap(state->indent, newindent);
      state->eol = true;
      consume();
    }
  }
}
