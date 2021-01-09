#ifndef SESSION_H
#define SESSION_H
#include "Editor.h"
#include "Dbase.h"
#include <vector>
#include <string>
#include <termios.h>

const std::string SQLITE_DB_ = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB_ = "/home/slzatz/listmanager_cpp/fts5.db";

struct Session {
  int screencols;
  int screenlines;

  //O.screenlines = sess.screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  int textlines; //total lines available in editor and organizer

  int divider;
  int totaleditorcols;
  std::vector<Editor*> editors;

  void eraseRightScreen(void);
  void draw_editors(void);
  void position_editors(void);
  int getWindowSize(void);

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
