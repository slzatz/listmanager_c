#ifndef SESSION_H
#define SESSION_H
#include "Editor.h"
#include "Dbase.h"
#include <vector>
#include <string>
#include <termios.h>
#include <sstream>
#include <fcntl.h> //file locking
#include "Organizer.h"

const std::string SQLITE_DB_ = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB_ = "/home/slzatz/listmanager_cpp/fts5.db";

struct Session {
  int screencols;
  int screenlines;

  //O.screenlines = sess.screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  int textlines; //total lines available in editor and organizer

  int divider;
  std::stringstream display_text; //klugy display of sync log file
  int initial_file_row = 0; //for arrowing or displaying files
  int temporary_tid = 99999;
  struct flock lock; 
  //int divider_pct
  int totaleditorcols;
  std::vector<Editor*> editors;
  Editor *p;
  Organizer O;

  void eraseScreenRedrawLines(void);
  void eraseRightScreen(void);
  void drawEditors(void);
  void positionEditors(void);
  void returnCursor(void);
  int getWindowSize(void);
  void moveDivider(int pct);

  // same function as outlineDrawStatusBar
  void drawOrgStatusBar(void);

  void getNote(int id);

  /* outlineRefreshScreen depends on three functions below
   * not sure whether they should be in session or not
   * right now in Organizer - I think they should stay in Organizer
  outlineRefreshScreen(void) -> refreshOrgScreen
  outlineDrawSearchRows(ab) -> drawOrgSearchRows
  outlineDrawFilters(ab) -> drawOrgFilters
  outlineDrawRows(ab) -> drawOrgRows
  */
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

  Sqlite db;
  Sqlite fts;

  struct termios orig_termios;
  std::string meta; // meta content for html for ultralight browser

  Session () : db(SQLITE_DB_), fts(FTS_DB_) {}
};

//inline Session sess;
extern Session sess;
#endif
