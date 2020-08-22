#include <string>
std::string system_call = "./lm_browser " + CURRENT_NOTE_FILE;
std::string meta;
int which_db;
int EDITOR_LEFT_MARGIN;
struct termios orig_termios;
int screenlines, screencols, new_screenlines, new_screencols;
std::stringstream display_text;
int initial_file_row = 0; //for arrowing or displaying files
//bool editor_mode;
std::string search_terms;
std::vector<std::vector<int>> word_positions;
std::vector<int> fts_ids;
int fts_counter;
std::string search_string; //word under cursor works with *, n, N etc.
std::vector<std::string> line_buffer; //yanking lines
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
std::unordered_map<std::string, pfunc> cmd_map1 = {{"i", f_i}, {"I", f_I}, {"a", f_a}, {"A", f_A}};
std::unordered_map<std::string, pfunc> cmd_map2 = {{"o", f_o}, {"O", f_O}};
std::unordered_map<std::string, pfunc> cmd_map3 = {{"x", f_x}, {"dw", f_dw}, {"daw", f_daw}, {"dd", f_dd}, {"d$", f_d$}, {"de", f_de}, {"dG", f_dG}};
std::unordered_map<std::string, pfunc> cmd_map4 = {{"cw", f_cw}, {"caw", f_caw}, {"s", f_s}};
*/
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
