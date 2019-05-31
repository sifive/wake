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
syn keyword wakeKeyword if then else here global subscribe match data

" definitions
" TODO How can we handle `def x + y` = syntax?
syn keyword wakeDef def publish nextgroup=wakeOperator,wakeLowerIdentifier skipwhite
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

