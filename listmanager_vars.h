// note that listmanager_vars.h is really the future expanded Organizer header
#define LEFT_MARGIN 2
#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define UNUSED(x) (void)(x)
#define MAX 500 // max rows to bring back
#define TZ_OFFSET 5 // time zone offset - either 4 or 5
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this
#define LEFT_MARGIN_OFFSET 4

// to use GIT_BRANCH in makefile (from cmake)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <Python.h>
#include <sys/ioctl.h>
#include <csignal>
//#include <termios.h>
#include <libpq-fe.h>
#include <sqlite3.h>
#include "inipp.h" // https://github.com/mcmtroffaes/inipp

#include <string>
#include <vector> // doesn't seem necessary ? why
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <set>
#include <chrono>
#include <iomanip>  //provides get_time used in time_delta function
#include <fmt/format.h>
//#include <fcntl.h> //file locking
#include "Common.h"
#include "Organizer.h"

void outlineShowMessage2(const std::string &); //erases outline message area and writes message so can be called separately

template<typename... Args>
void outlineShowMessage3(fmt::string_view format_str, const Args & ... args) {
  fmt::format_args argspack = fmt::make_format_args(args...);
  outlineShowMessage2(fmt::vformat(format_str, argspack));
}

const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
const std::string DB_INI = "db.ini";

//std::string meta;
//std::stringstream display_text;
//int initial_file_row = 0; //for arrowing or displaying files

std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
const int SMARTINDENT = 4; //should be in config
constexpr char BASE_DATE[] = "1970-01-01 00:00";
//int temporary_tid = 99999;
int link_id = 0;
char link_text[20];
//std::unordered_set<int> marked_entries;
//struct flock lock; 
//std::vector<std::string> line_buffer = {}; //yanking lines

struct sqlite_db {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};
struct sqlite_db S;

const std::unordered_set<std::string> quit_cmds = {"quit", "q", "quit!", "q!", "x"};
const std::unordered_set<std::string> insert_cmds = {"I", "i", "A", "a", "o", "O", "s", "cw", "caw"};
const std::unordered_set<std::string> file_cmds = {"savefile", "save", "readfile", "read"};

//these are not really move only but are commands that don't change text and shouldn't trigger a new push_current diff record
//better name something like no_edit_cmds or non_edit_cmds
const std::unordered_set<std::string> move_only = {"w", "e", "b", "0", "$", ":", "*", "n", "[s","]s", "z=", "gg", "G", "yy"}; //could put 'u' ctrl-r here

struct config {
  std::string user;
  std::string password;
  std::string dbname;
  std::string hostaddr;
  int port;
  int ed_pct;
};
struct config c;

PGconn *conn = nullptr;

void eraseScreenRedrawLines(void);
void outlineProcessKeypress(int = 0);
bool editorProcessKeypress(void);
//void outlineDrawPreview(std::string &ab);
void outlineDrawRows(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawFilters(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawSearchRows(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineShowMessage(const char *fmt, ...); //erases outline message area and writes message so can be called separately
//void outlineShowMessage2(const std::string &); //erases outline message area and writes message so can be called separately
void outlineRefreshScreen(void); //erases outline area and sort/time screen columns and writes outline rows so can be called separately
void outlineDrawStatusBar(void);
void outlineDrawMessageBar(std::string&);
void outlineRefreshAllEditors(void);
std::string outlinePreviewRowsToString(void);
void lsp_start(void);
void lsp_shutdown(void);

void navigate_page_hx(int direction);
void navigate_cmd_hx(int direction);

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
void outlineInsertRow(int, std::string&&, bool, bool, bool, std::string&&);
void outlineScroll(void);
void outlineSave(const std::string &);
void return_cursor(void);
void get_preview(int);
void draw_preview(void);
void draw_search_preview(void);
std::string draw_preview_box(int, int);
std::string generateWWString(std::vector<std::string> &, int, int, std::string);
void highlight_terms_string(std::string &);
void get_search_positions(int);

//Database-related Prototypes
void db_open(void);
void update_task_context(std::string &, int);
void update_task_folder(std::string &, int);
int get_id(void);
void get_note(int);
std::string get_title(int);
void update_title(void);
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
//std::pair<std::string, std::vector<std::string>> get_task_keywords(void); // puts them in comma delimited string
std::pair<std::string, std::vector<std::string>> get_task_keywords(int); // used in F_copy_entry
std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string
void update_note(bool); //used by Editor class 
//void solr_find(void);
void search_db(const std::string &); //void fts5_sqlite(std::string);
void search_db2(const std::string &); //just searches documentation - should be combined with above
void get_items_by_id(std::stringstream &);
int get_folder_tid(int); 
void map_context_titles(void);
void map_folder_titles(void);
void add_task_keyword(std::string &, int);
void add_task_keyword(int, int, bool update_fts=true);
//void delete_task_keywords(void); -> F_deletekeywords
void display_item_info(int); 
void display_item_info(void); //ctrl-i in NORMAL mode 0x9
void display_item_info_pg(int);
void display_container_info(int);
int keyword_exists(const std::string &);  
int folder_exists(std::string &);
int context_exists(std::string &);
std::string time_delta(std::string);
std::string now(void);

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
int title_callback(void *, int, char **, char **);
int offset_callback(void *, int, char **, char **);
int folder_tid_callback(void *, int, char **, char **); 
int context_info_callback(void *, int, char **, char **); 
int folder_info_callback(void *, int, char **, char **); 
int keyword_info_callback(void *, int, char **, char **);
int count_callback(void *, int, char **, char **);
int unique_data_callback(void *, int, char **, char **);
int preview_callback (void *, int, char **, char **);

void synchronize(int);

// Not used by Editor class
void readFile(const std::string &);
void displayFile(void);
void eraseRightScreen(void); //erases the note section; redundant if just did an eraseScreenRedrawLines

//std::string generate_html(void);
//std::string generate_html2(void);
void load_meta(void);
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void update_code_file(void);
//void editorSaveNoteToFile(const std::string &);
//void editorReadFileIntoNote(const std::string &); 

void update_solr(void); //works but not in use
void open_in_vim(void);

