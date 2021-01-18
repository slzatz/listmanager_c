#ifndef OUTLINE_NORMAL_FUNCTIONS_H
#define OUTLINE_NORMAL_FUNCTIONS_H
#define TZ_OFFSET 5 // time zone offset - either 4 or 5
#include "session.h"
#include "Common.h"
#include "Organizer.h"
#include "Dbase.h"
#include <string>
#include <iomanip>
#include <chrono>

int insertRow(orow& row);
int insertKeyword(orow& row);
std::string time_delta_(std::string t);
void get_items_by_id(std::string query);
//void searchDB(const std::string & st, bool help=false) {

/********************Beginning sqlite************************************/
/*I think these all go away eventually*/

void runSQL(void) {
  if (!sess.db.run()) {
    sess.showOrgMessage("SQL error: %s", sess.db.errmsg);
    return;
  }  
}

void dbOpen(void) {
  int rc = sqlite3_open(SQLITE_DB_.c_str(), &sess.S.db);
  if (rc != SQLITE_OK) {
    sqlite3_close(sess.S.db);
    exit(1);
  }

  rc = sqlite3_open(FTS_DB_.c_str(), &sess.S.fts_db);
  if (rc != SQLITE_OK) {
    sqlite3_close(sess.S.fts_db);
    exit(1);
  }
}

bool dbQuery(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     sess.showOrgMessage("SQL error: %s", errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

bool dbQuery(sqlite3 *db, const std::string& sql, sq_callback callback, void *pArg, char **errmsg, const char *func) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     sess.showOrgMessage("SQL error in %s: %s", func, errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

/********************Beginning sqlite************************************/

int getId(void) { 
  return org.rows.at(org.fr).id;
}

void toggleStar(void) {
  orow& row = org.rows.at(org.fr);
  int id = getId();
  std::string table;
  std::string column;

  switch(org.view) {

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
      sess.showOrgMessage("Not sure what you're trying to toggle");
      return;
  }

  Query q(db, "UPDATE {} SET {}={}, modified=datetime('now') WHERE id={};",
                                   table, column, (row.star) ? "False" : "True", id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'toggleStar': {}", error);
    return;
  }

  sess.showOrgMessage("Toggle star succeeded (new version)");
  row.star = !row.star;
}

void toggleDeleted(void) {
  orow& row = org.rows.at(org.fr);
  int id = getId();
  std::string table;

  switch(org.view) {
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
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }

  Query q(db, "UPDATE {} SET deleted={}, modified=datetime('now') WHERE id={};",
                    table, (row.deleted) ? "False" : "True", id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'toggleDeleted': {}", error);
    return;
  }
    
  sess.showOrgMessage("Toggle deleted succeeded (new version)");
  row.deleted = !row.deleted;
}

void toggleCompleted(void) {
  orow& row = org.rows.at(org.fr);
  int id = getId();

  Query q(db, "UPDATE task SET completed={}, modified=datetime('now') WHERE id={};",
                                  (row.completed) ? "NULL" : "date()", id); 

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'toggleCompleted': {}", error);
    return;
  }

  row.completed = !row.completed;
  sess.showOrgMessage3("Toggle completed succeeded (new version)");
}

void updateTaskContext(std::string &new_context, int id) {
  int context_tid = org.context_map.at(new_context);

  Query q(db, "UPDATE task SET context_tid={}, modified=datetime('now') WHERE id={};",
                                    context_tid, id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateTaskContext': {}", error);
    return;
  }
  // doesn't get called
  //sess.showOrgMessage3("Update task context succeeded (new version)");
}

void updateTaskFolder(std::string& new_folder, int id) {
  int folder_tid = org.folder_map.at(new_folder);

  Query q(db, "UPDATE task SET folder_tid={}, modified=datetime('now') WHERE id={};",
                                    folder_tid, id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateTaskContext': {}", error);
    return;
  }
}

void updateNote(void) {

  std::string text = sess.p->editorRowsToString();

  // need to escape single quotes with two single quotes
  size_t pos = text.find("'");
  while(pos != std::string::npos) {
    text.replace(pos, 1, "''");
    pos = text.find("'", pos + 2);
  }


  Query q(db, "UPDATE task SET note='{}', modified=datetime('now'), "
              "startdate=datetime('now', '-{} hours') where id={}",
              text, TZ_OFFSET, sess.p->id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateNote': {}", error);
    return;
  }
  /***************fts virtual table update*********************/

  Query q1(fts_db, "UPDATE fts SET note='{}' WHERE lm_id={}", text, sess.p->id);
  if (int res = q1.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateNote' (fts): {}", error);
    return;
  }

  sess.showOrgMessage3("Updated note and fts entry for item {} (new version)", sess.p->id);
  sess.refreshOrgScreen();
}

int getFolderTid(int id) {
  Query q(db, "SELECT folder_tid FROM task WHERE id={}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving entry info in copy_entry: {}", res);
    return -1;
  }
  return q.column_int(0);
}

void updateTitle(void) {

  orow& row = org.rows.at(org.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insertRow(row);
    return;
  }

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  Query q(db, "UPDATE task SET title='{}', modified=datetime('now') WHERE id={}",
              title, row.id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateTitle': {}", error);
    return;
  }

  /***************fts virtual table update*********************/

  Query q1(fts_db, "UPDATE fts SET title='{}' WHERE lm_id={}", title, row.id); //?->
  if (int res = q1.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateTitle' (fts): {}", error);
    return;
  }

  sess.showOrgMessage3("Updated title and fts entry for item {} (new version)", row.id);
  sess.refreshOrgScreen();
}

int insertRow(orow& row) {

  std::string title = row.title;
  size_t pos = title.find('\'');
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find('\'', pos + 2);
    }

  Query q(db, "INSERT INTO task (priority, title, folder_tid, context_tid, "
              "star, added, note, deleted, created, modified) "
              "VALUES (3, '{0}', {1}, {2}, True, date(), '', False, "
              "datetime('now', '-{3} hours'), "
              "datetime('now'));", 
              title,
             (org.folder == "") ? 1 : org.folder_map.at(org.folder),
             (org.context == "") ? 1 : org.context_map.at(org.context),
             TZ_OFFSET);

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
  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'insertRow': {}", error);
    org.mode = NORMAL;
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db.db);
  row.dirty = false;


  /***************fts virtual table update*********************/

  //should probably create a separate function that is a klugy
  //way of making up for fact that pg created tasks don't appear in fts db
  //"INSERT OR IGNORE INTO fts (title, lm_id) VALUES ('" << title << row.id << ");";
  /***************fts virtual table update*********************/

  Query q1(fts_db, "INSERT INTO fts (title, lm_id) VALUES ('{}', {});", title, row.id);
  if (int res = q1.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateTitle' (fts): {}", error);
    return -1;
  }
  sess.showOrgMessage3("Successfully inserted new row with id {} and indexed it (new vesrsion)", row.id);

  return row.id;
}

