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
#include <nuspell/dictionary.hxx>
#include <nuspell/finder.hxx>
#include <zmq.hpp>

#include <memory>

#include <fcntl.h>
#include <unistd.h>

//
//
extern "C" {
#include <mkdio.h>
}
static const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
static const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
static const std::string DB_INI = "db.ini";
static const std::string CURRENT_NOTE_FILE = "current.html";
static const std::string META_FILE = "assets/meta.html";
static  std::string system_call = "./lm_browser " + CURRENT_NOTE_FILE;
static std::string meta;
static int which_db;
static int EDITOR_LEFT_MARGIN;
static struct termios orig_termios;
static int screenlines, screencols, new_screenlines, new_screencols;
static std::stringstream display_text;
static int initial_file_row = 0; //for arrowing or displaying files
static bool editor_mode;
static std::string search_terms;
static std::vector<std::vector<int>> word_positions;
static std::vector<int> fts_ids;
static int fts_counter;
static std::string search_string; //word under cursor works with *, n, N etc.
static std::vector<std::string> line_buffer; //yanking lines
static std::string string_buffer; //yanking chars
static std::map<int, std::string> fts_titles;
static std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
static std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
static std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]
//static std::vector<std::string> task_keywords;
static std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
static std::set<int> unique_ids; //used in unique_data_callback
static std::vector<std::string> command_history; // the history of commands to make it easier to go back to earlier views
static std::vector<std::string> page_history; // the history of commands to make it easier to go back to earlier views
static size_t cmd_hx_idx = 0;
static size_t page_hx_idx = 0;
//static const std::set<int> cmd_set1 = {'I', 'i', 'A', 'a'};
static std::map<int, std::string> html_files;
static bool lm_browser = true;

const std::string COLOR_1 = "\x1b[0;31m";
const std::string COLOR_2 = "\x1b[0;32m";
const std::string COLOR_3 = "\x1b[0;33m";
const std::string COLOR_4 = "\x1b[0;34m";
const std::string COLOR_5 = "\x1b[0;35m";
const std::string COLOR_6 = "\x1b[0;36m";
const std::string COLOR_7 = "\x1b[0;37m";

static int SMARTINDENT = 4; //should be in config
static int temporary_tid = 99999;
static int link_id = 0;
static char link_text[20];

static int current_task_id;

static std::unordered_set<int> marked_entries;

enum outlineKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
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

static const std::string mode_text[] = {
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

static constexpr char BASE_DATE[] = "1970-01-01 00:00";

enum Command {
  C_caw = 2000,
  C_cw,
  C_daw,
  C_dw,
  C_de,
  C_d$,
  C_dd,//could just toggle deleted
  C_dG,
  C_indent,
  C_unindent,
  C_c$,
  C_gg,
  C_gt,

  C_yy,

  C_open,
  C_openfolder,
  C_openkeyword,
  C_join,

  C_sort,

  C_clear,
  C_find,
  C_search,
  C_fts,
  C_linked,

  C_refresh,

  C_new, //create a new item/entry

  C_contexts, //change O.view to CONTEXTs :c
  C_folders,  //change O.view to FOLDERs :f
  C_keywords,
  C_addkeyword,
  C_movetocontext,
  C_movetofolder,
  C_updatecontext,
  C_updatefolder,
  C_deletekeywords,

  C_delmarks,
  C_showall,

  C_update, //update solr db

  C_synch, // synchronixe sqlite and postgres dbs
  C_synch_test,//show what sync would do but don't do it 

  C_highlight,
  C_spellcheck,
  C_set, // [spell/nospell]
  C_suggestions, // [spell/nospell]
  C_next_mispelling,
  C_previous_mispelling,

  C_quit,
  C_quit0,

  C_recent,

  C_help,
  C_readfile,
  C_savefile,

  C_edit,

  C_database,
  //C_search,

  C_saveoutline,
  C_syntax,

  C_merge,

  C_vim,
  C_valgrind,
  C_write,

  C_browser,

  C_pdf
};

static const std::unordered_map<std::string, int> lookuptablemap {
  {"caw", C_caw},
  {"cw", C_cw},
  {"daw", C_daw},
  {"dw", C_dw},
  {"de", C_de},
  {"dd", C_dd},
  {"dG", C_dG},
  {">>", C_indent},
  {"<<", C_unindent},
  {"gg", C_gg},
  {"gt", C_gt},
  {"yy", C_yy},
  {"d$", C_d$},

  {"help", C_help},
  {"h", C_help}, //need because this is command line command with a target word
  {"open", C_open},
  {"o", C_open}, //need because this is command line command with a target word
  {"of", C_openfolder}, //need because this is command line command with a target word
  {"ok", C_openkeyword}, //need because this is command line command with a target word
  {"join", C_join}, //need because this is command line command with a target word
  {"filter", C_join}, //need because this is command line command with a target word
  {"fin", C_find},
  {"find", C_find},
  {"search", C_find},
  {"linked", C_linked}, // also 'l' in COMMAND_LINE
  {"related", C_linked}, // also 'l' in COMMAND_LINE
  {"fts", C_fts},
  {"refresh", C_refresh},
  {"new", C_new}, //don't need "n" because there is no target
  {"contexts", C_contexts},
  {"c", C_contexts},
  {"folders", C_folders},
  {"f", C_folders},
  {"keywords", C_keywords},
  {"k", C_keywords},
  {"mtc", C_movetocontext}, //need because this is command line command with a target word
  {"movetocontext", C_movetocontext},
  {"mtf", C_movetofolder}, //need because this is command line command with a target word
  {"movetofolder", C_movetofolder},
  {"updatefolder", C_updatefolder},
  {"updatecontext", C_updatecontext},
  {"addkeyword", C_addkeyword},
  {"addkw", C_addkeyword},
  {"delkeywords", C_deletekeywords},
  {"delk", C_deletekeywords},
  {"delmarks", C_delmarks},
  {"delm", C_delmarks},
  {"update", C_update},
  {"sort", C_sort},
  {"sync", C_synch},
  {"synchronize", C_synch},
  {"test", C_synch_test},
  {"showall", C_showall},
  {"show", C_showall},
  {"write", C_write},
  {"synchtest", C_synch_test},
  {"synch_test", C_synch_test},
  {"quit", C_quit},
  {"quit!", C_quit0},
  {"q!", C_quit0},
  {"edit", C_edit},
  {{0x17,0x17}, C_edit}, //CTRL-W,CTRL-W; = dec 23; hex 17
  {"rec", C_recent},
  {"recent", C_recent},
  {"val", C_valgrind},
  {"database", C_database},
  {"db", C_database},
  //{"search", C_search},
  {"set", C_set},
  {"z=", C_suggestions},
  {"savefile", C_savefile},
  {"save", C_savefile},
  {"saveoutline", C_saveoutline},
  {"so", C_saveoutline},
  {"highlight", C_highlight},
  {"spellcheck", C_spellcheck},
  {"[s", C_next_mispelling},
  {"]s", C_previous_mispelling},
  {"readfile", C_readfile},
  {"read", C_readfile},
  {"vim", C_vim},
  {"merge", C_merge},
  {"syntax", C_syntax},
  {"clear", C_clear},
  {"browser", C_browser},
  {"pdf", C_pdf}
};

struct sqlite_db {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};

static struct sqlite_db S;

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

static struct outlineConfig O;

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
  int last_command; //will use the number equivalent of the command
  int last_repeat;
  std::string last_typed; //what's typed between going into INSERT mode and leaving INSERT mode
  int repeat;
  int indent;
  int smartindent;
  int first_visible_row;
  int last_visible_row;
  bool spellcheck;
  bool move_only;
  bool highlight_syntax;
};

static struct editorConfig E;

struct flock lock;

/* note that you can call these either through explicit dereference: (*get_note)(4328)
 * or through implicit dereference: get_note(4328)
*/
int getWindowSize(int *, int *);

// I believe this is being called at times redundantly before editorEraseScreen and outlineRefreshScreen
void EraseScreenRedrawLines(void);

void outlineProcessKeypress(int = 0);
bool editorProcessKeypress(void);
void F_open(int);
void F_openfolder(int);
void F_openkeyword(int);
void F_deletekeywords(int); //int pos not used

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
void outlineMoveNextWord();
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

void editorHighlightWordsByPosition(void);
void editorSpellCheck(void);
void editorHighlightWord(int, int, int);

int keyfromstringcpp(const std::string&);
int commandfromstringcpp(const std::string&, std::size_t&);

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

void update_solr(void);

/* experimenting with map of functions*/
inline void f_i(int);
inline void f_I(int);
inline void f_a(int);
inline void f_A(int);
inline void f_O(int);
inline void f_o(int);
inline void f_dw(int);
inline void f_daw(int);
inline void f_dd(int);
inline void f_de(int);
inline void f_dG(int);
inline void f_cw(int);
inline void f_caw(int);
inline void f_s(int);
inline void f_x(int);
inline void f_d$(int);
inline void f_w(int);
inline void f_b(int);
inline void f_e(int);
inline void f_0(int);
inline void f_$(int);

//void generate_pdf(void);

static const std::set<int> cmd_set1 = {'I', 'i', 'A', 'a'};
typedef void (*pfunc)(int);
pfunc funcfromstring(const std::string&);
pfunc commandfromstring(const std::string&, std::size_t&);
//static std::map<int, int> cmd_map0 = {{'a', 1}, {'A', 1}, {C_caw, 4}, {C_cw, 4}, {C_daw, 3}, {C_dw, 3}, {'o', 2}, {'O', 2}, {'s', 4}, {'x', 3}, {'I', 1}, {'i', 1}};
static std::unordered_map<int, pfunc> cmd_map1 = {{'i', f_i}, {'I', f_I}, {'a', f_a}, {'A', f_A}};
static std::unordered_map<int, pfunc> cmd_map2 = {{'o', f_o}, {'O', f_O}};
static std::unordered_map<int, pfunc> cmd_map3 = {{'x', f_x}, {C_dw, f_dw}, {C_daw, f_daw}, {C_dd, f_dd}, {C_d$, f_d$}, {C_de, f_de}, {C_dG, f_dG}};
static std::unordered_map<int, pfunc> cmd_map4 = {{C_cw, f_cw}, {C_caw, f_caw}, {'s', f_s}};
static std::unordered_map<int, pfunc> cmd_map5 = {{'w', f_w}, {'b', f_b}, {'e', f_e}, {'0', f_0}, {'$', f_$}};
/*************************************/

static std::unordered_map<std::string, pfunc> lookuptable {
  {"open", F_open},
  {"o", F_open},
  {"openfolder", F_openfolder},
  {"of", F_openfolder},
  {"openkeyword", F_openkeyword},
  {"ok", F_openkeyword},
  {"deletekeywords", F_deletekeywords},
  {"delkw", F_deletekeywords},
  {"delk", F_deletekeywords},
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

void do_exit(PGconn *conn) {
    PQfinish(conn);
    exit(1);
}

void signalHandler(int signum) {
    getWindowSize(&new_screenlines, &new_screencols);
    screenlines = new_screenlines;
    screencols = new_screencols;
    EraseScreenRedrawLines();
    O.screenlines = screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
    O.screencols =  screencols/2 - OUTLINE_RIGHT_MARGIN - OUTLINE_LEFT_MARGIN;
    E.screenlines = screenlines - 2 - TOP_MARGIN;
    E.screencols = -2 + screencols/2;
    EDITOR_LEFT_MARGIN = screencols/2 + 1;

    /*
    the order of everything below
    seems to preserve cursor
    editorRefreshScreen may be called
    twice since called by get_note but that's OK
    */

    if (O.view == TASK && O.mode != NO_ROWS)
      get_note(O.rows.at(O.fr).id);

    if (editor_mode) {
      outlineRefreshScreen();
      editorRefreshScreen(true);
    } else {
      editorRefreshScreen(true);
      outlineRefreshScreen();
    }
    
  outlineDrawStatusBar();
  outlineShowMessage("rows: %d  cols: %d ", screenlines, screencols);
  return_cursor();
}

void parse_ini_file(std::string ini_name)
{
  inipp::Ini<char> ini;
  std::ifstream is(ini_name);
  ini.parse(is);
  inipp::extract(ini.sections["ini"]["user"], c.user);
  inipp::extract(ini.sections["ini"]["password"], c.password);
  inipp::extract(ini.sections["ini"]["dbname"], c.dbname);
  inipp::extract(ini.sections["ini"]["hostaddr"], c.hostaddr);
  inipp::extract(ini.sections["ini"]["port"], c.port);
}

void load_meta(void) {
  std::ifstream f(META_FILE);
  std::string line;
  static std::stringstream text;

  //text.str(std::string());
  //text.clear();

  while (getline(f, line)) {
    text << line << '\n';
  }
  meta = text.str();
  f.close();
}

// typedef char * (*mkd_callback_t)(const char*, const int, void*);
// needed by markdown code in update_html_file
char * (url_callback)(const char *x, const int y, void *z) {
  link_id++;
  sprintf(link_text,"id=\"%d\"", link_id);
  return link_text;
}  

/* this works but used mkd_generalhtml
 * and I thought it might be better to 
 * us mkd_document since then you can
 * write to the file once
 */
void update_html_file_works(std::string &&fn) {
  std::string note = editorRowsToString();
  std::stringstream text;
  std::string title = O.rows.at(O.fr).title;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); //did something

  FILE *fptr;
  fptr = fopen(fn.c_str(), "w");
  fprintf(fptr, meta_.c_str());
  mkd_generatehtml(blob, fptr);
  fprintf(fptr, "</article></body><html>");
  fclose(fptr);
  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 
  mkd_cleanup(blob);
  link_id = 0;
}

/* this version of update_html_file uses mkd_document
 * and only writes to the file once
 */
void update_html_file(std::string &&fn) {
  std::string note = editorRowsToString();
  std::stringstream text;
  std::stringstream html;
  char *doc = nullptr;
  std::string title = O.rows.at(O.fr).title;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); //did something
  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";
  
  /*
  std::ofstream myfile;
  myfile.open(fn); //filename
  myfile << html.str().c_str();
  myfile.close();
  */

  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else outlineShowMessage("Couldn't lock file");
  } else outlineShowMessage("Couldn't open file");

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 
  //
  mkd_cleanup(blob);
  link_id = 0;
}

/* this zeromq version works but there is a problem on the ultralight
 * side -- LoadHTML doesn't seem to use the style sheet.  Will check on slack
 * if this is my mistake or intentional
 * */
void update_html_zmq(std::string &&fn) {
  std::string note = editorRowsToString();
  std::stringstream text;
  std::stringstream html;
  std::string title = O.rows.at(O.fr).title;
  char *doc = nullptr;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); 

  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";

  zmq::message_t message(html.str().size()+1);

  // probably don't need snprint to get html into message
  snprintf ((char *) message.data(), html.str().size()+1, "%s", html.str().c_str()); 

  publisher.send(message, zmq::send_flags::dontwait);

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 

  mkd_cleanup(blob);
  link_id = 0;
}

void update_html_code_file(std::string &&fn) {
  std::ofstream myfile;
  myfile.open("code_file"); 
  myfile << editorRowsToString(); //don't need word wrap
  myfile.close();
  std::stringstream html;
  std::string line;

   procxx::process highlight("highlight", "code_file", "--out-format=html", 
                             "--style=gruvbox-dark-hard-slz", "--syntax=cpp");
    highlight.exec();
    while(getline(highlight.output(), line)) { html << line << '\n';}
    //while(getline(highlight.output(), line)) { html << line << "<br>";}

  /*
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  */
  
  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else outlineShowMessage("Couldn't lock file");
  } else outlineShowMessage("Couldn't open file");

}
void generate_persistent_html_file(int id) {
  std::string  fn = O.rows.at(O.fr).title.substr(0, 20);
    std::string illegal_chars = "\\/:?\"<>|\n ";
    for (auto& c: fn){
      if (illegal_chars.find(c) != std::string::npos) c = '_';
    }
  fn = fn + ".html";    
  html_files.insert(std::pair<int, std::string>(id, fn)); //could do html_files[id] = fn;
  update_html_file("assets/" + fn);

  std::string call = "./lm_browser " + fn;
  popen (call.c_str(), "r"); // returns FILE* id
  outlineShowMessage("Created file: %s and displayed with ultralight", system_call.c_str());
}

//pg ini stuff
void get_conn(void) {
  char conninfo[250];
  parse_ini_file(DB_INI);
  
  sprintf(conninfo, "user=%s password=%s dbname=%s hostaddr=%s port=%d", 
          c.user.c_str(), c.password.c_str(), c.dbname.c_str(), c.hostaddr.c_str(), c.port);

  conn = PQconnectdb(conninfo);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  } 
}

/* Begin sqlite database functions */

void db_open(void) {
  int rc = sqlite3_open(SQLITE_DB.c_str(), &S.db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.db);
    exit(1);
  }

  rc = sqlite3_open(FTS_DB.c_str(), &S.fts_db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.fts_db);
    exit(1);
  }
}

bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     outlineShowMessage("SQL error: %s", errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg, const char *func) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     outlineShowMessage("SQL error in %s: %s", func, errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

void map_context_titles(void) {

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM context;");

  bool no_rows = true;
  if (!db_query(S.db, query.c_str(), context_titles_callback, &no_rows, &S.err_msg, __func__)) return;
  if (no_rows) outlineShowMessage("There were no context titles to map!");
}

int context_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  context_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void map_folder_titles(void) {

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM folder;");

  bool no_rows = true;
  if (!db_query(S.db, query.c_str(), folder_titles_callback, &no_rows, &S.err_msg, __func__)) return;
  if (no_rows) outlineShowMessage("There were no folder titles to map!");
}

int folder_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  folder_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void get_containers(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::string table;
  std::string column = "title"; //only needs to be change for keyword
  int (*callback)(void *, int, char **, char **);
  switch (O.view) {
    case CONTEXT:
      table = "context";
      callback = context_callback;
      break;
    case FOLDER:
      table = "folder";
      callback = folder_callback;
      break;
    case KEYWORD:
      table = "keyword";
      column = "name";
      callback = keyword_callback;
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }

  std::stringstream query;
  query << "SELECT * FROM " << table << " ORDER BY " << column  << " COLLATE NOCASE ASC;";

  bool no_rows = true;
  if (!db_query(S.db, query.str().c_str(), callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
  } else {
    //O.mode = NORMAL;
    O.mode = O.last_mode;
    display_container_info(O.rows.at(O.fr).id);
  }

  O.context = O.folder = O.keyword = ""; // this makes sense if you are not in an O.view == TASK
}

int context_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: "default" = Boolean ? what this is; sql has to use quotes to refer to column
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //"default"
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[9], 16);
  O.rows.push_back(row);

  return 0;
}

int folder_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  5: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //private
  row.deleted = (atoi(argv[7]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[11], 16);
  O.rows.push_back(row);

  return 0;
}

int keyword_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5:deleted
  */

  orow row;

  row.title = std::string(argv[1]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; 
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[4], 16);
  O.rows.push_back(row);

  return 0;
}

std::pair<std::string, std::vector<std::string>> get_task_keywords(void) {

  std::stringstream query;
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << O.rows.at(O.fr).id << " =  task_keyword.task_id;";

   std::vector<std::string> task_keywords = {}; ////////////////////////////
   bool success =  db_query(S.db, query.str(), task_keywords_callback, &task_keywords, &S.err_msg);
   if (task_keywords.empty() || !success) return std::make_pair(std::string(), std::vector<std::string>());

   std::string delim = "";
   std::string s = "";
   for (const auto &kw : task_keywords) {
     s += delim += kw;
     delim = ",";
   }
   return std::make_pair(s, task_keywords);
}

// need this pg db function
std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int tid) {

  std::stringstream query;
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << tid << " =  task_keyword.task_id;";

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineShowMessage("Problem in get_task_keywords_pg!");
    PQclear(res);
    return std::make_pair(std::string(), std::vector<std::string>());
  }

  int rows = PQntuples(res);
  std::vector<std::string> task_keywords = {};
  for(int i=0; i<rows; i++) {
    task_keywords.push_back(PQgetvalue(res, i, 0));
  }
   std::string delim = "";
   std::string s = "";
   for (const auto &kw : task_keywords) {
     s += delim += kw;
     delim = ",";
   }
  PQclear(res);
  return std::make_pair(s, task_keywords);
  // PQfinish(conn);
}

int task_keywords_callback(void *ptr, int argc, char **argv, char **azColName) {

  std::vector<std::string>* task_keys = static_cast<std::vector<std::string> *>(ptr);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  task_keys->push_back(std::string(argv[0]));

  return 0; //you need this
}

//overload that takes keyword_id and task_id
void add_task_keyword(int keyword_id, int task_id) {

    std::stringstream query;
    query << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " 
          << task_id << ", keyword.id FROM keyword WHERE keyword.id = " 
          << keyword_id << ";";
    if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query2;
    // updates task modified column so we know that something changed with the task
    query2 << "UPDATE task SET modified = datetime('now', '-"
           << TZ_OFFSET << " hours') WHERE id =" << task_id << ";";
    if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    /**************fts virtual table update**********************/

    std::string s = get_task_keywords().first;
    std::stringstream query3;
    query3 << "Update fts SET tag='" << s << "' WHERE lm_id=" << task_id << ";";
    if (!db_query(S.fts_db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;
}

//void add_task_keyword(const std::string &kw, int id) {
//overload that takes keyword name and task_id
void add_task_keyword(std::string &kws, int id) {

  std::stringstream temp(kws);
  std::string phrase;
  std::vector<std::string> keyword_list;
  while(getline(temp, phrase, ',')) {
    keyword_list.push_back(phrase);
  }    

  for (std::string kw : keyword_list) {

    size_t pos = kw.find("'");
    while(pos != std::string::npos)
      {
        kw.replace(pos, 1, "''");
        pos = kw.find("'", pos + 2);
      }

    std::stringstream query;

    /*IF NOT EXISTS(SELECT 1 FROM keyword WHERE name = 'mango') INSERT INTO keyword (name) VALUES ('mango')
     * <- doesn't work for sqlite
     * note you don't have to do INSERT OR IGNORE but could just INSERT since unique constraint
     * on keyword.name but you don't want to trigger an error either so probably best to retain
     * INSERT OR IGNORE there is a default that tid = 0 but let's do it explicity*/

    query <<  "INSERT OR IGNORE INTO keyword (name, tid, star, modified, deleted) VALUES ('"
          <<  kw << "', " << 0 << ", true, datetime('now', '-" << TZ_OFFSET << " hours'), false);"; 

    if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query2;
    query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
    if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query3;
    // updates task modified column so we know that something changed with the task
    query3 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << id << ";";
    if (!db_query(S.db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    /**************fts virtual table update**********************/

    std::string s = get_task_keywords().first;
    std::stringstream query4;
    query4 << "Update fts SET tag='" << s << "' WHERE lm_id=" << id << ";";
    if (!db_query(S.fts_db, query4.str().c_str(), 0, 0, &S.err_msg, __func__)) return;
  }
}

int keyword_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT keyword.id from keyword WHERE keyword.name = '" << name << "';";
  int keyword_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &keyword_id, &S.err_msg, __func__)) return -1;
  return keyword_id;
}

/*
int keyword_id_callback(void *keyword_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(keyword_id);
  *id = atoi(argv[0]);
  return 0;
}
*/

// not in use but might have some use
int folder_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT folder.id from folder WHERE folder.name = '" << name << "';";
  int folder_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &folder_id, &S.err_msg, __func__)) return -1;
  return folder_id;
}

// not in use but might have some use
int context_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT context.id from context WHERE context.name = '" << name << "';";
  int context_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &context_id, &S.err_msg, __func__)) return -1;
  return context_id;
}

