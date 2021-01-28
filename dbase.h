#ifndef DBASE_H
#define DBASE_H

#include <sqlite3.h>
#include <string>
#include <fmt/core.h>

const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";

//typedef int (*sq_callback)(void *, int, char **, char **); //sqlite callback type
using sq_callback = int (*)(void *, int, char **, char **);

class Sqlite {
  public:
    Sqlite(std:: string db_path);

    template<typename... Args>
    void query(fmt::string_view format_str, const Args & ... args) {
      fmt::format_args argspack = fmt::make_format_args(args...);
      sql = fmt::vformat(format_str, argspack);
    }

    bool run();
    void params(sq_callback, void *);

    sqlite3 *db;
    sq_callback callback;
    void *pArg;
    char **errmsg;
    std::string sql;
};

extern Sqlite db;
extern Sqlite fts_db;

class Query {
  public:
    //note the constructor must take a reference to db
    //if not you construct a copy
    template<typename... Args>
    Query(Sqlite &db, fmt::string_view format_str, const Args & ... args) {
      fmt::format_args argspack = fmt::make_format_args(args...);
      sql = fmt::vformat(format_str, argspack);
      result = sqlite3_prepare_v2(db.db, sql.c_str(), -1, &res, 0);
    }

    ~Query() {
     //std::cout << "About to finalize" << std::endl; 
     sqlite3_finalize(res);
     //std::cout << "Finalized" << std::endl; 
    }

    int step(void);
    std::string column_text(int);
    int column_int(int);
    bool column_bool(int);

    std::string sql;
    int result;
    sqlite3_stmt *res;
};

// a Query would just be Query2("SELECT * FROM task WHERE id={};")
// Another disadvantage would be you would need a Query_fts(
// So not currently in use
class Query2 {
  public:
    //note the constructor must take a reference to db
    //if not you construct a copy
    template<typename... Args>
    Query2(fmt::string_view format_str, const Args & ... args) {
      fmt::format_args argspack = fmt::make_format_args(args...);
      sql = fmt::vformat(format_str, argspack);
      result = sqlite3_prepare_v2(db.db, sql.c_str(), -1, &res, 0);
    }

    ~Query2() {
     //std::cout << "About to finalize" << std::endl; 
     sqlite3_finalize(res);
     //std::cout << "Finalized" << std::endl; 
    }

    int step(void);
    std::string column_text(int);
    int column_int(int);
    bool column_bool(int);

    std::string sql;
    int result;
    sqlite3_stmt *res;
    static Sqlite db;
    static Sqlite fts_db;
};
#endif
