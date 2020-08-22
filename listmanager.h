#ifndef LISTMANAGER_H
#define LISTMANAGER_H

#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits
#define OUTLINE_LEFT_MARGIN 2
#define OUTLINE_RIGHT_MARGIN 18 // need this if going to have modified col
#define TOP_MARGIN 1
#define DEBUG 0
#define UNUSED(x) (void)(x)
#define MAX 500 // max rows to bring back
#define TZ_OFFSET 4 // time zone offset - either 4 or 5
#define SCROLL_DOWN 0
#define SCROLL_UP 1

// to use GIT_BRANCH in makefile (from cmake)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <Python.h>
#include <sys/ioctl.h>
#include <csignal>
#include <termios.h>
#include <libpq-fe.h>
#include <sqlite3.h>
#include "inipp.h" // https://github.com/mcmtroffaes/inipp
#include "process.h" // https://github.com/skystrife/procxx

#include <string>
#include <string_view> 
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <set>

#if __has_include (<nuspell/dictionary.hxx>)
  #include <nuspell/dictionary.hxx>
  #include <nuspell/finder.hxx>
  #define NUSPELL
#endif

#include <zmq.hpp>

#include <memory>

#include <fcntl.h>
#include <unistd.h>

/*
#include "Editor.h"
class Editor;
Editor E;
Editor *p;
*/

//
//
extern "C" {
#include <mkdio.h>
}
const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
const std::string DB_INI = "db.ini";
const std::string CURRENT_NOTE_FILE = "current.html";
const std::string META_FILE = "assets/meta.html";
/*
std::string system_call = "./lm_browser " + CURRENT_NOTE_FILE;
std::string meta;
int which_db;
int EDITOR_LEFT_MARGIN;
struct termios orig_termios;
int screenlines, screencols, new_screenlines, new_screencols;
std::stringstream display_text;
int initial_file_row = 0; //for arrowing or displaying files
bool editor_mode;
std::string search_terms;
std::vector<std::vector<int>> word_positions;
std::vector<int> fts_ids;
int fts_counter;
*/
extern bool editor_mode;
//std::string search_string; //word under cursor works with *, n, N etc.
/*
std::vector<std::string> line_buffer; //yanking lines
std::string string_buffer; //yanking chars
std::map<int, std::string> fts_titles;
std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]
std::vector<std::string> task_keywords;
std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
std::set<int> unique_ids; //used in unique_data_callback
std::vector<std::string> command_history; // the history of commands to make it easier to go back to earlier views
std::vector<std::string> page_history; // the history of commands to make it easier to go back to earlier views
size_t cmd_hx_idx = 0;

size_t page_hx_idx = 0;
std::map<int, std::string> html_files;
bool lm_browser = true;
*/
extern bool lm_browser;
extern std::map<int, std::string> html_files;

const std::string COLOR_1 = "\x1b[0;31m"; //red
const std::string COLOR_2 = "\x1b[0;32m"; //green
const std::string COLOR_3 = "\x1b[0;33m"; //yellow
const std::string COLOR_4 = "\x1b[0;34m"; //blue
const std::string COLOR_5 = "\x1b[0;35m"; //magenta
const std::string COLOR_6 = "\x1b[0;36m"; //cyan
const std::string COLOR_7 = "\x1b[0;37m"; //White
/*
int SMARTINDENT = 4; //should be in config
int temporary_tid = 99999;
int link_id = 0;
char link_text[20];

int current_task_id;
std::unordered_set<int> marked_entries;
*/

enum outlineKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000, //would have to be < 127 to be chars
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  SHIFT_TAB
};

enum Mode {
  NORMAL, // = 0,
  INSERT, // = 1,
  COMMAND_LINE, // = 2, //note: if no rows as result of search put in COMMAND_LINE mode
  VISUAL_LINE, // = 3, // only editor mode
  VISUAL, // = 4,
  REPLACE, // = 5,
  DATABASE, // = 6, // only outline mode
  FILE_DISPLAY,// = 7, // only outline mode
  NO_ROWS,// = 8
  VISUAL_BLOCK,
  SEARCH,
  ADD_KEYWORD  
};

enum View {
  TASK,
  CONTEXT,
  FOLDER,
  KEYWORD
};

enum TaskView {
  BY_CONTEXT,
  BY_FOLDER,
  BY_KEYWORD,
  BY_JOIN,
  BY_RECENT,
  BY_SEARCH
};

