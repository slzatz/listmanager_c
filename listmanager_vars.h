#include <string>
std::string system_call = "./lm_browser " + CURRENT_NOTE_FILE;
std::string meta;
int which_db;
//int EDITOR_LEFT_MARGIN; //in listmanager.h - needed by Editor.cpp
struct termios orig_termios;
int screenlines, screencols, new_screenlines, new_screencols;
std::stringstream display_text;
int initial_file_row = 0; //for arrowing or displaying files
//bool editor_mode;
std::string search_terms;
//std::vector<std::vector<int>> word_positions; //inline
std::vector<int> fts_ids;
int fts_counter;
std::string search_string; //word under cursor works with *, n, N etc.
//std::vector<std::string> line_buffer; //yanking lines //inline
std::string string_buffer; //yanking chars
std::map<int, std::string> fts_titles;
std::map<std::string, int> context_map; //filled in by map_context_titles_[db]
std::map<std::string, int> folder_map; //filled in by map_folder_titles_[db]
std::map<std::string, int> sort_map = {{"modified", 16}, {"added", 9}, {"created", 15}, {"startdate", 17}}; //filled in by map_folder_titles_[db]
std::vector<std::string> task_keywords;
std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
std::set<int> unique_ids; //used in unique_data_callback
std::vector<std::string> command_history; // the history of commands to make it easier to go back to earlier views
std::vector<std::string> page_history; // the history of commands to make it easier to go back to earlier views
size_t cmd_hx_idx = 0;
size_t page_hx_idx = 0;
//std::map<int, std::string> html_files;
//bool lm_browser = true;
int SMARTINDENT = 4; //should be in config
int temporary_tid = 99999;
int link_id = 0;
char link_text[20];
int current_task_id;
std::unordered_set<int> marked_entries;
struct flock lock;

struct sqlite_db {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};
struct sqlite_db S;

/*
typedef struct orow {
  std::string title;
  std::string fts_title;
  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  //bool code; //new to say the note is actually code
  char modified[16];

  // note the members below are temporary editing flags
  // and don't need to be reflected in database
  bool dirty;
  bool mark;
} orow;
*/

std::unordered_set<std::string> insert_cmds = {"I", "i", "A", "a", "o", "O", "s", "cw", "caw"};
std::unordered_set<std::string> move_only = {"w", "e", "b", "0", "$", ":", "*", "n", "[s","]s", "z=", "gg", "G", "yy"};
std::unordered_set<int> navigation = {
         ARROW_UP,
         ARROW_DOWN,
         ARROW_LEFT,
         ARROW_RIGHT,
         'h',
         'j',
         'k',
         'l'
};
/*
std::unordered_map<std::string, pfunc> c:md_map1 = {{"i", f_i}, {"I", f_I}, {"a", f_a}, {"A", f_A}};
std::unordered_map<std::string, pfunc> cmd_map2 = {{"o", f_o}, {"O", f_O}};
std::unordered_map<std::string, pfunc> cmd_map3 = {{"x", f_x}, {"dw", f_dw}, {"daw", f_daw}, {"dd", f_dd}, {"d$", f_d$}, {"de", f_de}, {"dG", f_dG}};
std::unordered_map<std::string, pfunc> cmd_map4 = {{"cw", f_cw}, {"caw", f_caw}, {"s", f_s}};
*/
struct config {
  std::string user;
  std::string password;
  std::string dbname;
  std::string hostaddr;
  int port;
};
struct config c;
zmq::context_t context (1);
zmq::socket_t publisher (context, ZMQ_PUB);

PGconn *conn = nullptr;

typedef struct orow {
  std::string title;
  std::string fts_title;
  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  //bool code; //new to say the note is actually code
  char modified[16];

  // note the members below are temporary editing flags
  // and don't need to be reflected in database
  bool dirty;
  bool mark;
} orow;

struct outlineConfig {
  int cx, cy; //cursor x and y position
  unsigned int fc, fr; // file x and y position
  unsigned int rowoff; //the number of rows scrolled (aka number of top rows now off-screen
  unsigned int coloff; //the number of columns scrolled (aka number of left rows now off-screen
  unsigned int screenlines; //number of lines in the display available to text
  unsigned int screencols;  //number of columns in the display available to text
  unsigned int right_screencols; //Number of columns on right hand side of screen
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
  int taskview; // enum BY_CONTEXT, BY_FOLDER, BY_RECENT, BY_SEARCH
};

struct outlineConfig O;
int getWindowSize(int *, int *);

// I believe this is being called at times redundantly before editorEraseScreen and outlineRefreshScreen
void EraseScreenRedrawLines(void);

