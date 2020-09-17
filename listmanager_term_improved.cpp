#include "listmanager.h"
#include "listmanager_vars.h"
#include "Editor.h"
#include <cstdarg> //va_start etc.
#include <string_view>

//Editor E; //this instantiates it - with () it looks like a function definition with type Editor
Editor *p;
//std::shared_ptr<Editor> p(nullptr);

typedef void (Editor::*efunc)(void);
typedef void (Editor::*eefunc)(int);
std::vector<Editor *> editors;

/* EDITOR COMMAND_LINE mode lookup */
std::unordered_map<std::string, efunc> E_lookup_C {
  {"write", &Editor::E_write_C},
  {"w", &Editor::E_write_C},
 /* all below handled (right now) in editor command line switch statement*/
 // {"x", &Editor::E_write_close_C},
 // {"quit", &Editor::E_quit_C},
 // {"q",&Editor:: E_quit_C},
 // {"quit!", &Editor::E_quit0_C},
 // {"q!", &Editor::E_quit0_C},
  {"vim", &Editor::E_open_in_vim_C},
  {"spell",&Editor:: E_spellcheck_C},
  {"spellcheck", &Editor::E_spellcheck_C},
  {"persist", &Editor::E_persist_C},
};

/* EDITOR NORMAL mode command lookup */
std::unordered_map<std::string, eefunc> e_lookup {

  {"i", &Editor::E_i}, 
  {"I", &Editor::E_I},
  {"a", &Editor::E_a},
  {"A", &Editor::E_A},
  {"o", &Editor::E_o}, //note there is an E_o_escape
  {"O", &Editor::E_O}, //note there is an E_O_escape
  {"x", &Editor::E_x},
  {"dw", &Editor::E_dw},
  {"de", &Editor::E_de},
  {"dG", &Editor::E_dG},
  {"d$", &Editor::E_d$},
  {"dd", &Editor::E_dd},
  {"cw", &Editor::E_cw},
  {"caw", &Editor::E_caw},
  {"s", &Editor::E_s},
  {"~", &Editor::E_tilde},
  {"J", &Editor::E_J},
  {"w", &Editor::E_w},
  {"e", &Editor::E_e},
  {"b", &Editor::E_b},
  {"0", &Editor::E_0},
  {"$", &Editor::E_$},
  {"r", &Editor::e_replace},
  {"[s", &Editor::E_next_misspelling},
  {"]s", &Editor::E_prev_misspelling},
  {"z=", &Editor::E_suggestions},
  {":", &Editor::E_change2command_line},
  {"V", &Editor::E_change2visual_line},
  {"v", &Editor::E_change2visual},
  {{0x16}, &Editor::E_change2visual_block},
  {"p", &Editor::E_paste},
  {"*", &Editor::E_find},
  {"n", &Editor::E_find_next_word},
 // {"u", &Editor::E_undo},
 // {{CTRL_KEY('r')}, &Editor::E_redo},
  {".", &Editor::editorDotRepeat},
  {">>", &Editor::E_indent},
  {"<<", &Editor::E_unindent},
  {{0x2}, &Editor::E_bold},
  {{0x5}, &Editor::E_emphasis},
  {{0x9}, &Editor::E_italic},
  {"yy", &Editor::editorYankLine},
  {"gg", &Editor::E_gg},
  {"G", &Editor::E_G},
  {{0x1A}, &Editor::E_toggle_smartindent},
 // {{0x8}, &Editor::E_goto_outline},
  {{0x13}, &Editor::E_save_note},
};

std::unordered_map<std::string, eefunc> cmd_map1 = {{"i", &Editor::E_i}, {"I", &Editor::E_I}, {"a", &Editor::E_a}, {"A", &Editor::E_A}};
std::unordered_map<std::string, eefunc> cmd_map2 = {{"o", &Editor::E_o_escape}, {"O", &Editor::E_O_escape}};
std::unordered_map<std::string, eefunc> cmd_map3 = {{"x", &Editor::E_x}, {"dw", &Editor::E_dw}, {"daw", &Editor::E_daw}, {"dd", &Editor::E_dd}, {"d$", &Editor::E_d$}, {"de", &Editor::E_de}, {"dG", &Editor::E_dG}};
std::unordered_map<std::string, eefunc> cmd_map4 = {{"cw", &Editor::E_cw}, {"caw", &Editor::E_caw}, {"s", &Editor::E_s}};

void do_exit(PGconn *conn) {
    PQfinish(conn);
    exit(1);
}

void signalHandler(int signum) {
  getWindowSize(&new_screenlines, &new_screencols);
  screenlines = new_screenlines;
  screencols = new_screencols;
  O.screenlines = screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  O.divider = screencols - c.ed_pct * screencols/100;
  O.titlecols =  O.divider - OUTLINE_RIGHT_MARGIN - OUTLINE_LEFT_MARGIN;
  O.totaleditorcols = screencols - O.divider - 2; //? OUTLINE MARGINS?
  O.left_screencols = O.divider - 2; //OUTLINE_MARGINS

  EDITOR_LEFT_MARGIN = O.divider + 1; //only used in Editor.cpp

  eraseScreenRedrawLines();

  int n = editors.size();
  int i = 0;
  for (auto z : editors) {
    z->screenlines = screenlines - 2 - TOP_MARGIN;
    z->screencols = -1 + (screencols - O.divider)/n;
    z->total_screenlines = screenlines - 2 - TOP_MARGIN;
    z->left_margin = O.divider + i*z->screencols + i; //was +1
    i++;
  }

  outlineRefreshScreen();
  outlineDrawStatusBar();
  outlineShowMessage("rows: %d  cols: %d ", screenlines, screencols);

  if (O.view == TASK && O.mode != NO_ROWS && !editor_mode)
    get_preview(O.rows.at(O.fr).id);

  for (auto e : editors) e->editorRefreshScreen(true);

  return_cursor();
}

void parse_ini_file(std::string ini_name)
{
  inipp::Ini<char> ini;
  std::ifstream is(ini_name);
  ini.parse(is);
  inipp::extract(ini.sections["ini"]["user"], c.user);
  inipp::extract(ini.sections["ini"]["password"], c.password);
  inipp::extract(ini.sections["ini"]["dbname"], c.dbname);
  inipp::extract(ini.sections["ini"]["hostaddr"], c.hostaddr);
  inipp::extract(ini.sections["ini"]["port"], c.port);
  inipp::extract(ini.sections["editor"]["ed_pct"], c.ed_pct);
}

//pg ini stuff
void get_conn(void) {
  char conninfo[250];
  parse_ini_file(DB_INI);
  
  sprintf(conninfo, "user=%s password=%s dbname=%s hostaddr=%s port=%d", 
          c.user.c_str(), c.password.c_str(), c.dbname.c_str(), c.hostaddr.c_str(), c.port);

  conn = PQconnectdb(conninfo);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  } 
}

void load_meta(void) {
  std::ifstream f(META_FILE);
  std::string line;
  static std::stringstream text;

  while (getline(f, line)) {
    text << line << '\n';
  }
  meta = text.str();
  f.close();
}

// typedef char * (*mkd_callback_t)(const char*, const int, void*);
// needed by markdown code in update_html_file
char * (url_callback)(const char *x, const int y, void *z) {
  link_id++;
  sprintf(link_text,"id=\"%d\"", link_id);
  return link_text;
}  

/* this version of update_html_file uses mkd_document
 * and only writes to the file once
 */
void update_html_file(std::string &&fn) {
  std::string note;
  if (editor_mode) note = p->editorRowsToString();
  else note = outlinePreviewRowsToString();
  std::stringstream text;
  std::stringstream html;
  char *doc = nullptr;
  std::string title = O.rows.at(O.fr).title;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); //did something
  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";
  
  /*
  std::ofstream myfile;
  myfile.open(fn); //filename
  myfile << html.str().c_str();
  myfile.close();
  */

  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else outlineShowMessage("Couldn't lock file");
  } else outlineShowMessage("Couldn't open file");

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 
  //
  mkd_cleanup(blob);
  link_id = 0;
}

/* this zeromq version works but there is a problem on the ultralight
 * side -- LoadHTML doesn't seem to use the style sheet.  Will check on slack
 * if this is my mistake or intentional
 * */
void update_html_zmq(std::string &&fn) {
  std::string note = outlinePreviewRowsToString();
  std::stringstream text;
  std::stringstream html;
  std::string title = O.rows.at(O.fr).title;
  char *doc = nullptr;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); 

  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";

  zmq::message_t message(html.str().size()+1);

  // probably don't need snprint to get html into message
  snprintf ((char *) message.data(), html.str().size()+1, "%s", html.str().c_str()); 

  publisher.send(message, zmq::send_flags::dontwait);

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 

  mkd_cleanup(blob);
  link_id = 0;
}

void update_html_code_file(std::string &&fn) {
  std::ofstream myfile;
  myfile.open("code_file"); 
  myfile << outlinePreviewRowsToString();
  myfile.close();
  std::stringstream html;
  std::string line;

   procxx::process highlight("highlight", "code_file", "--out-format=html", 
                             "--style=gruvbox-dark-hard-slz", "--syntax=cpp");
    highlight.exec();
    while(getline(highlight.output(), line)) { html << line << '\n';}
    //while(getline(highlight.output(), line)) { html << line << "<br>";}

  /*
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  */
  
  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else outlineShowMessage("Couldn't lock file");
  } else outlineShowMessage("Couldn't open file");

}
void generate_persistent_html_file(int id) {
  std::string  fn = O.rows.at(O.fr).title.substr(0, 20);
    std::string illegal_chars = "\\/:?\"<>|\n ";
    for (auto& c: fn){
      if (illegal_chars.find(c) != std::string::npos) c = '_';
    }
  fn = fn + ".html";    
  html_files.insert(std::pair<int, std::string>(id, fn)); //could do html_files[id] = fn;
  update_html_file("assets/" + fn);

  std::string call = "./lm_browser " + fn;
  popen (call.c_str(), "r"); // returns FILE* id
  outlineShowMessage("Created file: %s and displayed with ultralight", system_call.c_str());
}

// postgresql functions - they come before sqlite but could move them after
std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int tid) {

  std::stringstream query;
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << tid << " =  task_keyword.task_id;";

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineShowMessage("Problem in get_task_keywords_pg!");
    PQclear(res);
    return std::make_pair(std::string(), std::vector<std::string>());
  }

  int rows = PQntuples(res);
  std::vector<std::string> task_keywords = {};
  for(int i=0; i<rows; i++) {
    task_keywords.push_back(PQgetvalue(res, i, 0));
  }
   std::string delim = "";
   std::string s = "";
   for (const auto &kw : task_keywords) {
     s += delim += kw;
     delim = ",";
   }
  PQclear(res);
  return std::make_pair(s, task_keywords);
  // PQfinish(conn);
}
/* Begin sqlite database functions */

void db_open(void) {
  int rc = sqlite3_open(SQLITE_DB.c_str(), &S.db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.db);
    exit(1);
  }

  rc = sqlite3_open(FTS_DB.c_str(), &S.fts_db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.fts_db);
    exit(1);
  }
}

void display_item_info_pg(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineShowMessage("Postgres Error: %s", PQerrorMessage(conn)); 
    PQclear(res);
    return;
  }    

  char lf_ret[10];
  //snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1);

  std::string s;

  //set background color to blue
  s.append("\n\n");
  s.append("\x1b[44m", 5);
  char str[300];

  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", PQgetvalue(res, 0, 0));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", PQgetvalue(res, 0, 3));
  s.append(str);
  s.append(lf_ret);

  int context_tid = atoi(PQgetvalue(res, 0, 6));
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works

  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", it->first.c_str());
  s.append(str);
  s.append(lf_ret);

  int folder_tid = atoi(PQgetvalue(res, 0, 5));
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mfolder:\x1b[0;44m %s", it2->first.c_str());
  s.append(str);
  s.append(lf_ret);

  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (*PQgetvalue(res, 0, 8) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 14) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 10)) ? "true": "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", PQgetvalue(res, 0, 16));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", PQgetvalue(res, 0, 9));
  s.append(str);
  s.append(lf_ret);

  std::string ss = get_task_keywords_pg(id).first;
  sprintf(str,"\x1b[1mkeywords:\x1b[0;44m %s", ss.c_str());
  s.append(str);
  s.append(lf_ret);

  //sprintf(str,"\x1b[1mtag:\x1b[0;44m %s", PQgetvalue(res, 0, 4));
  //s.append(str);
  //s.append(lf_ret);

  s.append("\x1b[0m");

  write(STDOUT_FILENO, s.c_str(), s.size());

  PQclear(res);
}
// end of pg functions

std::string now(void) {
  std::time_t t = std::time(nullptr);
  //int adj_t = (int)t - 14400;
  //return fmt::format("{:%Y-%m-%d %H:%M}", fmt::localtime(t));
  return fmt::format("{:%H:%M}", fmt::localtime(t));
}

std::string time_delta(std::string t) {
  struct std::tm tm = {};
  std::istringstream iss;
  //std::stringstream iss;
  iss.str(t);
  iss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
  auto now = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = now-tp; //in seconds but with stuff to right of decimal
  auto int_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed_seconds);
  //int adj_secs = (int)int_secs.count() + 14400; //time zone adjustment of 4 hours - needs to be cleaned up
  int adj_secs = (int)int_secs.count() + 3600; //time zone adjustment of 1 hour works - no idea why!

  std::string s;

  if (adj_secs <= 120) s = fmt::format("{} seconds ago", adj_secs);
  else if (adj_secs <= 60*120) s = fmt::format("{} minutes ago", adj_secs/60); // <120 minutes we report minutes
  else if (adj_secs <= 48*60*60) s = fmt::format("{} hours ago", adj_secs/3600); // <48 hours report hours
  else if (adj_secs <= 24*60*60*60) s = fmt::format("{} days ago", adj_secs/3600/24); // <60 days report days
  else if (adj_secs <= 24*30*24*60*60) s = fmt::format("{} months ago", adj_secs/3600/24/30); // <24 months report months
  else s = fmt::format("{} years ago", adj_secs/3600/24/30/12);
 
  return s;
}

bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     outlineShowMessage("SQL error: %s", errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg, const char *func) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     outlineShowMessage("SQL error in %s: %s", func, errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

void map_context_titles(void) {

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM context;");

  bool no_rows = true;
  if (!db_query(S.db, query.c_str(), context_titles_callback, &no_rows, &S.err_msg, __func__)) return;
  if (no_rows) outlineShowMessage("There were no context titles to map!");
}

int context_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  context_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void map_folder_titles(void) {

  // note it's tid because it's sqlite
  std::string query("SELECT tid,title FROM folder;");

  bool no_rows = true;
  if (!db_query(S.db, query.c_str(), folder_titles_callback, &no_rows, &S.err_msg, __func__)) return;
  if (no_rows) outlineShowMessage("There were no folder titles to map!");
}

int folder_titles_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc);
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  folder_map[std::string(argv[1])] = atoi(argv[0]);

  return 0;
}

void get_containers(void) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::string table;
  std::string column = "title"; //only needs to be change for keyword
  int (*callback)(void *, int, char **, char **);
  switch (O.view) {
    case CONTEXT:
      table = "context";
      callback = context_callback;
      break;
    case FOLDER:
      table = "folder";
      callback = folder_callback;
      break;
    case KEYWORD:
      table = "keyword";
      column = "name";
      callback = keyword_callback;
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }
  
  std::stringstream query;
  query << "SELECT * FROM " << table << " ORDER BY " << column  << " COLLATE NOCASE ASC;";

  bool no_rows = true;
  if (!db_query(S.db, query.str().c_str(), callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
  } else {
    //O.mode = NORMAL;
    O.mode = O.last_mode;
    display_container_info(O.rows.at(O.fr).id);
  }

  O.context = O.folder = O.keyword = ""; // this makes sense if you are not in an O.view == TASK
}

int context_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: "default" = Boolean ? what this is; sql has to use quotes to refer to column
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //"default"
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[9], 16);
  O.rows.push_back(row);

  return 0;
}

int folder_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  5: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //private
  row.deleted = (atoi(argv[7]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[11], 16);
  O.rows.push_back(row);

  return 0;
}

int keyword_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5:deleted
  */

  orow row;

  row.title = std::string(argv[1]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; 
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  strncpy(row.modified, argv[4], 16);
  O.rows.push_back(row);

  return 0;
}

std::pair<std::string, std::vector<std::string>> get_task_keywords(void) {

  std::stringstream query;
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << O.rows.at(O.fr).id << " =  task_keyword.task_id;";

   std::vector<std::string> task_keywords = {}; ////////////////////////////
   bool success =  db_query(S.db, query.str(), task_keywords_callback, &task_keywords, &S.err_msg);
   if (task_keywords.empty() || !success) return std::make_pair(std::string(), std::vector<std::string>());

   std::string delim = "";
   std::string s = "";
   for (const auto &kw : task_keywords) {
     s += delim += kw;
     delim = ",";
   }
   return std::make_pair(s, task_keywords);
}

int task_keywords_callback(void *ptr, int argc, char **argv, char **azColName) {

  std::vector<std::string>* task_keys = static_cast<std::vector<std::string> *>(ptr);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  task_keys->push_back(std::string(argv[0]));

  return 0; //you need this
}

