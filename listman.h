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
#include <libpq-fe.h>
#include <string>
#include <vector> // doesn't seem necessary ? why
#include <unordered_set>
#include <fstream>
#include <set>
#include <chrono>
#include "organizer.h"

const std::string DB_INI = "db.ini";
const int SMARTINDENT = 4; //should be in config
constexpr char BASE_DATE[] = "1970-01-01 00:00";

const std::unordered_set<std::string> quit_cmds = {"quit", "q", "quit!", "q!", "x"};
const std::unordered_set<std::string> insert_cmds = {"I", "i", "A", "a", "o", "O", "s", "cw", "caw"};
const std::unordered_set<std::string> file_cmds = {"savefile", "save", "readfile", "read"};

//these are not really move only but are commands that don't change text and shouldn't trigger a new push_current diff record
//better name something like no_edit_cmds or non_edit_cmds
const std::unordered_set<std::string> move_only = {"w", "e", "b", "0", "$", ":", "*", "n", "[s","]s", "z=", "gg", "G", "yy"}; //could put 'u' ctrl-r here

struct autocomplete {
  std::string prevfilename;
  std::vector<std::string> completions;
  std::string prefix;
  int completion_index;
};

void outlineProcessKeypress(int c = 0);
bool editorProcessKeypress(void);
void openInVim(void);

void navigate_page_hx(int direction);
void navigate_cmd_hx(int direction);

//Database-related Prototypes
void readNoteIntoEditor(int id);
std::string readNoteIntoString(int id);
void generateContextMap(void);
void generateFolderMap(void);
void updateNote(void);/////////////////////////////

int insertContainer(orow& row);
void updateContainer(void);
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
void copyEntry(void);

std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int); // puts them in comma delimited string

std::string now(void);

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

