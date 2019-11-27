#define CTRL_KEY(k) ((k) & 0x1f) // )x1f is 31; first ascii is 32 space anding removes all higher bits
#define OUTLINE_LEFT_MARGIN 2
#define OUTLINE_RIGHT_MARGIN 18 // need this if going to have modified col
#define TOP_MARGIN 1
#define DEBUG 0
#define UNUSED(x) (void)(x)
#define MAX 500 // max rows to bring back
#define TZ_OFFSET 5 // time zone offset - either 4 or 5

#include <Python.h>
//#include <ctype.h>
//#include <errno.h>
#include <fcntl.h>
//#include <stdio.h>
//#include <stdarg.h>
//#include <stdlib.h>
//#include <string.h>
#include <sys/ioctl.h>
#include <csignal>
//#include <sys/types.h>
#include <termios.h>
//#include <time.h>
//#include <stdbool.h>
//#include <unistd.h>
#include <libpq-fe.h>
#include "inipp.h" //https://github.com/mcmtroffaes/inipp
#include <sqlite3.h>

#include <string>
//#include <string_view> //not in use yet
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <fstream>

/***the following is not yet in use and would get rid of switch statements***/
//typedef void (*pfunc)(void);
//static std::map<int, pfunc> outline_normal_map;

static const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
//static const std::string FTS_DB = "/home/slzatz/c_stuff/listmanager/fts5.db";
static const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
static const std::string DB_INI = "db.ini";
static int which_db;
static int EDITOR_LEFT_MARGIN;
static struct termios orig_termios;
static int screenlines, screencols, new_screenlines, new_screencols;
static std::stringstream display_text;
static int initial_file_row = 0; //for arrowing or displaying files
static bool editor_mode;
static std::string search_terms;
static std::vector<int> fts_ids;
static int fts_counter;
static std::string search_string; //word under cursor works with *, n, N etc.
static std::vector<std::string> line_buffer; //yanking lines
static std::string string_buffer; //yanking chars
static std::map<int, std::string> fts_titles;
static std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
static std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
static std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]
static std::vector<std::string> task_keywords;

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
  SEARCH
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
                        "SEARCH"
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

  C_find,
  C_fts,

  C_refresh,

  C_new, //create a new item

  C_contexts, //change O.view to CONTEXTs :c
  C_folders,  //change O.view to FOLDERs :f
  C_keywords,
  C_movetocontext,
  C_movetofolder,
  C_addkeyword,
  C_deletekeywords,

  C_delmarks,
  C_showall,

  C_update, //update solr db

  C_synch, // synchronixe sqlite and postgres dbs
  C_synch_test,//show what sync would do but don't do it 

  //C_highlight,

  C_quit,
  C_quit0,

  C_recent,

  C_help,

  C_edit,

  C_dbase,
  C_search,

  C_saveoutline,

  C_valgrind
};

static const std::unordered_map<std::string, int> lookuptablemap {
  {"caw", C_caw},
  {"cw", C_cw},
  {"daw", C_daw},
  {"dw", C_dw},
  {"de", C_de},
  {"dd", C_dd},
  {">>", C_indent},
  {"<<", C_unindent},
  {"gg", C_gg},
  {"gt", C_gt},
  {"yy", C_yy},
  {"d$", C_d$},

  {"help", C_help},
  {"open", C_open},
  {"o", C_open}, //need because this is command line command with a target word
  {"of", C_openfolder}, //need because this is command line command with a target word
  {"ok", C_openkeyword}, //need because this is command line command with a target word
  {"join", C_join}, //need because this is command line command with a target word
  {"filter", C_join}, //need because this is command line command with a target word
  {"fin", C_find},
  {"find", C_find},
  {"fts", C_fts},
  {"refresh", C_refresh},
  {"new", C_new}, //don't need "n" because there is no target
  {"contexts", C_contexts},
  {"context", C_contexts},
  {"folders", C_folders},
  {"folder", C_folders},
  {"keywords", C_keywords},
  {"keyword", C_keywords},
  {"kw", C_keywords},
  {"mtc", C_movetocontext}, //need because this is command line command with a target word
  {"movetocontext", C_movetocontext},
  {"mtf", C_movetofolder}, //need because this is command line command with a target word
  {"movetofolder", C_movetofolder},
  {"addkeyword", C_addkeyword},
  {"addkw", C_addkeyword},
  {"deletekeywords", C_deletekeywords},
  {"deletekeyword", C_deletekeywords},
  {"delkw", C_deletekeywords},
  {"delmarks", C_delmarks},
  {"delm", C_delmarks},
  {"update", C_update},
  {"sort", C_sort},
  {"sync", C_synch},
  {"synch", C_synch},
  {"synchronize", C_synch},
  {"test", C_synch_test},
  {"showall", C_showall},
  {"show", C_showall},
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
  {"dbase", C_dbase},
  {"database", C_dbase},
  {"search", C_search},
  {"saveoutline", C_saveoutline},
  {"so", C_saveoutline},
  {"valgrind", C_valgrind}
};

typedef struct orow {
  std::string title;
  std::string fts_title;
  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  bool dirty;
  bool mark;
  char modified[16];
  
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
  //char *filename; // in case try to save the titles
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
  int view; // enum TASK, CONTEXT, FOLDER
  int taskview; // enum BY_CONTEXT, BY_FOLDER, BY_RECENT, BY_SEARCH
};

static struct outlineConfig O;

struct editorConfig {
  int cx, cy; //cursor x and y position
  int fc, fr; // file x and y position
  int rx; //index into the render field - only nec b/o tabs
  int line_offset; //row the user is currently scrolled to
  int coloff; //column user is currently scrolled to
  int screenlines; //number of lines in the display
  int screencols;  //number of columns in the display
  std::vector<std::string> rows;
  std::vector<std::string> prev_rows;
  int dirty; //file changes since last save
  //char *filename;
  char message[120]; //status msg is a character array max 80 char
  int highlight[2];
  int mode;
  // probably OK that command is a char[] and not a std::string
  char command[10]; // right now includes normal mode commands and command line commands
  int repeat;
  int indent;
  int smartindent;
  int continuation;
};

static struct editorConfig E;

/* note that you can call these either through explicit dereference: (*get_note)(4328)
 * or through implicit dereference: get_note(4328)
*/
static void (*get_items)(int);
//static void (*get_items_by_id)(std::stringstream&);
static void (*get_note)(int);
static void (*update_note)(void);
static void (*toggle_star)(void);
static void (*toggle_completed)(void);
static void (*toggle_deleted)(void);
static void (*update_task_context)(std::string &, int);
static void (*update_task_folder)(std::string &, int);
static void (*update_rows)(void);
static void (*update_row)(void);
//static int (*insert_row)(int); //not called directly
static void (*display_item_info)(int);
static void (*touch)(void);
static void (*search_db)(void);
static void (*map_context_titles)(void);
static void (*map_folder_titles)(void);

static void (*get_containers)(void);
static void (*get_keywords)(void);
static void (*update_container)(void);
//static void (*insert_context)(void); //not called directly
static void (*add_task_keyword)(const std::string &, int);
static void (*delete_task_keywords)(void);
int getWindowSize(int *, int *);

// I believe this is being called at times redundantly before editorEraseScreen and outlineRefreshScreen
void EraseScreenRedrawLines(void);

void outlineProcessKeypress(void);
void editorProcessKeypress(void);

//Outline Prototypes
void outlineSetMessage(const char *fmt, ...);
void outlineRefreshScreen(void); //erases outline area but not sort/time screen columns
//void getcharundercursor();
void outlineDrawStatusBar(std::string&);
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
void outlineDrawRows(std::string&); // doesn't do any erasing which is done in outlineRefreshRows
void outlineDrawSearchRows(std::string&); //ditto
void outlineScroll(void);

void outlineSave(const std::string &);

//Database-related Prototypes
int get_id(void);
int insert_row_pg(orow&);
int insert_row_sqlite(orow&);
int insert_container_pg(orow&);
int insert_container_sqlite(orow&);
void update_container_sqlite(void);
void update_container_pg(void);
void get_items_sqlite(int); //////////
void get_items_pg(int);
void get_containers_sqlite(void); //has an if that determines callback: context_callback or folder_callback
void get_containers_pg(void);  //has an if that determines which columns go into which row variables (no callback in pg)
void update_note_pg(void);
void update_note_sqlite(void); 
void solr_find(void);
void fts5_sqlite(void);

void update_task_context_pg(const std::string &, int);
void update_task_context_sqlite(const std::string &, int);
void update_task_folder_pg(const std::string &, int);
void update_task_folder_sqlite(const std::string &, int);

void map_context_titles_pg(void);
void map_context_titles_sqlite(void);
void map_folder_titles_pg(void);
void map_folder_titles_sqlite(void);

void add_task_keyword_sqlite(const std::string &, int);
void add_task_keyword_pg(const std::string &, int);
void delete_task_keywords_pg(void);
void delete_task_keywords_sqlite(void);

//sqlite callback functions
int fts5_callback(void *, int, char **, char **);
int data_callback(void *, int, char **, char **);
int context_callback(void *, int, char **, char **);
int folder_callback(void *, int, char **, char **);
int keyword_callback(void *, int, char **, char **);
int context_titles_callback(void *, int, char **, char **);
int folder_titles_callback(void *, int, char **, char **);
int by_id_data_callback(void *, int, char **, char **);
int note_callback(void *, int, char **, char **);
void display_item_info_pg(int);
void display_item_info_sqlite(int);
int display_item_info_callback(void *, int, char **, char **);
int task_keywords_callback(void *, int, char **, char **);
int keyword_id_callback(void *, int, char **, char **);

void synchronize(int);

//Editor Prototypes
int *editorGetScreenPosFromRowCharPosWW(int, int); //do not delete but not currently in use
int *editorGetRowLineCharWW(void); //do not delete but not currently in use
int editorGetScreenYFromRowColWW(int, int); //used by editorScroll
int editorGetLinesInRowWW(int); //ESSENTIAL - do not delete

int editorGetFileRowByLineWW(int);
int editorGetLineInRowWW(int, int);
int editorGetCharInRowWW(int, int); 
int editorGetLineCharCountWW(int, int);
int editorGetScreenXFromRowCol(int, int);
int *editorGetRowLineScreenXFromRowCharPosWW(int, int);

void editorDrawRows(std::string&); //erases lines to right as it goes
void editorDrawMessageBar(std::string&);
void editorDrawStatusBar(std::string&);
void editorSetMessage(const char *fmt, ...);
void editorScroll(void);
void editorRefreshScreen(void); //(re)draws the note
void editorInsertReturn(void);
void editorDecorateWord(int c);
void editorDecorateVisual(int c);
void editorDelWord(void);
void editorIndentRow(void);
void editorUnIndentRow(void);
int editorIndentAmount(int y);
void editorMoveCursor(int key);
void editorBackspace(void);
void editorDelChar(void);
void editorDeleteToEndOfLine(void);
void editorYankLine(int n);
void editorPasteLine(void);
void editorPasteString(void);
void editorYankString(void);
void editorMoveCursorEOL(void);
void editorMoveCursorBOL(void);
void editorMoveBeginningWord(void);
void editorMoveEndWord(void); 
void editorMoveEndWord2(void); //not 'e' but just moves to end of word even if on last letter
void editorMoveNextWord(void);
void editorMarkupLink(void);
void getWordUnderCursor(void);
void editorFindNextWord(void);
void editorChangeCase(void);
void editorRestoreSnapshot(void); 
void editorCreateSnapshot(void); 
void editorInsertRow(int fr, std::string);
void editorReadFile(std::string);
void editorDisplayFile(void);
void editorEraseScreen(void); //erases the note section; redundant if just did an EraseScreenRedrawLines

int keyfromstringcpp(const std::string&);
int commandfromstringcpp(const std::string&, std::size_t&);

// config struct for reading db.ini file
struct config {
  std::string user;
  std::string password;
  std::string dbname;
  std::string hostaddr;
  int port;
};
struct config c;

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

    if (O.view == TASK && O.mode != NO_ROWS)
      get_note(O.rows.at(O.fr).id);

    // note this order puts cursor in outline regardless of where it was before resize
    outlineRefreshScreen();
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

void map_context_titles_pg(void) {

  // note it's id because it's pg
  std::string query("SELECT id,title FROM context;");

  PGresult *res = PQexec(conn, query.c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineSetMessage("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));

    //printf("No data retrieved\n");
    //printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    //do_exit(conn);
    return;
  }

  context_map.clear();
  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    context_map[std::string(PQgetvalue(res, i, 1))] = atoi(PQgetvalue(res, i, 0));
  }

  PQclear(res);
  // PQfinish(conn);
}

void map_folder_titles_pg(void) {

  // note it's id because it's pg
  std::string query("SELECT id,title FROM folder;");

  PGresult *res = PQexec(conn, query.c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }

  folder_map.clear();
  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    folder_map[std::string(PQgetvalue(res, i, 1))] = atoi(PQgetvalue(res, i, 0));
  }

  PQclear(res);
  // PQfinish(conn);
}

void map_context_titles_sqlite(void) {

  sqlite3 *db;
  char *err_msg = nullptr;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    //outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM context;");

    bool no_rows = true;
    rc = sqlite3_exec(db, query.c_str(), context_titles_callback, &no_rows, &err_msg);

    if (rc != SQLITE_OK ) {
      //outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    }
  sqlite3_close(db);

  if (no_rows)
    outlineSetMessage("There were no context titles to map!");
}

int context_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  context_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void map_folder_titles_sqlite(void) {

  sqlite3 *db;
  char *err_msg = nullptr;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    //outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM folder;");

    bool no_rows = true;
    rc = sqlite3_exec(db, query.c_str(), folder_titles_callback, &no_rows, &err_msg);

    if (rc != SQLITE_OK ) {
      //outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    }
  sqlite3_close(db);

  if (no_rows)
    outlineSetMessage("There were no folder titles to map!");
}

int folder_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  folder_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void get_items_pg(int max) {
  std::stringstream query;

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;


  if (O.taskview == BY_CONTEXT) {
    query << "SELECT * FROM task JOIN context ON context.id = task.context_tid"
          << " WHERE context.title = '" << O.context << "' ";
  } else if (O.taskview == BY_FOLDER) {
    query << "SELECT * FROM task JOIN folder ON folder.id = task.folder_tid"
          << " WHERE folder.title = '" << O.folder << "' ";
  } else if (O.taskview == BY_RECENT) {
    query << "SELECT * FROM task WHERE 1=1";
  } else if (O.taskview == BY_JOIN) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE context.title = '" << O.context << "'"
          << " AND folder.title = '" << O.folder << "'";
  } else if (O.taskview == BY_KEYWORD) {
    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND keyword.name = '" << O.keyword << "'";
  } else {
      outlineSetMessage("You asked for an unsupported db query");
      return;
  }
  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY task."
        << O.sort
        << " DESC NULLS LAST LIMIT " << max;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    printf("No data retrieved\n");        
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }    

  int rows = PQntuples(res);
  int sortcolnum = sort_map[O.sort];
  for(int i=0; i<rows; i++) {
    orow row;
    row.title = std::string(PQgetvalue(res, i, 3));
    row.id = atoi(PQgetvalue(res, i, 0));
    row.star = (*PQgetvalue(res, i, 8) == 't') ? true: false;
    row.deleted = (*PQgetvalue(res, i, 14) == 't') ? true: false;
    row.completed = (*PQgetvalue(res, i, 10)) ? true: false;
    row.dirty = false;
    row.mark = false;
    (PQgetvalue(res, i, sortcolnum) != nullptr) ? strncpy(row.modified, PQgetvalue(res, i, sortcolnum), 16) : strncpy(row.modified, " ", 16);
    O.rows.push_back(row);
  }

  PQclear(res);
  // PQfinish(conn);

  O.view = TASK;

  if (O.rows.empty()) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    //get_note(O.rows.at(0).id);
    if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
    else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

void get_containers_pg(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  //std::string query("SELECT * FROM context;");
  std::stringstream query;
  query << "SELECT * FROM "
        << ((O.view == CONTEXT) ? "context;" : "folder;");


  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }

  /* CONTEXT columns
  0: id => int
  1: tid => int
  2: title = string 32
  3: default = Boolean ? what this is
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */

  /* FOLDER Columns
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  4: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */

  int rows = PQntuples(res);

  if (O.view == CONTEXT) {
    for(int i=0; i<rows; i++) {
      orow row;
      row.title = std::string(PQgetvalue(res, i, 2));
      row.id = atoi(PQgetvalue(res, i, 0));
      row.star = (*PQgetvalue(res, i, 3) == 't') ? true: false;
      row.deleted = (*PQgetvalue(res, i, 5) == 't') ? true: false;
      row.completed = false;
      row.dirty = false;
      row.mark = false;
      strncpy(row.modified, PQgetvalue(res, i, 9), 16);
      O.rows.push_back(row);
    }
  } else {
    for(int i=0; i<rows; i++) {
      orow row;
      row.title = std::string(PQgetvalue(res, i, 2));
      row.id = atoi(PQgetvalue(res, i, 0));
      row.star = (*PQgetvalue(res, i, 3) == 't') ? true: false;
      row.deleted = (*PQgetvalue(res, i, 7) == 't') ? true: false;
      row.completed = false;
      row.dirty = false;
      row.mark = false;
      strncpy(row.modified, PQgetvalue(res, i, 11), 16);
      O.rows.push_back(row);
    }
  }
  // PQfinish(conn);
  PQclear(res);

  if (O.rows.empty()) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
  } else
    O.mode = NORMAL;

  O.context = O.folder = "";
}