int keywordExists(const std::string &name) {
  Query q(db, "SELECT keyword.id FROM keyword WHERE keyword.name='{}';", name); 
  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'keyword_exists'; result code: {}", q.result);
    return -1;
  }
  // if there are no rows returned q.step() returns SQLITE_DONE = 101; SQLITE_ROW = 100
  if (q.step() == SQLITE_ROW) return q.column_int(0);
  else return -1;
}

// used in Editor.cpp
std::string getTitle(int id) {
  Query q(db, "SELECT title FROM task WHERE id={};", id); 
  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'getTitle'; result code: {}", q.result);
    return "";
  }
  // if there are no rows returned q.step() returns SQLITE_DONE = 101; SQLITE_ROW = 100
  if (q.step() == SQLITE_ROW) return q.column_text(0);
  else return "";
}

/* note that after changing a keyword's name would really have to update every entry
 * in the fts_db that had a tag that included that keyword
 */
void updateKeyword(void) {
  orow& row = org.rows.at(org.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insertKeyword(row);
    return;
  }
  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  Query q(db, "UPDATE keyword SET name='{}', modified=datetime('now') WHERE id={}",
                                   title, row.id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'updateKeyword': {}", error);
    return;
  }

  row.dirty = false;
  sess.showOrgMessage3("Successfully updated keyword {} in row {}", title, row.id);
}

int insertKeyword(orow& row) {
  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
  }

  //note below that the temp tid is zero for all inserted keywords
  Query q(db, "INSERT INTO keyword (name, star, deleted, modified, tid) "
          "VALUES ('{}', {}, False, datetime('now'), 0);",
          title, row.star);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'insertRow': {}", error);
    org.mode = NORMAL;
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db.db);
  row.dirty = false;
  sess.showOrgMessage3("Successfully inserted new keyword {} with id {} and indexed it (new version)", title, row.id);

  return row.id;
}

