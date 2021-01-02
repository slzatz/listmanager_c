#include "Editor.h"
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this

using eefunc = void (Editor::*)(int);
using efunc = void (Editor::*)(void);

/* EDITOR NORMAL mode command lookup */
std::unordered_map<std::string, eefunc> e_lookup {
  {"i", &Editor::E_i}, 
  {"I", &Editor::E_I},
  {"a", &Editor::E_a},
  {"A", &Editor::E_A},
  {"o", &Editor::E_o}, //note there is an E_o_escape
  {"O", &Editor::E_O}, //note there is an E_O_escape
  {"x", &Editor::E_x},
  {"dw", &Editor::E_dw},
  {"daw", &Editor::E_daw},
  {"de", &Editor::E_de},
  {"dG", &Editor::E_dG},
  {"d$", &Editor::E_d$},
  {"dd", &Editor::E_dd},
  {"cw", &Editor::E_cw},
  {"caw", &Editor::E_caw},
  {"s", &Editor::E_s},
  {"~", &Editor::E_tilde},
  {"J", &Editor::E_J},
  {"w", &Editor::E_w},
  {"e", &Editor::E_e},
  {"b", &Editor::E_b},
  {"0", &Editor::E_0},
  {"$", &Editor::E_$},
  {"r", &Editor::e_replace},
  {"[s", &Editor::E_next_misspelling},
  {"]s", &Editor::E_prev_misspelling},
  {"z=", &Editor::E_suggestions},
  //{":", &Editor::E_change2command_line}, //commented out because in NORMAL switch statement
  {"V", &Editor::E_change2visual_line},
  {"v", &Editor::E_change2visual},
  {{0x16}, &Editor::E_change2visual_block},
  {"p", &Editor::E_paste},
  {"*", &Editor::E_find},
  {"n", &Editor::E_find_next_word},
 // {"u", &Editor::E_undo}, //currently in case NORMAL but not sure it has to be
 // {{CTRL_KEY('r')}, &Editor::E_redo}, //ditto - need to be in move_only -> no_edit_cmds
  {".", &Editor::editorDotRepeat},
  {">>", &Editor::E_indent},
  {"<<", &Editor::E_unindent},
  {{0x2}, &Editor::E_bold},
  {{0x5}, &Editor::E_emphasis},
  {{0x9}, &Editor::E_italic},
  {"yy", &Editor::editorYankLine},
  {"gg", &Editor::E_gg},
  {"G", &Editor::E_G},
  {{0x1A}, &Editor::E_toggle_smartindent},
  {{CTRL_KEY('p')}, &Editor::E_CTRL_P},
 // {{0x8}, &Editor::E_goto_outline},
  {"%", &Editor::E_move_to_matching_brace},
  {{CTRL_KEY('w'), 'H'}, &Editor::E_move_output_window_right},
  {{CTRL_KEY('w'), 'J'}, &Editor::E_move_output_window_below}
  //{{0x13}, &Editor::E_save_note}
};


/* EDITOR COMMAND_LINE mode lookup */
std::unordered_map<std::string, efunc> E_lookup_C {
  {"write", &Editor::E_write_C},
  {"w", &Editor::E_write_C},
 /* all below handled (right now) in editor command line switch statement*/
 // {"x", &Editor::E_write_close_C},
 // {"quit", &Editor::E_quit_C},
 // {"q",&Editor:: E_quit_C},
 // {"quit!", &Editor::E_quit0_C},
 // {"q!", &Editor::E_quit0_C},
  {"vim", &Editor::E_open_in_vim_C},
  {"spell",&Editor:: E_spellcheck_C},
  {"spellcheck", &Editor::E_spellcheck_C},
  {"read", &Editor::E_readfile_C},
  {"readfile", &Editor::E_readfile_C},

  {"compile", &Editor::E_compile_C},
  {"c", &Editor::E_compile_C},
  {"make", &Editor::E_compile_C},
  {"r", &Editor::E_runlocal_C}, // this does change the text/usually COMMAND_LINE doesn't
  {"runl", &Editor::E_runlocal_C}, // this does change the text/usually COMMAND_LINE doesn't
  {"runlocal", &Editor::E_runlocal_C}, // this does change the text/usually COMMAND_LINE doesn't
  {"run", &Editor::E_runlocal_C}, //compile and run on Compiler Explorer 
  {"rr", &Editor::E_run_code_C}, //compile and run on Compiler Explorer 
  {"runremote", &Editor::E_run_code_C}, //compile and run on Compiler Explorer 
  {"save", &Editor::E_save_note},
  {"savefile", &Editor::E_save_note}
};