void get_containers_sqlite(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  std::stringstream query;
  query << "SELECT * FROM "
        << ((O.view == CONTEXT) ? "context;" : "folder;");

    bool no_rows = true;

    if (O.view == CONTEXT) rc = sqlite3_exec(db, query.str().c_str(), context_callback, &no_rows, &err_msg);
    else rc = sqlite3_exec(db, query.str().c_str(), folder_callback, &no_rows, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    }
  sqlite3_close(db);

  if (no_rows) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
  } else
    O.mode = NORMAL;

  O.context = O.folder = "";

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
  4: "order" = integer
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

void get_keywords_sqlite(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  std::string query = "SELECT * FROM keyword ORDER BY name;";

  bool no_rows = true;

  rc = sqlite3_exec(db, query.c_str(), keyword_callback, &no_rows, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  sqlite3_close(db);

  if (no_rows) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
  } else
    O.mode = NORMAL;

  O.context = O.folder = "";

}

void get_task_keywords_sqlite(void) {

  task_keywords.clear();

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  std::stringstream query;
  //query << "SELECT task_keyword.task_id, task_keyword.keyword_id, keyword.id, keyword.name "
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << O.rows.at(O.fr).id << " =  task_keyword.task_id;";

    bool no_rows = true;

    rc = sqlite3_exec(db, query.str().c_str(), task_keywords_callback, &no_rows, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    }
  sqlite3_close(db);

}

int task_keywords_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  */

  task_keywords.push_back(std::string(argv[0]));

  return 0; //you need this
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
  */
  orow row;

  row.title = std::string(argv[1]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //private
  row.deleted = false;//(atoi(argv[7]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[4], 16);
  O.rows.push_back(row);

  return 0;
}

void get_keywords_pg(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::string query = "SELECT * FROM keyword ORDER BY name;";

  PGresult *res = PQexec(conn, query.c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineSetMessage("Problem retrieving the task's keywords");
    PQclear(res);
    return;
  }
  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {

    orow row;

    row.title = std::string(PQgetvalue(res, i, 1));
    row.id = atoi(PQgetvalue(res, i, 0)); //right now pulling sqlite id not tid
    row.star = false; //private
    row.deleted = false;//(atoi(argv[7]) == 1) ? true: false;
    row.completed = false;
    row.dirty = false;
    row.mark = false;
    strncpy(row.modified, BASE_DATE, 16);
    O.rows.push_back(row);
  }

  if (O.rows.empty()) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
  } else
    O.mode = NORMAL;

  O.context = O.folder = "";

}
void add_task_keyword_pg(const std::string &kw, int id) {

  std::stringstream query;
  // adds keyword user wants to add to task to list of keywords if it doesn't exist
  // for some reason right now can't alter pg keyword table to add star or modified!!
  query <<  "INSERT INTO keyword (name) VALUES ('" << kw << "') ON CONFLICT DO NOTHING;";  //<- works for postgres
  PGresult *res = PQexec(conn, query.str().c_str());
  //if (PQresultStatus(res) != PGRES_TUPLES_OK) { //for selects returning data
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Problem inserting into keyword table (whether kw present or not");
    //outlineSetMessage("Problem: %s", PQerrorMessage(conn));
    PQclear(res);
    return;
  }

  std::stringstream query2;
  //query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << O.rows.at(O.fr).id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
  query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";

  res = PQexec(conn, query2.str().c_str());
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Problem inserting into task_keyword");
    PQclear(res);
    return;
  }

  std::stringstream query3;
  // updates task modified column so know that something changed with the task
  //query3 << "UPDATE task SET modified = LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id =" << O.rows.at(O.fr).id << ";";
  query3 << "UPDATE task SET modified = LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id =" << id << ";";

  res = PQexec(conn, query3.str().c_str());
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Problem updating tasks modified date");
    PQclear(res);
    return;
  }
  PQclear(res);
}

void add_task_keyword_sqlite(const std::string &kw, int id) {

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  //IF NOT EXISTS(SELECT 1 FROM keyword WHERE name = 'mango') INSERT INTO keyword (name) VALUES ('mango') <- doesn't work for sqlite
  std::stringstream query;
  // adds keyword user wants to add to task to list of keywords if it doesn't exist
  // note you don't have to do INSERT OR IGNORE but could just do INSERT since there is a unique constraint on keyword.name
  // but you don't want to trigger an error either so probably best to retain INSERT OR IGNORE
  query <<  "INSERT OR IGNORE INTO keyword (name, star, modified) VALUES ('"
        <<  kw << "', true, datetime('now', '-" << TZ_OFFSET << " hours'));";  //<- works for sqlite

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    return;
  }

  std::stringstream query2;
  //query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << O.rows.at(O.fr).id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
  query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    return;
  }

  std::stringstream query3;
  // updates task modified column so know that something changed with the task
  //query3 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << O.rows.at(O.fr).id << ";";
  query3 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << id << ";";
  rc = sqlite3_exec(db, query3.str().c_str(), 0, 0, &err_msg);
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    return;
  }


  /**************fts virtual table update**********************/
  // tag update

  task_keywords.clear();
  get_task_keywords_sqlite();
  std::string delim = "";
  std::string s;
  for (const auto &kw : task_keywords) {
    s += delim += kw;
    delim = ",";
  }

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open fts database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  std::stringstream query4;
  //query4 << "Update fts SET tag='" << s << "' WHERE lm_id=" << O.rows.at(O.fr).id << ";";
  query4 << "Update fts SET tag='" << s << "' WHERE lm_id=" << id << ";";

  rc = sqlite3_exec(db, query4.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL fts error: %s", err_msg);
    sqlite3_free(err_msg);
  }
  sqlite3_close(db);
}

int keyword_id_callback(void *keyword_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(keyword_id);
  *id = atoi(argv[0]);
  return 0;
}

void delete_task_keywords_sqlite(void) {

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }
  std::stringstream query;
  query << "DELETE FROM task_keyword WHERE task_id = " << O.rows.at(O.fr).id << ";";
  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }

  std::stringstream query2;
  // updates task modified column so know that something changed with the task
  query2 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << O.rows.at(O.fr).id << ";";
  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    return;
  }
  /**************fts virtual table update**********************/

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open fts database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  std::stringstream query3;
  query3 << "Update fts SET tag='' WHERE lm_id=" << O.rows.at(O.fr).id << ";";

  rc = sqlite3_exec(db, query3.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL fts error: %s\n", err_msg);
    sqlite3_free(err_msg);
  }
  sqlite3_close(db);
}

void delete_task_keywords_pg(void) {

  std::stringstream query;
  query << "DELETE FROM task_keyword WHERE task_id = " << O.rows.at(O.fr).id << ";";
  PGresult *res = PQexec(conn, query.str().c_str());
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Problem deleting the task's keywords");
    PQclear(res);
    return;
  }

  std::stringstream query2;
  // updates task modified column so know that something changed with the task
  query2 << "UPDATE task SET modified = LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id =" << O.rows.at(O.fr).id << ";";
  res = PQexec(conn, query2.str().c_str());
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Problem updating tasks modified date");
    PQclear(res);
    return;
  }
  PQclear(res);
}

void get_task_keywords_pg(void) {

  std::stringstream query;
  task_keywords.clear();

  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << O.rows.at(O.fr).id << " =  task_keyword.task_id;";

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineSetMessage("Problem retrieving the task's keywords");
    PQclear(res);
    return;
  }
  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    task_keywords.push_back(PQgetvalue(res, i, 0));
  }
  PQclear(res);
  // PQfinish(conn);
}

void get_items_sqlite(int max) {
  std::stringstream query;

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = nullptr;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

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
    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND keyword.name = '" << O.keyword << "'";
  } else {
      outlineSetMessage("You asked for an unsupported db query");
      return;
  }

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY task."
        << O.sort
        << " DESC LIMIT " << max;

    int sortcolnum = sort_map[O.sort];
    rc = sqlite3_exec(db, query.str().c_str(), data_callback, &sortcolnum, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    }
  sqlite3_close(db);

  O.view = TASK;

  if (O.rows.empty()) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    //get_note(O.rows.at(0).id);
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
  (argv[*reinterpret_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*reinterpret_cast<int*>(sortcolnum)], 16)
                                                 : strncpy(row.modified, " ", 16);
  O.rows.push_back(row);

  return 0;
}

// called as part of :find -> fts5_sqlite(fts5_callback) -> get_items_by_id_sqlite (by_id_data_callback)
void get_items_by_id_sqlite(std::stringstream& query) {
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */

  // in fts5_sqlite
  //O.rows.clear();
  //O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    }

    bool no_rows = true;
    rc = sqlite3_exec(db, query.str().c_str(), by_id_data_callback, &no_rows, &err_msg);
    
    if (rc != SQLITE_OK ) {
        outlineSetMessage("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return; // can't close db twice
    } 

  sqlite3_close(db);

  O.view = TASK;

  if (no_rows) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = SEARCH;
    //get_note(O.rows.at(0).id);
    if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
    else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
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
  strncpy(row.modified, argv[16], 16);
  O.rows.push_back(row);

  return 0;
}

//brings back a set of ids generated by solr search
void get_items_by_id_pg(std::stringstream& query) {
  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");        
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }    
  
  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    orow row;
    row.title = std::string(PQgetvalue(res, i, 3));
    row.id = atoi(PQgetvalue(res, i, 0));
    row.star = (*PQgetvalue(res, i, 8) == 't') ? true: false;
    row.deleted = (*PQgetvalue(res, i, 14) == 't') ? true: false;
    row.completed = (*PQgetvalue(res, i, 10)) ? true: false;
    row.dirty = false;
    strncpy(row.modified, PQgetvalue(res, i, 16), 16);
    O.rows.push_back(row);
  }
  PQclear(res);

  O.view = TASK;

  if (O.rows.empty()) {
    outlineSetMessage("No results were returned");
    O.mode = NO_ROWS;
    editorEraseScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = DATABASE;
    //get_note(O.rows.at(0).id);
    if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
    else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

void get_note_sqlite(int id) {
  if (id ==-1) return; //maybe should be if (id < 0) and make all context id/tid negative

  /*
  if (!editor_mode && O.mode == DATABASE) {
    display_item_info(O.rows.at(O.fr).id);
    return;
  }
  */

  E.rows.clear();
  E.fr = E.fc = E.cy = E.cx = E.line_offset = 0; // 11-18-2019 commented out because in C_edit but a problem if you leave editor mode

  sqlite3 *db;
  char *err_msg = nullptr;
    
  std::stringstream query;
  int rc;

  if (editor_mode || O.mode != SEARCH) {
    query << "SELECT note FROM task WHERE id = " << id;
    rc = sqlite3_open(SQLITE_DB.c_str(), &db);
  } else {
    //17m = blue; 31m = red
    query << "SELECT highlight(fts, 1, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' AND lm_id = " << id;
    rc = sqlite3_open(FTS_DB.c_str(), &db);
  }

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    }

  // callback is *not* called if result (argv) is null
  rc = sqlite3_exec(db, query.str().c_str(), note_callback, nullptr, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("In get_note_sqlite: %s SAL error: %s", FTS_DB.c_str(), err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
  } 

  sqlite3_close(db);

  editorRefreshScreen();
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

void get_note_pg(int id) {
  if (id ==-1) return;

  /*
  if (!editor_mode && O.mode == DATABASE) {
    display_item_info(O.rows.at(O.fr).id);
    return;
  }
  */

  E.rows.clear();
  E.fr = E.fc = E.cy = E.cx = E.line_offset = 0; //11-18-2019 commented out because in C_edit but a problem if you leave editor mode

  std::stringstream query;
  query << "SELECT note FROM task WHERE id = " << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    outlineSetMessage("Problem retrieving note\n");        
    PQclear(res);
    //do_exit(conn);
  }    
  std::string note(PQgetvalue(res, 0, 0));
  note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    //snote will not contain the '\n'
    editorInsertRow(E.rows.size(), s);
  }

  E.dirty = 0;
  editorRefreshScreen();
  PQclear(res);
  return;
}

void view_html(int id) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  Py_Initialize();
  if (which_db == POSTGRES)
    pName = PyUnicode_DecodeFSDefault("view_html_pg"); //module
  else 
    pName = PyUnicode_DecodeFSDefault("view_html_sqlite"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "view_html"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); // PyTuple_New(x) creates a tuple with x elements
          pValue = Py_BuildValue("i", id); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
            outlineSetMessage("Successfully rendered the note in html");
          }
          else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Was not able to render the note in html!");
          }
      }
      else {
          if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: view_html!");
      }
      Py_XDECREF(pFunc);
      Py_DECREF(pModule);
  }
  else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: view_html!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
}

