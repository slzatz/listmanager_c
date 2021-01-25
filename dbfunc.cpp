#ifndef OUTLINE_NORMAL_FUNCTIONS_H
#define OUTLINE_NORMAL_FUNCTIONS_H
#define TZ_OFFSET 5 // time zone offset - either 4 or 5
#define MAX 500 // max rows to bring back
#include "session.h"
#include "common.h"
#include "organizer.h"
#include "dbase.h"
#include <string>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <fmt/chrono.h>

int insertRow(orow& row);
int insertKeyword(orow& row);
std::string timeDelta(std::string t);

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

std::string readNoteIntoString(int id) {
  if (id ==-1) return ""; // id given to new and unsaved entries

  Query q(db, "SELECT note FROM task WHERE id = {}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem in readNoteIntoString for item {}: {}", id, res);
    return "";
  }
  std::string note = q.column_text(0);
  std::erase(note, '\r'); //c++20
  return note;
}

void readNoteIntoEditor(int id) {
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
}
void linkEntries(int id1, int id2) {
  Query q(sess.db, "INSERT OR IGNORE INTO link (task_id0, task_id1) VALUES ({}, {});",
              id1, id2);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in linkEntries: {}", res);
    return;
  }

  Query q1(sess.db,"UPDATE link SET modified = datetime('now') WHERE task_id0={} AND task_id1={};", id1, id2);
  if (int res = q1.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in linkEntries: {}", res);
    return;
  }
}

std::pair<int, int> getLinkedEntry(int id) {
  Query q(sess.db, "SELECT task_id0, task_id1 FROM link WHERE task_id0={} OR task_id1={}", id, id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving linked item: {}", res);
    return std::make_pair(-1, -1);
  }

  return std::make_pair(q.column_int(0), q.column_int(1));
}

std::vector<std::vector<int>> getNoteSearchPositions(int id) {
  std::vector<std::vector<int>> word_positions = {};//static
  Query q(fts_db, "SELECT rowid FROM fts WHERE lm_id = {};", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving note from itemi {}: {}", id, res);
    return word_positions;
  }
  int rowid = q.column_int(0); //should ? return -1 if nullptr

  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(sess.fts_search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);

  for(auto v: vec) {
    //org.word_positions.push_back(std::vector<int>{});
    word_positions.push_back(std::vector<int>{});
    Query q1(fts_db, "SELECT offset FROM fts_v WHERE doc ={} AND term = '{}' AND col = 'note';", rowid, v);
    while (q1.step() == SQLITE_ROW){ 
      //org.word_positions.back().push_back(q1.column_int(0));
      word_positions.back().push_back(q1.column_int(0));
    }
  }

  /* debugging
  int ww = (word_positions.at(0).empty()) ? -1 : word_positions.at(0).at(0);
  sess.showOrgMessage("Word position first: %d; id = %d (new)", ww, id);
  */

  return word_positions;
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
        row.modified = timeDelta(q.column_text(9));

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
        row.modified = timeDelta(q.column_text(11));

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
        row.modified = timeDelta(q.column_text(4));

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
    if (q1.column_text(16) != "") row.modified = timeDelta(q1.column_text(16));
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

    if (q.column_text(sortcolnum) != "") row.modified = timeDelta(q.column_text(sortcolnum));
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

//overload that takes keyword_id and task_id
void addTaskKeyword(int keyword_id, int task_id, bool update_fts) {

  Query q(db, "INSERT OR IGNORE INTO task_keyword (task_id, keyword_id) VALUES ({}, {});",
              task_id, keyword_id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'addTaskKeyword': (keyword id) {}", error);
    return;
  }

   Query q1(db,"UPDATE task SET modified = datetime('now') WHERE id={};", task_id);
   q1.step();
  // *************fts virtual table update**********************
  if (!update_fts) return;
  std::string s = getTaskKeywords(task_id).first;
  Query q2(fts_db, "UPDATE fts SET tag='{}' WHERE lm_id={};", s, task_id);

  if (int res = q2.step(); res != SQLITE_DONE)
     sess.showOrgMessage3("Problem inserting in fts; result code: {}", res);
}

//overload that takes keyword name and task_id
void addTaskKeyword(std::string &kws, int id) {

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

    /*
    IF NOT EXISTS(SELECT 1 FROM keyword WHERE name = 'mango') INSERT INTO keyword (name) VALUES ('mango')
      <- doesn't work for sqlite
      note you don't have to do INSERT OR IGNORE but could just INSERT since unique constraint
      on keyword.name but you don't want to trigger an error either so probably best to retain
      INSERT OR IGNORE there is a default that tid = 0 but let's do it explicity
      */

    Query q(db, "INSERT OR IGNORE INTO keyword (name, tid, star, modified, deleted) VALUES ("
                "'{}', 0, true, datetime('now'), false);", kw);

    if (int res = q.step(); res != SQLITE_DONE) {
      std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
      sess.showOrgMessage3("Problem in 'addTaskKeyword' (string): {}", error);
      return;
    }

    Query q2(db, "INSERT OR IGNORE INTO task_keyword (task_id, keyword_id) SELECT "
                 "{}, keyword.id FROM keyword WHERE keyword.name = '{}';", id, kw);

    if (int res = q2.step(); res != SQLITE_DONE) {
      std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
      sess.showOrgMessage3("Problem in 'addTaskKeyword' (string): {}", error);
      return;
    }

    // updates task modified column so we know that something changed with the task
    Query q3(db, "UPDATE task SET modified = datetime('now') WHERE id={};", id);
    if (int res = q3.step(); res != SQLITE_DONE) {
      std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
      sess.showOrgMessage3("Problem in 'addTaskKeyword' (string): {}", error);
      return;
    }
  }
  std::string s = getTaskKeywords(id).first; // 11-10-2020
  Query q4(fts_db, "UPDATE fts SET tag='{}' WHERE lm_id={};", s, id);

  if (int res = q4.step(); res != SQLITE_DONE)
    sess.showOrgMessage3("Problem inserting in fts; result code: {}", res);
}

