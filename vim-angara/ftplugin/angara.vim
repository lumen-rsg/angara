if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

let s:cpo_save = &cpo
set cpo&vim

setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal commentstring=//\ %s
setlocal formatoptions-=t formatoptions+=croql

setlocal smartindent
setlocal autoindent
setlocal cindent
setlocal cinwords=if,orif,else,for,while,func,class,data,enum,trait,contract,try,catch,match,case

setlocal suffixesadd=.an

let b:undo_ftplugin = "setl com< cms< fo< si< ai< ci< cinw< sua<"

let &cpo = s:cpo_save
unlet s:cpo_save