//overload that takes keyword_id and task_id
void add_task_keyword(int keyword_id, int task_id) {

    std::stringstream query;
    query << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " 
          << task_id << ", keyword.id FROM keyword WHERE keyword.id = " 
          << keyword_id << ";";
    if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query2;
    // updates task modified column so we know that something changed with the task
    query2 << "UPDATE task SET modified = datetime('now', '-"
           << TZ_OFFSET << " hours') WHERE id =" << task_id << ";";
    if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    /**************fts virtual table update**********************/

    std::string s = get_task_keywords().first;
    std::stringstream query3;
    query3 << "Update fts SET tag='" << s << "' WHERE lm_id=" << task_id << ";";
    if (!db_query(S.fts_db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;
}

//void add_task_keyword(const std::string &kw, int id) {
//overload that takes keyword name and task_id
void add_task_keyword(std::string &kws, int id) {

  std::stringstream temp(kws);
  std::string phrase;
  std::vector<std::string> keyword_list;
  while(getline(temp, phrase, ',')) {
    keyword_list.push_back(phrase);
  }    

  for (std::string kw : keyword_list) {

    size_t pos = kw.find("'");
    while(pos != std::string::npos)
      {
        kw.replace(pos, 1, "''");
        pos = kw.find("'", pos + 2);
      }

    std::stringstream query;

    /*IF NOT EXISTS(SELECT 1 FROM keyword WHERE name = 'mango') INSERT INTO keyword (name) VALUES ('mango')
     * <- doesn't work for sqlite
     * note you don't have to do INSERT OR IGNORE but could just INSERT since unique constraint
     * on keyword.name but you don't want to trigger an error either so probably best to retain
     * INSERT OR IGNORE there is a default that tid = 0 but let's do it explicity*/

    query <<  "INSERT OR IGNORE INTO keyword (name, tid, star, modified, deleted) VALUES ('"
          <<  kw << "', " << 0 << ", true, datetime('now', '-" << TZ_OFFSET << " hours'), false);"; 

    if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query2;
    query2 << "INSERT INTO task_keyword (task_id, keyword_id) SELECT " << id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
    if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query3;
    // updates task modified column so we know that something changed with the task
    query3 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << id << ";";
    if (!db_query(S.db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    /**************fts virtual table update**********************/

    std::string s = get_task_keywords().first;
    std::stringstream query4;
    query4 << "Update fts SET tag='" << s << "' WHERE lm_id=" << id << ";";
    if (!db_query(S.fts_db, query4.str().c_str(), 0, 0, &S.err_msg, __func__)) return;
  }
}

int keyword_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT keyword.id from keyword WHERE keyword.name = '" << name << "';";
  int keyword_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &keyword_id, &S.err_msg, __func__)) return -1;
  return keyword_id;
}

/*
int keyword_id_callback(void *keyword_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(keyword_id);
  *id = atoi(argv[0]);
  return 0;
}
*/

// not in use but might have some use
int folder_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT folder.id from folder WHERE folder.name = '" << name << "';";
  int folder_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &folder_id, &S.err_msg, __func__)) return -1;
  return folder_id;
}

// not in use but might have some use
int context_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT context.id from context WHERE context.name = '" << name << "';";
  int context_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &context_id, &S.err_msg, __func__)) return -1;
  return context_id;
}

int container_id_callback(void *container_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(container_id);
  *id = atoi(argv[0]);
  return 0;
}
//void delete_task_keywords(void) {
void F_deletekeywords(int) {

  std::stringstream query;
  query << "DELETE FROM task_keyword WHERE task_id = " << O.rows.at(O.fr).id << ";";
  if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  std::stringstream query2;
  // updates task modified column so know that something changed with the task
  query2 << "UPDATE task SET modified = datetime('now', '-" << TZ_OFFSET << " hours') WHERE id =" << O.rows.at(O.fr).id << ";";
  if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  /**************fts virtual table update**********************/
  std::stringstream query3;
  query3 << "Update fts SET tag='' WHERE lm_id=" << O.rows.at(O.fr).id << ";";
  if (!db_query(S.fts_db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  outlineShowMessage("Keyword(s) for task %d will be deleted and fts searchdb updated", O.rows.at(O.fr).id);
  O.mode = O.last_mode;
}

void get_linked_items(int max) {
  std::vector<std::string> task_keywords = get_task_keywords().second;
  if (task_keywords.empty()) return;

  std::stringstream query;
  unique_ids.clear();

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

  for (auto it=task_keywords.begin(); it != task_keywords.end() - 1; ++it) {
    query << "keyword.name = '" << *it << "' OR ";
  }
  query << "keyword.name = '" << task_keywords.back() << "')";

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << O.sort
        << " DESC LIMIT " << max;

    int sortcolnum = sort_map[O.sort];
    if (!db_query(S.db, query.str().c_str(), unique_data_callback, &sortcolnum, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (O.rows.empty()) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

void get_items(int max) {
  std::stringstream query;
  std::vector<std::string> keyword_vec;
  int (*callback)(void *, int, char **, char **);
  callback = data_callback;

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  if (O.taskview == BY_CONTEXT) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " WHERE context.title = '" << O.context << "' ";
  } else if (O.taskview == BY_FOLDER) {
    query << "SELECT * FROM task JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE folder.title = '" << O.folder << "' ";
  } else if (O.taskview == BY_RECENT) {
    query << "SELECT * FROM task WHERE 1=1";
  } else if (O.taskview == BY_JOIN) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE context.title = '" << O.context << "'"
          << " AND folder.title = '" << O.folder << "'";
  } else if (O.taskview == BY_KEYWORD) {

 // if O.keyword has more than one keyword
    std::string k;
    std::stringstream skeywords;
    skeywords << O.keyword;
    while (getline(skeywords, k, ',')) {
      keyword_vec.push_back(k);
    }

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

    for (auto it = keyword_vec.begin(); it != keyword_vec.end() - 1; ++it) {
      query << "keyword.name = '" << *it << "' OR ";
    }
    query << "keyword.name = '" << keyword_vec.back() << "')";

    callback = unique_data_callback;
    unique_ids.clear();

  } else {
    outlineShowMessage("You asked for an unsupported db query");
    return;
  }

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << O.sort
        << " DESC LIMIT " << max;

  int sortcolnum = sort_map[O.sort];
  if (!db_query(S.db, query.str().c_str(), callback, &sortcolnum, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (O.rows.empty()) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
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


  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;

  //(argv[*static_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*static_cast<int*>(sortcolnum)], 16)
  //                                               : strncpy(row.modified, " ", 16);

  //the reason for below is I am thinking about changing row.modified to a std::string but that's lots of changes
  //wanted to show how it would work
  int *sc = static_cast<int*>(sortcolnum);
  std::string row_modified; //would be row.modified if changed to string
  (argv[*sc] != nullptr) ? row_modified.assign(argv[*sc], 16) : row_modified.assign(16, ' ');



  //int days = time_delta(row_modified);
  //strncpy(row.modified, std::to_string(days).c_str(), 16);

  strncpy(row.modified, time_delta(row_modified).c_str(), 16);

  //strncpy(row.modified, row_modified.c_str(), 16);
  O.rows.push_back(row);

  return 0;
}

int unique_data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int id = atoi(argv[0]);
  auto [it, success] = unique_ids.insert(id);
  if (!success) return 0;

  orow row;
  row.id = id;
  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;
  //(argv[*reinterpret_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*reinterpret_cast<int*>(sortcolnum)], 16)
  (argv[*static_cast<int*>(sortcolnum)] != nullptr) ? strncpy(row.modified, argv[*static_cast<int*>(sortcolnum)], 16)
                                                 : strncpy(row.modified, " ", 16);
  O.rows.push_back(row);

  return 0;
}

// called as part of :find -> search_db -> fts5_callback -> get_items_by_id -> by_id_data_callback
void get_items_by_id(std::stringstream &query) {
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */

  bool no_rows = true;
  if (!db_query(S.db, query.str().c_str(), by_id_data_callback, &no_rows, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    O.mode = NO_ROWS;
    eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = SEARCH;
    get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int by_id_data_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
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

  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.fts_title = fts_titles.at(row.id);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;
  row.dirty = false;
  row.mark = false;
  //if changed modified (which should be some generic time name) to string then row.modified.assign(argv[16], 16)
  strncpy(row.modified, argv[16], 16);
  O.rows.push_back(row);

  return 0;
}

void merge_note(int id) {
  std::stringstream query;

  query << "SELECT note FROM task WHERE id = " << id;

  if (!db_query(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg, __func__)) return;

  //int rc = sqlite3_exec(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg);

  //if (rc != SQLITE_OK ) {
  //  outlineShowMessage("In merge_note: %s SQL error: %s", FTS_DB.c_str(), S.err_msg);
  //  sqlite3_free(S.err_msg);
  //  sqlite3_close(S.db);
  //}
}

std::string get_title(int id) {
  std::string title;
  std::stringstream query;
  query << "SELECT title FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), title_callback, &title, &S.err_msg, __func__)) return std::string("SQL Problem");
  return title;
}

int title_callback (void *title, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  std::string *t = static_cast<std::string*>(title);
  *t = std::string(argv[0]);
  return 0;
}

void get_note(int id) {
  if (id ==-1) return; //maybe should be if (id < 0) and make all context id/tid negative

  std::stringstream query;
  query << "SELECT note FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), note_callback, nullptr, &S.err_msg, __func__)) return;

  /* this all needs to move to get_preview
  if (O.taskview != BY_SEARCH) {
    //p->editorRefreshScreen(true);
    //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
    if (lm_browser) {
      if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
      else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
    }   
    return;
  }

  std::stringstream query2;
  query2 << "SELECT rowid FROM fts WHERE lm_id = " << id << ";";

  int rowid = -1;
  // callback is *not* called if result (argv) is null
  if (!db_query(S.fts_db, query2.str().c_str(), rowid_callback, &rowid, &S.err_msg, __func__)) return;

  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);
  std::stringstream query3;
  int n = 0;
  for(auto v: vec) {
    word_positions.push_back(std::vector<int>{});
    query3.str(std::string()); // how you clear a stringstream
    query3 << "SELECT offset FROM fts_v WHERE doc =" << rowid << " AND term = '" << v << "' AND col = 'note';";
    if (!db_query(S.fts_db, query3.str().c_str(), offset_callback, &n, &S.err_msg, __func__)) return;

    n++;
  }

  int ww = (word_positions.at(0).empty()) ? -1 : word_positions.at(0).at(0);
  p->editorSetMessage("Word position first: %d; id = %d and row_id = %d", ww, id, rowid);

  p->editorRefreshScreen(true);
  //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
  */
}

// doesn't appear to be called if row is NULL
int note_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  if (!argv[0]) return 0; ////////////////////////////////////////////////////////////////////////////
  std::string note(argv[0]);
  note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    //snote will not contain the '\n'
    p->editorInsertRow(p->rows.size(), s);
  }

  p->dirty = 0;
  return 0;
}

int rowid_callback (void *rowid, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *rwid = static_cast<int*>(rowid);
  *rwid = atoi(argv[0]);
  return 0;
}

int offset_callback (void *n, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  int *nn= static_cast<int*>(n);

  word_positions.at(*nn).push_back(atoi(argv[0]));

  return 0;
}

//void fts5_sqlite(std::string search_terms) {
void search_db(std::string search_terms) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            //<< search_terms << "' ORDER BY rank";
            //<< search_terms << "' ORDER BY rank LIMIT " << 50;
            //<< search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0) LIMIT " << 50;
            << search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";

  fts_ids.clear();
  fts_titles.clear();
  fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query.str().c_str(), fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    eraseRightScreen(); //note can still return no rows from get_items_by_id if we found rows above that were deleted
    O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.id IN (";

  for (int i = 0; i < fts_counter-1; i++) {
    query << fts_ids[i] << ", ";
  }
  query << fts_ids[fts_counter-1]
        << ")"
        << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  for (int i = 0; i < fts_counter-1; i++) {
    query << "task.id = " << fts_ids[i] << " DESC, ";
  }
  query << "task.id = " << fts_ids[fts_counter-1] << " DESC";

  get_items_by_id(query);

  //outlineShowMessage(query.str().c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
  //outlineShowMessage(search_terms.c_str()); /////////////DEBUGGING///////////////////////////////////////////////////////////////////
}

//total kluge but just brings back context_tid = 16
void search_db2(std::string search_terms) {

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            << search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";

  fts_ids.clear();
  fts_titles.clear();
  fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query.str().c_str(), fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    outlineShowMessage("No results were returned");
    eraseRightScreen();
    O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.context_tid = 16 and task.id IN (";

  for (int i = 0; i < fts_counter-1; i++) {
    query << fts_ids[i] << ", ";
  }
  query << fts_ids[fts_counter-1]
        << ")"
        << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  for (int i = 0; i < fts_counter-1; i++) {
    query << "task.id = " << fts_ids[i] << " DESC, ";
  }
  query << "task.id = " << fts_ids[fts_counter-1] << " DESC";

  get_items_by_id(query);
}

int fts5_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  fts_ids.push_back(atoi(argv[0]));
  fts_titles[atoi(argv[0])] = std::string(argv[1]);
  fts_counter++;

  return 0;
}

int get_folder_tid(int id) {

  std::stringstream query;
  query << "SELECT folder_tid FROM task WHERE id = " << id;

  int folder_tid = -1;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg, __func__)) return -1;
  return folder_tid;
}

int folder_tid_callback(void *folder_tid, int argc, char **argv, char **azColName) {
  int *f_tid = static_cast<int*>(folder_tid);
  *f_tid = atoi(argv[0]);
  return 0;
}

void display_item_info(int id) {

  if (id ==-1) return;
  id = O.rows.at(O.fr).id;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  //int tid = 0;
  int tid;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg)) return;

  if (tid) display_item_info_pg(tid);
}

void display_item_info(void) {

  if (O.rows.empty()) return;

  int id = O.rows.at(O.fr).id;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  //int tid = 0;
  int tid;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), display_item_info_callback, &tid, &S.err_msg)) return;

  //if (tid) display_item_info_pg(tid); ////***** remember to remove this guard
}

int display_item_info_callback(void *tid, int argc, char **argv, char **azColName) {
    
  int *pg_id = static_cast<int*>(tid);

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
    
  /*
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
  */

  char lf_ret[10];
  //int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  eraseRightScreen();

  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  //set background color to blue
  //ab.append("\x1b[44m"); //blue is color 12 0;44 same as plain 44.
  //ab.append("\x1b[38;5;21m"); //this is color 17
  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  *pg_id = argv[1] ? atoi(argv[1]) : 0;
  sprintf(str,"tid: %d", *pg_id);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"title: %s", argv[3]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int context_tid = atoi(argv[6]);
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works
  sprintf(str,"context: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int folder_tid = atoi(argv[5]);
  auto it2 = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"folder: %s", it2->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star: %s", (atoi(argv[8]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"deleted: %s", (atoi(argv[14]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"completed: %s", (argv[10]) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"modified: %s", argv[16]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"added: %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  std::string s = get_task_keywords().first;
  sprintf(str,"keywords: %s", s.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  //sprintf(str,"tag: %s", argv[4]);
  //ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

void display_container_info(int id) {
  if (id ==-1) return;

  std::string table;
  std::string count_query;
  int (*callback)(void *, int, char **, char **);

  switch(O.view) {
    case CONTEXT:
      table = "context";
      callback = context_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN context ON context.tid = task.context_tid WHERE context.id = ";
      break;
    case FOLDER:
      table = "folder";
      callback = folder_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN folder ON folder.tid = task.folder_tid WHERE folder.id = ";
      break;
    case KEYWORD:
      table = "keyword";
      callback = keyword_info_callback;
      count_query = "SELECT COUNT(*) FROM task_keyword WHERE keyword_id = ";
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }
  std::stringstream query;
  int count = 0;

  query << count_query << id;
  int rc = sqlite3_exec(S.db, query.str().c_str(), count_callback, &count, &S.err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", S.err_msg);
    sqlite3_free(S.err_msg);
    sqlite3_close(S.db);
  } 

  std::stringstream query2;
  query2 << "SELECT * FROM " << table << " WHERE id = " << id;

  // callback is *not* called if result (argv) is null
 rc = sqlite3_exec(S.db, query2.str().c_str(), callback, &count, &S.err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("In display_container_info: %s SQL error: %s", FTS_DB.c_str(), S.err_msg);
    sqlite3_free(S.err_msg);
    sqlite3_close(S.fts_db);
  }
}

int count_callback (void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *cnt = static_cast<int*>(count);
  *cnt = atoi(argv[0]);
  return 0;
}

int context_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: "default" = Boolean ? what this is; sql has to use quotes to refer to column
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */
  char lf_ret[10];
  //int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < O.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);
  auto it = std::find_if(std::begin(context_map), std::end(context_map),
                         [&tid](auto& p) { return p.second == tid; }); //auto&& also works
  sprintf(str,"context: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/default: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int folder_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  5: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */
  char lf_ret[10];
  //int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < O.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);
  auto it = std::find_if(std::begin(folder_map), std::end(folder_map),
                         [&tid](auto& p) { return p.second == tid; }); //auto&& also works
  sprintf(str,"folder: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/private: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[6]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[7]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[11]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int keyword_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5: deleted
  */
  char lf_ret[10];
  //int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN); 
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf);

  //need to erase the screen
  for (int i=0; i < O.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"name: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m");

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

void update_note(void) {

  std::string text = p->editorRowsToString();
  std::stringstream query;

  // need to escape single quotes with two single quotes
  size_t pos = text.find("'");
  while(pos != std::string::npos) {
    text.replace(pos, 1, "''");
    pos = text.find("'", pos + 2);
  }

  //int id = get_id();

  //query << "UPDATE task SET note='" << text << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;
  query << "UPDATE task SET note='" << text << "', modified=datetime('now', '-" << TZ_OFFSET << " hours'), "
        << "startdate=datetime('now', '-" << TZ_OFFSET << " hours')WHERE id=" << p->id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Updated note for item %d", p->id);
    outlineRefreshScreen();
  }

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  rc = sqlite3_open(FTS_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  std::stringstream query2;
  //query.clear(); //this clear clears eof and fail flags so query.str(std::string());query.clear()
  query2 << "Update fts SET note='" << text << "' WHERE lm_id=" << p->id;

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL fts error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Updated note and fts entry for item %d", p->id);
    outlineRefreshScreen();
    p->editorSetMessage("Note update succeeeded"); 
  }
   
  sqlite3_close(db);

  p->dirty = 0;
}

void update_task_context(std::string &new_context, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int context_tid = context_map.at(new_context);
  query << "UPDATE task SET context_tid=" << context_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Setting context to %s succeeded", new_context.c_str());
  }

  sqlite3_close(db);
}

void update_task_folder(std::string& new_folder, int id) {

  std::stringstream query;
  //id = (id == -1) ? get_id() : id; //get_id should probably just be replaced by O.rows.at(O.fr).id
  int folder_tid = folder_map.at(new_folder);
  query << "UPDATE task SET folder_tid=" << folder_tid << ", modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Setting folder to %s succeeded", new_folder.c_str());
  }

  sqlite3_close(db);
}