void copyEntry(void) {

  int id = getId();
  Query q(db, "SELECT * FROM task WHERE id={}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving entry info in copy_entry: {}", res);
    return;
  }
  int priority = q.column_int(2);
  std::string title = "Copy of " + q.column_text(3);
  int folder_tid = q.column_int(5);
  int context_tid = q.column_int(6);
  bool star = q.column_bool(8);

  size_t pos;
  pos = title.find('\'');
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find('\'', pos + 2);
    }

  std::string note = q.column_text(12);
  pos = note.find("'");
  while(pos != std::string::npos) {
    note.replace(pos, 1, "''");
    pos = note.find("'", pos + 2);
  }

  Query q1(db, "INSERT INTO task (priority, title, folder_tid, context_tid, "
                                   "star, added, note, deleted, created, modified) "
                                   "VALUES ({0}, '{1}', {2}, {3}, {4}, date(), '{5}', False, "
                                   "datetime('now', '-{6} hours'), "
                                   "datetime('now'));", 
                                   priority,
                                   title,
                                   folder_tid,
                                   context_tid,
                                   star,
                                   note,
                                   TZ_OFFSET);

  if (int res = q1.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem inserting in copy_entry: {}", res);
    return;
  }

  int new_id =  sqlite3_last_insert_rowid(sess.db.db);

  Query q2(db, "SELECT task_keyword.keyword_id FROM task_keyword WHERE task_keyword.task_id={};",
              id);

  std::vector<int> task_keyword_ids = {}; 
  while (q2.step() == SQLITE_ROW) {
    task_keyword_ids.push_back(q2.column_int(0));
  }

  for (const int &k : task_keyword_ids) {
    addTaskKeyword(k, new_id, false); //don't update fts; done below
  }
  /***************fts virtual table update*********************/
  std::string tag = getTaskKeywords(new_id).first;
  Query q3(fts_db, "INSERT INTO fts (title, note, tag, lm_id) VALUES ('{}', '{}', '{}', {});", 
               title, note, tag, new_id); 

  if (int res = q3.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem inserting in fts in copy_entry: {}", res);
    return;
  }
  getItems(MAX);
}

void deleteKeywords(int id) {
  Query q(db, "DELETE FROM task_keyword WHERE task_id={};", id); 
  if (int res = q.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem in 'deleteKeywords': {}", res);
    return;
  }

  Query q2(db, "UPDATE task SET modified = datetime('now') WHERE id={};", id);
  if (int res = q2.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem in 'deleteKeywords': {}", res);
    return;
  }
  /**************fts virtual table update**********************/
  Query q3(fts_db, "UPDATE fts SET tag='' WHERE lm_id={}", id);
  if (int res = q3.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem in fts update in 'deleteKeywords' (fts): {}", res);
    return;
  }
}
//void display_item_info(void) {
Entry getEntryInfo(int id) {
  Entry e = {};
  if (id ==-1) return e;

  Query q(sess.db, "SELECT * FROM task WHERE id={};", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving container info in getEntryInfo: {}", res);
    return e;
  }

  e.id = q.column_int(0);
  e.tid = q.column_int(1);
  e.title = q.column_text(3);
  e.created = q.column_text(4);
  e.folder_tid = q.column_int((5));
  e.context_tid = q.column_int((6));
  e.star = q.column_bool(8);
  e.added = q.column_text(9);
  e.completed = q.column_text(10);
  e.deleted = q.column_bool(14);
  e.modified = q.column_text(16);

  return e;
}

