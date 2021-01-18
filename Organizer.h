#ifndef ORGANIZER_H
#define ORGANIZER_H

#include <string>
#include <vector> // doesn't seem necessary ? why
#include <map>
#include <unordered_set>
#include <fmt/format.h>
#include "Common.h"

Container getContainerInfo(int id);

class Organizer {
  public:
  int cx, cy; //cursor x and y position
  int fc, fr; // file x and y position
  int rowoff; //the number of rows scrolled (aka number of top rows now off-screen
  int coloff; //the number of columns scrolled (aka number of left rows now off-screen
  std::vector<orow> rows;
  //static std::vector<std::string> preview_rows;
  std::string context;
  std::string folder;
  std::string keyword;
  std::string sort;
  char message[100]; //status msg is a character array - enlarging to 200 did not solve problems with seg faulting
  int highlight[2];
  int mode;
  int last_mode;
  // probably ok that command isn't a std::string although it could be
  char command[10]; // doesn't include command_line commands
  std::string command_line; //for commands on the command line; string doesn't include ':'
  int repeat;
  bool show_deleted;
  bool show_completed;
  int view; // enum TASK, CONTEXT, FOLDER, SEARCH
  int taskview; // enum BY_CONTEXT, BY_FOLDER, BY_RECENT, BY_FIND
  int current_task_id;
  std::string string_buffer; //yanking chars
  std::map<int, std::string> fts_titles;

  static std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
  static std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
  const std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]

  std::vector<int> fts_ids;
  std::unordered_set<int> marked_entries;

  std::string title_search_string; //word under cursor works with *, n, N etc.
  //static std::vector<std::vector<int>> word_positions;

  //void displayContainerInfo(Container &c); //probably should not be here but in session

  /* started here */
  //std::string outlinePreviewRowsToString(void);//OK
  void outlineDelWord();//OK
  void outlineMoveCursor(int key);//OK
  void outlineBackspace(void);//OK
  void outlineDelChar(void);//OK
  void outlineDeleteToEndOfLine(void);//OK
  void outlinePasteString(void);//OK
  void outlineYankString();//OK
  void outlineMoveCursorEOL();//OK
  void outlineMoveBeginningWord();//OK
  void outlineMoveEndWord();//OK
  void outlineMoveEndWord2();//OK    not 'e' but just moves to end of word even if on last letter
  void outlineGetWordUnderCursor();//OK
  void outlineFindNextWord();//OK
  void outlineChangeCase();//OK
  void outlineInsertRow(int, std::string&&, bool, bool, bool, std::string&&);//OK
  void outlineScroll(void);//OK
  void outlineSave(const std::string &);//OK
  void outlineInsertChar(int c);//OK
  std::string outlineRowsToString(void);//OK

  //void get_preview(int);//needs to move to sess
};
extern Organizer org;
#endif
