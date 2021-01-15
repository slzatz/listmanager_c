#ifndef LISTMANAGER_H
#define LISTMANAGER_H

#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this
#define TOP_MARGIN 1 // Editor.cpp
#define DEBUG 0
#define SCROLL_UP 1 // in Editor.cpp  not in list...improved.cpp
#define LINKED_NOTE_HEIGHT 10 //height of subnote

//#include "process.h" // https://github.com/skystrife/procxx used by Editor.cpp and list...improved.cpp
#include <map> //there is an inline map
#include <nlohmann/json.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/chrono.h>
#include "Editor.h"
//#include "Common.h"

#if __has_include (<nuspell/dictionary.hxx>)
  #include <nuspell/dictionary.hxx>
  #include <nuspell/finder.hxx>
  #define NUSPELL
#endif

//#include <zmq.hpp>

//#include <memory> //unique pointer, shared pointer etc.

//#include <fcntl.h>
//#include <unistd.h>

extern "C" {
#include <mkdio.h>
}

/* as of C++17 you can do inline and variables will only be defined once */
inline bool lm_browser = true; //soon eliminate ./m_nb and make default false
//inline bool editor_mode = false;
inline int EDITOR_LEFT_MARGIN; //set in listman...improved.cpp but only used in Editor.cpp = O.divider + 1
inline std::vector<std::vector<int>> word_positions = {};
//inline Session sess;
//inline std::vector<std::string> line_buffer = {}; //yanking lines
//void update_note(bool is_subeditor, bool closing_editor=false); //used by Editor class 
void updateNote(void); //used by Editor class 
void update_code_file(void);
std::string get_title(int id);


/* also used by Editor class */
int get_folder_tid(int); 
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void open_in_vim(void);
void generate_persistent_html_file(int);
#endif
