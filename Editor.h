#ifndef EDITOR_H
#define EDITOR_H

#include <vector>
#include <string>
//#include "listmanager.h" //////

class Editor {

  public:

    Editor() {}

    int cx, cy; //cursor x and y position
    int fc, fr; // file x and y position
    int line_offset; //row the user is currently scrolled to
    int prev_line_offset;
    int coloff; //column user is currently scrolled to
    int screenlines; //number of lines in the display
    int screencols;  //number of columns in the display
    std::vector<std::string> rows;
    std::vector<std::string> prev_rows;
    int dirty; //file changes since last save
    char message[120]; //status msg is a character array max 80 char
    int highlight[2];
    int vb0[3];
    int mode;
    // probably OK that command is a char[] and not a std::string
    char command[10]; // right now includes normal mode commands and command line commands
    std::string command_line; //for commands on the command line; string doesn't include ':'
    //int last_command; //will use the number equivalent of the command
    std::string last_command; 
    int last_repeat;
    std::string last_typed; //what's typed between going into INSERT mode and leaving INSERT mode
    int repeat;
    int indent;
    int smartindent;
    int first_visible_row;
    int last_visible_row;
    bool spellcheck;
    bool highlight_syntax;
    std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
    std::string string_buffer; //yanking chars
    std::string search_string; //word under cursor works with *, n, N etc.
    int SMARTINDENT = 4; //should be in config
    int id; //listmanager db id of the row


/* EDITOR COMMAND_LINE mode functions */
    void E_write_C(void);
    void E_write_close_C(void);
    void E_quit_C(void);
    void E_quit0_C(void);
    void E_open_in_vim_C(void);
    void E_spellcheck_C(void);
    void E_persist_C(void);

    /* EDITOR mode NORMAL functions */
    void E_i(int);
    void E_I(int);
    void E_a(int);
    void E_A(int);
    void E_O(int);
    void E_o(int);
    void E_dw(int);
    void E_daw(int);
    void E_dd(int);
    void E_de(int);
    void E_dG(int);
    void E_cw(int);
    void E_caw(int);
    void E_s(int);
    void E_x(int);
    void E_d$(int);
    void E_w(int);
    void E_b(int);
    void E_e(int);
    void E_0(int);
    void E_$(int);
    void E_replace(int);
    void E_J(int);
    void E_tilde(int);
    void E_indent(int);
    void E_unindent(int);
    void E_bold(int);
    void E_emphasis(int);
    void E_italic(int);
    void E_gg(int);
    void E_G(int);
    void E_toggle_smartindent(int);
    void E_save_note(int);
    void E_change_case(int);

    void e_o(int);
    void e_O(int);
    void e_replace(int);
    void E_next_misspelling(int);
    void E_prev_misspelling(int);
    void E_suggestions(int);
    void E_change2command_line(int);
    void E_change2visual_line(int);
    void E_change2visual(int);
    void E_change2visual_block(int);
    void E_paste(int);
    void E_find(int);
    void E_find_next_word(int);
    void E_undo(int);

};
#endif