void toggle_completed(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET completed=" << ((row.completed) ? "NULL" : "date()") << ", "
        << "modified=datetime('now', '-" << TZ_OFFSET << " hours') "
        << "WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle completed succeeded");
    row.completed = !row.completed;
  }

  sqlite3_close(db);
    
}

void touch(void) {

  std::stringstream query;
  int id = get_id();

  query << "UPDATE task SET modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("'Touch' succeeded");
  }

  sqlite3_close(db);

}

void toggle_deleted(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table;

  switch(O.view) {
    case TASK:
      table = "task";
      break;
    case CONTEXT:
      table = "context";
      break;
    case FOLDER:
      table = "folder";
      break;
    case KEYWORD:
      table = "keyword";
      break;
    default:
      outlineShowMessage("Somehow you are in a view I can't handle");
      return;
  }

  query << "UPDATE " << table << " SET deleted=" << ((row.deleted) ? "False" : "True") << ", "
        <<  "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  // ? whether should move all tasks out here or in sync
  // UPDATE task SET folder_tid = 1 WHERE folder_tid = 4;
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle deleted succeeded");
    row.deleted = !row.deleted;
  }

  sqlite3_close(db);

}

void toggle_star(void) {

  orow& row = O.rows.at(O.fr);

  std::stringstream query;
  int id = get_id();
  std::string table;
  std::string column;

  switch(O.view) {

    case TASK:
      table = "task";
      column = "star";
      break;

    case CONTEXT:
      table = "context";
      column = "\"default\"";
      break;

    case FOLDER:
      table = "folder";
      column = "private";
      break;

    case KEYWORD:
      table = "keyword";
      column = "star";
      break;

    default:
      outlineShowMessage("Not sure what you're trying to toggle");
      return;
  }

  query << "UPDATE " << table << " SET " << column << "=" << ((row.star) ? "False" : "True") << ", "
        << "modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << id; //tid

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
    
  if (rc != SQLITE_OK) {
        
    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error: %s", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineShowMessage("Toggle star succeeded");
    row.star = !row.star;
  }
  sqlite3_close(db);
}

void update_row(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE task SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id;

    sqlite3 *db;
    char *err_msg = 0;
      
    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
      
    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);
    
    /**************fts virtual table update**********************/

    rc = sqlite3_open(FTS_DB.c_str(), &db);
    if (rc != SQLITE_OK) {
          
      outlineShowMessage("Cannot open fts database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }
  
    std::stringstream query2;
    query2 << "UPDATE fts SET title='" << title << "' WHERE lm_id=" << row.id;
    rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);
      
    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
      } else {
        outlineShowMessage("Updated title and fts title entry for item %d", row.id);
      }
  
      sqlite3_close(db);

  } else { //row.id == -1
    insert_row(row);
  }
}

void update_container(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE "
          << ((O.view == CONTEXT) ? "context" : "folder")
          << " SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id; //I think id is correct

    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }

    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);

  } else { //row.id == -1
    insert_container(row);
  }
}

void update_keyword(void) {

  orow& row = O.rows.at(O.fr);

  if (!row.dirty) {
    outlineShowMessage("Row has not been changed");
    return;
  }

  if (row.id != -1) {
    std::string title = row.title;
    size_t pos = title.find("'");
    while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

    std::stringstream query;
    query << "UPDATE "
          << "keyword "
          << "SET name='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id; //I think id is correct

    sqlite3 *db;
    char *err_msg = 0;

    int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

    if (rc != SQLITE_OK) {
      outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
    }

    rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

    if (rc != SQLITE_OK ) {
      outlineShowMessage("SQL error: %s", err_msg);
      sqlite3_free(err_msg);
    } else {
      row.dirty = false;
      outlineShowMessage("Successfully updated row %d", row.id);
    }

    sqlite3_close(db);

  } else { //row.id == -1
    insert_keyword(row);
  }
}

int insert_keyword(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO "
        << "keyword "
        << "("
        << "name, "
        << "star, "
        << "deleted, "
        << "modified, "
        << "tid"
        << ") VALUES ("
        << "'" << title << "'," //title
        << " " << row.star << ","
        << " False," //default for context and private for folder
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //modified
        //<< " " << temporary_tid << ");"; //tid originally 100 but that is a legit client tid server id
        << " " << 0 << ");"; //unproven belief that you don't have to have multiple tids if you insert multiple keywords

  //temporary_tid++;

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  outlineShowMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

int insert_row(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO task ("
        << "priority, "
        << "title, "
        << "folder_tid, "
        << "context_tid, "
        << "star, "
        << "added, "
        << "note, "
        << "deleted, "
        << "created, "
        << "modified "
        //<< "startdate "
        << ") VALUES ("
        << " 3," //priority
        << "'" << title << "'," //title
        //<< " 1," //folder_tid
        << ((O.folder == "") ? 1 : folder_map.at(O.folder)) << ", "
        //<< ((O.context != "search") ? context_map.at(O.context) : 1) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        //<< ((O.context == "search" || O.context == "recent" || O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << ((O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << " True," //star
        << "date()," //added
        //<< "'<This is a new note from sqlite>'," //note
        << "''," //note
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now', '-" << TZ_OFFSET << " hours')" // modified
        //<< " date()" //startdate
        << ");"; // RETURNING id;",

  /*
    not used:
    tid,
    tag,
    duetime,
    completed,
    duedate,
    repeat,
    remind
  */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  //should probably create a separate function that is a klugy
  //way of making up for fact that pg created tasks don't appear in fts db
  //"INSERT OR IGNORE INTO fts (title, lm_id) VALUES ('" << title << row.id << ");";

  std::stringstream query2;
  query2 << "INSERT INTO fts (title, lm_id) VALUES ('" << title << "', " << row.id << ")";

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    outlineShowMessage("Cannot open FTS database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return row.id;
  }

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing FTS insert: %s", err_msg);
    sqlite3_free(err_msg);
    return row.id; // would mean regular insert succeeded and fts failed - need to fix this
  }
  sqlite3_close(db);
  outlineShowMessage("Successfully inserted new row with id %d and indexed it", row.id);

  return row.id;
}

int insert_container(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO "
        //<< context
        << ((O.view == CONTEXT) ? "context" : "folder")
        << " ("
        << "title, "
        << "deleted, "
        << "created, "
        << "modified, "
        << "tid, "
        << ((O.view == CONTEXT) ? "\"default\", " : "private, ") //context -> "default"; folder -> private
      //  << "\"default\", " //folder does not have default
        << "textcolor "
        << ") VALUES ("
        << "'" << title << "'," //title
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now', '-" << TZ_OFFSET << " hours')," // modified
        //<< " 99999," //tid
        << " " << temporary_tid << "," //tid originally 100 but that is a legit client tid server id
        << " False," //default for context and private for folder
        << " 10" //textcolor
        << ");"; // RETURNING id;",

  temporary_tid++;      
  /*
   * not used:
     "default" (not sure why in quotes but may be system variable
      tid,
      icon (varchar 32)
      image (blob)
    */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    outlineShowMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  outlineShowMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

void update_rows(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  for (auto row: O.rows) {
    if (!(row.dirty)) continue;
    if (row.id != -1) {
      std::string title = row.title;
      size_t pos = title.find("'");
      while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

      std::stringstream query;
      query << "UPDATE task SET title='" << title << "', modified=datetime('now', '-" << TZ_OFFSET << " hours') WHERE id=" << row.id;

      sqlite3 *db;
      char *err_msg = 0;
        
      int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
        
      if (rc != SQLITE_OK) {
            
        outlineShowMessage("Cannot open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
        }
    
      rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

      if (rc != SQLITE_OK ) {
        outlineShowMessage("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return; // ? should we abort all other rows
      } else {
        row.dirty = false;
        updated_rows[n] = row.id;
        n++;
        sqlite3_close(db);
      }
    
    } else { 
      int id  = insert_row(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineShowMessage("There were no rows to update");
    return;
  }

  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    O.rows.at(updated_rows[j]).dirty = false; // 10-28-2019
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  outlineShowMessage("%s",  msg);
}

void update_solr(void) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("update_solr"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "update_solr"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(0); //presumably PyTuple_New(x) creates a tuple with that many elements
          //pValue = PyLong_FromLong(1);
          //pValue = Py_BuildValue("s", search_terms); // **************
          //PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineShowMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineShowMessage("Problem retrieving ids from solr!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineShowMessage("Was not able to find the function: update_solr!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      PyErr_Print();
      outlineShowMessage("Was not able to find the module: update_solr!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

  outlineShowMessage("%d items were added/updated to solr db", num);
}

[[ noreturn]] void die(const char *s) {
  // write is from <unistd.h> 
  //ssize_t write(int fildes, const void *buf, size_t nbytes);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; // minimum data to receive?
  raw.c_cc[VTIME] = 1; // timeout for read will return 0 if no bytes read

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int readKey() {
  int nread;
  char c;

  /* read is from <unistd.h> - not sure why read is used and not getchar <stdio.h>
   prototype is: ssize_t read(int fd, void *buf, size_t count); 
   On success, the number of bytes read is returned (zero indicates end of file)
   So the while loop below just keeps cycling until a byte is read
   it does check to see if there was an error (nread == -1)*/

   /*Note that ctrl-key maps to ctrl-a => 1, ctrl-b => 2 etc.*/

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  /* if the character read was an escape, need to figure out if it was
     a recognized escape sequence or an isolated escape to switch from
     insert mode to normal mode or to reset in normal mode
  */

  if (c == '\x1b') {
    char seq[3];
    //outlineShowMessage("You pressed %d", c); //slz
    // the reads time out after 0.1 seconds
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Assumption is that seq[0] == '[' 
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //need 4 bytes
      if (seq[2] == '~') {
        //outlineShowMessage("You pressed %c%c%c", seq[0], seq[1], seq[2]); //slz
        switch (seq[1]) {
          case '1': return HOME_KEY; //not being issued
          case '3': return DEL_KEY; //<esc>[3~
          case '4': return END_KEY;  //not being issued
          case '5': return PAGE_UP; //<esc>[5~
          case '6': return PAGE_DOWN;  //<esc>[6~
          case '7': return HOME_KEY; //not being issued
          case '8': return END_KEY;  //not being issued
        }
      }
    } else {
        //outlineShowMessage("You pressed %c%c", seq[0], seq[1]); //slz
        switch (seq[1]) {
          case 'A': return ARROW_UP; //<esc>[A
          case 'B': return ARROW_DOWN; //<esc>[B
          case 'C': return ARROW_RIGHT; //<esc>[C
          case 'D': return ARROW_LEFT; //<esc>[D
          case 'H': return HOME_KEY; // <esc>[H - this one is being issued
          case 'F': return END_KEY;  // <esc>[F - this one is being issued
          case 'Z': return SHIFT_TAB; //<esc>[Z
      }
    }

    return '\x1b'; // if it doesn't match a known escape sequence like ] ... or O ... just return escape
  
  } else {
    //outlineShowMessage("You pressed %d", c); //slz
    return c;
  }
}

int getWindowSize(int *rows, int *cols) {

  // ioctl(), TIOCGWINXZ and struct windsize come from <sys/ioctl.h>
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;

    return 0;
  }
}

/**** Outline COMMAND mode functions ****/
void F_open(int pos) { //C_open - by context
  std::string new_context;
  if (pos) {
    bool success = false;
    //structured bindings
    for (const auto & [k,v] : context_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.context = k;
        success = true;
        break;
      }
    }

    if (!success) {
      outlineShowMessage("%s is not a valid  context!", &O.command_line.c_str()[pos + 1]);
      O.mode = NORMAL;
      return;
    }

  } else {
    outlineShowMessage("You did not provide a context!");
    O.mode = NORMAL;
    return;
  }
  outlineShowMessage("\'%s\' will be opened", O.context.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);

  marked_entries.clear();
  O.folder = "";
  O.taskview = BY_CONTEXT;
  get_items(MAX);
  //O.mode = O.last_mode;
  O.mode = NORMAL;
  return;
}

void F_openfolder(int pos) {
  if (pos) {
    bool success = false;
    for (const auto & [k,v] : folder_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.folder = k;
        success = true;
        break;
      }
    }
    if (!success) {
      outlineShowMessage("%s is not a valid  folder!", &O.command_line.c_str()[pos + 1]);
      O.mode = NORMAL;
      return;
    }

  } else {
    outlineShowMessage("You did not provide a folder!");
    O.mode = NORMAL;
    return;
  }
  outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  marked_entries.clear();
  O.context = "";
  O.taskview = BY_FOLDER;
  get_items(MAX);
  O.mode = NORMAL;
  return;
}

void F_openkeyword(int pos) {
  if (!pos) {
    outlineShowMessage("You need to provide a keyword");
    O.mode = NORMAL;
    return;
  }
 
  //O.keyword = O.command_line.substr(pos+1);
  std::string keyword = O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
    O.mode = O.last_mode;
    outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
    return;
  }

  O.keyword = keyword;  
  outlineShowMessage("\'%s\' will be opened", O.keyword.c_str());
  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  marked_entries.clear();
  O.context = "";
  O.folder = "";
  O.taskview = BY_KEYWORD;
  get_items(MAX);
  O.mode = NORMAL;
  return;
}

void F_addkeyword(int pos) {
  if (!pos) {
    current_task_id = O.rows.at(O.fr).id;
    eraseRightScreen();
    O.view = KEYWORD;
    command_history.push_back(O.command_line);
    get_containers(); //O.mode = NORMAL is in get_containers
    O.mode = ADD_CHANGE_FILTER;
    outlineShowMessage("Select keyword to add to marked or current entry");
    return;
  }

  // only do this if there was text after C_addkeyword
  if (O.last_mode == NO_ROWS) return;

  {
  std::string keyword = O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
      O.mode = O.last_mode;
      outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
      return;
  }

  if (marked_entries.empty()) {
    add_task_keyword(keyword, O.rows.at(O.fr).id);
    outlineShowMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : marked_entries) {
      add_task_keyword(keyword, id);
    }
    outlineShowMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  }
  O.mode = O.last_mode;
  return;
}

void F_keywords(int pos) {
  if (!pos) {
    eraseRightScreen();
    O.view = KEYWORD;
    command_history.push_back(O.command_line); 
    get_containers(); //O.mode = NORMAL is in get_containers
    outlineShowMessage("Retrieved keywords");
    return;
  }  

  // only do this if there was text after C_keywords
  if (O.last_mode == NO_ROWS) return;

  {
  std::string keyword = O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
      O.mode = O.last_mode;
      outlineShowMessage("keyword '%s' does not exist!", keyword.c_str());
      return;
  }

  if (marked_entries.empty()) {
    add_task_keyword(keyword, O.rows.at(O.fr).id);
    outlineShowMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : marked_entries) {
      add_task_keyword(keyword, id);
    }
    outlineShowMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  }
  O.mode = O.last_mode;
  return;
}

void F_write(int) {
  if (O.view == TASK) update_rows();
  O.mode = O.last_mode;
  O.command_line.clear();
}

void F_x(int) {
  if (O.view == TASK) update_rows();
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //sends cursor home (upper left)
  exit(0);
}

void F_refresh(int) {
  if (O.view == TASK) {
    outlineShowMessage("Steve, tasks will be refreshed");
    if (O.taskview == BY_SEARCH)
      ;//search_db();
    else
      get_items(MAX);
  } else {
    outlineShowMessage("contexts/folders will be refreshed");
    get_containers();
  }
  O.mode = O.last_mode;
}

void F_new(int) {
  outlineInsertRow(0, "", true, false, false, now().c_str());
  O.fc = O.fr = O.rowoff = 0;
  O.command[0] = '\0';
  O.repeat = 0;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
  eraseRightScreen(); //erases the note area
  O.mode = INSERT;

  int fd;
  std::string fn = "assets/" + CURRENT_NOTE_FILE;
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, " ", 1);
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else outlineShowMessage("Couldn't lock file");
  } else outlineShowMessage("Couldn't open file");
}

