#ifndef LISTMANAGER_H
#define LISTMANAGER_H

#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this
#define TOP_MARGIN 1 // Editor.cpp
#define DEBUG 0
#define SCROLL_UP 1 // in Editor.cpp  not in list...improved.cpp
#define LINKED_NOTE_HEIGHT 10 //height of subnote

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

extern "C" {
#include <mkdio.h>
}

void updateNote(void); //used by Editor class 
void update_code_file(void);
std::string getTitle(int id);


/* also used by Editor class */
int getFolderTid(int); 
void open_in_vim(void);
#endif
