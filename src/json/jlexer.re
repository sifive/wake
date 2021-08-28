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

#include <string.h>

#include "util/location.h"
#include "lexint.h"
#include "utf8.h"
#include "json5.h"

/*!re2c
  re2c:flags:tags = 1;
  re2c:tags:expression = "in.@@";
  re2c:define:YYCTYPE = "unsigned char";
  re2c:flags:8 = 1;
*/

/*!max:re2c*/
static const size_t SIZE = 4 * 1024;

/*!include:re2c "unicode_categories.re" */

struct jinput_t {
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

  jinput_t(const char *fn, FILE *f, int start = SIZE, int end = SIZE)
   : buf(), lim(buf + end), cur(buf + start), mar(buf + start), tok(buf + start), sol(buf + start),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(-start), row(1), eof(false), filename(fn), file(f) { }
  jinput_t(const char *fn, const unsigned char *buf_, int end)
   : buf(), lim(buf_ + end), cur(buf_), mar(buf_), tok(buf_), sol(buf_),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(0), row(1), eof(false), filename(fn), file(0) { }

  int __attribute__ ((noinline)) fill();

  Coordinates coord() const { return Coordinates(row, 1 + cur - sol, offset + cur - &buf[0]); }
};

#define SYM_LOCATION Location(in.filename, start, in.coord()-1)
#define mkSym2(x, v) JSymbol(x, SYM_LOCATION, std::move(v))
#define mkSym(x) JSymbol(x, SYM_LOCATION)