//this is the main event - right now only way to initiate editing an entry
void F_edit(int) {
  
  if (!(O.view == TASK)) {
    O.command[0] = '\0';
    O.mode = NORMAL;
    outlineShowMessage("Only tasks have notes to edit!");
    return;
  }

  int id = get_id();
  if (id == -1) {
    outlineShowMessage("You need to save item before you can create a note");
    O.command[0] = '\0';
    O.mode = NORMAL;
    return;
  }

  outlineShowMessage("Edit note %d", id);
  outlineRefreshScreen();
  editor_mode = true;

  if (!editors.empty()){
    auto it = std::find_if(std::begin(editors), std::end(editors),
                       [&id](auto& pp) { return pp->id == id; }); //auto&& also works

    if (it == editors.end()) {
      //Editor E;
      //editors.push_back(&E);
      p = new Editor;
      editors.push_back(p);
      //editors.push_back(std::make_shared<Editor>(E));
      //p = editors.back();
      p->id = id;
      get_note(id); //if id == -1 does not try to retrieve note
    } else {
      p = *it;
    }    
  } else {
    //Editor E;
    p = new Editor;
    //editors.push_back(&E);
    editors.push_back(p);
    //editors.push_back(std::make_shared<Editor>(E));
    //p = editors.back();
    p->id = id;
    get_note(id); //if id == -1 does not try to retrieve note
    p->snapshot = p->rows; ////undo-related
 }

  int n = editors.size();
  int i = 0;
  for (auto z : editors) {
    z->screenlines = screenlines - 2 - TOP_MARGIN;
    z->screencols = -1 + (screencols - O.divider)/n;
    z->total_screenlines = screenlines - 2 - TOP_MARGIN;
    z->left_margin = O.divider + i*z->screencols + i;
    i++;
  }

  if (p->rows.empty()) {
    p->mode = INSERT;
    p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
    //p->editorRefreshScreen(false);
  } else {
    p->mode = NORMAL;
  }

  eraseRightScreen();
  //for (auto e : editors) e->editorRefreshScreen(true);


  
  /*********************************/
  std::string ab;
  //for (auto e : editors) {
  for (int i=0; i<editors.size(); i++) {  
    Editor *&e = editors.at(i);
    e->editorRefreshScreen(true);
    //char buf[32];
    std::string buf;
    ab.append("\x1b(0"); // Enter line drawing mode
    for (int j=1; j<O.screenlines+1; j++) {
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, e->left_margin + e->screencols+1); 
      buf = fmt::format("\x1b[{};{}H", TOP_MARGIN + j, e->left_margin +e->screencols+1);
      ab.append(buf);
      // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
      // only need one 'm'
      ab.append("\x1b[37;1mx");
    }
    //'T' corner = w or right top corner = k
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, e->left_margin + e->screencols+1); //may not need offset
    buf = fmt::format("\x1b[{};{}H", TOP_MARGIN, e->left_margin + e->screencols+1); //may not need offset
    ab.append(buf);
    if (i == editors.size() - 1) ab.append("\x1b[37;1mk");
    else ab.append("\x1b[37;1mw");

    //exit line drawing mode
    ab.append("\x1b(B");
  }
  ab.append("\x1b[?25h", 6); //shows the cursor
  ab.append("\x1b[0m"); //or else subsequent editors are bold
  write(STDOUT_FILENO, ab.c_str(), ab.size());
  /*********************************/

  O.command[0] = '\0';
  O.mode = NORMAL;
}

void F_contexts(int pos) {
  if (!pos) {
    eraseRightScreen();
    O.view = CONTEXT;
    command_history.push_back(O.command_line); ///////////////////////////////////////////////////////
    get_containers();
    O.mode = NORMAL;
    outlineShowMessage("Retrieved contexts");
    return;
  } else {

    std::string new_context;
    bool success = false;
    if (O.command_line.size() > 5) { //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : context_map) {
        if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_context = k;
          success = true;
          break;
        }
      }
      if (!success) {
        outlineShowMessage("What you typed did not match any context");
        O.mode = NORMAL;
        return;
      }

    } else {
      outlineShowMessage("You need to provide at least 3 characters "
                        "that match a context!");

      O.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : O.rows) {
      if (it.mark) {
        update_task_context(new_context, it.id);
        success = true;
      }
    }

    if (success) {
      outlineShowMessage("Marked tasks moved into context %s", new_context.c_str());
    } else {
      update_task_context(new_context, O.rows.at(O.fr).id);
      outlineShowMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
    }
    O.mode = O.last_mode;
    return;
  }
}

void F_folders(int pos) {
  if (!pos) {
    eraseRightScreen();
    O.view = FOLDER;
    command_history.push_back(O.command_line); 
    get_containers();
    O.mode = NORMAL;
    outlineShowMessage("Retrieved folders");
    return;
  } else {

    std::string new_folder;
    bool success = false;
    if (O.command_line.size() > 5) {  //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : folder_map) {
        if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_folder = k;
          success = true;
          break;
        }
      }
      if (!success) {
        outlineShowMessage("What you typed did not match any folder");
        O.mode = NORMAL;
        return;
      }

    } else {
      outlineShowMessage("You need to provide at least 3 characters "
                        "that match a folder!");

      O.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : O.rows) {
      if (it.mark) {
        update_task_folder(new_folder, it.id);
        success = true;
      }
    }

    if (success) {
      outlineShowMessage("Marked tasks moved into folder %s", new_folder.c_str());
    } else {
      update_task_folder(new_folder, O.rows.at(O.fr).id);
      outlineShowMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
    }
    O.mode = O.last_mode;
    return;
  }
}

void F_recent(int) {
  outlineShowMessage("Will retrieve recent items");
  command_history.push_back(O.command_line);
  page_history.push_back(O.command_line);
  page_hx_idx = page_history.size() - 1;
  marked_entries.clear();
  O.context = "No Context";
  O.taskview = BY_RECENT;
  O.folder = "No Folder";
  get_items(MAX);
}

void F_linked(int) {
  std::string keywords = get_task_keywords().first;
  if (keywords.empty()) {
    outlineShowMessage("The current entry has no keywords");
  } else {
    O.keyword = keywords;
    O.context = "No Context";
    O.folder = "No Folder";
    O.taskview = BY_KEYWORD;
    get_linked_items(MAX);
    command_history.push_back("ok " + keywords); 
  }
}

void F_find(int pos) {
  if (O.command_line.size() < 6) {
    outlineShowMessage("You need more characters");
    return;
  }  
  O.context = "";
  O.folder = "";
  O.taskview = BY_SEARCH;
  //O.mode = SEARCH; ////// it's in get_items_by_id
  search_terms = O.command_line.substr(pos+1);
  std::transform(search_terms.begin(), search_terms.end(), search_terms.begin(), ::tolower);
  command_history.push_back(O.command_line); 
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  outlineShowMessage("Searching for %s", search_terms.c_str());
  search_db(search_terms);
}

void F_sync(int) {
  synchronize(0); // do actual sync
  map_context_titles();
  map_folder_titles();
  initial_file_row = 0; //for arrowing or displaying files
  O.mode = FILE_DISPLAY; // needs to appear before displayFile
  outlineShowMessage("Synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_sync_test(int) {
  synchronize(1); //1 -> report_only
  initial_file_row = 0; //for arrowing or displaying files
  O.mode = FILE_DISPLAY; // needs to appear before displayFile
  outlineShowMessage("Testing synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_updatecontext(int) {
  current_task_id = O.rows.at(O.fr).id;
  eraseRightScreen();
  O.view = CONTEXT;
  command_history.push_back(O.command_line); 
  get_containers(); //O.mode = NORMAL is in get_containers
  O.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
  outlineShowMessage("Select context to add to marked or current entry");
}

void F_updatefolder(int) {
  current_task_id = O.rows.at(O.fr).id;
  eraseRightScreen();
  O.view = FOLDER;
  command_history.push_back(O.command_line); 
  get_containers(); //O.mode = NORMAL is in get_containers
  O.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
  outlineShowMessage("Select context to add to marked or current entry");
}

void F_delmarks(int) {
  for (auto& it : O.rows) {
    it.mark = false;}
  if (O.view == TASK) marked_entries.clear(); //why the if??
  O.mode = O.last_mode;
  outlineShowMessage("Marks all deleted");
}

// to avoid confusion should only be an editor command line function
void F_savefile(int pos) {
  command_history.push_back(O.command_line);
  std::string filename;
  if (pos) filename = O.command_line.substr(pos+1);
  else filename = "example.cpp";
  p->editorSaveNoteToFile(filename);
  outlineShowMessage("Note saved to file: %s", filename.c_str());
  O.mode = NORMAL;
}

//this really needs work - needs to be picked like keywords, folders etc.
void F_sort(int pos) { 
  if (pos && O.view == TASK && O.taskview != BY_SEARCH) {
    O.sort = O.command_line.substr(pos + 1);
    get_items(MAX);
    outlineShowMessage("sorted by \'%s\'", O.sort.c_str());
  } else {
    outlineShowMessage("Currently can't sort search, which is sorted on best match");
  }
}

void  F_showall(int) {
  if (O.view == TASK) {
    O.show_deleted = !O.show_deleted;
    O.show_completed = !O.show_completed;
    if (O.taskview == BY_SEARCH)
      ; //search_db();
    else
      get_items(MAX);
  }
  outlineShowMessage((O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
}


// does not seem to work
void F_syntax(int pos) {
  if (pos) {
    std::string action = O.command_line.substr(pos + 1);
    if (action == "on") {
      p->highlight_syntax = true;
      outlineShowMessage("Syntax highlighting will be turned on");
    } else if (action == "off") {
      p->highlight_syntax = false;
      outlineShowMessage("Syntax highlighting will be turned off");
    } else {outlineShowMessage("The syntax is 'sh on' or 'sh off'"); }
  } else {outlineShowMessage("The syntax is 'sh on' or 'sh off'");}
  p->editorRefreshScreen(true);
  O.mode = NORMAL;
}

// set spell | set nospell
// should also only be an editor function
void F_set(int pos) {
  std::string action = O.command_line.substr(pos + 1);
  if (pos) {
    if (action == "spell") {
      p->spellcheck = true;
      outlineShowMessage("Spellcheck active");
    } else if (action == "nospell") {
      p->spellcheck = false;
      outlineShowMessage("Spellcheck off");
    } else {outlineShowMessage("Unknown option: %s", action.c_str()); }
  } else {outlineShowMessage("Unknown option: %s", action.c_str());}
  p->editorRefreshScreen(true);
  O.mode = NORMAL;
}

// also should be only editor function
void F_open_in_vim(int) {
  open_in_vim(); //send you into editor mode
  p->mode = NORMAL;
  //O.command[0] = '\0';
  //O.repeat = 0;
  //O.mode = NORMAL;
}

void F_join(int pos) {
  if (O.view != TASK || O.taskview == BY_JOIN || pos == 0) {
    outlineShowMessage("You are either in a view where you can't join or provided no join container");
    O.mode = NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    O.mode = O.last_mode; //NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    return;
  }
  bool success = false;

  if (O.taskview == BY_CONTEXT) {
    for (const auto & [k,v] : folder_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.folder = k;
        success = true;
        break;
      }
    }
  } else if (O.taskview == BY_FOLDER) {
    for (const auto & [k,v] : context_map) {
      if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        O.context = k;
        success = true;
        break;
      }
    }
  }
  if (!success) {
    outlineShowMessage("You did not provide a valid folder or context to join!");
    O.command_line.resize(1);
    return;
  }

  outlineShowMessage("Will join \'%s\' with \'%s\'", O.folder.c_str(), O.context.c_str());
  O.taskview = BY_JOIN;
  get_items(MAX);
  return;
}

void F_saveoutline(int pos) { 
  if (pos) {
    std::string fname = O.command_line.substr(pos + 1);
    outlineSave(fname);
    O.mode = NORMAL;
    outlineShowMessage("Saved outline to %s", fname.c_str());
  } else {
    outlineShowMessage("You didn't provide a file name!");
  }
}

// should be only an editor function
void F_readfile(int pos) {
  std::string filename;
  if (pos) filename = O.command_line.substr(pos+1);
  else filename = "example.cpp";
  p->editorReadFileIntoNote(filename);
  outlineShowMessage("Note generated from file: %s", filename.c_str());
  O.mode = NORMAL;
}

void F_persist(int pos) {
  generate_persistent_html_file(O.rows.at(O.fr).id);
  O.mode = NORMAL;
}

void F_valgrind(int) {
  initial_file_row = 0; //for arrowing or displaying files
  readFile("valgrind_log_file");
  displayFile();//put them in the command mode case synch
  O.last_mode = O.mode;
  O.mode = FILE_DISPLAY;
}

//probably should be removed altogether
void F_merge(int) {
  int count = count_if(O.rows.begin(), O.rows.end(), [](const orow &row){return row.mark;});
  if (count < 2) {
    outlineShowMessage("Number of marked items = %d", count);
    O.mode = O.last_mode;
    return;
  }
  outlineInsertRow(0, "[Merged note]", true, false, false, BASE_DATE);
  insert_row(O.rows.at(0)); 
  p->rows.clear();
  
  int n = 0;
  auto it = O.rows.begin();
  for(;;) {
    it = find_if(it+1, O.rows.end(), [](const orow &row){return row.mark;});
    if (it != O.rows.end()) merge_note(it->id);
    else break;
    n++;
  }
  outlineShowMessage("Number of notes merged = %d", n);
  O.fc = O.fr = O.rowoff = 0; //O.fr = 0 needs to come before update_note
  p->editorRefreshScreen(true);
  update_note();
  O.command[0] = '\0';
  O.repeat = 0;
  O.mode = NORMAL;
}

void F_help(int pos) {
  if (!pos) {             
    /*This needs to be changed to show database text not ext file*/
    initial_file_row = 0;
    O.last_mode = O.mode;
    O.mode = FILE_DISPLAY;
    outlineShowMessage("Displaying help file");
    readFile("listmanager_commands");
    displayFile();
  } else {
    search_terms = O.command_line.substr(pos+1);
    O.context = "";
    O.folder = "";
    O.taskview = BY_SEARCH;
    //O.mode = SEARCH; ////// it's in get_items_by_id
    std::transform(search_terms.begin(), search_terms.end(), search_terms.begin(), ::tolower);
    command_history.push_back(O.command_line); 
    search_db2(search_terms);
    outlineShowMessage("Will look for help on %s", search_terms.c_str());
    //O.mode = NORMAL;
  }  
}

//case 'q':
void F_quit_app(int) {
  bool unsaved_changes = false;
  for (auto it : O.rows) {
    if (it.dirty) {
      unsaved_changes = true;
      break;
    }
  }
  if (unsaved_changes) {
    O.mode = NORMAL;
    outlineShowMessage("No db write since last change");
  } else {
    write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
    write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
    Py_FinalizeEx();
    if (which_db == SQLITE) sqlite3_close(S.db);
    else PQfinish(conn);
    exit(0);
  }
}

void F_quit_app_ex(int) {
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
  Py_FinalizeEx();
  exit(0);
}

/* need to look at this */
void F_clear(int) {
  html_files.clear();
  p->mode = NORMAL;
  p->command[0] = '\0';
  p->command_line.clear();
  p->editorSetMessage("");
}

/* OUTLINE NORMAL mode functions */
void goto_editor_N(void) {
  if (editors.empty()) {
    outlineShowMessage("There are no active editors");
    return;
  }

  eraseRightScreen();
  for (auto &e : editors) e->editorRefreshScreen(true);
  editor_mode = true;
}

void return_N(void) {
  orow& row = O.rows.at(O.fr);

  if(row.dirty){
    if (O.view == TASK) update_row();
    else if (O.view == CONTEXT || O.view == FOLDER) update_container();
    else if (O.view == KEYWORD) update_keyword();
    O.command[0] = '\0'; //11-26-2019
    O.mode = NORMAL;
    if (O.fc > 0) O.fc--;
    return;
    //outlineShowMessage("");
  }

  // return means retrieve items by context or folder
  // do this in database mode
  if (O.view == CONTEXT) {
    O.context = row.title;
    O.folder = "";
    O.taskview = BY_CONTEXT;
    outlineShowMessage("\'%s\' will be opened", O.context.c_str());
    O.command_line = "o " + O.context;
  } else if (O.view == FOLDER) {
    O.folder = row.title;
    O.context = "";
    O.taskview = BY_FOLDER;
    outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
    O.command_line = "o " + O.folder;
  } else if (O.view == KEYWORD) {
    O.keyword = row.title;
    O.folder = "";
    O.context = "";
    O.taskview = BY_KEYWORD;
    outlineShowMessage("\'%s\' will be opened", O.keyword.c_str());
    O.command_line = "ok " + O.keyword;
  }

  command_history.push_back(O.command_line);
  page_hx_idx++;
  page_history.insert(page_history.begin() + page_hx_idx, O.command_line);
  marked_entries.clear();

  get_items(MAX);
}

//case 'i':
void insert_N(void){
  O.mode = INSERT;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 's':
void s_N(void){
  orow& row = O.rows.at(O.fr);
  row.title.erase(O.fc, O.repeat);
  row.dirty = true;
  O.mode = INSERT;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
}          

//case 'x':
void x_N(void){
  orow& row = O.rows.at(O.fr);
  row.title.erase(O.fc, O.repeat);
  row.dirty = true;
}        

void daw_N(void) {
  for (int i = 0; i < O.repeat; i++) outlineDelWord();
}

void caw_N(void) {
  for (int i = 0; i < O.repeat; i++) outlineDelWord();
  O.mode = INSERT;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void dw_N(void) {
  for (int j = 0; j < O.repeat; j++) {
    int start = O.fc;
    outlineMoveEndWord2();
    int end = O.fc;
    O.fc = start;
    orow& row = O.rows.at(O.fr);
    row.title.erase(O.fc, end - start + 2);
  }
}

void cw_N(void) {
  for (int j = 0; j < O.repeat; j++) {
    int start = O.fc;
    outlineMoveEndWord2();
    int end = O.fc;
    O.fc = start;
    orow& row = O.rows.at(O.fr);
    row.title.erase(O.fc, end - start + 2);
  }
  O.mode = INSERT;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void de_N(void) {
  int start = O.fc;
  outlineMoveEndWord(); //correct one to use to emulate vim
  int end = O.fc;
  O.fc = start; 
  for (int j = 0; j < end - start + 1; j++) outlineDelChar();
  O.fc = (start < O.rows.at(O.fr).title.size()) ? start : O.rows.at(O.fr).title.size() -1;
}

void d$_N(void) {
  outlineDeleteToEndOfLine();
}
//case 'r':
void r_N(void) {
  O.mode = REPLACE;
}

//case '~'
void tilde_N(void) {
  for (int i = 0; i < O.repeat; i++) outlineChangeCase();
}

//case 'a':
void a_N(void){
  O.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
  outlineMoveCursor(ARROW_RIGHT);
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'A':
void A_N(void) {
  outlineMoveCursorEOL();
  O.mode = INSERT; //needs to be here for movecursor to work at EOLs
  outlineMoveCursor(ARROW_RIGHT);
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'b':
void b_N(void) {
  outlineMoveBeginningWord();
}

//case 'e':
void e_N(void) {
  outlineMoveEndWord();
}

//case '0':
void zero_N(void) {
  if (!O.rows.empty()) O.fc = 0; // this was commented out - not sure why but might be interfering with O.repeat
}

//case '$':
void dollar_N(void) {
  outlineMoveCursorEOL();
}

//case 'I':
void I_N(void) {
  if (!O.rows.empty()) {
    O.fc = 0;
    O.mode = 1;
    outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
  }
}

void gg_N(void) {
  O.fc = O.rowoff = 0;
  O.fr = O.repeat-1; //this needs to take into account O.rowoff
  if (O.view == TASK) get_preview(O.rows.at(O.fr).id);
  else display_container_info(O.rows.at(O.fr).id);
}

//case 'G':
void G_N(void) {
  O.fc = 0;
  O.fr = O.rows.size() - 1;
  if (O.view == TASK) get_preview(O.rows.at(O.fr).id);
  else display_container_info(O.rows.at(O.fr).id);
}

void gt_N(void) {
  std::map<std::string, int>::iterator it;

  if ((O.view == TASK && O.taskview == BY_FOLDER) || O.view == FOLDER) {
    if (!O.folder.empty()) {
      it = folder_map.find(O.folder);
      it++;
      if (it == folder_map.end()) it = folder_map.begin();
    } else {
      it = folder_map.begin();
    }
    O.folder = it->first;
    outlineShowMessage("\'%s\' will be opened", O.folder.c_str());
  } else {
    if (O.context.empty() || O.context == "search") {
      it = context_map.begin();
    } else {
      it = context_map.find(O.context);
      it++;
      if (it == context_map.end()) it = context_map.begin();
    }
    O.context = it->first;
    outlineShowMessage("\'%s\' will be opened", O.context.c_str());
  }
  get_items(MAX);
}

//case 'O': //Same as C_new in COMMAND_LINE mode
void O_N(void) {
  outlineInsertRow(0, "", true, false, false, now().c_str());
  O.fc = O.fr = O.rowoff = 0;
  outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
  eraseRightScreen(); //erases the note area
  O.mode = INSERT;
}

//case ':':
void colon_N(void) {
  outlineShowMessage(":");
  O.command_line.clear();
  O.last_mode = O.mode;
  O.mode = COMMAND_LINE;
}

//case 'v':
void v_N(void) {
  O.mode = VISUAL;
  O.highlight[0] = O.highlight[1] = O.fc;
  outlineShowMessage("\x1b[1m-- VISUAL --\x1b[0m");
}

//case 'p':  
void p_N(void) {
  if (!string_buffer.empty()) outlinePasteString();
}

//case '*':  
void asterisk_N(void) {
  outlineGetWordUnderCursor();
  outlineFindNextWord(); 
}

//case 'm':
void m_N(void) {
  O.rows.at(O.fr).mark = !O.rows.at(O.fr).mark;
  if (O.rows.at(O.fr).mark) {
    marked_entries.insert(O.rows.at(O.fr).id);
  } else {
    marked_entries.erase(O.rows.at(O.fr).id);
  }  
  outlineShowMessage("Toggle mark for item %d", O.rows.at(O.fr).id);
}

//case 'n':
void n_N(void) {
  outlineFindNextWord();
}

//case 'u':
void u_N(void) {
  //could be used to update solr - would use U
}

//case '^':
void caret_N(void) {
  generate_persistent_html_file(O.rows.at(O.fr).id);
}

//dd and 0x4 -> ctrl-d
void dd_N(void) {
  toggle_deleted();
}

//0x2 -> ctrl-b
void star_N(void) {
  toggle_star();
}

//0x18 -> ctrl-x
void completed_N(void) {
  toggle_completed();
}

void navigate_page_hx(int direction) {
  if (page_history.size() == 1 && O.view == TASK) return;

  if (direction == PAGE_UP) {

    // if O.view!=TASK and PAGE_UP - moves back to last page
    if (O.view == TASK) { //if in a container viewa - fall through to previous TASK view page

      if (page_hx_idx == 0) page_hx_idx = page_history.size() - 1;
      else page_hx_idx--;
    }

  } else {
    if (page_hx_idx == (page_history.size() - 1)) page_hx_idx = 0;
    else page_hx_idx++;
  }

  /* go into COMMAND_LINE mode */
  O.mode = COMMAND_LINE;
  O.command_line = page_history.at(page_hx_idx);
  outlineProcessKeypress('\r');
  O.command_line.clear();

  /* return to NORMAL mode */
  O.mode = NORMAL;
  page_history.erase(page_history.begin() + page_hx_idx);
  page_hx_idx--;
  outlineShowMessage(":%s", page_history.at(page_hx_idx).c_str());
}

void navigate_cmd_hx(int direction) {
  if (command_history.empty()) return;

  if (direction == ARROW_UP) {
    if (cmd_hx_idx == 0) cmd_hx_idx = command_history.size() - 1;
    else cmd_hx_idx--;
  } else {
    if (cmd_hx_idx == (command_history.size() - 1)) cmd_hx_idx = 0;
    else cmd_hx_idx++;
  }
  outlineShowMessage(":%s", command_history.at(cmd_hx_idx).c_str());
  O.command_line = command_history.at(cmd_hx_idx);
}

/*** outline operations ***/

void outlineInsertRow(int at, std::string&& s, bool star, bool deleted, bool completed, const char* modified) {
  /* note since only inserting blank line at top, don't really need at, s and also don't need size_t*/

  orow row;

  row.title = s;
  row.id = -1;
  row.star = star;
  row.deleted = deleted;
  row.completed = completed;
  row.dirty = true;
  strncpy(row.modified, modified, 16);

  row.mark = false;

  auto pos = O.rows.begin() + at;
  O.rows.insert(pos, row);
}

void outlineInsertChar(int c) {

  if (O.rows.size() == 0) return;

  orow& row = O.rows.at(O.fr);
  if (row.title.empty()) row.title.push_back(c);
  else row.title.insert(row.title.begin() + O.fc, c);
  row.dirty = true;
  O.fc++;
}

void outlineDelChar(void) {

  orow& row = O.rows.at(O.fr);

  if (O.rows.empty() || row.title.empty()) return;

  row.title.erase(row.title.begin() + O.fc);
  row.dirty = true;
}

void outlineBackspace(void) {
  orow& row = O.rows.at(O.fr);
  if (O.rows.empty() || row.title.empty() || O.fc == 0) return;
  row.title.erase(row.title.begin() + O.fc - 1);
  row.dirty = true;
  O.fc--;
}

/*** file i/o ***/

std::string outlineRowsToString() {
  std::string s = "";
  for (auto i: O.rows) {
      s += i.title;
      s += '\n';
  }
  s.pop_back(); //pop last return that we added
  return s;
}

void outlineSave(const std::string& fname) {
  if (O.rows.empty()) return;

  std::ofstream f;
  f.open(fname);
  f << outlineRowsToString();
  f.close();

  //outlineShowMessage("Can't save! I/O error: %s", strerror(errno));
  outlineShowMessage("saved to outline.txt");
}

// erases note
void eraseRightScreen(void) {
  //char lf_ret[10];
  //std::string lf_ret = fmt::format("\r\n\x1b[{}C", EDITOR_LEFT_MARGIN);
  //or

  //there may be an issue reusing the same memory buffer once it's created
  //so they may be reserved for use not in loops etc where you are changing
  //the value

  //std::string lf_ret =  fmt::format("\r\n\x1b[{}C", EDITOR_LEFT_MARGIN);

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  //char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, EDITOR_LEFT_MARGIN + 1); 

  //below positions cursor such that top line is erased the first time through
  //for loop although could really start on second line since need to redraw
  //horizontal line anyway
  fmt::memory_buffer buf;
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN, O.divider + 1); //need +1 or erase top T
  ab.append(buf.data(), buf.size());

  //erase the screen
  fmt::memory_buffer lf_ret;
  //note O.divider -1 would take out center divider line and don't want to do that
  fmt::format_to(lf_ret, "\r\n\x1b[{}C", O.divider);
  for (int i=0; i < screenlines - TOP_MARGIN; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret.data(), lf_ret.size());
  }

  // redraw top horizontal line which has t's so needs to be erased
  ab.append("\x1b(0"); // Enter line drawing mode
  //for (int j=1; j<screencols/2; j++) {
  //for (int j=1; j<O.divider; j++) {
  for (int j=1; j<O.totaleditorcols+1; j++) { //added +1 0906/2020
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, O.divider + j); //don't think need offset
    //fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN, O.divider + j);
    std::string buf2 = fmt::format("\x1b[{};{}H", TOP_MARGIN, O.divider + j);
    ab.append(buf2);
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    ab.append("\x1b[37;1mq");
  }
  //exit line drawing mode
  ab.append("\x1b(B");

  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); //necessary? 
  //fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1);
  //std::string buf3 = fmt::format("\x1b[{};{}H", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1);
  std::string buf3 = fmt::format("\x1b[{};{}H", TOP_MARGIN + 1, O.divider + 2);
  ab.append(buf3);
  ab.append("\x1b[0m"); // needed or else in bold mode from line drawing above

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

// currently used for sync log
void readFile(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  display_text.str(std::string());
  display_text.clear();

  while (getline(f, line)) {
    display_text << line << '\n';
  }
  f.close();
}

void displayFile(void) {

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char lf_ret[20];
  int lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider); //note no + 1

  char buf[20];
  //position cursor prior to erase
  int bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 1);
  ab.append(buf, bufchars); //don't need to give length but will if change to memory_buffer

  //erase the right half of the screen
  for (int i=0; i < O.screenlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, lf_chars);
  }

  bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, O.divider + 2);
  ab.append(buf, bufchars);

  ab.append("\x1b[36m", 5); //this is foreground cyan - we'll see

  std::string row;
  std::string line;
  int row_num = -1;
  int line_num = 0;
  display_text.clear();
  display_text.seekg(0, std::ios::beg);
  while(std::getline(display_text, row, '\n')) {
    if (line_num > O.screenlines - 2) break;
    row_num++;
    if (row_num < initial_file_row) continue;
    if (static_cast<int>(row.size()) < O.totaleditorcols) {
      ab.append(row);
      ab.append(lf_ret);
      line_num++;
      continue;
    }
    //int n = 0;
    lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 2); //indent text extra space
    int n = row.size()/(O.totaleditorcols - 1) + ((row.size()%(O.totaleditorcols - 1)) ? 1 : 0);
    for(int i=0; i<n; i++) {
      line_num++;
      if (line_num > O.screenlines - 2) break;
      line = row.substr(0, O.totaleditorcols - 1);
      row.erase(0, O.totaleditorcols - 1);
      ab.append(line);
      ab.append(lf_ret, lf_chars);
    }
  }
  ab.append("\x1b[0m", 4);
  write(STDOUT_FILENO, ab.c_str(), ab.size()); //01012020
}

