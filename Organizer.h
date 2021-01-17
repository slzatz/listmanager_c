#ifndef ORGANIZER_H
#define ORGANIZER_H

#include <string>
#include <vector> // doesn't seem necessary ? why
#include <map>
#include <unordered_set>
#include <fmt/format.h>
#include "Common.h"

class Organizer {
  public:
  int cx, cy; //cursor x and y position
  int fc, fr; // file x and y position
  int rowoff; //the number of rows scrolled (aka number of top rows now off-screen
  int coloff; //the number of columns scrolled (aka number of left rows now off-screen
  std::vector<orow> rows;
  static std::vector<std::string> preview_rows;
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
  static std::vector<std::vector<int>> word_positions;

  void displayContainerInfo(Container &c); //probably should not be here but in session

  /* started here */
  std::string outlinePreviewRowsToString(void);//OK
  void outlineDelWord();//OK
  void outlineMoveCursor(int key);//OK
  /////////////////////////////////////////
  void outlineBackspace(void);//OK
  void outlineDelChar(void);//OK
  void outlineDeleteToEndOfLine(void);//OK
  //void outlineYankLine(int n);
  void outlinePasteString(void);//OK
  void outlineYankString();//OK
  void outlineMoveCursorEOL();//OK
  void outlineMoveBeginningWord();//OK
  void outlineMoveEndWord();//OK
  void outlineMoveEndWord2();//OK    not 'e' but just moves to end of word even if on last letter
  //void outlineMoveNextWord();// now w_N
  void outlineGetWordUnderCursor();//OK
  void outlineFindNextWord();//OK
  void outlineChangeCase();//OK
  void outlineInsertRow(int, std::string&&, bool, bool, bool, std::string&&);//OK
  void outlineScroll(void);//OK
  void outlineSave(const std::string &);//OK
  //void return_cursor(void);
  void get_preview(int);//OK
  void draw_preview(void);//OK
  void draw_search_preview(void);//OK
  std::string draw_preview_box(int, int);//OK
  std::string generateWWString(std::vector<std::string> &, int, int, std::string);//OK
  void highlight_terms_string(std::string &);//OK
  void get_search_positions(int);//OK
  void outlineInsertChar(int c);//OK
  std::string outlineRowsToString(void);//OK
  void display_container_info(int id);//OK
  
  static int rowid_callback(void *, int, char **, char **);//OK
  static int offset_callback(void *, int, char **, char **);//OK
  static int preview_callback (void *, int, char **, char **);//OK
  static int keyword_info_callback(void *, int, char **, char **);//OK
  static int context_info_callback(void *, int, char **, char **);//OK
  static int folder_info_callback(void *, int, char **, char **);//OK 
  static int count_callback(void *, int, char **, char **);//OK
  //Database-related Prototypes
  //void db_open(void); -> in session
  //void update_task_context(std::string &, int);
  //void update_task_folder(std::string &, int);
  /*
  int get_id(void);
  void get_note(int); -> getNote in session
  std::string get_title(int);
  void update_title(void);
  void update_rows(void); 
  void toggle_deleted(void);-> in outline_normal_functions
  void toggle_star(void);-> in outline_normal_functions
  void toggle_completed(void);-> in outline_normal_functions
  void touch(void);
  int insert_row(orow&);
  int insert_container(orow&);
  int insert_keyword(orow &);
  void update_container(void);
  void update_keyword(void);
  void get_items(int); 
  void get_containers(void); //has an if that determines callback: context_callback or folder_callback
  std::pair<std::string, std::vector<std::string>> get_task_keywords(int); // used in F_copy_entry
  std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string
  void search_db(const std::string &); //void fts5_sqlite(std::string);
  void search_db2(const std::string &); //just searches documentation - should be combined with above
  void get_items_by_id(std::stringstream &);
  int get_folder_tid(int); 
  void map_context_titles(void);
  void map_folder_titles(void);
  void add_task_keyword(std::string &, int);
  void add_task_keyword(int, int, bool update_fts=true);
  void display_item_info(int); 
  void display_item_info(void); //ctrl-i in NORMAL mode 0x9
  void display_item_info_pg(int);
  int keyword_exists(const std::string &);  
  int folder_exists(std::string &);
  int context_exists(std::string &);
  std::string time_delta(std::string);
  std::string now(void);
  */

  /*
  //sqlite callback functions
  typedef int (*sq_callback)(void *, int, char **, char **); //sqlite callback type
  
  int fts5_callback(void *, int, char **, char **);
  int data_callback(void *, int, char **, char **);
  int context_callback(void *, int, char **, char **);
  int folder_callback(void *, int, char **, char **);
  int keyword_callback(void *, int, char **, char **);
  int context_titles_callback(void *, int, char **, char **);
  int folder_titles_callback(void *, int, char **, char **);
  int by_id_data_callback(void *, int, char **, char **);
  int note_callback(void *, int, char **, char **);
  int display_item_info_callback(void *, int, char **, char **);
  int task_keywords_callback(void *, int, char **, char **);
  int keyword_id_callback(void *, int, char **, char **);//? not in use
  int container_id_callback(void *, int, char **, char **);
  int title_callback(void *, int, char **, char **);
  int folder_tid_callback(void *, int, char **, char **); 
  int unique_data_callback(void *, int, char **, char **);
  */
  /* end here */
};
extern Organizer org;
#endif
