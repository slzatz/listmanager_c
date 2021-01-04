#include <unordered_map>
#include <string>

//typedef void (*pfunc)(int);
using pfunc = void (*)(int);

/* OUTLINE COMMAND_LINE functions */
void F_open(int);
void F_openfolder(int);
void F_openkeyword(int);
void F_deletekeywords(int); // pos not used
void F_addkeyword(int); 
void F_keywords(int); 
void F_write(int); // pos not used
void F_x(int); // pos not used
void F_refresh(int); // pos not used
void F_new(int); // pos not used
void F_edit(int); // pos not used
void F_edit2(int); // pos not used
void F_folders(int); 
void F_contexts(int); 
void F_recent(int); // pos not used
void F_linked(int); // pos not used
void F_find(int); 
void F_sync(int); // pos not used
void F_sync_test(int); // pos not used
void F_updatefolder(int); // pos not used
void F_updatecontext(int); // pos not used
void F_delmarks(int); // pos not used
void F_savefile(int);
void F_sort(int);
void F_showall(int); // pos not used
void F_syntax(int);
void F_set(int);
void F_open_in_vim(int); // pos not used
void F_join(int);
void F_saveoutline(int);
void F_readfile(int pos);
void F_valgrind(int pos); // pos not used
void F_quit_app(int pos); // pos not used
void F_quit_app_ex(int pos); // pos not used
void F_help(int pos);
//void F_persist(int pos); // pos not used
void F_copy_entry(int);
//void F_restart_lsp(int);
//void F_shutdown_lsp(int);
void F_lsp_start(int);
void F_launch_lm_browser(int);
void F_quit_lm_browser(int);
void F_resize(int);
void F_createLink(int);
void F_getLinked(int);


/* OUTLINE COMMAND_LINE mode command lookup */
std::unordered_map<std::string, pfunc> cmd_lookup {
  {"open", F_open}, //open_O
  {"o", F_open},
  {"openfolder", F_openfolder},
  {"of", F_openfolder},
  {"openkeyword", F_openkeyword},
  {"ok", F_openkeyword},
  {"deletekeywords", F_deletekeywords},
  {"delkw", F_deletekeywords},
  {"delk", F_deletekeywords},
  {"addkeywords", F_addkeyword},
  {"addkw", F_addkeyword},
  {"addk", F_addkeyword},
  {"k", F_keywords},
  {"keyword", F_keywords},
  {"keywords", F_keywords},
  {"write", F_write},
  {"w", F_write},
  {"x", F_x},
  {"refresh", F_refresh},
  {"r", F_refresh},
  {"n", F_new},
  {"new", F_new},
  {"e", F_edit},
  {"edit", F_edit},
  {"contexts", F_contexts},
  {"context", F_contexts},
  {"c", F_contexts},
  {"folders", F_folders},
  {"folder", F_folders},
  {"f", F_folders},
  {"recent", F_recent},
  {"linked", F_linked},
  {"l", F_linked},
  {"related", F_linked},
  {"find", F_find},
  {"fin", F_find},
  {"search", F_find},
  {"sync", F_sync},
  {"test", F_sync_test},
  {"updatefolder", F_updatefolder},
  {"uf", F_updatefolder},
  {"updatecontext", F_updatecontext},
  {"uc", F_updatecontext},
  {"delmarks", F_delmarks},
  {"delm", F_delmarks},
  {"save", F_savefile},
  {"sort", F_sort},
  {"show", F_showall},
  {"showall", F_showall},
  {"set", F_set},
  {"syntax", F_syntax},
  {"vim", F_open_in_vim},
  {"join", F_join},
  {"saveoutline", F_saveoutline},
  //{"readfile", F_readfile},
  //{"read", F_readfile},
  {"valgrind", F_valgrind},
  {"quit", F_quit_app},
  {"q", F_quit_app},
  {"quit!", F_quit_app_ex},
  {"q!", F_quit_app_ex},
  //{"merge", F_merge},
  {"help", F_help},
  {"h", F_help},
  {"copy", F_copy_entry},
  //{"restart_lsp", F_restart_lsp},
  //{"shutdown_lsp", F_shutdown_lsp},
  {"lsp", F_lsp_start},
  {"browser", F_launch_lm_browser},
  {"launch", F_launch_lm_browser},
  {"quitb", F_quit_lm_browser},
  {"quitbrowser", F_quit_lm_browser},
  {"createlink", F_createLink},
  {"getlinked", F_getLinked},
  {"resize", F_resize}

};
