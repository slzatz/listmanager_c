#ifndef COMMON_H
#define COMMON_H

#include <string>

const std::string COLOR_1 = "\x1b[0;31m"; //red
const std::string COLOR_2 = "\x1b[0;32m"; //green
const std::string COLOR_3 = "\x1b[0;33m"; //yellow
const std::string COLOR_4 = "\x1b[0;34m"; //blue
const std::string COLOR_5 = "\x1b[0;35m"; //magenta
const std::string COLOR_6 = "\x1b[0;36m"; //cyan
const std::string COLOR_7 = "\x1b[0;37m"; //White

typedef struct orow {
  std::string title;
  std::string fts_title;
  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  std::string modified;

  // note the members below are temporary editing flags
  // and don't need to be reflected in database
  bool dirty;
  bool mark;
} orow;

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
  COMMAND_LINE, // = 2 
  VISUAL_LINE, // = 3, // only editor mode
  VISUAL, // = 4,
  REPLACE, // = 5,
 // DATABASE, // = 6, // only outline mode
  FILE_DISPLAY,// = 7, // only outline mode
  NO_ROWS,// = 8
  VISUAL_BLOCK, //only editor mode
  SEARCH, // only editor mode
  FIND, // only outline mode
  ADD_CHANGE_FILTER //only outline mode 
};

const std::string mode_text[] = {
                        "NORMAL",
                        "INSERT",
                        "COMMAND LINE",
                        "VISUAL LINE",
                        "VISUAL",
                        "REPLACE",
                        "FILE DISPLAY",
                        "NO ROWS",
                        "VISUAL BLOCK",
                        "SEARCH",
                        "FIND",
                        "ADD/CHANGE FILTER"  
                       }; 
enum DB {
  SQLITE,
  POSTGRES
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
  BY_FIND
};
#endif
