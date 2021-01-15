#ifndef OUTLINE_NORMAL_FUNCTIONS_H
#define OUTLINE_NORMAL_FUNCTIONS_H
#define TZ_OFFSET 5 // time zone offset - either 4 or 5
#include "session.h"
#include "Common.h"
#include "Organizer.h"
#include "Dbase.h"
#include <string>

int insertRow(orow& row);

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
/*
// Not sure if this type of function should be here
// or in Organizer
// needs its callback
void display_container_info(int id) {
  if (id ==-1) return;

  std::string table;
  std::string count_query;
  int (*callback)(void *, int, char **, char **);

  switch(view) {
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
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }
  std::stringstream query;
  int count = 0;

  query << count_query << id;
    
  // note count obtained here but passed to the next callback so it can be printed
  if (!sess.db_query(sess.S.db, query.str().c_str(), count_callback, &count, &sess.S.err_msg)) return;

  std::stringstream query2;
  query2 << "SELECT * FROM " << table << " WHERE id = " << id;

  // callback is *not* called if result (argv) is null

  if (!sess.db_query(sess.S.db, query2.str().c_str(), callback, &count, &sess.S.err_msg)) return;

}
*/
#endif
