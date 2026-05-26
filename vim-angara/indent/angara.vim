" Angara indent file
" Based on the default C-like indenting

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal cindent
setlocal cinkeys=0{,0},0),0[,0],!^F,o,O,e,0#
setlocal cinwords=if,orif,else,for,while,func,class,data,enum,trait,contract,try,catch,match,case

let b:undo_indent = "setl ci< cink< cinw<"