void outlineProcessKeypress(int = 0);
bool editorProcessKeypress(void);
void outlineDrawPreview(std::string &ab);
void outlineShowMessage(const char *fmt, ...);
void outlineRefreshScreen(void); //erases outline area but not sort/time screen columns
void outlineDrawStatusBar(void);
void outlineDrawMessageBar(std::string&);
void outlineRefreshAllEditors(void);



/* OUTLINE COMMAND_LINE functions */
void F_open(int);
void F_openfolder(int);
void F_openkeyword(int);
void F_deletekeywords(int); // pos not used
void F_addkeyword(int); 
void F_keywords(int); 
void F_write(int); // pos not used
void F_x(int); // pos not used
void F_refresh(int); // pos not used
void F_new(int); // pos not used
void F_edit(int); // pos not used
void F_folders(int); 
void F_contexts(int); 
void F_recent(int); // pos not used
void F_linked(int); // pos not used
void F_find(int); 
void F_sync(int); // pos not used
void F_sync_test(int); // pos not used
void F_updatefolder(int); // pos not used
void F_updatecontext(int); // pos not used
void F_delmarks(int); // pos not used
void F_savefile(int);
void F_sort(int);
void F_showall(int); // pos not used
void F_syntax(int);
void F_set(int);
void F_open_in_vim(int); // pos not used
void F_join(int);
void F_saveoutline(int);
void F_readfile(int pos);
void F_valgrind(int pos); // pos not used
void F_quit_app(int pos); // pos not used
void F_quit_app_ex(int pos); // pos not used
void F_merge(int pos); // pos not used
void F_help(int pos);
void F_persist(int pos); // pos not used
void F_clear(int pos); // pos not used

/* OUTLINE mode NORMAL functions */
void goto_editor_N(void);
void return_N(void);
void w_N(void);
void insert_N(void);
void s_N(void);
void x_N(void);
void r_N(void);
void tilde_N(void);
void a_N(void);
void A_N(void);
void b_N(void);
void e_N(void);
void zero_N(void);
void dollar_N(void);
void I_N(void);
void G_N(void);
void O_N(void);
void colon_N(void);
void v_N(void);
void p_N(void);
void asterisk_N(void);
void m_N(void);
void n_N(void);
void u_N(void);
void caret_N(void);
void dd_N(void);
void star_N(void);
void completed_N(void);
void daw_N(void);
void caw_N(void);
void dw_N(void);
void cw_N(void);
void de_N(void);
void d$_N(void);
void gg_N(void);
void gt_N(void);
void edit_N(void);

void navigate_page_hx(int direction);
void navigate_cmd_hx(int direction);