int container_id_callback(void *container_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(container_id);
  *id = atoi(argv[0]);
  return 0;
}
//void delete_task_keywords(void) {
void F_deletekeywords(int pos) {

  std::stringstream query;
  query << "DELETE FROM task_keyword WHERE task_id = " << O.rows.at(O.fr).id << ";";
  if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  std::stringstream query2;
  // updates task modified column so know that something changed with the task
  query2 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << O.rows.at(O.fr).id << ";";
  if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  /**************fts virtual table update**********************/
  std::stringstream query3;
  query3 << "Update fts SET tag='' WHERE lm_id=" << O.rows.at(O.fr).id << ";";
  if (!db_query(S.fts_db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  outlineShowMessage("Keyword(s) for task %d will be deleted and fts searchdb updated", O.rows.at(O.fr).id);
  O.mode = O.last_mode;
}

void get_linked_items(int max) {
  std::vector<std::string> task_keywords = get_task_keywords().second;
  if (task_keywords.empty()) return;

  std::stringstream query;
  unique_ids.clear();

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

  for (auto it=task_keywords.begin(); it != task_keywords.end() - 1; ++it) {
    query << "keyword.name = '" << *it << "' OR ";
  }
  query << "keyword.name = '" << task_keywords.back() << "')";

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << O.sort
        << " DESC LIMIT " << max;

    int sortcolnum = sort_map[O.sort];
    if (!db_query(S.db, query.str().c_str(), unique_data_callback, &sortcolnum, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (O.rows.empty()) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
    else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

void get_items(int max) {
  std::stringstream query;
  std::vector<std::string> keyword_vec;
  int (*callback)(void *, int, char **, char **);
  callback = data_callback;

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  if (O.taskview == BY_CONTEXT) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " WHERE context.title = '" << O.context << "' ";
  } else if (O.taskview == BY_FOLDER) {
    query << "SELECT * FROM task JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE folder.title = '" << O.folder << "' ";
  } else if (O.taskview == BY_RECENT) {
    query << "SELECT * FROM task WHERE 1=1";
  } else if (O.taskview == BY_JOIN) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE context.title = '" << O.context << "'"
          << " AND folder.title = '" << O.folder << "'";
  } else if (O.taskview == BY_KEYWORD) {

 // if O.keyword has more than one keyword
    std::string k;
    std::stringstream skeywords;
    skeywords << O.keyword;
    while (getline(skeywords, k, ',')) {
      keyword_vec.push_back(k);
    }

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

    for (auto it = keyword_vec.begin(); it != keyword_vec.end() - 1; ++it) {
      query << "keyword.name = '" << *it << "' OR ";
    }
    query << "keyword.name = '" << keyword_vec.back() << "')";

    callback = unique_data_callback;
    unique_ids.clear();

  } else {
    outlineShowMessage("You asked for an unsupported db query");
    return;
  }

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << O.sort
        << " DESC LIMIT " << max;

  int sortcolnum = sort_map[O.sort];
  if (!db_query(S.db, query.str().c_str(), callback, &sortcolnum, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (O.rows.empty()) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
    else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL

  I thought I should be using tid as the "id" for sqlite version but realized
  that would work and mean you could always compare the tid to the pg id
  but for new items created with sqlite, there would be no tid so
  the right thing to use is the id.  At some point might also want to
  store the tid in orow row
  */


  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;

  //(argv[*static_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*static_cast<int*>(sortcolnum)], 16)
  //                                               : strncpy(row.modified, " ", 16);

  //the reason for below is I am thinking about changing row.modified to a std::string but that's lots of changes
  //wanted to show how it would work
  int *sc = static_cast<int*>(sortcolnum);
  std::string row_modified; //would be row.modified if changed to string
  (argv[*sc] != nullptr) ? row_modified.assign(argv[*sc], 16) : row_modified.assign(16, ' ');
  strncpy(row.modified, row_modified.c_str(), 16);
  O.rows.push_back(row);

  return 0;
}

int unique_data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int id = atoi(argv[0]);
  auto [it, success] = unique_ids.insert(id);
  if (!success) return 0;

  orow row;
  row.id = id;
  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;
  //(argv[*reinterpret_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*reinterpret_cast<int*>(sortcolnum)], 16)
  (argv[*static_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*static_cast<int*>(sortcolnum)], 16)
                                                 : strncpy(row.modified, " ", 16);
  O.rows.push_back(row);

  return 0;
}

// called as part of :find -> search_db -> fts5_callback -> get_items_by_id -> by_id_data_callback
void get_items_by_id(std::stringstream &query) {
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */

  bool no_rows = true;
  if (!db_query(S.db, query.str().c_str(), by_id_data_callback, &no_rows, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = SEARCH;
    get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int by_id_data_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag =
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL

  I thought I should be using tid as the "id" for sqlite version but realized
  that would work and mean you could always compare the tid to the pg id
  but for new items created with sqlite, there would be no tid so
  the right thing to use is the id.  At some point might also want to
  store the tid in orow row
  */

  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.fts_title = fts_titles.at(row.id);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;
  //if changed modified (which should be some generic time name) then row.modified.assign(argv[16], 16)
  strncpy(row.modified, argv[16], 16);
  O.rows.push_back(row);

  return 0;
}

void merge_note(int id) {
  std::stringstream query;

  query << "SELECT note FROM task WHERE id = " << id;

  if (!db_query(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg, __func__)) return;

  //int rc = sqlite3_exec(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg);

  //if (rc != SQLITE_OK ) {
  //  outlineShowMessage("In merge_note: %s SQL error: %s", FTS_DB.c_str(), S.err_msg);
  //  sqlite3_free(S.err_msg);
  //  sqlite3_close(S.db);
  //}
}

void get_note(int id) {
  if (id ==-1) return; //maybe should be if (id < 0) and make all context id/tid negative

  word_positions.clear();
  E.rows.clear();
  E.fr = E.fc = E.cy = E.cx = E.line_offset = E.prev_line_offset = E.first_visible_row = E.last_visible_row = 0; // 11-18-2019 commented out because in C_edit but a problem if you leave editor mode

  std::stringstream query;
  query << "SELECT note FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg, __func__)) return;

  if (O.taskview != BY_SEARCH) {
    editorRefreshScreen(true);
    //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
    if (lm_browser) {
      if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
      else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
    }   
    return;
  }

  std::stringstream query2;
  query2 << "SELECT rowid FROM fts WHERE lm_id = " << id << ";";

  int rowid = -1;
  // callback is *not* called if result (argv) is null
  if (!db_query(S.fts_db, query2.str().c_str(), rowid_callback, &rowid, &S.err_msg, __func__)) return;

  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);
  std::stringstream query3;
  int n = 0;
  for(auto v: vec) {
    word_positions.push_back(std::vector<int>{});
    query3.str(std::string()); // how you clear a stringstream
    query3 << "SELECT offset FROM fts_v WHERE doc =" << rowid << " AND term = '" << v << "' AND col = 'note';";
    if (!db_query(S.fts_db, query3.str().c_str(), offset_callback, &n, &S.err_msg, __func__)) return;

    n++;
  }

  int ww = (word_positions.at(0).empty()) ? -1 : word_positions.at(0).at(0);
  editorSetMessage("Word position first: %d; id = %d and row_id = %d", ww, id, rowid);

  editorRefreshScreen(true);
  //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
}

int rowid_callback (void *rowid, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *rwid = static_cast<int*>(rowid);
  *rwid = atoi(argv[0]);
  return 0;
}

int offset_callback (void *n, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  int *nn= static_cast<int*>(n);

  word_positions.at(*nn).push_back(atoi(argv[0]));

  return 0;
}

// doesn't appear to be called if row is NULL
int note_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  if (!argv[0]) return 0; ////////////////////////////////////////////////////////////////////////////
  std::string note(argv[0]);
  note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    //snote will not contain the '\n'
    editorInsertRow(E.rows.size(), s);
  }

  E.dirty = 0;
  return 0;
}
//void fts5_sqlite(std::string search_terms) {
void search_db(std::string search_terms) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            //<< search_terms << "' ORDER BY rank";
            //<< search_terms << "' ORDER BY rank LIMIT " << 50;
            //<< search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0) LIMIT " << 50;
            << search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";

  fts_ids.clear();
  fts_titles.clear();
  fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query.str().c_str(), fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    editorEraseScreen(); //note can still return no rows from get_items_by_id if we found rows above that were deleted
    O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.id IN (";

  for (int i = 0; i < fts_counter-1; i++) {
    query << fts_ids[i] << ", ";
  }
  query << fts_ids[fts_counter-1]
        << ")"
        << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  for (int i = 0; i < fts_counter-1; i++) {
    query << "task.id = " << fts_ids[i] << " DESC, ";
  }
  query << "task.id = " << fts_ids[fts_counter-1] << " DESC";

  get_items_by_id(query);

  //outlineShowMessage(query.str().c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
  //outlineShowMessage(search_terms.c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
}

//total kluge but just brings back context_tid = 16
void search_db2(std::string search_terms) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            << search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";

  fts_ids.clear();
  fts_titles.clear();
  fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query.str().c_str(), fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    editorEraseScreen();
    O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.context_tid = 16 and task.id IN (";

  for (int i = 0; i < fts_counter-1; i++) {
    query << fts_ids[i] << ", ";
  }
  query << fts_ids[fts_counter-1]
        << ")"
        << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  for (int i = 0; i < fts_counter-1; i++) {
    query << "task.id = " << fts_ids[i] << " DESC, ";
  }
  query << "task.id = " << fts_ids[fts_counter-1] << " DESC";

  get_items_by_id(query);
}

int fts5_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  fts_ids.push_back(atoi(argv[0]));
  fts_titles[atoi(argv[0])] = std::string(argv[1]);
  fts_counter++;

  return 0;
}

int get_folder_tid(int id) {

  std::stringstream query;
  query << "SELECT folder_tid FROM task WHERE id = " << id;

  int folder_tid = -1;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg, __func__)) return -1;
  return folder_tid;
}

int folder_tid_callback(void *folder_tid, int argc, char **argv, char **azColName) {
  int *f_tid = static_cast<int*>(folder_tid);
  *f_tid = atoi(argv[0]);
  return 0;
}

void display_item_info(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  //int tid = 0;
  int tid;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg)) return;

  if (tid) display_item_info_pg(tid);
}

int display_item_info_callback(void *tid, int argc, char **argv, char **azColName) {
    
  int *pg_id = static_cast<int*>(tid);

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
    
  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL
  */

  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  //set background color to blue
  //ab.append("\x1b[44m"); //blue is color 12 0;44 same as plain 44.
  //ab.append("\x1b[38;5;21m"); //this is color 17
  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  *pg_id = argv[1] ? atoi(argv[1]) : 0;
  sprintf(str,"tid: %d", *pg_id);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"title: %s", argv[3]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int context_tid = atoi(argv[6]);
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works
  sprintf(str,"context: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int folder_tid = atoi(argv[5]);
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"folder: %s", it2->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star: %s", (atoi(argv[8]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"deleted: %s", (atoi(argv[14]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"completed: %s", (argv[10]) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"modified: %s", argv[16]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"added: %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  std::string s = get_task_keywords().first;
  sprintf(str,"keywords: %s", s.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  //sprintf(str,"tag: %s", argv[4]);
  //ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

void display_item_info_pg(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineShowMessage("Postgres Error: %s", PQerrorMessage(conn)); 
    PQclear(res);
    return;
  }    

  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);

  std::string s;

  //set background color to blue
  s.append("\n\n");
  s.append("\x1b[44m", 5);
  char str[300];

  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", PQgetvalue(res, 0, 0));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", PQgetvalue(res, 0, 3));
  s.append(str);
  s.append(lf_ret);

  int context_tid = atoi(PQgetvalue(res, 0, 6));
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works

  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", it->first.c_str());
  s.append(str);
  s.append(lf_ret);

  int folder_tid = atoi(PQgetvalue(res, 0, 5));
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mfolder:\x1b[0;44m %s", it2->first.c_str());
  s.append(str);
  s.append(lf_ret);

  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (*PQgetvalue(res, 0, 8) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 14) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 10)) ? "true": "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", PQgetvalue(res, 0, 16));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", PQgetvalue(res, 0, 9));
  s.append(str);
  s.append(lf_ret);

  std::string ss = get_task_keywords_pg(id).first;
  sprintf(str,"\x1b[1mkeywords:\x1b[0;44m %s", ss.c_str());
  s.append(str);
  s.append(lf_ret);

  //sprintf(str,"\x1b[1mtag:\x1b[0;44m %s", PQgetvalue(res, 0, 4));
  //s.append(str);
  //s.append(lf_ret);

  s.append("\x1b[0m");

  write(STDOUT_FILENO, s.c_str(), s.size());

  PQclear(res);
}

void display_container_info(int id) {
  if (id ==-1) return;

  std::string table;
  std::string count_query;
  int (*callback)(void *, int, char **, char **);

  switch(O.view) {
    case CONTEXT:
      table = "context";
      callback = context_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN context ON context.tid = task.context_tid WHERE context.id = ";
      break;
    case FOLDER:
      table = "folder";
      callback = folder_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN folder ON folder.tid = task.folder_tid WHERE folder.id = ";
      break;
    case KEYWORD:
      table = "keyword";
      callback = keyword_info_callback;
      count_query = "SELECT COUNT(*) FROM task_keyword WHERE keyword_id = ";
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }
  std::stringstream query;
  int count = 0;

  query << count_query << id;
  int rc = sqlite3_exec(S.db, query.str().c_str(), count_callback, &count, &S.err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", S.err_msg);
    sqlite3_free(S.err_msg);
    sqlite3_close(S.db);
  } 

  std::stringstream query2;
  query2 << "SELECT * FROM " << table << " WHERE id = " << id;

  // callback is *not* called if result (argv) is null
 rc = sqlite3_exec(S.db, query2.str().c_str(), callback, &count, &S.err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("In get_note: %s SQL error: %s", FTS_DB.c_str(), S.err_msg);
    sqlite3_free(S.err_msg);
    sqlite3_close(S.fts_db);
  }
}

int count_callback (void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *cnt = static_cast<int*>(count);
  *cnt = atoi(argv[0]);
  return 0;
}

int context_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: "default" = Boolean ? what this is; sql has to use quotes to refer to column
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&tid](auto& p) { return p.second == tid; }); //auto&& also works
  sprintf(str,"context: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/default: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int folder_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  5: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);
  auto it = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&tid](auto& p) { return p.second == tid; }); //auto&& also works
  sprintf(str,"folder: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/private: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[6]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[7]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[11]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int keyword_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5: deleted
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"name: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

void update_note(void) {

  std::string text = editorRowsToString();
  std::stringstream query;

  // need to escape single quotes with two single quotes
  size_t pos = text.find("'");
  while(pos != std::string::npos) {
    text.replace(pos, 1, "''");
    pos = text.find("'", pos + 2);
  }

  int id = get_id();
  query << "UPDATE task SET note='" << text << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Updated note for item %d", id);
    outlineRefreshScreen();
  }

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  rc = sqlite3_open(FTS_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  std::stringstream query2;
  //query.clear(); //this clear clears eof and fail flags so query.str(std::string());query.clear()
  query2 << "Update fts SET note='" << text << "' WHERE lm_id=" << id;

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL fts error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Updated note and fts entry for item %d", id);
    outlineRefreshScreen();
    editorSetMessage("Note update succeeeded"); 
  }
   
  sqlite3_close(db);

  E.dirty = 0;
}

void update_task_context(std::string &new_context, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int context_tid = context_map.at(new_context);
  query << "UPDATE task SET context_tid=" << context_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Setting context to %s succeeded", new_context.c_str());
  }

  sqlite3_close(db);
}

void update_task_folder(std::string& new_folder, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int folder_tid = folder_map.at(new_folder);
  query << "UPDATE task SET folder_tid=" << folder_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Setting folder to %s succeeded", new_folder.c_str());
  }

  sqlite3_close(db);
}

void toggle_completed(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET completed=" << ((row.completed) ? "NULL" : "date()") << ", "
        << "modified=datetime('now', '-" << TZ_OFFSET << " hours') "
        << "WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle completed succeeded");
    row.completed = !row.completed;
  }

  sqlite3_close(db);
    
}

void touch(void) {

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("'Touch' succeeded");
  }

  sqlite3_close(db);

}

void toggle_deleted(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table;

  switch(O.view) {
    case TASK:
      table = "task";
      break;
    case CONTEXT:
      table = "context";
      break;
    case FOLDER:
      table = "folder";
      break;
    case KEYWORD:
      table = "keyword";
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }

  query << "UPDATE " << table << " SET deleted=" << ((row.deleted) ? "False" : "True") << ", "
        <<  "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  // ? whether should move all tasks out here or in sync
  // UPDATE task SET folder_tid = 1 WHERE folder_tid = 4;
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle deleted succeeded");
    row.deleted = !row.deleted;
  }

  sqlite3_close(db);

}

void toggle_star(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table;
  std::string column;

  switch(O.view) {

    case TASK:
      table = "task";
      column = "star";
      break;

    case CONTEXT:
      table = "context";
      column = "\"default\"";
      break;

    case FOLDER:
      table = "folder";
      column = "private";
      break;

    case KEYWORD:
      table = "keyword";
      column = "star";
      break;

    default:
      outlineShowMessage("Not sure what you're trying to toggle");
      return;
  }

  query << "UPDATE " << table << " SET " << column << "=" << ((row.star) ? "False" : "True") << ", "
        << "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle star succeeded");
    row.star = !row.star;
  }
  sqlite3_close(db);
}

void update_row(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE task SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id;

    sqlite3 *db;
    char *err_msg = 0;
      
    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
      
    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);
    
    /**************fts virtual table update**********************/

    rc = sqlite3_open(FTS_DB.c_str(), &db);
    if (rc != SQLITE_OK) {
          
      outlineShowMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    std::stringstream query2;
    query2 << "UPDATE fts SET title='" << title << "' WHERE lm_id=" << row.id;
    rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);
      
    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
      } else {
        outlineShowMessage("Updated title and fts title entry for item %d", row.id);
      }
  
      sqlite3_close(db);

  } else { //row.id == -1
    insert_row(row);
  }
}

void update_container(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE "
          << ((O.view == CONTEXT) ? "context" : "folder")
          << " SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id; //I think id is correct

    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }

    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);

  } else { //row.id == -1
    insert_container(row);
  }
}

void update_keyword(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE "
          << "keyword "
          << "SET name='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id; //I think id is correct

    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }

    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);

  } else { //row.id == -1
    insert_keyword(row);
  }
}

int insert_keyword(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO "
        << "keyword "
        << "("
        << "name, "
        << "star, "
        << "deleted, "
        << "modified, "
        << "tid"
        << ") VALUES ("
        << "'" << title << "'," //title
        << " " << row.star << ","
        << " False," //default for context and private for folder
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //modified
        //<< " " << temporary_tid << ");"; //tid originally 100 but that is a legit client tid server id
        << " " << 0 << ");"; //unproven belief that you don't have to have multiple tids if you insert multiple keywords

  //temporary_tid++;

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  outlineShowMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

int insert_row(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO task ("
        << "priority, "
        << "title, "
        << "folder_tid, "
        << "context_tid, "
        << "star, "
        << "added, "
        << "note, "
        << "deleted, "
        << "created, "
        << "modified, "
        << "startdate "
        << ") VALUES ("
        << " 3," //priority
        << "'" << title << "'," //title
        //<< " 1," //folder_tid
        << ((O.folder == "") ? 1 : folder_map.at(O.folder)) << ", "
        //<< ((O.context != "search") ? context_map.at(O.context) : 1) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        //<< ((O.context == "search" || O.context == "recent" || O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << ((O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << " True," //star
        << "date()," //added
        //<< "'<This is a new note from sqlite>'," //note
        << "''," //note
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now', '-" << TZ_OFFSET << " hours')," // modified
        << " date()" //startdate
        << ");"; // RETURNING id;",

  /*
    not used:
    tid,
    tag,
    duetime,
    completed,
    duedate,
    repeat,
    remind
  */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  //should probably create a separate function that is a klugy
  //way of making up for fact that pg created tasks don't appear in fts db
  //"INSERT OR IGNORE INTO fts (title, lm_id) VALUES ('" << title << row.id << ");";

  std::stringstream query2;
  query2 << "INSERT INTO fts (title, lm_id) VALUES ('" << title << "', " << row.id << ")";

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open FTS database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return row.id;
  }

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing FTS insert: %s", err_msg);
    sqlite3_free(err_msg);
    return row.id; // would mean regular insert succeeded and fts failed - need to fix this
  }
  sqlite3_close(db);
  outlineShowMessage("Successfully inserted new row with id %d and indexed it", row.id);

  return row.id;
}

