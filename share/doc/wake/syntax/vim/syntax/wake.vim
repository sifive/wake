" Vim syntax file
" Language:    Wake
" Maintainers: Jack Koenig
" Last Change: 2019 January 9
"   2018 October 23 : Initial Wake Syntax File
"   2019 January 9  : Add highlighting for Types
"   2019 January 9  : Add highlighting for raw strings
"   2023 July 20    : Add highlighting for macros and specify conditionals

if version < 600
    syntax clear
elseif exists("b:current_syntax")
    finish
endif

" comments
syn match lineComment "#.*"

" Keywords
" TODO Should important globals from prim.wake we marked keywords?
syn keyword wakeKeyword global type topic subscribe data tuple prim
syn match wakeMacro "here\|@here\|@line\|@file\|@!"
syn keyword wakeConditional if then else match require

syn keyword wakeOperatorModifier binary unary
syn keyword wakeInclude package export import from
syn keyword wakeException unreachable

" definitions
" TODO How can we handle `def x + y` = syntax?
syn keyword wakeDef def publish target topic nextgroup=wakeOperatorModifier,wakeOperator,wakeLowerIdentifier skipwhite
syn match wakeDefName "[^ =:;()[]\+" contained skipwhite
syn match wakeOperator "[+-=$]\+" contained
syn match wakeLowerIdentifier "[a-z][A-Za-z0-9_]*" contained
syn match wakeUpperIdentifier "\<[A-Z][A-Za-z0-9_]*"

" Strings
" string literals with escapes
syn region wakeString start=/\v"/ skip=/\v\\./ end=/\v"/ contains=wakeStringEscape,wakeMacro
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
hi link wakeConditional Conditional
hi link wakeMacro Macro

hi link lineComment Comment

hi link wakeDef Keyword
hi link wakeDefName Function
hi link wakeLowerIdentifier Function
hi link wakeOperatorModifier Keyword
hi link wakeOperator Function
hi link wakeInclude Include
hi link wakeException Exception

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

