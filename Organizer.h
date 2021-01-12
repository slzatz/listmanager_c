#ifndef ORGANIZER_H
#define ORGANIZER_H

#include <string>
#include <vector> // doesn't seem necessary ? why
#include <map>
#include <unordered_set>
#include <fmt/format.h>

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

class Organizer {
  public:
  int cx, cy; //cursor x and y position
  int fc, fr; // file x and y position
  int rowoff; //the number of rows scrolled (aka number of top rows now off-screen
  int coloff; //the number of columns scrolled (aka number of left rows now off-screen
  std::vector<orow> rows;
  std::vector<std::string> preview_rows;
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

  std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
  std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
  const std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]

  std::vector<int> fts_ids;
  std::unordered_set<int> marked_entries;

  //void outlineScroll(void);
  void outlineDrawRows(std::string& ab);//needed
  void outlineDrawFilters(std::string& ab);// needed
  void outlineDrawSearchRows(std::string& ab); // needed
  void outlineDrawStatusBar(void); //needed
  void outlineRefreshScreen(void);//needed
  void outlineShowMessage(const char *fmt, ...);//needed
  void outlineShowMessage2(const std::string &s);

  template<typename... Args>
  void outlineShowMessage3(fmt::string_view format_str, const Args & ... args) {
    fmt::format_args argspack = fmt::make_format_args(args...);
    outlineShowMessage2(fmt::vformat(format_str, argspack));
  }
};
#endif
