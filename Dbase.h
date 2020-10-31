#include <sqlite3.h>
#include <string>
#include <fmt/core.h>

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

 // private:
    sqlite3 *db;
    sq_callback callback;
    void *pArg;
    char **errmsg;
    std::string sql;
};