int insert_container(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO "
        //<< context
        << ((O.view == CONTEXT) ? "context" : "folder")
        << " ("
        << "title, "
        << "deleted, "
        << "created, "
        << "modified, "
        << "tid, "
        << ((O.view == CONTEXT) ? "\"default\", " : "private, ") //context -> "default"; folder -> private
      //  << "\"default\", " //folder does not have default
        << "textcolor "
        << ") VALUES ("
        << "'" << title << "'," //title
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now', '-" << TZ_OFFSET << " hours')," // modified
        //<< " 99999," //tid
        << " " << temporary_tid << "," //tid originally 100 but that is a legit client tid server id
        << " False," //default for context and private for folder
        << " 10" //textcolor
        << ");"; // RETURNING id;",

  temporary_tid++;      
  /*
   * not used:
     "default" (not sure why in quotes but may be system variable
      tid,
      icon (varchar 32)
      image (blob)
    */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  outlineShowMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

void update_rows(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  for (auto row: O.rows) {
    if (!(row.dirty)) continue;
    if (row.id != -1) {
      std::string title = row.title;
      size_t pos = title.find("'");
      while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

      std::stringstream query;
      query << "UPDATE task SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id;

      sqlite3 *db;
      char *err_msg = 0;
        
      int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
        
      if (rc != SQLITE_OK) {
            
        outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
        }
    
      rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

      if (rc != SQLITE_OK ) {
        outlineShowMessage("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return; // ? should we abort all other rows
      } else {
        row.dirty = false;
        updated_rows[n] = row.id;
        n++;
        sqlite3_close(db);
      }
    
    } else { 
      int id  = insert_row(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineShowMessage("There were no rows to update");
    return;
  }

  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    O.rows.at(updated_rows[j]).dirty = false; // 10-28-2019
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  outlineShowMessage("%s",  msg);
}

void update_solr(void) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("update_solr"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "update_solr"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(0); //presumably PyTuple_New(x) creates a tuple with that many elements
          //pValue = PyLong_FromLong(1);
          //pValue = Py_BuildValue("s", search_terms); // **************
          //PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineShowMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineShowMessage("Problem retrieving ids from solr!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineShowMessage("Was not able to find the function: update_solr!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      PyErr_Print();
      outlineShowMessage("Was not able to find the module: update_solr!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

  outlineShowMessage("%d items were added/updated to solr db", num);
}

int keyfromstringcpp(const std::string& key) {

  // c++20 = c++2a contains on associate containers
  //if (lookuptablemap.contains(key))
  if (lookuptablemap.count(key))
    return lookuptablemap.at(key); //note can't use [] on const unordered map since it could change map
  else
    return -1;
}

//this is so short should just incorporate in new commandfromstring
pfunc funcfromstring(const std::string& key) {

  // c++20 = c++2a contains on associate containers
  //if (lookuptablemap.contains(key))
  if (lookuptable.count(key))
    return lookuptable.at(key); //note can't use [] on const unordered map since it could change map
  else
    return nullptr;
}

int commandfromstringcpp(const std::string& key, std::size_t& found) { //for commands like find nemo - that consist of a command a space and further info

  // seems faster to do this but less general and forces to have 'case k:' explicitly, whereas would not need to if removed
  // should probably just drop this if entirely 08052020
  if (key.size() == 1) {
    found = 0;
    return key[0]; //? return keyfromstring[key] or just drop this if entirely
  }

  found = key.find(' ');
  if (found != std::string::npos) {
    std::string command = key.substr(0, found);
    return keyfromstringcpp(command);
  } else {
    found = 0;
    return keyfromstringcpp(key);
  }
}

// new one
pfunc commandfromstring(const std::string& key, std::size_t& found) { //for commands like find nemo - that consist of a command a space and further info

  found = key.find(' ');
  if (found != std::string::npos) {
    return funcfromstring(key.substr(0, found));
  } else {
    found = 0;
    return funcfromstring(key);
  }
}

[[ noreturn]] void die(const char *s) {
  // write is from <unistd.h> 
  //ssize_t write(int fildes, const void *buf, size_t nbytes);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; // minimum data to receive?
  raw.c_cc[VTIME] = 1; // timeout for read will return 0 if no bytes read

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}
/*** vim-like functions***/

/*
void move_up(void)
{
  int id;
  orow *row;

  if (O.fr > 0) O.fr--;
  O.fc = O.coloff = 0;
  get_note(id); //if id == -1 does not try to retrieve note
}
*/

/*** end vim-like functions***/

/*
outline_normal_map = move_up;
outline_normal_map[ARROW_UP] = move_up();
outline_normal_map['j'] = move_down();
outline_normal_map[ARROW_DOWN] = move_down()
*/

int readKey() {
  int nread;
  char c;

  /* read is from <unistd.h> - not sure why read is used and not getchar <stdio.h>
   prototype is: ssize_t read(int fd, void *buf, size_t count); 
   On success, the number of bytes read is returned (zero indicates end of file)
   So the while loop below just keeps cycling until a byte is read
   it does check to see if there was an error (nread == -1)*/

   /*Note that ctrl-key maps to ctrl-a => 1, ctrl-b => 2 etc.*/

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  /* if the character read was an escape, need to figure out if it was
     a recognized escape sequence or an isolated escape to switch from
     insert mode to normal mode or to reset in normal mode
  */

  if (c == '\x1b') {
    char seq[3];
    //outlineShowMessage("You pressed %d", c); //slz
    // the reads time out after 0.1 seconds
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Assumption is that seq[0] == '[' 
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //need 4 bytes
      if (seq[2] == '~') {
        //outlineShowMessage("You pressed %c%c%c", seq[0], seq[1], seq[2]); //slz
        switch (seq[1]) {
          case '1': return HOME_KEY; //not being issued
          case '3': return DEL_KEY; //<esc>[3~
          case '4': return END_KEY;  //not being issued
          case '5': return PAGE_UP; //<esc>[5~
          case '6': return PAGE_DOWN;  //<esc>[6~
          case '7': return HOME_KEY; //not being issued
          case '8': return END_KEY;  //not being issued
        }
      }
    } else {
        //outlineShowMessage("You pressed %c%c", seq[0], seq[1]); //slz
        switch (seq[1]) {
          case 'A': return ARROW_UP; //<esc>[A
          case 'B': return ARROW_DOWN; //<esc>[B
          case 'C': return ARROW_RIGHT; //<esc>[C
          case 'D': return ARROW_LEFT; //<esc>[D
          case 'H': return HOME_KEY; // <esc>[H - this one is being issued
          case 'F': return END_KEY;  // <esc>[F - this one is being issued
          case 'Z': return SHIFT_TAB; //<esc>[Z
      }
    }

    return '\x1b'; // if it doesn't match a known escape sequence like ] ... or O ... just return escape
  
  } else {
      //outlineShowMessage("You pressed %d", c); //slz
      return c;
  }
}

int getWindowSize(int *rows, int *cols) {

  // ioctl(), TIOCGWINXZ and struct windsize come from <sys/ioctl.h>
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;

    return 0;
  }
}

void F_open(int pos) { //C_open - by context
  std::string new_context;
  if (pos) {
    bool success = false;
    //structured bindings
    for (const auto & [k,v] : context_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.context = k;
        success = true;
        break;
      }
    }
    if (!success) {
      outlineShowMessage("%s is not a valid  context!", &O.command_line.c_str()[pos + 1]);
      O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
      return;
    }
  } else {
    outlineShowMessage("You did not provide a context!");
    O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
    return;
  }
  //EraseScreenRedrawLines(); //*****************************
  outlineShowMessage("\'%s\' will be opened", O.context.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);

  marked_entries.clear();
  O.folder = "";
  O.taskview = BY_CONTEXT;
  get_items(MAX);
  //O.mode = O.last_mode;
  O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
  return;
}

void F_openfolder(int pos) {
  if (pos) {
    bool success = false;
    for (const auto & [k,v] : folder_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.folder = k;
        success = true;
        break;
      }
    }
    if (!success) {
      outlineShowMessage("%s is not a valid  folder!", &O.command_line.c_str()[pos + 1]);
      O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
      return;
    }

  } else {
    outlineShowMessage("You did not provide a folder!");
    O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
    return;
  }
  outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  marked_entries.clear();
  O.context = "";
  O.taskview = BY_FOLDER;
  get_items(MAX);
  O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
  return;
}

void F_openkeyword(int pos) {
  if (!pos) {
    outlineShowMessage("You need to provide a keyword");
    O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
    return;
  }
 
  //O.keyword = O.command_line.substr(pos+1);
  std::string keyword = O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
    O.mode = O.last_mode;
    outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
    return;
  }

  O.keyword = keyword;  
  outlineShowMessage("\'%s\' will be opened", O.keyword.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  marked_entries.clear();
  O.context = "";
  O.folder = "";
  O.taskview = BY_KEYWORD;
  get_items(MAX);
  //editorRefreshScreen(); //in get_note
  O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
  return;
}

/*** outline operations ***/

void outlineInsertRow(int at, std::string&& s, bool star, bool deleted, bool completed, const char* modified) {
  /* note since only inserting blank line at top, don't really need at, s and also don't need size_t*/

  orow row;

  row.title = s;
  row.id = -1;
  row.star = star;
  row.deleted = deleted;
  row.completed = completed;
  row.dirty = true;
  strncpy(row.modified, modified, 16);

  row.mark = false;

  auto pos = O.rows.begin() + at;
  O.rows.insert(pos, row);
}

void outlineInsertChar(int c) {

  if (O.rows.size() == 0) return;

  orow& row = O.rows.at(O.fr);
  if (row.title.empty()) row.title.push_back(c);
  else row.title.insert(row.title.begin() + O.fc, c);
  row.dirty = true;
  O.fc++;
}

void outlineDelChar(void) {

  orow& row = O.rows.at(O.fr);

  if (O.rows.empty() || row.title.empty()) return;

  row.title.erase(row.title.begin() + O.fc);
  row.dirty = true;
}

void outlineBackspace(void) {
  orow& row = O.rows.at(O.fr);
  if (O.rows.empty() || row.title.empty() || O.fc == 0) return;
  row.title.erase(row.title.begin() + O.fc - 1);
  row.dirty = true;
  O.fc--;
}

/*** file i/o ***/

std::string outlineRowsToString() {
  std::string s = "";
  for (auto i: O.rows) {
      s += i.title;
      s += '\n';
  }
  s.pop_back(); //pop last return that we added
  return s;
}

void outlineSave(const std::string& fname) {
  if (O.rows.empty()) return;

  std::ofstream f;
  f.open(fname);
  f << outlineRowsToString();
  f.close();

  //outlineShowMessage("Can't save! I/O error: %s", strerror(errno));
  outlineShowMessage("saved to outline.txt");
}

/*** editor row operations ***/
//#include "editor_functions.h"

void f_cw(int repeat) {
  for (int j = 0; j < repeat; j++) {
    int start = E.fc;
    editorMoveEndWord();
    int end = E.fc;
    E.fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
    // text repeats once
  }
}

void f_caw(int repeat) {
   for (int i=0; i < repeat; i++)  editorDelWord();
    // text repeats once
}

void f_de(int repeat) {
  // should this use editorDelWord?
  for (int j = 0; j < repeat; j++) {
    if (E.fc == E.rows.at(E.fr).size() - 1) {
      editorDelChar();
      //start--;
      return;
    }
    int start = E.fc;
    editorMoveEndWord(); //correct one to use to emulate vim
    int end = E.fc;
    E.fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
  }
}

void f_dw(int repeat) {
  // should this use editorDelWord?
  for (int j = 0; j < repeat; j++) {
    //start = E.fc;
    if (E.fc == E.rows.at(E.fr).size() - 1) {
      editorDelChar();
      //start--;
      return;
    }
    if (E.rows.at(E.fr).at(E.fc + 1) == ' ') {
      //E.fc = start + 1;
      // this is not correct - it removes all spaces
      // woud need to use the not a find_first_not_of space and delete all of them
      editorDelChar();
      editorDelChar();
      continue;
    }
    int start = E.fc;
    editorMoveEndWord();
    int end = E.fc + 1;
    end = (E.rows.at(E.fr).size() > end) ? end : E.rows.at(E.fr).size() - 1;
    E.fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
  }
  //E.fc = start; 
}

void f_daw(int repeat) {
   for (int i=0; i < repeat; i++) editorDelWord();
}

void f_dG(int repeat) {
  if (E.rows.empty()) return;
  E.rows.erase(E.rows.begin() + E.fr, E.rows.end());
  if (E.rows.empty()) {
    E.fr = E.fc = E.cy = E.cx = E.line_offset = 0;
    E.mode = NO_ROWS;
  } else {
     E.fr--;
     E.fc = 0;
  }
}

void f_s(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorDelChar();
}

void f_x(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorDelChar();
}

void f_dd(int repeat) {
  //editorCreateSnapshot();
  int r = E.rows.size() - E.fr;
  repeat = (r >= repeat) ? repeat : r ;
  editorYankLine(repeat);
  for (int i = 0; i < repeat ; i++) editorDelRow(E.fr);
}

void f_d$(int repeat) {
  editorDeleteToEndOfLine();
  if (!E.rows.empty()) {
    int r = E.rows.size() - E.fr;
    repeat--;
    repeat = (r >= repeat) ? repeat : r ;
    //editorYankLine(E.repeat); //b/o 2 step won't really work right
    for (int i = 0; i < repeat ; i++) editorDelRow(E.fr);
    }
}

void f_change_case(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorChangeCase();
}

void f_replace(int repeat) {
  for (int i = 0; i < repeat; i++) {
    editorDelChar();
    editorInsertChar(E.last_typed[0]);
  }
}

void f_i(int repeat) {}

void f_I(int repeat) {
  editorMoveCursorBOL();
  E.fc = editorIndentAmount(E.fr);
}

void f_a(int repeat) {
  editorMoveCursor(ARROW_RIGHT);
}

void f_A(int repeat) {
  editorMoveCursorEOL();
  editorMoveCursor(ARROW_RIGHT); //works even though not in INSERT mode
}

void f_o(int repeat) {
  for (int n=0; n<repeat; n++) {
    editorInsertNewline(1);
    for (char const &c : E.last_typed) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
  }
}

void f_O(int repeat) {
  for (int n=0; n<repeat; n++) {
    editorInsertNewline(0);
    for (char const &c : E.last_typed) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
  }
}

//lands on punctuation, lands on blank lines ...
void f_w(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveNextWord();
  }
}

void f_b(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveBeginningWord();
  }
}

void f_e(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveEndWord();
  }
}

void f_0(int repeat) {
  editorMoveCursorBOL();
}

void f_$(int repeat) {
  editorMoveCursorEOL();
  for (int i=0; i<repeat-1; i++) {
    if (E.fr < E.rows.size() - 1) {
      E.fr++;
      editorMoveCursorEOL();
    } else break;  
  }
}

void editorDoRepeat(void) {

  editorCreateSnapshot();

  switch (E.last_command) {

    case 'i': case 'I': case 'a': case 'A': 
      cmd_map1[E.last_command](E.last_repeat);

      for (int n=0; n<E.last_repeat; n++) {
        for (char const &c : E.last_typed) {
          if (c == '\r') editorInsertReturn();
          else editorInsertChar(c);
        }
      }
      return;

    case 'o': case 'O':
      cmd_map2[E.last_command](E.last_repeat);
      return;

    case 'x': case C_dw: case C_daw: case C_dd: case C_de: case C_dG: case C_d$:
      cmd_map3[E.last_command](E.last_repeat);
      return;

    case C_cw: case C_caw: case 's':
      cmd_map4[E.last_command](E.last_repeat);

      for (char const &c : E.last_typed) {
        if (c == '\r') editorInsertReturn();
        else editorInsertChar(c);
      }
      return;

    case '~':
      f_change_case(E.last_repeat);
      return;

    case 'r':
      f_replace(E.last_repeat);
      return;

    default:
      editorSetMessage("You tried to repeat a command that doesn't repeat; Last command = %d", E.last_command);
      return;
  }
}

// used by numerous other functions to sometimes insert zero length rows
// and other times rows with characters like when retrieving a note
// fr is the position of the row with counting starting at zero
void editorInsertRow(int fr, std::string s) {
  auto pos = E.rows.begin() + fr;
  E.rows.insert(pos, s);
  E.dirty++;
}

// untested
void editorDelRow(int r) {
  //editorSetMessage("Row to delete = %d; E.numrows = %d", fr, E.numrows); 
  if (E.rows.empty()) return; // creation of NO_ROWS may make this unnecessary

  E.rows.erase(E.rows.begin() + r);
  if (E.rows.size() == 0) {
    E.fr = E.fc = E.cy = E.cx = E.line_offset = E.prev_line_offset = E.first_visible_row = E.last_visible_row = 0;

    E.mode = NO_ROWS;
    return;
  }

  E.dirty++;
  //editorSetMessage("Row deleted = %d; E.numrows after deletion = %d E.cx = %d E.row[fr].size = %d", fr,
  //E.numrows, E.cx, E.row[fr].size); 
}

/*** editor operations ***/
void editorInsertChar(int chr) {
  if (E.rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
  }
  std::string &row = E.rows.at(E.fr);
  row.insert(row.begin() + E.fc, chr); // this works if row empty
  //row.at(E.fc) = chr; // this doesn't work if row is empty
  E.dirty++;
  E.fc++;
}

void editorIndentRow(void) {
  if (E.rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
  }
  std::string &row = E.rows.at(E.fr);
  row.insert(0, E.indent, ' ');
  E.fc = E.indent;
  E.dirty++;
}

void editorInsertReturn(void) { // right now only used for editor->INSERT mode->'\r'
  if (E.rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
    editorInsertRow(0, std::string());
    E.fc = 0;
    E.fr = 1;
    return;
  }
    
  std::string& current_row = E.rows.at(E.fr);
  std::string new_row1(current_row.begin(), current_row.begin() + E.fc);
  std::string new_row2(current_row.begin() + E.fc, current_row.end());

  int indent = (E.smartindent) ? editorIndentAmount(E.fr) : 0;

  E.fr++;
  current_row = new_row1;
  E.rows.insert(E.rows.begin() + E.fr, new_row2);

  E.fc = 0;
  for (int j=0; j < indent; j++) editorInsertChar(' ');
}

// now 'o' and 'O' separated from '\r' (in INSERT mode)
//'o' -> direction == 1 and 'O' direction == 0
void editorInsertNewline(int direction) {
  /* note this func does position E.fc and E.fr*/
  if (E.rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
    return;
  }

  if (E.fr == 0 && direction == 0) { // this is for 'O'
    editorInsertRow(0, std::string());
    E.fc = 0;
    return;
  }
    
  int indent = (E.smartindent) ? editorIndentAmount(E.fr) : 0;

  std::string spaces;
  for (int j=0; j<indent; j++) {
      spaces.push_back(' ');
  }
  E.fc = indent;

  E.fr += direction;
  editorInsertRow(E.fr, spaces);
}

void editorDelChar(void) {
  if (E.rows.empty()) return; // creation of NO_ROWS may make this unnecessary
  std::string& row = E.rows.at(E.fr);
  if (row.empty() || E.fc > static_cast<int>(row.size()) - 1) return;
  row.erase(row.begin() + E.fc);
  E.dirty++;
}

void editorBackspace(void) {

  if (E.fc == 0 && E.fr == 0) return;

  std::string &row = E.rows.at(E.fr);
  if (E.fc > 0) {
    row.erase(row.begin() + E.fc - 1);
    E.fc--;
  } else if (row.size() > 1){
    E.rows.at(E.fr-1) = E.rows.at(E.fr-1) + row;
    editorDelRow(E.fr); //05082020
    E.fr--;
    E.fc = E.rows.at(E.fr).size();
  } else {
    editorDelRow(E.fr);
    E.fr--;
    E.fc = E.rows.at(E.fr).size();
}
  E.dirty++;
}

/*** file i/o ***/

std::string editorRowsToString(void) {

  std::string z = "";
  for (auto i: E.rows) {
      z += i;
      z += '\n';
  }
  if (!z.empty()) z.pop_back(); //pop last return that we added
  return z;
}

// erases note
void editorEraseScreen(void) {

  E.rows.clear();

  char lf_ret[10];
  //std::string lf_ret = fmt::format("\r\n\x1b[{}C", EDITOR_LEFT_MARGIN);
  //or
  //fmt::memory_buffer lf_ret;
  //format_to(lf_ret, "\r\n\x1b[{}C", EDITOR_LEFT_MARGIN)
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  char buf[32];
  //std::string lf_ret = fmt::format("\r\n\x1b[{}C", EDITOR_LEFT_MARGIN);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf);

  //erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

// currently used for sync log
void editorReadFile(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  display_text.str(std::string());
  display_text.clear();

  while (getline(f, line)) {
    display_text << line << '\n';
  }
  f.close();
}

void editorDisplayFile(void) {

  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1);
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1);
  ab.append(buf, strlen(buf));

  ab.append("\x1b[36m", 5); //this is foreground cyan - we'll see

  std::string row;
  std::string line;
  int row_num = -1;
  int line_num = 0;
  display_text.clear();
  display_text.seekg(0, std::ios::beg);
  while(std::getline(display_text, row, '\n')) {
    if (line_num > E.screenlines - 2) break;
    row_num++;
    if (row_num < initial_file_row) continue;
    if (static_cast<int>(row.size()) < E.screencols) {
      ab.append(row);
      ab.append(lf_ret);
      line_num++;
      continue;
    }
    //int n = 0;
    int n = row.size()/(E.screencols - 1) + ((row.size()%(E.screencols - 1)) ? 1 : 0);
    for(int i=0; i<n; i++) {
      line_num++;
      if (line_num > E.screenlines - 2) break;
      line = row.substr(0, E.screencols - 1);
      row.erase(0, E.screencols - 1);
      ab.append(line);
      ab.append(lf_ret);
    }
  }
  ab.append("\x1b[0m", 4);
  write(STDOUT_FILENO, ab.c_str(), ab.size()); //01012020
}

void open_in_vim(void){
  std::string filename;
  if (get_folder_tid(O.rows.at(O.fr).id) != 18) filename = "vim_file.txt";
  else filename = "vim_file.cpp";
  editorSaveNoteToFile(filename);
  std::stringstream s;
  s << "vim " << filename << " >/dev/tty";
  system(s.str().c_str());
  editorReadFileIntoNote(filename);
}

void editorDrawCodeRows(std::string &ab) {
  //save the current file to code_file with correct extension
  std::ofstream myfile;
  myfile.open("code_file"); 
  myfile << editorGenerateWWString();
  myfile.close();

  std::stringstream display;
  std::string line;

  // below is a quick hack folder tid = 18 -> code
  if (get_folder_tid(O.rows.at(O.fr).id) == 18) {
   procxx::process highlight("highlight", "code_file", "--out-format=xterm256", 
                             "--style=gruvbox-dark-hard-slz", "--syntax=cpp");
   // procxx::process highlight("bat", "code_file", "--style=plain", "--paging=never", "--color=always", "--language=cpp", "--theme=gruvbox");
    highlight.exec();
    while(getline(highlight.output(), line)) { display << line << '\n';}
  } else {
    procxx::process highlight("bat", "code_file", "--style=plain", "--paging=never", 
                               "--color=always", "--language=md.hbs", "--italic-text=always",
                               "--theme=gruvbox-markdown");
    highlight.exec();
    while(getline(highlight.output(), line)) { display << line << '\n';}
  }

  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf << "\x1b[" << TOP_MARGIN + 1 << ";" <<  EDITOR_LEFT_MARGIN + 1 << "H";
  ab.append(buf.str());

  // erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret, nchars);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 1 << ";" <<  EDITOR_LEFT_MARGIN + 1 << "H";
  ab.append(buf2.str()); //reposition cursor

  //std::string line;
  display.clear();
  display.seekg(0, std::ios::beg);
  int n = 0;
  while(std::getline(display, line, '\n')) {
    if (n >= E.line_offset) {
      ab.append(line);
      ab.append(lf_ret);
    }
    n++;
  }
  ab.append("\x1b[0m");

}

void editorReadFileIntoNote(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  E.rows.clear();
  E.fr = E.fc = E.cy = E.cx = E.line_offset = E.prev_line_offset = E.first_visible_row = E.last_visible_row = 0;

  while (getline(f, line)) {
    //replace(line.begin(), line.end(), '\t', "  ");
    size_t pos = line.find('\t');
    while(pos != std::string::npos) {
      line.replace(pos, 1, "  "); // number is number of chars to replace
      pos = line.find('\t');
    }
    E.rows.push_back(line);
  }
  f.close();

  E.dirty = true;
  editor_mode = true;
  editorRefreshScreen(true);
  return;
}

void editorSaveNoteToFile(const std::string &filename) {
  std::ofstream myfile;
  myfile.open(filename); //filename
  myfile << editorRowsToString();
  editorSetMessage("wrote file");
  myfile.close();
}

// positions the cursor ( O.cx and O.cy) and O.coloff and O.rowoff
void outlineScroll(void) {

  if(O.rows.empty()) {
      O.fr = O.fc = O.coloff = O.cx = O.cy = 0;
      return;
  }

  if (O.fr > O.screenlines + O.rowoff - 1) {
    O.rowoff =  O.fr - O.screenlines + 1;
  }

  if (O.fr < O.rowoff) {
    O.rowoff =  O.fr;
  }

  if (O.fc > O.screencols + O.coloff - 1) {
    O.coloff =  O.fc - O.screencols + 1;
  }

  if (O.fc < O.coloff) {
    O.coloff =  O.fc;
  }


  O.cx = O.fc - O.coloff;
  O.cy = O.fr - O.rowoff;
}

void outlineDrawRows(std::string& ab) {
  int j, k; //to swap highlight if O.highlight[1] < O.highlight[0]
  char buf[32];

  if (O.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN);

  int spaces;

  for (y = 0; y < O.screenlines; y++) {
    unsigned int fr = y + O.rowoff;
    if (fr > O.rows.size() - 1) return;
    orow& row = O.rows[fr];

    // if a line is long you only draw what fits on the screen
    //below solves problem when deleting chars from a scrolled long line
    unsigned int len = (fr == O.fr) ? row.title.size() - O.coloff : row.title.size(); //can run into this problem when deleting chars from a scrolled log line
    if (len > O.screencols) len = O.screencols;

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    //else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    else if (row.deleted) ab.append(COLOR_1); //red (specific color depends on theme)
    if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    //if (row.mark) ab.append("\x1b[46m", 5); //cyan background
    if (marked_entries.find(row.id) != marked_entries.end()) ab.append("\x1b[46m", 5);

    // below - only will get visual highlighting if it's the active
    // then also deals with column offset
    if (O.mode == VISUAL && fr == O.fr) {

       // below in case E.highlight[1] < E.highlight[0]
      k = (O.highlight[1] > O.highlight[0]) ? 1 : 0;
      j =!k;
      ab.append(&(row.title[O.coloff]), O.highlight[j] - O.coloff);
      ab.append("\x1b[48;5;242m", 11);
      ab.append(&(row.title[O.highlight[j]]), O.highlight[k]
                                             - O.highlight[j]);
      ab.append("\x1b[49m", 5); // return background to normal
      ab.append(&(row.title[O.highlight[k]]), len - O.highlight[k] + O.coloff);

    } else {
        // current row is only row that is scrolled if O.coloff != 0
        ab.append(&row.title[((fr == O.fr) ? O.coloff : 0)], len);
    }

    // for a 'dirty' (red) row or ithe selected row, the spaces make it look
    // like the whole row is highlighted
    spaces = O.screencols - len;
    for (int i=0; i < spaces; i++) ab.append(" ", 1);
    //abAppend(ab, "\x1b[1C", 4); // move over vertical line; below better for cell being edited
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - OUTLINE_RIGHT_MARGIN + 2); // + offset
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append("\x1b[0m"); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
}

void outlineDrawKeywords(std::string& ab) {

  if (O.rows.empty()) return;

  char lf_ret[16];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%dG", EDITOR_LEFT_MARGIN + 1); 
  ab.append(buf); 

  for (int y = 0; y < O.screenlines; y++) {
    unsigned int fr = y + O.rowoff;
    if (fr > O.rows.size() - 1) return;

    orow& row = O.rows[fr];

    size_t len = (row.title.size() > O.screencols) ? O.screencols : row.title.size();

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    //? do this after everything drawn
    if (fr == O.fr) ab.append("\x1b[48;5;236m"); // 236 is a grey

    ab.append(&row.title[0], len);
    int spaces = O.screencols - len; //needs to change but reveals stuff being written
    std::string s(spaces, ' '); 
    ab.append(s);
    ab.append("\x1b[0m"); // return background to normal /////
    ab.append(lf_ret);
  }
}

