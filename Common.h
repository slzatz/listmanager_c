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

const std::string CURRENT_NOTE_FILE = "current.html";
const std::string META_FILE = "assets/meta.html";

struct orow {  //Entry
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
};

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

struct Container {
  int id;
  int tid;
  std::string title;
  bool star;
  std::string created;
  bool deleted;
  std::string modified;
  int count;   
};
/* Task
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
/* Context
0: id => int in use
1: tid => int in use
2: title = string 32 in use
3: "default" = Boolean ? -> star in use
4: created = 2016-08-05 23:05:16.256135 in use
5: deleted => bool in use
6: icon => string 32
7: textcolor, Integer
8: image, largebinary
9: modified in use
*/
  /* Folder
0: id => int
1: tid => int
2: title = string 32
3: private = Boolean -> star
4: archived = Boolean ? what this is
5: "order" = integer
6: created = 2016-08-05 23:05:16.256135
7: deleted => bool
8: icon => string 32
9: textcolor, Integer
10: image, largebinary
11: modified
*/
/* Keyword
0: id => int
1: name = string 25
2: tid => int
3: star = Boolean
4: modified
5: deleted
  */
#endif