void get_preview(int id) {
  std::stringstream query;
  O.preview_rows.clear();
  query << "SELECT note FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), preview_callback, nullptr, &S.err_msg, __func__)) return;

  if (O.taskview != BY_SEARCH) draw_preview();
  else {
    word_positions.clear(); 
    get_search_positions(id);
    draw_search_preview();
  }
  //draw_preview();

  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
}

void get_search_positions(int id) {
  std::stringstream query;
  query << "SELECT rowid FROM fts WHERE lm_id = " << id << ";";

  int rowid = -1;
  // callback is *not* called if result (argv) is null
  if (!db_query(S.fts_db, query.str().c_str(), rowid_callback, &rowid, &S.err_msg, __func__)) return;

  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);
  std::stringstream query3;
  int n = 0;
  for(auto v: vec) {
    word_positions.push_back(std::vector<int>{});
    query.str(std::string()); // how you clear a stringstream
    query << "SELECT offset FROM fts_v WHERE doc =" << rowid << " AND term = '" << v << "' AND col = 'note';";
    if (!db_query(S.fts_db, query.str().c_str(), offset_callback, &n, &S.err_msg, __func__)) return;

    n++;
  }

  int ww = (word_positions.at(0).empty()) ? -1 : word_positions.at(0).at(0);
  outlineShowMessage("Word position first: %d; id = %d ", ww, id);

  //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
  /*
  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  } 
  */
}

// doesn't appear to be called if row is NULL
int preview_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  if (!argv[0]) return 0; ////////////////////////////////////////////////////////////////////////////
  std::string note(argv[0]);
  note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    //snote will not contain the '\n'
    O.preview_rows.push_back(s);
  }
  return 0;
}

void draw_preview(void) {

  char buf[50];
  std::string ab;
  unsigned int width = O.totaleditorcols - 10;
  unsigned int length = O.screenlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, EDITOR_LEFT_MARGIN + 5);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, O.divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  //snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN + 5);
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 6);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  //buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  EDITOR_LEFT_MARGIN + 6 << "H";
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  O.divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", O.totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  //buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  EDITOR_LEFT_MARGIN + 6 << "H";
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  O.divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  //snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;44$r\x1b[*x", 
  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               //TOP_MARGIN+6, EDITOR_LEFT_MARGIN+6, TOP_MARGIN+4+length, EDITOR_LEFT_MARGIN+6+width);
               TOP_MARGIN+6, O.divider+7, TOP_MARGIN+4+length, O.divider+7+width);
  if (O.preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }

  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  ab.append(generateWWString(O.preview_rows, width, length, lf_ret));
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

/* this exists to create a text file that has the proper
 * line breaks based on screen width for syntax highlighters
 * to operate on 
 * Produces a text string that starts at the first line of the
 * file and ends on the last visible line
 */
// essentially the same as the word wrap portion of draw_preview and could be used by
// draw_preview and draw_search_preview
std::string generateWWString(std::vector<std::string> &rows, int width, int length, std::string ret) {
  if (rows.empty()) return "";

  std::string ab = "";
  //int y = -line_offset;
  int y = 0;
  int filerow = 0;

  for (;;) {
    //if (filerow == rows.size()) {last_visible_row = filerow - 1; return ab;}
    if (filerow == rows.size()) return ab;

    std::string row = rows.at(filerow);
    
    if (row.empty()) {
      if (y == length - 1) return ab;
      //ab.append("\n");
      ab.append(ret);
      filerow++;
      y++;
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      // this is needed because it deals where the end of the line doesn't have a space
      if (row.substr(pos+1).size() <= width) {
        ab.append(row, pos+1, width);
        //if (y == length - 1) {last_visible_row = filerow - 1; return ab;}
        if (y == length - 1) return ab;
       // ab.append("\n");
        ab.append(ret);
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+width);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + width;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = width - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      if (y == length - 1) return ab; //{last_visible_row = filerow - 1; return ab;}
      //ab.append("\n");
      ab.append(ret);
      y++;
    }
  }
}

void draw_search_preview(void) {
  //need to bring back the note with some marker around the words that
  //we search and replace or retrieve the note with the actual
  //escape codes and not worry that the word wrap will be messed up
  //but it shouldn't ever split an escaped word.  Would start
  //with escapes and go from there
 //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";
 //fts_query << "SELECT highlight(fts, ??1, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE lm_id=? AND fts MATCH '" << search_terms << "' ORDER BY rank";

  char buf[50];
  std::string ab;
  unsigned int width = O.totaleditorcols - 10;
  unsigned int length = O.screenlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, EDITOR_LEFT_MARGIN + 5);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, O.divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  // \x1b[NC moves cursor forward by N columns
  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 6);

  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  //buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  EDITOR_LEFT_MARGIN + 6 << "H";
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  O.divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", O.totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  //buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  EDITOR_LEFT_MARGIN + 6 << "H";
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  O.divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  //snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;44$r\x1b[*x", 
  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               //TOP_MARGIN+6, EDITOR_LEFT_MARGIN+6, TOP_MARGIN+4+length, EDITOR_LEFT_MARGIN+6+width);
               TOP_MARGIN+6, O.divider+7, TOP_MARGIN+4+length, O.divider+7+width);
  if (O.preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }
  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  std::string t = generateWWString(O.preview_rows, width, length, "\f");
  //ab.append(generateWWString(O.preview_rows, width, length, lf_ret));
  highlight_terms_string(t);

  size_t p = 0;
  for (;;) {
    if (p > t.size()) break;
    p = t.find('\f', p);
    if (p == std::string::npos) break;
    t.replace(p, 1, lf_ret);
    p +=7;
   }

  ab.append(t);
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void highlight_terms_string(std::string &text) {

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\f\\"; //must have \f if using as placeholder

  for (auto v: word_positions) { //v will be an int vector of word positions like 15, 30, 70
    int word_num = -1;
    auto pos = v.begin(); //pos = word count of the next word
    auto prev_pos = pos;
    int end = -1; //this became a problem in comparing -1 to unsigned int (always larger)
    int start;
    for (;;) {
      if (end >= static_cast<int>(text.size()) - 1) break;
      //if (word_num > v.back()) break; ///////////
      start = end + 1;
      end = text.find_first_of(delimiters, start);
      if (end == std::string::npos) end = text.size() - 1;
      
      if (end != start) word_num++;

      // start the search from the last match? 12-23-2019
      pos = std::find(pos, v.end(), word_num); // is this saying if word_num is in the vector you have a match
      if (pos != v.end()) {
        //editorHighlightWord(n, start, end-start); put escape codes or [at end and start]
        text.insert(end, "\x1b[48;5;235m"); //49m"); //48:5:235
        text.insert(start, "\x1b[48;5;31m");
        if (pos == v.end() - 1) break;
        end += 21;
        pos++;
        prev_pos = pos;
      } else pos = prev_pos; //pos == v.end() => the current word position was not in v[n]
    }
  }
}