void solr_find(void) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("solr_find"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "solr_find"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = Py_BuildValue("s", search_terms.c_str()); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              Py_ssize_t size; 
              int len = PyList_Size(pValue);

          if (O.rows.empty()) {
            outlineSetMessage("No results were returned");
            O.mode = NO_ROWS;
            return;
          }
              /*
              We want to create a query that looks like:
              SELECT * FROM task 
              WHERE task.id IN (1234, 5678, , 9012) 
              ORDER BY task.id = 1234 DESC, task.id = 5678 DESC, task.id = 9012 DESC
              */

              std::stringstream query;

              query << "SELECT * FROM task WHERE task.id IN (";

              for (int i=0; i<len-1; i++) {
                query << PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size) << ", ";
              }
              query << PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, len-1), &size)
                    << ")"
                    << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
                    << " ORDER BY ";

              for (int i = 0; i < len-1; i++) {
                query << "task.id = " << PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size) << " DESC, ";
               }
              query << "task.id = " << PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, len-1), &size) << " DESC";

              Py_DECREF(pValue);
              //DEBUGGING
              //outlineSetMessage(query.str().c_str());
              //return;
              get_items_by_id_pg(query);
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Problem retrieving ids from solr!");
          }
      } else {
          if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: solr_find!");
      }
      Py_XDECREF(pFunc);
      Py_DECREF(pModule);
  } else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: solr_find!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

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
                  outlineSetMessage("Problem converting c variable for use in calling python function");
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
              outlineSetMessage("Problem retrieving ids from solr!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: update_solr!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: update_solr!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

  outlineSetMessage("%d items were added/updated to solr db", num);
}

int keyfromstringcpp(const std::string& key) {
  /*
  std::unordered_map<std::string,int>::const_iterator it;
  it = lookuptablemap.find(key); // or could have been done through count(key) which returns 0 or 1
  if (it != lookuptablemap.end())
    return it->second;
  else
    return -1;
  */

  // c++20 = c++2a contains on associate containers
  if (lookuptablemap.contains(key))
    return lookuptablemap.at(key); //note can't use [] on const unordered map since it could change map
  else
    return -1;


}

int commandfromstringcpp(const std::string& key, std::size_t& found) { //for commands like find nemo - that consist of a command a space and further info

  if (key.size() == 1) return key[0];  //need this

  found = key.find(' ');
  if (found != std::string::npos) {
    std::string command = key.substr(0, found);
    return keyfromstringcpp(command);
  } else
    return keyfromstringcpp(key);
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

   /*Note that ctrl-key maps to ctrl-A=1, ctrl-b=2 etc.*/

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  /* if the character read was an escape, need to figure out if it was
     a recognized escape sequence or an isolated escape to switch from
     insert mode to normal mode or to reset in normal mode
  */

  if (c == '\x1b') {
    char seq[3];
    //outlineSetMessage("You pressed %d", c); //slz
    // the reads time out after 0.1 seconds
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Assumption is that seq[0] == '[' 
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //need 4 bytes
      if (seq[2] == '~') {
        //outlineSetMessage("You pressed %c%c%c", seq[0], seq[1], seq[2]); //slz
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
        //outlineSetMessage("You pressed %c%c", seq[0], seq[1]); //slz
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
      //outlineSetMessage("You pressed %d", c); //slz
      return c;
  }
}

int getWindowSize(int *rows, int *cols) {

//TIOCGWINSZ = fill in the winsize structure
/*struct winsize
{
  unsigned short ws_row;	 rows, in characters 
  unsigned short ws_col;	 columns, in characters 
  unsigned short ws_xpixel;	 horizontal size, pixels 
  unsigned short ws_ypixel;	 vertical size, pixels 
};*/

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

  //outlineSetMessage("Can't save! I/O error: %s", strerror(errno));
  outlineSetMessage("saved to outline.txt");
}

/*** editor row operations ***/

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
  if (E.rows.size() == 0) return; // some calls may duplicate this guard

  E.rows.erase(E.rows.begin() + r);
  //E.numrows--;
  if (E.rows.size() == 0) {
    E.rows.clear();
    E.fr = 0;
    E.fc = 0;
    return;
  }

  E.dirty++;
  //editorSetMessage("Row deleted = %d; E.numrows after deletion = %d E.cx = %d E.row[fr].size = %d", fr, E.numrows, E.cx, E.row[fr].size); 
}

// only used by editorBackspace
void editorRowAppendString(std::string& row, std::string& s) {
  row.insert(row.end() - 1, s.begin(), s.end());
  E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int chr) {
  if (E.rows.empty()) {
    editorInsertRow(0, std::string());
  }
  std::string& row = E.rows.at(E.fr);
  row.insert(row.begin() + E.fc, chr);
  E.dirty++;
  E.fc++;
}

void editorInsertReturn(void) { // right now only used for editor->INSERT mode->'\r'
  if (E.rows.empty()) {
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
  if (E.rows.empty()) {
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
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.empty()) return;
  row.erase(row.begin() + E.fc);
  E.dirty++;
}

// used by 'x' in editor/visual mode
void editorDelChar2(int fr, int fc) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(fr);
  if (row.empty()) return;
  row.erase(row.begin() + fc);
  E.dirty++;
}

//Really need to look at this
void editorBackspace(void) {

  if (E.fc == 0 && E.fr == 0) return;

  std::string& row = E.rows.at(E.fr);

  if (E.fc > 0) {
    if (E.cx > 0) { // We want this E.cx - don't change to E.fc!!!
      row.erase(row.begin() + E.fc - 1);

     // below seems like a complete kluge but definitely want that E.cx!!!!!
      if (E.cx == 1 && E.fc > 1) E.continuation = 1; //right now only backspace in multi-line
      E.fc--;

    } else {  //E.fc > 0 and E.cx = 0 meaning you're in line > 1 of a multiline row
      row.erase(row.begin() + E.fc - 1);
      E.fc--;
      E.continuation = 0;
    } 
  } else {// this means we're at fc == 0 so we're in the first filecolumn
    editorRowAppendString(E.rows.at(E.fr - 1), row); //only use of this function
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
  z.pop_back(); //pop last return that we added
  return z;
}

// erases note
void editorEraseScreen(void) {

  E.rows.clear();

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

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void editorReadFile(std::string file_name) {

  std::ifstream f(file_name);
  std::string line;

  display_text.str(std::string());
  display_text.clear();
  //display_text.seekg(0, std::ios::beg); /////////////

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

  //abAppend(&ab, "\x1b[44m", 5); //tried background blue - didn't love it
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
    if (row.size() < E.screencols) {
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
  outlineDrawStatusBar(ab);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

//not used
void editorSave(void) {

  std::ofstream myfile;
  myfile.open("test_save"); //filename
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

    if (row.star) ab.append("\x1b[1m", 4); //bold
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    if (row.mark) ab.append("\x1b[46m", 5); //cyan background

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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - OUTLINE_RIGHT_MARGIN + 2);
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append("\x1b[0m", 4); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
}

void outlineDrawSearchRows(std::string& ab) {
  char buf[32];

  if (O.rows.empty()) return;

  int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN);

  int spaces;

  for (y = 0; y < O.screenlines; y++) {
    int fr = y + O.rowoff;
    if (fr > O.rows.size() - 1) return;
    orow& row = O.rows[fr];
    int len;

    if (row.star) ab.append("\x1b[1m", 4); //bold

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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - OUTLINE_RIGHT_MARGIN + 2);
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
}

//status bar has inverted colors
void outlineDrawStatusBar(std::string& ab) {

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
  char status[80], rstatus[80];

  std::string s;
  if (O.view == TASK) {
      if (O.taskview == BY_SEARCH) { s = "search";
    } else if (O.taskview == BY_FOLDER) { s = O.folder + "[f]";
    } else if (O.taskview == BY_CONTEXT) { s = O.context + "[c]";
    } else if (O.taskview == BY_RECENT) {s = "recent";
    } else if (O.taskview == BY_JOIN) {s = O.context + "[c] + " + O.folder + "[f]";
    } else if (O.taskview == BY_KEYWORD) {s = O.keyword + "[k]";}
  } else if (O.view == CONTEXT) {
    s = "Contexts";
  } else if (O.view == FOLDER) {
    s = "Folders";
  } else if (O.view == KEYWORD) {
    s = "Keywords";
  }

  if (O.rows.empty()) { //********************************** or (!O.numrows)
    len = snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
  } else {

    orow& row = O.rows.at(O.fr);
    std::string truncated_title = row.title.substr(0, 19);

    len = snprintf(status, sizeof(status),
                              // because video is reversted [42 sets text to green and 49 undoes it
                              // I think the [0;7m is revert to normal and reverse video
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              truncated_title.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());

  }
  //because of escapes
  len-=22;

  int rlen = snprintf(rstatus, sizeof(rstatus), "\x1b[1m %s %s\x1b[0;7m ", ((which_db == SQLITE) ? "sqlite" : "postgres"), "c++");

  if (len > O.screencols + OUTLINE_LEFT_MARGIN) len = O.screencols + OUTLINE_LEFT_MARGIN;

  ab.append(status, len + 22);

  while (len < O.screencols + OUTLINE_LEFT_MARGIN) {
    if ((O.screencols + OUTLINE_LEFT_MARGIN - len) == rlen - 10) { //10 of chars not printable
      ab.append(rstatus, rlen);
      break;
    } else {
      ab.append(" ", 1);
      len++;
    }
  }

  ab.append("\x1b[m", 3); //switches back to normal formatting
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

  if (0)
    outlineSetMessage("length = %d, O.cx = %d, O.cy = %d, O.fc = %d, O.fr = %d row id = %d", O.rows.at(O.fr).title.size(), O.cx, O.cy, O.fc, O.fr, get_id());

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  for (int j=TOP_MARGIN; j < O.screenlines + 1;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
    //O.screencols + OUTLINE_LEFT_MARGIN);
    O.screencols + OUTLINE_LEFT_MARGIN + 17); ////////////////////////////////////////////////////////////////////////////
    ab.append(buf, strlen(buf));
  }

  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , OUTLINE_LEFT_MARGIN + 1); // *****************
  ab.append(buf, strlen(buf));

  if (O.mode == SEARCH)
    outlineDrawSearchRows(ab);
  else
    outlineDrawRows(ab); //unlike editorDrawRows, outDrawRows doesn't do any erasing

  outlineDrawStatusBar(ab);
  outlineDrawMessageBar(ab);

  //[y;xH positions cursor and [1m is bold [31m is red and here they are
  //chained (note syntax requires only trailing 'm')
  if (O.mode == SEARCH || O.mode == DATABASE) {
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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.screenlines + 2 + TOP_MARGIN, O.command_line.size() + OUTLINE_LEFT_MARGIN); /// ****
    ab.append(buf, strlen(buf));
    ab.append("\x1b[?25h", 6); // want to show cursor in non-DATABASE modes
  }
  ab.append("\x1b[0m", 4); //return background to normal
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void outlineSetMessage(const char *fmt, ...) {
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  std::vsnprintf(O.message, sizeof(O.message), fmt, ap);
  va_end(ap); //free a va_list
}

//Note: outlineMoveCursor worries about moving cursor beyond the size of the row
//OutlineScroll worries about moving cursor beyond the screen
void outlineMoveCursor(int key) {

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (O.fc > 0) O.fc--; 
      // arowing left in NORMAL puts you into DATABASE mode
      else {
        O.mode = DATABASE;
        display_item_info(O.rows.at(O.fr).id);
        O.command[0] = '\0';
        O.repeat = 0;
      }
      break;

    case ARROW_RIGHT:
    case 'l':
    {
      orow& row = O.rows.at(O.fr);
      if (!O.rows.empty()) O.fc++;
      break;
    }
    case ARROW_UP:
    case 'k':
      if (O.fr > 0) O.fr--; 
      O.fc = O.coloff = 0; 

      if (O.view == TASK) {
        if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
        else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
      }
      break;

    case ARROW_DOWN:
    case 'j':
      if (O.fr < O.rows.size() - 1) O.fr++;
      O.fc = O.coloff = 0;
      if (O.view == TASK) {
        if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
        else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
      }
      break;
  }

  orow& row = O.rows.at(O.fr);
  if (O.fc >= row.title.size()) O.fc = row.title.size() - (O.mode != INSERT);
}

