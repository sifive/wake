" Vim syntax file
" Language:    Wake
" Maintainers: Jack Koenig
" Last Change: 2019 January 9
"   2018 October 23 : Initial Wake Syntax File
"   2019 January 9  : Add highlighting for Types
"   2019 January 9  : Add highlighting for raw strings

if version < 600
    syntax clear
elseif exists("b:current_syntax")
    finish
endif

" comments
syn match lineComment "#.*"

" Keywords
" TODO Should important globals from prim.wake we marked keywords?
syn keyword wakeKeyword if then else here global export import package from type topic binary unary subscribe match require data tuple prim

" definitions
" TODO How can we handle `def x + y` = syntax?
syn keyword wakeDef def publish target nextgroup=wakeOperator,wakeLowerIdentifier skipwhite
syn match wakeDefName "[^ =:;()[]\+" contained skipwhite
syn match wakeOperator "[+-=$]\+" contained
syn match wakeLowerIdentifier "[a-z][A-Za-z0-9_]*" contained
syn match wakeUpperIdentifier "\<[A-Z][A-Za-z0-9_]*"

" Strings
" string literals with escapes
syn region wakeString start=/\v"/ skip=/\v\\./ end=/\v"/ contains=wakeStringEscape
syn match wakeStringEscape "\\[nrfvb\\\"]" contained
syn region wakeRawString start=/\v'/ skip=/\v\\./ end=/\v'/
syn region wakeRegexString start=/\v`/ skip=/\v\\./ end=/\v`/

" Numeric literals
syn match wakeDecNumber /\<[1-9][0-9]*\>/
syn match wakeBinNumber /\<0b[01_]\+\>/
syn match wakeOctNumber /\<0[0-7_]*\>/
syn match wakeHexNumber /\<0x[0-9a-fA-F_]\+\>/
syn match wakeDecFloatNumber /\<\([1-9][0-9_]*\|0\)\.[0-9]\+\([eE][+-]\?[0-9_]\+\)\?\>/
syn match wakeDecFloatNumber /\<\([1-9][0-9_]*\|0\)[eE][+-]\?[0-9_]\+\>/
syn match wakeHexFloatNumber /\<0x[0-9a-fA-F_]\+\.[0-9a-fA-F_]\+\([pP][+-]\?[0-9a-fA-F_]\+\)\?\>/
syn match wakeHexFloatNumber /\<0x[0-9a-fA-F_]\+[pP][+-]\?[0-9a-fA-F_]\+\>/

"===== Links =====
hi link wakeKeyword Keyword

hi link lineComment Comment

hi link wakeDef Keyword
hi link wakeDefName Function
hi link wakeLowerIdentifier Function
hi link wakeOperator Function

hi link wakeUpperIdentifier Type

hi link wakeString String
hi link wakeStringEscape Special
hi link wakeRawString String
hi link wakeRegexString String
hi link wakeDecNumber wakeNumber
hi link wakeBinNumber wakeNumber
hi link wakeOctNumber wakeNumber
hi link wakeHexNumber wakeNumber
hi link wakeNumber Number
hi link wakeDecFloatNumber wakeFloatNumber
hi link wakeHexFloatNumber wakeFloatNumber
hi link wakeFloatNumber Float