std::string draw_preview_box(unsigned int &width, unsigned int &length) {
  std::string ab;
  //char move_cursor[20];
  fmt::memory_buffer move_cursor;
  fmt::format_to(move_cursor, "\x1b[{}C", width);
  //snprintf(move_cursor, sizeof(move_cursor), "\x1b[%dC", width);
  ab.append("\x1b(0"); // Enter line drawing mode
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 5, O.divider + 6); 
  fmt::memory_buffer buf;
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, O.divider + 6); 
  ab.append(buf.data(), buf.size());
  buf.clear();
  ab.append("\x1b[37;1ml"); //upper left corner
  for (int j=1; j<length; j++) { //+1
   // snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 5 + j, O.divider + 6); 
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5 + j, O.divider + 6); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    ab.append("\x1b[37;1mx");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mx");
  }
  //ab.append(buf); //might be needed!!
  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 4 + length, O.divider + 6));
  ab.append("\x1b[1B");
  ab.append("\x1b[37;1mm"); //lower left corner

  move_cursor.clear();
  //snprintf(move_cursor, sizeof(move_cursor), "\x1b[1D\x1b[%dB", length);
  fmt::format_to(move_cursor, "\x1b[1D\x1b[{}B", length);
  for (int j=1; j<width+1; j++) {
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 5, O.divider + 6 + j); 
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, O.divider + 6 + j); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    ab.append("\x1b[37;1mq");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mq");
  }
  ab.append("\x1b[37;1mj"); //lower right corner
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 5, O.divider + 7 + width); 
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, O.divider + 7 + width); 
  ab.append(buf.data(), buf.size());
  ab.append("\x1b[37;1mk"); //upper right corner

  //exit line drawing mode
  ab.append("\x1b(B");
  ab.append("\x1b[0m");
  ab.append("\x1b[?25h", 6); //shows the cursor
  return ab;
}

// should also just be editor command
void open_in_vim(void){
  std::string filename;
  if (get_folder_tid(O.rows.at(O.fr).id) != 18) filename = "vim_file.txt";
  else filename = "vim_file.cpp";
  p->editorSaveNoteToFile(filename);
  std::stringstream s;
  s << "vim " << filename << " >/dev/tty";
  system(s.str().c_str());
  p->editorReadFileIntoNote(filename);
}

// positions the cursor ( O.cx and O.cy) and O.coloff and O.rowoff
void outlineScroll(void) {

  if(O.rows.empty()) {
      O.fr = O.fc = O.coloff = O.cx = O.cy = 0;
      return;
  }

  if (O.fr > O.screenlines + O.rowoff - 1) {
    O.rowoff =  O.fr - O.screenlines + 1;
  }

  if (O.fr < O.rowoff) {
    O.rowoff =  O.fr;
  }

  if (O.fc > O.titlecols + O.coloff - 1) {
    O.coloff =  O.fc - O.titlecols + 1;
  }

  if (O.fc < O.coloff) {
    O.coloff =  O.fc;
  }


  O.cx = O.fc - O.coloff;
  O.cy = O.fr - O.rowoff;
}

void outlineDrawRows(std::string& ab) {
  int j, k; //to swap highlight if O.highlight[1] < O.highlight[0]
  char buf[32];

  if (O.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN);

  for (y = 0; y < O.screenlines; y++) {
    unsigned int fr = y + O.rowoff;
    if (fr > O.rows.size() - 1) return;
    orow& row = O.rows[fr];

    // if a line is long you only draw what fits on the screen
    //below solves problem when deleting chars from a scrolled long line
    unsigned int len = (fr == O.fr) ? row.title.size() - O.coloff : row.title.size(); //can run into this problem when deleting chars from a scrolled log line
    if (len > O.titlecols) len = O.titlecols;

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    //else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    else if (row.deleted) ab.append(COLOR_1); //red (specific color depends on theme)
    if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    //if (row.mark) ab.append("\x1b[46m", 5); //cyan background
    if (marked_entries.find(row.id) != marked_entries.end()) ab.append("\x1b[46m", 5);

    // below - only will get visual highlighting if it's the active
    // then also deals with column offset
    if (O.mode == VISUAL && fr == O.fr) {

       // below in case O.highlight[1] < O.highlight[0]
      k = (O.highlight[1] > O.highlight[0]) ? 1 : 0;
      j =!k;
      ab.append(&(row.title[O.coloff]), O.highlight[j] - O.coloff);
      ab.append("\x1b[48;5;242m", 11);
      ab.append(&(row.title[O.highlight[j]]), O.highlight[k]
                                             - O.highlight[j]);
      ab.append("\x1b[49m", 5); // return background to normal
      ab.append(&(row.title[O.highlight[k]]), len - O.highlight[k] + O.coloff);

    } else {
        // current row is only row that is scrolled if O.coloff != 0
        ab.append(&row.title[((fr == O.fr) ? O.coloff : 0)], len);
    }

    // the spaces make it look like the whole row is highlighted
    //note len can't be greater than titlecols so always positive
    ab.append(O.titlecols - len + 1, ' ');

    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, O.divider - OUTLINE_RIGHT_MARGIN + 2); // + offset
    // believe the +2 is just to give some space from the end of long titles
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + TOP_MARGIN + 1, O.divider - OUTLINE_RIGHT_MARGIN + 2); // + offset
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append("\x1b[0m"); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
}

void outlineDrawFilters(std::string& ab) {

  if (O.rows.empty()) return;

  char lf_ret[16];
  //snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", O.divider + 1);

  char buf[32];
  //snprintf(buf, sizeof(buf), "\x1b[%dG", EDITOR_LEFT_MARGIN + 1); 
  snprintf(buf, sizeof(buf), "\x1b[%dG", O.divider + 2); 
  ab.append(buf); 

  for (int y = 0; y < O.screenlines; y++) {
    unsigned int fr = y + O.rowoff;
    if (fr > O.rows.size() - 1) return;

    orow& row = O.rows[fr];

    size_t len = (row.title.size() > O.titlecols) ? O.titlecols : row.title.size();

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    //? do this after everything drawn
    if (fr == O.fr) ab.append("\x1b[48;5;236m"); // 236 is a grey

    ab.append(&row.title[0], len);
    int spaces = O.titlecols - len; //needs to change but reveals stuff being written
    std::string s(spaces, ' '); 
    ab.append(s);
    ab.append("\x1b[0m"); // return background to normal /////
    ab.append(lf_ret);
  }
}

void outlineDrawSearchRows(std::string& ab) {
  char buf[32];

  if (O.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN);

  int spaces;

  for (y = 0; y < O.screenlines; y++) {
    int fr = y + O.rowoff;
    if (fr > static_cast<int>(O.rows.size()) - 1) return;
    orow& row = O.rows[fr];
    int len;

    //if (row.star) ab.append("\x1b[1m"); //bold
    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  

    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground

    //if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey but gets stopped as soon as it hits search highlight

    //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";

    // I think the following blows up if there are multiple search terms hits in a line longer than O.titlecols

    if (row.title.size() <= O.titlecols) // we know it fits
      ab.append(row.fts_title.c_str(), row.fts_title.size());
    else {
      size_t pos = row.fts_title.find("\x1b[49m");
      if (pos < O.titlecols + 10) //length of highlight escape
        ab.append(row.fts_title.c_str(), O.titlecols + 15); // length of highlight escape + remove formatting escape
      else
        ab.append(row.title.c_str(), O.titlecols);
}
    len = (row.title.size() <= O.titlecols) ? row.title.size() : O.titlecols;
    spaces = O.titlecols - len;
    for (int i=0; i < spaces; i++) ab.append(" ", 1);
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - OUTLINE_RIGHT_MARGIN + 2); //wouldn't need offset
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, O.divider - OUTLINE_RIGHT_MARGIN + 2); //wouldn't need offset
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified, 16);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
}

void outlineRefreshAllEditors(void) {

  //eraseRightScreen();

  //may be a redundant partial erase in each editorRefreshScreen
  for (auto e : editors) e->editorRefreshScreen(true);
    
}

void outlineDrawStatusBar(void) {

  std::string ab;  
  int len;
  /*
  so the below should 1) position the cursor on the status
  bar row and midscreen and 2) erase previous statusbar
  r -> l and then put the cursor back where it should be
  at OUTLINE_LEFT_MARGIN
  */

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH",
                             O.screenlines + TOP_MARGIN + 1,
                             O.divider,
                             O.screenlines + TOP_MARGIN + 1,
                             1); //status bar comes right out to left margin

  ab.append(buf, strlen(buf));

  ab.append("\x1b[7m"); //switches to inverted colors
  char status[300], status0[300], rstatus[80];

  std::string s;
  switch (O.view) {
    case TASK:
      switch (O.taskview) {
        case BY_SEARCH:
          s =  "search"; 
          break;
        case BY_FOLDER:
          s = O.folder + "[f]";
          break;
        case BY_CONTEXT:
          s = O.context + "[c]";
          break;
        case BY_RECENT:
          s = "recent";
          break;
        case BY_JOIN:
          s = O.context + "[c] + " + O.folder + "[f]";
          break;
        case BY_KEYWORD:
          s = O.keyword + "[k]";
          break;
      }    
      break;
    case CONTEXT:
      s = "Contexts";
      break;
    case FOLDER:
      s = "Folders";
      break;
    case KEYWORD:  
      s = "Keywords";
      break;
  }

  if (!O.rows.empty()) {

    orow& row = O.rows.at(O.fr);
    // note the format is for 15 chars - 12 from substring below and "[+]" when needed
    std::string truncated_title = row.title.substr(0, 12);
    
    //if (p->dirty) truncated_title.append( "[+]"); /****this needs to be in editor class*******/

    // needs to be here because O.rows could be empty
    std::string keywords = (O.view == TASK) ? get_task_keywords().first : ""; // see before and in switch

    // because video is reversted [42 sets text to green and 49 undoes it
    // also [0;35;7m -> because of 7m it reverses background and foreground
    // I think the [0;7m is revert to normal and reverse video
    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s...\x1b[0;35;7m %s \x1b[0;7m %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());

    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %s  %d %d/%zu %s",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());

  } else {

    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
    
    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %d %d/%zu %s",
                              s.c_str(), (O.taskview == BY_SEARCH)  ? " - " : "",
                              (O.taskview == BY_SEARCH) ? search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
  }

  int rlen = snprintf(rstatus, sizeof(rstatus), " %s %s ", ((which_db == SQLITE) ? "sqlite:" : "postgres:"), TOSTRING(GIT_BRANCH));

  if (len > O.left_screencols + 1) {
    ab.append(status0, O.left_screencols + 1);
  } else if (len + rlen > O.left_screencols + 1) {
    ab.append(status);
    ab.append(rstatus, O.left_screencols + 1 - len);
  } else {
    ab.append(status);
    ab.append(O.left_screencols + 1 - len - rlen, ' ');
    ab.append(rstatus);
  }

  ab.append("\x1b[0m"); //switches back to normal formatting
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void return_cursor() {
  std::string ab;
  char buf[32];

  if (editor_mode) {
  // the lines below position the cursor where it should go
    if (p->mode != COMMAND_LINE){
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", p->cy + TOP_MARGIN + 1, p->cx + EDITOR_LEFT_MARGIN + 1); //03022019
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", p->cy + TOP_MARGIN + 1, p->cx + p->left_margin + 1); //03022019
      ab.append(buf, strlen(buf));
    } else { //E.mode == COMMAND_LINE
      //snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", p->total_screenlines + TOP_MARGIN + 2, p->command_line.size() + EDITOR_LEFT_MARGIN + 1); 
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", p->total_screenlines + TOP_MARGIN + 2, p->command_line.size() + O.divider + 2); 
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h"); // show cursor
    }
  } else {
    if (O.mode == ADD_CHANGE_FILTER){
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, EDITOR_LEFT_MARGIN); 
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, O.divider + 1); 
      ab.append(buf, strlen(buf));
    } else if (O.mode == SEARCH) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;34m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN); //blue
      ab.append(buf, strlen(buf));
    } else if (O.mode != COMMAND_LINE) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;31m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN);
      ab.append(buf, strlen(buf));
      // below restores the cursor position based on O.cx and O.cy + margin
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, O.cx + OUTLINE_LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h", 6); // show cursor 
  // no 'caret' if in COMMAND_LINE and want to move the cursor to the message line
    } else { //O.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", O.screenlines + 2 + TOP_MARGIN, O.command_line.size() + OUTLINE_LEFT_MARGIN); /// ****
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h", 6); // show cursor 
    }
  }
  ab.append("\x1b[0m"); //return background to normal
  ab.append("\x1b[?25h"); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void outlineDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << O.screenlines + 2 + TOP_MARGIN << ";"
      //<< screencols/2 << "H" << "\x1b[1K\x1b["
      << O.divider << "H" << "\x1b[1K\x1b["
      << O.screenlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab += buf.str();

  int msglen = strlen(O.message);
  //if (msglen > screencols/2) msglen = screencols/2;
  if (msglen > O.divider) msglen = O.divider;
  ab.append(O.message, msglen);
}

void outlineRefreshScreen(void) {

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  //if (O.view != KEYWORD) {
  if (O.mode != ADD_CHANGE_FILTER) {
    for (unsigned int j=TOP_MARGIN; j < O.screenlines + 1; j++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
      O.titlecols + OUTLINE_LEFT_MARGIN + 17); 
      ab.append(buf, strlen(buf));
    }
  }
  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , OUTLINE_LEFT_MARGIN + 1); // *****************
  ab.append(buf, strlen(buf));

  if (O.mode == SEARCH) outlineDrawSearchRows(ab);
  else if (O.mode == ADD_CHANGE_FILTER) outlineDrawFilters(ab);
  else  outlineDrawRows(ab);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void outlineShowMessage(const char *fmt, ...) {
  char message[100];  
  std::string ab;
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  std::vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap); //free a va_list

  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << O.screenlines + 2 + TOP_MARGIN << ";"
      //<< screencols/2 << "H" << "\x1b[1K\x1b["
      << O.divider << "H" << "\x1b[1K\x1b["
      << O.screenlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab = buf.str();
  //ab.append("\x1b[0m"); //checking if necessary

  int msglen = strlen(message);
  //if (msglen > screencols/2) msglen = screencols/2;
  if (msglen > O.divider) msglen = O.divider;
  ab.append(message, msglen);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

//Note: outlineMoveCursor worries about moving cursor beyond the size of the row
//OutlineScroll worries about moving cursor beyond the screen
void outlineMoveCursor(int key) {

  if (O.rows.empty()) return;

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (O.fc > 0) O.fc--; 
      break;

    case ARROW_RIGHT:
    case 'l':
    {
      O.fc++;
      break;
    }
    case ARROW_UP:
    case 'k':
      if (O.fr > 0) O.fr--; 
      O.fc = O.coloff = 0; 

      if (O.view == TASK) {
        get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note

      } else display_container_info(O.rows.at(O.fr).id);
      break;

    case ARROW_DOWN:
    case 'j':
      if (O.fr < O.rows.size() - 1) O.fr++;
      O.fc = O.coloff = 0;
      if (O.view == TASK) {
        get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
      } else display_container_info(O.rows.at(O.fr).id);
      break;
  }

  orow& row = O.rows.at(O.fr);
  if (O.fc >= row.title.size()) O.fc = row.title.size() - (O.mode != INSERT);
  if (row.title.empty()) O.fc = 0;
}

std::string outlinePreviewRowsToString(void) {

  std::string z = "";
  for (auto i: O.preview_rows) {
      z += i;
      z += '\n';
  }
  if (!z.empty()) z.pop_back(); //pop last return that we added
  return z;
}