// depends on readKey()
void outlineProcessKeypress(void) {
  int start, end, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  size_t n;
  switch (int c = readKey(); O.mode) { //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          O.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          O.command_line.clear();
          outlineSetMessage(":");
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
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.fc = O.fr = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
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
          outlineSetMessage("\'%s\' will be opened", O.folder.c_str());
        } else {
          if (O.context.empty() || O.context == "search") {
            it = context_map.begin();
          } else {
            it = context_map.find(O.context);
            it++;
            if (it == context_map.end()) it = context_map.begin();
          }
          O.context = it->first;
          outlineSetMessage("\'%s\' will be opened", O.context.c_str());
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
          if (O.view == TASK) update_row();
          else update_container();
          O.command[0] = '\0'; //11-26-2019
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          //outlineSetMessage("");
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
          outlineSetMessage("");
          return;

        default:
          outlineInsertChar(c);
          return;
      } //end of switch inside INSERT

     // return; //End of case INSERT: No need for a return at the end of INSERT because we insert the characters that fall through in switch default:

    case NORMAL:  

      if (c == '\x1b') {
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
 
      /*leading digit is a multiplier*/
      if (isdigit(c)) { //equiv to if (c > 47 && c < 58) 
        if ( O.repeat == 0 ){

          //if c=48=>0 then it falls through to move to beginning of line
          if ( c != 48 ) { 
            O.repeat = c - 48;
            return;
          }  

        } else { 
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

          if (O.view == TASK) {
            update_row();
            O.command[0] = '\0';
            return;
          }
          // because of first if above - this is in effect:
          //if (O.view != TASK && row.dirty)
          if (row.dirty) { // means context or folder title has changed
            update_container();
            O.command[0] = '\0';
            return;
          }
          // return means retrieve items by context or folder
          if (O.view == CONTEXT) {
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
          }
          get_items(MAX);
          O.command[0] = '\0';
          return;

        // should look at these since using arrow key and 
        // don't use these at all
        case '<':
        case '\t':
        case SHIFT_TAB:
          O.fc = 0; //intentionally leave O.fr wherever it is
          O.mode = DATABASE;
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'i':
          O.mode = INSERT;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 's':
          for (int i = 0; i < O.repeat; i++) outlineDelChar();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = INSERT;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
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
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'A':
          outlineMoveCursorEOL();
          O.mode = INSERT; //needs to be here for movecursor to work at EOLs
          outlineMoveCursor(ARROW_RIGHT);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
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
          if (!O.rows.empty()) O.fc = 0;
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
            outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          }
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'G':
          O.fc = 0;
          O.fr = O.rows.size() - 1;
          O.command[0] = '\0';
          O.repeat = 0;

          if (O.view == TASK) { //{get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
            if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
            else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
          }
          return;
      
        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.fc = O.fr = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          editorEraseScreen(); //erases the note area
          O.mode = INSERT;
          return;

        case ':':
          outlineSetMessage(":");
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
          outlineSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
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
          if (O.view == TASK) {
            O.rows.at(O.fr).mark = !O.rows.at(O.fr).mark;
          outlineSetMessage("Toggle mark for item %d", O.rows.at(O.fr).id);
          }
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
          {
          orow& row = O.rows.at(O.fr);
          view_html(row.id);

          /*
          not getting error messages with qutebrowser
          so below not necessary (for the moment)
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */

          O.command[0] = '\0';
          return;
          }

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            O.fr -= O.screenlines; //should be screen lines although same
            if (O.fr < 0) O.fr = 0;
          } else {
             O.fr += O.screenlines;
             if (O.fr > O.rows.size() - 1) O.fr = O.rows.size() - 1;
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

        // not sure that this should be CTRL-h; maybe CTRL-m
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          editorRefreshScreen();
          update_note(); // write updated note to database
          O.command[0] = '\0';
          return;

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
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        //tested with repeat on one line
        case C_caw:
          for (int i = 0; i < O.repeat; i++) outlineDelWord();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = INSERT;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case C_gg:
          O.fc = O.rowoff = 0;
          O.fr = O.repeat-1; //this needs to take into account O.rowoff
          O.command[0] = '\0';
          O.repeat = 0;
          if (O.view == TASK) {
            if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
            else get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
          }
          //if (O.view == TASK) get_note(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
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
            outlineSetMessage("\'%s\' will be opened", O.folder.c_str());
          } else {
            if (O.context.empty() || O.context == "search") {
              it = context_map.begin();
            } else {
              it = context_map.find(O.context);
              it++;
              if (it == context_map.end()) it = context_map.begin();
            }
            O.context = it->first;
            outlineSetMessage("\'%s\' will be opened", O.context.c_str());
          }
          //EraseScreenRedrawLines(); //*****************************
          get_items(MAX);
          //editorRefreshScreen(); //in get_note
          O.command[0] = '\0';
          return;
          }

        case C_edit: //CTRL-W,CTRL-W
          // can't edit note if rows_are_contexts
          if (!O.view == TASK) {
            O.command[0] = '\0';
            O.mode = NORMAL;
            outlineSetMessage("Contexts and Folders do not have notes to edit");
            return;
          }
          {
          int id = get_id();
          if (id != -1) {
            outlineSetMessage("Edit note %d", id);
            outlineRefreshScreen();
            //editor_mode needs go before get_note in case we retrieved item via a search
            editor_mode = true;
            get_note(id); //if id == -1 does not try to retrieve note
            E.mode = NORMAL;
            E.command[0] = '\0';
          } else {
            outlineSetMessage("You need to save item before you can "
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
          outlineSetMessage(""); 
          return;

        case '\r':
          std::size_t pos;

          // passes back position of space (if there is one) in var pos
          command = commandfromstringcpp(O.command_line, pos); //assume pos paramter is now a reference but should check
          switch(command) {

            case 'w':
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
                outlineSetMessage("Tasks will be refreshed");
                if (O.taskview == BY_SEARCH)
                  search_db();
                else
                  get_items(MAX);
              } else {
                outlineSetMessage("contexts/folders will be refreshed");
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
              outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
              editorEraseScreen(); //erases the note area
              O.mode = INSERT;
              return;

            case 'e':
            case C_edit: //edit the note of the current item
              if (!O.view == TASK) {
                O.command[0] = '\0';
                O.mode = NORMAL;
                outlineSetMessage("Only tasks have notes to edit!");
                return;
              }
              {
              int id = get_id();
              if (id != -1) {
                outlineSetMessage("Edit note %d", id);
                outlineRefreshScreen();
                //editor_mode needs go before get_note in case we retrieved item via a search
                editor_mode = true;
                get_note(id); //if id == -1 does not try to retrieve note
                E.fr = E.fc = E.cx = E.cy = E.line_offset = 0;
                E.mode = NORMAL;
                E.command[0] = '\0';
              } else {
                outlineSetMessage("You need to save item before you can "
                                  "create a note");
              }
              O.command[0] = '\0';
              O.mode = NORMAL;
              return;
              }

            case C_find: //catches 'fin' and 'find' 
              if (O.command_line.size() < 6) {
                outlineSetMessage("You need more characters");
                return;
              }  

              O.context = "";
              O.folder = "";
              O.taskview = BY_SEARCH;
              search_terms = O.command_line.substr(pos+1);
              search_db();
              return;

            case C_fts: 
              if (O.command_line.size() < 6) {
                outlineSetMessage("You need more characters");
                return;
              }  

              //EraseScreenRedrawLines(); //*****************************
              O.context = "search";
              search_terms = O.command_line.substr(pos+1);
              fts5_sqlite();
              if (O.mode != NO_ROWS) {
                O.mode = DATABASE;
                get_note(get_id());
              } else {
                outlineRefreshScreen();
              }
              return;

            case C_update: //update solr
              update_solr();
              O.mode = NORMAL;
              return;

            case 'c':
            case C_contexts: //catches context, contexts and c
              editorEraseScreen();
              O.view = CONTEXT;
              get_containers();
              O.mode = NORMAL;
              outlineSetMessage("Retrieved contexts");
              return;

            case 'f':
            case C_folders: //catches folder, folders and f
              editorEraseScreen();
              O.view = FOLDER;
              get_containers();
              O.mode = NORMAL;
              outlineSetMessage("Retrieved folders");
              return;

            case 'k':
            case C_keywords: //catches keyword, keywords and kw
              editorEraseScreen();
              O.view = KEYWORD;
              get_keywords();
              O.mode = NORMAL;
              outlineSetMessage("Retrieved keywords");
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
                  outlineSetMessage("What you typed did not match any context");
                  return;
                }

              } else {
                outlineSetMessage("You need to provide at least 3 characters "
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
                outlineSetMessage("Marked tasks moved into context %s", new_context.c_str());
              } else {
                update_task_context(new_context, O.rows.at(O.fr).id);
                outlineSetMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
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
                  outlineSetMessage("What you typed did not match any folder");
                  return;
                }

              } else {
                outlineSetMessage("You need to provide at least 3 characters "
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
                outlineSetMessage("Marked tasks moved into folder %s", new_folder.c_str());
              } else {
                update_task_folder(new_folder, O.rows.at(O.fr).id);
                outlineSetMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
              }
              O.mode = O.last_mode;
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              return;
              }

            case C_addkeyword:
            {
              if (O.last_mode == NO_ROWS) return;
              std::string keyword = O.command_line.substr(pos+1);
              outlineSetMessage("keyword \'%s\' will be added to task %d", keyword.c_str(), O.rows.at(O.fr).id);

              bool success = false;
              for (const auto& it : O.rows) {
                if (it.mark) {
                  add_task_keyword(keyword, it.id);
                  success = true;
                }
              }

              if (success) {
                outlineSetMessage("Marked tasks had keyword %s added", keyword.c_str());
              } else {
                add_task_keyword(keyword, O.rows.at(O.fr).id);
                outlineSetMessage("No tasks were marked so added %s to current task", keyword.c_str());
              }
              O.mode = O.last_mode;
              //get_note(O.rows.at(O.fr).id); // 11-26-2019
              if (O.mode == DATABASE) display_item_info(O.rows.at(O.fr).id);
              return;
            }

            case C_deletekeywords:
              outlineSetMessage("Keyword(s) for task %d will be deleted and fts updated if sqlite", O.rows.at(O.fr).id);
              delete_task_keywords();
              O.mode = O.last_mode;
              return;

            case C_open: //by context
              {
              std::string new_context;
              if (pos) {
                bool success = false;
                /*
                for (auto i : context_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], i.first.c_str(), 3) == 0) {
                    new_context = i.first;
                    success = true;
                    break;
                  }*/
                //structured bindings
                for (const auto & [k,v] : context_map) {
                  if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
                    new_context = k;
                    success = true;
                    break;
                  }
                }
                if (!success) return;

              } else {
                outlineSetMessage("You did not provide a valid  context!");
                //O.command_line[1] = '\0';
                O.command_line.resize(1);
                return;
              }
              //EraseScreenRedrawLines(); //*****************************
              outlineSetMessage("\'%s\' will be opened", new_context.c_str());
              O.context = new_context;
              O.folder = "";
              O.taskview = BY_CONTEXT;
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              O.mode = O.last_mode;
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
                if (!success) return;

              } else {
                outlineSetMessage("You did not provide a valid  folder!");
                O.command_line.resize(1);
                return;
              }
              //EraseScreenRedrawLines(); //*****************************
              outlineSetMessage("\'%s\' will be opened", O.folder.c_str());
              O.context = "";
              O.taskview = BY_FOLDER;
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              return;


            case C_openkeyword:

              if (!pos) {
                outlineSetMessage("You need to provide a keyword");
                return;
              }

              O.keyword = O.command_line.substr(pos+1);
              //EraseScreenRedrawLines(); //*****************************
              outlineSetMessage("\'%s\' will be opened", O.keyword.c_str());
              O.context = "No Context";
              O.folder = "No Folder";
              O.taskview = BY_KEYWORD;
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              return;


            case C_join:
              {
              if (O.view != TASK || O.taskview == BY_JOIN || pos == 0) {
                outlineSetMessage("You are either in a view where you can't join or provided no join container");
                //O.command_line.clear();
                //O.command[0] = '\0';
                O.mode = NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
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
                outlineSetMessage("You did not provide a valid folder or context to join!");
                O.command_line.resize(1);
                return;
              }

               //EraseScreenRedrawLines(); //needed to erase sort (time) column*****************************
               outlineSetMessage("Will join \'%s\' with \'%s\'", O.folder.c_str(), O.context.c_str());
               O.taskview = BY_JOIN;
               get_items(MAX);
               return;
               }

            case C_sort:
              if (pos && O.view == TASK && O.taskview != BY_SEARCH) {
                //EraseScreenRedrawLines(); //*****************************
                O.sort = O.command_line.substr(pos + 1);
                get_items(MAX);
                outlineSetMessage("sorted by \'%s\'", O.sort.c_str());
              } else {
                outlineSetMessage("Currently can't sort search, which is sorted on best match");
              }
              return;

            case C_recent:
              //EraseScreenRedrawLines(); //*****************************
              outlineSetMessage("Will retrieve recent items");
              O.context = "recent";
              O.taskview = BY_RECENT;
              O.folder = "";
              get_items(MAX);
              //editorRefreshScreen(); //in get_note
              return;

            case 's':
            case C_showall:
              if (O.view == TASK) {
                O.show_deleted = !O.show_deleted;
                O.show_completed = !O.show_completed;
                if (O.taskview == BY_SEARCH)
                  search_db();
                else
                  get_items(MAX);
              }
              outlineSetMessage((O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
              return;

            case C_synch:
              synchronize(0); // do actual sync
              map_context_titles();
              map_folder_titles();
              initial_file_row = 0; //for arrowing or displaying files
              //O.last_mode = O.mode; // probably COMMAND_LINE, right
              O.mode = FILE_DISPLAY; // needs to appear before editorDisplayFile
              outlineSetMessage("Synching local db and server and displaying results");
              //outlineRefreshScreen(); ////////////////////////////////////////////
              editorReadFile("log");
              editorDisplayFile();//put them in the command mode case synch
              return;

            case C_synch_test:
              synchronize(1); //1 -> report_only

              initial_file_row = 0; //for arrowing or displaying files
              //O.last_mode = O.mode;
              O.mode = FILE_DISPLAY; // needs to appear before editorDisplayFile
              outlineSetMessage("Testing synching local db and server and displaying results");
              //outlineRefreshScreen(); ///////////////////////////////////////////////////////////////
              editorReadFile("log");
              editorDisplayFile();//put them in the command mode case synch
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
              O.mode = O.last_mode;
              outlineSetMessage("Marks all deleted");
              return;

            case C_dbase:
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
                outlineSetMessage("Saved outline to %s", fname.c_str());
              } else {
                outlineSetMessage("You didn't provide a file name!");
              }
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
                outlineSetMessage("No db write since last change");
           
              } else {
                write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
                write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
                Py_FinalizeEx();
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
              initial_file_row = 0;
              O.last_mode = O.mode;
              O.mode = FILE_DISPLAY;
              outlineSetMessage("Displaying help file");
              //outlineRefreshScreen();///////////////////////////////////////////////////////////////////////
              editorReadFile("listmanager_commands");
              editorDisplayFile();
              return;

            default: // default for commandfromstring

              //\x1b[41m => red background
              outlineSetMessage("\x1b[41mNot an outline command: %s\x1b[0m", O.command_line.c_str());
              O.mode = NORMAL;
              return;

          } //end of commandfromstring switch within '\r' of case COMMAND_LINE

        default: //default for switch 'c' in case COMMAND_LINE
          if (c == DEL_KEY || c == BACKSPACE)
            O.command_line.pop_back();
          else
            O.command_line.push_back(c);

          outlineSetMessage(":%s", O.command_line.c_str());

        } // end of 'c' switch within case COMMAND_LINE

      return; //end of outer case COMMAND_LINE

    // note database mode always deals with current character regardless of previously typed char
    // since all commands are one char.
    case DATABASE:
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
        case 'h':
        case 'l':
          outlineMoveCursor(c);
          return;

        case ARROW_RIGHT:
        case '\x1b':
          O.fc = 0; //otherwise END in DATABASE mode could have done bad things
          O.mode = NORMAL;
          get_note(O.rows.at(O.fr).id); //only needed if previous comand was 'i'
          outlineSetMessage("");
          return;

        case ARROW_LEFT:
          if (O.mode == DATABASE && O.taskview == BY_SEARCH) {
            O.mode = SEARCH;
            outlineSetMessage("You are now in SEARCH mode");
          } else if (O.mode == SEARCH) {
            O.mode = DATABASE;
            outlineSetMessage("You are now in DATABASE mode");
          } else {
            outlineSetMessage("There is no active search!");
          }
          return;

        case '0':
        case HOME_KEY:
          O.fc = 0;
          return;

        case END_KEY:
        case '$':
          {
          O.fc = O.rows.at(O.fr).title.size();
          return;
          }

        case ':':
          outlineSetMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          O.last_mode = O.mode;
          O.mode = COMMAND_LINE;
          return;

        case 'x':
          if (O.view == TASK) toggle_completed();
          return;

        /*
        case 'c': // show contexts
          editorEraseScreen(); //erase note if there is one
          O.view = CONTEXT;
          get_containers();
          O.mode = NORMAL;
          outlineSetMessage("Retrieved contexts");
          return;

        case 'f':
          editorEraseScreen(); //erase note if there is one
          O.view = FOLDER;
          get_containers();
          O.mode = NORMAL;
          outlineSetMessage("Retrieved folders");
          return;

        case 'y':
          editorEraseScreen(); //erase note if there is one
          O.view = KEYWORD;
          get_keywords();
          O.mode = NORMAL;
          outlineSetMessage("Retrieved keywords");
          return;
        */

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
          outlineSetMessage("Toggle mark for item %d", O.rows.at(O.fr).id);
          }
          return;

        case 's':
          if (O.view == TASK) {
            O.show_deleted = !O.show_deleted;
            O.show_completed = !O.show_completed;
            if (O.taskview == BY_SEARCH)
              search_db();
            else
              get_items(MAX);
          }
          outlineSetMessage((O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
          return;

        case 'r':
          if (O.view == TASK) {
            outlineSetMessage("Tasks will be refreshed");
            if (O.taskview == BY_SEARCH)
              search_db();
             else
              get_items(MAX);
          } else {
            outlineSetMessage("contexts will be refreshed");
            get_containers();
          }
          return;
  
        case 'i': //display item info
          display_item_info(O.rows.at(O.fr).id);
          return;
  
        case 'v': //render in browser
          
          view_html(O.rows.at(O.fr).id);

          /* when I switched from chrome to qutebrowser I thought I was not
          getting error/status messages to stdout but they do appear to 
          occur sometimes and so may need to do a clear screen
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */
          return;

        default:
          if (c < 33 || c > 127) outlineSetMessage("<%d> doesn't do anything in DATABASE/SEARCH mode", c);
          else outlineSetMessage("<%c> doesn't do anything in DATABASE/SEARCH mode", c);
          return;
      } // end of switch(c) in case DATABASLE

      //return; //end of outer case DATABASE //won't be executed

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
          outlineSetMessage("");
          return;
  
        case 'y':  
          O.repeat = O.highlight[1] - O.highlight[0] + 1;
          O.fc = O.highlight[0];
          outlineYankString();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineSetMessage("");
          return;
  
        case '\x1b':
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      //return; //end of case VISUAL (return here would not be executed)

    case REPLACE: 
      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar();
        outlineInsertChar(c);
      }
      O.repeat = 0;
      O.command[0] = '\0';
      O.mode = 0;

      return; //////// end of outer case REPLACE

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
          outlineSetMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          //O.last_mode was set when entering file mode
          O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          O.mode = O.last_mode;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("");
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
                  outlineSetMessage("Problem converting c variable for use in calling python function");
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
              outlineSetMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      outlineSetMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  if (report_only) outlineSetMessage("Number of tasks/items that would be affected is %d", num);
  else outlineSetMessage("Number of tasks/items that were affected is %d", num);
}

