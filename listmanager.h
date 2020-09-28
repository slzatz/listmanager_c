#ifndef LISTMANAGER_H
#define LISTMANAGER_H

#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this
#define TOP_MARGIN 1 // Editor.cpp
#define DEBUG 0
#define SCROLL_UP 1 // in Editor.cpp  not in list...improved.cpp
#define LINKED_NOTE_HEIGHT 10 //height of subnote

#include "process.h" // https://github.com/skystrife/procxx used by Editor.cpp and list...improved.cpp
#include <map> //there is an inline map

#if __has_include (<nuspell/dictionary.hxx>)
  #include <nuspell/dictionary.hxx>
  #include <nuspell/finder.hxx>
  #define NUSPELL
#endif

#include <zmq.hpp>

//#include <memory> //unique pointer, shared pointer etc.

//#include <fcntl.h>
//#include <unistd.h>

extern "C" {
#include <mkdio.h>
}

// used by list..improved.cpp and Editor.cpp
const std::string CURRENT_NOTE_FILE = "current.html";
const std::string META_FILE = "assets/meta.html";

const std::string COLOR_1 = "\x1b[0;31m"; //red
const std::string COLOR_2 = "\x1b[0;32m"; //green
const std::string COLOR_3 = "\x1b[0;33m"; //yellow
const std::string COLOR_4 = "\x1b[0;34m"; //blue
const std::string COLOR_5 = "\x1b[0;35m"; //magenta
const std::string COLOR_6 = "\x1b[0;36m"; //cyan
const std::string COLOR_7 = "\x1b[0;37m"; //White

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
  SEARCH, // only outline mode
  ADD_CHANGE_FILTER //only outline mode 
};

const std::string mode_text[] = {
                        "NORMAL",
                        "INSERT",
                        "COMMAND LINE",
                        "VISUAL LINE",
                        "VISUAL",
                        "REPLACE",
                       // "DATABASE",
                        "FILE DISPLAY",
                        "NO ROWS",
                        "VISUAL BLOCK",
                        "SEARCH",
                        "ADD/CHANGE FILTER"  
                       }; 


/* as of C++17 you can do inline and variables will only be defined once */
inline bool lm_browser = true;
inline bool editor_mode = false;
inline std::map<int, std::string> html_files;
inline int EDITOR_LEFT_MARGIN; //set in listman...improved.cpp but only used in Editor.cpp = O.divider + 1
inline std::vector<std::vector<int>> word_positions = {};
//inline std::vector<std::string> line_buffer = {}; //yanking lines
void update_note(bool); //used by Editor class 
std::string get_title(int id);


/* also used by Editor class */
int get_folder_tid(int); 
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void open_in_vim(void);
void generate_persistent_html_file(int);
#endif