void outlineDrawSearchRows(std::string& ab) {
  char buf[32];

  if (O.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN);

  int spaces;

  for (y = 0; y < O.screenlines; y++) {
    int fr = y + O.rowoff;
    if (fr > static_cast<int>(O.rows.size()) - 1) return;
    orow& row = O.rows[fr];
    int len;

    //if (row.star) ab.append("\x1b[1m"); //bold
    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  

    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground

    //if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey but gets stopped as soon as it hits search highlight

    //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";

    // I think the following blows up if there are multiple search terms hits in a line longer than O.screencols

    if (row.title.size() <= O.screencols) // we know it fits
      ab.append(row.fts_title.c_str(), row.fts_title.size());
    else {
      size_t pos = row.fts_title.find("\x1b[49m");
      if (pos < O.screencols + 10) //length of highlight escape
        ab.append(row.fts_title.c_str(), O.screencols + 15); // length of highlight escape + remove formatting escape
      else
        ab.append(row.title.c_str(), O.screencols);
}
    len = (row.title.size() <= O.screencols) ? row.title.size() : O.screencols;
    spaces = O.screencols - len;
    for (int i=0; i < spaces; i++) ab.append(" ", 1);
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - OUTLINE_RIGHT_MARGIN + 2); //wouldn't need offset
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
}
void outlineDrawStatusBar(void) {

  std::string ab;  
  int len;
  /*
  so the below should 1) position the cursor on the status
  bar row and midscreen and 2) erase previous statusbar
  r -> l and then put the cursor back where it should be
  at OUTLINE_LEFT_MARGIN
  */

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH",
                             O.screenlines + TOP_MARGIN + 1,
                             O.screencols + OUTLINE_LEFT_MARGIN,
                             O.screenlines + TOP_MARGIN + 1,
                             1); //status bar comes right out to left margin

  ab.append(buf, strlen(buf));

  ab.append("\x1b[7m", 4); //switches to inverted colors
  char status[300], rstatus[80];

  std::string s;
  //std::string keywords = "";
  switch (O.view) {
      case TASK:
        //keywords = get_task_keywords(); can't be here because O.rows.empty() might be true
        switch (O.taskview) {
            case BY_SEARCH:
              s =  "search"; 
              break;
            case BY_FOLDER:
              s = O.folder + "[f]";
              break;
            case BY_CONTEXT:
              s = O.context + "[c]";
              break;
            case BY_RECENT:
              s = "recent";
              break;
            case BY_JOIN:
              s = O.context + "[c] + " + O.folder + "[f]";
              break;
            case BY_KEYWORD:
              s = O.keyword + "[k]";
              break;
        }    
        break;
      case CONTEXT:
        s = "Contexts";
        break;
      case FOLDER:
        s = "Folders";
        break;
      case KEYWORD:  
        s = "Keywords";
        break;
  }

  if (!O.rows.empty()) {

    orow& row = O.rows.at(O.fr);
    // note the format is for 15 chars - 12 from substring below and "[+]" when needed
    std::string truncated_title = row.title.substr(0, 12);
    if (E.dirty) truncated_title.append( "[+]");
    // needs to be here because O.rows could be empty
    std::string keywords = (O.view == TASK) ? get_task_keywords().first : ""; // see before and in switch

    len = snprintf(status, sizeof(status),
                              // because video is reversted [42 sets text to green and 49 undoes it
                              // also [0;35;7m -> because of 7m it reverses background and foreground
                              // I think the [0;7m is revert to normal and reverse video
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s...\x1b[0;35;7m %s \x1b[0;7m %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());


  } else {

    len = snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
  }

  ab.append(status, len);
  ab.append(" ", 1);

  char editor_status[200];
  int editor_len = 0;

  if (DEBUG) {
    if (!E.rows.empty()){
      int line = editorGetLineInRowWW(E.fr, E.fc);
      int line_char_count = editorGetLineCharCountWW(E.fr, line);
      int lines = editorGetLinesInRowWW(E.fr);

      editor_len = snprintf(editor_status,
                     sizeof(editor_status), "E.fr(0)=%d lines(1)=%d line(1)=%d E.fc(0)=%d LO=%d initial_row=%d last_row=%d line chrs(1)="
                                     "%d  E.cx(0)=%d E.cy(0)=%d E.scols(1)=%d",
                                     E.fr, lines, line, E.fc, E.line_offset, E.first_visible_row, E.last_visible_row, line_char_count, E.cx, E.cy, E.screencols);
    } else {
      editor_len =  snprintf(editor_status, sizeof(editor_status), "E.row is NULL E.cx = %d E.cy = %d  E.numrows = %ld E.line_offset = %d",
                                        E.cx, E.cy, E.rows.size(), E.line_offset);
    }
  }  

  ab.append(editor_status, editor_len);

  len = len + editor_len;
  //because of escapes
  len-=22;

  int rlen = snprintf(rstatus, sizeof(rstatus), "\x1b[1m %s %s\x1b[0;7m ", ((which_db == SQLITE) ? "sqlite:" : "postgres:"), TOSTRING(GIT_BRANCH));

  if (len > screencols - 1) len = screencols - 1;

  while (len < screencols - 1 ) {
    if ((screencols - len) == rlen - 9) { //10 of chars not printable
      ab.append(rstatus, rlen);
      break;
    } else {
      ab.append(" ", 1);
      len++;
    }
  }
  ab.append("\x1b[0m"); //switches back to normal formatting
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}
void return_cursor() {
  std::string ab;
  char buf[32];

  if (editor_mode) {
  // the lines below position the cursor where it should go
    if (E.mode != COMMAND_LINE){
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + TOP_MARGIN + 1, E.cx + EDITOR_LEFT_MARGIN + 1); //03022019
      ab.append(buf, strlen(buf));
    } else { //O.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", E.screenlines + 2 + TOP_MARGIN, E.command_line.size() + EDITOR_LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h", 6); // want to show cursor in non-DATABASE modes
    }
  } else {
    if (O.mode == ADD_KEYWORD){
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN + 60); //offset
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, EDITOR_LEFT_MARGIN); //offset
      ab.append(buf, strlen(buf));
    } else if (O.mode == SEARCH || O.mode == DATABASE) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;34m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN); //blue
      ab.append(buf, strlen(buf));
    } else if (O.mode != COMMAND_LINE) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;31m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN);
      ab.append(buf, strlen(buf));
      // below restores the cursor position based on O.cx and O.cy + margin
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, O.cx + OUTLINE_LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h", 6); // want to show cursor in non-DATABASE modes
  // no 'caret' if in COMMAND_LINE and want to move the cursor to the message line
    } else { //O.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", O.screenlines + 2 + TOP_MARGIN, O.command_line.size() + OUTLINE_LEFT_MARGIN); /// ****
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h", 6); // want to show cursor in non-DATABASE modes
    }
  }
  ab.append("\x1b[0m"); //return background to normal
  ab.append("\x1b[?25h"); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void outlineDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << O.screenlines + 2 + TOP_MARGIN << ";"
      << screencols/2 << "H" << "\x1b[1K\x1b["
      << O.screenlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab += buf.str();

  int msglen = strlen(O.message);
  if (msglen > screencols/2) msglen = screencols/2;
  ab.append(O.message, msglen);
}

void outlineRefreshScreen(void) {

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  //if (O.view != KEYWORD) {
  if (O.mode != ADD_KEYWORD) {
    for (unsigned int j=TOP_MARGIN; j < O.screenlines + 1; j++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
      O.screencols + OUTLINE_LEFT_MARGIN + 17); 
      ab.append(buf, strlen(buf));
    }
  }
  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , OUTLINE_LEFT_MARGIN + 1); // *****************
  ab.append(buf, strlen(buf));

  if (O.mode == SEARCH) outlineDrawSearchRows(ab);
  //else if (O.view == KEYWORD) outlineDrawKeywords(ab);
  else if (O.mode == ADD_KEYWORD) outlineDrawKeywords(ab);
  else  outlineDrawRows(ab);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void outlineShowMessage(const char *fmt, ...) {
  char message[100];  
  std::string ab;
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  std::vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap); //free a va_list

  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << O.screenlines + 2 + TOP_MARGIN << ";"
      << screencols/2 << "H" << "\x1b[1K\x1b["
      << O.screenlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab = buf.str();
  //ab.append("\x1b[0m"); //checking if necessary

  int msglen = strlen(message);
  if (msglen > screencols/2) msglen = screencols/2;
  ab.append(message, msglen);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

//Note: outlineMoveCursor worries about moving cursor beyond the size of the row
//OutlineScroll worries about moving cursor beyond the screen
void outlineMoveCursor(int key) {

  if (O.rows.empty()) return;

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (O.fc > 0) O.fc--; 
      // arowing left in NORMAL puts you into DATABASE mode
     // else {
     //   O.mode = DATABASE;
     //   if (O.view == TASK) display_item_info(O.rows.at(O.fr).id);
     //   else display_container_info(O.rows.at(O.fr).id);
     //   O.command[0] = '\0';
     //   O.repeat = 0;
     // }
      break;

    case ARROW_RIGHT:
    case 'l':
    {
      O.fc++;
      break;
    }
    case ARROW_UP:
    case 'k':
      if (O.fr > 0) O.fr--; 
      O.fc = O.coloff = 0; 

      if (O.view == TASK) {
        if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
        else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
      } else display_container_info(O.rows.at(O.fr).id);
      break;

    case ARROW_DOWN:
    case 'j':
      if (O.fr < O.rows.size() - 1) O.fr++;
      O.fc = O.coloff = 0;
      if (O.view == TASK) {
        if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
        else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
      } else display_container_info(O.rows.at(O.fr).id);
      break;
  }

  orow& row = O.rows.at(O.fr);
  if (O.fc >= row.title.size()) O.fc = row.title.size() - (O.mode != INSERT);
  if (row.title.empty()) O.fc = 0;
}