// depends on readKey()
//void outlineProcessKeypress(void) {
void outlineProcessKeypress(int c) { //prototype has int = 0  

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  c = (!c) ? readKey() : c;
  switch (O.mode) {
  size_t n;
  //switch (int c = readKey(); O.mode)  //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          O.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          O.command_line.clear();
          outlineShowMessage(":");
          O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.mode = INSERT;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          O.fc = O.fr = O.rowoff = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("\x1b[1m-- INSERT --\x1b[0m");
          eraseRightScreen(); //erases the note area
          O.mode = INSERT;
          return;
      }

      return; //in NO_ROWS - do nothing if no command match

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
          if (O.view == TASK)  {
            update_row();
            if (lm_browser) {
              if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
              else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
            }   
          } else if (O.view == CONTEXT || O.view == FOLDER) update_container();
          else if (O.view == KEYWORD) update_keyword();
          O.command[0] = '\0'; //11-26-2019
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          //outlineShowMessage("");
          return;

        case HOME_KEY:
          O.fc = 0;
          return;

        case END_KEY:
          {
            orow& row = O.rows.at(O.fr);
          if (row.title.size()) O.fc = row.title.size(); // mimics vim to remove - 1;
          return;
          }

        case BACKSPACE:
          outlineBackspace();
          return;

        case DEL_KEY:
          outlineDelChar();
          return;

        case '\t':
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          outlineMoveCursor(c);
          return;

        case CTRL_KEY('z'):
          // not in use
          return;

        case '\x1b':
          O.command[0] = '\0';
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          outlineShowMessage("");
          return;

        default:
          outlineInsertChar(c);
          return;
      } //end of switch inside INSERT

     // return; //End of case INSERT: No need for a return at the end of INSERT because we insert the characters that fall through in switch default:

    case NORMAL:  

      if (c == '\x1b') {
        if (O.view == TASK) get_preview(O.rows.at(O.fr).id); //get out of display_item_info
        outlineShowMessage("");
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
 
      /*leading digit is a multiplier*/
      //if (isdigit(c))  //equiv to if (c > 47 && c < 58)

      if ((c > 47 && c < 58) && (strlen(O.command) == 0)) {

        if (O.repeat == 0 && c == 48) {

        } else if (O.repeat == 0) {
          O.repeat = c - 48;
          return;
        }  else {
          O.repeat = O.repeat*10 + c - 48;
          return;
        }
      }

      if (O.repeat == 0) O.repeat = 1;

      n = strlen(O.command);
      O.command[n] = c;
      O.command[n+1] = '\0';

      if (n_lookup.count(O.command)) {
        n_lookup.at(O.command)();
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }

      //also means that any key sequence ending in something
      //that matches below will perform command

      //might be able to put these into hex char - nope too large

      if (navigation.count(c)) {
          for (int j = 0;j < O.repeat;j++) outlineMoveCursor(c);
          O.command[0] = '\0'; 
          O.repeat = 0;
          return;
      }

      if ((c == PAGE_UP) || (c == PAGE_DOWN)) {
        navigate_page_hx(c);
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
        
      return; // end of case NORMAL 

    case COMMAND_LINE:

      if (c == '\x1b') {
          O.mode = NORMAL;
          outlineShowMessage(""); 
          return;
      }

      if ((c == ARROW_UP) || (c == ARROW_DOWN)) {
        navigate_cmd_hx(c);
        return;
      }  

      if (c == '\r') {
        std::size_t pos = O.command_line.find(' ');
        std::string cmd = O.command_line.substr(0, pos);
        if (cmd_lookup.count(cmd)) {
          if (pos == std::string::npos) pos = 0;
          cmd_lookup.at(cmd)(pos);
          return;
        }

        outlineShowMessage("\x1b[41mNot an outline command: %s\x1b[0m", cmd.c_str());
        O.mode = NORMAL;
        return;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!O.command_line.empty()) O.command_line.pop_back();
      } else {
        O.command_line.push_back(c);
      }

      outlineShowMessage(":%s", O.command_line.c_str());
      return; //end of case COMMAND_LINE

    case SEARCH:  
      switch (c) {

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            O.fr = (O.screenlines > O.fr) ? 0 : O.fr - O.screenlines; //O.fr and O.screenlines are unsigned ints
          } else if (c == PAGE_DOWN) {
             O.fr += O.screenlines;
             if (O.fr > O.rows.size() - 1) O.fr = O.rows.size() - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
        case 'h':
        case 'l':
          outlineMoveCursor(c);
          return;

        //TAB and SHIFT_TAB moves from SEARCH to OUTLINE NORMAL mode but SHIFT_TAB gets back
        case '\t':  
        case SHIFT_TAB:  
          O.fc = 0; 
          O.mode = NORMAL;
          get_preview(O.rows.at(O.fr).id); //only needed if previous comand was 'i'
          outlineShowMessage("");
          return;

        default:
          O.mode = NORMAL;
          O.command[0] = '\0'; 
          outlineProcessKeypress(c); 
          //if (c < 33 || c > 127) outlineShowMessage("<%d> doesn't do anything in SEARCH mode", c);
          //else outlineShowMessage("<%c> doesn't do anything in SEARCH mode", c);
          return;
      } // end of switch(c) in case SEARCH

    case VISUAL:
  
      switch (c) {
  
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          outlineMoveCursor(c);
          O.highlight[1] = O.fc; //this needs to be getFileCol
          return;
  
        case 'x':
          O.repeat = abs(O.highlight[1] - O.highlight[0]) + 1;
          outlineYankString(); //reportedly segfaults on the editor side

          // the delete below requires positioning the cursor
          O.fc = (O.highlight[1] > O.highlight[0]) ? O.highlight[0] : O.highlight[1];

          for (int i = 0; i < O.repeat; i++) {
            outlineDelChar(); //uses editorDeleteChar2! on editor side
          }
          if (O.fc) O.fc--; 
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineShowMessage("");
          return;
  
        case 'y':  
          O.repeat = O.highlight[1] - O.highlight[0] + 1;
          O.fc = O.highlight[0];
          outlineYankString();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineShowMessage("");
          return;
  
        case '\x1b':
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      //return; //end of case VISUAL (return here would not be executed)

    case REPLACE: 

      if (c == '\x1b') {
        O.command[0] = '\0';
        O.repeat = 0;
        O.mode = NORMAL;
        return;
      }

      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar();
        outlineInsertChar(c);
      }

      O.repeat = 0;
      O.command[0] = '\0';
      O.mode = NORMAL;

      return; //////// end of outer case REPLACE

    case ADD_CHANGE_FILTER:

      switch(c) {

        case '\x1b':
          {
          O.mode = COMMAND_LINE;
          size_t temp = page_hx_idx;  
          outlineShowMessage(":%s", page_history.at(page_hx_idx).c_str());
          O.command_line = page_history.at(page_hx_idx);
          outlineProcessKeypress('\r');
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.command_line.clear();
          page_history.pop_back();
          page_hx_idx = temp;
          O.repeat = 0;
          current_task_id = -1; //not sure this is right
          }
          return;

        // could be generalized for folders and contexts too  
        // update_task_folder and update_task_context
        // update_task_context(std::string &, int)
        // maybe make update_task_context(int, int)
        case '\r':
          {
          orow& row = O.rows.at(O.fr); //currently highlighted keyword
          if (marked_entries.empty()) {
            switch (O.view) {
              case KEYWORD:
                add_task_keyword(row.id, current_task_id);
                outlineShowMessage("No tasks were marked so added keyword %s to current task",
                                   row.title.c_str());
                break;
              case FOLDER:
                update_task_folder(row.title, current_task_id);
                outlineShowMessage("No tasks were marked so current task had folder changed to %s",
                                   row.title.c_str());
                break;
              case CONTEXT:
                update_task_context(row.title, current_task_id);
                outlineShowMessage("No tasks were marked so current task had context changed to %s",
                                   row.title.c_str());
                break;
            }
          } else {
            for (const auto& task_id : marked_entries) {
              //add_task_keyword(row.id, task_id);
              switch (O.view) {
                case KEYWORD:
                  add_task_keyword(row.id, task_id);
                  outlineShowMessage("Marked tasks had keyword %s added",
                                     row.title.c_str());
                break;
                case FOLDER:
                  update_task_folder(row.title, task_id);
                  outlineShowMessage("Marked tasks had folder changed to %s",
                                     row.title.c_str());
                break;
                case CONTEXT:
                  update_task_context(row.title, task_id);
                  outlineShowMessage("Marked tasks had context changed to %s",
                                     row.title.c_str());
                break;
              }
            }
          }
          }

          O.command[0] = '\0'; //might not be necessary
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
          //for (int j = 0;j < O.repeat;j++) outlineMoveCursor(c);
          outlineMoveCursor(c);
          //O.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          //O.repeat = 0;
          return;

        default:
          if (c < 33 || c > 127) outlineShowMessage("<%d> doesn't do anything in ADD_CHANGE_FILTER mode", c);
          else outlineShowMessage("<%c> doesn't do anything in ADD_CHANGE_FILTER mode", c);
          return;
      }

      return; //end  ADD_CHANGE_FILTER - do nothing if no c match

    case FILE_DISPLAY: 

      switch (c) {
  
        case ARROW_UP:
        case 'k':
          initial_file_row--;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          break;

        case ARROW_DOWN:
        case 'j':
          initial_file_row++;
          break;

        case PAGE_UP:
          initial_file_row = initial_file_row - O.screenlines;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          break;

        case PAGE_DOWN:
          initial_file_row = initial_file_row + O.screenlines;
          break;

        case ':':
          outlineShowMessage(":");
          O.command[0] = '\0';
          O.command_line.clear();
          //O.last_mode was set when entering file mode
          O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          O.mode = O.last_mode;
          if (O.view == TASK) get_preview(O.rows.at(O.fr).id);
          else display_container_info(O.rows.at(O.fr).id);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineShowMessage("");
          return;
      }

      displayFile();

      return;
  } //end of outer switch(O.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
} //end outlineProcessKeypress

void synchronize(int report_only) { //using 1 or 0

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("synchronize"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "synchronize"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = PyLong_FromLong(report_only);
          //pValue = Py_BuildValue("s", search_terms); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineShowMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineShowMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineShowMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      outlineShowMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  if (report_only) outlineShowMessage("Number of tasks/items that would be affected is %d", num);
  else outlineShowMessage("Number of tasks/items that were affected is %d", num);
}

int get_id(void) { 
  return O.rows.at(O.fr).id;
}

void outlineChangeCase() {
  orow& row = O.rows.at(O.fr);
  char d = row.title.at(O.fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    outlineMoveCursor(ARROW_RIGHT);
    return;
  }
  outlineDelChar();
  outlineInsertChar(d);
}

void outlineYankLine(int n){

  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(O.rows.at(O.fr+i).title);
  }
  // set string_buffer to "" to signal should paste line and not chars
  string_buffer.clear();
}

void outlineYankString() {
  orow& row = O.rows.at(O.fr);
  string_buffer.clear();

  std::string::const_iterator first = row.title.begin() + O.highlight[0];
  std::string::const_iterator last = row.title.begin() + O.highlight[1];
  string_buffer = std::string(first, last);
}

void outlinePasteString(void) {
  orow& row = O.rows.at(O.fr);

  if (O.rows.empty() || string_buffer.empty()) return;

  row.title.insert(row.title.begin() + O.fc, string_buffer.begin(), string_buffer.end());
  O.fc += string_buffer.size();
  row.dirty = true;
}

