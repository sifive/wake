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

#include <cstdio>
#include <cstdint>

#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "utf8proc.h"
#include "lexint.h"
#include "utf8.h"

/*!include:re2c "unicode_categories.re" */

/*!re2c
    re2c:tags:expression = "in.@@";
    re2c:define:YYCTYPE = "uint8_t";
    re2c:define:YYCURSOR = s;
    re2c:define:YYLIMIT = e;
    re2c:define:YYMARKER = m;
    re2c:flags:8 = 1;
    re2c:yyfill:enable = 0;
    re2c:eof = 0;

    // Math symbols that belong in identifiers
    Sm_id = [϶∂∅∆∇∞∿⋮⋯⋰⋱▷◁◸◹◺◻◼◽◾◿⟀⟁⦰⦱⦲⦳⦴⦵⦽⧄⧅⧈⧉⧊⧋⧌⧍⧖⧗⧝⧞⧠⧨⧩⧪⧫⧬⧭⧮⧯⧰⧱⧲⧳];

    // Partition all of Unicode except #"'`[](){}

    l = L | M | Pc | (Pd\[-]) | Sm_id | So;
    n = N;
    o = [-] | Sc | (Sk\[`]) | (Sm\Sm_id) | (Po\[#"'@?]);
    r = Pi | Pf | (Ps\[([{]) | (Pe\[)\]}]) | [?@];
    w = C | Z;

    // Partition (legal) 'o' into precedence classes:
    o_dot     = [.];
    o_exp     = [\^];
    o_muldiv  = [*/%×∙∩≀⊓⊗⊙⊛⊠⊡⋄⋅⋇⋈⋉⋊⋋⋌⋒⟐⟕⟖⟗⟡⦁⦻⦿⧆⧑⧒⧓⧔⧕⧢⨝⨯⨰⨱⨲⨳⨴⨵⨶⨷⨻⨼⨽⩀⩃⩄⩋⩍⩎÷⊘⟌⦸⦼⧶⧷⨸⫻⫽∘⊚⋆⦾⧇];
    o_addsub  = [-+~¬±∓∔∪∸∸∹∺∻≂⊌⊍⊎⊔⊕⊖⊞⊟⊹⊻⋓⧺⧻⧾⧿⨢⨣⨤⨥⨦⨧⨨⨩⨪⨫⨬⨭⨮⨹⨺⨿⩁⩂⩅⩊⩌⩏⩐⩪⩫⫬⫭⫾];
    o_compare = [∈∉∋∌∝∟∠∡∢∥∦≬⊾⊿⋔⋲⋳⋵⋶⋸⋹⋺⋻⋽⋿⍼⟊⟒⦛⦜⦝⦞⦟⦠⦡⦢⦣⦤⦥⦦⦧⦨⦩⦪⦫⦬⦭⦮⦯⦶⦷⦹⦺⩤⩥⫙⫚⫛⫝̸⫝⫡⫮⫲⫳⫴⫵⫶⫼<≤≦≨≪≮≰≲≴≶≸≺≼≾⊀⊂⊄⊆⊈⊊⊏⊑⊰⊲⊴⊷⋐⋖⋘⋚⋜⋞⋠⋢⋤⋦⋨⋪⋬⟃⟈⧀⧏⧡⩹⩻⩽⩿⪁⪃⪅⪇⪉⪋⪍⪏⪑⪓⪕⪗⪙⪛⪝⪟⪡⪣⪦⪨⪪⪬⪯⪱⪳⪵⪷⪹⪻⪽⪿⫁⫃⫅⫇⫉⫋⫍⫏⫑⫓⫕⫷⫹>≥≧≩≫≯≱≳≵≷≹≻≽≿⊁⊃⊅⊇⊉⊋⊐⊒⊱⊳⊵⊶⋑⋗⋙⋛⋝⋟⋡⋣⋥⋧⋩⋫⋭⟄⟉⧁⧐⩺⩼⩾⪀⪂⪄⪆⪈⪊⪌⪎⪐⪒⪔⪖⪘⪚⪜⪞⪠⪢⪧⪩⪫⪭⪰⪲⪴⪶⪸⪺⪼⪾⫀⫂⫄⫆⫈⫊⫌⫎⫐⫒⫔⫖⫸⫺];
    o_inequal = [:!=≃≄≅≆≇≈≉≊≋≌≍≎≏≐≑≒≓≔≕≖≗≘≙≚≛≜≝≞≟≠≡≢≣≭⊜⋍⋕⧂⧃⧎⧣⧤⧥⧦⧧⩆⩇⩈⩉⩙⩦⩧⩨⩩⩬⩭⩮⩯⩰⩱⩲⩳⩷⩸⪤⪥⪮⫗⫘];
    o_and     = [&∧⊼⋏⟎⟑⨇⩑⩓⩕⩘⩚⩜⩞⩞⩟⩟⩠⩠];
    o_or      = [||∨⊽⋎⟇⟏⨈⩒⩔⩖⩗⩛⩝⩡⩢⩣];
    o_dollar  = Sc | [$♯];
    o_lrarrow = [←↑↚⇷⇺⇽⊣⊥⟣⟥⟰⟲⟵⟸⟻⟽⤂⤆⤉⤊⤌⤎⤒⤙⤛⤝⤟⤣⤦⤧⤪⤱⤲⤴⤶⤺⤽⤾⥀⥃⥄⥆⥉⥒⥔⥖⥘⥚⥜⥞⥠⥢⥣⥪⥫⥳⥶⥷⥺⥻⥼⥾⫣⫤⫥⫨⫫⬰⬱⬲⬳⬴⬵⬶⬷⬸⬹⬺⬻⬼⬽⬾⬿⭀⭁⭂⭉⭊⭋→↓↛↠↣↦⇏⇒⇴⇶⇸⇻⇾⊢⊤⊦⊧⊨⊩⊪⊫⊬⊭⊮⊯⊺⟢⟤⟱⟳⟴⟶⟹⟼⟾⟿⤀⤁⤃⤅⤇⤈⤋⤍⤏⤐⤑⤓⤔⤕⤖⤗⤘⤚⤜⤞⤠⤤⤥⤨⤩⤭⤮⤯⤰⤳⤵⤷⤸⤹⤻⤼⤿⥁⥂⥅⥇⥓⥕⥗⥙⥛⥝⥟⥡⥤⥥⥬⥭⥰⥱⥲⥴⥵⥸⥹⥽⥿⧴⫢⫦⫧⫪⭃⭄⭇⭈⭌];
    o_eqarrow = [↔↮⇎⇔⇵⇹⇼⇿⟚⟛⟠⟷⟺⤄⤡⤢⤫⤬⥈⥊⥋⥌⥍⥎⥏⥐⥑⥦⥧⥨⥩⥮⥯⫩];
    o_quant   = [√∛∜∏⋂⨀⨂⨅⨉∐∑∫∮∱∲∳⋃⨁⨃⨄⨆⨊⨋⨍⨎⨏⨐⨑⨒⨓⨔⨕⨖⨗⨘⨙⨚⨛⨜⫿⋀⋁∀∁∃∄∎∴∵∷];
    o_comma   = [,;];

    // Legal whitespace categories
    lws   = [\t \xa0\u1680\u2000-\u200A\u202F\u205F\u3000];
    nlc   = [\n\v\f\r\x85\u2028\u2029];
    nl    = nlc | "\r\n";
    notnl = [^] \ nlc;

    // !!! These go away after normalization, but we need to categorize them first
    //Sm_nfkc   = [⁄⁺⁻⁼₊₋₌⅀−∕∖∣∤∬∭∯∰∶∼≁⨌⩴⩵⩶﬩﹢﹤﹥﹦＋＜＝＞｜～￢￩￪￫￬];
    //Sm_norm   = [؆؇⁒℘⅁⅂⅃⅄⅋∊∍∗∽∾⊝⋴⋷⋼⋾⟂⟋⟍⟘⟙⟝⟞⦀⦂⧵⧸⧹⨟⨾⫞⫟⫠];
 */