void updateRows(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  for (auto row: org.rows) {
    if (!(row.dirty)) continue;
    if (row.id != -1) {
      std::string title = row.title;
      size_t pos = title.find("'");
      while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }
      
      Query q(db, "UPDATE task SET title='{}', modified=datetime('now') WHERE id={}",
                                   title, row.id);

      if (int res = q.step(); res != SQLITE_DONE) {
        std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
        sess.showOrgMessage3("Problem in 'updateKeyword': {}", error);
        return;
      }
      row.dirty = false;
      updated_rows[n] = row.id;
      n++;
    } else { 
      int id  = insertRow(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    sess.showOrgMessage("There were no rows to update");
    return;
  }

  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    org.rows.at(updated_rows[j]).dirty = false; // 10-28-2019
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  sess.showOrgMessage("%s",  msg);
}

void readNoteIntoVec(int id) {
  if (id ==-1) return; // id given to new and unsaved entries

  Query q(db, "SELECT note FROM task WHERE id = {}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving note from itemi {}: {}", id, res);
    return;
  }
  std::string note = q.column_text(0);
  std::erase(note, '\r'); //c++20
  std::stringstream sNote(note);
  std::string s;
  while (getline(sNote, s, '\n')) {
    sess.p->editorInsertRow(sess.p->rows.size(), s);
  }
  sess.p->dirty = 0; //assume editorInsertRow increments dirty so this needed
  if (!sess.p->linked_editor) return;

  sess.p->linked_editor->rows = std::vector<std::string>{" "};

  /* below works but don't think we want to store output_window
  db.query("SELECT subnote FROM task WHERE id = {}", id);
  db.callback = note_callback;
  db.pArg = p->linked_editor;
  run_sql();
  */

}

void readNoteIntoPreviewVec(int id) {
  org.preview_rows.clear();
  if (id ==-1) return; // id given to new and unsaved entries

  Query q(db, "SELECT note FROM task WHERE id = {}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving note from itemi {}: {}", id, res);
    return;
  }
  std::string note = q.column_text(0);
  if (note == "") return;
  std::erase(note, '\r'); //c++20
  std::stringstream sNote(note);
  std::string s;
  while (getline(sNote, s, '\n')) {
    org.preview_rows.push_back(s);
  }
}
void getNoteSearchPositions(int id) {
  Query q(fts_db, "SELECT rowid FROM fts WHERE lm_id = {};", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving note from itemi {}: {}", id, res);
    return;
  }
  int rowid = q.column_int(0); //should ? return -1 if nullptr

  //if (!sess.db_query(sess.S.fts_db, query.str().c_str(), rowid_callback, &rowid, &sess.S.err_msg, __func__)) return;

 // std::vector<std::vector<int>> word_positions = {};//static
  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(sess.fts_search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);

  for(auto v: vec) {
    org.word_positions.push_back(std::vector<int>{});
    Query q1(fts_db, "SELECT offset FROM fts_v WHERE doc ={} AND term = '{}' AND col = 'note';", rowid, v);
    while (q1.step() == SQLITE_ROW) 
      org.word_positions.back().push_back(q1.column_int(0));
  }

  int ww = (org.word_positions.at(0).empty()) ? -1 : org.word_positions.at(0).at(0);
  sess.showOrgMessage("Word position first: %d; id = %d (new)", ww, id);

  //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
  /*
  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  } 
  */
}

void generateContextMap(void) {
  // note it's tid because it's sqlite
  Query q(db, "SELECT tid, title FROM context;"); 
  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'map_context_titles'; result code: {}", q.result);
    return;
  }

  while (q.step() == SQLITE_ROW) {
    org.context_map[q.column_text(1)] = q.column_int(0);
  }
}

void generateFolderMap(void) {
  // note it's tid because it's sqlite
  Query q(db, "SELECT tid,title FROM folder;"); 
  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'map_folder_titles'; result code: {}", q.result);
    return;
  }
  while (q.step() == SQLITE_ROW) {
    org.folder_map[q.column_text(1)] = q.column_int(0);
  }
}