void outlineDelWord();
void outlineMoveCursor(int key);
void outlineBackspace(void);
void outlineDelChar(void);
void outlineDeleteToEndOfLine(void);
void outlineYankLine(int n);
void outlinePasteString(void);
void outlineYankString();
void outlineMoveCursorEOL();
void outlineMoveBeginningWord();
void outlineMoveEndWord(); 
void outlineMoveEndWord2(); //not 'e' but just moves to end of word even if on last letter
//void outlineMoveNextWord();// now w_N
void outlineGetWordUnderCursor();
void outlineFindNextWord();
void outlineChangeCase();
void outlineInsertRow(int, std::string&&, bool, bool, bool, const char *);
void outlineDrawRows(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawKeywords(std::string&); // doesn't do any erasing; done in outlineRefreshRows
void outlineDrawSearchRows(std::string&); //ditto
void outlineScroll(void);
void outlineSave(const std::string &);
void return_cursor(void);
void draw_preview(void);

//Database-related Prototypes
void db_open(void);
void update_task_context(std::string &, int);
void update_task_folder(std::string &, int);
int get_id(void);
void get_note(int);
void update_row(void);
void update_rows(void);
void toggle_deleted(void);
void toggle_star(void);
void toggle_completed(void);
void touch(void);
int insert_row(orow&);
int insert_container(orow&);
int insert_keyword(orow &);
void update_container(void);
void update_keyword(void);
void get_items(int); 
void get_containers(void); //has an if that determines callback: context_callback or folder_callback
std::pair<std::string, std::vector<std::string>> get_task_keywords(void); // puts them in comma delimited string
std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string
void update_note(void); //used by Editor class 
//void solr_find(void);
void search_db(std::string); //void fts5_sqlite(std::string);
void search_db2(std::string); //just searches documentation - should be combined with above
void get_items_by_id(std::stringstream &);
int get_folder_tid(int); 
void map_context_titles(void);
void map_folder_titles(void);
void add_task_keyword(std::string &, int);
void add_task_keyword(int, int);
//void delete_task_keywords(void); -> F_deletekeywords
void display_item_info(int); 
void display_item_info(void); //ctrl-i in NORMAL mode 0x9
void display_item_info_pg(int);
void display_container_info(int);
int keyword_exists(std::string &);  
int folder_exists(std::string &);
int context_exists(std::string &);
void get_preview(int);

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
int rowid_callback(void *, int, char **, char **);
int offset_callback(void *, int, char **, char **);
int folder_tid_callback(void *, int, char **, char **); 
int context_info_callback(void *, int, char **, char **); 
int folder_info_callback(void *, int, char **, char **); 
int keyword_info_callback(void *, int, char **, char **);
int count_callback(void *, int, char **, char **);
int unique_data_callback(void *, int, char **, char **);
int preview_callback (void *, int, char **, char **);

void synchronize(int);

// Not used by Editor class
void readFile(const std::string &);
void displayFile(void);
void eraseRightScreen(void); //erases the note section; redundant if just did an EraseScreenRedrawLines

std::string generate_html(void);
std::string generate_html2(void);
void generate_persistent_html_file(int);
void load_meta(void);
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
//void editorSaveNoteToFile(const std::string &);
//void editorReadFileIntoNote(const std::string &); 

void update_solr(void); //works but not in use
void open_in_vim(void);

typedef void (*pfunc)(int);
typedef void (*zfunc)(void);

/* OUTLINE COMMAND_LINE mode command lookup */
std::unordered_map<std::string, pfunc> cmd_lookup {
  {"open", F_open}, //open_O
  {"o", F_open},
  {"openfolder", F_openfolder},
  {"of", F_openfolder},
  {"openkeyword", F_openkeyword},
  {"ok", F_openkeyword},
  {"deletekeywords", F_deletekeywords},
  {"delkw", F_deletekeywords},
  {"delk", F_deletekeywords},
  {"addkeywords", F_addkeyword},
  {"addkw", F_addkeyword},
  {"addk", F_addkeyword},
  {"k", F_keywords},
  {"keyword", F_keywords},
  {"keywords", F_keywords},
  {"write", F_write},
  {"w", F_write},
  {"x", F_x},
  {"refresh", F_refresh},
  {"r", F_refresh},
  {"n", F_new},
  {"new", F_new},
  {"e", F_edit},
  {"edit", F_edit},
  {"contexts", F_contexts},
  {"context", F_contexts},
  {"c", F_contexts},
  {"folders", F_folders},
  {"folder", F_folders},
  {"f", F_folders},
  {"recent", F_recent},
  {"linked", F_linked},
  {"l", F_linked},
  {"related", F_linked},
  {"find", F_find},
  {"fin", F_find},
  {"search", F_find},
  {"sync", F_sync},
  {"test", F_sync_test},
  {"updatefolder", F_updatefolder},
  {"uf", F_updatefolder},
  {"updatecontext", F_updatecontext},
  {"uc", F_updatecontext},
  {"delmarks", F_delmarks},
  {"delm", F_delmarks},
  {"save", F_savefile},
  {"sort", F_sort},
  {"show", F_showall},
  {"showall", F_showall},
  {"set", F_set},
  {"syntax", F_syntax},
  {"vim", F_open_in_vim},
  {"join", F_join},
  {"saveoutline", F_saveoutline},
  {"readfile", F_readfile},
  {"read", F_readfile},
  {"valgrind", F_valgrind},
  {"quit", F_quit_app},
  {"q", F_quit_app},
  {"quit!", F_quit_app_ex},
  {"q!", F_quit_app_ex},
  {"merge", F_merge},
  {"help", F_help},
  {"h", F_help},
  {"persist", F_persist},
  {"clear", F_clear},

};

/* OUTLINE NORMAL mode command lookup */
std::unordered_map<std::string, zfunc> n_lookup {
  {{0xC}, goto_editor_N},
  {"\r", return_N}, //return_O
  {"i", insert_N},
  {"s", s_N},
  {"~", tilde_N},
  {"r", r_N},
  {"a", a_N},
  {"A", A_N},
  {"x", x_N},
  {"w", w_N},

  {"daw", daw_N},
  {"dw", dw_N},
  {"daw", caw_N},
  {"dw", cw_N},
  {"de", de_N},
  {"d$", d$_N},

  {"gg", gg_N},

  {"gt", gt_N},

  {{0x17,0x17}, edit_N},
  {{0x9}, display_item_info},

  {"b", b_N},
  {"e", e_N},
  {"0", zero_N},
  {"$", dollar_N},
  {"I", I_N},
  {"G", G_N},
  {"O", O_N},
  {":", colon_N},
  {"v", v_N},
  {"p", p_N},
  {"*", asterisk_N},
  {"m", m_N},
  {"n", n_N},
  {"u", u_N},
  {"dd", dd_N},
  {{0x4}, dd_N}, //ctrl-d
  {{0x2}, star_N}, //ctrl-b -probably want this go backwards (unimplemented) and use ctrl-e for this
  {{0x18}, completed_N}, //ctrl-x
  {"^", caret_N},

};