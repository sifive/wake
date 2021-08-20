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

#include "lexer.h"
#include "parser.h"

/*!include:re2c "unicode_categories.re" */

/*!re2c
    re2c:tags:expression = "in.@@";
    re2c:define:YYCTYPE = "uint8_t";
    re2c:define:YYCURSOR = s;
    re2c:define:YYLIMIT = e;
    re2c:define:YYMARKER = m;
    re2c:define:YYCTXMARKER = c;
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
        '"""' notnl* { return Token(TOKEN_MSTR_BEGIN, s); }

        // Legacy multiline strings
        '"%' notnl* { return Token(TOKEN_LSTR_BEGIN, s); }

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

        * { return Token(TOKEN_STR_CLOSE, s, false); }
        $ { return Token(TOKEN_STR_CLOSE, s, false); }
     */
}

Token lex_rstr(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$\\]*  { return Token(TOKEN_REG_CLOSE, s, false); }
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$]*[`] { return Token(TOKEN_REG_CLOSE, s); }
        [^]([$]*[\\]notnl|rchar|[$]+dchar)*[$]+[{] { return Token(TOKEN_REG_MID,   s); }

        * { return Token(TOKEN_REG_CLOSE, s, false); }
        $ { return Token(TOKEN_REG_CLOSE, s, false); }
     */
}

Token lex_mstr_continue(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        nl    { return Token(TOKEN_NL,       s); }
        lws+  { return Token(TOKEN_WS,       s); }
        '"""' { return Token(TOKEN_MSTR_END, s); }

        pchar = notnl \ [%];
        mchar = notnl \ [%{];
        fchar = pchar \ lws;

         (fchar|[%]+mchar)(pchar|[%]+mchar)*  [%]*     { return Token(TOKEN_MSTR_CONTINUE, s); }
        ((fchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]+ [{] { return Token(TOKEN_MSTR_PAUSE,    s); }

        * { return Token(TOKEN_MSTR_END, s, false); }
        $ { return Token(TOKEN_MSTR_END, s, false); }
     */
}

Token lex_mstr_resume(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^](pchar|[%]+mchar)*[%]*       { return Token(TOKEN_MSTR_RESUME, s); }
        [^](pchar|[%]+mchar)*[%]+ [{]   { return Token(TOKEN_MSTR_MID,    s); }

        * { return Token(TOKEN_MSTR_RESUME, s, false); }
        $ { return Token(TOKEN_MSTR_RESUME, s, false); }
     */
}

Token lex_lstr_continue(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        nl    { return Token(TOKEN_NL,       s); }
        lws+  { return Token(TOKEN_WS,       s); }
        '%"'  { return Token(TOKEN_LSTR_END, s); }

         (fchar|[%]+mchar)(pchar|[%]+mchar)*  [%]*     { return Token(TOKEN_LSTR_CONTINUE, s); }
        ((fchar|[%]+mchar)(pchar|[%]+mchar)*)?[%]+ [{] { return Token(TOKEN_LSTR_PAUSE,    s); }

        * { return Token(TOKEN_LSTR_END, s, false); }
        $ { return Token(TOKEN_LSTR_END, s, false); }
     */
}

Token lex_lstr_resume(const uint8_t *s, const uint8_t *e) {
    const uint8_t *m;
    /*!re2c
        [^](pchar|[%]+mchar)*[%]*       { return Token(TOKEN_LSTR_RESUME, s); }
        [^](pchar|[%]+mchar)*[%]+ [{]   { return Token(TOKEN_LSTR_MID,    s); }

        * { return Token(TOKEN_LSTR_RESUME, s, false); }
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
