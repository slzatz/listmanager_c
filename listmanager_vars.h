// note that listmanager_vars.h is really the future expanded Organizer header
#define LEFT_MARGIN 2
#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define UNUSED(x) (void)(x)
#define MAX 500 // max rows to bring back
#define TZ_OFFSET 5 // time zone offset - either 4 or 5
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this
#define LEFT_MARGIN_OFFSET 4

// to use GIT_BRANCH in makefile (from cmake)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <Python.h>
#include <sys/ioctl.h>
#include <csignal>
//#include <termios.h>
#include <libpq-fe.h>
#include <sqlite3.h>
#include "inipp.h" // https://github.com/mcmtroffaes/inipp

#include <string>
#include <vector> // doesn't seem necessary ? why
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <set>
#include <chrono>
#include <iomanip>  //provides get_time used in time_delta function
#include <fmt/format.h>
//#include <fcntl.h> //file locking
#include "Common.h"
#include "Organizer.h"

const std::string SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
const std::string FTS_DB = "/home/slzatz/listmanager_cpp/fts5.db";
const std::string DB_INI = "db.ini";
const int SMARTINDENT = 4; //should be in config
constexpr char BASE_DATE[] = "1970-01-01 00:00";

std::vector<std::pair<int, int>> pos_mispelled_words; //row, col
int link_id = 0;
char link_text[20];

struct sqlite_db {
  sqlite3 *db;
  char *err_msg;
  sqlite3 *fts_db;
};
struct sqlite_db S;

const std::unordered_set<std::string> quit_cmds = {"quit", "q", "quit!", "q!", "x"};
const std::unordered_set<std::string> insert_cmds = {"I", "i", "A", "a", "o", "O", "s", "cw", "caw"};
const std::unordered_set<std::string> file_cmds = {"savefile", "save", "readfile", "read"};

//these are not really move only but are commands that don't change text and shouldn't trigger a new push_current diff record
//better name something like no_edit_cmds or non_edit_cmds
const std::unordered_set<std::string> move_only = {"w", "e", "b", "0", "$", ":", "*", "n", "[s","]s", "z=", "gg", "G", "yy"}; //could put 'u' ctrl-r here

struct config {
  std::string user;
  std::string password;
  std::string dbname;
  std::string hostaddr;
  int port;
  int ed_pct;
};
struct config c;

PGconn *conn = nullptr;

void outlineProcessKeypress(int = 0);
bool editorProcessKeypress(void);

void lsp_start(void);
void lsp_shutdown(void);

void navigate_page_hx(int direction);
void navigate_cmd_hx(int direction);

//Database-related Prototypes
void readNoteIntoVec(int id);
std::string readNoteIntoString(int id);
void generateContextMap(void);
void generateFolderMap(void);
void updateNote(void);/////////////////////////////

//void db_open(void);
void dbOpen(void);
void runSQL(void);

void deleteKeywords(int id);
void addTaskKeyword(std::string &kws, int id);
void addTaskKeyword(int keyword_id, int task_id, bool update_fts=true);
void updateTaskContext(std::string &, int);
void updateTaskFolder(std::string &, int);
int getId(void);/////////////////////////////////////////////////////////////////////////////////////////
std::string getTitle(int); //right now only in Editor.cpp
void updateTitle(void);
void updateRows(void);
void toggleDeleted(void);
void toggleStar(void);////////////////////////////////////////////////////////////////////////////////////
void toggleCompleted(void);
void touch(void);
int insertRow(orow&);
void getContainers(void); //has an if that determines callback: context_callback or folder_callback
int insertKeyword(orow &);
void updateKeyword(void);
int getFolderTid(int); 
std::pair<std::string, std::vector<std::string>> getTaskKeywords(int); // used in F_copy_entry
int keywordExists(const std::string &);  
void getItems(int); 
void searchDB(const std::string & st, bool help=false);
Container getContainerInfo(int id);
Entry getEntryInfo(int id);

void update_container(void);
int insert_container(orow&);

std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string

//void add_task_keyword(std::string &, int);
//void add_task_keyword(int, int, bool update_fts=true);
void display_item_info(int); 
void display_item_info(void); //ctrl-i in NORMAL mode 0x9
void display_item_info_pg(int);
void display_container_info(int);
std::string time_delta(std::string);
std::string now(void);

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
int title_callback(void *, int, char **, char **);
int offset_callback(void *, int, char **, char **);
int folder_tid_callback(void *, int, char **, char **); 
int context_info_callback(void *, int, char **, char **); 
int folder_info_callback(void *, int, char **, char **); 
int keyword_info_callback(void *, int, char **, char **);
int count_callback(void *, int, char **, char **);
int unique_data_callback(void *, int, char **, char **);
int preview_callback (void *, int, char **, char **);
/* end here */

void synchronize(int);

// Not used by Editor class
void readFile(const std::string &);
void displayFile(void);
//void eraseRightScreen(void); //erases the note section; redundant if just did an eraseScreenRedrawLines

//std::string generate_html(void);
//std::string generate_html2(void);
void load_meta(void);
void update_html_file(std::string &&);
void update_html_code_file(std::string &&);
void update_code_file(void);
//void editorSaveNoteToFile(const std::string &);
//void editorReadFileIntoNote(const std::string &); 

void update_solr(void); //works but not in use
void open_in_vim(void);