// depends on readKey()
//void outlineProcessKeypress(void) {
void outlineProcessKeypress(int c) { //prototype has int = 0  
  int start, end, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  c = (!c) ? readKey() : c;
  switch (O.mode) {
  size_t n;
  //switch (int c = readKey(); O.mode)  //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          O.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          O.command_line.clear();
          outlineShowMessage(":");
          O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.mode = INSERT;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.fc = O.fr = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          editorEraseScreen(); //erases the note area
          O.mode = INSERT;
          return;
      }

      n = strlen(O.command);
      O.command[n] = c;
      O.command[n+1] = '\0';

      //C_gt
      if (keyfromstringcpp(O.command) == C_gt) {
        std::map<std::string, int>::iterator it;

        if ((O.view == TASK && O.taskview == BY_FOLDER) || O.view == FOLDER) {
          if (!O.folder.empty()) {
            it = folder_map.find(O.folder);
            it++;
            if (it == folder_map.end()) it = folder_map.begin();
          } else {
            it = folder_map.begin();
          }
          O.folder = it->first;
          outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
        } else {
          if (O.context.empty() || O.taskview == BY_SEARCH) {
            it = context_map.begin();
          } else {
            it = context_map.find(O.context);
            it++;
            if (it == context_map.end()) it = context_map.begin();
          }
          O.context = it->first;
          outlineShowMessage("\'%s\' will be opened", O.context.c_str());
        }
        //EraseScreenRedrawLines(); //*****************************
        get_items(MAX);
        O.command[0] = '\0';
        return;
      }

      return; //in NO_ROWS - do nothing if no command match

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
          if (O.view == TASK)  {
            update_row();
            if (lm_browser) {
              if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
              else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
            }   
          } else if (O.view == CONTEXT || O.view == FOLDER) update_container();
          else if (O.view == KEYWORD) update_keyword();
          O.command[0] = '\0'; //11-26-2019
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          //outlineShowMessage("");
          return;

        case HOME_KEY:
          O.fc = 0;
          return;

        case END_KEY:
          {
            orow& row = O.rows.at(O.fr);
          if (row.title.size()) O.fc = row.title.size(); // mimics vim to remove - 1;
          return;
          }

        case BACKSPACE:
          outlineBackspace();
          return;

        case DEL_KEY:
          outlineDelChar();
          return;

        case '\t':
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          outlineMoveCursor(c);
          return;

        case CTRL_KEY('z'):
          // not in use
          return;

        case '\x1b':
          O.command[0] = '\0';
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          outlineShowMessage("");
          return;

        default:
          outlineInsertChar(c);
          return;
      } //end of switch inside INSERT

     // return; //End of case INSERT: No need for a return at the end of INSERT because we insert the characters that fall through in switch default:

    case NORMAL:  

      if (c == '\x1b') {
        outlineShowMessage("");
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
 
      /*leading digit is a multiplier*/
      //if (isdigit(c))  //equiv to if (c > 47 && c < 58)

      if ((c > 47 && c < 58) && (strlen(O.command) == 0)) {

        if (O.repeat == 0 && c == 48) {

        } else if (O.repeat == 0) {
          O.repeat = c - 48;
          return;
        }  else {
          O.repeat = O.repeat*10 + c - 48;
          return;
        }
      }

      if ( O.repeat == 0 ) O.repeat = 1;

      n = strlen(O.command);
      O.command[n] = c;
      O.command[n+1] = '\0';

      // arrow keys are mapped above ascii range (start at 1000) so
      // can't just have below be keyfromstring return command[0]
      //probably also faster to check if n=0 and just return c as below
      //also means that any key sequence ending in arrow key will move cursor
      command = (n && c < 128) ? keyfromstringcpp(O.command) : c;

      switch(command) {  

        case '\r':
          {
          orow& row = O.rows.at(O.fr);

          if(row.dirty){
            if (O.view == TASK) update_row();
            else if (O.view == CONTEXT || O.view == FOLDER) update_container();
            else if (O.view == KEYWORD) update_keyword();
            O.command[0] = '\0'; //11-26-2019
            O.mode = NORMAL;
            if (O.fc > 0) O.fc--;
            //outlineShowMessage("");
            return;
          }

          // return means retrieve items by context or folder
          // do this in database mode
          if (O.view == CONTEXT) {
            O.context = row.title;
            O.folder = "";
            O.taskview = BY_CONTEXT;
            outlineShowMessage("\'%s\' will be opened", O.context.c_str());
            O.command_line = "o " + O.context;
          } else if (O.view == FOLDER) {
            O.folder = row.title;
            O.context = "";
            O.taskview = BY_FOLDER;
            outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
            O.command_line = "o " + O.folder;
          } else if (O.view == KEYWORD) {
            O.keyword = row.title;
            O.folder = "";
            O.context = "";
            O.taskview = BY_KEYWORD;
            outlineShowMessage("\'%s\' will be opened", O.keyword.c_str());
            O.command_line = "ok " + O.keyword;
          }
          }

          command_history.push_back(O.command_line);
          page_hx_idx++;
          page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
          marked_entries.clear();

          get_items(MAX);
          O.command[0] = '\0';
          O.mode = NORMAL;
          return;

        //Tab cycles between OUTLINE and DATABASE modes
        case '\t':
          O.fc = 0; //intentionally leave O.fr wherever it is
          O.mode = DATABASE;
          if (O.view == TASK) display_item_info(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          outlineShowMessage("");
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        //SHIFT_Tab cycles between OUTLINE and SEARCH modes
        case SHIFT_TAB:
          if (O.taskview == BY_SEARCH) {
            O.fc = 0; //intentionally leave O.fr wherever it is
            O.mode = SEARCH;
            O.command[0] = '\0';
            O.repeat = 0;
            outlineShowMessage("");
          }
          return;

        case 'i':
          O.mode = INSERT;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 's':
          for (int i = 0; i < O.repeat; i++) outlineDelChar();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = INSERT;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
          return;

        case 'x':
          for (int i = 0; i < O.repeat; i++) outlineDelChar();
          O.command[0] = '\0';
          O.repeat = 0;
          return;
        
        case 'r':
          O.command[0] = '\0';
          O.mode = REPLACE;
          return;

        case '~':
          for (int i = 0; i < O.repeat; i++) outlineChangeCase();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'a':
          O.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
          outlineMoveCursor(ARROW_RIGHT);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'A':
          outlineMoveCursorEOL();
          O.mode = INSERT; //needs to be here for movecursor to work at EOLs
          outlineMoveCursor(ARROW_RIGHT);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'w':
          outlineMoveNextWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'b':
          outlineMoveBeginningWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'e':
          outlineMoveEndWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case '0':
          if (!O.rows.empty()) O.fc = 0; // this was commented out - not sure why but might be interfering with O.repeat
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case '$':
          outlineMoveCursorEOL();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'I':
          if (!O.rows.empty()) {
            O.fc = 0;
            O.mode = 1;
            outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          }
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'G':
          O.fc = 0;
          O.fr = O.rows.size() - 1;
          O.command[0] = '\0';
          O.repeat = 0;
          if (O.view == TASK) get_note(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          return;
      
        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.fc = O.fr = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          editorEraseScreen(); //erases the note area
          O.mode = INSERT;
          return;

        case ':':
          outlineShowMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          O.last_mode = O.mode;
          O.mode = COMMAND_LINE;
          return;

        case 'v':
          O.mode = VISUAL;
          O.command[0] = '\0';
          O.repeat = 0;
          O.highlight[0] = O.highlight[1] = O.fc;
          outlineShowMessage("\x1b[1m-- VISUAL --\x1b[0m");
          return;

        case 'p':  
          if (!string_buffer.empty()) outlinePasteString();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case '*':  
          outlineGetWordUnderCursor();
          outlineFindNextWord(); 
          O.command[0] = '\0';
          return;

        case 'm':
          O.rows.at(O.fr).mark = !O.rows.at(O.fr).mark;
          if (O.rows.at(O.fr).mark) {
            marked_entries.insert(O.rows.at(O.fr).id);
          } else {
            marked_entries.erase(O.rows.at(O.fr).id);
          }  
          outlineShowMessage("Toggle mark for item %d", O.rows.at(O.fr).id);
          O.command[0] = '\0';
          return;

        case 'n':
          outlineFindNextWord();
          O.command[0] = '\0';
          return;

        case 'u':
          //could be used to update solr - would use U
          O.command[0] = '\0';
          return;

        case '^':
          generate_persistent_html_file(O.rows.at(O.fr).id);
          O.command[0] = '\0';
          return;

        case PAGE_UP:
        case PAGE_DOWN:  
          if (page_history.size() == 1 && O.view == TASK) {
            O.mode = NORMAL;
            O.command[0] = '\0';
            O.command_line.clear();
            return;
          }
          {
          //size_t temp;  
          O.mode = COMMAND_LINE;
          if (c == PAGE_UP) {

            // if O.view!=TASK and PAGE_UP - moves back to last page
            if (O.view == TASK) { //if in a container viewa - fall through to previous TASK view page

              if (page_hx_idx == 0) page_hx_idx = page_history.size() - 1;
              else page_hx_idx--;
            }

          } else {
            if (page_hx_idx == (page_history.size() - 1)) page_hx_idx = 0;
            else page_hx_idx++;
          }

          //temp = page_hx_idx;
          //outlineShowMessage(":%s", page_history.at(page_hx_idx).c_str());
          O.command_line = page_history.at(page_hx_idx);
          outlineProcessKeypress('\r');
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.command_line.clear();
          //page_history.pop_back();
          page_history.erase(page_history.begin() + page_hx_idx);
          //page_hx_idx = temp;
          page_hx_idx--;
          outlineShowMessage(":%s", page_history.at(page_hx_idx).c_str());
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          for (int j = 0;j < O.repeat;j++) outlineMoveCursor(c);
          O.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          O.repeat = 0;
          return;

        /* now using simpler links but preserving the code for a while
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          update_note(); 
          O.command[0] = '\0';
          return;
        */  

        case C_daw:
          for (int i = 0; i < O.repeat; i++) outlineDelWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_dw:
          for (int j = 0; j < O.repeat; j++) {
            start = O.fc;
            outlineMoveEndWord2();
            end = O.fc;
            O.fc = start;
            for (int j = 0; j < end - start + 2; j++) outlineDelChar();
          }
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_de:
          start = O.fc;
          outlineMoveEndWord(); //correct one to use to emulate vim
          end = O.fc;
          O.fc = start; 
          for (int j = 0; j < end - start + 1; j++) outlineDelChar();
          O.fc = (start < O.rows.at(O.fr).title.size()) ? start : O.rows.at(O.fr).title.size() -1;
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_d$:
        case C_dd: //note not standard definition but seems right for outline
          outlineDeleteToEndOfLine();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        //tested with repeat on one line
        case C_cw:
          for (int j = 0; j < O.repeat; j++) {
            start = O.fc;
            outlineMoveEndWord();
            end = O.fc;
            O.fc = start;
            for (int j = 0; j < end - start + 1; j++) outlineDelChar();
          }
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = INSERT;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        //tested with repeat on one line
        case C_caw:
          for (int i = 0; i < O.repeat; i++) outlineDelWord();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = INSERT;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case C_gg:
          O.fc = O.rowoff = 0;
          O.fr = O.repeat-1; //this needs to take into account O.rowoff
          O.command[0] = '\0';
          O.repeat = 0;
          if (O.view == TASK) get_note(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          return;

        case C_gt:
          // may actually work in context_mode
          {
          std::map<std::string, int>::iterator it;

          if ((O.view == TASK && O.taskview == BY_FOLDER) || O.view == FOLDER) {
            if (!O.folder.empty()) {
              it = folder_map.find(O.folder);
              it++;
              if (it == folder_map.end()) it = folder_map.begin();
            } else {
              it = folder_map.begin();
            }
            O.folder = it->first;
            outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
          } else {
            if (O.context.empty() || O.context == "search") {
              it = context_map.begin();
            } else {
              it = context_map.find(O.context);
              it++;
              if (it == context_map.end()) it = context_map.begin();
            }
            O.context = it->first;
            outlineShowMessage("\'%s\' will be opened", O.context.c_str());
          }
          //EraseScreenRedrawLines(); //*****************************
          get_items(MAX);
          //editorRefreshScreen(); //in get_note
          O.command[0] = '\0';
          return;
          }

        case C_edit: //CTRL-W,CTRL-W
          // can't edit note if rows_are_contexts
          if (!(O.view == TASK)) {
            O.command[0] = '\0';
            O.mode = NORMAL;
            outlineShowMessage("Contexts and Folders do not have notes to edit");
            return;
          }
          {
          int id = get_id();
          if (id != -1) {
            outlineShowMessage("Edit note %d", id);
            outlineRefreshScreen();
            //editor_mode needs go before get_note in case we retrieved item via a search
            editor_mode = true;
            get_note(id); //if id == -1 does not try to retrieve note
            E.mode = NORMAL;
            E.command[0] = '\0';
          } else {
            outlineShowMessage("You need to save item before you can "
                                   "create a note");
          }
          O.command[0] = '\0';
          O.mode = NORMAL;
          return;
          }

        default:
          // if a single char or sequence of chars doesn't match then
          // do nothing - the next char may generate a match
          return;

      } //end of keyfromstring switch under case NORMAL 

      //return; // end of case NORMAL (don't think it can be reached)

    case COMMAND_LINE:

      switch(c) {

        case '\x1b': 
          O.mode = NORMAL;
          outlineShowMessage(""); 
          return;

        case ARROW_UP:
          if (command_history.empty()) return;
          if (cmd_hx_idx == 0) cmd_hx_idx = command_history.size() - 1;
          else cmd_hx_idx--;
          //cmd_hx_idx = (cmd_hx_idx == 0) ? command_history.size() - 1 : --cmd_hx_idx;
          outlineShowMessage(":%s", command_history.at(cmd_hx_idx).c_str());
          O.command_line = command_history.at(cmd_hx_idx);
          return;

        case ARROW_DOWN:
          if (command_history.empty()) return;
          if (cmd_hx_idx == (command_history.size() - 1)) cmd_hx_idx = 0;
          else cmd_hx_idx++;
          //cmd_hx_idx = (cmd_hx_idx == (command_history.size() - 1)) ? 0 : ++cmd_hx_idx;
          outlineShowMessage(":%s", command_history.at(cmd_hx_idx).c_str());
          O.command_line = command_history.at(cmd_hx_idx);
          return;

        case '\r':
          std::size_t pos;

          // passes back position of space (if there is one) in var pos
          //auto f = commandfromstring(O.command_line, pos); 
          if (auto f = commandfromstring(O.command_line, pos);f) {
            f(pos);
            return;
          }
          /*
          command = commandfromstringcpp(O.command_line, pos); 
          if (auto it = cmd_map.find(command);it != cmd_map.end()) {
            it->second(pos);
            return;
          }
         */

          command = commandfromstringcpp(O.command_line, pos); 
          switch(command) {

            case 'w':
            case C_write:  
              if (O.view == TASK) update_rows();
              O.mode = O.last_mode;
              O.command_line.clear();
              return;

            case 'x':
              if (O.view == TASK) update_rows();
              write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
              write(STDOUT_FILENO, "\x1b[H", 3); //sends cursor home (upper left)
              exit(0);
              //return;

            case 'r':
            case C_refresh:
              //EraseScreenRedrawLines(); //doesn't seem necessary ****11162019*************************

              if (O.view == TASK) {
                outlineShowMessage("Tasks will be refreshed");
                if (O.taskview == BY_SEARCH)
                  ;//search_db();
                else
                  get_items(MAX);
              } else {
                outlineShowMessage("contexts/folders will be refreshed");
                get_containers();
              }
              O.mode = O.last_mode;
              return;

            //in vim create new window and edit a file in it - here creates new item
            case 'n':
            case C_new: 
              outlineInsertRow(0, "", true, false, false, BASE_DATE);
              O.fc = O.fr = O.rowoff = 0;
              O.command[0] = '\0';
              O.repeat = 0;
              outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
              editorEraseScreen(); //erases the note area
              O.mode = INSERT;

              {
              int fd;
              std::string fn = "assets/" + CURRENT_NOTE_FILE;
              if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
                lock.l_type = F_WRLCK;  
                if (fcntl(fd, F_SETLK, &lock) != -1) {
                write(fd, " ", 1);
                lock.l_type = F_UNLCK;
                fcntl(fd, F_SETLK, &lock);
                } else outlineShowMessage("Couldn't lock file");
              } else outlineShowMessage("Couldn't open file");
              }

              return;

            case 'e':
            case C_edit: //edit the note of the current item
              if (!(O.view == TASK)) {
                O.command[0] = '\0';
                O.mode = NORMAL;
                outlineShowMessage("Only tasks have notes to edit!");
                return;
              }
              {
              int id = get_id();
              if (id != -1) {
                outlineShowMessage("Edit note %d", id);
                outlineRefreshScreen();
                //editor_mode needs go before get_note in case we retrieved item via a search
                editor_mode = true;
                get_note(id); //if id == -1 does not try to retrieve note
                //E.fr = E.fc = E.cy = E.cx = E.line_offset = E.prev_line_offset = E.first_visible_row = E.last_visible_row = 0;
                if (E.rows.empty()) {
                  E.mode = INSERT;
                  editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
                  editorRefreshScreen(false);
                } else E.mode = NORMAL;
                //E.mode = (E.rows.empty()) ? INSERT : NORMAL;
                //E.mode = NORMAL;
                E.command[0] = '\0';
              } else {
                outlineShowMessage("You need to save item before you can "
                                  "create a note");
              }
              O.command[0] = '\0';
              O.mode = NORMAL;
              return;
              }

            case 'l':  
            case C_linked: //linked, related, l
              {
              std::string keywords = get_task_keywords().first;
              if (keywords.empty()) {
                outlineShowMessage("The current entry has no keywords");
              } else {
                O.keyword = keywords;
                O.context = "No Context";
                O.folder = "No Folder";
                O.taskview = BY_KEYWORD;
                get_linked_items(MAX);
                command_history.push_back("ok " + keywords); ///////////////////////////////////////////////////////
              }   //O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
              }
              return;

            case C_find: //catches 'fin' and 'find' 
              
              if (O.command_line.size() < 6) {
                outlineShowMessage("You need more characters");
                return;
              }  
              {
              O.context = "";
              O.folder = "";
              O.taskview = BY_SEARCH;
              //O.mode = SEARCH; ////// it's in get_items_by_id
              search_terms = O.command_line.substr(pos+1);
              std::transform(search_terms.begin(), search_terms.end(), search_terms.begin(), ::tolower);
              command_history.push_back(O.command_line); 

             // page_history.push_back(O.command_line); 
             // page_hx_idx = page_history.size() - 1;

              page_hx_idx++;
              page_history.insert(page_history.begin() + page_hx_idx, O.command_line);


              outlineShowMessage("Searching for %s", search_terms.c_str());
              search_db(search_terms);
              }
              return;

            case C_fts: 
              {
              std::string s;
              if (O.command_line.size() < 6) {
                outlineShowMessage("You need more characters");
                return;
              }  

              //EraseScreenRedrawLines(); //*****************************
              O.context = "search";
              s = O.command_line.substr(pos+1);
              search_db(s); //fts5_sqlite(s);
              //std::istringstream iss(s);
              //for(std::string ss; iss >> s; ) search_terms2.push_back(ss);
              if (O.mode != NO_ROWS) {
                //O.mode = SEARCH;
                get_note(get_id());
              } else {
                outlineRefreshScreen();
              }
              }
              return;

            case C_update: //update solr
              update_solr();
              O.mode = NORMAL;
              return;

            case 'c':
            case C_contexts: //catches context, contexts and c
              if (!pos) {
                editorEraseScreen();
                O.view = CONTEXT;
                command_history.push_back(O.command_line); ///////////////////////////////////////////////////////
                get_containers();
                O.mode = NORMAL;
                outlineShowMessage("Retrieved contexts");
                return;
              } else {

                std::string new_context;
                bool success = false;
                if (O.command_line.size() > 5) { //this needs work - it's really that pos+1 to end needs to be > 2
                  // structured bindings
                  for (const auto & [k,v] : context_map) {
                    if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                      new_context = k;
                      success = true;
                      break;
                    }
                  }
                  if (!success) {
                    outlineShowMessage("What you typed did not match any context");
                    return;
                  }

                } else {
                  outlineShowMessage("You need to provide at least 3 characters "
                                    "that match a context!");

                  O.command_line.clear();
                  return;
                }
                success = false;
                for (const auto& it : O.rows) {
                  if (it.mark) {
                    update_task_context(new_context, it.id);
                    success = true;
                  }
                }

                if (success) {
                  outlineShowMessage("Marked tasks moved into context %s", new_context.c_str());
                } else {
                  update_task_context(new_context, O.rows.at(O.fr).id);
                  outlineShowMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
                }
                O.mode = O.last_mode;
                if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
                //O.command_line.clear(); //calling : in all modes should clear command_line
                return;
                }

            case 'f':
            case C_folders: //catches folder, folders and f
              if (!pos) {
                editorEraseScreen();
                O.view = FOLDER;
                command_history.push_back(O.command_line); 
                get_containers();
                O.mode = NORMAL;
                outlineShowMessage("Retrieved folders");
                return;
              } else {

                std::string new_folder;
                bool success = false;
                if (O.command_line.size() > 5) {  //this needs work - it's really that pos+1 to end needs to be > 2
                  // structured bindings
                  for (const auto & [k,v] : folder_map) {
                    if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                      new_folder = k;
                      success = true;
                      break;
                    }
                  }
                  if (!success) {
                    outlineShowMessage("What you typed did not match any folder");
                    return;
                  }

                } else {
                  outlineShowMessage("You need to provide at least 3 characters "
                                    "that match a folder!");

                  O.command_line.clear();
                  return;
                }
                success = false;
                for (const auto& it : O.rows) {
                  if (it.mark) {
                    update_task_folder(new_folder, it.id);
                    success = true;
                  }
                }

                if (success) {
                  outlineShowMessage("Marked tasks moved into folder %s", new_folder.c_str());
                } else {
                  update_task_folder(new_folder, O.rows.at(O.fr).id);
                  outlineShowMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
                }
                O.mode = O.last_mode;
                if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
                return;
               }

            case 'k':
            case C_keywords: //catches keyword, keywords, kw and k
              if (!pos) {
                editorEraseScreen();
                O.view = KEYWORD;
                command_history.push_back(O.command_line); 
                get_containers(); //O.mode = NORMAL is in get_containers
                outlineShowMessage("Retrieved keywords");
                return;
              }  

              // only do this if there was text after C_keywords
              if (O.last_mode == NO_ROWS) return;

              {
              std::string keyword = O.command_line.substr(pos+1);
              if (!keyword_exists(keyword)) {
                  O.mode = O.last_mode;
                  outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
                  return;
              }

              if (marked_entries.empty()) {
                add_task_keyword(keyword, O.rows.at(O.fr).id);
                outlineShowMessage("No tasks were marked so added %s to current task", keyword.c_str());
              } else {
                for (const auto& id : marked_entries) {
                  add_task_keyword(keyword, id);
                }
                outlineShowMessage("Marked tasks had keyword %s added", keyword.c_str());
              }
              }
              O.mode = O.last_mode;
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              return;

            case C_updatefolder:
            case C_updatecontext:
              if (!pos) {
                current_task_id = O.rows.at(O.fr).id;
                editorEraseScreen();
                if (command == C_updatefolder) O.view = FOLDER;
                else O.view = CONTEXT;
                command_history.push_back(O.command_line); 
                get_containers(); //O.mode = NORMAL is in get_containers
                O.mode = ADD_KEYWORD;
                outlineShowMessage("Select keyword to add to marked or current entry");
              }  
              return;

            case C_addkeyword: //catches addkeyword, addkw 
              if (!pos) {
                current_task_id = O.rows.at(O.fr).id;
                editorEraseScreen();
                O.view = KEYWORD;
                command_history.push_back(O.command_line); 
                get_containers(); //O.mode = NORMAL is in get_containers
                O.mode = ADD_KEYWORD;
                outlineShowMessage("Select keyword to add to marked or current entry");
                return;
              }  

              // only do this if there was text after C_addkeyword
              if (O.last_mode == NO_ROWS) return;

              {
              std::string keyword = O.command_line.substr(pos+1);
              if (!keyword_exists(keyword)) {
                  O.mode = O.last_mode;
                  outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
                  return;
              }

              if (marked_entries.empty()) {
                add_task_keyword(keyword, O.rows.at(O.fr).id);
                outlineShowMessage("No tasks were marked so added %s to current task", keyword.c_str());
              } else {
                for (const auto& id : marked_entries) {
                  add_task_keyword(keyword, id);
                }
                outlineShowMessage("Marked tasks had keyword %s added", keyword.c_str());
              }
              }
              O.mode = O.last_mode;
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              return;

            case C_movetocontext:
              {
              std::string new_context;
              bool success = false;
              if (O.command_line.size() > 5) {
                // structured bindings
                for (const auto & [k,v] : context_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    new_context = k;
                    success = true;
                    break;
                  }
                }
                if (!success) {
                  outlineShowMessage("What you typed did not match any context");
                  return;
                }

              } else {
                outlineShowMessage("You need to provide at least 3 characters "
                                  "that match a context!");

                O.command_line.clear();
                return;
              }
              success = false;
              for (const auto& it : O.rows) {
                if (it.mark) {
                  update_task_context(new_context, it.id);
                  success = true;
                }
              }

              if (success) {
                outlineShowMessage("Marked tasks moved into context %s", new_context.c_str());
              } else {
                update_task_context(new_context, O.rows.at(O.fr).id);
                outlineShowMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
              }
              O.mode = O.last_mode;
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              //O.command_line.clear(); //calling : in all modes should clear command_line
              return;
              }

            case C_movetofolder:
              {
              std::string new_folder;
              bool success = false;
              if (O.command_line.size() > 5) {
                // structured bindings
                for (const auto & [k,v] : folder_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    new_folder = k;
                    success = true;
                    break;
                  }
                }
                if (!success) {
                  outlineShowMessage("What you typed did not match any folder");
                  return;
                }

              } else {
                outlineShowMessage("You need to provide at least 3 characters "
                                  "that match a folder!");

                O.command_line.clear();
                return;
              }
              success = false;
              for (const auto& it : O.rows) {
                if (it.mark) {
                  update_task_folder(new_folder, it.id);
                  success = true;
                }
              }

              if (success) {
                outlineShowMessage("Marked tasks moved into folder %s", new_folder.c_str());
              } else {
                update_task_folder(new_folder, O.rows.at(O.fr).id);
                outlineShowMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
              }
              O.mode = O.last_mode;
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              return;
              }

              /*
            case C_deletekeywords:
              outlineShowMessage("Keyword(s) for task %d will be deleted and fts updated if sqlite", O.rows.at(O.fr).id);
              delete_task_keywords();
              O.mode = O.last_mode;
              return;

            //case 'o': //klugy since commandfromstring doesn't connect single letters to commands;note that this will notify user of error if no context given  
            case C_open: //by context
              F_open(pos);
              return;
              {
              std::string new_context;
              if (pos) {
                bool success = false;
                //structured bindings
                for (const auto & [k,v] : context_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    new_context = k;
                    success = true;
                    break;
                  }
                }
                if (!success) {
                  outlineShowMessage("%s is not a valid  context!", &O.command_line.c_str()[pos + 1]);
                  O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
                  return;
                }
              } else {
                outlineShowMessage("You did not provide a context!");
                O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
                return;
              }
              //EraseScreenRedrawLines(); //////////
              outlineShowMessage("\'%s\' will be opened", new_context.c_str());
              command_history.push_back(O.command_line);
              page_hx_idx++;
              page_history.insert(page_history.begin() + page_hx_idx, O.command_line);

              marked_entries.clear();
              O.context = new_context;
              O.folder = "";
              O.taskview = BY_CONTEXT;
              get_items(MAX);
              //O.mode = O.last_mode;
              O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
              return;
              }

            case C_openfolder:
              if (pos) {
                bool success = false;
                for (const auto & [k,v] : folder_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    O.folder = k;
                    success = true;
                    break;
                  }
                }
                if (!success) {
                  outlineShowMessage("%s is not a valid  folder!", &O.command_line.c_str()[pos + 1]);
                  O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
                  return;
                }

              } else {
                outlineShowMessage("You did not provide a folder!");
                O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
                return;
              }
              outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
              command_history.push_back(O.command_line);
              page_hx_idx++;
              page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
              marked_entries.clear();
              O.context = "";
              O.taskview = BY_FOLDER;
              get_items(MAX);
              O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
              return;

            case C_openkeyword:

              if (!pos) {
                outlineShowMessage("You need to provide a keyword");
                O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
                return;
              }

              O.keyword = O.command_line.substr(pos+1);
              outlineShowMessage("\'%s\' will be opened", O.keyword.c_str());
              command_history.push_back(O.command_line);
              page_hx_idx++;
              page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
              marked_entries.clear();
              O.context = "";
              O.folder = "";
              O.taskview = BY_KEYWORD;
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
              return;
              */


            case C_join:
              {
              if (O.view != TASK || O.taskview == BY_JOIN || pos == 0) {
                outlineShowMessage("You are either in a view where you can't join or provided no join container");
                O.mode = NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
                O.mode = O.last_mode; //NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
                return;
              }
              bool success = false;

              if (O.taskview == BY_CONTEXT) {
                for (const auto & [k,v] : folder_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    O.folder = k;
                    success = true;
                    break;
                  }
                }
              } else if (O.taskview == BY_FOLDER) {
                for (const auto & [k,v] : context_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    O.context = k;
                    success = true;
                    break;
                  }
                }
              }

              if (!success) {
                outlineShowMessage("You did not provide a valid folder or context to join!");
                O.command_line.resize(1);
                return;
              }

               outlineShowMessage("Will join \'%s\' with \'%s\'", O.folder.c_str(), O.context.c_str());
               O.taskview = BY_JOIN;
               get_items(MAX);
               return;
               }

            case C_sort:
              if (pos && O.view == TASK && O.taskview != BY_SEARCH) {
                O.sort = O.command_line.substr(pos + 1);
                get_items(MAX);
                outlineShowMessage("sorted by \'%s\'", O.sort.c_str());
              } else {
                outlineShowMessage("Currently can't sort search, which is sorted on best match");
              }
              return;

            case C_recent:
              outlineShowMessage("Will retrieve recent items");
              command_history.push_back(O.command_line);
              page_history.push_back(O.command_line);
              page_hx_idx = page_history.size() - 1;
              marked_entries.clear();
              O.context = "No Context";
              O.taskview = BY_RECENT;
              O.folder = "No Folder";
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              return;

            case 's':
            case C_showall:
              if (O.view == TASK) {
                O.show_deleted = !O.show_deleted;
                O.show_completed = !O.show_completed;
                if (O.taskview == BY_SEARCH)
                  ; //search_db();
                else
                  get_items(MAX);
              }
              outlineShowMessage((O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
              return;

            case C_synch:
              synchronize(0); // do actual sync
              map_context_titles();
              map_folder_titles();
              initial_file_row = 0; //for arrowing or displaying files
              O.mode = FILE_DISPLAY; // needs to appear before editorDisplayFile
              outlineShowMessage("Synching local db and server and displaying results");
              editorReadFile("log");
              editorDisplayFile();//put them in the command mode case synch
              return;

            case C_synch_test:
              synchronize(1); //1 -> report_only

              initial_file_row = 0; //for arrowing or displaying files
              O.mode = FILE_DISPLAY; // needs to appear before editorDisplayFile
              outlineShowMessage("Testing synching local db and server and displaying results");
              editorReadFile("log");
              editorDisplayFile();//put them in the command mode case synch
              return;

            case C_readfile:
              {
              std::string filename;
              if (pos) filename = O.command_line.substr(pos+1);
              else filename = "example.cpp";
              editorReadFileIntoNote(filename);
              outlineShowMessage("Note generated from file: %s", filename.c_str());
              }
              //O.mode = O.last_mode; editorReadfile puts in editor mode but probably shouldn't
              O.command[0] = '\0';
              return;
             

            //case CTRL_KEY('s'):
            case C_savefile:  
              command_history.push_back(O.command_line); ///////////////////////////////////////////////////////
              {
              std::string filename;
              if (pos) filename = O.command_line.substr(pos+1);
              else filename = "example.cpp";
              editorSaveNoteToFile(filename);
              O.mode = O.last_mode;
              outlineShowMessage("Note saved to file: %s", filename.c_str());
              }
              return;

            case C_valgrind:
              initial_file_row = 0; //for arrowing or displaying files
              editorReadFile("valgrind_log_file");
              editorDisplayFile();//put them in the command mode case synch
              O.last_mode = O.mode;
              O.mode = FILE_DISPLAY;
              return;

            case C_delmarks:
              for (auto& it : O.rows) {
                it.mark = false;}
              if (O.view == TASK) marked_entries.clear();
              O.mode = O.last_mode;
              outlineShowMessage("Marks all deleted");
              return;

            case C_database:
              O.mode = DATABASE;
              O.command[0] = '\0';
              O.repeat = 0;
              return;

            case C_search:
              if (O.taskview == BY_SEARCH) {
                O.mode = SEARCH;
                O.command[0] = '\0';
                O.repeat = 0;
              }
              return;

            case C_saveoutline: //saveoutline, so
              if (pos) {
                std::string fname = O.command_line.substr(pos + 1);
                outlineSave(fname);
                O.mode = NORMAL;
                outlineShowMessage("Saved outline to %s", fname.c_str());
              } else {
                outlineShowMessage("You didn't provide a file name!");
              }
              return;

            case C_vim:
              open_in_vim();
              O.command[0] = '\0';
              O.repeat = 0;
              O.mode = NORMAL;
              return;

            case C_merge:
              {

              int count = count_if(O.rows.begin(), O.rows.end(), [](const orow &row){return row.mark;});
              if (count < 2) {
                outlineShowMessage("Number of marked items = %d", count);
                O.mode = O.last_mode;
                return;
              }
              outlineInsertRow(0, "[Merged note]", true, false, false, BASE_DATE);
              insert_row(O.rows.at(0)); 
              E.rows.clear();
              
              int n = 0;
              auto it = O.rows.begin();
              for(;;) {
                it = find_if(it+1, O.rows.end(), [](const orow &row){return row.mark;});
                if (it != O.rows.end()) merge_note(it->id);
                else break;
                n++;
              }
              outlineShowMessage("Number of notes merged = %d", n);
              }
              //outlineRefreshScreen(); 
              O.fc = O.fr = O.rowoff = 0; //O.fr = 0 needs to come before update_note
              editorRefreshScreen(true);
              update_note();
              //E.dirty = 1;
              O.command[0] = '\0';
              O.repeat = 0;
              O.mode = NORMAL;
              return;

            case C_quit:
            case 'q':
              {
              bool unsaved_changes = false;
              for (auto it : O.rows) {
                if (it.dirty) {
                  unsaved_changes = true;
                  break;
                }
              }
              if (unsaved_changes) {
                O.mode = NORMAL;
                outlineShowMessage("No db write since last change");
           
              } else {
                write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
                write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
                Py_FinalizeEx();
                if (which_db == SQLITE) sqlite3_close(S.db);
                else PQfinish(conn);
                exit(0);
              }
              return;
              }

            case C_quit0: //catches both :q! and :quit!
              write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
              write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
              Py_FinalizeEx();
              exit(0);

            case 'h':
            case C_help:
              if (!pos) {             
                /*This needs to be changed to show database text not ext file*/
                initial_file_row = 0;
                O.last_mode = O.mode;
                O.mode = FILE_DISPLAY;
                outlineShowMessage("Displaying help file");
                editorReadFile("listmanager_commands");
                editorDisplayFile();
              } else {
                //std::string help_topic = O.command_line.substr(pos+1);
                search_terms = O.command_line.substr(pos+1);
                O.context = "";
                O.folder = "";
                O.taskview = BY_SEARCH;
                //O.mode = SEARCH; ////// it's in get_items_by_id
                std::transform(search_terms.begin(), search_terms.end(), search_terms.begin(), ::tolower);
                command_history.push_back(O.command_line); 
                search_db2(search_terms);
                outlineShowMessage("Will look for help on %s", search_terms.c_str());
                //O.mode = NORMAL;
              }  
              return;

            case C_highlight:
              editorHighlightWordsByPosition();
              O.mode = O.last_mode;
              outlineShowMessage("%s highlighted", search_terms.c_str());
              return;

            case C_syntax:
              if (pos) {
                std::string action = O.command_line.substr(pos + 1);
                if (action == "on") {
                  E.highlight_syntax = true;
                  outlineShowMessage("Syntax highlighting will be turned on");
                } else if (action == "off") {
                  E.highlight_syntax = false;
                  outlineShowMessage("Syntax highlighting will be turned off");
                } else {outlineShowMessage("The syntax is 'sh on' or 'sh off'"); }
              } else {outlineShowMessage("The syntax is 'sh on' or 'sh off'");}
              editorRefreshScreen(true);
              O.mode = O.last_mode;
              //O.command[0] = '\0';
              //O.command_line.clear();
              //O.repeat = 0;
              return;

            case C_set:
              {
              std::string action = O.command_line.substr(pos + 1);
              if (pos) {
                if (action == "spell") {
                  E.spellcheck = true;
                  outlineShowMessage("Spellcheck active");
                } else if (action == "nospell") {
                  E.spellcheck = false;
                  outlineShowMessage("Spellcheck off");
                } else {outlineShowMessage("Unknown option: %s", action.c_str()); }
              } else {outlineShowMessage("Unknown option: %s", action.c_str());}
              editorRefreshScreen(true);
              O.mode = O.last_mode;
              //O.command[0] = '\0';
              //O.command_line.clear();
              //O.repeat = 0;
              }
              return;

            case C_spellcheck:
              E.spellcheck = !E.spellcheck;
              if (E.spellcheck) editorSpellCheck();
              else editorRefreshScreen(true);
              O.mode = O.last_mode;
              outlineShowMessage("Spellcheck");
              return;

            case C_pdf:
              //generate_pdf();
              return;

            case 'b':
            case C_browser:
              generate_persistent_html_file(O.rows.at(O.fr).id);
              O.command[0] = '\0';
              O.mode = O.last_mode;
              return;

            default: // default for commandfromstring

              //\x1b[41m => red background
              outlineShowMessage("\x1b[41mNot an outline command: %s\x1b[0m", O.command_line.c_str());
              O.mode = NORMAL;
              return;

          // command_history.push_back()    

          } //end of commandfromstring switch within '\r' of case COMMAND_LINE

        default: //default for switch 'c' in case COMMAND_LINE
          if (c == DEL_KEY || c == BACKSPACE) {
            if (!O.command_line.empty()) O.command_line.pop_back();
          } else {
            O.command_line.push_back(c);
          }
          outlineShowMessage(":%s", O.command_line.c_str());

        } // end of 'c' switch within case COMMAND_LINE

      return; //end of outer case COMMAND_LINE

    // note database mode always deals with current character regardless of previously typed char
    // since all commands are one char.
    case DATABASE:

      switch (c) {

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            O.fr = (O.screenlines > O.fr) ? 0 : O.fr - O.screenlines; //O.fr and O.screenlines are unsigned ints
          } else if (c == PAGE_DOWN) {
             O.fr += O.screenlines;
             if (O.fr > O.rows.size() - 1) O.fr = O.rows.size() - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
        case 'h':
        case 'l':
          outlineMoveCursor(c);
          return;

        //TAB toggles between DATABASE and OUTLINE mode
        //Escape one way between DATABASE and OUTLINE
        case '\x1b':
        case '\t':  
          O.fc = 0; 
          O.mode = NORMAL;
          if (O.view == TASK) get_note(O.rows.at(O.fr).id);
          outlineShowMessage("");
          return;

        case 'G':
          O.fc = 0;
          O.fr = O.rows.size() - 1;
          O.command[0] = '\0';
          O.repeat = 0;
          if (O.view == TASK) display_item_info(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          return;

        case 'g':
          O.fr = O.fc = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          if (O.view == TASK) display_item_info(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id); 
          return;


        case '\r':
          if (orow& row = O.rows.at(O.fr); O.view == TASK) {
            O.command[0] = '\0';
            return;
          } else if (O.view == CONTEXT) {
            O.context = row.title;
            O.folder = "";
            O.taskview = BY_CONTEXT;
          } else if (O.view == FOLDER) {
            O.folder = row.title;
            O.context = "";
            O.taskview = BY_FOLDER;
          } else if (O.view == KEYWORD) {
            O.keyword = row.title;
            O.folder = "";
            O.context = "";
            O.taskview = BY_KEYWORD;
          }
          get_items(MAX);
          O.command[0] = '\0';
          return;

        case ':':
          outlineShowMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          O.last_mode = O.mode;
          O.mode = COMMAND_LINE;
          return;

        case 'x':
          if (O.view == TASK) toggle_completed();
          return;

        case 'f':
          if (O.view == TASK) {
            std::string keywords = get_task_keywords().first;
            if (keywords.empty()) {
              outlineShowMessage("The current entry has no keywords");
            } else {
              O.keyword = keywords;
              O.context = "No Context";
              O.folder = "No Folder";
              O.taskview = BY_KEYWORD;

              get_linked_items(MAX);
            }   //O.mode = (O.last_mode == DATABASE) ? DATABASE : NORMAL;
          }
          return;

        case 'd':
          toggle_deleted();
          return;

        case 't': //touch
          if (O.view == TASK) touch();
          return;

        case '*':
          toggle_star(); //row.star -> "default" (sqlite) or default for context and private for folder
          return;

        case 'm':
          if (O.view == TASK) {
            O.rows.at(O.fr).mark = !O.rows.at(O.fr).mark;
          outlineShowMessage("Toggle mark for item %d", O.rows.at(O.fr).id);
          }
          return;

        case 's':
          if (O.view == TASK) {
            O.show_deleted = !O.show_deleted;
            O.show_completed = !O.show_completed;
            if (O.taskview == BY_SEARCH)
              ; //search_db();
            else
              get_items(MAX);
          }
          outlineShowMessage((O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
          return;

        case 'r':
          if (O.view == TASK) {
            outlineShowMessage("Tasks will be refreshed");
            if (O.taskview == BY_SEARCH)
              ; //search_db();
             else
              get_items(MAX);
          } else {
            outlineShowMessage("contexts will be refreshed");
            get_containers();
          }
          return;
  
        case 'i': //display item info
          display_item_info(O.rows.at(O.fr).id);
          return;
  
        default:
          if (c < 33 || c > 127) outlineShowMessage("<%d> doesn't do anything in DATABASE mode", c);
          else outlineShowMessage("<%c> doesn't do anything in DATABASE mode", c);
          return;
      } // end of switch(c) in case DATABASLE

      //return; //end of outer case DATABASE //won't be executed

    case SEARCH:  
      switch (c) {

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            O.fr = (O.screenlines > O.fr) ? 0 : O.fr - O.screenlines; //O.fr and O.screenlines are unsigned ints
          } else if (c == PAGE_DOWN) {
             O.fr += O.screenlines;
             if (O.fr > O.rows.size() - 1) O.fr = O.rows.size() - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
        case 'h':
        case 'l':
          outlineMoveCursor(c);
          return;

        //TAB and SHIFT_TAB moves from SEARCH to OUTLINE NORMAL mode but SHIFT_TAB gets back
        case '\t':  
        case SHIFT_TAB:  
          O.fc = 0; //otherwise END in DATABASE mode could have done bad things
          O.mode = NORMAL;
          get_note(O.rows.at(O.fr).id); //only needed if previous comand was 'i'
          outlineShowMessage("");
          return;

        default:
          O.mode = NORMAL;
          O.command[0] = '\0'; 
          outlineProcessKeypress(c); 
          //if (c < 33 || c > 127) outlineShowMessage("<%d> doesn't do anything in SEARCH mode", c);
          //else outlineShowMessage("<%c> doesn't do anything in SEARCH mode", c);
          return;
      } // end of switch(c) in case SEARCH

    case VISUAL:
  
      switch (c) {
  
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          outlineMoveCursor(c);
          O.highlight[1] = O.fc; //this needs to be getFileCol
          return;
  
        case 'x':
          O.repeat = abs(O.highlight[1] - O.highlight[0]) + 1;
          outlineYankString(); //reportedly segfaults on the editor side

          // the delete below requires positioning the cursor
          O.fc = (O.highlight[1] > O.highlight[0]) ? O.highlight[0] : O.highlight[1];

          for (int i = 0; i < O.repeat; i++) {
            outlineDelChar(); //uses editorDeleteChar2! on editor side
          }
          if (O.fc) O.fc--; 
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineShowMessage("");
          return;
  
        case 'y':  
          O.repeat = O.highlight[1] - O.highlight[0] + 1;
          O.fc = O.highlight[0];
          outlineYankString();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineShowMessage("");
          return;
  
        case '\x1b':
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      //return; //end of case VISUAL (return here would not be executed)

    case REPLACE: 

      if (c == '\x1b') {
        O.command[0] = '\0';
        O.repeat = 0;
        O.mode = NORMAL;
        return;
      }

      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar();
        outlineInsertChar(c);
      }

      O.repeat = 0;
      O.command[0] = '\0';
      O.mode = NORMAL;

      return; //////// end of outer case REPLACE

    case ADD_KEYWORD:

      switch(c) {

        case '\x1b':
          {
          O.mode = COMMAND_LINE;
          size_t temp = page_hx_idx;  
          outlineShowMessage(":%s", page_history.at(page_hx_idx).c_str());
          O.command_line = page_history.at(page_hx_idx);
          outlineProcessKeypress('\r');
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.command_line.clear();
          page_history.pop_back();
          page_hx_idx = temp;
          O.repeat = 0;
          current_task_id = -1; //not sure this is right
          }
          return;

        // could be generalized for folders and contexts too  
        // update_task_folder and update_task_context
        // update_task_context(std::string &, int)
        // maybe make update_task_context(int, int)
        case '\r':
          {
          orow& row = O.rows.at(O.fr); //currently highlighted keyword
          if (marked_entries.empty()) {
            switch (O.view) {
              case KEYWORD:
                add_task_keyword(row.id, current_task_id);
                outlineShowMessage("No tasks were marked so added keyword %s to current task",
                                   row.title.c_str());
                break;
              case FOLDER:
                update_task_folder(row.title, current_task_id);
                outlineShowMessage("No tasks were marked so current task had folder changed to %s",
                                   row.title.c_str());
                break;
              case CONTEXT:
                update_task_context(row.title, current_task_id);
                outlineShowMessage("No tasks were marked so current task had context changed to %s",
                                   row.title.c_str());
                break;
            }
          } else {
            for (const auto& task_id : marked_entries) {
              //add_task_keyword(row.id, task_id);
              switch (O.view) {
                case KEYWORD:
                  add_task_keyword(row.id, task_id);
                  outlineShowMessage("Marked tasks had keyword %s added",
                                     row.title.c_str());
                break;
                case FOLDER:
                  update_task_folder(row.title, task_id);
                  outlineShowMessage("Marked tasks had folder changed to %s",
                                     row.title.c_str());
                break;
                case CONTEXT:
                  update_task_context(row.title, task_id);
                  outlineShowMessage("Marked tasks had context changed to %s",
                                     row.title.c_str());
                break;
              }
            }
          }
          }

          O.command[0] = '\0'; //might not be necessary
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
          //for (int j = 0;j < O.repeat;j++) outlineMoveCursor(c);
          outlineMoveCursor(c);
          //O.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          //O.repeat = 0;
          return;

        default:
          if (c < 33 || c > 127) outlineShowMessage("<%d> doesn't do anything in ADD_KEYWORD mode", c);
          else outlineShowMessage("<%c> doesn't do anything in ADD_KEYWORD mode", c);
          return;
      }

      return; //in ADD_KEYWORDS - do nothing if no c match

    case FILE_DISPLAY: 

      switch (c) {
  
        case ARROW_UP:
        case 'k':
          initial_file_row--;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          break;

        case ARROW_DOWN:
        case 'j':
          initial_file_row++;
          break;

        case PAGE_UP:
          initial_file_row = initial_file_row - E.screenlines;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          break;

        case PAGE_DOWN:
          initial_file_row = initial_file_row + E.screenlines;
          break;

        case ':':
          outlineShowMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          //O.last_mode was set when entering file mode
          O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          O.mode = O.last_mode;
          if (O.view == TASK) get_note(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("");
          return;
      }

      editorDisplayFile();

      return;
  } //end of outer switch(O.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
} //end outlineProcessKeypress

void synchronize(int report_only) { //using 1 or 0

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("synchronize"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "synchronize"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = PyLong_FromLong(report_only);
          //pValue = Py_BuildValue("s", search_terms); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineShowMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineShowMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineShowMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      outlineShowMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  if (report_only) outlineShowMessage("Number of tasks/items that would be affected is %d", num);
  else outlineShowMessage("Number of tasks/items that were affected is %d", num);
}

int get_id(void) { 
  return O.rows.at(O.fr).id;
}

void outlineChangeCase() {
  orow& row = O.rows.at(O.fr);
  char d = row.title.at(O.fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    outlineMoveCursor(ARROW_RIGHT);
    return;
  }
  outlineDelChar();
  outlineInsertChar(d);
}

void outlineYankLine(int n){

  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(O.rows.at(O.fr+i).title);
  }
  // set string_buffer to "" to signal should paste line and not chars
  string_buffer.clear();
}

void outlineYankString() {
  orow& row = O.rows.at(O.fr);
  string_buffer.clear();

  std::string::const_iterator first = row.title.begin() + O.highlight[0];
  std::string::const_iterator last = row.title.begin() + O.highlight[1];
  string_buffer = std::string(first, last);
}

void outlinePasteString(void) {
  orow& row = O.rows.at(O.fr);

  if (O.rows.empty() || string_buffer.empty()) return;

  row.title.insert(row.title.begin() + O.fc, string_buffer.begin(), string_buffer.end());
  O.fc += string_buffer.size();
  row.dirty = true;
}

void outlineDelWord() {

  orow& row = O.rows.at(O.fr);
  if (row.title[O.fc] < 48) return;

  int i,j,x;
  for (i = O.fc; i > -1; i--){
    if (row.title[i] < 48) break;
    }
  for (j = O.fc; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  row.dirty = true;
  //outlineShowMessage("i = %d, j = %d", i, j ); 
}

void outlineDeleteToEndOfLine(void) {
  orow& row = O.rows.at(O.fr);
  row.title.resize(O.fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  row.dirty = true;
  }

void outlineMoveCursorEOL() {

  O.fc = O.rows.at(O.fr).title.size() - 1;  //if O.cx > O.screencols will be adjusted in EditorScroll
}

// not same as 'e' but moves to end of word or stays put if already on end of word
void outlineMoveEndWord2() {
  int j;
  orow& row = O.rows.at(O.fr);

  for (j = O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = j - 1;
}

void outlineMoveNextWord() {
  int j;
  orow& row = O.rows.at(O.fr);

  for (j = O.fc + 1; j < row.title.size(); j++) {
    if (row.title[j] < 48) break;
  }

  O.fc = j - 1;

  for (j = O.fc + 1; j < row.title.size() ; j++) { //+1
    if (row.title[j] > 48) break;
  }
  O.fc = j;
}

void outlineMoveBeginningWord() {
  orow& row = O.rows.at(O.fr);
  if (O.fc == 0) return;
  for (;;) {
    if (row.title[O.fc - 1] < 48) O.fc--;
    else break;
    if (O.fc == 0) return;
  }
  int i;
  for (i = O.fc - 1; i > -1; i--){
    if (row.title[i] < 48) break;
  }
  O.fc = i + 1;
}

void outlineMoveEndWord() {
  orow& row = O.rows.at(O.fr);
  if (O.fc == row.title.size() - 1) return;
  for (;;) {
    if (row.title[O.fc + 1] < 48) O.fc++;
    else break;
    if (O.fc == row.title.size() - 1) return;
  }
  int j;
  for (j = O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = j - 1;
}

void outlineGetWordUnderCursor(){
  std::string& title = O.rows.at(O.fr).title;
  if (title[O.fc] < 48) return;

  int i,j,x;

  for (i = O.fc - 1; i > -1; i--){
    if (title[i] < 48) break;
  }

  for (j = O.fc + 1; j < title.size() ; j++) {
    if (title[j] < 48) break;
  }

  for (x=i+1; x<j; x++) {
      search_string.push_back(title.at(x));
  }
  outlineShowMessage("word under cursor: <%s>", search_string.c_str());
}

void outlineFindNextWord() {

  int y, x;
  y = O.fr;
  x = O.fc + 1; //in case sitting on beginning of the word
   for (unsigned int n=0; n < O.rows.size(); n++) {
     std::string& title = O.rows.at(y).title;
     auto res = std::search(std::begin(title) + x, std::end(title), std::begin(search_string), std::end(search_string));
     if (res != std::end(title)) {
         O.fr = y;
         O.fc = res - title.begin();
         break;
     }
     y++;
     x = 0;
     if (y == O.rows.size()) y = 0;
   }

    outlineShowMessage("x = %d; y = %d", x, y); 
}

// returns true if display needs to scroll and false if it doesn't
bool editorScroll(void) {

  if (E.rows.empty()) {
    E.fr = E.fc = E.cy = E.cx = E.line_offset = E.prev_line_offset = E.first_visible_row = E.last_visible_row = 0;
    return true;
  }

  if (E.fr >= E.rows.size()) E.fr = E.rows.size() - 1;

  int row_size = E.rows.at(E.fr).size();
  if (E.fc >= row_size) E.fc = row_size - (E.mode != INSERT); 

  if (E.fc < 0) E.fc = 0;

  E.cx = editorGetScreenXFromRowColWW(E.fr, E.fc);
  int cy = editorGetScreenYFromRowColWW(E.fr, E.fc);

  //my guess is that if you wanted to adjust E.line_offset to take into account that you wanted
  // to only have full rows at the top (easier for drawing code) you would do it here.
  // something like E.screenlines goes from 4 to 5 so that adjusts E.cy
  // it's complicated and may not be worth it.

  //deal with scroll insufficient to include the current line
  if (cy > E.screenlines + E.line_offset - 1) {
    E.line_offset = cy - E.screenlines + 1; ////
    int line_offset = E.line_offset;
    E.first_visible_row = editorGetInitialRow(line_offset);
    E.line_offset = line_offset;
  }

 //let's check if the current line_offset is causing there to be an incomplete row at the top

  // this may further increase E.line_offset so we can start
  // at the top with the first line of some row
  // and not start mid-row which complicates drawing the rows

  //deal with scrol where current line wouldn't be visible because we're scrolled too far
  if (cy < E.line_offset) {
    E.line_offset = cy;
    E.first_visible_row = editorGetInitialRow(E.line_offset, SCROLL_UP);
  }
  if (E.line_offset == 0) E.first_visible_row = 0; 

  E.cy = cy - E.line_offset;

  // vim seems to want full rows to be displayed although I am not sure
  // it's either helpful or worth it but this is a placeholder for the idea

  // returns true if display needs to scroll and false if it doesn't
  if (E.line_offset == E.prev_line_offset) return false;
  else {E.prev_line_offset = E.line_offset; return true;}
}

/* this exists to create a text file that has the proper
 * line breaks based on screen width for syntax highlighters
 * to operate on 
 * Produces a text string that starts at the first line of the
 * file and ends on the last visible line
 */
std::string editorGenerateWWString(void) {
  if (E.rows.empty()) return "";

  std::string ab = "";
  int y = -E.line_offset;
  int filerow = 0;

  for (;;) {
    if (filerow == E.rows.size()) {E.last_visible_row = filerow - 1; return ab;}

    std::string row = E.rows.at(filerow);
    
    if (row.empty()) {
      if (y == E.screenlines - 1) return ab;
      ab.append("\n");
      filerow++;
      y++;
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      /* this is needed because it deals where the end of the line doesn't have a space*/
      if (row.substr(pos+1).size() <= E.screencols) {
        ab.append(row, pos+1, E.screencols);
        if (y == E.screenlines - 1) {E.last_visible_row = filerow - 1; return ab;}
        ab.append("\n");
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+E.screencols);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + E.screencols;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = E.screencols - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      if (y == E.screenlines - 1) {E.last_visible_row = filerow - 1; return ab;}
      ab.append("\n");
      y++;
    }
  }
}

void editorDrawRows(std::string &ab) {

  /* for visual modes */
  int begin = 0;
  std::string abs = "";
 
  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf << "\x1b[" << TOP_MARGIN + 1 << ";" <<  EDITOR_LEFT_MARGIN + 1 << "H";
  ab.append(buf.str());

  // erase the screen
  for (int i=0; i < E.screenlines; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret, nchars);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 1 << ";" <<  EDITOR_LEFT_MARGIN + 1 << "H";
  ab.append(buf2.str()); //reposition cursor

  if (E.rows.empty()) return;

  int highlight[2] = {0,0};
  bool visual = false;
  bool visual_block = false;
  bool visual_line = false;
  if (E.mode == VISUAL || E.mode == VISUAL_LINE) {
    if (E.highlight[1] < E.highlight[0]) { //note E.highlight[1] should == E.fc
      highlight[1] = E.highlight[0];
      highlight[0] = E.highlight[1];
    } else {
      highlight[0] = E.highlight[0];
      highlight[1] = E.highlight[1];
    }
  }

  int y = 0;
  int filerow = E.first_visible_row;
  bool flag = false;

  for (;;){
    if (flag) break;
    if (filerow == E.rows.size()) {E.last_visible_row = filerow - 1; break;}
    std::string row = E.rows.at(filerow);

    if (E.mode == VISUAL_LINE && filerow == highlight[0]) {
      ab.append("\x1b[48;5;242m", 11);
      visual_line = true;
    }

    if (E.mode == VISUAL && filerow == E.fr) {
      /* zz counting the number of additional chars generated by '\n' at end of line*/
      int zz = highlight[0]/E.screencols;
      begin = ab.size() + highlight[0] + zz;
      visual = true;
    }

    /**************************************/
    if (E.mode == VISUAL_BLOCK && (filerow > (E.vb0[1] - 1) && filerow < (E.fr + 1))) { //highlight[3] top row highlight[4] lower (higher #)
      int zz = E.vb0[0]/E.screencols;
      begin = ab.size() + E.vb0[0] + zz;
      visual_block = true;
    } else visual_block = false;
    /**************************************/

    if (row.empty()) {
      if (y == E.screenlines - 1) break;
      //ab.append(lf_ret, nchars);
      ab.append(1, '\f');
      filerow++;
      y++;

      // pretty ugly that this has to appear here but need to take into account empty rows  
      if (visual_line) {
        if (filerow == highlight[1] + 1){ //could catch VISUAL_LINE starting on last row outside of for
          ab.append("\x1b[0m", 4); //return background to normal
          visual_line = false;
        }
      }
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      /* this is needed because it deals where the end of the line doesn't have a space*/
      if (row.substr(pos+1).size() <= E.screencols) {
        ab.append(row, pos+1, E.screencols);
        abs.append(row, pos+1, E.screencols);
        if (y == E.screenlines - 1) {flag=true; break;}
        ab.append(1, '\f');
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+E.screencols);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + E.screencols;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = E.screencols - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      abs.append(row, prev_pos+1, pos-prev_pos);
      if (y == E.screenlines - 1) {flag=true; break;}
      ab.append(1, '\f');
      y++;
    }

    /* The VISUAL mode code that actually does the writing */
    if (visual) { 
      std::string visual_snippet = ab.substr(begin, highlight[1]-highlight[0]); 
      int pos = -1;
      int n = 0;
      for (;;) {
        pos += 1;
        pos = visual_snippet.find('\f', pos);
        if (pos == std::string::npos) break;
        n += 1;
      }
      ab.insert(begin + highlight[1] - highlight[0] + n + 1, "\x1b[0m");
      ab.insert(begin, "\x1b[48;5;242m");
      visual = false;
    }

    if (visual_block) { 
      std::string visual_snippet = ab.substr(begin, E.fc-E.vb0[0]); //E.fc = highlight[1] ; vb0[0] = highlight[0] 
      int pos = -1;
      int n = 0;
      for (;;) {
        pos += 1;
        pos = visual_snippet.find('\f', pos);
        if (pos == std::string::npos) break;
        n += 1;
      }
      ab.insert(begin + E.fc - E.vb0[0] + n + 1, "\x1b[0m");
      ab.insert(begin, "\x1b[48;5;242m");
    }

    if (visual_line) {
      if (filerow == highlight[1] + 1){ //could catch VISUAL_LINE starting on last row outside of for
        ab.append("\x1b[0m", 4); //return background to normal
        visual_line = false;
      }
    }

  }
  E.last_visible_row = filerow - 1; // note that this is not exactly true - could be the whole last row is visible
  ab.append("\x1b[0m", 4); //return background to normal - would catch VISUAL_LINE starting and ending on last row

  size_t p = 0;
  for (;;) {
      if (p > ab.size()) break;
      p = ab.find('\f', p);
      if (p == std::string::npos) break;
      ab.replace(p, 1, lf_ret);
      p += 7;
  }
}

void editorHighlightWord(int r, int c, int len) {
  std::string &row = E.rows.at(r);
  int x = editorGetScreenXFromRowColWW(r, c) + EDITOR_LEFT_MARGIN + 1;
  int y = editorGetScreenYFromRowColWW(r, c) + TOP_MARGIN + 1 - E.line_offset; // added line offset 12-25-2019
  std::stringstream s;
  s << "\x1b[" << y << ";" << x << "H" << "\x1b[48;5;31m"
    << row.substr(c, len)
    << "\x1b[0m";

  write(STDOUT_FILENO, s.str().c_str(), s.str().size());
}

void editorSpellCheck(void) {

  if (E.rows.empty()) return;

  pos_mispelled_words.clear();

  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  //auto sugs = std::vector<std::string>();
  auto dict = nuspell::Dictionary::load_from_path(path);

  std::string delimiters = " -<>!$,.;?:()[]{}&#~^";

  for (int n=E.first_visible_row; n<=E.last_visible_row; n++) {
    int end = -1;
    int start;
    std::string &row = E.rows.at(n);
    if (row.empty()) continue;
    for (;;) {
      if (end >= static_cast<int>(row.size()) - 1) break;
      start = end + 1;
      end = row.find_first_of(delimiters, start);
      if (end == std::string::npos)
        end = row.size();

      if (!dict.spell(row.substr(start, end-start))) {
        pos_mispelled_words.push_back(std::make_pair(n, start)); 
        editorHighlightWord(n, start, end-start);
      }
    }
  }

  //reposition the cursor back to where it belongs
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + TOP_MARGIN + 1, E.cx + EDITOR_LEFT_MARGIN + 1); //03022019
  write(STDOUT_FILENO, buf, strlen(buf));
}

// uses sqlite offsets contained in word_positions
// currently just highlights the words in rows that are visible on the screen
// You can't currently scroll because when you edit the search highlights disappear
// ie text can't be scrolled unless you enter editor_mode which doesn't highlight
// while this works ? better to use find_if as per
// https://stackoverflow.com/questions/9333333/c-split-string-with-space-and-punctuation-chars
void editorHighlightWordsByPosition(void) {

  if (E.rows.empty()) return;

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\\"; //removed period?? since it is in list?
  for (auto v: word_positions) {
    int word_num = -1;
    auto pos = v.begin();
    auto prev_pos = pos;
    for (int n=0; n<=E.last_visible_row; n++) {
      int end = -1; //this became a problem in comparing -1 to unsigned int (always larger)
      int start;
      std::string &row = E.rows.at(n);
      if (row.empty()) continue;
      for (;;) {
        if (end >= static_cast<int>(row.size()) - 1) break;
        start = end + 1;
        end = row.find_first_of(delimiters, start);
        if (end == std::string::npos) {
          end = row.size();
        }
        if (end != start) word_num++;
  
        if (n < E.first_visible_row) continue;
        // start the search from the last match? 12-23-2019
        pos = std::find(pos, v.end(), word_num);
        if (pos != v.end()) {
          prev_pos = pos;
          editorHighlightWord(n, start, end-start);
        } else pos = prev_pos;
      }
    }
  }
}

void editorSpellingSuggestions(void) {
  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  auto sugs = std::vector<std::string>();
  auto dict = nuspell::Dictionary::load_from_path(path);

  std::string word;
  std::stringstream s;
  word = editorGetWordUnderCursor();
  if (word.empty()) return;

  if (dict.spell(word)) {
      editorSetMessage("%s is spelled correctly", word.c_str());
      return;
  }

  dict.suggest(word, sugs);
  if (sugs.empty()) {
      editorSetMessage("No suggestions");
  } else {
    for (auto &sug : sugs) s << sug << ' ';
    editorSetMessage("Suggestions for %s: %s", word.c_str(), s.str().c_str());
  }
}

//status bar has inverted colors
/*****************************************/
/*
void editorDrawStatusBar(std::string& ab) {
  int len;
  char status[200];
  // position the cursor at the beginning of the editor status bar at correct indent
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenlines + TOP_MARGIN + 1,
                                            //EDITOR_LEFT_MARGIN);//+1
                                            EDITOR_LEFT_MARGIN - OUTLINE_RIGHT_MARGIN);
  ab.append(buf, strlen(buf));
  ab.append("\x1b[K", 3); //cursor in middle of screen and doesn't move on erase
  ab.append("\x1b[7m", 4); //switches to inverted colors
  ab.append(" ", 1);

  if (!E.rows.empty()){
    int line = editorGetLineInRowWW(E.fr, E.fc);
    int line_char_count = editorGetLineCharCountWW(E.fr, line);
    int lines = editorGetLinesInRowWW(E.fr);



    len = snprintf(status,
                   sizeof(status), "E.fr(0)=%d lines(1)=%d line(1)=%d E.fc(0)=%d LO=%d initial_row=%d last_row=%d line chrs(1)="
                                   "%d  E.cx(0)=%d E.cy(0)=%d E.scols(1)=%d",
                                   E.fr, lines, line, E.fc, E.line_offset, E.first_visible_row, E.last_visible_row, line_char_count, E.cx, E.cy, E.screencols);
  } else {
    len =  snprintf(status, sizeof(status), "E.row is NULL E.cx = %d E.cy = %d  E.numrows = %ld E.line_offset = %d",
                                      E.cx, E.cy, E.rows.size(), E.line_offset);
  }

  if (len > E.screencols + OUTLINE_RIGHT_MARGIN) len = E.screencols + OUTLINE_RIGHT_MARGIN;
  ab.append(status, len);

  while (len < E.screencols + OUTLINE_RIGHT_MARGIN) {
      ab.append(" ", 1);
      len++;
    }
  ab.append("\x1b[m", 3); //switches back to normal formatting
}
*/

void editorDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  buf  << "\x1b[" << E.screenlines + TOP_MARGIN + 2 << ";" << EDITOR_LEFT_MARGIN << "H";
  ab += buf.str();
  ab += "\x1b[K"; // will erase midscreen -> R; cursor doesn't move after erase
  int msglen = strlen(E.message);
  if (msglen > E.screencols) msglen = E.screencols;
  ab.append(E.message, msglen);
}

// called by get_note (and others) and in main while loop
// redraw == true means draw the rows
void editorRefreshScreen(bool redraw) {
  char buf[32];
  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); //03022019 added len
  ab.append(buf, strlen(buf));

  if (redraw) {
    //Temporary kluge tid for code folder = 18
    if (get_folder_tid(O.rows.at(O.fr).id) == 18 && !(E.mode == VISUAL || E.mode == VISUAL_LINE || E.mode == VISUAL_BLOCK)) editorDrawCodeRows(ab);
    //if (E.highlight_syntax == true) editorDrawCodeRows(ab);
    else editorDrawRows(ab);
  }
  editorDrawMessageBar(ab); //01012020

  // the lines below position the cursor where it should go
  if (E.mode != COMMAND_LINE){
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + TOP_MARGIN + 1, E.cx + EDITOR_LEFT_MARGIN + 1); //03022019
    ab.append(buf, strlen(buf));
  }

  ab.append("\x1b[?25h", 6); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());

  // can't do this until ab is written or will just overwite highlights
  if (redraw) {
    if (E.spellcheck) editorSpellCheck();
    else if (O.mode == SEARCH) editorHighlightWordsByPosition();
  }
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void editorSetMessage(const char *fmt, ...) {
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  vsnprintf(E.message, sizeof(E.message), fmt, ap);
  va_end(ap); //free a va_list
}

void editorMoveCursor(int key) {

  if (E.rows.empty()) return; //could also be !E.numrows

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (E.fc > 0) E.fc--;
      break;

    case ARROW_RIGHT:
    case 'l':
      E.fc++;
      break;

    case ARROW_UP:
    case 'k':
      if (E.fr > 0) E.fr--;
      break;

    case ARROW_DOWN:
    case 'j':
      if (E.fr < E.rows.size() - 1) E.fr++;
      break;
  }
}

void editorPageUpDown(int key) {

  if (key == PAGE_UP) {
    if(E.fr==0) return;
    int lines = 0;
    int r = E.fr - 1;
    for(;;) {
        lines += editorGetLinesInRowWW(r);
        if (r == 0) {
            break;
        }
        if (lines > E.screenlines) {
            r++;
            break;
        }
        r--;
    }
    E.fr = r;
  } else {
    int lines = 0;
    int r = E.fr;
    for(;;) {
        lines += editorGetLinesInRowWW(r);
        if (r == E.rows.size() - 1) {
            break;
        }
        if (lines > E.screenlines) {
            r--;
            break;
        }
        r++;
    }
    E.fr = r;
  }
  return;
}
// calls readKey()
bool editorProcessKeypress(void) {
  //int start, end;
  int i, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  switch (int c = readKey(); E.mode) {

    case NO_ROWS:

      switch(c) {
        case ':':
          E.mode = COMMAND_LINE;
          E.command_line.clear();
          E.command[0] = '\0';
          editorSetMessage(":");
          return false;

        case '\x1b':
          E.command[0] = '\0';
          E.repeat = 0;
          return false;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
        case 'O':
        case 'o':
          editorInsertRow(0, std::string());
          E.mode = INSERT;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return true;
      }

      return false;

    case INSERT:

      switch (c) {

        case '\r':
          editorInsertReturn();
          E.last_typed += c;
          return true;

        // not sure this is in use
        case CTRL_KEY('s'):
          editorSaveNoteToFile("lm_temp");
          return false;

        case HOME_KEY:
          editorMoveCursorBOL();
          return false;

        case END_KEY:
          editorMoveCursorEOL();
          editorMoveCursor(ARROW_RIGHT);
          return false;

        case BACKSPACE:
          editorCreateSnapshot();
          editorBackspace();
          return true;
    
        case DEL_KEY:
          editorCreateSnapshot();
          editorDelChar();
          return true;
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          editorMoveCursor(c);
          return false;
    
        case CTRL_KEY('b'):
        //case CTRL_KEY('i'): CTRL_KEY('i') -> 9 same as tab
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);
          return true;
    
        // this should be a command line command
        case CTRL_KEY('z'):
          E.smartindent = (E.smartindent) ? 0 : SMARTINDENT;
          editorSetMessage("E.smartindent = %d", E.smartindent); 
          return false;
    
        case '\x1b':

          /*
           * below deals with certain NORMAL mode commands that
           * cause entry to INSERT mode - deals with repeats
           */

          /****************************************************/
          if(cmd_set1.count(E.last_command)) { //nuspell needed gcc+17 so no contains
            for (int n=0; n<E.last_repeat-1; n++) {
              for (char const &c : E.last_typed) {editorInsertChar(c);}
            }
          }

          if (cmd_map2.count(E.last_command)) cmd_map2[E.last_command](E.last_repeat - 1);
          //if (E.last_command == 'o') f_o(E.last_repeat-1);
          //else if (E.last_command == 'O') f_O(E.last_repeat-1);
          /****************************************************/

          //'I' in VISUAL BLOCK mode
          if (E.last_command == -1) {
            for (int n=0; n<E.last_repeat-1; n++) {
              for (char const &c : E.last_typed) {editorInsertChar(c);}
            }
            {
            int temp = E.fr;

            for (E.fr=E.fr+1; E.fr<E.vb0[1]+1; E.fr++) {
              for (int n=0; n<E.last_repeat; n++) { //NOTICE not E.last_repeat - 1
                E.fc = E.vb0[0]; 
                for (char const &c : E.last_typed) {editorInsertChar(c);}
              }
            }
            E.fr = temp;
            E.fc = E.vb0[0];
          }
          }

          //'A' in VISUAL BLOCK mode
          if (E.last_command == -2) {
            //E.fc++;a doesn't go here
            for (int n=0; n<E.last_repeat-1; n++) {
              for (char const &c : E.last_typed) {editorInsertChar(c);}
            }
            {
            int temp = E.fr;

            for (E.fr=E.fr+1; E.fr<E.vb0[1]+1; E.fr++) {
              for (int n=0; n<E.last_repeat; n++) { //NOTICE not E.last_repeat - 1
                int size = E.rows.at(E.fr).size();
                if (E.vb0[2] > size) E.rows.at(E.fr).insert(size, E.vb0[2]-size, ' ');
                E.fc = E.vb0[2];
                for (char const &c : E.last_typed) {editorInsertChar(c);}
              }
            }
            E.fr = temp;
            E.fc = E.vb0[0];
          }
          }

          E.mode = NORMAL;
          E.repeat = 0;
          if (E.fc > 0) E.fc--;

          // below - if the indent amount == size of line then it's all blanks
          // can hit escape with E.row == NULL or E.row[E.fr].size == 0
          if (!E.rows.empty() && E.rows[E.fr].size()) {
            int n = editorIndentAmount(E.fr);
            if (n == E.rows[E.fr].size()) {
              E.fc = 0;
              for (int i = 0; i < n; i++) {
                editorDelChar();
              }
            }
          }
          editorSetMessage(""); 
          //editorSetMessage(E.last_typed.c_str());
          return true;
    
        // deal with tab in insert mode - was causing segfault  
        case '\t':
          for (int i=0; i<4; i++) editorInsertChar(' ');
          return true;  

        default:
          editorInsertChar(c);
          E.last_typed += c;
          //editorSetMessage(E.last_typed.c_str());
          return true;
     
      } //end inner switch for outer case INSERT

      return true; // end of case INSERT: - should not be executed

    case NORMAL: 
 
      // could be fixed but as code is now all escapes if E.command already
      // had characters would not fall through
      if (c == '\x1b') {
        E.command[0] = '\0';
        E.repeat = 0;
        return false;
      }

      /*leading digit is a multiplier*/

      if ((c > 47 && c < 58) && (strlen(E.command) == 0)) {

        if (E.repeat == 0 && c == 48) {

        } else if (E.repeat == 0) {
          E.repeat = c - 48;
          // return false because command not complete
          return false;
        } else {
          E.repeat = E.repeat*10 + c - 48;
          // return false because command not complete
          return false;
        }
      }
      if ( E.repeat == 0 ) E.repeat = 1;
      {
      int n = strlen(E.command);
      E.command[n] = c;
      E.command[n+1] = '\0';
      command = (n && c < 128) ? keyfromstringcpp(E.command) : c;
      }

      E.move_only = false; /////////this needs to get into master

      switch (command) {

        case 'i': case 'I': case 'a': case 'A': 
          editorCreateSnapshot();
          cmd_map1[command](E.repeat);
          E.mode = INSERT;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          break;

        case 'o': case 'O':
          editorCreateSnapshot();
          E.last_typed.clear(); //this is necessary
          cmd_map2[command](1); //note this is ! not e.repeat
          E.mode = INSERT;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          break;

        case 'x': case C_dw: case C_daw: case C_dd: case C_de: case C_dG: case C_d$:
          editorCreateSnapshot();
          cmd_map3[command](E.repeat);
          break;

        case C_cw: case C_caw: case 's':
          editorCreateSnapshot();
          cmd_map4[command](E.repeat);
          E.mode = INSERT; 
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          break;

        case '~':
          editorCreateSnapshot();
          f_change_case(E.repeat);
          break;

        case 'J':
          //1J and 2J are same in vim
            editorCreateSnapshot();
            E.fc = E.rows.at(E.fr).size();
            E.repeat = (E.repeat == 1) ? 2 : E.repeat;
            for (i=1; i<E.repeat; i++) { 
              if (E.fr == E.rows.size()- 1) break;
              E.rows.at(E.fr) += " " + E.rows.at(E.fr+1);
              editorDelRow(E.fr+1); 
            }  
            E.command[0] = '\0';
            E.repeat = 0;
          //}  
          return true;

        case 'w': case 'e': case 'b': case '0': case '$':
          cmd_map5[command](E.repeat);
          E.move_only = true;// still need to draw status line, message and cursor
          break;

        /*  
        case SHIFT_TAB:
          editor_mode = false;
          E.fc = E.fr = E.cy = E.cx = E.line_offset = 0;
          return false;
        */

        case 'r':
          // editing cmd: can be dotted and does repeat
          //f_r exists for dot
          editorCreateSnapshot();
          E.mode = REPLACE;
          return false; //? true or handled in REPLACE mode
    
        case C_next_mispelling:
          {
          if (!E.spellcheck || pos_mispelled_words.empty()) {
            editorSetMessage("Spellcheck is off or no words mispelled");
            E.command[0] = '\0';
            E.repeat = 0;
            return false;
          }
          auto &z = pos_mispelled_words;
          auto it = find_if(z.begin(), z.end(), [](const std::pair<int, int> &p) {return (p.first == E.fr && p.second > E.fc);});
          if (it == z.end()) {
            it = find_if(z.begin(), z.end(), [](const std::pair<int, int> &p) {return (p.first > E.fr);});
            if (it == z.end()) {E.fr = z[0].first; E.fc = z[0].second;
            } else {E.fr = it->first; E.fc = it->second;} 
          } else {E.fc = it->second;}
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("E.fr = %d, E.fc = %d", E.fr, E.fc);
          return true;
          }

        case C_previous_mispelling:
          {
          if (!E.spellcheck || pos_mispelled_words.empty()) {
            editorSetMessage("Spellcheck is off or no words mispelled");
          E.command[0] = '\0';
          E.repeat = 0;
            return false;
          }
          auto &z = pos_mispelled_words;
          auto it = find_if(z.rbegin(), z.rend(), [](const std::pair<int, int> &p) {return (p.first == E.fr && p.second < E.fc);});
          if (it == z.rend()) {
            it = find_if(z.rbegin(), z.rend(), [](const std::pair<int, int> &p) {return (p.first < E.fr);});
            if (it == z.rend()) {E.fr = z.back().first; E.fc = z.back().second;
            } else {E.fr = it->first; E.fc = it->second;} 
          } else {E.fc = it->second;}
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("E.fr = %d, E.fc = %d", E.fr, E.fc);
          return true;
          }

        case ':':
          editorSetMessage(":");
          E.command[0] = '\0';
          E.command_line.clear();
          E.mode = COMMAND_LINE;
          return false;
    
        case 'V':
          E.mode = VISUAL_LINE;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fr;
          editorSetMessage("\x1b[1m-- VISUAL LINE --\x1b[0m");
          return true;
    
        case 'v':
          E.mode = VISUAL;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fc;
          editorSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
          return true; //want a redraw to highlight current char like vim does

        case CTRL_KEY('v'):
          E.mode = VISUAL_BLOCK;
          E.command[0] = '\0';
          E.repeat = 0;
          E.vb0[0] = E.fc;
          E.vb0[1] = E.fr;
          editorSetMessage("\x1b[1m-- VISUAL BLOCK --\x1b[0m");
          return true; //want a redraw to highlight current char like vim does

        case 'p':  
          editorCreateSnapshot();
          if (!string_buffer.empty()) editorPasteString();
          else editorPasteLine();
          E.command[0] = '\0';
          E.repeat = 0;
          return true;
    
        case '*':  
          // does not clear dot
          editorGetWordUnderCursor();
          editorFindNextWord();
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0; // prob not necessary but doesn't hurt
          E.move_only = true;
          return false;
    
        case 'n':
          // n does not clear dot
          editorFindNextWord();
          // in vim the repeat does work with n - it skips that many words
          // not a dot command so should leave E.last_repeat alone
          E.command[0] = '\0';
          E.repeat = 0; // prob not necessary but doesn't hurt
          E.move_only = true;
          return false;
    
        case 'u':
          editorRestoreSnapshot();
          E.command[0] = '\0';
          return true;
    
        case '^':
          generate_persistent_html_file(O.rows.at(O.fr).id);
          outlineRefreshScreen(); //to get outline message updated (could just update that last row??)
          O.command[0] = '\0';
          return false;

        case C_suggestions:
          editorSpellingSuggestions();
          E.command[0] = '\0';
          E.move_only = true;
          return false;

        case '.':
          editorDoRepeat();
          E.command[0] = '\0';
          return true;

        case CTRL_KEY('z'):
          E.smartindent = (E.smartindent) ? 0 : SMARTINDENT;
          editorSetMessage("E.smartindent = %d", E.smartindent);
          return false;

        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);

          break;

        case CTRL_KEY('s'):
          editorSaveNoteToFile("lm_temp");
          return false;

        /*  
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          return true;
        */

        case C_indent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) { 
            editorIndentRow();
            E.fr++;
            if (E.fr == E.rows.size() - 1) break;
          }
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return true;
    
        case C_unindent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) {
            editorUnIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return true;
    
       case C_yy:  
         editorYankLine(E.repeat);
         E.command[0] = '\0';
         E.repeat = 0;
         return false;

        case PAGE_UP: case PAGE_DOWN:
          editorPageUpDown(c);
          E.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          E.repeat = 0;
          return false; //there is a check if editorscroll == true

        case ARROW_UP: case ARROW_DOWN: case ARROW_LEFT: case ARROW_RIGHT:
        case 'h': case 'j': case 'k': case 'l':
          editorMoveCursor(c);
          E.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          E.move_only = true;
          E.repeat = 0;
          return false; //there is a check if editorscroll == true

        case C_gg:
          // navigation: does not clear dot
          E.fc = E.line_offset = 0;
          E.fr = E.repeat - 1;
          E.move_only = true;
          break; //see code after switch

        case 'G':
          // navigation: can't be dotted and doesn't repeat
          // navigation doesn't clear last dotted command
          E.fc = 0;
          E.fr = E.rows.size() - 1;
          E.move_only = true;
         break; //see code after switch

       default:
          /* return false when no need to redraw rows like when command is not matched yet*/
          return false;
    
      } // end of keyfromstring switch under case NORMAL 

      if(E.move_only) {
        E.command[0] = '\0';
        E.repeat = 0;
        editorSetMessage("command = %d", command);
        return false;
      } else {
        E.last_repeat = E.repeat;
        E.last_typed.clear();
        E.last_command = command;
        E.command[0] = '\0';
        E.repeat = 0;
        editorSetMessage("command = %d", command);
        return true;
      }    

      // end of case NORMAL - there are breaks that can get to code above

    case COMMAND_LINE:

      switch (c) {

        case '\x1b':

          E.mode = NORMAL;
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0;
          editorSetMessage(""); 
          return false;
  
        case '\r':

          std::size_t pos;

          // passes back position of space (if there is one) in var pos
          command = commandfromstringcpp(E.command_line, pos); //assume pos paramter is now a reference but should check

          switch (command) {

            case C_write:
            case 'w':
              update_note();
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
              if (lm_browser) {
                if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
                else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
              }   
              {
              auto it = html_files.find(O.rows.at(O.fr).id);
              if (it != html_files.end()) update_html_file("assets/" + it->second);
              }
              editorSetMessage("");
              return false;
  
            case C_clear:
              html_files.clear();
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              editorSetMessage("");
              return false;

            case 'x':
              update_note();
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              editor_mode = false;
              //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
              if (lm_browser) {
                if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
                else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
              }   
              {
              auto it = html_files.find(O.rows.at(O.fr).id);
              if (it != html_files.end()) update_html_file("assets/" + it->second);
              }
              editorSetMessage("");
              return false;
  
            case C_quit:
            case 'q':
              if (E.dirty) {
                  E.mode = NORMAL;
                  E.command[0] = '\0';
                  E.command_line.clear();
                  editorSetMessage("No write since last change");
              } else {
                editorSetMessage("");
                E.fr = E.fc = E.cy = E.cx = E.line_offset = 0; //added 11-26-2019 but may not be necessary having restored this in get_note.
                editor_mode = false;
              }
              editorRefreshScreen(false); // don't need to redraw rows
              return false;

            case C_quit0:
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              editor_mode = false;
              return false;

            case C_spellcheck:
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              E.spellcheck = !E.spellcheck;
              if (E.spellcheck) editorSpellCheck();
              else editorRefreshScreen(true);
              editorSetMessage("Spellcheck %s", (E.spellcheck) ? "on" : "off");
              return false;

            case C_next_mispelling:
              {
              if (!E.spellcheck || pos_mispelled_words.empty()) {
                editorSetMessage("Spellcheck is off or no words mispelled");
                return false;
              }
              auto &z = pos_mispelled_words;
              auto it = find_if(z.begin(), z.end(), [](const std::pair<int, int> &p) {return (p.first == E.fr && p.second > E.fc);});
              if (it == z.end()) {
                it = find_if(z.begin(), z.end(), [](const std::pair<int, int> &p) {return (p.first > E.fr);});
                if (it == z.end()) {E.fr = z[0].first; E.fc = z[0].second;}
              } else {E.fr = it->first; E.fc = it->second;}
              editorSetMessage("E.fr = %d, E.fc = %d", E.fr, E.fc);
              return true;
              }

            case C_refresh:
              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              return true;

            case C_syntax:
              if (pos) {
                std::string action = E.command_line.substr(pos + 1);
                if (action == "on") {
                  E.highlight_syntax = true;
                  editorSetMessage("Syntax highlighting will be turned on");
                } else if (action == "off") {
                  E.highlight_syntax = false;
                  editorSetMessage("Syntax highlighting will be turned off");
                } else {editorSetMessage("The syntax is 'sh on' or 'sh off'"); }
              } else {editorSetMessage("The syntax is 'sh on' or 'sh off'");}

              E.mode = NORMAL;
              E.command[0] = '\0';
              E.command_line.clear();
              return true;

            case C_vim:
              open_in_vim();
              E.command[0] = '\0';
              E.command_line.clear();
              E.mode = NORMAL;
              return true;

            case 'b':
            case C_browser:
              generate_persistent_html_file(O.rows.at(O.fr).id);
              E.command[0] = '\0';
              E.command_line.clear();
              E.mode = NORMAL;
              return false;

            default: // default for switch (command)
              editorSetMessage("\x1b[41mNot an editor command: %s\x1b[0m", E.command_line.c_str());
              E.mode = NORMAL;
              return false;

          } // end of case '\r' switch (command)
     
          return false;
  
        default: //default for switch 'c' in case COMMAND_LINE
          if (c == DEL_KEY || c == BACKSPACE) {
            if (!E.command_line.empty()) E.command_line.pop_back();
          } else {
            E.command_line.push_back(c);
          }
          editorSetMessage(":%s", E.command_line.c_str());

      } // end of COMMAND_LINE switch (c)
  
      return false; //end of case COMMAND_LINE

    case VISUAL_LINE:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.highlight[1] = E.fr;
          return true;
    
        case 'x':
          if (!E.rows.empty()) {
            editorCreateSnapshot();
            E.repeat = E.highlight[1] - E.highlight[0] + 1;
            E.fr = E.highlight[0]; 
            editorYankLine(E.repeat);
    
            for (int i = 0; i < E.repeat; i++) editorDelRow(E.highlight[0]);
          }

          E.fc = 0;
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0;
          E.mode = NORMAL;
          editorSetMessage("");
          return true;
    
        case 'y':  
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          editorYankLine(E.repeat);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        case '>':
          editorCreateSnapshot();
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          for ( i = 0; i < E.repeat; i++ ) {
            editorIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        // changed to E.fr on 11-26-2019
        case '<':
          editorCreateSnapshot();
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          for ( i = 0; i < E.repeat; i++ ) {
            editorUnIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL_BLOCK:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          //E.highlight[1] = E.fr;
          return true;
    
        case '$':
          editorMoveCursorEOL();
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0;
          editorSetMessage("");
          return true;

        case 'x':
          if (!E.rows.empty()) {
            editorCreateSnapshot();
    
          for (int i = E.vb0[1]; i < E.fr + 1; i++) {
            E.rows.at(i).erase(E.vb0[0], E.fc - E.vb0[0] + 1); //needs to be cleaned up for E.fc < E.vb0[0] ? abs
          }

          E.fc = E.vb0[0];
          E.fr = E.vb0[1];
          }
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0;
          E.mode = NORMAL;
          editorSetMessage("");
          return true;
    
        case 'I':
          if (!E.rows.empty()) {
            editorCreateSnapshot();
      
          //E.repeat = E.fr - E.vb0[1];  
            {
          int temp = E.fr; //E.fr is wherever cursor Y is    
          //E.vb0[2] = E.fr;
          E.fc = E.vb0[0]; //vb0[0] is where cursor X was when ctrl-v happened
          E.fr = E.vb0[1]; //vb0[1] is where cursor Y was when ctrl-v happened
          E.vb0[1] = temp; // resets E.vb0 to last cursor Y position - this could just be E.vb0[2]
          //cmd_map1[c](E.repeat);
          command = -1;
          E.repeat = 1;
          E.mode = INSERT;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          E.last_repeat = E.repeat;
          E.last_typed.clear();
          E.last_command = command;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("command = %d", command);
          return true;

        case 'A':
          if (!E.rows.empty()) {
            editorCreateSnapshot();
      
          //E.repeat = E.fr - E.vb0[1];  
            {
          int temp = E.fr;    
          E.fr = E.vb0[1];
          E.vb0[1] = temp;
          E.fc++;
          E.vb0[2] = E.fc;
          //int last_row_size = E.rows.at(E.vb0[1]).size();
          int first_row_size = E.rows.at(E.fr).size();
          if (E.vb0[2] > first_row_size) E.rows.at(E.fr).insert(first_row_size, E.vb0[2]-first_row_size, ' ');
          //cmd_map1[c](E.repeat);
          command = -2;
          E.repeat = 1;
          E.mode = INSERT;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          E.last_repeat = E.repeat;
          E.last_typed.clear();
          E.last_command = command;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("command = %d", command);
          return true;

        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.highlight[1] = E.fc;
          return true;
    
        case 'x':
          if (!E.rows.empty()) {
            editorCreateSnapshot();
            editorYankString(); 
            editorDeleteVisual();
          }
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        case 'y':
          E.fc = E.highlight[0];
          editorYankString();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        case 'p':
          editorCreateSnapshot();
          if (!string_buffer.empty()) editorPasteStringVisual();
          else editorPasteLineVisual();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = NORMAL;
          return true;

        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateVisual(c);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return true;
    
        case '\x1b':
          E.mode = NORMAL;
          E.command[0] = '\0';
          E.repeat = E.last_repeat = 0;
          editorSetMessage("");
          return true;
    
        default:
          return false;
      }
    
      return false;

    case REPLACE:

      if (c == '\x1b') {
        E.command[0] = '\0';
        E.repeat = E.last_repeat = 0;
        E.last_command = 0;
        E.last_typed.clear();
        E.mode = NORMAL;
        return true;
      }

      editorCreateSnapshot();
      for (int i = 0; i < E.repeat; i++) {
        editorDelChar();
        editorInsertChar(c);
        E.last_typed.clear();
        E.last_typed += c;
      }
      E.last_command = 'r';
      E.last_repeat = E.repeat;
      E.repeat = 0;
      E.command[0] = '\0';
      E.mode = NORMAL;
      return true;

  }  //end of outer switch(E.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
  return true; // this should not be reachable but was getting an error
} //end of editorProcessKeyPress

/********************************************************** WW stuff *****************************************/
// used by editorDrawRows to figure out the first row to draw
// there should be only one of these (see below)
int editorGetInitialRow(int &line_offset) {

  if (line_offset == 0) return 0;

  int r = 0;
  int lines = 0;

  for (;;) {
    lines += editorGetLinesInRowWW(r);
    r++;

    // there is no need to adjust line_offset
    // if it happens that we start
    // on the first line of row r
    if (lines == line_offset) break;

    // need to adjust line_offset
    // so we can start on the first
    // line of row r
    if (lines > line_offset) {
      line_offset = lines;
      break;
    }
  }
  return r;
}

// used by editorDrawRows to figure out the first row to draw
int editorGetInitialRow(int &line_offset, int direction) {

  if (line_offset == 0) return 0;

  int r = 0;
  int lines = 0;

  for (;;) {
    lines += editorGetLinesInRowWW(r);
    r++;

    // there is no need to adjust line_offset
    // if it happens that we start
    // on the first line of row r
    if (lines == line_offset) {
        line_offset = lines;
        return r;
    }

    // need to adjust line_offset
    // so we can start on the first
    // line of row r
      if (lines > line_offset) {
        line_offset = lines;
        break;
    }
  }
  lines = 0;
  int n = 0;
  for (n=0; n<r-1; n++) {
      lines += editorGetLinesInRowWW(n);
  }
  line_offset = lines;
  return n;
}

//used by editorGetInitialRow
//used by editorGetScreenYFromRowColWW
int editorGetLinesInRowWW(int r) {
  std::string_view row(E.rows.at(r));

  if (row.size() <= E.screencols) return 1; //seems obvious but not added until 03022019

  int lines = 0;
  int pos = -1; //pos is the position of the last character in the line (zero-based)
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= E.screencols) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+E.screencols);

    //note npos when signed = -1
    //order can be reversed of if, else if and can drop prev_pos != -1: see editorDrawRows
    if (pos == std::string::npos) {
      pos = prev_pos + E.screencols;
    } else if (pos == prev_pos) {
      row = row.substr(pos+1);
      //prev_pos = -1; 12-27-2019
      pos = E.screencols - 1;
    }
    lines++;
  }
  return lines;
}

