#include "Dbase.h"

//typedef int (*sq_callback)(void *, int, char **, char **); //sqlite callback type
//using sq_callback = int (*)(void *, int, char **, char **);

Sqlite db = Sqlite(SQLITE_DB__); //global; extern Session sess in session.h
Sqlite fts_db = Sqlite(FTS_DB__); //global; extern Session sess in session.h

Sqlite::Sqlite(std::string db_path) {
  int rc = sqlite3_open(db_path.c_str(), &db);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    exit(1);
  }
}

//bool Sqlite::db_query(sqlite3 *db, const std::string& sql, sq_callback callback, void *pArg, char **errmsg, const char *func) {
bool Sqlite::run() {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     //outlineShowMessage("SQL error in %s: %s", func, errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   pArg = nullptr;
   callback = nullptr;
   return true;
}

void Sqlite::params(sq_callback cb, void * arg) {
  callback = cb;
  pArg = arg;
}

int Query::step(void) {
  return sqlite3_step(res);
 }
std::string Query::column_text(int col) {
  if (sqlite3_column_text(res, col) == nullptr) return "";
  return std::string(reinterpret_cast<const char *>(sqlite3_column_text(res, col)));
}

int Query::column_int(int col) {
  return sqlite3_column_int(res, col);
}

bool Query::column_bool(int col) {
  return (sqlite3_column_int(res, col) == 1) ? true : false;
}

// Not currently in use
int Query2::step(void) {
  return sqlite3_step(res);
 }
std::string Query2::column_text(int col) {
  return std::string(reinterpret_cast<const char *>(sqlite3_column_text(res, col)));
}

int Query2::column_int(int col) {
  return sqlite3_column_int(res, col);
}

bool Query2::column_bool(int col) {
  return (sqlite3_column_int(res, col) == 1) ? true : false;
}