int jinput_t::fill() {
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

static void lex_jcomment(jinput_t &in) {
  unsigned const char *s = in.tok;
  unsigned const char *e = in.cur;
  const unsigned char *ignore;
  (void)ignore;

  while (s != e) {
    /*!re2c
        re2c:yyfill:enable = 0;
        re2c:eof= -1;
        re2c:define:YYCURSOR = s;
        re2c:define:YYMARKER = ignore;

        // LineTerminator ::
        //   <LF>
        //   <CR>
        //   <LS>
        //   <PS>
        LineTerminator = [\n\r\u2028\u2029];

        *               { continue; }
        LineTerminator  { ++in.row; in.sol = s; continue; }
    */
  }
}

static bool lex_jstr(JLexer &lex, std::string &out, unsigned char eos)
{
  jinput_t &in = *lex.engine.get();
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

        // LineTerminatorSequence ::
        //   <LF>
        //   <CR> [lookahead ∉ <LF> ]
        //   <LS>
        //   <PS>
        //   <CR> <LF>
        LineTerminatorSequence = [\n\r\u2028\u2029] | "\r\n";

        // LineContinuation ::
        //   \ LineTerminatorSequence
        LineContinuation = "\\" LineTerminatorSequence;

        // HexDigit ::
        //   one of 0 1 2 3 4 5 6 7 8 9 a b c d e f A B C D E F
        HexDigit = [0-9a-fA-F];

        // SingleEscapeCharacter ::
        //   one of ' " \ b f n r t v
        // EscapeCharacter ::
        //   SingleEscapeCharacter
        //   DecimalDigit
        //   x
        //   u
        // NonEscapeCharacter ::
        //   SourceCharacter but not one of EscapeCharacter or LineTerminator
        NonEscapeCharacter = [^'"\\bfnrtv0-9xu\n\r\u2028\u2029];

        // CharacterEscapeSequence ::
        //   SingleEscapeCharacter
        //   NonEscapeCharacter

        // HexEscapeSequence ::
        //   x HexDigit HexDigit
        HexEscapeSequence = "x" HexDigit{2};

        // UnicodeEscapeSequence ::
        //   u HexDigit HexDigit HexDigit HexDigit
        UnicodeEscapeSequence = "u" HexDigit{4};

        // EscapeSequence ::
        //   CharacterEscapeSequence
        //   0 [lookahead ∉ DecimalDigit]
        //   HexEscapeSequence
        //   UnicodeEscapeSequence

        // JSON5DoubleStringCharacter::
        //   SourceCharacter but not one of " or \ or LineTerminator
        //   \ EscapeSequence
        //   LineContinuation
        //   U+2028
        //   U+2029
        // JSON5SingleStringCharacter::
        //   SourceCharacter but not one of ' or \ or LineTerminator
        //   \ EscapeSequence
        //   LineContinuation
        //   U+2028
        //   U+2029

        // JSON5DoubleStringCharacters::
        //   JSON5DoubleStringCharacter JSON5DoubleStringCharactersopt
        // JSON5SingleStringCharacters::
        //   JSON5SingleStringCharacter JSON5SingleStringCharactersopt

        *                { return false; }
        $                { return false; }
        LineContinuation { continue; }
        ["'] {
          if (in.cur[-1] == eos) {
             break;
          } else {
             slice.push_back(in.cur[-1]);
             continue;
          }
        }

	// Two surrogates => one character
        "\\u" [dD] [89abAB] HexDigit{2} "\\u" [dD] [c-fC-F] HexDigit{2} {
          uint32_t lo = lex_hex(in.tok,   in.tok+6);
          uint32_t hi = lex_hex(in.tok+6, in.tok+12);
          uint32_t x = ((lo & 0x3ff) << 10) + (hi & 0x3ff) + 0x10000;
          if (!push_utf8(slice, x)) return false;
          continue;
        }
	"\\u" HexDigit{4}       { if (!push_utf8(slice, lex_hex(in.tok, in.cur))) return false; continue; }
	"\\x" HexDigit{2}       { if (!push_utf8(slice, lex_hex(in.tok, in.cur))) return false; continue; }
        "\\0" / [^0-9]          { slice.push_back('\0'); continue; }
        "\\b"                   { slice.push_back('\b'); continue; }
        "\\t"                   { slice.push_back('\t'); continue; }
        "\\n"                   { slice.push_back('\n'); continue; }
        "\\v"                   { slice.push_back('\v'); continue; }
        "\\f"                   { slice.push_back('\f'); continue; }
        "\\r"                   { slice.push_back('\r'); continue; }
        "\\'"                   { slice.push_back('\''); continue; }
        "\\\""                  { slice.push_back('"');  continue; }
        "\\\\"                  { slice.push_back('\\'); continue; }
        "\\" NonEscapeCharacter { slice.append(in.tok+1, in.cur);  continue; }
        "\\"                    { return false; }
        [^\r\n]                 { slice.append(in.tok, in.cur); continue; }
    */
  }

  out = std::move(slice);
  return true;
}

static JSymbol lex_json(JLexer &lex) {
  jinput_t &in = *lex.engine.get();
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

      // WhiteSpace ::
      //   <TAB>
      //   <VT>
      //   <FF>
      //   <SP>
      //   <NBSP>
      //   <BOM>
      //   <USP>
      WhiteSpace = [\t\v\f \u00a0\ufeff] | Z;

      // MultiLineNotForwardSlashOrAsteriskChar ::
      //   SourceCharacter but not one of / or *
      // PostAsteriskCommentChars ::
      //   MultiLineNotForwardSlashOrAsteriskChar MultiLineCommentCharsopt
      //   * PostAsteriskCommentCharsopt
      // => PostAsteriskCommentCharsopt = "*"* ([^/*] MultiLineCommentCharsopt)?
      // MultiLineNotAsteriskChar ::
      //   SourceCharacter but not *
      // MultiLineCommentChars ::
      //   MultiLineNotAsteriskChar MultiLineCommentCharsopt
      //   * PostAsteriskCommentCharsopt
      // => MultiLineCommentCharsopt = [^*]* ("*" PostAsteriskCommentCharsopt)?

      // To cut the cyclic definition above, note:
      //   a = c* (x b)?
      //   b = d* (y a)?
      //   => a = c* (x d* (y c* (x d* (y c* (x d* ...)?)?)?)?)?
      //   => a = c* (x d* y c*)* (x d*)?
      //     where c = [^*], x = "*", d = "*", y = [^/*]
      //   => MultiLineCommentCharsopt = [^*]* ("*" "*"* [^/*] [^*]*)* ("*" "*"*)?
      MultiLineCommentCharsopt = [^*]* ("*"+ [^/*] [^*]*)* "*"*;

      // MultiLineComment ::
      //   /* MultiLineCommentCharsopt */
      MultiLineComment = "/*" MultiLineCommentCharsopt "*/";

      // SingleLineCommentChar ::
      //   SourceCharacter but not LineTerminator
      // SingleLineCommentChars ::
      //   SingleLineCommentChar SingleLineCommentCharsopt
      // SingleLineComment ::
      //   // SingleLineCommentCharsopt
      SingleLineComment = "//" [^\n\r\u2028\u2029]*;

      // Comment ::
      //   MultiLineComment
      //   SingleLineComment
      Comment = MultiLineComment | SingleLineComment;

      // HexIntegerLiteral ::
      //   0x HexDigit
      //   0X HexDigit
      //   HexIntegerLiteral HexDigit
      HexIntegerLiteral = "0" [xX] HexDigit+;

      // DecimalDigit ::
      //   one of 0 1 2 3 4 5 6 7 8 9
      DecimalDigit = [0-9];

      // NonZeroDigit ::
      //   one of 1 2 3 4 5 6 7 8 9
      NonZeroDigit = [1-9];

      // DecimalDigits ::
      //   DecimalDigit
      //   DecimalDigits DecimalDigit
      DecimalDigits = DecimalDigit+;

      // DecimalIntegerLiteral ::
      //   0
      //   NonZeroDigit DecimalDigitsopt
      DecimalIntegerLiteral = "0" | NonZeroDigit DecimalDigits?;

      // SignedInteger ::
      //   DecimalDigits
      //   + DecimalDigits
      //   - DecimalDigits
      // ExponentIndicator ::
      //   one of e E
      // ExponentPart ::
      //   ExponentIndicator SignedInteger
      ExponentPart = [eE] [+-]? DecimalDigits;

      // DecimalLiteral ::
      //   DecimalIntegerLiteral . DecimalDigitsopt ExponentPartopt
      //   . DecimalDigits ExponentPartopt
      //   DecimalIntegerLiteral ExponentPartopt
      DecimalLiteral = (DecimalIntegerLiteral "." DecimalDigits? ExponentPart?)
                     | ("." DecimalDigits ExponentPart?)
                     | DecimalIntegerLiteral ExponentPart?;

      // NumericLiteral ::
      //   DecimalLiteral
      //   HexIntegerLiteral
      NumericLiteral = DecimalLiteral | HexIntegerLiteral;

      // JSON5NumericLiteral::
      //   NumericLiteral
      //   Infinity
      //   NaN
      JSON5NumericLiteral = NumericLiteral | "Infinity" | "NaN";

      // JSON5Number::
      //   JSON5NumericLiteral
      //   + JSON5NumericLiteral
      //   - JSON5NumericLiteral
      JSON5Number = [+-]? JSON5NumericLiteral;

      // UnicodeLetter ::
      //   any character in the Unicode categories “Uppercase letter (Lu)”, “Lowercase letter (Ll)”, “Titlecase letter (Lt)”, “Modifier letter (Lm)”, “Other letter (Lo)”, or “Letter number (Nl)”.
      UnicodeLetter = Lu | Ll | Lt | Lm | Lo | Nl;
      // UnicodeCombiningMark ::
      //   any character in the Unicode categories “Non-spacing mark (Mn)” or “Combining spacing mark (Mc)”
      UnicodeCombiningMark = Mn | Mc;
      // UnicodeDigit ::
      //   any character in the Unicode category “Decimal number (Nd)”
      UnicodeDigit = Nd;
      // UnicodeConnectorPunctuation ::
      //   any character in the Unicode category “Connector punctuation (Pc)”
      UnicodeConnectorPunctuation = Pc;

      // IdentifierStart ::
      //   UnicodeLetter
      //   $
      //   _
      //   \ UnicodeEscapeSequence
      IdentifierStart = UnicodeLetter | [$_] | ("\\" UnicodeEscapeSequence);

      // IdentifierPart ::
      //   IdentifierStart
      //   UnicodeCombiningMark
      //   UnicodeDigit
      //   UnicodeConnectorPunctuation
      //   <ZWNJ>
      //   <ZWJ>
      IdentifierPart = IdentifierStart | UnicodeCombiningMark | UnicodeDigit | UnicodeConnectorPunctuation | [\u200C\u200D];

      // IdentifierName ::
      //   IdentifierStart
      //   IdentifierName IdentifierPart
      // JSON5Identifier::
      //   IdentifierName
      JSON5Identifier = IdentifierStart IdentifierPart*;

      // JSON5SourceCharacter::
      //   SourceCharacter
      // SourceCharacter ::
      //   any Unicode code unit
      * { return mkSym(JSON_ERROR); }
      $ { return mkSym(JSON_END); }

      // JSON5InputElement::
      //   WhiteSpace
      //   LineTerminator
      //   Comment
      //   JSON5Token
      WhiteSpace      { goto top; }
      LineTerminator  { ++in.row; in.sol = in.cur; goto top; }
      Comment         {
        lex_jcomment(in);
        goto top;
      }

      // JSON5Token::
      //   JSON5Identifier
      //   JSON5Punctuator
      //   JSON5String
      //   JSON5Number

      // Special case the integers
      [+-]? (DecimalIntegerLiteral|HexIntegerLiteral) {
        std::string value(in.tok[0] == '+' ? in.tok+1 : in.tok, in.cur);
        return mkSym2(JSON_INTEGER, value);
      }
      [+-]? NumericLiteral {
        std::string value(in.tok[0] == '+' ? in.tok+1 : in.tok, in.cur);
        return mkSym2(JSON_DOUBLE, value);
      }
      [+-]? "Infinity" {
        std::string value(in.tok[0]=='-'?"-":"+");
        return mkSym2(JSON_INFINITY, value);
      }
      [+-]? "NaN" {
        return mkSym(JSON_NAN);
      }

      // JSON5Punctuator::
      //   one of {}[]:,
      "{"             { return mkSym(JSON_BOPEN);   }
      "}"             { return mkSym(JSON_BCLOSE);  }
      "["             { return mkSym(JSON_SOPEN);   }
      "]"             { return mkSym(JSON_SCLOSE);  }
      ":"             { return mkSym(JSON_COLON);   }
      ","             { return mkSym(JSON_COMMA);   }

      // JSON5Null::
      //   NullLiteral
      // NullLiteral ::
      //   null
      "null"          { return mkSym(JSON_NULLVAL); }

      // JSON5Boolean::
      //   BooleanLiteral
      // BooleanLiteral ::
      //   true
      //   false
      "false"         { return mkSym(JSON_FALSE);   }
      "true"          { return mkSym(JSON_TRUE);    }

      JSON5Identifier {
        std::string value(in.tok, in.cur);
        return mkSym2(JSON_ID, value);
      }

      // JSON5String::
      //  " JSON5DoubleStringCharactersopt "
      //  ' JSON5SingleStringCharactersopt '

      ['"] {
        std::string out;
        bool ok = lex_jstr(lex, out, in.cur[-1]);
        return mkSym2(ok ? JSON_STR : JSON_ERROR, out);
      }
  */
}

JLexer::JLexer(const char *file)
 : engine(new jinput_t(file, fopen(file, "r"))), next(JSON_ERROR, Location(file, Coordinates(), Coordinates())), fail(false)
{
  if (engine->file) consume();
}

JLexer::JLexer(const std::string &body)
  : engine(new jinput_t("string", reinterpret_cast<const unsigned char *>(body.c_str()), body.size())), next(JSON_ERROR, LOCATION), fail(false)
{
  consume();
}

JLexer::JLexer(const char *body, size_t len)
  : engine(new jinput_t("string", reinterpret_cast<const unsigned char *>(body), len)), next(JSON_ERROR, LOCATION), fail(false)
{
  consume();
}

JLexer::~JLexer() {
  if (engine->file) fclose(engine->file);
}

void JLexer::consume() {
  next = lex_json(*this);
}