// used in editorGetScreenYFromRowColWW
/**************can't take substring of row because absolute position matters**************************/
int editorGetLineInRowWW(int r, int c) {
  // can't use reference to row because replacing blanks to handle corner case
  std::string row = E.rows.at(r);

  if (row.size() <= E.screencols ) return 1; //seems obvious but not added until 03022019

  /* pos is the position of the last char in the line
   * and pos+1 is the position of first character of the next row
   */

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= E.screencols) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+E.screencols);

    if (pos == std::string::npos) {
        pos = prev_pos + E.screencols;

   // only replace if you have enough characters without a space to trigger this
   // need to start at the beginning each time you hit this
   // unless you want to save the position which doesn't seem worth it
    } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + E.screencols;
    }

    lines++;
    if (pos >= c) break;
  }
  return lines;
}

//used in status bar because interesting but not essential
int editorGetLineCharCountWW(int r, int line) {
  //This should be a string view and use substring like lines in row
  std::string row = E.rows.at(r);
  if (row.empty()) return 0;

  if (row.size() <= E.screencols) return row.size();

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

  // we know the first time around this can't be true
  // could add if (line > 1 && row.substr(pos+1).size() ...);
  if (row.substr(pos+1).size() <= E.screencols) {
    return row.substr(pos+1).size();
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+E.screencols);

  if (pos == std::string::npos) {
    pos = prev_pos + E.screencols;

  // only replace if you have enough characters without a space to trigger this
  // need to start at the beginning each time you hit this
  // unless you want to save the position which doesn't seem worth it
  } else if (pos == prev_pos) {
    replace(row.begin(), row.begin()+pos+1, ' ', '+');
    pos = prev_pos + E.screencols;
  }

  lines++;
  if (lines == line) break;
  }
  return pos - prev_pos;
}


