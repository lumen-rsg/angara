" Vim syntax file
" Language: Angara
" Maintainer: Angara Contributors
" Filenames: *.an

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim
syn match   angaraTodo        contained "\(TODO\|FIXME\|XXX\|HACK\|NOTE\|BUG\)" display
syn region  angaraString      start=+"+ skip=+\\\\\|\\"+ end=+"+ oneline contains=angaraStringEscape
syn match   angaraStringEscape contained +\\[\\\"nrtbfva0-7]\|\\x[0-9a-fA-F]\{2}+ display
syn region  angaraTripleString start=+"""+ end=+"""+ fold contains=angaraStringEscape
syn match   angaraOperator    display "\V+\|-\|*\|%\|=\|<\|>\|!\|&\||\|\^\|~\|?"
syn match   angaraOperator    display "/\ze\([^/*]\|$\)"
syn match   angaraOperator    display "\V==\|!=\|<=\|>=\|&&\|||\|->\|??\|?."
syn match   angaraOperator    display "\V+=\|-=\|*=\|/="
syn match   angaraOperator    display "\V..\|...\|++\|--"
syn match   angaraComment     "//.*" contains=angaraTodo,@Spell
syn region  angaraComment     start="/\*" end="\*/" fold contains=angaraTodo,@Spell
syn match   angaraNumber      display "\v<0[xX][0-9a-fA-F_]+>"
syn match   angaraNumber      display "\v<0[bB][01_]+>"
syn match   angaraNumber      display "\v<[0-9][0-9_]*>"
syn match   angaraFloat       display "\v<[0-9][0-9_]*\.[0-9_]+>"
syn keyword angaraBoolean     true false
syn keyword angaraConstant    nil
syn keyword angaraConditional if orif else match case
syn keyword angaraRepeat      for while in
syn keyword angaraLabel       public private
syn keyword angaraKeyword     break continue return is
syn keyword angaraKeyword     this super
syn keyword angaraDeclaration func function class data enum trait contract
syn keyword angaraDeclaration let const static export
syn keyword angaraDeclaration foreign intrinsic
syn keyword angaraImport      attach from as
syn keyword angaraException   try catch throw
syn keyword angaraType        i8 i16 i32 i64 u8 u16 u32 u64 f32 f64
syn keyword angaraType        int uint float bool string void any
syn keyword angaraType        list map record function c_ptr
syn keyword angaraType        Thread Mutex Exception
syn keyword angaraBuiltin     len typeof spawn sizeof retype
syn match   angaraAnnotation  display "@\h\w*"
syn match   angaraPunctuation display "\v[({})\[\];:,]"
hi def link angaraComment         Comment
hi def link angaraTodo            Todo
hi def link angaraString          String
hi def link angaraTripleString    String
hi def link angaraStringEscape    SpecialChar
hi def link angaraNumber          Number
hi def link angaraFloat           Float
hi def link angaraBoolean         Boolean
hi def link angaraConstant        Constant
hi def link angaraConditional     Conditional
hi def link angaraRepeat          Repeat
hi def link angaraLabel           Label
hi def link angaraKeyword         Statement
hi def link angaraDeclaration     Define
hi def link angaraImport          Include
hi def link angaraException       Exception
hi def link angaraType            Type
hi def link angaraBuiltin         Function
hi def link angaraOperator        Operator
hi def link angaraAnnotation      PreProc
hi def link angaraPunctuation     Delimiter

let b:current_syntax = "angara"

let &cpo = s:cpo_save
unlet s:cpo_save