void display_item_info_pg(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn)); 
    PQclear(res);
    return;
  }    

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

  //set background color to blue
  ab.append("\x1b[44m", 5);

  char str[300];
  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", PQgetvalue(res, 0, 0));
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", PQgetvalue(res, 0, 3));
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int context_tid = atoi(PQgetvalue(res, 0, 6));
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works

  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  //int folder_tid = atoi(argv[5]);
  int folder_tid = atoi(PQgetvalue(res, 0, 5));
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mfolder:\x1b[0;44m %s", it2->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (*PQgetvalue(res, 0, 8) == 't') ? "true" : "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 14) == 't') ? "true" : "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 10)) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", PQgetvalue(res, 0, 16));
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", PQgetvalue(res, 0, 9));
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  //ab.append("\x1b[0m", 4);

  ///////////////////////////

  get_task_keywords_pg();
  std::string delim = "";
  std::string s;
  for (const auto &kw : task_keywords) {
    s += delim += kw;
    delim = ",";
  }
  sprintf(str,"\x1b[1mkeywords:\x1b[0;44m %s", s.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"\x1b[1mtag:\x1b[0;44m %s", PQgetvalue(res, 0, 4));
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);


  ///////////////////////////


  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  PQclear(res);
}

void fts5_sqlite(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            //<< search_terms << "' ORDER BY rank";
            << search_terms << "' ORDER BY rank LIMIT " << 50;

  sqlite3 *db;
  char *err_msg = nullptr;
    
  int rc = sqlite3_open(FTS_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  fts_ids.clear();
  fts_titles.clear();
  fts_counter = 0;

  bool no_rows = true;
  rc = sqlite3_exec(db, fts_query.str().c_str(), fts5_callback, &no_rows, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } 
  sqlite3_close(db);

  if (no_rows) {
    outlineSetMessage("No results were returned");
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

  get_items_by_id_sqlite(query);

  //outlineSetMessage(query.str().c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
  //outlineSetMessage(search_terms.c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
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

void display_item_info_sqlite(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  
  rc = sqlite3_exec(db, query.str().c_str(), display_item_info_callback, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } 
  sqlite3_close(db);
}

int display_item_info_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    
  UNUSED(NotUsed);
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

  //set background color to blue
  ab.append("\x1b[44m", 5);

  char str[300];
  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mtid:\x1b[0;44m %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", argv[3]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int context_tid = atoi(argv[6]);
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int folder_tid = atoi(argv[5]);
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mfolder:\x1b[0;44m %s", it2->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (atoi(argv[8]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (atoi(argv[14]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (argv[10]) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", argv[16]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  //ab.append("\x1b[0m", 4);

  ///////////////////////////

  get_task_keywords_sqlite();
  std::string delim = "";
  std::string s;
  for (const auto &kw : task_keywords) {
    s += delim += kw;
    delim = ",";
  }
  sprintf(str,"\x1b[1mkeywords:\x1b[0;44m %s", s.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"\x1b[1mtag:\x1b[0;44m %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);


  ///////////////////////////


  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

void update_note_pg(void) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  //int len;
  std::string text = editorRowsToString();
  size_t pos = text.find("'");
  while(pos != std::string::npos)
    {
      text.replace(pos, 1, "''");
      pos = text.find("'", pos + 2);
    }

  int id = get_id();

  std::stringstream query;

  query  << "UPDATE task SET note='" << text << "', "
         << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    editorSetMessage(PQerrorMessage(conn));
  } else {
    outlineSetMessage("Updated note for item %d", id);
    outlineRefreshScreen();
    //editorSetMessage("Note update succeeeded"); 
    /**************** need to update modified in orow row->strncpy (Some C function) ************************/
  }
  
  PQclear(res);
  //do_exit(conn);
  E.dirty = 0;

  outlineSetMessage("Updated %d", id);

  return;
}

void update_note_sqlite(void) {

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
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Updated note for item %d", id);
    outlineRefreshScreen();
  }

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  rc = sqlite3_open(FTS_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  std::stringstream query2;
  //query.clear(); //this clear clears eof and fail flags so query.str(std::string());query.clear()
  query2 << "Update fts SET note='" << text << "' WHERE lm_id=" << id;

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL fts error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Updated note and fts entry for item %d", id);
    outlineRefreshScreen();
    editorSetMessage("Note update succeeeded"); 
  }
   
  sqlite3_close(db);

  E.dirty = 0;
}

void update_task_context_pg(std::string &new_context, int id) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
      outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
    }
  }

  std::stringstream query;
  //int id = get_id();
  int context_tid = context_map.at(new_context);

  query << "UPDATE task SET context_tid=" << context_tid << ", "
        << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' "
        << "WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
  } else {
    outlineSetMessage("Setting context to %s succeeded", new_context.c_str());
  }
  PQclear(res);
}

void update_task_context_sqlite(std::string &new_context, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int context_tid = context_map.at(new_context);
  query << "UPDATE task SET context_tid=" << context_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Setting context to %s succeeded", new_context.c_str());
  }

  sqlite3_close(db);
}

void update_task_folder_pg(std::string& new_folder, int id) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
      outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
    }
  }

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int folder_tid = folder_map.at(new_folder);

  query << "UPDATE task SET folder_tid=" << folder_tid << ", "
        << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' "
        << "WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
  } else {
    outlineSetMessage("Setting folder to %s succeeded", new_folder.c_str());
  }
  PQclear(res);
}

void update_task_folder_sqlite(std::string& new_folder, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int folder_tid = folder_map.at(new_folder);
  query << "UPDATE task SET folder_tid=" << folder_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Setting folder to %s succeeded", new_folder.c_str());
  }

  sqlite3_close(db);
}

void toggle_completed_pg(void) {

  orow& row = O.rows.at(O.fr);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  std::stringstream query;
  int id = get_id();
  query << "UPDATE task SET completed=" << ((row.completed) ? "NULL" : "CURRENT_DATE") << ", "
        << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' "
        <<  "WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle completed failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle completed succeeded");
    row.completed = !row.completed;
  }
  PQclear(res);
}

void toggle_completed_sqlite(void) {

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
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle completed succeeded");
    row.completed = !row.completed;
  }

  sqlite3_close(db);
    
}

void toggle_deleted_pg(void) {

  orow& row = O.rows.at(O.fr);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  std::stringstream query;
  int id = get_id();
  std::string table = (O.view == TASK) ? "task" : ((O.view == CONTEXT) ? "context" : "folder");

  query << "UPDATE " << table << " SET deleted=" << ((row.deleted) ? "False" : "True") << ", "
        << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle deleted failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle deleted succeeded");
    row.deleted = !row.deleted;
  }
  PQclear(res);
  return;
}

void touch_pg(void) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle deleted failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("'Touch' succeeded");
  }
  PQclear(res);
  return;
}


void touch_sqlite(void) {

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("'Touch' succeeded");
  }

  sqlite3_close(db);

}

void toggle_deleted_sqlite(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table = (O.view == TASK) ? "task" : ((O.view == CONTEXT) ? "context" : "folder");

  query << "UPDATE " << table << " SET deleted=" << ((row.deleted) ? "False" : "True") << ", "
        <<  "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  // ? whether should move all tasks out here or in sync
  // UPDATE task SET folder_tid = 1 WHERE folder_tid = 4;
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle deleted succeeded");
    row.deleted = !row.deleted;
  }

  sqlite3_close(db);

}

void toggle_star_pg(void) {

  orow& row = O.rows.at(O.fr);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  std::stringstream query;
  int id = get_id();
  std::string table = (O.view == TASK) ? "task" : ((O.view == CONTEXT) ? "context" : "folder");
  std::string column= (O.view == TASK) ? "star" : ((O.view == CONTEXT) ? "\"default\"" : "private");


  query << "UPDATE " << table << " SET " << column << "=" << ((row.star) ? "FALSE" : "TRUE") << ", "
        << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle star failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle star succeeded");
    row.star = !row.star;
  }
  PQclear(res);
  return;
}

void toggle_star_sqlite(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table = (O.view == TASK) ? "task" : ((O.view == CONTEXT) ? "context" : "folder");
  std::string column= (O.view == TASK) ? "star" : ((O.view == CONTEXT) ? "\"default\"" : "private");

  query << "UPDATE " << table << " SET " << column << "=" << ((row.star) ? "False" : "True") << ", "
        << "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle star succeeded");
    row.star = !row.star;
  }
  sqlite3_close(db);
}

void update_row_pg(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineSetMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {

    if (PQstatus(conn) != CONNECTION_OK){
      if (PQstatus(conn) == CONNECTION_BAD) {
          
          fprintf(stderr, "Connection to database failed: %s",
              PQerrorMessage(conn));
          do_exit(conn);
      }
    }

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

    std::stringstream query;
    query << "UPDATE task SET title='" << title << "', modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << row.id;

    PGresult *res = PQexec(conn, query.str().c_str());
      
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      outlineSetMessage(PQerrorMessage(conn));
    } else {
      row.dirty = false;
      outlineSetMessage("Successfully update row %d", row.id);
    }  

    PQclear(res);

  } else { 
    insert_row_pg(row);
  }  
}

void update_container_pg(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineSetMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {

    if (PQstatus(conn) != CONNECTION_OK){
      if (PQstatus(conn) == CONNECTION_BAD) {

          fprintf(stderr, "Connection to database failed: %s\n",
              PQerrorMessage(conn));
          do_exit(conn);
      }
    }

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
          << " SET title='" << title << "', modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << row.id;

    PGresult *res = PQexec(conn, query.str().c_str());

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      outlineSetMessage(PQerrorMessage(conn));
    } else {
      row.dirty = false;
      outlineSetMessage("Successfully update row %d", row.id);
    }

    PQclear(res);

  } else {
    insert_container_pg(row);
  }
}

void update_row_sqlite(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineSetMessage("Row has not been changed");
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
      outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineSetMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);
    
    /**************fts virtual table update**********************/

    rc = sqlite3_open(FTS_DB.c_str(), &db);
    if (rc != SQLITE_OK) {
          
      outlineSetMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    std::stringstream query2;
    query2 << "UPDATE fts SET title='" << title << "' WHERE lm_id=" << row.id;
    rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);
      
    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
      } else {
        outlineSetMessage("Updated title and fts entry for item %d", row.id);
      }
  
      sqlite3_close(db);

  } else { //row.id == -1
    insert_row_sqlite(row);
  }
}

void update_container_sqlite(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineSetMessage("Row has not been changed");
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
      outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }

    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineSetMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);

  } else { //row.id == -1
    insert_container_sqlite(row);
  }
}

int insert_row_pg(orow& row) {

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
        << ((O.context == "search" || O.context == "recent" || O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << " True," //star
        << "CURRENT_DATE," //added
        << "'<This is a new note from sqlite>'," //note
        << " FALSE," //deleted
        << " LOCALTIMESTAMP - interval '" << TZ_OFFSET << "hours'," //created
        << " LOCALTIMESTAMP - interval '" << TZ_OFFSET << "hours'," //modified
        << " CURRENT_DATE" //startdate
        << ") RETURNING id;";

  /*
    not used:
    tid
    tag
    duetime
    completed
    duedate
    repeat
    remind
  */

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) { //PGRES_TUPLES_OK is for query that returns data
    outlineSetMessage("PQerrorMessage: %s", PQerrorMessage(conn)); //often same message - one below is on the problematic result
    //outlineSetMessage("PQresultErrorMessage: %s", PQresultErrorMessage(res));
    PQclear(res);
    return -1;
  }

  row.id = atoi(PQgetvalue(res, 0, 0));
  row.dirty = false;

  PQclear(res);
  outlineSetMessage("Successfully inserted new row with id %d", row.id);

  return row.id;
}

int insert_container_pg(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;

  query << "INSERT INTO "
        << ((O.view == CONTEXT) ? "context" : "folder")
        << " ("
        << "title, "
        << "deleted, "
        << "created, "
        << "modified, "
        << "tid, "
        << ((O.view == CONTEXT) ? "\"default\", " : "private, ") //context -> default; folder -> private
        << "textcolor "
        << ") VALUES ("
        << "'" << title << "'," //title
        << " False," //deleted
        << " LOCALTIMESTAMP - interval '" << TZ_OFFSET << "hours'" //created
        << " LOCALTIMESTAMP - interval '" << TZ_OFFSET << "hours'" //modified
        << " 100," //tid
        << " False," //default for context and private for folder
        << " 10" //textcolor
        << ") RETURNING id;";

  /*
     not used:
     tid,
     icon (varchar 32)
     image (blob)
   */

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) { //PGRES_TUPLES_OK is for query that returns data
    outlineSetMessage("PQerrorMessage: %s", PQerrorMessage(conn)); //often same message - one below is on the problematic result
    //outlineSetMessage("PQresultErrorMessage: %s", PQresultErrorMessage(res));
    PQclear(res);
    return -1;
  }

  row.id = atoi(PQgetvalue(res, 0, 0));
  row.dirty = false;

  PQclear(res);
  outlineSetMessage("Successfully inserted new context with id %d", row.id);

  return row.id;
}

int insert_row_sqlite(orow& row) {

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
        << ((O.context == "search" || O.context == "recent" || O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << " True," //star
        << "date()," //added
        << "'<This is a new note from sqlite>'," //note
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

    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  std::stringstream query2;
  query2 << "INSERT INTO fts (title, lm_id) VALUES ('" << title << "', " << row.id << ")";

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open FTS database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return row.id;
  }

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error doing FTS insert: %s", err_msg);
    sqlite3_free(err_msg);
    return row.id; // would mean regular insert succeeded and fts failed - need to fix this
  }
  sqlite3_close(db);
  outlineSetMessage("Successfully inserted new row with id %d and indexed it", row.id);

  return row.id;
}

int insert_container_sqlite(orow& row) {

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
        << " 100," //tid
        << " False," //default for context and private for folder
        << " 10" //textcolor
        << ");"; // RETURNING id;",

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

    outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  outlineSetMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

void update_rows_pg(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
      outlineSetMessage(PQerrorMessage(conn));
    }
  }

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

      query << "UPDATE task SET title='" << title << "', "
            << "modified=LOCALTIMESTAMP - interval '" << TZ_OFFSET << " hours' WHERE id=" << row.id;

      PGresult *res = PQexec(conn, query.str().c_str());
  
      if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        outlineSetMessage(PQerrorMessage(conn));
        PQclear(res);
        return;
      } else {
        row.dirty = false;
        updated_rows[n] = row.id;
        n++;
        PQclear(res);
      }  
    } else { 
      int id  = insert_row_pg(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineSetMessage("There were no rows to update");
    return;
  }

  outlineSetMessage("Rows successfully updated ... %d", sizeof(updated_rows));
  
  outlineSetMessage("Rows successfully updated ... ");
  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  outlineSetMessage("%s",  msg);
}

