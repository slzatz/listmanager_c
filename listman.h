#include <string>
#include <vector>
#include <unordered_set>
#include "common.h"

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

//Database-related Prototypes
void readNoteIntoEditor(int id);
std::string readNoteIntoString(int id);
void generateContextMap(void);
void generateFolderMap(void);
void updateNote(void);

int insertContainer(orow& row);
void updateContainerTitle(void);
void deleteKeywords(int id);
void addTaskKeyword(std::string &kws, int id);
void addTaskKeyword(int keyword_id, int task_id, bool update_fts=true);
void updateTaskContext(std::string &, int);
void updateTaskFolder(std::string &, int);
int getId(void);
//std::string getTitle(int); //right now only in Editor.cpp
void updateTitle(void);
void updateRows(void);
void toggleDeleted(void);
void toggleStar(void);
void toggleCompleted(void);
void touch(void);
int insertRow(orow&);
void getContainers(void); //has an if that determines callback: context_callback or folder_callback
int insertKeyword(orow &);
void updateKeywordTitle(void);
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

void readFile(const std::string &);
void displayFile(void);

//void update_solr(void); //works but not in use