/**************can't take substring of row because absolute position matters**************************/
//called by editorScroll to get E.cx
int editorGetScreenXFromRowColWW(int r, int c) {
  // can't use reference to row because replacing blanks to handle corner case
  std::string row = E.rows.at(r);

  /* pos is the position of the last char in the line
   * and pos+1 is the position of first character of the next row
   */

  if (row.size() <= E.screencols ) return c; //seems obvious but not added until 03022019

  int pos = -1;
  int prev_pos;
  for (;;) {

  if (row.substr(pos+1).size() <= E.screencols) {
    prev_pos = pos;
    break;
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+E.screencols);

  if (pos == std::string::npos) {
      pos = prev_pos + E.screencols;
  } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + E.screencols;
  }
    /*
    else
      replace(row.begin()+prev_pos+1, row.begin()+pos+1, ' ', '+');
    */

  if (pos >= c) break;
  }
  return c - prev_pos - 1;
}

/* called by editorScroll to get E.cy
E.line_offset is taken into account in editorScroll*/
int editorGetScreenYFromRowColWW(int r, int c) {
  int screenline = 0;

  for (int n = 0; n < r; n++)
    screenline+= editorGetLinesInRowWW(n);

  screenline = screenline + editorGetLineInRowWW(r, c) - 1;
  return screenline;
}
/************************************* end of WW ************************************************/