/*Inserting a new keyword should not require any fts_db update. Just like any keyword
 *added to an entry - the tag created is entered into fts_db when that keyword is
 *attached to an entry.
*/
int insertContainer(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
  }

  Query q(db, "INSERT INTO {} (title, deleted, created, modified, tid, {}, textcolor) "
              "VALUES ('{}', False, datetime('now', '-{} hours'), datetime('now'), {}, False, 10);",
              (org.view == CONTEXT) ? "context" : "folder",
              (org.view == CONTEXT) ? "\"default\"" : "private", //con-> "default"; fol-> private
              title,
              TZ_OFFSET,
              sess.temporary_tid);

  if (int res = q.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem in 'insertContainer': {}", res);
    return -1;
  }

  sess.temporary_tid++;      

  row.id =  sqlite3_last_insert_rowid(db.db);
  row.dirty = false;

  sess.temporary_tid++;      
  sess.showOrgMessage3("Successfully inserted new {} with id {}", 
                      (org.view == CONTEXT) ? "context" : "folder", row.id);
  return row.id;

  /*
  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    sess.showOrgMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    sess.showOrgMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  sess.showOrgMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
  */
}
void updateContainer(void) {

  orow& row = org.rows.at(org.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insertContainer(row);
    return;
  }

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  Query q(db, "UPDATE {} SET title='{}', modified=datetime('now') WHERE id={}",
              (org.view == CONTEXT) ? "context" : "folder", title, row.id);
  if (int res = q.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem in 'updateConstainer' (fts): {}", res);
    return;
  }

  row.dirty = false;
  sess.showOrgMessage("Successfully updated row %d", row.id);
}
/*****************************Non-database-related utilities************************************/
std::string now(void) {
  std::time_t t = std::time(nullptr);
  return fmt::format("{:%H:%M}", fmt::localtime(t));
}

std::string timeDelta(std::string t) {
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

std::string generateWWString(const std::string &text, int width, int length, std::string ret) {

  if (text == "") return "";

  std::vector<std::string> rows;
  std::stringstream t(text);
  std::string s;
  while (getline(t, s, '\n')) {
    rows.push_back(s);
  }

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
void highlight_terms_string(std::string &text, std::vector<std::vector<int>> word_positions) {

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\f\\"; //must have \f if using it as placeholder

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
void updateCodeFile(void) {
  std::ofstream myfile;
  std::string file_path;
  std::string lsp_name;
  int tid = getFolderTid(sess.p->id);

  if (tid == 18) {
    file_path  = "/home/slzatz/clangd_examples/test.cpp";
    lsp_name = "clangd";
  } else {
    file_path = "/home/slzatz/go/src/example/main.go";
    lsp_name = "gopls";
  }

  myfile.open(file_path);
  myfile << sess.p->code;
  myfile.close();

  if (!sess.lsp_v.empty()) {
    auto it = std::ranges::find_if(sess.lsp_v, [&lsp_name](auto & lsp){return lsp->name == lsp_name;});
    if (it != sess.lsp_v.end()) (*it)->code_changed = true;
  }
}

void openInVim(void){
  std::string filename;
  if (getFolderTid(org.rows.at(org.fr).id) != 18) filename = "vim_file.txt";
  else filename = "vim_file.cpp";
  sess.p->editorSaveNoteToFile(filename);
  std::stringstream s;
  s << "vim " << filename << " >/dev/tty";
  system(s.str().c_str());
  sess.p->editorReadFileIntoNote(filename);
}

void signalHandler(int signum) {
  sess.getWindowSize();
  //that percentage should be in session
  // so right now this reverts back if it was changed during session
  sess.moveDivider(sess.cfg.ed_pct);
}
// currently used for sync log
void readFile(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  sess.display_text.str(std::string());
  sess.display_text.clear();

  while (getline(f, line)) {
    sess.display_text << line << '\n';
  }
  f.close();
}
#endif