void update_rows_sqlite(void) {
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
            
        outlineSetMessage("Cannot open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
        }
    
      rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

      if (rc != SQLITE_OK ) {
        outlineSetMessage("SQL error: %s", err_msg);
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
      int id  = insert_row_sqlite(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineSetMessage("There were no rows to update");
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
  outlineSetMessage("%s",  msg);
}

int get_id(void) { //default is in prototype but should have no default at all
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
  //outlineSetMessage("i = %d, j = %d", i, j ); 
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
  // below is same is outlineMoveEndWord2
  int j;
  orow& row = O.rows.at(O.fr);

  for (j = O.fc + 1; j < row.title.size(); j++) {
    if (row.title[j] < 48) break;
  }

  O.fc = j - 1;
  // end outlineMoveEndWord2

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

  outlineSetMessage("word under cursor: <%s>", search_string.c_str());

}

void outlineFindNextWord() {

  int y, x;
  char *z;
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

    outlineSetMessage("x = %d; y = %d", x, y); 
}

void editorScroll(void) {

  if (E.rows.empty()) {
    E.cx = E.cy = E.fr = E.fc = 0;
    return;
  }

  //initially added the below to C_dd but probably should be here:
  if (E.fr >= E.rows.size()) E.fr = E.rows.size() - 1;

  // This check used to be in editorMoveCursor but there are other ways like
  // issuring 'd$' that will shorten the row but cursor will be beyond the line
  int row_size = E.rows.at(E.fr).size();
  if (E.fc >= row_size) E.fc = row_size - (E.mode != INSERT); 

  if (E.fc < 0) E.fc = 0;

  E.cx = editorGetScreenXFromRowCol(E.fr, E.fc);
  int cy = editorGetScreenYFromRowColWW(E.fr, E.fc); // this may be the problem with zero char rows

  if (cy > E.screenlines + E.line_offset - 1) {
    E.line_offset =  cy - E.screenlines + 1;
  }

  if (cy < E.line_offset) {
    E.line_offset =  cy;
  }
  
  E.cy = cy - E.line_offset;

  // vim seems to want full rows to be displayed although I am not sure
  // it's either helpful or worthit but this is a placeholder for the idea
}

void editorDrawRows(std::string& ab) {
  if (E.rows.empty()) {
      editorEraseScreen(); //erases the note area since E.rows is empty
      return;
  }
  //appears that it erases old text to the right of text as it goes.
  int y = 0;
  int len; //, n;
  int j,k; // to swap E.highlitgh[0] and E.highlight[1] if necessary
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);

  // this is the first visible row on the screen given E.line_offset
  // seems like it will always then display all top row's lines
  int filerow = editorGetFileRowByLineWW(0);

  for (;;){
    if (y >= E.screenlines) break; //somehow making this >= made all the difference

    // if there is still screen real estate but we've run out of text rows (E.numrows)
    //drawing '~' below: first escape is red color (31m) and K erases rest of line
    if (filerow > E.rows.size() - 1) {

      //may not be worth this if else to not draw ~ in first row
      //and probably there is a better way to do it
      if (y) ab.append("\x1b[31m~\x1b[K", 9);
      else ab.append("\x1b[K", 3);
      ab.append(lf_ret, nchars);
      y++;

    // this else is the main drawing code
    } else {

      int lines = editorGetLinesInRowWW(filerow); // changed from E.fr to filerow 04012019

      // below is trying to emulate what vim does when it can't show an entire row (because it generates
      // multiple screen lines that can't all be displayed at the bottom of the screen because of where
      // the screen scroll is.)  It shows nothing of the row rather than show a partial row.  May not be worth it.
      // And *may* actually be causing problems

      if ((y + lines) > E.screenlines) {
          for (int n = 0; n < (E.screenlines - y); n++) {
            ab.append("@@", 2); //??????
            ab.append("\x1b[K", 3);
            ab.append(lf_ret, nchars);
          }
        break;
      }

    //erow *row = &E.row[filerow];
    std::string& row = E.rows.at(filerow);
    char *start,*right_margin;
    int left, width;
    bool more_lines = true;

    left = row.size(); //although maybe time to use strlen(preamble);
    start = &row[0]; //char * to string that is going to be wrapped
    width = E.screencols; //wrapping width

    while(more_lines) {

        if (left <= width) {//after creating whatever number of lines if remainer <= width: get out
          len = left;
          more_lines = false;
        } else {
          // each time start pointer moves you are adding the width to it and checking for spaces
          right_margin = start+width - 1;

          if (right_margin > &row[0] + row.size() - 1) right_margin = &row[0] + row.size() - 1; //02272019
          while(!isspace(*right_margin)) { //#2
            right_margin--;
            if( right_margin == start) {// situation in which there were no spaces to break the link
              right_margin += width - 1;
              break;
            }

          } //end #2

          len = right_margin - start + 1;
          left -= right_margin-start+1;      /* +1 for the space */
          //start = [see below]
        }
        y++;

        if (E.mode == VISUAL_LINE) {

          // below in case E.highlight[1] < E.highlight[0]
          k = (E.highlight[1] > E.highlight[0]) ? 1 : 0;
          j =!k;

          if (filerow >= E.highlight[j] && filerow <= E.highlight[k]) {
            ab.append("\x1b[48;5;242m", 11);
            ab.append(start, len);
            ab.append("\x1b[49m", 5); //return background to normal
          } else ab.append(start, len);

        } else if (E.mode == VISUAL && filerow == E.fr) {

          // below in case E.highlight[1] < E.highlight[0]
          k = (E.highlight[1] > E.highlight[0]) ? 1 : 0;
          j =!k;

          char *Ehj = &(row[E.highlight[j]]);
          char *Ehk = &(row[E.highlight[k]]);

          if ((Ehj >= start) && (Ehj < start + len)) {
            ab.append(start, Ehj - start);
            ab.append("\x1b[48;5;242m", 11);
            if ((Ehk - start) > len) {
              ab.append(Ehj, len - (Ehj - start));
              ab.append("\x1b[49m", 5); //return background to normal
            } else {
              ab.append(Ehj, E.highlight[k] - E.highlight[j]);
              ab.append("\x1b[49m", 5); //return background to normal
              ab.append(Ehk, start + len - Ehk);
            }
          } else if ((Ehj < start) && (Ehk > start)) {
              ab.append("\x1b[48;5;242m", 11);

            if ((Ehk - start) > len) {
              ab.append(start, len);
              ab.append("\x1b[49m", 5); //return background to normal
            } else {
              ab.append(start, Ehk - start);
              ab.append("\x1b[49m", 5); //return background to normal
              ab.append(Ehk, start + len - Ehk);
            }

          } else ab.append(start, len);
        } else ab.append(start, len);

      // "\x1b[K" erases the part of the line to the right of the cursor in case the
      // new line i shorter than the old
      ab.append("\x1b[K", 3);

      ab.append(lf_ret, nchars);
      ab.append("\x1b[0m", 4); //slz return background to normal

      start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
      }

      filerow++;
    } // end of main drawing else block
   ab.append("\x1b[0m", 4); //slz return background to normal
  } // end of top for loop
}

//status bar has inverted colors

/*****************************************/
void editorDrawStatusBar(std::string& ab) {
  int len;
  char status[100];
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

    len = snprintf(status,
                   sizeof(status), "E.fr(0)=%d line(1)=%d E.fc(0)=%d line chrs(1)="
                                   "%d E.cx(0)=%d E.cy(0)=%d E.scols(1)=%d",
                                   E.fr, line, E.fc, line_char_count, E.cx, E.cy, E.screencols);
  } else {
    len =  snprintf(status, sizeof(status), "E.row is NULL E.cx = %d E.cy = %d  E.numrows = %d E.line_offset = %d",
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

void editorDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  buf  << "\x1b[" << E.screenlines + TOP_MARGIN + 2 << ";" << EDITOR_LEFT_MARGIN << "H";
  ab += buf.str();
  ab += "\x1b[K"; // will erase midscreen -> R; cursor doesn't move after erase
  int msglen = strlen(E.message);
  if (msglen > E.screencols) msglen = E.screencols;
  ab.append(E.message, msglen);
}

// called by get_note (and others)
void editorRefreshScreen(void) {
  char buf[32];

  if (DEBUG) {
    if (!E.rows.empty()){
      int screenx = editorGetScreenXFromRowCol(E.fr, E.fc);
      int line = editorGetLineInRowWW(E.fr, E.fc);
      int line_char_count = editorGetLineCharCountWW(E.fr, line);

      editorSetMessage("row(0)=%d line(1)=%d char(0)=%d line-char-count=%d screenx(0)=%d, E.screencols=%d", E.fr, line, E.fc, line_char_count, screenx, E.screencols);
    } else
      editorSetMessage("E.row is NULL, E.cx = %d, E.cy = %d,  E.numrows = %d, E.line_offset = %d", E.cx, E.cy, E.rows.size(), E.line_offset);
  }

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); //03022019 added len
  ab.append(buf, strlen(buf));

  editorDrawRows(ab);
  editorDrawStatusBar(ab);
  editorDrawMessageBar(ab);

  // the lines below position the cursor where it should go
  if (E.mode != COMMAND_LINE){
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + TOP_MARGIN + 1, E.cx + EDITOR_LEFT_MARGIN + 1); //03022019
    ab.append(buf, strlen(buf));
  }

  if (E.dirty == 1) {
    //The below needs to be in a function that takes the color as a parameter
    write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode

    for (int k = OUTLINE_LEFT_MARGIN + O.screencols + 1; k < screencols ;k++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
      write(STDOUT_FILENO, buf, strlen(buf));
      write(STDOUT_FILENO, "\x1b[31mq", 6); //horizontal line
    }
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, screencols/2); // added len 03022019
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[31mw", 6); //'T' corner
    write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
    write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
  }

  ab.append("\x1b[?25h", 6); //shows the cursor

  write(STDOUT_FILENO, ab.c_str(), ab.size());
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

// calls readKey()
void editorProcessKeypress(void) {
  int i, start, end, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  int c = readKey();

  switch (E.mode) {

    case INSERT:

      switch (c) {
    
        case '\r':
          editorCreateSnapshot();
          editorInsertReturn();
          break;
    
        case CTRL_KEY('s'):
          editorSave();
          break;

        case HOME_KEY:
          editorMoveCursorBOL();
          break;
    
        case END_KEY:
          editorMoveCursorEOL();
          editorMoveCursor(ARROW_RIGHT);
          break;
    
        case BACKSPACE:
          editorCreateSnapshot();
          editorBackspace();
          break;
    
        case DEL_KEY:
          editorCreateSnapshot();
          editorDelChar();
          break;
    
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          editorMoveCursor(c);
          break;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);
          break;
    
        case CTRL_KEY('z'):
          E.smartindent = (E.smartindent == 1) ? 0 : 1;
          editorSetMessage("E.smartindent = %d", E.smartindent); 
          break;
    
        case '\x1b':
          if (E.mode == NORMAL) return;
          E.mode = NORMAL;
          E.continuation = 0; // right now used by backspace in multi-line filerow
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
          return;
    
        default:
          editorCreateSnapshot();
          editorInsertChar(c);
          return;
     
      } //end inner switch for outer case INSERT

      return;

    case NORMAL: 
 
        if (c == '\x1b') {
          E.command[0] = '\0';
          E.repeat = 0;
          return;
        }  
      /*leading digit is a multiplier*/
      if (isdigit(c)) { //equiv to if (c > 47 && c < 58) 
        if ( E.repeat == 0 ){
    
          //if c=48=>0 then it falls through to move to beginning of line
          if ( c != 48 ) { 
            E.repeat = c - 48;
            return;
          }  
    
        } else { 
          E.repeat = E.repeat*10 + c - 48;
          return;
        }
      }
    
      if ( E.repeat == 0 ) E.repeat = 1;
    
      {
      int n = strlen(E.command);
      E.command[n] = c;
      E.command[n+1] = '\0';
      command = (n && c < 128) ? keyfromstringcpp(E.command) : c;
      }

      //if (commmand == 'x') editornormal_x();

      switch (command) {
    
        case SHIFT_TAB:
          editor_mode = false;
          E.fc = E.fr = 0;
          return;
    
        case 'i':
          E.mode = INSERT;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 's':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelChar();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
          return;
    
        case 'x':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelChar();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
        
        case 'r':
          E.mode = 5;
          E.command[0] = '\0';
          return;
    
        case '~':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorChangeCase();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'a':
          E.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
          editorMoveCursor(ARROW_RIGHT);
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'A':
          editorMoveCursorEOL();
          E.mode = INSERT; //needs to be here for movecursor to work at EOLs
          editorMoveCursor(ARROW_RIGHT);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'w':
          editorMoveNextWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'b':
          editorMoveBeginningWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'e':
          editorMoveEndWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '0':
          editorMoveCursorBOL();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '$':
          editorMoveCursorEOL();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'I':
          editorMoveCursorBOL();
          E.fc = editorIndentAmount(E.fr);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'o':
          editorCreateSnapshot();
          editorInsertNewline(1);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'O':
          editorCreateSnapshot();
          editorInsertNewline(0);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'G':
          E.fc = 0;
          E.fr = E.rows.size() - 1;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
      
        case ':':
          E.mode = COMMAND_LINE;
          E.command[0] = ':';
          E.command[1] = '\0';
          editorSetMessage(":"); 
          return;
    
        case 'V':
          E.mode = VISUAL_LINE;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fr;
          editorSetMessage("\x1b[1m-- VISUAL LINE --\x1b[0m");
          return;
    
        case 'v':
          E.mode = VISUAL;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fc;
          editorSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
          return;
    
        case 'p':  
          editorCreateSnapshot();
          if (!string_buffer.empty()) editorPasteString();
          else editorPasteLine();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '*':  
          getWordUnderCursor();
          editorFindNextWord();
          E.command[0] = '\0';
          return;
    
        case 'n':
          editorFindNextWord();
          E.command[0] = '\0';
          return;
    
        case 'u':
          editorRestoreSnapshot();
          return;
    
        case '^':
          view_html(O.rows.at(O.fr).id);

          /*
          not getting error messages with qutebrowser
          so below not necessary (for the moment)
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */

          outlineRefreshScreen(); //to get outline message updated (could just update that last row??)
          O.command[0] = '\0';//}
          return;

        case CTRL_KEY('z'):

          E.smartindent = (E.smartindent == 4) ? 0 : 4;
          editorSetMessage("E.smartindent = %d", E.smartindent);
          return;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);
          return;


        case CTRL_KEY('s'):
          editorSave();
          return;

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
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
        /*
        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            E.fr -= E.screenlines;
            if (E.fr < 0) E.fr = 0;
          } else if (c == PAGE_DOWN) {
            E.fr += E.screenlines;
            if (E.fr > E.rows.size() - 1) E.fr = E.rows.size() - 1;
          }
          return;
        */

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          E.repeat = 0;
          return;
    
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          return;
    
        case C_daw:
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_dw:
          editorCreateSnapshot();
          for (int j = 0; j < E.repeat; j++) {
            start = E.fc;
            editorMoveEndWord2();
            end = E.fc;
            E.fc = start;
            for (int j = 0; j < end - start + 2; j++) editorDelChar();
          }
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_de:
          editorCreateSnapshot();
          start = E.fc;
          editorMoveEndWord(); //correct one to use to emulate vim
          end = E.fc;
          E.fc = start; 
          for (int j = 0; j < end - start + 1; j++) editorDelChar();
          //E.fc = (start < E.rows.at(E.cy).size()) ? start : E.rows.at(E.cy).size() -1;
          // below 11-26-2019
          E.fc = (start < E.rows.at(E.fr).size()) ? start : E.rows.at(E.fr).size() -1;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_dd:
          if (!E.rows.empty()) {
            int r = E.rows.size() - E.fr;
            E.repeat = (r >= E.repeat) ? E.repeat : r ;
            editorCreateSnapshot();
            editorYankLine(E.repeat);
            for (int i = 0; i < E.repeat ; i++) editorDelRow(E.fr);
          }
          E.fc = 0;
          //trying this in editorScroll
          //if (E.fr >= E.rows.size()) E.fr = E.rows.size() - 1; //11-26-2019
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_d$:
          editorCreateSnapshot();
          editorDeleteToEndOfLine();
          if (!E.rows.empty()) {
            int r = E.rows.size() - E.fr;
            E.repeat--;
            E.repeat = (r >= E.repeat) ? E.repeat : r ;
            //editorYankLine(E.repeat); //b/o 2 step won't really work right
            for (int i = 0; i < E.repeat ; i++) editorDelRow(E.fr);
          }
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        //tested with repeat on one line
        case C_cw:
          editorCreateSnapshot();
          for (int j = 0; j < E.repeat; j++) {
            start = E.fc;
            editorMoveEndWord();
            end = E.fc;
            E.fc = start;
            for (int j = 0; j < end - start + 1; j++) editorDelChar();
          }
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        //tested with repeat on one line
        case C_caw:
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelWord();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        //indent and unindent changed to E.fr from E.cy on 11-26-2019
        case C_indent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) { //i defined earlier - need outside block
            editorIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_unindent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) { //i defined earlier - need outside block
            editorUnIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_gg:
         E.fc = E.line_offset = 0;
         E.fr = E.repeat-1;
         E.command[0] = '\0';
         E.repeat = 0;
         return;
    
       case C_yy:  
         editorYankLine(E.repeat);
         E.command[0] = '\0';
         E.repeat = 0;
         return;
    
       default:
          // if a single char or sequence of chars doesn't match then
          // do nothing - the next char may generate a match
          return;
    
      } // end of keyfromstring switch under case NORMAL 

      return; // end of case NORMAL (don't think it can be reached)

    case COMMAND_LINE:

      switch (c) {

        case '\x1b':

          E.mode = 0;
          E.command[0] = '\0';
          editorSetMessage(""); 

          return;
  
        case '\r':
          // probably easier to maintain if this was same as case '\r'
          // in outline mode/COMMAND_LINE mode/case '\r'
          // but with only single char commands except q!
          // probably not worth it
          switch (E.command[1]) {

            case 'w':
              update_note();
              E.mode = NORMAL;
              E.command[0] = '\0';
              editorSetMessage("");
              editorRefreshScreen();

              //The below needs to be in a function that takes the color as a parameter
              {
              char buf[32];
              write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
          
              for (int k=OUTLINE_LEFT_MARGIN+O.screencols+1; k < screencols ;k++) {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
              }
              snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, O.screencols + OUTLINE_LEFT_MARGIN + 1);
              write(STDOUT_FILENO, buf, strlen(buf));
              write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner
              write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
              write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
              }
              return;
  
            case 'x':
              update_note();
              E.mode = NORMAL;
              E.command[0] = '\0';
              editor_mode = false;
              editorSetMessage("");
              editorRefreshScreen();

              //The below needs to be in a function that takes the color as a parameter
              {
              char buf[32];
              write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
          
              for (int k=OUTLINE_LEFT_MARGIN+O.screencols+1; k < screencols ;k++) {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
              }
              snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, O.screencols + OUTLINE_LEFT_MARGIN + 1);
              write(STDOUT_FILENO, buf, strlen(buf));
              write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner
              write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
              write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
              }
              return;
  
            case 'q':
              if (E.dirty) {
                if (strlen(E.command) == 3 && E.command[2] == '!') {
                  E.mode = NORMAL;
                  E.command[0] = '\0';
                  editor_mode = false;
                } else {
                  E.mode = NORMAL;
                  E.command[0] = '\0';
                  editorSetMessage("No write since last change");
                }
              } else {
                editorSetMessage("");
                E.fr = E.fc = E.cy = E.cx = E.line_offset = 0; //added 11-26-2019 but may not be necessary having restored this in get_note.
                editor_mode = false;
              }
              editorRefreshScreen(); //if not quiting I am guessing puts cursor in right place
              return;

          } // end of case '\r' switch
     
          return;
  
        default:
          ;
          int n = strlen(E.command);
          if (c == DEL_KEY || c == BACKSPACE) {
            E.command[n-1] = '\0';
          } else {
            E.command[n] = c;
            E.command[n+1] = '\0';
          }

          editorSetMessage(E.command);
      } 
  
      return;

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
          return;
    
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
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case 'y':  
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          editorYankLine(E.repeat);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
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
          return;
    
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
          return;
    
        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return;
    
        default:
          return;
      }

      return;

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
          return;
    
        case 'x':
          editorCreateSnapshot();
          E.repeat = abs(E.highlight[1] - E.highlight[0]) + 1;
          //editorYankString();  /// *** causing segfault

          // the delete below requires positioning the cursor
          E.fc = (E.highlight[1] > E.highlight[0]) ? E.highlight[0] : E.highlight[1];
    
          for (int i = 0; i < E.repeat; i++) {
            editorDelChar2(E.fr, E.fc);
          }
          if (E.fc) E.fc--;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case 'y':  
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fc = E.highlight[0];
          editorYankString();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateVisual(c);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return;
    
        default:
          return;
      }
    
      return;

    case REPLACE:

      editorCreateSnapshot();
      for (int i = 0; i < E.repeat; i++) {
        editorDelChar();
        editorInsertChar(c);
      }
      E.repeat = 0;
      E.command[0] = '\0';
      E.mode = 0;
      return;

  }  //end of outer switch(E.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
} //end of editorProcessKeyPress

