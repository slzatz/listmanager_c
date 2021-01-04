#ifndef SESSION_H
#define SESSION_H
#include "Editor.h"
#include <vector>
#include <string>

struct Session {
  int screencols;
  int screenlines;
  int divider;
  int totaleditorcols;
  std::vector<Editor*> editors;

  void eraseRightScreen(void);
  void draw_editors(void);

  // the history of commands to make it easier to go back to earlier views
  // Not sure it is very helpful and I don't use it at all
  std::vector<std::string> page_history;
  int page_hx_idx = 0;
  //
  // the history of commands to make it easier to go back to earlier views
  // Not sure it is very helpful and I don't use it at all
  std::vector<std::string> command_history; 
  int cmd_hx_idx = 0;
};

//inline Session sess;
extern Session sess;
#endif