Token lex_wake(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        // Detect end-of-input
        $ { return Token(TOKEN_EOF, s); }

        // Comments
        "#" notnl* { return Token(TOKEN_COMMENT, s); }

        // Legal whitespace
        nl   { return Token(TOKEN_NL, s); }
        lws+ { return Token(TOKEN_WS, s); }

        // All keywords
        "def"       { return Token(TOKEN_KW_DEF,       s); }
        "tuple"     { return Token(TOKEN_KW_TUPLE,     s); }
        "data"      { return Token(TOKEN_KW_DATA,      s); }
        "global"    { return Token(TOKEN_KW_GLOBAL,    s); }
        "target"    { return Token(TOKEN_KW_TARGET,    s); }
        "publish"   { return Token(TOKEN_KW_PUBLISH,   s); }
        "subscribe" { return Token(TOKEN_KW_SUBSCRIBE, s); }
        "prim"      { return Token(TOKEN_KW_PRIM,      s); }
        "if"        { return Token(TOKEN_KW_IF,        s); }
        "then"      { return Token(TOKEN_KW_THEN,      s); }
        "else"      { return Token(TOKEN_KW_ELSE,      s); }
        "here"      { return Token(TOKEN_KW_HERE,      s); }
        "match"     { return Token(TOKEN_KW_MATCH,     s); }
        "require"   { return Token(TOKEN_KW_REQUIRE,   s); }
        "package"   { return Token(TOKEN_KW_PACKAGE,   s); }
        "import"    { return Token(TOKEN_KW_IMPORT,    s); }
        "export"    { return Token(TOKEN_KW_EXPORT,    s); }
        "from"      { return Token(TOKEN_KW_FROM,      s); }
        "type"      { return Token(TOKEN_KW_TYPE,      s); }
        "topic"     { return Token(TOKEN_KW_TOPIC,     s); }
        "unary"     { return Token(TOKEN_KW_UNARY,     s); }
        "binary"    { return Token(TOKEN_KW_BINARY,    s); }

        // All special punctuation
        "\\" { return Token(TOKEN_P_BSLASH, s); }
        "="  { return Token(TOKEN_P_EQUALS, s); }
        ":"  { return Token(TOKEN_P_COLON,  s); }
        "("  { return Token(TOKEN_P_POPEN,  s); }
        ")"  { return Token(TOKEN_P_PCLOSE, s); }
        "{"  { return Token(TOKEN_P_BOPEN,  s); }
        "}"  { return Token(TOKEN_P_BCLOSE, s); }
        "["  { return Token(TOKEN_P_SOPEN,  s); }
        "]"  { return Token(TOKEN_P_SCLOSE, s); }
        "_"  { return Token(TOKEN_P_HOLE,   s); }

        // Operators
        o_dot     o* { return Token(TOKEN_OP_DOT,     s); }
        o_quant   o* { return Token(TOKEN_OP_QUANT,   s); }
        o_exp     o* { return Token(TOKEN_OP_EXP,     s); }
        o_muldiv  o* { return Token(TOKEN_OP_MULDIV,  s); }
        o_addsub  o* { return Token(TOKEN_OP_ADDSUB,  s); }
        o_compare o* { return Token(TOKEN_OP_COMPARE, s); }
        o_inequal o* { return Token(TOKEN_OP_INEQUAL, s); }
        o_and     o* { return Token(TOKEN_OP_AND,     s); }
        o_or      o* { return Token(TOKEN_OP_OR,      s); }
        o_dollar  o* { return Token(TOKEN_OP_DOLLAR,  s); }
        o_lrarrow o* { return Token(TOKEN_OP_LRARROW, s); }
        o_eqarrow o* { return Token(TOKEN_OP_EQARROW, s); }
        o_comma   o* { return Token(TOKEN_OP_COMMA,   s); }

        // Double literals
        dec = [1-9][0-9_]*;
        double10  = (dec|"0") "." [0-9_]+ ([eE] [+-]? [0-9_]+)?;
        double10e = (dec|"0") [eE] [+-]? [0-9_]+;
        double16  = "0x" [0-9a-fA-F_]+ "." [0-9a-fA-F_]+ ([pP] [+-]? [0-9a-fA-F_]+)?;
        double16e = "0x" [0-9a-fA-F_]+ [pP] [+-]? [0-9a-fA-F_]+;
        (double10 | double10e | double16 | double16e) { return Token(TOKEN_DOUBLE, s); }

        // Integer literals
        oct = '0'  [0-7_]*;
        hex = '0x' [0-9a-fA-F_]+;
        bin = '0b' [01_]+;
        (dec | oct | hex | bin) { return Token(TOKEN_INTEGER, s); }

        // Raw string literals
        nchar = notnl \ ['];
        [']nchar*    { return Token(TOKEN_STR_RAW, s, false); }
        [']nchar*['] { return Token(TOKEN_STR_RAW, s); }

        // Multiline string start
        '"""' lws*   { return Token(TOKEN_MSTR_BEGIN, s); }
        '"""' notnl* { return Token(TOKEN_MSTR_BEGIN, s, false); }

        // Legacy multiline strings
        '"%' lws*   { return Token(TOKEN_LSTR_BEGIN, s); }
        '"%' notnl* { return Token(TOKEN_LSTR_BEGIN, s, false); }

        // Interpolated string literals (escapes will be processed later)
        schar = notnl \ [\\"{];
        ["]([\\]notnl|schar)*[\\]? { return Token(TOKEN_STR_SINGLE, s, false); }
        ["]([\\]notnl|schar)*["]   { return Token(TOKEN_STR_SINGLE, s); }
        ["]([\\]notnl|schar)*[{]   { return Token(TOKEN_STR_OPEN,   s); }

        // Regular expression literals (legality will be checked later)
        rchar = notnl \ [\\`$];
        dchar = notnl \ [\\`${];
        [`]([$]*[\\]notnl|rchar|[$]+dchar)*[$\\]*  { return Token(TOKEN_REG_SINGLE, s, false); }
        [`]([$]*[\\]notnl|rchar|[$]+dchar)*[$]*[`] { return Token(TOKEN_REG_SINGLE, s); }
        [`]([$]*[\\]notnl|rchar|[$]+dchar)*[$]+[{] { return Token(TOKEN_REG_OPEN,   s); }

        // Identifiers
        l(n|l)* { return Token(TOKEN_ID, s); }

        // Make forward lexing progress using illegal tokens:
        n(n|l)* { return Token(TOKEN_INTEGER,   s, false); }
        (r|o)+  { return Token(TOKEN_OP_DOLLAR, s, false); }
        w       { return Token(TOKEN_WS,        s, false); }
        *       { return Token(TOKEN_WS,        s, false); }

        [^]     { #error Should be unreachable; lexer makes forward progress on all unicode symbols }
     */
}

Token lex_dstr(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^]([\\]notnl|schar)*[\\]? { return Token(TOKEN_STR_CLOSE, s, false); }
        [^]([\\]notnl|schar)*["]   { return Token(TOKEN_STR_CLOSE, s); }
        [^]([\\]notnl|schar)*[{]   { return Token(TOKEN_STR_MID,   s); }

        * { return Token(TOKEN_WS,        s, false); }
        $ { return Token(TOKEN_STR_CLOSE, s, false); }
     */
}

Token lex_rstr(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$\\]*  { return Token(TOKEN_REG_CLOSE, s, false); }
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$]*[`] { return Token(TOKEN_REG_CLOSE, s); }
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$]+[{] { return Token(TOKEN_REG_MID,   s); }

        * { return Token(TOKEN_WS,        s, false); }
        $ { return Token(TOKEN_REG_CLOSE, s, false); }
     */
}

Token lex_mstr_continue(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        nl         { return Token(TOKEN_NL,       s); }
        lws+       { return Token(TOKEN_WS,       s); }
        '"""'      { return Token(TOKEN_MSTR_END, s); }
        lws+ '"""' { return Token(TOKEN_MSTR_END, s); }

        pchar = notnl \ [%];
        mchar = notnl \ [%{];
        qchar = notnl \ [%"];
        fchar = notnl \ lws \ [%"];

        ["]{1,2}((qchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]*     |
                ((fchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]*     { return Token(TOKEN_MSTR_CONTINUE, s); }
        ["]{1,2}((qchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]+ [{] |
                ((fchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]+ [{] { return Token(TOKEN_MSTR_PAUSE,    s); }

        * { return Token(TOKEN_WS,       s, false); }
        $ { return Token(TOKEN_MSTR_END, s, false); }
     */
}

Token lex_mstr_resume(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^](pchar|[%]+mchar)*[%]*       { return Token(TOKEN_MSTR_RESUME, s); }
        [^](pchar|[%]+mchar)*[%]+ [{]   { return Token(TOKEN_MSTR_MID,    s); }

        * { return Token(TOKEN_WS,          s, false); }
        $ { return Token(TOKEN_MSTR_RESUME, s, false); }
     */
}

Token lex_lstr_continue(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        nl         { return Token(TOKEN_NL,       s); }
        lws+       { return Token(TOKEN_WS,       s); }
        '%"'       { return Token(TOKEN_LSTR_END, s); }
        lws+ '%"'  { return Token(TOKEN_LSTR_END, s); }

         (fchar|[%]+mchar)(pchar|[%]+mchar)*  [%]*     { return Token(TOKEN_LSTR_CONTINUE, s); }
        ((fchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]+ [{] { return Token(TOKEN_LSTR_PAUSE,    s); }

        * { return Token(TOKEN_WS,       s, false); }
        $ { return Token(TOKEN_LSTR_END, s, false); }
     */
}

Token lex_lstr_resume(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^](pchar|[%]+mchar)*[%]*       { return Token(TOKEN_LSTR_RESUME, s); }
        [^](pchar|[%]+mchar)*[%]+ [{]   { return Token(TOKEN_LSTR_MID,    s); }

        * { return Token(TOKEN_WS,          s, false); }
        $ { return Token(TOKEN_LSTR_RESUME, s, false); }
     */
}

Token lex_printable(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        L|M|N|P|S|Zs { return Token(0, s, true);  }
        $            { return Token(0, s, true);  }
        *            { return Token(0, s, false); }
     */
}

IdKind lex_kind(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        "unary "       { return OPERATOR; }
        "binary "      { return OPERATOR; }
        (Lm|M)*(Lt|Lu) { return UPPER; }
        *              { return LOWER; }
        $              { return LOWER; }
     */
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

std::string relex_id(const uint8_t *s, const uint8_t *e) {
    std::string out;
    char *dst;
    ssize_t len;

    len = unicode_escape(s, e, &dst, true); // compat
    if (len >= 0) {
        out.assign(dst, dst+len);
        free(dst);
    } else {
        out.assign(s, e);
    }

    return out;
}

std::string relex_string(const uint8_t *s, const uint8_t *e) {
    std::string out;
    ++s; --e; // Remove the ""    "{    }"   }{

    while (true) {
        const uint8_t *m;
	const uint8_t *h = s;
        /*!re2c
            * { return out; }
            $ { return out; }

            "\\a"                { out.push_back('\a'); continue; }
            "\\b"                { out.push_back('\b'); continue; }
            "\\f"                { out.push_back('\f'); continue; }
            "\\n"                { out.push_back('\n'); continue; }
            "\\r"                { out.push_back('\r'); continue; }
            "\\t"                { out.push_back('\t'); continue; }
            "\\v"                { out.push_back('\v'); continue; }
            "\\" (lws|[{}\\'"?]) { out.append(h+1, s); continue; }
            "\\"  [0-7]{1,3}     { push_utf8(out, lex_oct(h, s)); continue; }
            "\\x" [0-9a-fA-F]{2} { push_utf8(out, lex_hex(h, s)); continue; }
            "\\u" [0-9a-fA-F]{4} { /* !!! ok &= */ push_utf8(out, lex_hex(h, s)); continue; }
            "\\U" [0-9a-fA-F]{8} { /* !!! ok &= */ push_utf8(out, lex_hex(h, s)); continue; }

            [^]                  { out.append(h, s); continue;  }
         */
    }

    return unicode_escape_canon(std::move(out));
}

std::string relex_regexp(uint8_t id, const uint8_t *s, const uint8_t *e) {
    switch (id) {
    case TOKEN_REG_SINGLE: ++s; --e;    break; // skip ``
    case TOKEN_REG_MID:    ++s; e -= 2; break; // skip `${
    case TOKEN_REG_OPEN:   ++s; e -= 2; break; // skip }${
    case TOKEN_REG_CLOSE:  ++s; --e;    break; // skpi }`
    }

    return relex_id(s, e); // !!! unicode_escape_canon
}

op_type op_precedence(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;

    /*!re2c
        o_dot     { return op_type(15, 1); }
        l         { return op_type(14, 1); }
        o_quant   { return op_type(13, 1); }
        o_exp     { return op_type(12, 0); }
        o_muldiv  { return op_type(11, 1); }
        o_addsub  { return op_type(10, 1); }
        o_compare { return op_type( 9, 1); }
        o_inequal { return op_type( 8, 0); }
        o_and     { return op_type( 7, 1); }
        o_or      { return op_type( 6, 1); }
        o_dollar  { return op_type( 5, 0); }
        ":"       { return op_type( 4, 0); }
        o_lrarrow { return op_type( 3, 1); }
        o_eqarrow { return op_type( 2, 0); }
        o_comma   { return op_type( 1, 0); }

        *         { return op_type(-1, -1); }
        $         { return op_type(-1, -1); }
     */
}