/********************************************************** WW stuff *****************************************/
// used by editorDrawRows to figure out the first row to draw
int editorGetFileRowByLineWW(int y){
  /*
  y is the actual screenline (E.cy)
  the display may be scrolled so below add offset
  */

  int screenrow = -1;
  int n = 0;
  int linerows;

  y+= E.line_offset; 

  if (y == 0) return 0;
  for (;;) {
    linerows = editorGetLinesInRowWW(n);
    screenrow+= linerows;
    if (screenrow >= y) break;
    n++;
  }
  return n;
}

/****************************ESSENTIAL*****************************/
int editorGetLinesInRowWW(int r) {
  //if (r > E.rows.size() - 1) return 0; ////////reasonable but callers check
  //if (E.rows.empty()) return 0; ////////////////
  std::string& row = E.rows.at(r);

  if (row.size() <= E.screencols) return 1; //seems obvious but not added until 03022019

  char *start,*right_margin;
  int left, width, num;  //, len;
  bool more_lines = true;

  left = row.size(); //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  num = 0;
  while(more_lines) { 

    if(left <= width) { //after creating whatever number of lines if remainer <= width: get out
      more_lines = false;
      num++; 
          
    } else {
      right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
      while(!isspace(*right_margin)) { 
        right_margin--;
        if(right_margin == start) { // situation in which there were no spaces to break the link
          right_margin += width - 1;
          break; 
        }    
      } 
      left -= right_margin-start+1;      /* +1 for the space */
      start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
      num++;
    }
  }
  return num;
}

// new  called by editorGetScreenYFromRowColWW that is in editScroll
// this doesn't seem right to me
// should be a self-contained version that
// may look almost identical to editorGetLineCharCount
// also right now only looks like it works for first row
// not sure if problem is here or elsewhere
int editorGetLineInRowWW__old(int r, int c) {

  //erow *row = &E.row[r];
  std::string& row = E.rows.at(r);
  if (row.empty()) return 1;

  char *start,*right_margin;
  int left, width, num;  //, len;
  bool more_lines = true;

  left = c + 1; //row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  num = 0;
  while(more_lines) { 

    // not sure which of the two following are more correct
    // or practically they may be equivalent
    //if (left <= ((E.mode == INSERT) ? width : editorGetLineCharCountWW(r, num))) { 
    //if (left <= (editorGetLineCharCountWW(r, num + 1) + (E.mode == INSERT))) { 
    if (left <= ((E.mode == INSERT && c >= row.size()) ? E.screencols  + 1 : editorGetLineCharCountWW(r, num + 1))) {
      more_lines = false;
      num++; 
          
    } else {
      right_margin = start + width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
      if (right_margin > &row[0] + row.size() - 1) right_margin = &row[0] + row.size() - 1; //02252019 ? kluge
      while(!isspace(*right_margin)) { 
        right_margin--;
        if(right_margin == start) { // situation in which there were no spaces to break the link
          right_margin += width - 1;
          break; 
        }    
      } 
      left -= right_margin - start + 1;      /* +1 for the space */
      start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
      num++;
    }
  }
  return num;
}

// ESSENTIAL*********************************************
// used by editorGetScreenXFromRowCol and by editorGetLineInRow
int editorGetLineCharCountWW(int r, int line) {
// doesn't take into account insert mode (which seems to be OK)

  std::string& row = E.rows.at(r);
  if (row.empty()) return 0;

  char *start,*right_margin;
  int width, num, len, left;  //length left

  left = row.size(); //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  num = 1; 
  for (;;) {
    if (left <= width) return left; 

    // each time start pointer moves you are adding the width to it and checking for spaces
    right_margin = start + width - 1; 
    if (right_margin > &row[0] + row.size() - 1) right_margin = &row[0] + row.size() - 1; //02262019 ? kluge
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left = left - (right_margin - start + 1); 
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    //length += len;
    if (num == line) break;
    num++;
    //length += len;
  }
  return len;
}

// ESSENTIAL ***************************************************
// if it works should combine them -- not sure there is any benefit to combining
// used by editorGetScreenYFromRowColWW
// there is a corner case of INSERT mode when you really can't know if you
//should be beyond the end of the line in a multiline or in the zeroth position
// of the next line, which mean cursor jumps from end to the E.cx/E.fc = 1 position
int editorGetLineInRowWW(int r, int c) {

  std::string& row = E.rows.at(r);
  if (row.empty()) return 1; //10-12-2019

  char *start,*right_margin;
  int width, len, left, length;  //num 

  left = row.size(); //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;

  int num = 1; ////// Don't see how this works for line = 1
  for (;;) {

    /*
    if (left <= ((E.mode == INSERT) ? width : editorGetLineCharCountWW(r, num))) { 
      length += left;
      len = left;
      break;
    }
    */

    // each time start pointer moves you are adding the width to it and checking for spaces
    right_margin = start + width - 1; 

    // Alternative that if it works doesn't depend on knowing the line count!!
    //seems to work
    if ((right_margin - &row[0]) >= (row.size() + (E.mode == INSERT))) {
      break; 
    }

    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= len;
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    length += len;
    //if (c - (E.mode == INSERT) < length) break; // this seems to work
    if (c < length) break; // I think best solution because handles end of line/beginning of next better
    num++;
  }

  return num;
}
// called in editScroll to get E.cx
// seems to correctly take into account insert mode which means you can go beyond chars in line
// there is a corner case of INSERT mode when you really can't know if you
//should be beyond the end of the line in a multiline or in the zeroth position
// of the next line, which mean cursor jumps from end to the E.cx/E.fc = 1 position
int editorGetScreenXFromRowCol(int r, int c) {

  std::string& row = E.rows.at(r);
  if (row.empty() || (c == 0)) return 0;

  char *start,*right_margin;
  int width, len, left, length;

  left = row.size();
  start = &row[0];
  width = E.screencols; // width we are wrapping to
  
  length = 0;

  int num = 1;
  for (;;) {

    /*
    // this is neccessary here or the alternative solution below
    if (left <= ((E.mode == INSERT) ? width : editorGetLineCharCountWW(r, num))) { 
      length += left;
      len = left;
      break;
    }
    */


    //each time start pointer moves you are adding the width to it and checking for spaces
    right_margin = start + width - 1; 

    // Alternative that doesn't depend on knowing the line count
    if ((right_margin - &row[0]) >= (row.size() + (E.mode == INSERT))) {
       length += left;
       len = left;
       break; 
       }

    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 

    len = right_margin - start + 1; // the length of the row just analyzed
    left -= len; // how much of the row is left to be analyzed
    start = right_margin + 1; // The pointer to the beginning of the next line to be analyzed
    length += len; // the total length we've analyzed

    /* if the char position in the row (usually E.fc) is less than the
       length we've already analyzed then we can break since we know we've
       gone too far*/

    //if (c - (E.mode == INSERT) < length) break; // c gets to stay on line for extra char in INSERT mode
    if (c < length) break; // I think best solution because handles end of line/beginning of next better
    num++;
  }
  
  /* The below says to find the x position we need to know how many chars
     make up the preceding n lines before the line that C is in and that
     would be c - (length - len) --> c - length + len
  */

  return c - length + len;
}
/****************************ESSENTIAL (above) *****************************/

// returns row, line in row, and column
// now only useful (possibly) for debugging
int *editorGetRowLineCharWW(void) {
  int screenrow = -1;
  int r = 0;

  //if not use static then it's a variable local to function
  static int row_line_char[3];

  int linerows;
  int y = E.cy + E.line_offset; 
  if (y == 0) {
    row_line_char[0] = 0;
    row_line_char[1] = 1;
    row_line_char[2] = E.cx;
    return row_line_char;
  }
  for (;;) {
    linerows = editorGetLinesInRowWW(r);
    screenrow += linerows;
    if (screenrow >= y) break;
    r++;
  }

  // right now this is necesssary for backspacing in a multiline filerow
  // no longer seems necessary for insertchar
  if (E.continuation) r--;

  row_line_char[0] = r;
  row_line_char[1] = linerows - screenrow + y;
  row_line_char[2] = editorGetCharInRowWW(r, linerows - screenrow + y);

  return row_line_char;
}

// below only used in editorGetRowLineChar which
// returns row, line in row and column
// now only useful (possibly) for debugging
int editorGetCharInRowWW(int r, int line) {
  //erow *row = &E.row[r];
  std::string& row = E.rows.at(r);
  char *start,*right_margin;
  int left, width, num, len, length;

  left = row.size(); //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;
  num = 1; 
  for (;;) {
    if (left <= width) return length + E.cx; 
    right_margin = start+width - 1; 
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= right_margin-start+1;      // +1 for the space //
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    if (num == line) break;
    num++;
    length += len;
  }
  return length + E.cx;
}