void editorCreateSnapshot(void) {
  if (E.rows.empty()) return; //don't create snapshot if there is no text
  E.prev_rows = E.rows;
}

void editorRestoreSnapshot(void) {
    if (E.prev_rows.empty()) return;
    E.rows = E.prev_rows;
}

void editorChangeCase(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  char d = row.at(E.fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    editorMoveCursor(ARROW_RIGHT);
    return;
  }
  editorDelChar();
  editorInsertChar(d);
}

void editorYankLine(int n){
  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(E.rows.at(E.fr+i));
  }
  string_buffer.clear();
}

void editorYankString(void) {
  // doesn't cross rows right now
  if (E.rows.empty()) return;

  std::string& row = E.rows.at(E.fr);
  string_buffer.clear();

  int highlight[2] = {0,0};

  if (E.highlight[1] < E.highlight[0]) { //note E.highlight[1] should == E.fc
    highlight[1] = E.highlight[0];
    highlight[0] = E.highlight[1];
  } else {
    highlight[0] = E.highlight[0];
    highlight[1] = E.highlight[1];
  }

  std::string::const_iterator first = row.begin() + highlight[0];
  std::string::const_iterator last = row.begin() + highlight[1] + 1;
  string_buffer = std::string(first, last);
}

void editorPasteString(void) {
  if (E.rows.empty() || string_buffer.empty()) return;
  std::string& row = E.rows.at(E.fr);

  row.insert(row.begin() + E.fc, string_buffer.begin(), string_buffer.end());
  E.fc += string_buffer.size();
  E.dirty++;
}

void editorPasteStringVisual(void) {
  if (E.rows.empty() || string_buffer.empty()) return;
  std::string& row = E.rows.at(E.fr);

  int highlight[2] = {0,0};
  if (E.highlight[1] < E.highlight[0]) { //note E.highlight[1] should == E.fc
    highlight[1] = E.highlight[0];
    highlight[0] = E.highlight[1];
  } else {
    highlight[0] = E.highlight[0];
    highlight[1] = E.highlight[1];
  }

  row.replace(row.begin() + highlight[0], row.begin() + highlight[1] + 1, string_buffer);
  E.fc = highlight[0] + string_buffer.size();
  E.dirty++;
}

void editorPasteLine(void){
  if (E.rows.empty())  editorInsertRow(0, std::string());

  for (size_t i=0; i < line_buffer.size(); i++) {
    //int len = (line_buffer[i].size());
    E.fr++;
    editorInsertRow(E.fr, line_buffer[i]);
  }
}

void editorPasteLineVisual(void){
  if (E.rows.empty())  editorInsertRow(0, std::string());

  /* could just call editorDeleteVisual below*/
  int highlight[2] = {0,0};
  if (E.highlight[1] < E.highlight[0]) { //note E.highlight[1] should == E.fc
    highlight[1] = E.highlight[0];
    highlight[0] = E.highlight[1];
  } else {
    highlight[0] = E.highlight[0];
    highlight[1] = E.highlight[1];
  }

  E.rows.at(E.fr).erase(highlight[0], highlight[1] - highlight[0] + 1);
  E.fc = highlight[0];
  /* could just call editorDeleteVisual above*/

  editorInsertReturn();
  E.fr--;

  for (size_t i=0; i < line_buffer.size(); i++) {
    E.fr++;
    editorInsertRow(E.fr, line_buffer[i]);
  }
}

void editorDeleteVisual(void){
  if (E.rows.empty()) return;

  int highlight[2] = {0,0};
  if (E.highlight[1] < E.highlight[0]) { //note E.highlight[1] should == E.fc
    highlight[1] = E.highlight[0];
    highlight[0] = E.highlight[1];
  } else {
    highlight[0] = E.highlight[0];
    highlight[1] = E.highlight[1];
  }
  E.rows.at(E.fr).erase(highlight[0], highlight[1] - highlight[0] + 1);
  E.fc = highlight[0];
}

void editorUnIndentRow(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.empty()) return;
  E.fc = 0;
  for (int i = 0; i < E.indent; i++) {
    if (row.empty()) break;
    if (row[0] == ' ') {
      editorDelChar();
    }
  }
  E.dirty++;
}

int editorIndentAmount(int r) {
  if (E.rows.empty()) return 0;
  int i;
  std::string& row = E.rows.at(r);

  for (i=0; i<row.size(); i++) {
    if (row[i] != ' ') break;}

  return i;
}

// called by caw and daw
// doesn't handle punctuation correctly
// but vim pretty wierd for punctuation
void editorDelWord(void) {

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row[E.fc] < 48) return;

//below is finding beginning of word
  auto beg = row.find_last_of(' ', E.fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

//below is finding end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

// below is deleting between beginning and end
  row.erase(beg, end-beg+1);
  E.fc = beg;

  E.dirty++;
  editorSetMessage("beg = %d, end = %d", beg, end);
}


void editorDeleteToEndOfLine(void) {
  std::string& row = E.rows.at(E.fr);
  row.resize(E.fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  E.dirty++;
}

void editorMoveCursorBOL(void) {
  if (E.rows.empty()) return;
  E.fc = 0;
}

void editorMoveCursorEOL(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.size()) E.fc = row.size() - 1;
}

// normal mode 'e'
void editorMoveEndWord(void) {

if (E.rows.empty()) return;

if (E.rows.at(E.fr).empty() || E.fc == E.rows.at(E.fr).size() - 1) {
  if (E.fr+1 > E.rows.size() -1) {return;}
  else {E.fr++; E.fc = 0;}
} else E.fc++;

  int r = E.fr;
  int c = E.fc;
  int pos;
  std::string delimiters = " <>,.;?:()[]{}&#~'";
  std::string delimiters_without_space = "<>,.;?:()[]{}&#~'";

  for(;;) {

    if (r > E.rows.size() - 1) {return;}

    std::string &row = E.rows.at(r);

    if (row.empty()) {r++; c = 0; continue;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric
      if (c == row.size()-1 || ispunct(row.at(c+1))) {E.fc = c; E.fr = r; return;}

      pos = row.find_first_of(delimiters, c);
      if (pos == std::string::npos) {E.fc = row.size() - 1; return;}
      else {E.fr = r; E.fc = pos - 1; return;}

    // we started on punct or space
    } else {
      if (row.at(c) == ' ') {
        if (c == row.size() - 1) {r++; c = 0; continue;}
        else {c++; continue;}

      } else {
        pos = row.find_first_not_of(delimiters_without_space, c);
        if (pos != std::string::npos) {E.fc = pos - 1; return;}
        else {E.fc = row.size() -1; return;}
      }
    }
  }
}

// not same as 'e' but moves to end of word or stays put if already
//on end of word - used by dw
//took out of use
void editorMoveEndWord2() {
  int j;

  if (E.rows.empty()) return;
  std::string &row = E.rows.at(E.fr);

  for (j = E.fc + 1; j < row.size() ; j++) {
    if (row[j] < 48) break;
  }

  E.fc = j - 1;

}

// used by 'w' -> goes to beginning of work left to right
// believe it matches vim in all cases
void editorMoveNextWord(void) {

  if (E.rows.empty()) return;

  // like with 'b', we are incrementing by one to start
  if (E.fc == E.rows.at(E.fr).size() - 1) {
      if (E.fr+1 > E.rows.size() - 1) {return;}
      E.fr++;
      E.fc = 0;
  } else E.fc++;

  int r = E.fr;
  int c = E.fc;
  int pos;
  std::string delimiters = " <>,.;?:()[]{}&#~'";
  std::string delimiters_without_space = "<>,.;?:()[]{}&#~'";

  for(;;) {

    if (r > E.rows.size() - 1) {return;}

    std::string &row = E.rows.at(r);

    if (row.empty()) {E.fr = r; E.fc = 0; return;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric

        if (c == 0) {E.fc = 0; E.fr = r; return;}
        if (ispunct(row.at(c-1))) {E.fc =c; E.fr = r; return;}

      pos = row.find_first_of(delimiters, c);
      // found punctuation or space after starting on alphanumeric
      if (pos != std::string::npos) {
          if (row.at(pos) == ' ') {c = pos; continue;}
          else {E.fc = pos; return;}
      }

      //did not find punctuation or space after starting on alphanumeric
      r++; c = 0; continue;
    }

    // we started on punctuation or space
    if (row.at(c) == ' ') {
       pos = row.find_first_not_of(' ', c);
       if (pos == std::string::npos) {r++; c = 0; continue;}
       else {E.fc = pos; E.fr = r; return;}
    // we started on punctuation and need to find first alpha
    } else {
        if (isalnum(row.at(c-1))) {E.fc = c; return;}
        c++;
        if (c > row.size() - 1) {r++; c=0; continue;}
        pos = row.find_first_not_of(delimiters_without_space, c); //this is equiv of searching for space or alphanumeric
        if (pos != std::string::npos) {
            if (row.at(pos) == ' ') {c = pos; continue;}
            else {E.fc = pos; return;}
        }
        // this just says that if you couldn't find a space or alpha (you're sitting on punctuation) go to next line
        r++; c = 0;
    }
  }
}

// normal mode 'b'
// not well tested but seems identical to vim including hanlding of runs of punctuation
void editorMoveBeginningWord(void) {
  if (E.rows.empty()) return;
  if (E.fr == 0 && E.fc == 0) return;

   //move back one character

  if (E.fc == 0) { // this should also cover an empty row
    E.fr--;
    E.fc = E.rows.at(E.fr).size() - 1;
    if (E.fc == -1) {E.fc = 0; return;}
  } else E.fc--;

  int r = E.fr;
  int c = E.fc;
  int pos;
  std::string delimiters = " ,.;?:()[]{}&#~'";

  for(;;) {

    std::string &row = E.rows.at(r);

    if (row.empty()) {E.fr = r; E.fc = 0; return;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric
      pos = row.find_last_of(delimiters, c);
      if (pos != std::string::npos) {E.fc = pos + 1; E.fr = r; return;}
      else {E.fc = 0; E.fr = r; return;}
    }

   // If we get here we started on punctuation or space
    if (row.at(c) == ' ') {
      if (c == 0) {
        r--;
        c = E.rows.at(r).size() - 1;
        if (c == -1) {E.fc = 0; E.fr = r; return;}
      } else {c--; continue;}
    }

    if (c == 0 || row.at(c-1) == ' ' || isalnum(row.at(c-1))) {E.fc = c; E.fr = r; return;}

    c--;
   }
}

// decorates, undecorates, redecorates
void editorDecorateWord(int c) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.at(E.fc) == ' ') return;

  //find beginning of word
  auto beg = row.find_last_of(' ', E.fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  //find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (row.substr(beg, 2) == "**") {
    row.erase(beg, 2);
    end -= 4;
    row.erase(end, 2);
    E.fc -=2;
    if (c == CTRL_KEY('b')) return;
  } else if (row.at(beg) == '*') {
    row.erase(beg, 1);
    end -= 2;
    E.fc -= 1;
    row.erase(end, 1);
    if (c == CTRL_KEY('i')) return;
  } else if (row.at(beg) == '`') {
    row.erase(beg, 1);
    end -= 2;
    E.fc -= 1;
    row.erase(end, 1);
    if (c == CTRL_KEY('e')) return;
  }

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  switch(c) {
    case CTRL_KEY('b'):
      row.insert(beg, "**");
      row.insert(end+2, "**"); //
      E.fc +=2;
      return;
    case CTRL_KEY('i'):
      row.insert(beg, "*");
      row.insert(end+1 , "*");
      E.fc++;
      return;
    case CTRL_KEY('e'):
      row.insert(beg, "`");
      row.insert(end+1 , "`");
      E.fc++;
      return;
  }
}

// only decorates which I think makes sense
void editorDecorateVisual(int c) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);

  int beg = E.highlight[0];
  int end = E.highlight[1] + 1;

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  switch(c) {
    case CTRL_KEY('b'):
      row.insert(beg, "**");
      row.insert(end+2, "**"); //
      E.fc += 2;
      return;
    case CTRL_KEY('i'):
      row.insert(beg, "*");
      row.insert(end+1 , "*");
      E.fc++;
      return;
    case CTRL_KEY('e'):
      row.insert(beg, "`");
      row.insert(end+1 , "`");
      E.fc++;
      return;
  }
}

//handles punctuation
std::string editorGetWordUnderCursor(void) {

  if (E.rows.empty()) return "";
  std::string &row = E.rows.at(E.fr);
  if (row[E.fc] < 48) return "";

  std::string delimiters = " ,.;?:()[]{}&#";

  // find beginning of word
  auto beg = row.find_last_of(delimiters, E.fc);
  if (beg == std::string::npos) beg = 0;
  else beg++;

  // find end of word
  auto end = row.find_first_of(delimiters, beg);
  if (end == std::string::npos) {end = row.size();}

  return row.substr(beg, end-beg);

  //editorSetMessage("beg = %d, end = %d  word = %s", beg, end, search_string.c_str());
}

void editorFindNextWord(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);

  int y = E.fr;
  int x = E.fc;
  size_t found;

  // make sure you're not sitting on search word to start
  auto beg = row.find_last_of(' ', E.fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  // find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (search_string == row.substr(beg, end-beg+1)) x++;
  int passes = 0;
  for(;;) {
    std::string& row = E.rows.at(y);
    found = row.find(search_string, x);
    if (found != std::string::npos)
       break;
    y++;
    if (y == E.rows.size()) {
      passes++;
      if (passes > 1) break;
      y = 0;
    }
    x = 0;
  }
  if (passes <= 1) {
    E.fc = found;
    E.fr = y;
  }
}
/* no longer doing links this way but preserving code
void editorMarkupLink(void) {
  int y, numrows, n, nn;
  std::string http = "http";
  std::string bracket_http = "[http";
  numrows = E.rows.size();

  // this could strip white space and could check if second char in number range
  // pos = E.rows.at().find_first_note_of(' ');
  // s = E.rows.at().substring(pos)
  n = 0;
  nn = numrows - 1;
  for(;;) {
    std::string_view row(E.rows.at(nn));
    if (row.size() < 10) { 
      if (n == 0) {
        nn--; continue;
      } else break;
    }
    size_t pos = row.find_first_not_of(' ');
    if (pos != std::string::npos) row = row.substr(pos); 
    if (row[0] == '[' && isdigit(row[1])) {
        nn--; n++;
    } else break;
   } 

  //n = 1;
  //for (n; E.rows[numrows-n][0] == '['; n++);
  n++;
  for (y=0; y<numrows; y++){
    std::string& row = E.rows.at(y);
    size_t pos = row.find_first_not_of(' ');
    if (pos == std::string::npos) pos = 0;
    if (row[pos] == '[' && isdigit(row[pos+1])) continue;
    if (row.find(bracket_http) != std::string::npos) continue;

    auto beg = row.find(http);
    if (beg==std::string::npos) continue;

    auto end = row.find(' ', beg);
    if (end == std::string::npos) end = row.size();
    std::string url = row.substr(beg, end - beg);
    row.insert(beg, "[");
    std::stringstream ss;
    ss << "][" << n << "]";
    row.insert(end + 1, ss.str());
    editorInsertRow(E.rows.size(), url);
    std::string& last_row = E.rows.back();
    std::stringstream ss2;
    ss2 << "[" << n << "]: ";
    last_row.insert(0, ss2.str());

    n++;
  }
  editorSetMessage("n = %d", n); //debug
  E.dirty++;
  E.fc = E.fr = E.line_offset = 0;
}
*/

void EraseScreenRedrawLines(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Erase the screen
  int pos = screencols/2;
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (int j = 1; j < screenlines + 1; j++) {

    // First vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos - OUTLINE_RIGHT_MARGIN + 1); //don't think need offset
    write(STDOUT_FILENO, buf, strlen(buf));
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    write(STDOUT_FILENO, "\x1b[37;1mx", 8);

    // Second vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mx", 8); 
}

  write(STDOUT_FILENO, "\x1b[1;1H", 6);
  for (int k=1; k < screencols ;k++) {
    // note: cursor advances automatically so don't need to 
    // do that explicitly
    write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
  }

  // draw first column's 'T' corner
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, pos - OUTLINE_RIGHT_MARGIN + 1); //may not need offset
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  // draw next column's 'T' corner
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, pos);
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
  write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
}

/*** init ***/

void initOutline() {
  O.cx = 0; //cursor x position
  O.cy = 0; //cursor y position
  O.fc = 0; //file x position
  O.fr = 0; //file y position
  O.rowoff = 0;  //number of rows scrolled off the screen
  O.coloff = 0;  //col the user is currently scrolled to  
  O.sort = "modified";
  O.show_deleted = false; //not treating these separately right now
  O.show_completed = true;
  O.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  O.highlight[0] = O.highlight[1] = -1;
  O.mode = NORMAL; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  O.last_mode = NORMAL;
  O.command[0] = '\0';
  O.command_line = "";
  O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  O.view = TASK; // not necessary here since set when searching database
  O.taskview = BY_FOLDER;
  O.folder = "todo";
  O.context = "No Context";
  O.keyword = "";

  // ? whether the screen-related stuff should be in one place
  O.screenlines = screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  O.screencols =  screencols/2 - OUTLINE_RIGHT_MARGIN - OUTLINE_LEFT_MARGIN; 
}

void initEditor(void) {
  E.cx = 0; //cursor x position
  E.cy = 0; //cursor y position
  E.fc = 0; //file x position
  E.fr = 0; //file y position
  E.line_offset = 0;  //the number of lines of text at the top scrolled off the screen
  E.prev_line_offset = 0;  //the number of lines of text at the top scrolled off the screen
  //E.coloff = 0;  //should always be zero because of line wrap
  E.dirty = 0; //has filed changed since last save
  E.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  E.highlight[0] = E.highlight[1] = -1;
  E.mode = 0; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  E.command[0] = '\0';
  E.command_line = "";
  E.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y
  E.indent = 4;
  E.smartindent = 1; //CTRL-z toggles - don't want on what pasting from outside source
  E.first_visible_row = 0;
  E.spellcheck = false;
  E.move_only = false;
  E.highlight_syntax = true; // should only apply to code

  // ? whether the screen-related stuff should be in one place
  E.screenlines = screenlines - 2 - TOP_MARGIN;
  E.screencols = -2 + screencols/2;
  EDITOR_LEFT_MARGIN = screencols/2 + 1;
}

int main(int argc, char** argv) { 

  //outline_normal_map[ARROW_UP] = move_up;
  //outline_normal_map['k'] = move_up;
  //zmq::context_t context (1);
  //zmq::socket_t publisher (context, ZMQ_PUB);
  publisher.bind("tcp://*:5556");
  publisher.bind("ipc://scroll.ipc"); 

  //publisher.bind("tcp://*:5557");
  //publisher.bind("ipc://html.ipc"); 

  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_pid = getpid();

  if (argc > 1 && argv[1][0] == '-') lm_browser = false;

  db_open();
  //get_conn(); //pg
  load_meta(); 

  which_db = SQLITE;

  map_context_titles();
  map_folder_titles();

  //if (getWindowSize(&screenlines, &screencols) == -1) die("getWindowSize");
  getWindowSize(&screenlines, &screencols);
  enableRawMode();
  EraseScreenRedrawLines();
  initOutline();
  initEditor();
  get_items(MAX);
  command_history.push_back("of todo"); //klugy - this could be read from config and generalized
  page_history.push_back("of todo"); //klugy - this could be read from config and generalized
  
 // PQfinish(conn); // this should happen when exiting

  // putting this here seems to speed up first search but still slow
  // might make sense to do the module imports here too
  // assume the reimports are essentially no-ops
  //Py_Initialize(); 

  if (lm_browser) popen (system_call.c_str(), "r"); //returns FILE* id

  signal(SIGWINCH, signalHandler);
  bool text_change;
  bool scroll;
  bool redraw;

  outlineRefreshScreen(); // now just draws rows
  outlineDrawStatusBar();
  outlineShowMessage("rows: %d  cols: %d", screenlines, screencols);
  return_cursor();

  while (1) {

    // just refresh what has changed
    if (editor_mode) {
      text_change = editorProcessKeypress(); 
      scroll = editorScroll();
      redraw = (E.mode == COMMAND_LINE) ? false : (text_change || scroll);
      editorRefreshScreen(redraw);
      ////////////////////
      if (scroll) {
        zmq::message_t message(20);
        snprintf ((char *) message.data(), 20, "%d", E.line_offset*25); //25 - complete hack but works ok
        publisher.send(message, zmq::send_flags::dontwait);
      }
      ////////////////////
    } else if (O.mode != FILE_DISPLAY) { 
      outlineProcessKeypress();
      outlineScroll();
      outlineRefreshScreen(); // now just draws rows
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode

    outlineDrawStatusBar();
    return_cursor();
  }
  return 0;
}
