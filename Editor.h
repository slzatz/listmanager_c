#ifndef EDITOR_H
#define EDITOR_H

#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
//#include "listmanager.h" //////


struct Diff {
  int fr;
  int fc;
  int repeat;
  std::string command;
  std::vector<std::string> rows;
  std::string inserted_text;
  std::string deleted_text; //deleted chars - being recorded by not used right now or perhaps ever!
  std::vector<std::pair<char, int>> diff; //c = changed; d = deleted; a = added
  std::vector<std::pair<int, std::string>> changed_rows;
  int undo_method; //CHANGE_ROW< REPLACE_NOTE< ADD_ROWS, DELETE_ROWS
  int mode;
};


class Editor {

  public:

    Editor() {
      cx = 0; //cursor x position
      cy = 0; //cursor y position
      fc = 0; //file x position
      fr = 0; //file y position
      line_offset = 0;  //the number of lines of text at the top scrolled off the screen
      prev_line_offset = 0;  //the number of lines of text at the top scrolled off the screen
      //E.coloff = 0;  //should always be zero because of line wrap
      dirty = 0; //has filed changed since last save
      message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
      highlight[0] = highlight[1] = -1;
      mode = 0; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
      command[0] = '\0';
      command_line = "";
      repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y
      indent = 4;
      smartindent = 1; //CTRL-z toggles - don't want on what pasting from outside source
      first_visible_row = 0;
      spellcheck = false;
      highlight_syntax = true; // should only apply to code
      undo_mode = false;

      // ? whether the screen-related stuff should be in one place
      /*
      screenlines = screenlines - 2 - TOP_MARGIN - 10;
      screencols = -2 + screencols/2 - 10;
      total_screenlines = screenlines - 2 - TOP_MARGIN;
      EDITOR_LEFT_MARGIN = screencols/2 + 1;
      */
}

    int cx, cy; //cursor x and y position
    int fc, fr; // file x and y position
    int line_offset; //row the user is currently scrolled to
    int prev_line_offset;
    int coloff; //column user is currently scrolled to
    int screenlines; //number of lines for this Editor
    int screencols;  //number of columns for this Editor
    int left_margin; //can vary (so could TOP_MARGIN - will do that later
    int total_screenlines; //number of lines in display
    std::vector<std::string> rows;
    //std::vector<std::string> prev_rows;
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
    int prev_fr, prev_fc;
    std::string last_typed; //what's typed between going into INSERT mode and leaving INSERT mode
    int repeat;
    int indent;
    int smartindent;
    int first_visible_row;
    int last_visible_row;
    bool spellcheck;
    bool highlight_syntax;
    std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
    static std::string string_buffer; //yanking chars
    static std::vector<std::string> line_buffer; //yanking lines
    std::string search_string; //word under cursor works with *, n, N etc.
    int SMARTINDENT = 4; //should be in config
    int id; //listmanager db id of the row
    //std::deque<std::pair<int,std::string>> undo_deque; //if neg it was a delete
    std::deque<Diff> undo_deque; //if neg it was a delete
    //std::deque<std::pair<int,std::vector<std::string>>> delete_deque; //if neg it was a delete
    int d_index; //undo_deque index
    bool undo_mode;
    std::vector<std::string> snapshot;


/* undo - redo */
    void push_current(void);
    void push_base(void);
    void undo(void);
    void redo(void);
    std::vector<std::string> str2vec(std::string &);

/* EDITOR COMMAND_LINE mode functions */
    void E_write_C(void);
    //below currently done in COMMAND_LINE switch
    //void E_write_close_C(void);
    //void E_quit_C(void);
    //void E_quit0_C(void);
    void E_open_in_vim_C(void);
    void E_spellcheck_C(void);
    void E_persist_C(void);
    void E_readfile_C(void);

    /* EDITOR mode NORMAL functions */
    void E_i(int);
    void E_I(int);
    void E_a(int);
    void E_A(int);
    void E_O(int); //there is an E_O_escape
    void E_o(int);  //there is an E_o_escape
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
    void E_goto_outline(int);

    void E_o_escape(int); //used in INSERT escape and dot
    void E_O_escape(int); //used in INSERT escape and dot
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
    void E_redo(int);

    void editorInsertNewline(int);
    void editorDelChar(void);
    void editorInsertRow(int, std::string);
    int editorIndentAmount(int);
    void editorInsertReturn(void);
    void editorMoveCursorEOL(void);
    void editorMoveCursorBOL(void);
    void editorMoveEndWord(void);
    void editorMoveNextWord(void);
    void editorMoveBeginningWord(void);
    void editorDecorateWord(int);
    void editorDecorateVisual(int);
    void editorSetMessage(const char *fmt, ...);
    void editorSpellCheck(void);
    void editorHighlightWord(int, int, int);
    void editorReadFileIntoNote(const std::string &);
    void editorSaveNoteToFile(const std::string &);
    void editorMoveCursor(int);
    void editorBackspace(void);
    std::string editorRowsToString(void);
    void editorInsertChar(int);
    void editorDelRow(int);
    void editorDelWord(void);
    void editorFindNextWord(void); //apparently doesn't work
    void editorRefreshScreen(bool); // true means need to redraw rows; false just redraw message and command line
    void editorDrawMessageBar(std::string &);
    void editorDrawStatusBar(std::string &);
    void editorDrawRows(std::string &);
    void editorDrawCodeRows(std::string &);
    void editorHighlightWordsByPosition(void);
    void editorYankLine(int);
    void editorYankString(void);
    void paste_line(void);
    void editorIndentRow(void);
    void editorUnIndentRow(void);
    void editorPasteString(void);
    void editorPasteStringVisual(void);
    std::string editorGetWordUnderCursor(void);
    void editorSpellingSuggestions(void);
    void editorChangeCase(void);
    void editorDeleteToEndOfLine(void);
    bool editorScroll(void);
    int editorGetInitialRow(int &); // there should be just one of these
    int editorGetInitialRow(int &, int);  // there should be just one of these
    void editorDotRepeat(int repeat);
    void editorPageUpDown(int);
    void editorDeleteVisual(void);
    void editorPasteLineVisual(void);

    int editorGetScreenXFromRowColWW(int, int);
    int editorGetScreenYFromRowColWW(int, int);
    std::string editorGenerateWWString(void);
    int editorGetLineInRowWW(int, int);
    int editorGetLinesInRowWW(int);
    int editorGetLineCharCountWW(int, int);

    typedef void (Editor::*eefunc)(int);
    std::unordered_map<std::string, eefunc> cmd_map1 = {{"i", &Editor::E_i}, {"I", &Editor::E_I}, {"a", &Editor::E_a}, {"A", &Editor::E_A}};
    std::unordered_map<std::string, eefunc> cmd_map2 = {{"o", &Editor::E_o_escape}, {"O", &Editor::E_O_escape}};
    std::unordered_map<std::string, eefunc> cmd_map3 = {{"x", &Editor::E_x}, {"dw", &Editor::E_dw}, {"daw", &Editor::E_daw}, {"dd", &Editor::E_dd}, {"d$", &Editor::E_d$}, {"de", &Editor::E_de}, {"dG", &Editor::E_dG}};
    std::unordered_map<std::string, eefunc> cmd_map4 = {{"cw", &Editor::E_cw}, {"caw", &Editor::E_caw}, {"s", &Editor::E_s}};
};
#endif
