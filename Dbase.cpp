#include "Dbase.h"

//typedef int (*sq_callback)(void *, int, char **, char **); //sqlite callback type
//using sq_callback = int (*)(void *, int, char **, char **);

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

/*
Sqlite lm_db(LM_DB);
Sqlite fts_db(FTS_DB);

void do_something() {
  lm_db.query("fjalkdsf {} lkdsjflskdjflds {} lksdjfldsj {}", x, y, z);
  lm_db.run_query()
*/  