void outlineDelWord() {

  orow& row = O.rows.at(O.fr);
  if (row.title[O.fc] < 48) return;

  int i,j,x;
  for (i = O.fc; i > -1; i--){
    if (row.title[i] < 48) break;
    }
  for (j = O.fc; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  row.dirty = true;
  //outlineShowMessage("i = %d, j = %d", i, j ); 
}

void outlineDeleteToEndOfLine(void) {
  orow& row = O.rows.at(O.fr);
  row.title.resize(O.fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  row.dirty = true;
  }

void outlineMoveCursorEOL() {

  O.fc = O.rows.at(O.fr).title.size() - 1;  //if O.cx > O.titlecols will be adjusted in EditorScroll
}

// not same as 'e' but moves to end of word or stays put if already on end of word
void outlineMoveEndWord2() {
  int j;
  orow& row = O.rows.at(O.fr);

  for (j = O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = j - 1;
}

//void outlineMoveNextWord() {
void w_N(void) {
  int j;
  orow& row = O.rows.at(O.fr);

  for (j = O.fc + 1; j < row.title.size(); j++) {
    if (row.title[j] < 48) break;
  }

  O.fc = j - 1;

  for (j = O.fc + 1; j < row.title.size() ; j++) { //+1
    if (row.title[j] > 48) break;
  }
  O.fc = j;

  O.command[0] = '\0';
  O.repeat = 0;
}

void outlineMoveBeginningWord() {
  orow& row = O.rows.at(O.fr);
  if (O.fc == 0) return;
  for (;;) {
    if (row.title[O.fc - 1] < 48) O.fc--;
    else break;
    if (O.fc == 0) return;
  }
  int i;
  for (i = O.fc - 1; i > -1; i--){
    if (row.title[i] < 48) break;
  }
  O.fc = i + 1;
}

void outlineMoveEndWord() {
  orow& row = O.rows.at(O.fr);
  if (O.fc == row.title.size() - 1) return;
  for (;;) {
    if (row.title[O.fc + 1] < 48) O.fc++;
    else break;
    if (O.fc == row.title.size() - 1) return;
  }
  int j;
  for (j = O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  O.fc = j - 1;
}

void outlineGetWordUnderCursor(){
  std::string& title = O.rows.at(O.fr).title;
  if (title[O.fc] < 48) return;

  int i,j,x;

  for (i = O.fc - 1; i > -1; i--){
    if (title[i] < 48) break;
  }

  for (j = O.fc + 1; j < title.size() ; j++) {
    if (title[j] < 48) break;
  }

  for (x=i+1; x<j; x++) {
      search_string.push_back(title.at(x));
  }
  outlineShowMessage("word under cursor: <%s>", search_string.c_str());
}

void outlineFindNextWord() {

  int y, x;
  y = O.fr;
  x = O.fc + 1; //in case sitting on beginning of the word
   for (unsigned int n=0; n < O.rows.size(); n++) {
     std::string& title = O.rows.at(y).title;
     auto res = std::search(std::begin(title) + x, std::end(title), std::begin(search_string), std::end(search_string));
     if (res != std::end(title)) {
         O.fr = y;
         O.fc = res - title.begin();
         break;
     }
     y++;
     x = 0;
     if (y == O.rows.size()) y = 0;
   }

    outlineShowMessage("x = %d; y = %d", x, y); 
}

// calls readKey()
bool editorProcessKeypress(void) {
  //int start, end;
  int i;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  switch (int c = readKey(); p->mode) {

    case NO_ROWS:

      switch(c) {
        case ':':
          p->mode = COMMAND_LINE;
          p->command_line.clear();
          p->command[0] = '\0';
          p->editorSetMessage(":");
          return false;

        case '\x1b':
          p->command[0] = '\0';
          p->repeat = 0;
          return false;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
        case 'O':
        case 'o':
          p->editorInsertRow(0, std::string());
          p->mode = INSERT;
          p->command[0] = '\0';
          p->repeat = 0;
          p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return true;
      }

      return false;

    case INSERT:

      switch (c) {

        case '\r':
          //p->push_current();
          p->editorInsertReturn();
          p->last_typed += c;
          return true;

        // not sure this is in use
        case CTRL_KEY('s'):
          p->editorSaveNoteToFile("lm_temp");
          return false;

        case HOME_KEY:
          p->editorMoveCursorBOL();
          return false;

        case END_KEY:
          p->editorMoveCursorEOL();
          p->editorMoveCursor(ARROW_RIGHT);
          return false;

        case BACKSPACE:
          //p->push_current(); //p->editorCreateSnapshot();
          p->editorBackspace();
          return true;
    
        case DEL_KEY:
          //p->push_current(); //p->editorCreateSnapshot();
          p->editorDelChar();
          return true;
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          p->editorMoveCursor(c);
          return false;
    
        case CTRL_KEY('b'):
        //case CTRL_KEY('i'): CTRL_KEY('i') -> 9 same as tab
        case CTRL_KEY('e'):
          p->push_current(); //p->editorCreateSnapshot();
          p->editorDecorateWord(c);
          return true;
    
        // this should be a command line command
        case CTRL_KEY('z'):
          p->smartindent = (p->smartindent) ? 0 : SMARTINDENT;
          p->editorSetMessage("smartindent = %d", p->smartindent); 
          return false;
    
        case '\x1b':

          /*
           * below deals with certain NORMAL mode commands that
           * cause entry to INSERT mode includes dealing with repeats
           */

          //i,I,a,A - deals with repeat
          if(cmd_map1.contains(p->last_command)) { //nuspell needed gcc+17 so no contains
            p->push_current(); //
            for (int n=0; n<p->last_repeat-1; n++) {
              for (char const &c : p->last_typed) {p->editorInsertChar(c);}
            }
          }

          //cmd_map2 -> E_o_escape and E_O_escape - here deals with deals with repeat > 1
          if (cmd_map2.count(p->last_command)) {
            (p->*cmd_map2[p->last_command])(p->last_repeat - 1);
            p->push_current(); //
          }

          if (cmd_map4.count(p->last_command)) { //cw, caw, s
            //(p->*cmd_map4[p->last_command])(p->last_repeat - 1);
            p->push_current(); //
          }
          //'I' in VISUAL BLOCK mode
          //if (p->last_command == -1) {
          if (p->last_command == "VBI") {
            for (int n=0; n<p->last_repeat-1; n++) {
              for (char const &c : p->last_typed) {p->editorInsertChar(c);}
            }
            //{
            int temp = p->fr;

            for (p->fr=p->fr+1; p->fr<p->vb0[1]+1; p->fr++) {
              for (int n=0; n<p->last_repeat; n++) { //NOTICE not p->last_repeat - 1
                p->fc = p->vb0[0]; 
                for (char const &c : p->last_typed) {p->editorInsertChar(c);}
              }
            }
            p->fr = temp;
            p->fc = p->vb0[0];
          //}
          }

          //'A' in VISUAL BLOCK mode
          //if (p->last_command == -2) {
          if (p->last_command == "VBA") {
            //p->fc++;a doesn't go here
            for (int n=0; n<p->last_repeat-1; n++) {
              for (char const &c : p->last_typed) {p->editorInsertChar(c);}
            }
            {
            int temp = p->fr;

            for (p->fr=p->fr+1; p->fr<p->vb0[1]+1; p->fr++) {
              for (int n=0; n<p->last_repeat; n++) { //NOTICE not p->last_repeat - 1
                int size = p->rows.at(p->fr).size();
                if (p->vb0[2] > size) p->rows.at(p->fr).insert(size, p->vb0[2]-size, ' ');
                p->fc = p->vb0[2];
                for (char const &c : p->last_typed) {p->editorInsertChar(c);}
              }
            }
            p->fr = temp;
            p->fc = p->vb0[0];
          }
          }

          /*falls through to here*/
          p->mode = NORMAL;
          p->repeat = 0;
          p->last_typed = std::string(); //probably messes up dot but dot could use last cmd from diff
          if (p->fc > 0) p->fc--;

          // below - if the indent amount == size of line then it's all blanks
          // can hit escape with p->row == NULL or p->row[p->fr].size == 0
          if (!p->rows.empty() && p->rows[p->fr].size()) {
            int n = p->editorIndentAmount(p->fr);
            if (n == p->rows[p->fr].size()) {
              p->fc = 0;
              for (int i = 0; i < n; i++) {
                p->editorDelChar();
              }
            }
          }
          //p->editorSetMessage(""); // commented out to debug push_current
          //editorSetMessage(p->last_typed.c_str());
          return true; //end case x1b:
    
        // deal with tab in insert mode - was causing segfault  
        case '\t':
          for (int i=0; i<4; i++) p->editorInsertChar(' ');
          return true;  

        default:
          p->editorInsertChar(c);
          p->last_typed += c;
          // not 100% clear but should deal with O, o which can't be dealt with as single line
          //p->undo_deque[0].last_typed += c;
          return true;
     
      } //end inner switch for outer case INSERT

      return true; // end of case INSERT: - should not be executed

    case NORMAL: 
 
      if (c == '\x1b') {
        p->command[0] = '\0';
        p->repeat = 0;
        return false;
      }

      if (c == 'u') {
        p->command[0] = '\0';
        p->undo();
        return true;
      }

      if (c == CTRL_KEY('r')) {
        p->command[0] = '\0';
        p->redo();
        return true;
      }

      if (c == CTRL_KEY('h')) {
        p->command[0] = '\0';
        if (editors.size() == 1) {
          editor_mode = false;
          get_preview(O.rows.at(O.fr).id); 
          return false;
        }
        auto it = std::find(editors.begin(), editors.end(), p);
        int index = std::distance(editors.begin(), it);
        if (index) {
          p = editors[index - 1];
        } else {editor_mode = false;
          get_preview(O.rows.at(O.fr).id); 
          return false;
        }
      }
      
      if (c == CTRL_KEY('l')) {
        p->command[0] = '\0';
        auto it = std::find(editors.begin(), editors.end(), p);
        int index = std::distance(editors.begin(), it);
        if (index < editors.size() - 1) p = editors[index + 1];
        return false;
      }

      /*leading digit is a multiplier*/

      if ((c > 47 && c < 58) && (strlen(p->command) == 0)) {

        if (p->repeat == 0 && c == 48) {

        } else if (p->repeat == 0) {
          p->repeat = c - 48;
          // return false because command not complete
          return false;
        } else {
          p->repeat = p->repeat*10 + c - 48;
          // return false because command not complete
          return false;
        }
      }
      if ( p->repeat == 0 ) p->repeat = 1;
      {
      int n = strlen(p->command);
      p->command[n] = c;
      p->command[n+1] = '\0';
      }

      if (e_lookup.count(p->command)) {
        //if (!move_only.count(p->command)) p->push_current(); 
        //here's the problem - we need fr and fc now
        p->prev_fr = p->fr;
        p->prev_fc = p->fc;

        (p->*e_lookup.at(p->command))(p->repeat); //money shot

        if (insert_cmds.count(p->command)) {
          p->mode = INSERT;
          p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          p->last_repeat = p->repeat;
          p->last_command = p->command; //p->last_command must be a string
          p->command[0] = '\0';
          p->repeat = 0;
          return true;
        } else if (move_only.count(p->command)) {
          p->command[0] = '\0';
          p->repeat = 0;
          return false; //note text did not change
        } else {
          if (p->command[0] != '.') {
            p->last_repeat = p->repeat;
            p->last_command = p->command; //p->last_command must be a string
            p->push_current();
            p->command[0] = '\0';
            p->repeat = 0;
          }
        }    
      }

      if (navigation.count(c)) {
          for (int j=0; j<p->repeat; j++) p->editorMoveCursor(c);
          p->command[0] = '\0';
          p->repeat = 0;
          return false;
      }

      if ((c == PAGE_UP) || (c == PAGE_DOWN)) {
          p->editorPageUpDown(c);
          p->command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          p->repeat = 0;
          return false;
      }

      return true;// end of case NORMAL - there are breaks that can get to code above

    case COMMAND_LINE:

      if (c == '\x1b') {
        p->mode = NORMAL;
        p->command[0] = '\0';
        p->repeat = p->last_repeat = 0;
        p->editorSetMessage(""); 
        return false;
      }

      if (c == '\r') {

        // right now only command that has a space is readfile
        std::size_t pos = p->command_line.find(' ');
        std::string cmd = p->command_line.substr(0, pos);

        // note that right now we are not calling editor commands like E_write_close_C
        // and E_quit_C and E_quit0_C
        if (quit_cmds.count(cmd)) {
        //if (p->command_line == "q" || p->command_line == "quit") {
          if (cmd == "x") update_note();

          if (cmd == "q!" || cmd == "quit!") {
            // do nothing = allow editor to be closed
          } else if (p->dirty) {
              p->mode = NORMAL;
              p->command[0] = '\0';
              p->command_line.clear();
              p->editorSetMessage("No write since last change");
              return false;
          }

          eraseRightScreen();
          if (auto n = editors.size(); n > 1) {
            editors.erase(std::remove(editors.begin(), editors.end(), p), editors.end());
            p = editors[0]; //kluge should move in some logical fashion
            n--;
            int i = 0;
            for (auto z : editors) {
              z->screenlines = screenlines - 2 - TOP_MARGIN;
              z->screencols = -1 + (screencols - O.divider)/n;
              z->total_screenlines = screenlines - 2 - TOP_MARGIN;
              z->left_margin = O.divider +  i*z->screencols + i; //was +1
              //z->editorRefreshScreen(true);
              i++;
            }
            /**********also in F_edit - should be in a function ***********************/
            std::string ab;
            for (auto &e : editors) {
            //for (int i=0; i<editors.size(); i++) {  
              //Editor *&e = editors.at(i);
              e->editorRefreshScreen(true);
              //char buf[32];
              std::string buf;
              ab.append("\x1b(0"); // Enter line drawing mode
              for (int j=1; j<O.screenlines+1; j++) {
                //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, e->left_margin + e->screencols+1); 
                buf = fmt::format("\x1b[{};{}H", TOP_MARGIN + j, e->left_margin +e->screencols+1);
                ab.append(buf);
                // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
                // only need one 'm'
                ab.append("\x1b[37;1mx");
              }
              //'T' corner = w or right top corner = k
              //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, e->left_margin + e->screencols+1); //may not need offset
              buf = fmt::format("\x1b[{};{}H", TOP_MARGIN, e->left_margin + e->screencols+1); //may not need offset
              ab.append(buf);
              //if (i == editors.size() - 1) ab.append("\x1b[37;1mk");
              if (&editors.back() == &e) ab.append("\x1b[37;1mk");
              else ab.append("\x1b[37;1mw");

              //exit line drawing mode
              ab.append("\x1b(B");
            }
            ab.append("\x1b[?25h", 6); //shows the cursor
            ab.append("\x1b[0m"); //or else subsequent editors are bold
            write(STDOUT_FILENO, ab.c_str(), ab.size());
            /*********************************/
          } else {
            editors.clear(); // there's only one so also could just erase editors0]
            editor_mode = false;
            get_preview(O.rows.at(O.fr).id);
          }
        //editorRefreshScreen(false); // don't need to redraw rows
          p->editorSetMessage("");
          return false;
        }

        if (E_lookup_C.count(cmd)) {
          //E_lookup_C.at(p->command_line)();
          (p->*E_lookup_C.at(cmd))();
          return false;
        }

        p->editorSetMessage("\x1b[41mNot an editor command: %s\x1b[0m", p->command_line.c_str());
        p->mode = NORMAL;
        return false;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!p->command_line.empty()) p->command_line.pop_back();
      } else {
        p->command_line.push_back(c);
      }

      p->editorSetMessage(":%s", p->command_line.c_str());
      return false; //end of case COMMAND_LINE

    case VISUAL_LINE:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          p->editorMoveCursor(c);
          p->highlight[1] = p->fr;
          return true;
    
        case 'x':
          if (!p->rows.empty()) {
            p->push_current(); //p->editorCreateSnapshot();
            p->repeat = p->highlight[1] - p->highlight[0] + 1;
            p->fr = p->highlight[0]; 
            p->editorYankLine(p->repeat);
    
            for (int i = 0; i < p->repeat; i++) p->editorDelRow(p->highlight[0]);
          }

          p->fc = 0;
          p->command[0] = '\0';
          p->repeat = p->last_repeat = 0;
          p->mode = NORMAL;
          p->editorSetMessage("");
          return true;
    
        case 'y':  
          p->repeat = p->highlight[1] - p->highlight[0] + 1;
          p->fr = p->highlight[0];
          p->editorYankLine(p->repeat);
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        case '>':
          p->push_current(); //p->editorCreateSnapshot();
          p->repeat = p->highlight[1] - p->highlight[0] + 1;
          p->fr = p->highlight[0];
          for ( i = 0; i < p->repeat; i++ ) {
            p->editorIndentRow();
            p->fr++;}
          p->fr-=i;
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        // changed to p->fr on 11-26-2019
        case '<':
          p->push_current(); //p->editorCreateSnapshot();
          p->repeat = p->highlight[1] - p->highlight[0] + 1;
          p->fr = p->highlight[0];
          for ( i = 0; i < p->repeat; i++ ) {
            p->editorUnIndentRow();
            p->fr++;}
          p->fr-=i;
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        case '\x1b':
          p->mode = 0;
          p->command[0] = '\0';
          p->repeat = 0;
          p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL_BLOCK:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          p->editorMoveCursor(c);
          //p->highlight[1] = E.fr;
          return true;
    
        case '$':
          p->editorMoveCursorEOL();
          p->command[0] = '\0';
          p->repeat = p->last_repeat = 0;
          p->editorSetMessage("");
          return true;

        case 'x':
          if (!p->rows.empty()) {
            //p->editorCreateSnapshot();
    
          for (int i = p->vb0[1]; i < p->fr + 1; i++) {
            p->rows.at(i).erase(p->vb0[0], p->fc - p->vb0[0] + 1); //needs to be cleaned up for p->fc < p->vb0[0] ? abs
          }

          p->fc = p->vb0[0];
          p->fr = p->vb0[1];
          }
          p->command[0] = '\0';
          p->repeat = p->last_repeat = 0;
          p->mode = NORMAL;
          p->editorSetMessage("");
          return true;
    
        case 'I':
          if (!p->rows.empty()) {
            //p->editorCreateSnapshot();
      
          //p->repeat = p->fr - p->vb0[1];  
            {
          int temp = p->fr; //p->fr is wherever cursor Y is    
          //p->vb0[2] = p->fr;
          p->fc = p->vb0[0]; //vb0[0] is where cursor X was when ctrl-v happened
          p->fr = p->vb0[1]; //vb0[1] is where cursor Y was when ctrl-v happened
          p->vb0[1] = temp; // resets p->vb0 to last cursor Y position - this could just be p->vb0[2]
          //cmd_map1[c](p->repeat);
          //command = -1;
          p->repeat = 1;
          p->mode = INSERT;
          p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          p->last_repeat = p->repeat;
          p->last_typed.clear();
          //p->last_command = command;
          p->last_command = std::string_view("VBI");
          p->command[0] = '\0';
          p->repeat = 0;
          //editorSetMessage("command = %d", command);
          return true;

        case 'A':
          if (!p->rows.empty()) {
            //p->editorCreateSnapshot();
      
          //p->repeat = p->fr - p->vb0[1];  
            {
          int temp = p->fr;    
          p->fr = p->vb0[1];
          p->vb0[1] = temp;
          p->fc++;
          p->vb0[2] = p->fc;
          //int last_row_size = p->rows.at(p->vb0[1]).size();
          int first_row_size = p->rows.at(p->fr).size();
          if (p->vb0[2] > first_row_size) p->rows.at(p->fr).insert(first_row_size, p->vb0[2]-first_row_size, ' ');
          //cmd_map1[c](p->repeat);
          //command = -2;
          p->repeat = 1;
          p->mode = INSERT;
          p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          p->last_repeat = p->repeat;
          p->last_typed.clear();
          //p->last_command = command;
          p->last_command = std::string_view("VBA");
          p->command[0] = '\0';
          p->repeat = 0;
          //editorSetMessage("command = %d", command);
          return true;

        case '\x1b':
          p->mode = 0;
          p->command[0] = '\0';
          p->repeat = 0;
          p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          p->editorMoveCursor(c);
          p->highlight[1] = p->fc;
          return true;
    
        case 'x':
          if (!p->rows.empty()) {
            p->push_current(); //p->editorCreateSnapshot();
            p->editorYankString(); 
            p->editorDeleteVisual();
          }
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        case 'y':
          p->fc = p->highlight[0];
          p->editorYankString();
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        case 'p':
          p->push_current();
          if (!string_buffer.empty()) p->editorPasteStringVisual();
          else p->editorPasteLineVisual();
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = NORMAL;
          return true;

        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          p->push_current(); //p->editorCreateSnapshot();
          p->editorDecorateVisual(c);
          p->command[0] = '\0';
          p->repeat = 0;
          p->mode = 0;
          p->editorSetMessage("");
          return true;
    
        case '\x1b':
          p->mode = NORMAL;
          p->command[0] = '\0';
          p->repeat = p->last_repeat = 0;
          p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }
    
      return false;

    case REPLACE:

      if (c == '\x1b') {
        p->command[0] = '\0';
        p->repeat = p->last_repeat = 0;
        p->last_command = "";
        p->last_typed.clear();
        p->mode = NORMAL;
        return true;
      }

      //editorCreateSnapshot();
      for (int i = 0; i < p->last_repeat; i++) {
        p->editorDelChar();
        p->editorInsertChar(c);
        p->last_typed.clear();
        p->last_typed += c;
      }
      //other than p->mode = NORMAL - all should go
      p->last_command = "r";
      //p->last_repeat = p->repeat;
      p->repeat = 0;
      p->command[0] = '\0';
      p->mode = NORMAL;
      return true;

  }  //end of outer switch(p->mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
  return true; // this should not be reachable but was getting an error
} //end of editorProcessKeyPress

void eraseScreenRedrawLines(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Erase the screen
  //int pos = screencols/2;
  int pos = O.divider;
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (int j = 1; j < screenlines + 1; j++) {

    // First vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos - OUTLINE_RIGHT_MARGIN + 1); //don't think need offset
    write(STDOUT_FILENO, buf, strlen(buf));
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    write(STDOUT_FILENO, "\x1b[37;1mx", 8);

    // Second vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mx", 8); 
}

  write(STDOUT_FILENO, "\x1b[1;1H", 6);
  for (int k=1; k < screencols ;k++) {
    // note: cursor advances automatically so don't need to 
    // do that explicitly
    write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
  }

  // draw first column's 'T' corner
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, pos - OUTLINE_RIGHT_MARGIN + 1); //may not need offset
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  // draw next column's 'T' corner
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, pos);
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
  write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
}

/*** init ***/

void initOutline() {
  O.cx = 0; //cursor x position
  O.cy = 0; //cursor y position
  O.fc = 0; //file x position
  O.fr = 0; //file y position
  O.rowoff = 0;  //number of rows scrolled off the screen
  O.coloff = 0;  //col the user is currently scrolled to  
  O.sort = "modified";
  O.show_deleted = false; //not treating these separately right now
  O.show_completed = true;
  O.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  O.highlight[0] = O.highlight[1] = -1;
  O.mode = NORMAL; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  O.last_mode = NORMAL;
  O.command[0] = '\0';
  O.command_line = "";
  O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  O.view = TASK; // not necessary here since set when searching database
  O.taskview = BY_FOLDER;
  O.folder = "todo";
  O.context = "No Context";
  O.keyword = "";

  // ? where this should be.  Also in signal.
  O.screenlines = screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  O.divider = screencols - c.ed_pct * screencols/100;
  O.titlecols =  O.divider - OUTLINE_RIGHT_MARGIN - OUTLINE_LEFT_MARGIN; 
  O.totaleditorcols = screencols - O.divider - 1; // was 2 
  O.left_screencols = O.divider - 1; //was 2
}

int main(int argc, char** argv) { 

  publisher.bind("tcp://*:5556");
  publisher.bind("ipc://scroll.ipc"); 

  //publisher.bind("tcp://*:5557");
  //publisher.bind("ipc://html.ipc"); 

  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_pid = getpid();

  if (argc > 1 && argv[1][0] == '-') lm_browser = false;

  db_open();
  get_conn(); //for pg
  load_meta(); 

  which_db = SQLITE;

  map_context_titles();
  map_folder_titles();

  getWindowSize(&screenlines, &screencols);
  enableRawMode();
  initOutline();
  eraseScreenRedrawLines();
  EDITOR_LEFT_MARGIN = O.divider + 1; //only used in Editor.cpp
  get_items(MAX);
  command_history.push_back("of todo"); //klugy - this could be read from config and generalized
  page_history.push_back("of todo"); //klugy - this could be read from config and generalized
  
 // PQfinish(conn); // this should happen when exiting

  // putting this here seems to speed up first search but still slow
  // might make sense to do the module imports here too
  // assume the reimports are essentially no-ops
  //Py_Initialize(); 

  if (lm_browser) popen (system_call.c_str(), "r"); //returns FILE* id

  signal(SIGWINCH, signalHandler);
  bool text_change;
  bool scroll;
  bool redraw;

  outlineRefreshScreen(); // now just draws rows
  outlineDrawStatusBar();
  outlineShowMessage("rows: %d  cols: %d", screenlines, screencols);
  return_cursor();

  while (1) {
    // just refresh what has changed
    if (editor_mode) {
      text_change = editorProcessKeypress(); 
      scroll = p->editorScroll();
      redraw = (p->mode == COMMAND_LINE) ? false : (text_change || scroll);
      p->editorRefreshScreen(redraw);
      ////////////////////
      if (scroll) {
        zmq::message_t message(20);
        snprintf ((char *) message.data(), 20, "%d", p->line_offset*25); //25 - complete hack but works ok
        publisher.send(message, zmq::send_flags::dontwait);
      }
      ////////////////////
    } else if (O.mode != FILE_DISPLAY) { 
      outlineProcessKeypress();
      outlineScroll();
      outlineRefreshScreen(); // now just draws rows
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode

    outlineDrawStatusBar();
    return_cursor();
  }
  return 0;
}