enum DB {
  SQLITE,
  POSTGRES
};

const std::string mode_text[] = {
                        "NORMAL",
                        "INSERT",
                        "COMMAND LINE",
                        "VISUAL LINE",
                        "VISUAL",
                        "REPLACE",
                        "DATABASE",
                        "FILE DISPLAY",
                        "NO ROWS",
                        "VISUAL_BLOCK",
                        "SEARCH",
                        "ADD_KEYWORD"  
                       }; 

constexpr char BASE_DATE[] = "1970-01-01 00:00";

/*
struct sqlite_db {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};

struct sqlite_db S;
*/

typedef struct orow {
  std::string title;
  std::string fts_title;
  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  //bool code; //new to say the note is actually code
  char modified[16];

  // note the members below are temporary editing flags
  // and don't need to be reflected in database
  bool dirty;
  bool mark;
} orow;

struct outlineConfig {
  int cx, cy; //cursor x and y position
  unsigned int fc, fr; // file x and y position
  unsigned int rowoff; //the number of rows scrolled (aka number of top rows now off-screen
  unsigned int coloff; //the number of columns scrolled (aka number of left rows now off-screen
  unsigned int screenlines; //number of lines in the display available to text
  unsigned int screencols;  //number of columns in the display available to text
  std::vector<orow> rows;
  std::string context;
  std::string folder;
  std::string keyword;
  std::string sort;
  char message[100]; //status msg is a character array - enlarging to 200 did not solve problems with seg faulting
  int highlight[2];
  int mode;
  int last_mode;
  // probably ok that command isn't a std::string although it could be
  char command[10]; // doesn't include command_line commands
  std::string command_line; //for commands on the command line; string doesn't include ':'
  int repeat;
  bool show_deleted;
  bool show_completed;
  int view; // enum TASK, CONTEXT, FOLDER, SEARCH
  int taskview; // enum BY_CONTEXT, BY_FOLDER, BY_RECENT, BY_SEARCH
};

extern struct outlineConfig O;
/*
struct editorConfig {
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
};

static struct editorConfig E;

struct flock lock;
*/

/* note that you can call these either through explicit dereference: (*get_note)(4328)
 * or through implicit dereference: get_note(4328)
*/

int getWindowSize(int *, int *);

// I believe this is being called at times redundantly before editorEraseScreen and outlineRefreshScreen
void EraseScreenRedrawLines(void);

void outlineProcessKeypress(int = 0);
bool editorProcessKeypress(void);

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
void F_merge(int pos); // pos not used
void F_help(int pos);
void F_persist(int pos); // pos not used
void F_clear(int pos); // pos not used

/* EDITOR COMMAND_LINE functions 
void E_write_C(void);
void E_write_close_C(void);
void E_quit_C(void);
void E_quit0_C(void);
void E_open_in_vim_C(void);
void E_spellcheck_C(void);
void E_persist_C(void);
*/

/* OUTLINE mode NORMAL functions */
void return_N(void);
void w_N(void);
void insert_N(void);
void s_N(void);
void x_N(void);
void r_N(void);
void tilde_N(void);
void a_N(void);
void A_N(void);
void b_N(void);
void e_N(void);
void zero_N(void);
void dollar_N(void);
void I_N(void);
void G_N(void);
void O_N(void);
void colon_N(void);
void v_N(void);
void p_N(void);
void asterisk_N(void);
void m_N(void);
void n_N(void);
void u_N(void);
void caret_N(void);
void dd_N(void);
void star_N(void);
void completed_N(void);
void daw_N(void);
void caw_N(void);
void dw_N(void);
void cw_N(void);
void de_N(void);
void d$_N(void);
void gg_N(void);
void gt_N(void);
void edit_N(void);

void navigate_page_hx(int direction);
void navigate_cmd_hx(int direction);