std::pair<std::string, std::vector<std::string>> getTaskKeywords(int id) {

  Query q(db, "SELECT keyword.name FROM task_keyword LEFT OUTER JOIN keyword ON "
              "keyword.id=task_keyword.keyword_id WHERE {}=task_keyword.task_id;",
              id);

  std::vector<std::string> task_keywords = {}; 
  while (q.step() == SQLITE_ROW) {
    task_keywords.push_back(q.column_text(0));
 }

  if (task_keywords.empty()) return std::make_pair(std::string(), std::vector<std::string>());

  std::string s = fmt::format("{}", fmt::join(task_keywords, ","));
  return std::make_pair(s, task_keywords);
}

void getContainers(void) {
  org.rows.clear();
  org.fc = org.fr = org.rowoff = 0;

  std::string table;
  std::string column = "title"; //only needs to be change for keyword

  switch (org.view) {
    case CONTEXT:
      table = "context";
      break;
    case FOLDER:
      table = "folder";
      break;
    case KEYWORD:
      table = "keyword";
      column = "name";
      break;
    default:
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }
 
  Query q(db, "SELECT * FROM {} ORDER BY {} COLLATE NOCASE ASC;", table, column);

  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'getContainers'; result code: {}", q.result);
    return;
  }

  switch(org.view) {
    case CONTEXT:
      while (q.step() == SQLITE_ROW) {
        orow row;
        row.id = q.column_int(0);
        row.title = q.column_text(2);
        row.star = q.column_bool(3);
        row.deleted = q.column_bool(5);
        row.modified = time_delta_(q.column_text(9));

        row.completed = false;
        row.dirty = false;
        row.mark = false;

        org.rows.push_back(row);
      }
      break;
    case FOLDER:
      while (q.step() == SQLITE_ROW) {
        orow row;
        row.id = q.column_int(0);
        row.title = q.column_text(2);
        row.star = q.column_bool(3);
        row.deleted = q.column_bool(7);
        row.modified = time_delta_(q.column_text(11));

        row.completed = false;
        row.dirty = false;
        row.mark = false;

        org.rows.push_back(row);
      }
      break;
    case KEYWORD:
      while (q.step() == SQLITE_ROW) {
        orow row;
        row.id = q.column_int(0);
        row.title = q.column_text(1);
        row.star = q.column_bool(3);
        row.deleted = q.column_bool(5);
        row.modified = time_delta_(q.column_text(4));

        row.completed = false;
        row.dirty = false;
        row.mark = false;

        org.rows.push_back(row);
      }
      break;
  }

  if (org.rows.empty()) {
    sess.showOrgMessage("No results were returned");
    org.mode = NO_ROWS;
  } 
 
  // below should be somewhere else
  org.context = org.folder = org.keyword = ""; // this makes sense if you are not in an O.view == TASK
}


std::string time_delta_(std::string t) {
  struct std::tm tm = {};
  std::istringstream iss;
  iss.str(t);
  iss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));

  //auto now = std::chrono::utc_clock::now(); //unfortunately c++20 and not available yet
  auto now = std::chrono::system_clock::now(); //this needs to be utc
  std::chrono::duration<double> elapsed_seconds = now-tp; //in seconds but with stuff to right of decimal

  /* this didn't work as a kluge
  //from https://stackoverflow.com/questions/63501664/current-utc-time-point-in-c
  auto now =std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()); 
  std::chrono::duration<double> elapsed_seconds = now.time_since_epoch()-tp.time_since_epoch(); //in seconds but with stuff to right of decimal
  */

  auto int_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed_seconds);
  int adj_secs = (int)int_secs.count() + 18000; //kluge that requires tz adjustment; need utc_clock

  std::string s;

  if (adj_secs <= 120) s = fmt::format("{} seconds ago", adj_secs);
  else if (adj_secs <= 60*120) s = fmt::format("{} minutes ago", adj_secs/60); // <120 minutes we report minutes
  else if (adj_secs <= 48*60*60) s = fmt::format("{} hours ago", adj_secs/3600); // <48 hours report hours
  else if (adj_secs <= 24*60*60*60) s = fmt::format("{} days ago", adj_secs/3600/24); // <60 days report days
  else if (adj_secs <= 24*30*24*60*60) s = fmt::format("{} months ago", adj_secs/3600/24/30); // <24 months report months
  else s = fmt::format("{} years ago", adj_secs/3600/24/30/12);
 
  return s;
}