// debugging
int *editorGetRowLineScreenXFromRowCharPosWW(int r, int c) {

  static int rowline_screenx[2]; //if not use static then it's a variable local to function
  //erow *row = &E.row[r];
  std::string& row = E.rows.at(r);
  char *start,*right_margin;
  int width, len, left, length, num; //, prev_length;  

  left = row.size(); //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = &row[0]; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;

  // not 100% sure where this if should be maybe editorScroll /********************************************/
  if (row.size() == 0) {
    //E.fc = -1;
    E.fc = 0;
    rowline_screenx[1] = 0;
    rowline_screenx[0] = 1;
    return rowline_screenx;
  } //else if (c == -1 || E.fc == -1) E.fc = c = 0;

 
  num = 1; 
  for (;;) {
    if (left < width + 1) { //// didn't have the + 1 and + 1 seems better 02182019
      length += left;
      len = left;
      break;
    }
    right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= right_margin - start+1;      // +1 for the space //
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    length += len;
    if (c < length) break; // changing from <= to < fixed a problem in editorMoveNextWork - no idea if it introduced new issues !! 02182019
    num++;
  }
  rowline_screenx[1] = c - length + len;
  rowline_screenx[0] = num;
  return rowline_screenx;
}

/*********************************MAY BE ESENTIAL*****************************/
// was in used in editorScroll -- don't delete yet slim chance might go back there
int *editorGetScreenPosFromRowCharPosWW(int r, int c) { //, int fc){
  static int screeny_screenx[2]; //if not use static then it's a variable local to function
  int screenline = 0;
  int n = 0;

  for (n=0; n < r; n++) { 
    screenline+= editorGetLinesInRowWW(n);
  }

  screenline -= E.line_offset;
  // below seems like a total kluge and (barely tested) but actually seems to work
  //- ? should be in editorScroll - I did try to put a version in editorScroll but
  // it didn't work and I didn't investigate why so here it will remain at least  for the moment
  if (screenline<=0 && r==0) {
    E.line_offset = 0; 
    screenline = 0;
    }
  // since E.cx should be less than E.row[].size (since E.cx counts from zero and E.row[].size from 1
  // this can put E.cx one farther right than it should be but editorMoveCursor checks and moves it back if not in insert mode
  int *rowline_screenx = editorGetRowLineScreenXFromRowCharPosWW(r, c);
  screeny_screenx[0] = screenline + rowline_screenx[0] - 1; //new -1
  screeny_screenx[1] = rowline_screenx[1];
  return screeny_screenx;
}
/*************************************MAY BE ESSENTIAL (above)  ************************************************/

// ESSENTIAL - used in editScroll
// not 100% sure this should take into account E.line_offset
int editorGetScreenYFromRowColWW(int r, int c) { //, int fc){
  int screenline = 0;

  for (int n = 0; n < r; n++) { 
    screenline+= editorGetLinesInRowWW(n);
  }

  // now doubting this should include E.line_offset but not sure
  //screenline = screenline + editorGetLineInRowWW(r, c) - E.line_offset - 1;
  screenline = screenline + editorGetLineInRowWW(r, c) - 1;

  return screenline; // ? check if it's less than zero -- do it in editorScroll
  
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
    //line_buffer.push_back(E.rows.at(E.cy+i)); 11-26-2019
    line_buffer.push_back(E.rows.at(E.fr+i));
  }
  string_buffer.clear();
}

void editorYankString(void) {
  // doesn't cross rows right now
  if (E.rows.empty()) return;

  std::string& row = E.rows.at(E.fr);
  string_buffer.clear();

  std::string::const_iterator first = row.begin() + E.highlight[0];
  std::string::const_iterator last = row.begin() + E.highlight[1];
  string_buffer = std::string(first, last);
}

void editorPasteString(void) {
  if (E.rows.empty() || string_buffer.empty()) return;
  std::string& row = E.rows.at(E.fr);

  row.insert(row.begin() + E.fc, string_buffer.begin(), string_buffer.end());
  E.fc += string_buffer.size();
  E.dirty++;
}

void editorPasteLine(void){
  if (E.rows.empty())  editorInsertRow(0, std::string());

  for (int i=0; i < line_buffer.size(); i++) {
    //int len = (line_buffer[i].size());
    E.fr++;
    editorInsertRow(E.fr, line_buffer[i]);
  }
}

void editorIndentRow(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.empty()) return;
  E.fc = editorIndentAmount(E.fr);
  for (int i = 0; i < E.indent; i++) editorInsertChar(' ');
  E.dirty++;
}

void editorUnIndentRow(void) {
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row.empty()) return;
  E.fc = 0;
  for (int i = 0; i < E.indent; i++) {
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

  for (i = 0; i<row.size(); i++) {
    if (row[i] != ' ') break;}

  return i;
}

// called by caw and daw
void editorDelWord(void) {

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row[E.fc] < 48) return;

  int i,j,x;
  for (i = E.fc; i > -1; i--){
    if (row[i] < 48) break;
    }
  for (j = E.fc; j < row.size() ; j++) {
    if (row[j] < 48) break;
  }

  for (x = 0 ; x < j-i; x++) {
      editorDelChar();
  }
  E.dirty++;
  //editorSetMessage("i = %d, j = %d", i, j ); 
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

// not same as 'e' but moves to end of word or stays put if already
//on end of word - used by dw
void editorMoveEndWord2() {
  int j;

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);

  for (j = E.fc + 1; j < row.size() ; j++) {
    if (row[j] < 48) break;
  }

  E.fc = j - 1;

}

// used by 'w'
void editorMoveNextWord(void) {
// doesn't handle multiple white space characters at EOL
  int i,j;

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);

  if (row[E.fc] < 48) j = E.fc;
  else {
    for (j = E.fc + 1; j < row.size(); j++) {
      if (row[j] < 48) break;
    }
  } 

  if (j >= row.size() - 1) { // at end of line was ==

    if (E.fr == E.rows.size() - 1) return; // no more rows
    
    for (;;) {
      E.fr++;
      std::string& row = E.rows.at(E.fr);
      //row = E.row[E.fr];
      if (row.size() == 0 && E.fr == E.rows.size() - 1) return;
      if (row.size()) break;
      }
  
    if (row[0] > 47) {
      E.fc = 0;
      return;
    } else {
      for (j = E.fc + 1; j < row.size(); j++) {
        if (row[j] < 48) break;
      }
    }
  }

  for (i = j + 1; j < row.size() ; i++) { //+1
    if (row[i] > 48) {
      E.fc = i;
      break;
    }
  }
}

// normal mode 'b'
void editorMoveBeginningWord(void) {

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  int j = E.fc;

  if (E.fc == 0 || (E.fc == 1 && row[0] < 48)) {
    if (E.fr == 0) return;
    for (;;) {
      E.fr--;
      std::string& row = E.rows.at(E.fr);
      if (E.fr == 0 && row.size() == 0) return;
      if (row.size() == 0) continue;
      if (row.size()) {
        j = row.size() - 1;
        break;
      }
    } 
  }

  for (;;) {
    if (j > 1 && row[j - 1] < 48) j--;
    else break;
  }

  int i;
  for (i = j - 1; i > -1; i--){
    if (i == 0) { 
      if (row[0] > 47) {
        E.fc = 0;
        break;
      } else return;
    }
    if (row[i] < 48) {
      E.fc = i + 1;
      break;
    }
  }
}

// normal mode 'e' - seems to handle all corner cases
void editorMoveEndWord(void) {

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);

  int j = (row[E.fc + 1] < 48) ? E.fc + 1 : E.fc;

  for(;;) {

    j++;

    if (j > row.size() - 1) { //>=

      for (;;) {
        if (E.fr == E.rows.size() - 1) return; // no more rows
        E.fr++;
        std::string& row = E.rows.at(E.fr);
        if (row.size() == 0 && E.fr == E.rows.size() - 1) return;
        if (row.size()) {
          j = 0;
          break;
        }
      }
    }
    if (j == row.size() - 1) {
      E.fc = j;
      break;
    }
    if (row[j] < 48 && (j < row.size() - 1) && row[j - 1] > 48) {
      E.fc = j-1;
      break;
    }
 
  }
}

void editorDecorateWord(int c) {

  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  char cc;
  if (row[E.fc] < 48) return;

  int i, j;

  /*Note to catch ` would have to be row->chars[i] < 48 || row-chars[i] == 96 - may not be worth it*/

  for (i = E.fc - 1; i > -1; i--){
    if (row[i] < 48) break;
  }

  for (j = E.fc + 1; j < row.size() ; j++) {
    if (row[j] < 48) break;
  }
  
  if (row[i] != '*' && row[i] != '`'){
    cc = (c == CTRL_KEY('b') || c ==CTRL_KEY('i')) ? '*' : '`';
    E.fc = i + 1;
    editorInsertChar(cc);
    E.fc = j + 1;
    editorInsertChar(cc);

    if (c == CTRL_KEY('b')) {
      E.fc = i + 1;
      editorInsertChar('*');
      E.fc = j + 2;
      editorInsertChar('*');
    }
  } else {
    E.fc = i;
    editorDelChar();
    E.fc = j - 1;
    editorDelChar();

    if (c == CTRL_KEY('b')) {
      E.fc = i - 1;
      editorDelChar();
      E.fc = j - 2;
      editorDelChar();
    }
  }
}

void editorDecorateVisual(int c) {
  if (E.rows.empty()) return;
  E.fc = E.highlight[0];
  if (c == CTRL_KEY('b')) {
    editorInsertChar('*');
    editorInsertChar('*');
    E.fc = E.highlight[1]+3;
    editorInsertChar('*');
    editorInsertChar('*');
  } else {
    char cc = (c ==CTRL_KEY('i')) ? '*' : '`';
    editorInsertChar(cc);
    E.fc = E.highlight[1]+2;
    editorInsertChar(cc);
  }
}

void getWordUnderCursor(void){

  search_string.clear();
 
  if (E.rows.empty()) return;
  std::string& row = E.rows.at(E.fr);
  if (row[E.fc] < 48) return;

  int i,j,x;

  for (i = E.fc - 1; i > -1; i--){
    if (row[i] < 48) break;
  }

  for (j = E.fc + 1; j < row.size() ; j++) {
    if (row[j] < 48) break;
  }

  for (x=i+1; x<j; x++) {
      search_string.push_back(row.at(x));
  }

}

// needs a little work and needs to wrap back on itself something odd about wrapping matches
void editorFindNextWord(void) {
  if (E.rows.empty()) return;
  int y, x;
  char *z;

  y = E.fr;
  x = E.fc + 10;
  std::string row;
 
  /*n counter so we can exit for loop if there are  no matches for command 'n'*/
  for (;;) {
    row = E.rows[y];
    z = strstr(&(row[x]), search_string.c_str());
    if (z != NULL) break;
    y++;
    x = 0;
    if (y == E.rows.size()) y = 0;
  }

  E.fc = z - &row[0];
  E.fr = y;

  editorSetMessage("x = %d; y = %d", x, y); 
}

void editorMarkupLink(void) {
  int y, numrows, j, n; //p;
  std::string http = "http";
  std::string bracket_http = "[http";
  numrows = E.rows.size();

  n = 1;
  for (n; E.rows[numrows-n][0] == '['; n++);

  for (y=0; y<numrows; y++){
    std::string& row = E.rows.at(y);
    if (row[0] == '[') continue;
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
  E.dirty++;
  E.fc = E.fr = E.line_offset = 0;
}

void EraseScreenRedrawLines(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Erase the screen
  int pos = screencols/2;
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (int j = 1; j < screenlines + 1; j++) {

    // First vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos - OUTLINE_RIGHT_MARGIN + 1);
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
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, pos - OUTLINE_RIGHT_MARGIN + 1);
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
  O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  O.view = TASK; // not necessary here since set when searching database
  O.taskview = BY_FOLDER;
  O.folder = "todo";
  O.context = "";
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
  //E.coloff = 0;  //should always be zero because of line wrap
  E.dirty = 0; //has filed changed since last save
  //E.filename = NULL; //not used currently
  E.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  E.highlight[0] = E.highlight[1] = -1;
  E.mode = 0; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  E.command[0] = '\0';
  E.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y
  E.indent = 4;
  E.smartindent = 1; //CTRL-z toggles - don't want on what pasting from outside source
  E.continuation = 0; //circumstance when a line wraps

  // ? whether the screen-related stuff should be in one place
  E.screenlines = screenlines - 2 - TOP_MARGIN;
  E.screencols = -2 + screencols/2;
  EDITOR_LEFT_MARGIN = screencols/2 + 1;
}

int main(int argc, char** argv) { 

  //outline_normal_map[ARROW_UP] = move_up;
  //outline_normal_map['k'] = move_up;

  if (argc > 1 && argv[1][0] == 's') {
    get_items = get_items_sqlite;
    //get_items_by_id = get_items_by_id_sqlite;
    get_note = get_note_sqlite;
    update_note = update_note_sqlite;
    toggle_star = toggle_star_sqlite;
    toggle_completed = toggle_completed_sqlite;
    toggle_deleted = toggle_deleted_sqlite;
    //insert_row = insert_row_sqlite;
    update_rows = update_rows_sqlite;
    update_row = update_row_sqlite;
    update_task_context = update_task_context_sqlite;
    update_task_folder = update_task_folder_sqlite;
    display_item_info = display_item_info_sqlite;
    touch = touch_sqlite;
    search_db = fts5_sqlite;
    get_containers = get_containers_sqlite;
    update_container = update_container_sqlite;
    map_context_titles =  map_context_titles_sqlite;
    map_folder_titles =  map_folder_titles_sqlite;
    add_task_keyword = add_task_keyword_sqlite;
    delete_task_keywords = delete_task_keywords_sqlite;
    get_keywords = get_keywords_sqlite;

    which_db = SQLITE;

  } else {
    get_conn();
    get_items = get_items_pg;
    //get_items_by_id = get_items_by_id_pg;
    get_note = get_note_pg;
    update_note = update_note_pg;
    toggle_star = toggle_star_pg;
    toggle_completed = toggle_completed_pg;
    toggle_deleted = toggle_deleted_pg;
    //insert_row = insert_row_pg;
    update_rows = update_rows_pg;
    update_row = update_row_pg;
    update_task_context = update_task_context_pg;
    update_task_folder = update_task_folder_pg;
    display_item_info = display_item_info_pg;
    touch = touch_pg;
    search_db = solr_find;
    get_containers = get_containers_pg;
    update_container = update_container_pg;
    map_context_titles = map_context_titles_pg;
    map_folder_titles = map_folder_titles_pg;
    add_task_keyword = add_task_keyword_pg;
    delete_task_keywords = delete_task_keywords_pg;
    get_keywords = get_keywords_pg;

    which_db = POSTGRES;
  }

  map_context_titles();
  map_folder_titles();

  //if (getWindowSize(&screenlines, &screencols) == -1) die("getWindowSize");
  getWindowSize(&screenlines, &screencols);
  enableRawMode();
  EraseScreenRedrawLines();
  initOutline();
  initEditor();
  //O.taskview = BY_FOLDER; //set in initOutline
  get_items(MAX);
  
 // PQfinish(conn); // this should happen when exiting

  O.fc = O.fr = O.rowoff = 0; 
  outlineSetMessage("rows: %d  cols: %d orow size: %d int: %d char*: %d bool: %d", O.screenlines, O.screencols, sizeof(orow), sizeof(int), sizeof(char*), sizeof(bool)); //for display screen dimens

  // putting this here seems to speed up first search but still slow
  // might make sense to do the module imports here too
  // assume the reimports are essentially no-ops
  //Py_Initialize(); 

  signal(SIGWINCH, signalHandler);

  while (1) {

    if (editor_mode){
      editorScroll();
      editorRefreshScreen();
      editorProcessKeypress();
    } else if (O.mode != FILE_DISPLAY) { 
      outlineScroll();
      outlineRefreshScreen();
      outlineProcessKeypress();
      // problem is that mode does not get updated in status bar
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode
  }
  return 0;
}