//Outline Prototypes
void outlineShowMessage(const char *fmt, ...);
void outlineRefreshScreen(void); //erases outline area but not sort/time screen columns
void outlineDrawStatusBar(void);
void outlineDrawMessageBar(std::string&);
void outlineDelWord();
void outlineMoveCursor(int key);
void outlineBackspace(void);
void outlineDelChar(void);
void outlineDeleteToEndOfLine(void);
void outlineYankLine(int n);
void outlinePasteString(void);
void outlineYankString();
void outlineMoveCursorEOL();
void outlineMoveBeginningWord();
void outlineMoveEndWord(); 
void outlineMoveEndWord2(); //not 'e' but just moves to end of word even if on last letter
//void outlineMoveNextWord();// now w_N
void outlineGetWordUnderCursor();
void outlineFindNextWord();
void outlineChangeCase();
void outlineInsertRow(int, std::string&&, bool, bool, bool, const char *);
void outlineDrawRows(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawKeywords(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawSearchRows(std::string&); //ditto
void outlineScroll(void);
void outlineSave(const std::string &);
void return_cursor(void);

//Database-related Prototypes
void db_open(void);
void update_task_context(std::string &, int);
void update_task_folder(std::string &, int);
int get_id(void);
void get_note(int);
void update_row(void);
void update_rows(void);
void toggle_deleted(void);
void toggle_star(void);
void toggle_completed(void);
void touch(void);
int insert_row(orow&);
int insert_container(orow&);
int insert_keyword(orow &);
void update_container(void);
void update_keyword(void);
void get_items(int); 
void get_containers(void); //has an if that determines callback: context_callback or folder_callback
std::pair<std::string, std::vector<std::string>> get_task_keywords(void); // puts them in comma delimited string
std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string
void update_note(void); 
//void solr_find(void);
void search_db(std::string); //void fts5_sqlite(std::string);
void search_db2(std::string); //just searches documentation - should be combined with above
void get_items_by_id(std::stringstream &);
int get_folder_tid(int); 
void map_context_titles(void);
void map_folder_titles(void);
void add_task_keyword(std::string &, int);
void add_task_keyword(int, int);
//void delete_task_keywords(void); -> F_deletekeywords
void display_item_info(int); 
void display_item_info(void); //ctrl-i in NORMAL mode 0x9
void display_item_info_pg(int);
void display_container_info(int);
int keyword_exists(std::string &);  
int folder_exists(std::string &);
int context_exists(std::string &);

//sqlite callback functions
typedef int (*sq_callback)(void *, int, char **, char **); //sqlite callback type

int fts5_callback(void *, int, char **, char **);
int data_callback(void *, int, char **, char **);
int context_callback(void *, int, char **, char **);
int folder_callback(void *, int, char **, char **);
int keyword_callback(void *, int, char **, char **);
int context_titles_callback(void *, int, char **, char **);
int folder_titles_callback(void *, int, char **, char **);
int by_id_data_callback(void *, int, char **, char **);
int note_callback(void *, int, char **, char **);
int display_item_info_callback(void *, int, char **, char **);
int task_keywords_callback(void *, int, char **, char **);
int keyword_id_callback(void *, int, char **, char **);//? not in use
int container_id_callback(void *, int, char **, char **);
int rowid_callback(void *, int, char **, char **);
int offset_callback(void *, int, char **, char **);
int folder_tid_callback(void *, int, char **, char **); 
int context_info_callback(void *, int, char **, char **); 
int folder_info_callback(void *, int, char **, char **); 
int keyword_info_callback(void *, int, char **, char **);
int count_callback(void *, int, char **, char **);
int unique_data_callback(void *, int, char **, char **);

void synchronize(int);

//Editor Word Wrap
int editorGetScreenXFromRowColWW(int, int);
int editorGetScreenYFromRowColWW(int, int); //used by editorScroll
int editorGetLineInRowWW(int, int);
int editorGetLinesInRowWW(int);
int editorGetLineCharCountWW(int, int);
std::string editorGenerateWWString(void); // only used by editorDrawCodeRows
void editorDrawCodeRows(std::string &);
int editorGetInitialRow(int &);
int editorGetInitialRow(int &, int);

//Editor Prototypes
void editorDrawRows(std::string &); //erases lines to right as it goes
void editorDrawMessageBar(std::string &);
//void editorDrawStatusBar(std::string &); //only one status bar
void editorSetMessage(const char *fmt, ...);
bool editorScroll(void);
void editorRefreshScreen(bool); // true means need to redraw rows; false just redraw message and command line
void editorInsertReturn(void);
void editorDecorateWord(int);
void editorDecorateVisual(int);
void editorDelWord(void);
void editorDelRow(int);
void editorIndentRow(void);
void editorUnIndentRow(void);
int editorIndentAmount(int);
void editorMoveCursor(int);
void editorBackspace(void);
void editorDelChar(void);
void editorDeleteToEndOfLine(void);
void editorDeleteVisual(void);
void editorYankLine(int);
void editorPasteLine(void);
void editorPasteLineVisual(void);
void editorPasteString(void);
void editorPasteStringVisual(void);
void editorYankString(void); //only for VISUAL mode
void editorMoveCursorEOL(void);
void editorMoveCursorBOL(void);
void editorMoveBeginningWord(void);
void editorMoveEndWord(void); 
void editorMoveEndWord2(void); //not 'e' but just moves to end of word even if on last letter
void editorMoveNextWord(void);
//void editorMarkupLink(void); //no longer doing links this way but preserving code
std::string editorGetWordUnderCursor(void);
void editorFindNextWord(void);
void editorChangeCase(void);
void editorRestoreSnapshot(void); 
void editorCreateSnapshot(void); 
void editorInsertRow(int, std::string);
void editorInsertChar(int);
void editorDisplayFile(void);
void editorEraseScreen(void); //erases the note section; redundant if just did an EraseScreenRedrawLines
void editorInsertNewline(int);
void editorSpellingSuggestions(void);
void editorDotRepeat(int);

void editorHighlightWordsByPosition(void);
void editorSpellCheck(void);
void editorHighlightWord(int, int, int);


std::string editorRowsToString(void);
std::string generate_html(void);
std::string generate_html2(void);
void generate_persistent_html_file(int);
void load_meta(void);
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void editorSaveNoteToFile(const std::string &);
void editorReadFile(const std::string &);
void editorReadFileIntoNote(const std::string &); 

void update_solr(void); //works but not in use
void open_in_vim(void);

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

void e_o(int);
void e_O(int);
void e_replace(int);
void f_next_misspelling(int);
void f_prev_misspelling(int);
void f_suggestions(int);
void f_change2command_line(int);
void f_change2visual_line(int);
void f_change2visual(int);
void f_change2visual_block(int);
void f_paste(int);
void f_find(int);
void f_find_next_word(int);
void f_undo(int);

//std::unordered_set<std::string> insert_cmds = {"I", "i", "A", "a", "o", "O", "s", "cw", "caw"};

//these are not just move only but commands to don't require redrawing text
//although the cursor and the message line will still be redrawn
//more accurate would be something like no_redraw
//will still redraw if page needs scrolling
//std::unordered_set<std::string> move_only = {"w", "e", "b", "0", "$", ":", "*", "n", "[s","]s", "z=", "gg", "G", "yy"};

/*
std::unordered_set<int> navigation = {
         ARROW_UP,
         ARROW_DOWN,
         ARROW_LEFT,
         ARROW_RIGHT,
         'h',
         'j',
         'k',
         'l'
};
*/

//static const std::set<std::string> cmd_set1a = {"I", "i", "A", "a"};

typedef void (*pfunc)(int);
typedef void (*zfunc)(void);
//typedef void (Editor::*efunc)(void);

/* note that if the key to the unordered_maps was an array
 * I'd have to implement the hash function whereas 
 * std::string has a hash function implemented
 */

/*
std::unordered_map<std::string, pfunc> cmd_map1 = {{"i", f_i}, {"I", f_I}, {"a", f_a}, {"A", f_A}};
std::unordered_map<std::string, pfunc> cmd_map2 = {{"o", f_o}, {"O", f_O}};
std::unordered_map<std::string, pfunc> cmd_map3 = {{"x", f_x}, {"dw", f_dw}, {"daw", f_daw}, {"dd", f_dd}, {"d$", f_d$}, {"de", f_de}, {"dG", f_dG}};
std::unordered_map<std::string, pfunc> cmd_map4 = {{"cw", f_cw}, {"caw", f_caw}, {"s", f_s}};
*/

// not in use right nowa - ? if has use
//static std::unordered_map<std::string, pfunc> cmd_map5 = {{"w", f_w}, {"b", f_b}, {"e", f_e}, {"0", f_0}, {"$", f_$}};

/* OUTLINE COMMAND_LINE mode command lookup 
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
  {"readfile", F_readfile},
  {"read", F_readfile},
  {"valgrind", F_valgrind},
  {"quit", F_quit_app},
  {"q", F_quit_app},
  {"quit!", F_quit_app_ex},
  {"q!", F_quit_app_ex},
  {"merge", F_merge},
  {"help", F_help},
  {"h", F_help},
  {"persist", F_persist},
  {"clear", F_clear},

};

static std::unordered_map<std::string, zfunc> E_lookup_C {
  {"write", E_write_C},
  {"w", E_write_C},
  {"x", E_write_close_C},
  {"quit", E_quit_C},
  {"q", E_quit_C},
  {"quit!", E_quit0_C},
  {"q!", E_quit0_C},
  {"vim", E_open_in_vim_C},
  {"spell", E_spellcheck_C},
  {"spellcheck", E_spellcheck_C},
  {"persist", E_persist_C},
};

std::unordered_map<std::string, efunc> E_lookup_C {
  {"write", &Editor::E_write_C},
  {"w", &Editor::E_write_C},
  {"x", &Editor::E_write_close_C},
  {"quit", &Editor::E_quit_C},
  {"q",&Editor:: E_quit_C},
  {"quit!", &Editor::E_quit0_C},
  {"q!", &Editor::E_quit0_C},
  {"vim", &Editor::E_open_in_vim_C},
  {"spell",&Editor:: E_spellcheck_C},
  {"spellcheck", &Editor::E_spellcheck_C},
  {"persist", &Editor::E_persist_C},
};
// OUTLINE NORMAL mode command lookup 
std::unordered_map<std::string, zfunc> n_lookup {
  {"\r", return_N}, //return_O
  {"i", insert_N},
  {"s", s_N},
  {"~", tilde_N},
  {"r", r_N},
  {"a", a_N},
  {"A", A_N},
  {"x", x_N},
  {"w", w_N},

  {"daw", daw_N},
  {"dw", dw_N},
  {"daw", caw_N},
  {"dw", cw_N},
  {"de", de_N},
  {"d$", d$_N},

  {"gg", gg_N},

  {"gt", gt_N},

  {{0x17,0x17}, edit_N},
  {{0x9}, display_item_info},

  {"b", b_N},
  {"e", e_N},
  {"0", zero_N},
  {"$", dollar_N},
  {"I", I_N},
  {"G", G_N},
  {"O", O_N},
  {":", colon_N},
  {"v", v_N},
  {"p", p_N},
  {"*", asterisk_N},
  {"m", m_N},
  {"n", n_N},
  {"u", u_N},
  {"dd", dd_N},
  {{0x4}, dd_N}, //ctrl-d
  {{0x2}, star_N}, //ctrl-b -probably want this go backwards (unimplemented) and use ctrl-e for this
  {{0x18}, completed_N}, //ctrl-x
  {"^", caret_N},

};

// EDITOR NORMAL mode command lookup 
std::unordered_map<std::string, pfunc> e_lookup {

  {"i", E_i}, //i_E
  {"I", E_I},
  {"a", E_a},
  {"A", E_A},
  {"o", e_o},
  {"O", e_O},
  {"x", E_x},
  {"dw", E_dw},
  {"de", E_de},
  {"dG", E_dG},
  {"d$", E_d$},
  {"dd", E_dd},
  {"cw", E_cw},
  {"caw", E_caw},
  {"s", E_s},
  {"~", E_tilde},
  {"J", E_J},
  {"w", E_w},
  {"e", E_e},
  {"b", E_b},
  {"0", E_0},
  {"$", E_$},
  {"r", e_replace},
  {"[s", E_next_misspelling},
  {"]s", E_prev_misspelling},
  {"z=", E_suggestions},
  {":", E_change2command_line},
  {"V", E_change2visual_line},
  {"v", E_change2visual},
  {{0x16}, E_change2visual_block},
  {"p", E_paste},
  {"*", E_find},
  {"n", E_find_next_word},
  {"u", E_undo},
  {".", editorDotRepeat},
  {">>", E_indent},
  {"<<", E_unindent},
  {{0x2}, E_bold},
  {{0x5}, E_emphasis},
  {{0x9}, E_italic},
  {"yy", editorYankLine},
  {"gg", E_gg},
  {"G", E_G},
  {{0x1A}, E_toggle_smartindent},
  {{0x13}, E_save_note},
};
// config struct for reading db.ini file
struct config {
  std::string user;
  std::string password;
  std::string dbname;
  std::string hostaddr;
  int port;
};
struct config c;

zmq::context_t context (1);
zmq::socket_t publisher (context, ZMQ_PUB);

PGconn *conn = nullptr;
*/
#endif