void searchDB(const std::string & st, bool help) {

  org.rows.clear();
  org.fc = org.fr = org.rowoff = 0;


  Query q(fts_db, "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') " 
              "FROM fts WHERE fts MATCH '{}' ORDER BY bm25(fts, 2.0, 1.0, 5.0);",
               st);

  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'searchDB'; result code: {}", q.result);
    return;
  }

  org.fts_ids.clear();
  org.fts_titles.clear();
  while (q.step() == SQLITE_ROW) {
    int id = q.column_int(0);
    org.fts_ids.push_back(id);
    org.fts_titles[id] = std::string(q.column_text(1));
  }

  if (org.fts_ids.empty()) {
    sess.showOrgMessage("No results were returned");
    sess.eraseRightScreen(); //note can still return no rows from get_items_by_id if we found rows above that were deleted
    org.mode = NO_ROWS;
    return;
  }
  std::stringstream stmt;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  if (help)
    stmt << "SELECT * FROM task WHERE task.context_tid = 16 and task.id IN (";
  else 
    stmt << "SELECT * FROM task WHERE task.id IN (";

  //for (int i = 0; i < fts_counter-1; i++) {
  int max = org.fts_ids.size() - 1;
  for (int i=0; i < max; i++) {
    stmt << org.fts_ids[i] << ", ";
  }
  //query << fts_ids[fts_counter-1]
  stmt << org.fts_ids[max]
        << ")"
        << ((!org.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  //for (int i = 0; i < fts_counter-1; i++) {
  for (int i=0; i < max; i++) {
    stmt << "task.id = " << org.fts_ids[i] << " DESC, ";
  }
  //query << "task.id = " << fts_ids[fts_counter-1] << " DESC";
  stmt << "task.id = " << org.fts_ids[max] << " DESC";

  //get_items_by_id(query.str());
  Query q1(db, stmt.str()); 

  if (q1.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'searchDB' (part 2); result code: {}", q1.result);
    return;
  }
  while (q1.step() == SQLITE_ROW) {
    orow row;
    row.title = q1.column_text(3);
    row.id = q1.column_int(0);
    row.fts_title = org.fts_titles.at(row.id);
    row.star = q1.column_bool(8);
    row.deleted = q1.column_bool(14);
    row.completed = (q1.column_text(10) != "") ? true: false;

    // we're not giving user choice of time column here but could 
    if (q1.column_text(16) != "") row.modified = time_delta_(q1.column_text(16));
    else row.modified.assign(15, ' ');

    row.dirty = false;
    row.mark = false;

    org.rows.push_back(row);
  }
  if (org.rows.empty()) {
    sess.showOrgMessage("No results were returned");
    org.mode = NO_ROWS;
    sess.eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    org.mode = FIND;
    sess.drawPreviewWindow(org.rows.at(org.fr).id); //if id == -1 does not try to retrieve note
  }
}

void getItems(int max) {
  std::stringstream stmt;
  std::vector<std::string> keyword_vec;

  org.rows.clear();
  org.fc = org.fr = org.rowoff = 0;

  if (org.taskview == BY_CONTEXT) {
    stmt << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " WHERE context.title = '" << org.context << "' ";
  } else if (org.taskview == BY_FOLDER) {
    stmt << "SELECT * FROM task JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE folder.title = '" << org.folder << "' ";
  } else if (org.taskview == BY_RECENT) {
    stmt << "SELECT * FROM task WHERE 1=1";
  } else if (org.taskview == BY_JOIN) {
    stmt << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE context.title = '" << org.context << "'"
          << " AND folder.title = '" << org.folder << "'";
  } else if (org.taskview == BY_KEYWORD) {

 // if O.keyword has more than one keyword
    std::string k;
    std::stringstream skeywords;
    skeywords << org.keyword;
    while (getline(skeywords, k, ',')) {
      keyword_vec.push_back(k);
    }

    stmt << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

    for (auto it = keyword_vec.begin(); it != keyword_vec.end() - 1; ++it) {
      stmt << "keyword.name = '" << *it << "' OR ";
    }
    stmt << "keyword.name = '" << keyword_vec.back() << "')";

  } else {
    sess.showOrgMessage("You asked for an unsupported db query");
    return;
  }

  stmt << ((!org.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << org.sort
        << " DESC LIMIT " << max;

  int sortcolnum = org.sort_map.at(org.sort);
  Query q(db, stmt.str()); 

  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'getItems'; result code: {}", q.result);
    return;
  }
  while (q.step() == SQLITE_ROW) {
    orow row;
    row.id = q.column_int(0);
    row.title = q.column_text(3);
    row.star = q.column_bool(8);
    row.deleted = q.column_bool(14);
    row.completed = (q.column_text(10) != "") ? true: false;

    if (q.column_text(sortcolnum) != "") row.modified = time_delta_(q.column_text(sortcolnum));
    else row.modified.assign(15, ' ');

    row.dirty = false;
    row.mark = false;

    org.rows.push_back(row);
  }

  org.view = TASK;

  if (org.rows.empty()) {
    sess.showOrgMessage("No results were returned");
    org.mode = NO_ROWS;
    sess.eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    org.mode = org.last_mode;
    sess.drawPreviewWindow(org.rows.at(org.fr).id); //if id == -1 does not try to retrieve note
  }
}
// Not sure if this type of function should be here
// or in Organizer
// needs its callback
Container getContainerInfo(int id) {
  Container c = {};
  if (id ==-1) return c;

  std::string table;
  std::string count_query;

  switch(org.view) {
    case CONTEXT:
      table = "context";
      count_query = "SELECT COUNT(*) FROM task JOIN context ON context.tid = task.context_tid WHERE context.id={}";
      break;
    case FOLDER:
      table = "folder";
      count_query = "SELECT COUNT(*) FROM task JOIN folder ON folder.tid = task.folder_tid WHERE folder.id={}";
      break;
    case KEYWORD:
      table = "keyword";
      count_query = "SELECT COUNT(*) FROM task_keyword WHERE keyword_id={}";
      break;
    default:
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return c;
  }

  Query q1(db, count_query, id);
  if (int res = q1.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving container info in getContainerInfo: {}", res);
    return c;
  }
  c.count = q1.column_int(0);
    
  Query q2(db, "SELECT * FROM {} WHERE id ={};", table, id);
  if (int res = q2.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving container info in getContainerInfo(2): {}", res);
    return c;
  }
  switch(org.view) {
    case CONTEXT:
      c.id = q2.column_int(0);
      c.tid = q2.column_int(1);
      c.title = q2.column_text(2);
      c.star = q2.column_bool(3);
      c.created = q2.column_text(4);
      c.deleted = q2.column_bool(5);
      c.modified = q2.column_text(9);
      break;
    case FOLDER:
      c.id = q2.column_int(0);
      c.tid = q2.column_int(1);
      c.title = q2.column_text(2);
      c.star = q2.column_bool(3);
      c.created = q2.column_text(6);
      c.deleted = q2.column_bool(7);
      c.modified = q2.column_text(11);
      break;
    case KEYWORD:
      c.id = q2.column_int(0);
      c.tid = q2.column_int(2);
      c.title = q2.column_text(1);
      c.star = q2.column_bool(3);
      c.deleted = q2.column_bool(5);
      c.modified = q2.column_text(4);
      break;
  }
  return c;
}
std::string generateWWString(std::vector<std::string> &rows, int width, int length, std::string ret) {
  if (rows.empty()) return "";

  std::string ab = "";
  //int y = -line_offset; **set to zero because always starting previews at line 0**
  int y = 0;
  int filerow = 0;

  for (;;) {
    //if (filerow == rows.size()) {last_visible_row = filerow - 1; return ab;}
    if (filerow == rows.size()) return ab;

    std::string_view row = rows.at(filerow);
    
    if (row.empty()) {
      if (y == length - 1) return ab;
      ab.append(ret);
      filerow++;
      y++;
      continue;
    }

    size_t pos;
    size_t prev_pos = 0; //this should really be called prev_pos_plus_one
    for (;;) {
      // if remainder of line is less than screen width
      if (prev_pos + width > row.size() - 1) {
        ab.append(row.substr(prev_pos));

        if (y == length - 1) return ab;
        ab.append(ret);
        y++;
        filerow++;
        break;
      }

      pos = row.find_last_of(' ', prev_pos + width - 1);
      if (pos == std::string::npos || pos == prev_pos - 1) {
        pos = prev_pos + width - 1;
      }
      ab.append(row.substr(prev_pos, pos - prev_pos + 1));
      if (y == length - 1) return ab; //{last_visible_row = filerow - 1; return ab;}
      ab.append(ret);
      y++;
      prev_pos = pos + 1;
    }
  }
}

void highlight_terms_string(std::string &text) {

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\f\\"; //must have \f if using as placeholder

  for (auto v: org.word_positions) { //v will be an int vector of word positions like 15, 30, 70
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
#endif
