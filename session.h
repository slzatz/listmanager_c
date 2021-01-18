#ifndef SESSION_H
#define SESSION_H
#include "Editor.h"
#include "Dbase.h"
#include <vector>
#include <string>
#include <termios.h>
#include <sstream>
#include <fcntl.h> //file locking
#include "Common.h"

const std::string SQLITE_DB_ = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB_ = "/home/slzatz/listmanager_cpp/fts5.db";

struct sqlite_db_ {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};

struct Session {
  int screencols;
  int screenlines;
  int textlines; //total lines available in editor and organizer
  int divider;
  int totaleditorcols;

  std::stringstream display_text; //klugy display of sync log file
  int initial_file_row = 0; //for arrowing or displaying files
  int temporary_tid = 99999;
  struct flock lock; 
  //int divider_pct
  //
  bool lm_browser = false;

  static int link_id;
  static char link_text[20];

  std::vector<Editor*> editors;
  Editor *p;
  //Organizer O;

  Sqlite db;
  Sqlite fts;
  //
  // the history of commands to make it easier to go back to earlier views
  // Not sure it is very helpful and I don't use it at all
  std::vector<std::string> page_history;
  int page_hx_idx = 0;
  //
  // the history of commands to make it easier to go back to earlier views
  // Not sure it is very helpful and I don't use it at all
  std::vector<std::string> command_history; 
  int cmd_hx_idx = 0;
  bool editor_mode = false;
  std::string fts_search_terms;

  struct termios orig_termios;
  std::string meta; // meta content for html for ultralight browser

  void eraseScreenRedrawLines(void);
  void eraseRightScreen(void);
  void drawEditors(void);
  void positionEditors(void);
  void returnCursor(void);
  int getWindowSize(void);
  void moveDivider(int pct);

  // same function as outlineDrawStatusBar
  void drawOrgStatusBar(void);

  void drawPreviewWindow(int id); //used to be get_preview
  void drawPreviewText(void); //used to be draw_preview
  std::string drawPreviewBox(int width, int length);
  void drawSearchPreview(void);
  /*
  void getNote(int id);
  void generateContextMap(void);
  void generateFolderMap(void);
  */

  void drawOrgRows(std::string& ab); //-> outlineDrawRows
  void drawOrgFilters(std::string& ab); //-> outlineDrawFilters
  void drawOrgSearchRows(std::string& ab); // ->outlineDrawSearchRows
  void refreshOrgScreen(void); //-> outlineRefreshScreen
  void displayContainerInfo(Container &c);

  void showOrgMessage(const char *fmt, ...);
  void showOrgMessage2(const std::string &s);

  template<typename... Args>
  void showOrgMessage3(fmt::string_view format_str, const Args & ... args) {
    fmt::format_args argspack = fmt::make_format_args(args...);
    showOrgMessage2(fmt::vformat(format_str, argspack));
  }

  void update_html_file(std::string &&fn);
  void update_html_code_file(std::string &&fn);
  static char * (url_callback)(const char *x, const int y, void *z);

  struct sqlite_db_ S;
  void run_sql(void);
  void db_open(void);
  bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg);
  bool db_query(sqlite3 *db, const std::string& sql, sq_callback callback, void *pArg, char **errmsg, const char *func);

  int get_folder_tid(int id);
  static int folder_tid_callback(void *folder_tid, int argc, char **argv, char **azColName);

  Session () : db(SQLITE_DB_), fts(FTS_DB_) {}
};

//inline Session sess;
extern Session sess;
#endif
