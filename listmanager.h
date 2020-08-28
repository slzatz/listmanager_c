#ifndef LISTMANAGER_H
#define LISTMANAGER_H

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

#if __has_include (<nuspell/dictionary.hxx>)
  #include <nuspell/dictionary.hxx>
  #include <nuspell/finder.hxx>
  #define NUSPELL
#endif

#include <zmq.hpp>

#include <memory>

#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <mkdio.h>
}
const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
const std::string DB_INI = "db.ini";
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

const std::string mode_text[] = {
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

constexpr char BASE_DATE[] = "1970-01-01 00:00";
/*
extern struct outlineConfig O;
extern bool editor_mode;
extern bool lm_browser;
extern std::map<int, std::string> html_files;
*/

/* as of C++17 you can do inline and variables will only be defined once */
inline bool lm_browser = true;
//inline struct outlineConfig O; //right now only for O.mode == SEARCH
inline bool editor_mode = false;
inline std::map<int, std::string> html_files;
inline int EDITOR_LEFT_MARGIN;
inline std::vector<std::vector<int>> word_positions = {};
inline std::vector<std::string> line_buffer = {}; //yanking lines
void update_note(void); //used by Editor class 
std::string get_title(int id);


/* also used by Editor class */
int get_folder_tid(int); 
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void open_in_vim(void);
void generate_persistent_html_file(int);
#endif
