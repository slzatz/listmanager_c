 /***  includes ***/

//#define _DEFAULT_SOURCE
//#define _BSD_SOURCE
//#define _GNU_SOURCE
#define CTRL_KEY(k) ((k) & 0x1f)
#define OUTLINE_ACTIVE 0 //tab should move back and forth between these
#define EDITOR_ACTIVE 1
#define OUTLINE_LEFT_MARGIN 2
#define TOP_MARGIN 1
//#define OUTLINE_RIGHT_MARGIN 2
//#define EDITOR_LEFT_MARGIN 55
#define NKEYS ((int) (sizeof(lookuptable)/sizeof(lookuptable[0])))
#define ABUF_INIT {NULL, 0}
#define DEBUG 1
#define UNUSED(x) (void)(x)
#define MAX 500

#include <Python.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <libpq-fe.h>
#include <iniparser.h>
#include <sqlite3.h>

/*** defines ***/

char *SQLITE_DB = "/home/slzatz/mylistmanager3/lmdb_s/mylistmanager_s.db";
char *which_db; //which db (sqlite or pg) are we using - command line ./listmanager_term s
int EDITOR_LEFT_MARGIN;
int NN = 0; //which context is being displayed on message line (if none then NN==0)
struct termios orig_termios;
int screenlines, screencols;
int initial_file_row = 0; //for arrowing or displaying files
bool editor_mode;
char search_terms[50];

char *context[] = {
                        "", //maybe should be "search" - we'll see
                        "No Context", // 1
                        "financial", // 2
                        "health", // 3
                        "industry",// 4
                        "journal",// 5
                        "facts", // 6
                        "not work",// 7
                        "programming",// 8
                        "todo",// 9
                        "test",// 10
                        "work"//11
                       }; 


enum outlineKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  SHIFT_TAB
};

enum Mode {
  NORMAL = 0,
  INSERT = 1,
  COMMAND_LINE = 2,
  VISUAL_LINE = 3, //outline mode does not have VISUAL_LINE
  VISUAL = 4,
  REPLACE = 5,
  DATABASE = 6,
  FILE_DISPLAY = 7
};

char *mode_text[] = {
                        "NORMAL",
                        "INSERT",
                        "COMMAND LINE",
                        "VISUAL_LINE",
                        "VISUAL",
                        "REPLACE",
                        "DATABASE"
                       }; 

enum Command {
  C_caw = 2000,
  C_cw,
  C_daw,
  C_dw,
  C_de,
  C_d$,
  C_dd,//could just toggle deleted
  C_indent,
  C_unindent,
  C_c$,
  C_gg,
  C_yy,

  C_open,

  C_find,

  C_new, //create a new item

  C_context, //change an item's context

  C_update, //update solr db

  C_synch, // synchronixe sqlite and postgres dbs
  C_synch_test,//show what sync would do but don't do it 

  C_quit,
  C_quit0,

  C_help,

  C_edit
};

//below is for multi-character commands
//does a lookup to see which enum (representing a corresponding command) was matched
//so can be used in a case statement
typedef struct { char *key; int val; } t_symstruct;
static t_symstruct lookuptable[] = {
  {"caw", C_caw},
  {"cw", C_cw},
  {"daw", C_daw},
  {"dw", C_dw},
  {"de", C_de},
  {"dd", C_dd},
  {">>", C_indent},
  {"<<", C_unindent},
  {"gg", C_gg},
  {"yy", C_yy},
  {"d$", C_d$},

  {"help", C_help},
  {"open", C_open},
  // doesn't work if you use arrow keys
  {"o", C_open}, //need 'o' because this is command with target word
  {"fin", C_find},
  {"find", C_find},
  {"new", C_new}, //don't need "n" because there is no target
  {"context", C_context},
  {"con", C_context},
  // doesn't work if you use arrow keys
  {"c", C_context}, //need because there is a target
  {"update", C_update},
  {"sync", C_synch},
  {"synch", C_synch},
  {"synchronize", C_synch},
  {"test", C_synch_test},
  {"synchtest", C_synch_test},
  {"synch_test", C_synch_test},
  {"quit", C_quit},
  {"quit!", C_quit0},
  {"q!", C_quit0},
  {"edit", C_edit}
  //{"e", C_edit}
};

char search_string[30] = {'\0'}; //used for '*' and 'n' searches
// buffers below for yanking
char *line_buffer[20] = {NULL}; //yanking lines
char string_buffer[200] = {'\0'}; //yanking chars ******* this needs to be malloc'd

/*** data ***/

 /*
 row size = n means there are n chars starting with chars[0] and ending with 
 chars[n-1] and there is also an additional char = '\0' at chars[n] so memmove
 generally has to move n+1 bytes to include the terminal '\0'
 For the avoidance of doubt:  row->size does not include the terminating '\0' char
 Lastly size 0 means 0 visible chars but there should be '\0' 
*/

typedef struct orow {
  int size; //the number of characters in the line -- doesn't seem necessary - why not just use strlen(chars) [renamed to title]
  char *chars; //points at the character array of a row - mem assigned by malloc

  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  bool dirty;
  
} orow;

typedef struct erow {
  int size; //the number of characters in the line
  char *chars; //points at the character array of a row - mem assigned by malloc
} erow;

/*** append buffer used for writing to the screen***/

struct abuf {
  char *b;
  int len;
};

struct outlineConfig {
  int cx, cy; //cursor x and y position 
  int fc, fr; // file x and y position
  int rowoff; //the number of rows the view is scrolled (aka number of top rows now off-screen
  int coloff; //the number of columns the view is scrolled (aka number of left rows now off-screen
  int screenlines; //number of lines in the display available to text
  int screencols;  //number of columns in the display available to text
  int numrows; // the number of rows of items/tasks
  orow *row; //(e)ditorrow stores a pointer to a contiguous collection of orow structures 
  //orow *prev_row; //for undo purposes
  //int dirty; //each row has a row->dirty
  char *context;
  char *filename; // in case try to save the titles
  char message[100]; //status msg is a character array - enlarging to 200 did not solve problems with seg faulting
  int highlight[2];
  int mode;
  char command[10]; //was 20 but probably could be 10 or less if doesn't include command_line needs to accomodate file name ?malloc heap array
  char command_line[20]; //for commands on the command line doesn't include ':' where that applies
  int repeat;
  bool show_deleted;
  bool show_completed;
};

struct outlineConfig O;

struct editorConfig {
  int cx, cy; //cursor x and y position
  int fc, fr; // file x and y position
  int rx; //index into the render field - only nec b/o tabs
  int line_offset; //row the user is currently scrolled to
  int coloff; //column user is currently scrolled to
  int screenlines; //number of lines in the display
  int screencols;  //number of columns in the display
  int numrows; // the number of rows(lines) of text delineated by /n if written out to a file
  erow *row; //(e)ditorrow stores a pointer to a contiguous collection of erow structures 
  int prev_numrows; // the number of rows of text so last text row is always row numrows
  erow *prev_row; //for undo purposes
  int dirty; //file changes since last save
  char *filename;
  char message[120]; //status msg is a character array max 80 char
  //time_t message_time;
  int highlight[2];
  int mode;
  char command[20]; //needs to accomodate file name ?malloc heap array
  int repeat;
  int indent;
  int smartindent;
  int continuation;
};

struct editorConfig E;


void abFree(struct abuf *ab); 
/*** outline prototypes ***/
void (*get_data)(char *, int);
void (*get_solr_data)(char *);
void (*get_note)(int);
void (*update_note)(void); 
void (*toggle_star)(void);
void (*toggle_completed)(void);
void (*toggle_deleted)(void);
void (*update_context)(int);
void (*update_rows)(void);
void (*update_row)(void);
int (*insert_row)(int); 
void (*display_item_info)(int);
int data_callback(void *, int, char **, char **);
int note_callback(void *, int, char **, char **);
int display_item_info_callback(void *, int, char **, char **);
int tid_callback(void *, int, char **, char **);
void outlineSetMessage(const char *fmt, ...);
void outlineRefreshScreen(void);
//void getcharundercursor();
void outlineDelWord();
void outlineMoveCursor(int key);
void outlineBackspace(void);
void outlineDelChar(void);
void outlineDeleteToEndOfLine(void);
void outlineYankLine(int n);
void outlinePasteString(void);
void outlineYankString();
void outlineMoveCursorEOL();
void outlineMoveBeginningWord();
void outlineMoveEndWord(); 
void outlineMoveEndWord2(); //not 'e' but just moves to end of word even if on last letter
void outlineMoveNextWord();
void outlineGetWordUnderCursor();
void outlineFindNextWord();
void outlineChangeCase();
void outlineInsertRow2(int at, char *s, size_t len, int id, bool star, bool deleted, bool completed); 
void outlineDrawRows(struct abuf *ab);
void outlineScroll(void); 
int get_id(int fr);
int insert_row_pg(int); 
int insert_row_sqlite(int); 
void update_note_pg(void); 
void update_note_sqlite(void); 
void synchronize(int);

//editor Prototypes
int editorGetLinesInRowWW(int); //ESSENTIAL - do not delete
int *editorGetScreenPosFromRowCharPosWW(int, int); //ESENTIAL - do not delete

int editorGetFileRowByLineWW(int);
int editorGetLineInRowWW(void);
int *editorGetRowLineCharWW(void);
int editorGetCharInRowWW(int, int); 
int editorGetLineCharCountWW(int, int);
int editorGetScreenXFromRowCharPosWW(int, int);
int *editorGetRowLineScreenXFromRowCharPosWW(int, int);

void editorSetMessage(const char *fmt, ...);
void editorRefreshScreen(void);
//void getcharundercursor(void);
void editorInsertReturn(void);
void editorDecorateWord(int c);
void editorDecorateVisual(int c);
void editorDelWord(void);
void editorIndentRow(void);
void editorUnIndentRow(void);
int editorIndentAmount(int y);
void editorMoveCursor(int key);
void editorBackspace(void);
void editorDelChar(void);
void editorDeleteToEndOfLine(void);
void editorYankLine(int n);
void editorPasteLine(void);
void editorPasteString(void);
void editorYankString(void);
void editorMoveCursorEOL(void);
void editorMoveCursorBOL(void);
void editorMoveBeginningWord(void);
void editorMoveEndWord(void); 
void editorMoveEndWord2(void); //not 'e' but just moves to end of word even if on last letter
void editorMoveNextWord(void);
void editorMarkupLink(void);
void getWordUnderCursor(void);
void editorFindNextWord(void);
void editorChangeCase(void);
void editorRestoreSnapshot(void); 
void editorCreateSnapshot(void); 
void editorInsertRow(int fr, char *s, size_t len); 
void abAppend(struct abuf *ab, const char *s, int len);

// config struct for reading db.ini file

struct config {
  const char * user;
  const char * password;
  const char * dbname;
  const char * hostaddr;
  int port;
};

struct config c;

PGconn *conn = NULL;

void do_exit(PGconn *conn) {
    
    PQfinish(conn);
    exit(1);
}

int parse_ini_file(char * ini_name)
{
    dictionary  *   ini ;

    ini = iniparser_load(ini_name);
    if (ini==NULL) {
        fprintf(stderr, "cannot parse file: %s\n", ini_name);
        return -1 ;
    }

    c.user = iniparser_getstring(ini, "ini:user", NULL);
    c.password = iniparser_getstring(ini, "ini:password", NULL);
    c.dbname = iniparser_getstring(ini, "ini:dbname", NULL);
    c.hostaddr = iniparser_getstring(ini, "ini:hostaddr", NULL);
    c.port = iniparser_getint(ini, "ini:port", -1);
  
  return 0;
}

/*
//seems you don't need this and you open sqlite db each time
void get_conn_sqlite(void) {

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open("test.db", &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
        
    //return 1;
  }
}
*/

void get_conn(void) {
  char conninfo[250];
  parse_ini_file("db.ini");
  
  sprintf(conninfo, "user=%s password=%s dbname=%s hostaddr=%s port=%d", 
          c.user, c.password, c.dbname, c.hostaddr, c.port);
  conn = PQconnectdb(conninfo);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  } 
}

void get_data_pg(char *context, int n) {
  char query[400];
  if (!O.show_deleted) {
  sprintf(query, "SELECT * FROM task JOIN context ON context.id = task.context_tid "
                    "WHERE context.title = \'%s\' "
                    "AND (task.deleted = %s "
                    "OR task.completed = %s) "
                    "ORDER BY task.modified DESC LIMIT %d",
                    context,
                    "False",
                    "NULL",
                    n);
  }
  else {

  sprintf(query, "SELECT * FROM task JOIN context ON context.id = task.context_tid "
                    "WHERE context.title = \'%s\' "
                    "ORDER BY task.modified DESC LIMIT %d",
                    context,
                    n);
  }

  PGresult *res = PQexec(conn, query);    
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");        
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }    
  
  for (int j = 0 ; j < O.numrows ; j++ ) {
    free(O.row[j].chars);
  } 
  free(O.row);
  O.row = NULL; 
  O.numrows = 0;

  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    char *title = PQgetvalue(res, i, 3);
    char *zz = PQgetvalue(res, i, 0);
    bool star = (*PQgetvalue(res, i, 8) == 't') ? true: false;
    bool deleted = (*PQgetvalue(res, i, 14) == 't') ? true: false;
    bool completed = (*PQgetvalue(res, i, 10)) ? true: false;
    int id = atoi(zz);
    outlineInsertRow2(O.numrows, title, strlen(title), id, star, deleted, completed); 
  }

  PQclear(res);
  // PQfinish(conn);

  O.fc = O.fr = O.rowoff = 0;
  //O.context = context;
}

void get_data_sqlite(char *context, int n) {
  char query[400];

  for (int j = 0 ; j < O.numrows ; j++ ) {
    free(O.row[j].chars);
  } 
  free(O.row);
  O.row = NULL; 
  O.numrows = 0;

  O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  // why does this have substitutions since !O.show_deleted determines them
  if (!O.show_deleted) {
    sprintf(query, "SELECT * FROM task JOIN context ON context.id = task.context_tid "
                    "WHERE context.title = \'%s\' "
                    "AND task.deleted = %s "
                    "AND task.completed IS %s " // has to be IS NULL
                    "ORDER BY task.modified DESC LIMIT %d",
                    context,
                    "False",
                    "NULL",
                    n);
  }
  else {

    sprintf(query, "SELECT * FROM task JOIN context ON context.id = task.context_tid "
                    "WHERE context.title = \'%s\' "
                    "ORDER BY task.modified DESC LIMIT %d",
                    context,
                    n);
  }
        
    rc = sqlite3_exec(db, query, data_callback, 0, &err_msg);
    
    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    } 
  sqlite3_close(db);
}

int data_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    
  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
    
  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL

  I thought I should be using tid as the "id" for sqlite version but realized
  that would work and mean you could always compare the tid to the pg id
  but for new items created with sqlite, there would be no tid so
  the right thing to use is the id.  At some point might also want to
  store the tid in orow row
  */

  char *title = argv[3];
  char *zz = argv[0]; // ? use tid? = argv[1] see note above
  bool star = (atoi(argv[8]) == 1) ? true: false;
  bool deleted = (atoi(argv[14]) == 1) ? true: false;
  bool completed = (argv[10]) ? true: false;
  int id = atoi(zz);
  outlineInsertRow2(O.numrows, title, strlen(title), id, star, deleted, completed); 
  //outlineInsertRow2(O.numrows, title, strlen(title), id, star, deleted, false); 
  return 0;
}

void get_solr_data_sqlite(char *query) {

  for (int j = 0 ; j < O.numrows ; j++ ) {
    free(O.row[j].chars);
  } 
  free(O.row);
  O.row = NULL; 
  O.numrows = 0;

  O.fc = O.fr = O.rowoff = 0;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    }

    rc = sqlite3_exec(db, query, data_callback, 0, &err_msg);
    
    if (rc != SQLITE_OK ) {
        outlineSetMessage("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
    } 
  sqlite3_close(db);
}

//brings back a set of ids generated by solr search
void get_solr_data_pg(char *query) {
  PGresult *res = PQexec(conn, query);    
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");        
    printf("PQresultErrorMessage: %s\n", PQresultErrorMessage(res));
    PQclear(res);
    do_exit(conn);
  }    
  
  if (O.numrows) {
  for (int j = 0 ; j < O.numrows ; j++ ) {
    free(O.row[j].chars);
  } 
  free(O.row);
  O.row = NULL; 
  O.numrows = 0;
  }

  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    char *title = PQgetvalue(res, i, 3);
    char *zz = PQgetvalue(res, i, 0);
    bool star = (*PQgetvalue(res, i, 8) == 't') ? true: false;
    bool deleted = (*PQgetvalue(res, i, 14) == 't') ? true: false;
    bool completed = (*PQgetvalue(res, i, 10)) ? true: false;
    int id = atoi(zz);
    outlineInsertRow2(O.numrows, title, strlen(title), id, star, deleted, completed); 
  }

  PQclear(res);
 // PQfinish(conn);

  O.fc = O.fr = O.rowoff = 0;
}

void get_tid_sqlite(int id) {
  if (id ==-1) return;

  // free the E.row[j].chars
  for (int j = 0 ; j < E.numrows ; j++ ) {
    free(E.row[j].chars);
  } 
  free(E.row);
  E.row = NULL; 
  E.numrows = 0;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    }
  char query[100];
  sprintf(query, "SELECT tid FROM task WHERE id = %d", id); //tid

  // callback does *not* appear to be called if result (argv) is null
  rc = sqlite3_exec(db, query, tid_callback, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
  } 
  sqlite3_close(db);
}


int tid_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  //note strsep handles multiple \n\n and strtok did not
  if (argv[0] != NULL) { //added this guard 02052019 - not sure
    //tid = atoi(argv[0]);
  }
  return 0;
}

void get_note_sqlite(int id) {
  if (id ==-1) return;

  // free the E.row[j].chars
  for (int j = 0 ; j < E.numrows ; j++ ) {
    free(E.row[j].chars);
  } 
  free(E.row);
  E.row = NULL; 
  E.numrows = 0;

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    }
  char query[100];
  sprintf(query, "SELECT note FROM task WHERE id = %d", id); //tid

  // callback does *not* appear to be called if result (argv) is null
  rc = sqlite3_exec(db, query, note_callback, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
  } 
  sqlite3_close(db);
}

// doesn't appear to be called if row is NULL
int note_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  //note strsep handles multiple \n\n and strtok did not
  //note tabs in notes cause problems so that probably need
  //to do same thing we did with ' - find them and just replace with ' '
  //VLA
  /*
  char *note;
  note = strdup(argv[0]); // ******************
  char clean_note[strlen(note) + 1];
  char *out = clean_note;
  const char *in = note;
  while (*in) {
      *out = *in;
      if (*in == 9) *out = 32;
      in++;
      out++;
  }
  *out = '\0';
  */

  if (argv[0] != NULL) { //added this guard 02052019 - not sure
    char *note;
    note = strdup(argv[0]); // ******************
    char *found;
    while ((found = strsep(&note, "\r\n")) !=NULL) { //if we cleaned the tabs then strsep(&clean_note, ...)
      editorInsertRow(E.numrows, found, strlen(found));
    }
    free(note);
  }
  E.dirty = 0;
  editorRefreshScreen();
  return 0;
}

void get_note_pg(int id) {
  if (id ==-1) return;

  // free the E.row[j].chars
  for (int j = 0 ; j < E.numrows ; j++ ) {
    free(E.row[j].chars);
  } 
  free(E.row);
  E.row = NULL; 
  E.numrows = 0;

  char query[100];
  sprintf(query, "SELECT note FROM task WHERE id = %d", id);

  PGresult *res = PQexec(conn, query);    
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    outlineSetMessage("Problem retrieving note\n");        
    PQclear(res);
    //do_exit(conn);
  }    

  //note strsep handles multiple \n\n and strtok did not
  if (PQgetvalue(res, 0, 0) != NULL) { //added this guard 02052019 - not sure
    char *note;
    note = strdup(PQgetvalue(res, 0, 0)); // ******************
    char *found;
    while ((found = strsep(&note, "\r\n")) !=NULL) {
      editorInsertRow(E.numrows, found, strlen(found));
    }
    free(note);
  }
  PQclear(res);
  E.dirty = 0;
  editorRefreshScreen();
  return;
}

bool starts_with(const char *str, const char *pre)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

void view_html(int id) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  Py_Initialize();
  if (which_db[0] == 'p') 
    pName = PyUnicode_DecodeFSDefault("view_html_pg"); //module
  else 
    pName = PyUnicode_DecodeFSDefault("view_html_sqlite"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "view_html"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); // PyTuple_New(x) creates a tuple with x elements
          pValue = Py_BuildValue("i", id); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
            outlineSetMessage("Successfully rendered the note in html");
          }
          else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Was not able to render the note in html!");
          }
      }
      else {
          if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: view_html!");
      }
      Py_XDECREF(pFunc);
      Py_DECREF(pModule);
  }
  else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: view_html!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
}

void solr_find(char *search_terms) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("solr_find"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "solr_find"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = Py_BuildValue("s", search_terms); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              Py_ssize_t size; 
              int len = PyList_Size(pValue);

              /*
              We want to create a query that looks like:
              SELECT * FROM task 
              WHERE task.id IN (1234, 5678, , 9012) 
              ORDER BY task.id = 1234 DESC, task.id = 5678 DESC, task.id = 9012 DESC
              */

              char query[2000];
              char *put;

              if (which_db[0] == 'p') {
                strncpy(query, "SELECT * FROM task WHERE task.id IN (", sizeof(query));
              } else {
                strncpy(query, "SELECT * FROM task WHERE task.tid IN (", sizeof(query));
              }

              put = &query[strlen(query)];

              for (int i=0; i<len;i++) {
                put += snprintf(put, sizeof(query) - (put - query), "%s, ", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
              }

              int slen = strlen(query);
              query[slen-2] = ')'; //last IN id has a trailing space and comma 
              query[slen-1] = '\0';

              put = &query[strlen(query)];
              put += snprintf(put, sizeof(query) - (put - query), "%s", " ORDER BY ");

              for (int i=0; i<len;i++) {
                put += snprintf(put, sizeof(query) - (put - query), "task.id = %s DESC, ", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
              }

              slen = strlen(query);
              query[slen-2] = '\0'; //have extra comma space

              Py_DECREF(pValue);

             //this is where you would do the search
             (*get_solr_data)(query);


          }
          else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Problem retrieving ids from solr!");
          }
      }
      else {
          if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: solr_find!");
      }
      Py_XDECREF(pFunc);
      Py_DECREF(pModule);
  }
  else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: view_html!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

}

void update_solr(void) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("update_solr"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "update_solr"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(0); //presumably PyTuple_New(x) creates a tuple with that many elements
          //pValue = PyLong_FromLong(1);
          //pValue = Py_BuildValue("s", search_terms); // **************
          //PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Problem retrieving ids from solr!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: solr_find!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      PyErr_Print();
      outlineSetMessage("Was not able to find the module: view_html!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

  outlineSetMessage("%d items were added/updated to solr db", num);
}

int keyfromstring(char *key) {

  //if (strlen(key) == 1) return key[0]; //catching this before calling keyfromstring

  int i;
  for (i=0; i <  NKEYS; i++) {
    if (strcmp(lookuptable[i].key, key) == 0) return lookuptable[i].val;
    }

    //nothing should match -1
    return -1;
}

//through pointer passes back position of space (if there is one)
int commandfromstring(char *key, int *p) { //for commands like find nemo - that consist of a command a space and further info

  if (strlen(key) == 1) return key[0];  //need this

  //probably should strip trailing spaces with isspace

  int i, pos;
  char *command;
  char *ptr_2_space = strchr(key, ' ');
  if (ptr_2_space) {
    pos = ptr_2_space - key;
    // reference to position of space in commands like "open todo"
    *p = pos; 
    command = strndup(key, pos);
    command[pos] = '\0'; 
  } else {
    command = key;
    pos = 0;
    *p = pos; //not sure this is necessary - not using it when command has no space
  }

  for (i=0; i <  NKEYS; i++) {
    if (strcmp(lookuptable[i].key, command) == 0) {
      if (pos) free(command);
      return lookuptable[i].val;
    }
  }

  //if don't match anything and not a single char then just return -1
  if (pos) free(command);
  return -1;
}

void die(const char *s) {
  // write is from <unistd.h> 
  //ssize_t write(int fildes, const void *buf, size_t nbytes);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; // minimum data to receive?
  raw.c_cc[VTIME] = 1; // timeout for read will return 0 if no bytes read

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int readKey() {
  int nread;
  char c;

  /* read is from <unistd.h> - not sure why read is used and not getchar <stdio.h>
   prototype is: ssize_t read(int fd, void *buf, size_t count); 
   On success, the number of bytes read is returned (zero indicates end of file)
   So the while loop below just keeps cycling until a byte is read
   it does check to see if there was an error (nread == -1)*/

   /*Note that ctrl-key maps to ctrl-A=1, ctrl-b=2 etc.*/

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  /* if the character read was an escape, need to figure out if it was
     a recognized escape sequence or an isolated escape to switch from
     insert mode to normal mode or to reset in normal mode
  */

  if (c == '\x1b') {
    char seq[3];
    //outlineSetMessage("You pressed %d", c); //slz
    // the reads time out after 0.1 seconds
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Assumption is that seq[0] == '[' 
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //need 4 bytes
      if (seq[2] == '~') {
        //outlineSetMessage("You pressed %c%c%c", seq[0], seq[1], seq[2]); //slz
        switch (seq[1]) {
          case '1': return HOME_KEY; //not being issued
          case '3': return DEL_KEY; //<esc>[3~
          case '4': return END_KEY;  //not being issued
          case '5': return PAGE_UP; //<esc>[5~
          case '6': return PAGE_DOWN;  //<esc>[6~
          case '7': return HOME_KEY; //not being issued
          case '8': return END_KEY;  //not being issued
        }
      }
    } else {
        //outlineSetMessage("You pressed %c%c", seq[0], seq[1]); //slz
        switch (seq[1]) {
          case 'A': return ARROW_UP; //<esc>[A
          case 'B': return ARROW_DOWN; //<esc>[B
          case 'C': return ARROW_RIGHT; //<esc>[C
          case 'D': return ARROW_LEFT; //<esc>[D
          case 'H': return HOME_KEY; // <esc>[H - this one is being issued
          case 'F': return END_KEY;  // <esc>[F - this one is being issued
          case 'Z': return SHIFT_TAB; //<esc>[Z
      }
    }

    return '\x1b'; // if it doesn't match a known escape sequence like ] ... or O ... just return escape
  
  } else {
      //outlineSetMessage("You pressed %d", c); //slz
      return c;
  }
}

int getWindowSize(int *rows, int *cols) {

//TIOCGWINSZ = fill in the winsize structure
/*struct winsize
{
  unsigned short ws_row;	 rows, in characters 
  unsigned short ws_col;	 columns, in characters 
  unsigned short ws_xpixel;	 horizontal size, pixels 
  unsigned short ws_ypixel;	 vertical size, pixels 
};*/

// ioctl(), TIOCGWINXZ and struct windsize come from <sys/ioctl.h>
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;

    return 0;
  }
}

/*** outline row operations ***/

void outlineFreeRow(orow *row) {
  free(row->chars);
}

void outlineInsertRow2(int at, char *s, size_t len, int id, bool star, bool deleted, bool completed) {
  /*O.row is a pointer to an array of database row structures
  The array of rows that O.row points to needs to have its memory enlarged when
  you add a row. Note that db row structures now include:

  int size; //the number of characters in the line
  char *chars; //pointer to the character array of the item title

  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  bool dirty;
  */

  O.row = realloc(O.row, sizeof(orow) * (O.numrows + 1));

  /*
  memmove(dest, source, number of bytes to move?)
  moves the line at at to at+1 and all the other orow structs until the end
  when you insert into the last row O.numrows==at then no memory is moved
  apparently ok if there is no O.row[at+1] if number of bytes = 0
  so below we are moving the row structure currently at *at* to x+1
  and all the rows below *at* to a new location to make room at *at*
  to create room for the line that we are inserting
  */

  memmove(&O.row[at + 1], &O.row[at], sizeof(orow) * (O.numrows - at));

  // section below creates an orow struct for the new row
  O.row[at].size = len;
  O.row[at].chars = malloc(len + 1);
  memcpy(O.row[at].chars, s, len);
  O.row[at].id = id;
  O.row[at].star = star;
  O.row[at].deleted = deleted;
  O.row[at].completed = completed;
  O.row[at].dirty = (id != -1) ? false : true;
  //memcpy(O.row[at].chars, s, len);
  O.row[at].chars[len] = '\0'; //each line is made into a c-string (maybe for searching)
  O.numrows++;

}

/*** outline operations ***/
void outlineInsertChar(int c) {

  if ( O.numrows == 0 ) {
    //outlineInsertRow(0, "", 0); //outlineInsertRow will insert '\0' should rev
    return;
  }

  orow *row = &O.row[O.fr];

  row->chars = realloc(row->chars, row->size + 2); // yes *2* is correct row->size + 1 = existing bytes + 1 new byte

  /* moving all the chars at the current x cursor position one char
     farther down the char string to make room for the new character
     Note that if fc = row->size then beyond last character in insert
     mode and char is inserted before the closing '\0'
     Maybe a clue from outlineInsertRow - it's memmove is below
     memmove(&O.row[at + 1], &O.row[at], sizeof(orow) * (O.numrows - at));
  */

  memmove(&row->chars[O.fc + 1], &row->chars[O.fc], row->size - O.fc + 1); 

  row->size++;
  row->chars[O.fc] = c;
  row->chars[row->size] = '\0'; //????
  row->dirty = true;
  O.fc++;
}

void outlineDelChar(void) {

  orow *row = &O.row[O.fr];

  // note below order important because row->size undefined if 
  // O.numrows = 0 because O.row is NULL
  if (O.numrows == 0 || row->size == 0 ) return; 

  memmove(&row->chars[O.fc], &row->chars[O.fc + 1], row->size - O.fc);
  row->chars = realloc(row->chars, row->size); 
  row->size--;

  //shouldn't have to do this since trailing '\0' should move too
  row->chars[row->size] = '\0'; 

  // don't know if this is is necessary - you have one row i
  // with no chars - that's fine
  if (O.numrows == 1 && row->size == 0) {
    O.numrows = 0;
    free(O.row);
    O.row = NULL;
  }

  row->dirty = true;

}

void outlineBackspace(void) {

  if (O.fc == 0) return;

  orow *row = &O.row[O.fr];

  memmove(&row->chars[O.fc - 1], &row->chars[O.fc], row->size - O.fc + 1);
  row->chars = realloc(row->chars, row->size); 
  row->size--;
  row->chars[row->size] = '\0'; //shouldn't have to do this but does it hurt anything??
  O.fc--; //if O.cx goes negative outlineScroll should handle it
 
  row->dirty = true;
}

/*** file i/o ***/

char *outlineRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < O.numrows; j++)
    totlen += O.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < O.numrows; j++) {
    memcpy(p, O.row[j].chars, O.row[j].size);
    p += O.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void outlineSave() {
  if (O.context == NULL) return;
  int len;
  char *buf = outlineRowsToString(&len);

  int fd = open(O.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        outlineSetMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  outlineSetMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** editor row operations ***/

//fr is the row number of the row to insert
void editorInsertRow(int fr, char *s, size_t len) {

  /*E.row is a pointer to an array of erow structures
  The array of erows that E.row points to needs to have its memory enlarged when
  you add a row. Note that erow structues are just a size and a char pointer*/

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  /*
  memmove(dest, source, number of bytes to move?)
  moves the line at fr to fr+1 and all the other erow structs until the end
  when you insert into the last row E.numrows==fr then no memory is moved
  apparently ok if there is no E.row[fr+1] if number of bytes = 0
  so below we are moving the row structure currently at *fr* to x+1
  and all the rows below *fr* to a new location to make room at *fr*
  to create room for the line that we are inserting
  */

  memmove(&E.row[fr + 1], &E.row[fr], sizeof(erow) * (E.numrows - fr));

  // section below creates an erow struct for the new row
  E.row[fr].size = len;
  E.row[fr].chars = malloc(len + 1);
  memcpy(E.row[fr].chars, s, len);
  E.row[fr].chars[len] = '\0'; //each line is made into a c-string (maybe for searching)
  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->chars);
}

// untested
void editorDelRow(int r) {
  //editorSetMessage("Row to delete = %d; E.numrows = %d", fr, E.numrows); 
  if (E.numrows == 0) return; // some calls may duplicate this guard

  editorFreeRow(&E.row[r]); 
  memmove(&E.row[r], &E.row[r + 1], sizeof(erow) * (E.numrows - r - 1));
  E.numrows--; 
  if (E.numrows == 0) {
    E.row = NULL;
    E.fr = 0;
    E.fc = 0;
    return;
  } else if (r == E.numrows) r--;

  E.dirty++;
  //editorSetMessage("Row deleted = %d; E.numrows after deletion = %d E.cx = %d E.row[fr].size = %d", fr, E.numrows, E.cx, E.row[fr].size); 
}

// only used by editorBackspace
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  E.dirty++;
}

/* not in use right now
void editorRowDelChar(erow *row, int fr) {
  if (fr < 0 || fr >= row->size) return;
  // is there any reason to realloc for one character?
  // row->chars = realloc(row->chars, row->size -1); 
  //have to realloc when adding but I guess no need to realloc for one character
  memmove(&row->chars[fr], &row->chars[fr + 1], row->size - fr);
  row->size--;
  E.dirty++;
}
*/

/*** editor operations ***/
void editorInsertChar(int chr) {

  if ( E.numrows == 0 ) {
    editorInsertRow(0, "", 0); //editorInsertRow will insert '\0' and row->size=0
  }

  erow *row = &E.row[E.fr];

  // yes *2* is correct row->size + 1 = existing bytes + 1 new byte
  row->chars = realloc(row->chars, row->size + 2); 

  /* moving all the chars r the current x cursor position on char
     farther down the char string to make room for the new character
     Maybe a clue from editorInsertRow - it's memmove is below
     memmove(&E.row[r + 1], &E.row[r], sizeof(erow) * (E.numrows - r));
  */

   //if (E.fc == -1) E.fc = 0;// may not set E.fc = -1 so wouldn't need this check
  memmove(&row->chars[E.fc + 1], &row->chars[E.fc], row->size - E.fc + 1); 

  row->size++;
  row->chars[E.fc] = chr;
  E.dirty++;

  E.fc++;
}

void editorInsertReturn(void) { // right now only used for editor->INSERT mode->'\r'
  if (E.numrows == 0) {
    editorInsertRow(0, "", 0);
    editorInsertRow(0, "", 0);
    //E.fc = -1;
    E.fc = 0;
    E.fr = 1;
    return;
  }
    
  erow *row = &E.row[E.fr];
  //could use VLA
  int len = row->size - E.fc;
  //note that you need row-size - fc + 1 but InsertRow will take care of that
  //here you just need to copy the actual characters without the terminating '\0'
  //note below we indent but could be combined so that inserted into
  //the new line both the chars and the number of indent spaces
  char *moved_chars = malloc(len);
  //if (E.fc == -1) E.fc = 0;
  memcpy(moved_chars, &row->chars[E.fc], len); //? I had issues with strncpy so changed to memcpy
  
  //This is the row from which the return took place which is now smaller 
  //because some characters where moved into the next row(although could
  //be zero chars moved at the end of a line
  // fc can equal row->size because we are in insert mode and could be beyond the last char column
  // in that case the realloc should do nothing - assume that it behaves correctly
  // when there is no actual change in the memory allocation
  // also malloc and memcpy should behave ok with zero arguments
  row->size = E.fc;
  row->chars[row->size] = '\0';//someday may actually figure out if row-chars has to be a c-string 
  row->chars = realloc(row->chars, row->size + 1); 

  int indent = (E.smartindent) ? editorIndentAmount(E.fr) : 0;

  E.fr++;
  editorInsertRow(E.fr, moved_chars, len);
  free(moved_chars);

  E.fc = 0;
  for (int j=0; j < indent; j++) editorInsertChar(' ');
}

// now 'o' and 'O' separated from '\r' (in INSERT mode)
//'o' -> direction == 1 and 'O' direction == 0
void editorInsertNewline(int direction) {
  /* note this func does position cursor*/
  if (E.numrows == 0) {
    editorInsertRow(0, "", 0);
    return;
  }

  if (E.fr == 0 && direction == 0) { // this is for 'O'
    editorInsertRow(0, "", 0);
    E.fc = 0;
    return;
  }
    
  int indent = (E.smartindent) ? editorIndentAmount(E.fr) : 0;

  //VLA
  char spaces[indent + 1]; 
  for (int j=0; j < indent; j++) {
    spaces[j] = ' ';
  }
  spaces[indent] = '\0';

  E.fc = indent;

  E.fr += direction;
  editorInsertRow(E.fr, spaces, indent);

  /*
  int *screeny_screenx = editorGetScreenPosFromRowCharPosWW(r, c); 
  E.cx = screeny_screenx[1]; 
  E.cy = screeny_screenx[0]; 
  */
}

void editorDelChar(void) {

  erow *row = &E.row[E.fr];

  /* row size = 1 means there is 1 char; size 0 means 0 chars */
  /* Note that row->size does not count the terminating '\0' char*/
  // note below order important because row->size undefined if 
  // E.numrows = 0 because E.row is NULL
  if (E.numrows == 0 || row->size == 0 ) return; 

  memmove(&row->chars[E.fc], &row->chars[E.fc + 1], row->size - E.fc);
  row->chars = realloc(row->chars, row->size); // ******* this is untested but similar to outlineBackspace
  row->size--;

  //shouldn't have to do this since trailing '\0' should move too
  row->chars[row->size] = '\0'; 

  // don't know if this is is necessary - you have one row i
  // with no chars - that's fine
  if (E.numrows == 1 && row->size == 0) {
    E.numrows = 0;
    free(E.row);
    E.row = NULL;
  }
  E.dirty++;
}

// used by 'x' in editor/visual mode
void editorDelChar2(int fr, int fc) {
  erow *row = &E.row[fr];

  /* row size = 1 means there is 1 char; size 0 means 0 chars */
  /* Note that row->size does not count the terminating '\0' char*/
  // note below order important because row->size undefined if E.numrows = 0 because E.row is NULL
  if (E.numrows == 0 || row->size == 0 ) return; 

  memmove(&row->chars[fc], &row->chars[fc + 1], row->size - fc);
  row->chars = realloc(row->chars, row->size); // ******* this is untested but similar to outlineBackspace
  row->size--;
  row->chars[row->size] = '\0'; //shouldn't have to do this but does it hurt anything??

  if (E.numrows == 1 && row->size == 0) {
    E.numrows = 0;
    free(E.row);
    E.row = NULL;
  }
  E.dirty++;
}

//Really need to look at this
void editorBackspace(void) {

  if (E.fc == 0 && E.fr == 0) return;

  erow *row = &E.row[E.fr];

  if (E.fc > 0) {
    if (E.cx > 0) { // We want this E.cx - don't change to E.fc!!!
      memmove(&row->chars[E.fc - 1], &row->chars[E.fc], row->size - E.fc + 1);
      row->size--;

     // below seems like a complete kluge but definitely want that E.cx!!!!!
      if (E.cx == 1 && E.fc > 1) E.continuation = 1; //right now only backspace in multi-line

      E.fc--;

    } else { 
      memmove(&row->chars[E.fc - 1], &row->chars[E.fc], row->size - E.fc + 1);
      row->chars = realloc(row->chars, row->size); 
      row->size--;
      row->chars[row->size] = '\0'; //shouldn't have to do this but does it hurt anything??
      E.fc--;
      E.continuation = 0;
    } 
  } else {// this means we're at fc == 0 so we're in the first filecolumn
    editorRowAppendString(&E.row[E.fr - 1], row->chars, row->size); //only use of this function
    editorFreeRow(&E.row[E.fr]);
    memmove(&E.row[E.fr], &E.row[E.fr + 1], sizeof(erow) * (E.numrows - E.fr - 1));
    E.numrows--;
    E.fr--;
    E.fc = E.row[E.fr].size;
  }
  E.dirty++;
}
///////////////////// stopped 02202019 1 pm
/*** file i/o ***/

// have not looked at this 02212019
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen + 1);
  //char *buf = calloc(totlen, sizeof(char)); // worked because it made sure c string terminated with '\0' but not necessary.
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  *p = '\0'; //does not work if this is missing
  return buf;
}

void editorEraseScreen(void) {

  if (E.row) {
    for (int j = 0 ; j < E.numrows ; j++ ) {
      free(E.row[j].chars);
    } 
    free(E.row);
    E.row = NULL; 
    E.numrows = 0;
  }

  char offset_lf_ret[20];
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", EDITOR_LEFT_MARGIN, TOP_MARGIN); 

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    abAppend(&ab, "\x1b[K", 3); 
    abAppend(&ab, offset_lf_ret, 7);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab); 
}

void editorDisplayFile(char *filename) {

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char offset_lf_ret[20];
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", EDITOR_LEFT_MARGIN, TOP_MARGIN); 

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    abAppend(&ab, "\x1b[K", 3); 
    abAppend(&ab, offset_lf_ret, 7);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //abAppend(&ab, "\x1b[44m", 5); //tried background blue - didn't love it
  abAppend(&ab, "\x1b[36m", 5); //this is foreground cyan - we'll see
  int file_line = 0;
  int file_row = 0;
  //initial_file_row is a global - should be set to zero when you open a file
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    //editorInsertRow(E.numrows, line, linelen);
    //probably should have if (lineline > E.screencols) break into multiple lines
    int n = 0;
    file_row++;
    if (file_row < initial_file_row + 1) continue;
    for(;;) {
      if (linelen < (n+1)*E.screencols) break;
      abAppend(&ab, &line[n*E.screencols], E.screencols);
      abAppend(&ab, offset_lf_ret, 7);
      //should be num_liness++ here
      n++;
    }
    abAppend(&ab, &line[n*E.screencols], linelen-n*E.screencols);
    file_line++;
    if (file_line > E.screenlines - 2) break; //was - 1
    abAppend(&ab, offset_lf_ret, 7);
  }
  abAppend(&ab, "\x1b[0m", 4);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab); 
  free(line);
  
  fclose(fp);
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  //E.dirty = 0;
}

void editorSave(void) {
  if (E.filename == NULL) return;
  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/
/*
struct abuf {
  char *b;
  int len;
};
*/
//#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {

  /*
     initally abuf consists of *b = NULL pointer and int len = 0
     abuf.b holds the pointer to the memory that holds the string
     the below creates a new memory pointer that reallocates
     memory to something 
  */

  /*realloc's first argument is the current pointer to memory and the second argumment is the size_t needed*/
  char *new = realloc(ab->b, ab->len + len); 

  if (new == NULL) return;
  /*new is the new pointer to the string
    new[len] is a value and memcpy needs a pointer
    to the location in the string where s is being copied*/

  //copy s on to the end of whatever string was there
  memcpy(&new[ab->len], s, len); 

  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

// positions the cursor ( O.cx and O.cy) and O.coloff and O.rowoff
void outlineScroll(void) {

  if(!O.row) return;

  O.cy = O.fr - O.rowoff;
  O.cx = O.fc - O.coloff;

  if (O.cy > O.screenlines - 1) {   //there was >= no -1 but changed on 02222019
    //O.cy--;
    O.cy = O.screenlines - 1;
    O.rowoff = O.fr - O.cy;
    //O.rowoff++;
    //O.cy = O.screenlines - 1;
  } 

  if (O.cy < 0) {
    O.rowoff+=O.cy;
    O.cy = 0;
  }

  if (O.cx >= O.screencols) {
    O.coloff = O.coloff + O.cx - O.screencols + 1;
    O.cx = O.screencols - 1;
  }
  if (O.cx < 0) {
    O.coloff+=O.cx;
    O.cx = 0;
  }
}

// "drawing" rows really means updating the ab buffer
// filerow/filecol are the row/column of the titles regardless of scroll
// result_set_row/filecol are the row/column of the titles regardless of scroll
void outlineDrawRows(struct abuf *ab) {
  int j, k; //to swap highlight if O.highlight[1] < O.highlight[0]

  if (!O.row) return; //***************************

  int y;
  char offset_lf_ret[20];
  //snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC", OUTLINE_LEFT_MARGIN); 
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", OUTLINE_LEFT_MARGIN, TOP_MARGIN); 

  for (y = 0; y < O.screenlines; y++) {
    int result_set_row = y + O.rowoff;
    if (result_set_row > O.numrows - 1) return;
    orow *row = &O.row[result_set_row];

    // below says that if a line is long you only draw what fits on the screen
    //if (len > O.screencols) len = O.screencols;
    int len = (row->size > O.screencols) ? O.screencols : row->size;
    
    if (row->star) abAppend(ab, "\x1b[1m", 4); //bold

    if (row->completed && row->deleted) abAppend(ab, "\x1b[32m", 5); //green foreground
    else if (row->completed) abAppend(ab, "\x1b[33m", 5); //yellow foreground
    else if (row->deleted) abAppend(ab, "\x1b[31m", 5); //red foreground

    if (row->dirty) abAppend(ab, "\x1b[41m", 5); //red background

    // below - only will get visual highlighting if it's the active row and 
    //then also deals with column offset
    if (O.mode == VISUAL && result_set_row == O.cy + O.rowoff) { 
            
       // below in case E.highlight[1] < E.highlight[0]
      k = (O.highlight[1] > O.highlight[0]) ? 1 : 0;
      j =!k;
      abAppend(ab, &(row->chars[O.coloff]), O.highlight[j] - O.coloff);
      abAppend(ab, "\x1b[48;5;242m", 11);
      abAppend(ab, &(row->chars[O.highlight[j]]), O.highlight[k]
                                             - O.highlight[j]);
      abAppend(ab, "\x1b[49m", 5); //slz return background to normal
      abAppend(ab, &(row->chars[O.highlight[k]]), len - O.highlight[k] + O.coloff);
      
    } else {
        // below means that the only row that is scrolled is the row that is active
        abAppend(ab, &O.row[result_set_row].chars[(result_set_row == O.cy + O.rowoff) ? O.coloff : 0], len);
    }
    
    abAppend(ab, offset_lf_ret, 7);
    abAppend(ab, "\x1b[0m", 4); //slz return background to normal
  }
}

//status bar has inverted colors
void outlineDrawStatusBar(struct abuf *ab) {

  if (!O.row) return; //**********************************

  orow *row = &O.row[O.fr];

  /*
  so the below should 1) position the cursor on the status
  bar row and midscreen and 2) erase previous statusbar
  r -> l and then put the cursor back where it should be
  at OUTLINE_LEFT_MARGIN
  */

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH", 
                             O.screenlines + TOP_MARGIN + 1,
                             O.screencols + OUTLINE_LEFT_MARGIN,

                             O.screenlines + TOP_MARGIN + 1,
                             1); //status bar comes right out to left margin

  abAppend(ab, buf, strlen(buf));

  abAppend(ab, "\x1b[7m", 4); //switches to inverted colors
  char status[80], rstatus[80];

  int len = (row->size < 20) ? row->size : 19;
  char *truncated_title = malloc(len + 1);
  memcpy(truncated_title, row->chars, len); //had issues with strncpy so changed to memcpy
  truncated_title[len] = '\0'; // if title is shorter than 19, should be fine

  len = snprintf(status, sizeof(status), "%s%s%s - %d rows - %.15s... - \x1b[1m%s\x1b[0;7m",
                              O.context, (strcmp(O.context, "search") == 0) ? " - " : "",
                              (strcmp(O.context, "search") == 0) ? search_terms : "",
                              O.numrows, truncated_title, which_db);

  //because of escapes
  len-=10;

  int rlen = snprintf(rstatus, sizeof(rstatus), "mode: %s id: %d %d/%d",
                      mode_text[O.mode], row->id, O.fr + 1, O.numrows);

  if (len > (O.screencols + OUTLINE_LEFT_MARGIN)) 
    len = O.screencols + OUTLINE_LEFT_MARGIN;

  abAppend(ab, status, len+10);

  while (len < (O.screencols + OUTLINE_LEFT_MARGIN)) {
    if (O.screencols + OUTLINE_LEFT_MARGIN - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }

  abAppend(ab, "\x1b[m", 3); //switches back to normal formatting
  free(truncated_title);
}

void outlineDrawMessageBar(struct abuf *ab) {

  // Erase from mid-screen to the left and then place cursor all the way left 
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH", 
                             O.screenlines + 2 + TOP_MARGIN,
                             O.screencols + OUTLINE_LEFT_MARGIN,
                             O.screenlines + 2 + TOP_MARGIN,
                             //OUTLINE_LEFT_MARGIN + 1);
                             1);

  abAppend(ab, buf, strlen(buf));
  int msglen = strlen(O.message);
  if (msglen > O.screencols) msglen = O.screencols;
  abAppend(ab, O.message, msglen);
}

void outlineRefreshScreen(void) {
  //outlineScroll();

  /*  struct abuf {
      char *b;
      int len;
    };*/

  //if (O.row)
  if (0)
    outlineSetMessage("length = %d, O.cx = %d, O.cy = %d, O.fc = %d, O.fr = %d row id = %d", O.row[O.cy].size, O.cx, O.cy, O.fc, O.fr, get_id(-1));

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  char buf[20];
  for (int j=TOP_MARGIN; j < O.screenlines + 1;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j +TOP_MARGIN, 
    O.screencols + OUTLINE_LEFT_MARGIN); 
    abAppend(&ab, buf, strlen(buf));
  }

  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , OUTLINE_LEFT_MARGIN + 1); // ***************** 

  abAppend(&ab, buf, strlen(buf));
  outlineDrawRows(&ab);
  outlineDrawStatusBar(&ab);
  outlineDrawMessageBar(&ab);

  //[y;xH positions cursor and [1m is bold [31m is red and here they are
  //chained (note syntax requires only trailing 'm')
  if (O.mode != DATABASE) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;31m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN); 
    abAppend(&ab, buf, strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6); //shows the cursor
  }
  else { 
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;34m>", O.cy + TOP_MARGIN + 1, OUTLINE_LEFT_MARGIN); //blue
    abAppend(&ab, buf, strlen(buf));
}
  // below restores the cursor position based on O.cx and O.cy + margin
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, O.cx + OUTLINE_LEFT_MARGIN + 1); /// ****
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[0m", 4); //return background to normal

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void outlineSetMessage(const char *fmt, ...) {
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  vsnprintf(O.message, sizeof(O.message), fmt, ap);
  va_end(ap); //free a va_list
  //O.message_time = time(NULL);
}

//Note: outlineMoveCursor worries about moving cursor beyond the size of the row
//OutlineScroll worries about moving cursor beyond the screen
void outlineMoveCursor(int key) {
  int id;
  orow *row;

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (O.fc > 0) O.fc--; 
      else {
        O.mode = DATABASE;
        O.command[0] = '\0';
        O.repeat = 0;
      }
      break;

    case ARROW_RIGHT:
    case 'l':
      row = &O.row[O.fr];
      if (row) O.fc++;  //segfaults on opening if you arrow right w/o row
      break;

    case ARROW_UP:
    case 'k':
      if (O.fr > 0) O.fr--; 
      O.fc = O.coloff = 0; 

      // note need to determine row after moving cursor
      id = O.row[O.fr].id;
      (*get_note)(id); //if id == -1 does not try to retrieve note 
      //editorRefreshScreen(); //in get_note
      break;

    case ARROW_DOWN:
    case 'j':
      if (O.fr < O.numrows - 1) O.fr++;
      O.fc = O.coloff = 0;

      // note need to determine row after moving cursor
      row = &O.row[O.fr];
      (*get_note)(row->id); //if id == -1 does not try to retrieve note 
      //editorRefreshScreen(); //in get_note
      break;
  }

  row = &O.row[O.fr];
  if (O.fc >= row->size) O.fc = row->size - (O.mode != INSERT); 
}

// higher level outline function depends on readKey()
void outlineProcessKeypress() {
  int start, end, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  int c = readKey();

  orow *row;

  switch (O.mode) { 

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
          update_row();
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          outlineSetMessage("");
          return;

        case HOME_KEY:
          O.fc = 0;
          return;

        case END_KEY:
          row = &O.row[O.fr];
          if (row->size) O.fc = row->size; // mimics vim to remove - 1;
          return;

        case BACKSPACE:
          outlineBackspace();
          return;

        case DEL_KEY:
          outlineDelChar();
          return;


        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          outlineMoveCursor(c);
          return;

        case CTRL_KEY('z'):
          // not in use
          return;

        case '\x1b':
          O.command[0] = '\0';
          O.mode = NORMAL;
          if (O.fc > 0) O.fc--;
          outlineSetMessage("");
          return;

        default:
          outlineInsertChar(c);
          return;

      } 
      return; //end outer case INSERT

    case NORMAL:  

        if (c == '\x1b') {
          O.command[0] = '\0';
          O.repeat = 0;
          return;
        }  
 
      /*leading digit is a multiplier*/
      if (isdigit(c)) { //equiv to if (c > 47 && c < 58) 
        if ( O.repeat == 0 ){

          //if c=48=>0 then it falls through to move to beginning of line
          if ( c != 48 ) { 
            O.repeat = c - 48;
            return;
          }  

        } else { 
          O.repeat = O.repeat*10 + c - 48;
          return;
        }
      }

      if ( O.repeat == 0 ) O.repeat = 1;

      int n = strlen(O.command);
      O.command[n] = c;
      O.command[n+1] = '\0';
      // I believe because arrow keys above ascii range could not just
      // have keyfromstring return command[0]
      command = (n && c < 128) ? keyfromstring(O.command) : c;

      switch(command) {  

        case '\r':
          update_row();
          O.command[0] = '\0';
          return;

        case '<':
        case '\t':
        case SHIFT_TAB:
          O.fc = 0; //intentionally leave O.fr wherever it is
          O.mode = DATABASE;
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'i':
          O.mode = INSERT;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 's':
          for (int i = 0; i < O.repeat; i++) outlineDelChar();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 1;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
          return;

        case 'x':
          for (int i = 0; i < O.repeat; i++) outlineDelChar();
          O.command[0] = '\0';
          O.repeat = 0;
          return;
        
        case 'r':
          O.command[0] = '\0';
          O.mode = REPLACE; //REPLACE_MODE = 5
          return;

        case '~':
          for (int i = 0; i < O.repeat; i++) outlineChangeCase();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'a':
          O.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
          outlineMoveCursor(ARROW_RIGHT);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'A':
          outlineMoveCursorEOL();
          O.mode = INSERT; //needs to be here for movecursor to work at EOLs
          outlineMoveCursor(ARROW_RIGHT);
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'w':
          outlineMoveNextWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'b':
          outlineMoveBeginningWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'e':
          outlineMoveEndWord();
          O.command[0] = '\0';
          O.repeat = 0;
            return;

        case '0':
          //O.cx = -O.coloff; //surprisingly seems to work

          if (O.row != NULL) O.fc = 0;
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case '$':
          outlineMoveCursorEOL();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'I':
          if (O.row != NULL) {
            O.fc = 0;
            O.mode = 1;
            outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          }
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case 'G':
          O.fc = 0;
          O.fr = O.numrows - 1;
          O.command[0] = '\0';
          O.repeat = 0;

         (*get_note)(O.row[O.fr].id); //if id == -1 does not try to retrieve note 

          return;
      
        case ':':
          NN = 0; //index to contexts
          O.command[0] = '\0';
          O.command_line[0] = '\0';
          outlineSetMessage(":"); 
          O.mode = COMMAND_LINE;
          return;

        case 'v':
          O.mode = VISUAL;
          O.command[0] = '\0';
          O.repeat = 0;
          O.highlight[0] = O.highlight[1] = O.fc;
          outlineSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
          return;

        case 'p':  
          if (strlen(string_buffer)) outlinePasteString();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case '*':  
          outlineGetWordUnderCursor();
          outlineFindNextWord(); 
          O.command[0] = '\0';
          return;

        case 'n':
          outlineFindNextWord();
          O.command[0] = '\0';
          return;

        case 'u':
          //could be used to update solr - would use U
          O.command[0] = '\0';
          return;

        case '^':
          row = &O.row[O.fr];
          view_html(row->id);

          /*
          not getting error messages with qutebrowser
          so below not necessary (for the moment)
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */

          O.command[0] = '\0';
          return;

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            O.fr -= O.screenlines; //should be screen lines although same
            if (O.fr < 0) O.fr = 0;
          } else if (c == PAGE_DOWN) {
             O.fr += O.screenlines;
             if (O.fr > O.numrows - 1) O.fr = O.numrows - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          outlineMoveCursor(c);
          O.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          O.repeat = 0;
          return;

        // for testing purposes I am using CTRL-h in normal mode
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          editorRefreshScreen();
          (*update_note)(); //not going this in EDITOR mode but should catch it because note dirty
          O.command[0] = '\0';
          return;

        case C_daw:
          for (int i = 0; i < O.repeat; i++) outlineDelWord();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_dw:
          for (int j = 0; j < O.repeat; j++) {
            start = O.fc;
            outlineMoveEndWord2();
            end = O.fc;
            O.fc = start;
            for (int j = 0; j < end - start + 2; j++) outlineDelChar();
          }
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_de:
          start = O.fc;
          outlineMoveEndWord(); //correct one to use to emulate vim
          end = O.fc;
          O.fc = start; 
          for (int j = 0; j < end - start + 1; j++) outlineDelChar();
          O.fc = (start < O.row[O.cy].size) ? start : O.row[O.cy].size -1;
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        case C_d$:
        case C_dd: //note not standard definition but seems right for outline
          outlineDeleteToEndOfLine();
          O.command[0] = '\0';
          O.repeat = 0;
          return;

        //tested with repeat on one line
        case C_cw:
          for (int j = 0; j < O.repeat; j++) {
            start = O.fc;
            outlineMoveEndWord();
            end = O.fc;
            O.fc = start;
            for (int j = 0; j < end - start + 1; j++) outlineDelChar();
          }
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 1;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        //tested with repeat on one line
        case C_caw:
          for (int i = 0; i < O.repeat; i++) outlineDelWord();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 1;
          outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case C_gg:
         O.fc = O.rowoff = 0;
         O.fr = O.repeat-1; //this needs to take into account O.rowoff
         O.command[0] = '\0';
         O.repeat = 0;
         (*get_note)(O.row[O.fr].id); //if id == -1 does not try to retrieve note 
         return;

        default:
          // if a single char or sequence of chars doesn't match then
          // do nothing - the next char may generate a match
          return;

      } //end of keyfromstring switch under case NORMAL 

      return; // end of case NORMAL (don't think it can be reached)

    case COMMAND_LINE:

      switch(c) {

        case '\x1b': 
          O.mode = NORMAL;
          outlineSetMessage(""); 
          return;

        case ARROW_UP:
            if (NN == 11) NN = 1;
            else NN++;
            outlineSetMessage(":%s %s", O.command_line, context[NN]);
            return;

        case ARROW_DOWN:
            if (NN < 2) NN = 11;
            else NN--;
            outlineSetMessage(":%s %s", O.command_line, context[NN]);
            return;

        case '\r':
          ;
          int pos;
          char *new_context;
          //pointer passes back position of space (if there is one) in var pos
          //switch (commandfromstring(O.command_line, &pos))  
          command = commandfromstring(O.command_line, &pos); 
          switch(command) {

            case 'w':
              update_rows();
              O.mode = 0;
              O.command_line[0] = '\0';
              return;

             case 'x':
               update_rows();
               write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
               write(STDOUT_FILENO, "\x1b[H", 3); //sends cursor home (upper left)
               exit(0);
               return;

             case 'r':
               outlineSetMessage("\'%s\' will be refreshed", O.context);
               if (strcmp(O.context, "search") == 0) solr_find(search_terms);
               else (*get_data)(O.context, MAX); 
               O.mode = NORMAL;
               return;

            //in vim create new window and edit a file in it - here creates new item
            case 'n':
            case C_new: 
              outlineInsertRow2(0, "<new item>", 10, -1, true, false, false);
              O.fc = O.fr = O.rowoff = 0;
              outlineScroll();
              outlineRefreshScreen();  //? necessary
              O.mode = NORMAL;
              O.command[0] = '\0';
              O.repeat = 0;
              outlineSetMessage("");
              editorEraseScreen();
              editorRefreshScreen();
              return;

            case 'e':
            case C_edit: //edit the note of the current item
              ;
               int id = get_id(-1);
               if (id != -1) {
                 outlineSetMessage("Edit note %d", id);
                 outlineRefreshScreen();
                 (*get_note)(id); //if id == -1 does not try to retrieve note
                 editor_mode = true;
                 E.fr = E.fc = E.cx = E.cy = E.line_offset = 0;
                 E.mode = NORMAL;
                 E.command[0] = '\0';
               } else {
                 outlineSetMessage("You need to save item before you can "
                                   "create a note");
               }
               O.command[0] = '\0';
               O.mode = NORMAL;
               return;

            case 'f':
            case C_find: //catches 'fin' and 'find' 
              if (strlen(O.command_line) < 6) {
                outlineSetMessage("You need more characters");
                return;
              }  
              solr_find(&O.command_line[pos + 1]);
              outlineSetMessage("Will search items for \'%s\'", &O.command_line[pos + 1]);
              O.mode = NORMAL;
              O.context = "search";
              strcpy(search_terms, &O.command_line[pos + 1]);
              (*get_note)(get_id(-1));
              return;

            case C_update: //update solr
              update_solr();
              O.mode = NORMAL;
              return;

            case C_context:
              //NN is set to zero when entering COMMAND_LINE mode
               ;
               int context_tid;
               //char *new_context;
               if (NN) {
                 new_context = context[NN];
                 context_tid = NN;
               } else if (strlen(O.command_line) > 7) {
                 bool success = false;
                 for (int k = 1; k < 12; k++) { 
                   if (strncmp(&O.command_line[pos + 1], context[k], 3) == 0) {
                     new_context = context[k];
                     context_tid = k;
                     success = true;
                     break;
                   }
                 }

                 if (!success) {
                   outlineSetMessage("What you typed did not match any context");
                   return;
                 }

               } else {
                 outlineSetMessage("You need to provide at least 3 characters"
                                   "that match a context!");

                 O.command_line[1] = '\0';
                 return;
               }

               outlineSetMessage("Item %d will get context \'%s\'(%d)",
                                 get_id(-1), new_context, context_tid);

               (*update_context)(context_tid); 
               O.mode = NORMAL;
               O.command_line[0] = '\0'; //probably not necessary 
               return;

            case C_open:
              //NN is set to zero when entering COMMAND_LINE mode
               if (NN) {
                 new_context = context[NN];
               } else if (pos) {
                 bool success = false;
                 for (int k = 1; k < 12; k++) { 
                   if (strncmp(&O.command_line[pos + 1], context[k], 3) == 0) {
                     new_context = context[k];
                     success = true;
                     break;
                   }
                 }

                 if (!success) return;

               } else {
                 outlineSetMessage("You did not provide a valid  context!");
                 O.command_line[1] = '\0';
                 return;
               }
               outlineSetMessage("\'%s\' will be opened", new_context);
               (*get_data)(new_context, MAX); 
               O.context = new_context; 
               O.mode = NORMAL;
               (*get_note)(O.row[0].id);
               //editorRefreshScreen(); //in get_note
               return;

            case C_synch:
              synchronize(0); //1 -> report_only

              // free the E.row[j].chars not sure this has to be done
              if (E.row) {
                for (int j = 0 ; j < E.numrows ; j++ ) {
                  free(E.row[j].chars);
                } 
                free(E.row);
                E.row = NULL; 
                E.numrows = 0;
              }
              editorDisplayFile("log");//put them in the command mode case synch
              //editorRefreshScreen();
              O.mode = NORMAL;
              return;

            case C_synch_test:
              synchronize(1); //1 -> report_only

              // free the E.row[j].chars
              if (E.row) {
                for (int j = 0 ; j < E.numrows ; j++ ) {
                  free(E.row[j].chars);
                } 
                free(E.row);
                E.row = NULL; 
                E.numrows = 0;
              }
              editorDisplayFile("log");//put them in the command mode case synch
              //editorRefreshScreen();
              O.mode = NORMAL;
              return;

             case C_quit:
             case 'q':
               ;
               bool unsaved_changes = false;
               for (int i=0;i<O.numrows;i++) {
                 orow *row = &O.row[i];
                 if (row->dirty) {
                   unsaved_changes = true;
                   break;
                 }
               } 

               if (unsaved_changes) {
                 O.mode = NORMAL;
                 outlineSetMessage("No db write since last change");
           
               } else {
                 write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
                 write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
                 Py_FinalizeEx();
                 exit(0);
               }
               return;

             case C_quit0: //catches both :q! and :quit!
               write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
               write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
               Py_FinalizeEx();
               exit(0);
               return;

            case 'h':
            case C_help:
              initial_file_row = 0;
              O.mode = FILE_DISPLAY;
              editorDisplayFile("listmanager_commands");
              return;

            default: // default for commandfromstring

              //"\x1b[41m", 5); //red background
              outlineSetMessage("\x1b[41mNot an outline command: %s\x1b[0m", O.command_line);
              O.mode = NORMAL;
              return;

          } //end of commandfromstring switch within '\r' of case COMMAND_LINE

        default: //default for switch 'c' in case COMMAND_LINE
          ;
          int n = strlen(O.command_line);
          if (c == DEL_KEY || c == BACKSPACE) {
            O.command_line[n-1] = '\0';
          } else {
            O.command_line[n] = c;
            O.command_line[n+1] = '\0';
          }

          outlineSetMessage(":%s", O.command_line);

        } // end of 'c' switch within case COMMAND_LINE

      return; //end of outer case COMMAND_LINE

    case DATABASE:

      switch (c) {

        case ARROW_UP:
        case ARROW_DOWN:
        case 'h':
        case 'l':
          outlineMoveCursor(c);
          return;

        case ':':
          NN = 0; //index to contexts
          O.command[0] = '\0';
          O.command_line[0] = '\0';
          outlineSetMessage(":"); 
          O.mode = COMMAND_LINE;
          return;

        case 'x':
          O.fc = 0;
          O.repeat = 0;
          (*toggle_completed)();
          return;

        case 'd':
          O.fc = 0;
          O.repeat = 0;
          (*toggle_deleted)();
          return;

        case '*':
          O.fc = 0;
          O.repeat = 0;
          (*toggle_star)();
          return;

        case 's': 
          O.show_deleted = !O.show_deleted;
          O.show_completed = !O.show_completed;
          (*get_data)(O.context, MAX);
            
          return;

        case 'r':
          O.fc = 0;
          O.repeat = 0;
          (*get_data)(O.context, MAX);
          return;

        case ARROW_RIGHT:
        case '\x1b':
        case '>':
        case SHIFT_TAB:
        case '\t':
          O.mode = NORMAL;
          outlineSetMessage("");
          return;
  
        case 'i': //display item info
          ;
          orow *row = &O.row[O.fr];
          display_item_info(row->id);
          return;
  
        case 'v': //render in browser
          
          row = &O.row[O.fr];
          view_html(row->id);
          /* not getting error messages with qutebrowser so below not necessary (for the moment)
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */
          return;

        default:
          outlineSetMessage("I don't recognize %c", c);
          return;
      } // end of switch(c) in case DATABASLE

      return; //end of outer case DATABASE

    case VISUAL:
  
      switch (c) {
  
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          outlineMoveCursor(c);
          O.highlight[1] = O.fc; //this needs to be getFileCol
          return;
  
        case 'x':
          //O.repeat = O.highlight[1] - O.highlight[0] + 1;

          O.repeat = abs(O.highlight[1] - O.highlight[0]) + 1;
          outlineYankString(); //reportedly segfaults on the editor side

          // the delete below requires positioning the cursor
          O.fc = (O.highlight[1] > O.highlight[0]) ? O.highlight[0] : O.highlight[1];

          //O.cx = O.highlight[0] - O.coloff;
          //outlineYankString(); 
  
          for (int i = 0; i < O.repeat; i++) {
            outlineDelChar(); //uses editorDeleteChar2! on editor side
          }
          if (O.fc) O.fc--; 
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineSetMessage("");
          return;
  
        case 'y':  
          O.repeat = O.highlight[1] - O.highlight[0] + 1;
          O.fc = O.highlight[0];
          outlineYankString();
          O.command[0] = '\0';
          O.repeat = 0;
          O.mode = 0;
          outlineSetMessage("");
          return;
  
        case '\x1b':
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      return; //end of case VISUAL

    case REPLACE: 
      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar();
        outlineInsertChar(c);
      }
      O.repeat = 0;
      O.command[0] = '\0';
      O.mode = 0;

      return; //////// end of outer case REPLACE

    case FILE_DISPLAY: 

      //int initial_file_row; //for arrowing or displaying files
      switch (c) {
  
        case ARROW_UP:
        case 'k':
          initial_file_row--;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          editorDisplayFile("listmanager_commands");
          return;

        case ARROW_DOWN:
        case 'j':
          initial_file_row++;
          editorDisplayFile("listmanager_commands");
          return;

        case PAGE_UP:
          initial_file_row = initial_file_row - E.screenlines;
          initial_file_row = (initial_file_row < 0) ? 0: initial_file_row;
          editorDisplayFile("listmanager_commands");
          return;

        case PAGE_DOWN:
          initial_file_row = initial_file_row + E.screenlines;
          editorDisplayFile("listmanager_commands");
          return;

        case '\x1b':
          O.mode = NORMAL;
          O.command[0] = '\0';
          O.repeat = 0;
          outlineSetMessage("");
          return;
      }
      return;
  } //End of outer switch(O.mode)
}

// calls editorOpen to read the log file
void synchronize(int report_only) { //using 1 or 0

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("synchronize"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "synchronize"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = PyLong_FromLong(report_only);
          //pValue = Py_BuildValue("s", search_terms); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  outlineSetMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              outlineSetMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          outlineSetMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      outlineSetMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  //editorOpen("log");//put them in the command mode case synch
  //editorRefreshScreen();
  if (report_only) outlineSetMessage("Number of tasks/items that would be affected is %d", num);
  else outlineSetMessage("Number of tasks/items that were affected is %d", num);
}

void display_item_info_pg(int id) {

  if (id ==-1) return;

  char query[100];
  sprintf(query, "SELECT * FROM task WHERE id = %d", id);

  PGresult *res = PQexec(conn, query);    
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn)); 
    PQclear(res);
    return;
  }    

  char offset_lf_ret[20];
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", EDITOR_LEFT_MARGIN, TOP_MARGIN); 

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    abAppend(&ab, "\x1b[K", 3); 
    abAppend(&ab, offset_lf_ret, 7);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //set background color to blue
  abAppend(&ab, "\x1b[44m", 5);

  char str[300];
  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", PQgetvalue(res, 0, 0));
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", PQgetvalue(res, 0, 3));
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", context[atoi(PQgetvalue(res, 0, 6))]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (*PQgetvalue(res, 0, 8) == 't') ? "true" : "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 14) == 't') ? "true" : "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 10)) ? "true": "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", PQgetvalue(res, 0, 16));
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", PQgetvalue(res, 0, 9));
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  abAppend(&ab, offset_lf_ret, 7);
  abAppend(&ab, offset_lf_ret, 7);

  //note strsep handles multiple \n\n and strtok did not
  char *note;
  note = strdup(PQgetvalue(res, 0, 12)); // ******************
  char *found;

  for (int k=0; k < 4; k++) {

    if ((found = strsep(&note, "\n")) == NULL) break; 

    size_t len = E.screencols;
    abAppend(&ab, found, (strlen(found) < len) ? strlen(found) : len);
    abAppend(&ab, offset_lf_ret, 7);
  }

  abAppend(&ab, "\x1b[0m", 4);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab); 
  free(note);
  note = NULL; //? not necessary

  PQclear(res);
}

void display_item_info_sqlite(int id) {

  for (int j = 0 ; j < E.numrows ; j++ ) {
    free(E.row[j].chars);
  } 
  free(E.row);
  E.row = NULL; 
  E.numrows = 0;

  if (id ==-1) return;

  char query[100];
  sprintf(query, "SELECT * FROM task WHERE id = %d", id);

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  
  rc = sqlite3_exec(db, query, display_item_info_callback, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } 
  sqlite3_close(db);
}

// this version doesn't save the text in E.row
//which didn't make sense since you're not going to edit it
int display_item_info_callback(void *NotUsed, int argc, char **argv, char **azColName) {
    
  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
    
  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL
  */

  char offset_lf_ret[20];
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", EDITOR_LEFT_MARGIN, TOP_MARGIN); 

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < E.screenlines; i++) {
    abAppend(&ab, "\x1b[K", 3); 
    abAppend(&ab, offset_lf_ret, 7);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));

  //set background color to blue
  abAppend(&ab, "\x1b[44m", 5);

  char str[300];
  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", argv[0]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mtid:\x1b[0;44m %s", argv[1]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", argv[3]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", context[atoi(argv[6])]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (atoi(argv[8]) == 1) ? "true": "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (atoi(argv[14]) == 1) ? "true": "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (argv[10]) ? "true": "false");
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", argv[16]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", argv[9]);
  abAppend(&ab, str, strlen(str));
  abAppend(&ab, offset_lf_ret, 7);
  abAppend(&ab, offset_lf_ret, 7);
  abAppend(&ab, offset_lf_ret, 7);

  //note strsep handles multiple \n\n and strtok did not
  char *note;
  note = strdup(argv[12]); // ******************
  char *found;

  for (int k=0; k < 4; k++) {

    if ((found = strsep(&note, "\n")) == NULL) break; 

    size_t len = E.screencols;
    abAppend(&ab, found, (strlen(found) < len) ? strlen(found) : len);
    abAppend(&ab, offset_lf_ret, 7);
  }

  abAppend(&ab, "\x1b[0m", 4);

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab); 
  free(note);
  note = NULL; //? not necessary

  //editorRefreshScreen();
  return 0;
}

void update_note_pg(void) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  int len;
  char *text = editorRowsToString(&len);

 /*
 Note previously had used strncpy but  Valgrind just does not seem to like strncpy
 and no one else seems to like it as well
 */

  char *note = malloc(len + 1);
  memcpy(note, text, len);
  note[len] = '\0'; 

  /*
  Below replaces single quotes with two single quotes which escapes the 
  single quote see:
  https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
  */

  const char *str = note;
  int cnt = strlen(str)+1;
   for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
          ;
  //VLA
  char escaped_note[cnt];
  char *out = escaped_note;
  const char *in = str;
  while (*in) {
      *out++ = *in;
      if (*in == 39) *out++ = 39;
      //if (*in == 133) *out++ = 32;

      in++;
  }

  *out = '\0';

  int id = get_id(O.fr);

  char *query = malloc(cnt + 100);

  sprintf(query, "UPDATE task SET note=\'%s\', "
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                   escaped_note, id);

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    editorSetMessage(PQerrorMessage(conn));
  } else {
    outlineSetMessage("Updated note for item %d", id);
    outlineRefreshScreen();
    editorSetMessage("Note update succeeeded"); 
  }
  
  free(note);
  free(query);
  PQclear(res);
  //do_exit(conn);
  free(text);
  E.dirty = 0;

  outlineSetMessage("Updated %d", id);

  return;
}

void update_note_sqlite(void) {

  int len;
  char *text = editorRowsToString(&len);

 /*
 Note previously had used strncpy but  Valgrind just does not seem to like strncpy
 and no one else seems to like it as well
 */

  char *note = malloc(len + 1);
  memcpy(note, text, len);
  note[len] = '\0'; 

  //Below replaces single quotes with two single quotes which escapes the single quote
  // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
  const char *str = note;
  int cnt = strlen(str)+1;
   for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
          ;
  //VLA
  char escaped_note[cnt];
  char *out = escaped_note;
  const char *in = str;
  while (*in) {
      *out++ = *in;
      if (*in == 39) *out++ = 39;
      //if (*in == 133) *out++ = 32;

      in++;
  }

  *out = '\0';

  int id = get_id(O.fr);

  char *query = malloc(cnt + 100);

  sprintf(query, "UPDATE task SET note=\'%s\', "
                   "modified=datetime('now', '-5 hours') "
                   "WHERE id=%d", //tid
                   escaped_note, id);


  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Updated note for item %d", id);
    outlineRefreshScreen();
    editorSetMessage("Note update succeeeded"); 
  }

  sqlite3_close(db);

  free(note);
  free(query);
  free(text);
  E.dirty = 0;
}

void update_context_pg(int context_tid) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
      outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
    }
  }

  char query[300];
  int id = get_id(-1);

  sprintf(query, "UPDATE task SET context_tid=%d, " 
                 "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    context_tid,
                    id);

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Postgres Error: %s", PQerrorMessage(conn));
  } else {
    outlineSetMessage("Setting context to %s succeeded", context[context_tid]);
  }
  PQclear(res);
}

void update_context_sqlite(int context_tid) {

  char query[300];
  int id = get_id(-1);

  sprintf(query, "UPDATE task SET context_tid=%d, " 
                 "modified=datetime('now', '-5 hours') "
                   "WHERE id=%d",
                    context_tid,
                    id);

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Setting context to %s succeeded", context[context_tid]);
  }

  sqlite3_close(db);
}

void toggle_completed_pg(void) {

  orow *row = &O.row[O.fr];

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  char query[300];
  int id = get_id(-1);
  if (row->completed) 
     sprintf(query, "UPDATE task SET completed=NULL, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);
  else 
     sprintf(query, "UPDATE task SET completed=CURRENT_DATE, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle completed failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle completed succeeded");
    row->completed = !row->completed;
    //row->dirty = true;
  }
  PQclear(res);
}

void toggle_completed_sqlite(void) {

  orow *row = &O.row[O.fr];

  char query[300];
  int id = get_id(-1);

  sprintf(query, "UPDATE task SET completed=%s, " 
                   "modified=datetime('now', '-5 hours') "
                   "WHERE id=%d", //tid
                   (row->completed) ? "NULL" : "date()", id);

  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle completed succeeded");
    row->completed = !row->completed;
    //row->dirty = true;
  }

  sqlite3_close(db);
    
}

void toggle_deleted_pg(void) {

  orow *row = &O.row[O.fr];

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  char query[300];
  int id = get_id(-1);
  if (row->deleted) 
     sprintf(query, "UPDATE task SET deleted=False, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);
  else 
     sprintf(query, "UPDATE task SET deleted=True, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle deleted failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle deleted succeeded");
    row->deleted = !row->deleted;
    row->dirty = true;
  }
  PQclear(res);
  return;
}

void toggle_deleted_sqlite(void) {

  orow *row = &O.row[O.fr];

  char query[300];
  int id = get_id(-1);

  sprintf(query, "UPDATE task SET deleted=%s, " 
                 "modified=datetime('now', '-5 hours') "
                 "WHERE id=%d", //tid
                 (row->deleted) ? "False" : "True", id);
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
    }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle deleted succeeded");
    row->deleted = !row->deleted;
    //row->dirty = true;
  }

  sqlite3_close(db);

}

void toggle_star_pg(void) {

  orow *row = &O.row[O.fr];

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  char query[300];
  int id = get_id(-1);
  if (row->star) 
     sprintf(query, "UPDATE task SET star=False, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);
  else 
     sprintf(query, "UPDATE task SET star=True, " 
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                    id);

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("Toggle star failed - %s", PQresultErrorMessage(res));
  }
  else {
    outlineSetMessage("Toggle star succeeded");
    row->star = !row->star;
    row->dirty = true;
  }
  PQclear(res);
  return;
}

void toggle_star_sqlite(void) {

  orow *row = &O.row[O.fr];

  char query[300];
  int id = get_id(-1);

  sprintf(query, "UPDATE task SET star=%s, " 
                 "modified=datetime('now', '-5 hours') "
                 "WHERE id=%d", //tid
                 (row->star) ? "False" : "True", id);
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
  } else {
    outlineSetMessage("Toggle star succeeded");
    row->star = !row->star;
    //row->dirty = true;
  }

  sqlite3_close(db);

}

void update_row_pg(void) {

  orow *row = &O.row[O.fr];

  if (!row->dirty) {
    outlineSetMessage("Row has not been changed");
    return;
  }

  if (row->id != -1) {

    if (PQstatus(conn) != CONNECTION_OK){
      if (PQstatus(conn) == CONNECTION_BAD) {
          
          fprintf(stderr, "Connection to database failed: %s\n",
              PQerrorMessage(conn));
          do_exit(conn);
      }
    }
  
    int len = row->size;
    char *title = malloc(len + 1);
    memcpy(title, row->chars, len);
    title[len] = '\0';
  
    // Need to replace single quotes with two single quotes which escapes the single quote 
    // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
    const char *str = title;
    int cnt = strlen(str)+1;
    for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
            ;
    //VLA
    char escaped_title[cnt];
    escaped_title[cnt - 1] = '\0';
    char *out = escaped_title;
    const char *in = str;
    while (*in) {
      *out++ = *in;
      if (*in == 39) *out++ = 39;
      in++;
    }
  
    *out = '\0';
  
    char *query = malloc(cnt + 100);
  
    sprintf(query, "UPDATE task SET title=\'%s\', "
                     "modified=LOCALTIMESTAMP - interval '5 hours' "
                     "WHERE id=%d",
                     escaped_title, row->id);
  
    PGresult *res = PQexec(conn, query); 
      
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      outlineSetMessage(PQerrorMessage(conn));
    } else {
      row->dirty = false;    
      outlineSetMessage("Successfully update row %d", row->id);
    }  
    free(query);
    free(title);
    PQclear(res);

  } else { 
    insert_row_pg(O.fr);
  }  
}

void update_row_sqlite(void) {

  orow *row = &O.row[O.fr];

  if (!row->dirty) {
    outlineSetMessage("Row has not been changed");
    return;
  }

  if (row->id != -1) {

    int len = row->size;
    char *title = malloc(len + 1);
    memcpy(title, row->chars, len);
    title[len] = '\0';
  
    // Need to replace single quotes with two single quotes which escapes the single quote 
    // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
    const char *str = title;
    int cnt = strlen(str)+1;
    for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
            ;
    //VLA
    char escaped_title[cnt];
    escaped_title[cnt - 1] = '\0';
    char *out = escaped_title;
    const char *in = str;
    while (*in) {
      *out++ = *in;
      if (*in == 39) *out++ = 39;
      in++;
    }
  
    *out = '\0';
  
    char *query = malloc(cnt + 100);
  
    sprintf(query, "UPDATE task SET title=\'%s\', "
                     "modified=datetime('now', '-5 hours') "
                     "WHERE id=%d",
                     escaped_title, row->id);
  
    sqlite3 *db;
    char *err_msg = 0;
      
    int rc = sqlite3_open(SQLITE_DB, &db);
      
    if (rc != SQLITE_OK) {
          
      outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
      sqlite3_close(db);
      return;
      }
  
    rc = sqlite3_exec(db, query, 0, 0, &err_msg);
      
    if (rc != SQLITE_OK ) {
      outlineSetMessage("SQL error: %s\n", err_msg);
      sqlite3_free(err_msg);
    } else {
      row->dirty = false;    
      outlineSetMessage("Successfully updated row %d", row->id);
    }

    free(query);
    free(title);
    sqlite3_close(db);

  } else { 
    insert_row_sqlite(O.fr);
  }  
}

int insert_row_pg(int ofr) {

  orow *row = &O.row[ofr];

  int len = row->size;
  char *title = malloc(len + 1);
  memcpy(title, row->chars, len);
  title[len] = '\0';

  //Below is the code that replaces single quotes with two single quotes which escapes the single quote - this is required.
  // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
  const char *str = title;
  int cnt = strlen(str)+1;
  for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates at the end of the string where *p == 0
          ;
  //VLA
  char escaped_title[cnt];
  escaped_title[cnt - 1] = '\0';
  char *out = escaped_title;
  const char *in = str;
  while (*in) {
    *out++ = *in;
    if (*in == 39) *out++ = 39;
      in++;
    }

 int context_tid = 1; //just in case there isn't a match but there has to be
 for (int k = 1; k < 12; k++) { 
   if (strncmp(O.context, context[k], 3) == 0) { //2 chars all you need
     context_tid = k;
     break;
   }
 }

 char *query = malloc(cnt + 400); //longer than usual update query - non-title part takes about 300 bytes so being safe

 //may be a problem if note or title have characters like ' so may need to \ ahead of those characters
 sprintf(query, "INSERT INTO task ("
                                   //"tid, "
                                   "priority, "
                                   "title, "
                                   //"tag, "
                                   "folder_tid, "
                                   "context_tid, "
                                   //"duetime, "
                                   "star, "
                                   "added, "
                                   //"completed, "
                                   //"duedate, "
                                   "note, "
                                   //"repeat, "
                                   "deleted, "
                                   "created, "
                                   "modified, "
                                   "startdate " 
                                   //"remind) "
                                   ") VALUES ("
                                   //"%s," //tid 
                                   " %d," //priority
                                   " \'%s\',"//title
                                   //%s //tag 
                                   " %d," //folder_tid
                                   " %d," //context_tid 
                                   //%s, //duetime
                                   " %s," //star 
                                   "%s," //added 
                                   //"%s," //completed 
                                   //"%s," //duedate 
                                   " \'%s\'," //note
                                   //s% //repeat
                                   " %s," //deleted
                                   " %s," //created 
                                   " %s," //modified
                                   " %s" //startdate
                                   //%s //remind
                                   ") RETURNING id;",
                                     
                                   //tid, 
                                   3, //priority, 
                                   escaped_title, 
                                   //tag, 
                                   1, //folder_tid,
                                   context_tid, 
                                   //duetime, 
                                   "True", //star, 
                                   "CURRENT_DATE", //starttime
                                   //completed, 
                                   //duedate, 
                                   "<This is a new note>", //note, 
                                   //repeat, 
                                   "False", //deleted 
                                   "LOCALTIMESTAMP - interval '5 hours'", //created, 
                                   "LOCALTIMESTAMP - interval '5 hours'", //modified 
                                   "CURRENT_DATE" //startdate  
                                   //remind
                                   );
                                     
                            
    
    
  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) { //PGRES_TUPLES_OK is for query that returns data
    outlineSetMessage("PQerrorMessage: %s", PQerrorMessage(conn)); //often same message - one below is on the problematic result
    //outlineSetMessage("PQresultErrorMessage: %s", PQresultErrorMessage(res));
    PQclear(res);
    return -1;
  }

  row->id = atoi(PQgetvalue(res, 0, 0));
  row->dirty = false;
        
  free(title);
  free(query);
  PQclear(res);
  outlineSetMessage("Successfully inserted new row with id %d", row->id);
    
  return row->id;
}

int insert_row_sqlite(int ofr) {

  orow *row = &O.row[ofr];

  int len = row->size;
  char *title = malloc(len + 1);
  memcpy(title, row->chars, len);
  title[len] = '\0';

  //Below is the code that replaces single quotes with two single quotes which escapes the single quote - this is required.
  // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
  const char *str = title;
  int cnt = strlen(str)+1;
  for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates at the end of the string where *p == 0
          ;
  //VLA
  char escaped_title[cnt];
  escaped_title[cnt - 1] = '\0';
  char *out = escaped_title;
  const char *in = str;
  while (*in) {
    *out++ = *in;
    if (*in == 39) *out++ = 39;
      in++;
    }

 int context_tid = 1; //just in case there isn't a match but there has to be
 for (int k = 1; k < 12; k++) { 
   if (strncmp(O.context, context[k], 3) == 0) { //2 chars all you need
     context_tid = k;
     break;
   }
 }

 char *query = malloc(cnt + 400); //longer than usual update query - non-title part takes about 300 bytes so being safe

 //may be a problem if note or title have characters like ' so may need to \ ahead of those characters
 sprintf(query, "INSERT INTO task ("
                                   //"tid, "
                                   "priority, "
                                   "title, "
                                   //"tag, "
                                   "folder_tid, "
                                   "context_tid, "
                                   //"duetime, "
                                   "star, "
                                   "added, "
                                   //"completed, "
                                   //"duedate, "
                                   "note, "
                                   //"repeat, "
                                   "deleted, "
                                   "created, "
                                   "modified, "
                                   "startdate " 
                                   //"remind) "
                                   ") VALUES ("
                                   //"%s," //tid 
                                   " %d," //priority
                                   " \'%s\',"//title
                                   //%s //tag 
                                   " %d," //folder_tid
                                   " %d," //context_tid 
                                   //%s, //duetime
                                   " %s," //star 
                                   "%s," //added 
                                   //"%s," //completed 
                                   //"%s," //duedate 
                                   " \'%s\'," //note
                                   //s% //repeat
                                   " %s," //deleted
                                   " %s," //created 
                                   " %s," //modified
                                   " %s" //startdate
                                   //%s //remind
                                   ");", // RETURNING id;", // ************************************
                                     
                                   //tid, 
                                   3, //priority, 
                                   escaped_title, 
                                   //tag, 
                                   1, //folder_tid,
                                   context_tid, 
                                   //duetime, 
                                   "True", //star, 
                                   "date()", //starttime
                                   //completed, 
                                   //duedate, 
                                   "<This is a new note from sqlite>", //note, 
                                   //repeat, 
                                   "False", //deleted 
                                   "datetime()", //created, 
                                   "datetime()", //modified 
                                   "date()" //startdate  
                                   //remind
                                   );
                                     
  sqlite3 *db;
  char *err_msg = 0;
    
  int rc = sqlite3_open(SQLITE_DB, &db);
    
  if (rc != SQLITE_OK) {
        
    outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query, 0, 0, &err_msg);
    
  if (rc != SQLITE_OK ) {
    outlineSetMessage("SQL error: %s\n", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  row->id =  sqlite3_last_insert_rowid(db);
  row->dirty = false;
        
  free(title);
  free(query);
  sqlite3_close(db);
  outlineSetMessage("Successfully inserted new row with id %d", row->id);
    
  return row->id;
}

void update_rows_pg(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
      outlineSetMessage(PQerrorMessage(conn));
      //do_exit(conn);
    }
  }

  for (int i=0; i < O.numrows;i++) {
    orow *row = &O.row[i];

    if (!(row->dirty)) continue;

    if (row->id != -1) {

      int len = row->size;
      char *title = malloc(len + 1);
      memcpy(title, row->chars, len); //seems to me I could also memcpy len + 1 and get the '\0' and not have to set it below
      title[len] = '\0';

      //Below is the code that replaces single quotes with two single quotes which escapes the single quote - this is required.
      // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
      const char *str = title;
      int cnt = strlen(str)+1;
      for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
          ;
      //VLA
      char escaped_title[cnt];
      escaped_title[cnt - 1] = '\0';
      char *out = escaped_title;
      const char *in = str;
      while (*in) {
          *out++ = *in;
          if (*in == 39) *out++ = 39;
          in++;
      }

      *out = '\0';

      char *query = malloc(cnt + 200);

      sprintf(query, "UPDATE task SET title=\'%s\', "
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                   escaped_title, row->id);

      PGresult *res = PQexec(conn, query); 
  
      if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        outlineSetMessage(PQerrorMessage(conn));
        PQclear(res);
        return;
      } else {
        row->dirty = false;    
        updated_rows[n] = row->id;
        n++;
        free(query);
        free(title);
        PQclear(res);
      }  
    } else { 
      int id  = insert_row_pg(i);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineSetMessage("There were no rows to update");
    return;
  }

  outlineSetMessage("Rows successfully updated ... %d", sizeof(updated_rows));
  
  outlineSetMessage("Rows successfully updated ... ");
  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  outlineSetMessage("%s",  msg);
}

void update_rows_sqlite(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  for (int i=0; i < O.numrows;i++) {
    orow *row = &O.row[i];

    if (!(row->dirty)) continue;

    if (row->id != -1) {

      int len = row->size;
      char *title = malloc(len + 1);
      memcpy(title, row->chars, len); //seems to me I could also memcpy len + 1 and get the '\0' and not have to set it below
      title[len] = '\0';

      //Below is the code that replaces single quotes with two single quotes which escapes the single quote - this is required.
      // see https://stackoverflow.com/questions/25735805/replacing-a-character-in-a-string-char-array-with-multiple-characters-in-c
      const char *str = title;
      int cnt = strlen(str)+1;
      for (const char *p = str ; *p ; cnt += (*p++ == 39)) //39 -> ' *p terminates for at the end of the string where *p == 0
          ;
      //VLA
      char escaped_title[cnt];
      escaped_title[cnt - 1] = '\0';
      char *out = escaped_title;
      const char *in = str;
      while (*in) {
          *out++ = *in;
          if (*in == 39) *out++ = 39;
          in++;
      }

      *out = '\0';

      char *query = malloc(cnt + 200);

      sprintf(query, "UPDATE task SET title=\'%s\', "
                   "modified=datetime('now', '-5 hours') "
                   "WHERE id=%d", //tid
                   escaped_title, row->id);

  
      sqlite3 *db;
      char *err_msg = 0;
        
      int rc = sqlite3_open(SQLITE_DB, &db);
        
      if (rc != SQLITE_OK) {
            
        outlineSetMessage("Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
        }
    
      rc = sqlite3_exec(db, query, 0, 0, &err_msg);
        
      if (rc != SQLITE_OK ) {
        outlineSetMessage("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        free(query);
        free(title);
        sqlite3_close(db);
        return; // ? should we abort all other rows
      } else {
        row->dirty = false;    
        updated_rows[n] = row->id;
        n++;
        free(query);
        free(title);
        sqlite3_close(db);
      }
    
    } else { 
      int id  = insert_row_sqlite(i);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    outlineSetMessage("There were no rows to update");
    return;
  }

  outlineSetMessage("Rows successfully updated ... %d", sizeof(updated_rows));
  
  outlineSetMessage("Rows successfully updated ... ");
  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  outlineSetMessage("%s",  msg);
}

/*
int outlineGetResultSetRow(void) {
  return O.cy + O.rowoff; 
}

int outlineGetFileCol(void) {
  return O.cx + O.coloff; ////////
}
*/
int get_id(int fr) {
  if(fr==-1) fr = O.fr;
  int id = O.row[fr].id;
  return id;
}

void outlineChangeCase() {
  orow *row = &O.row[O.fr];
  char d = row->chars[O.fc];
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    outlineMoveCursor(ARROW_RIGHT);
    return;
  }
  outlineDelChar();
  outlineInsertChar(d);
}

void outlineYankLine(int n){
  for (int i=0; i < 10; i++) {
    free(line_buffer[i]);
    line_buffer[i] = NULL;
    }

  for (int i=0; i < n; i++) {
    int len = O.row[O.cy+i].size;
    line_buffer[i] = malloc(len + 1);
    memcpy(line_buffer[i], O.row[O.cy+i].chars, len);
    line_buffer[i][len] = '\0';
  }
  // set string_buffer to "" to signal should paste line and not chars
  string_buffer[0] = '\0';
}

void outlineYankString() {
  int n,x;
  orow *row = &O.row[O.cy];
  for (x = O.highlight[0], n = 0; x < O.highlight[1]+1; x++, n++) {
      string_buffer[n] = row->chars[x];
  }

  string_buffer[n] = '\0';
}

/*************started here*****************/



void outlinePasteString(void) {
  if (O.numrows == 0) return;

  orow *row = &O.row[O.fr];
  int len = strlen(string_buffer);
  row->chars = realloc(row->chars, row->size + len); 

  /* moving all the chars at the current x cursor position on char
     farther down the char string to make room for the new character
     Maybe a clue from outlineInsertRow - it's memmove is below
     memmove(&O.row[at + 1], &O.row[at], sizeof(orow) * (O.numrows - at));
  */

  memmove(&row->chars[O.fc + len], &row->chars[O.fc], row->size - O.fc); //****was O.cx + 1

  for (int i = 0; i < len; i++) {
    row->size++;
    row->chars[O.fc] = string_buffer[i];
    O.fc++;
  }
  row->dirty = true;
}

void outlineDelWord() {

  orow *row = &O.row[O.fr];
  if (row->chars[O.fc] < 48) return;

  int i,j,x;
  for (i = O.fc; i > -1; i--){
    if (row->chars[i] < 48) break;
    }
  for (j = O.fc; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }
  O.fc = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  row->dirty = true;
  //outlineSetMessage("i = %d, j = %d", i, j ); 
}

void outlineDeleteToEndOfLine(void) {

  orow *row = &O.row[O.fr];
  row->size = O.fc;
  //Arguably you don't have to reallocate when you reduce the length of chars
  row->chars = realloc(row->chars, O.fc + 1); //added 10042018 - before wasn't reallocating memory
  row->chars[O.fc] = '\0';
  row->dirty = true;
  }

void outlineMoveCursorEOL() {

  O.fc = O.row[O.fr].size - 1;  //if O.cx > O.screencols will be adjusted in EditorScroll
}

// not same as 'e' but moves to end of word or stays put if already on end of word
void outlineMoveEndWord2() {
  int j;
  orow row = O.row[O.fr];

  for (j = O.fc + 1; j < row.size ; j++) {
    if (row.chars[j] < 48) break;
  }

  O.fc = j - 1;
}

void outlineMoveNextWord() {
  // below is same is outlineMoveEndWord2
  int j;
  orow row = O.row[O.fr];

  for (j = O.fc + 1; j < row.size ; j++) {
    if (row.chars[j] < 48) break;
  }

  O.fc = j - 1;
  // end outlineMoveEndWord2

  for (j = O.fc + 1; j < row.size ; j++) { //+1
    if (row.chars[j] > 48) break;
  }
  O.fc = j;
}

void outlineMoveBeginningWord() {
  orow *row = &O.row[O.fr];
  if (O.fc == 0) return;
  for (;;) {
    if (row->chars[O.fc - 1] < 48) O.fc--;
    else break;
    if (O.fc == 0) return;
  }

  int i;
  for (i = O.fc - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  O.fc = i + 1;
}


/***************************************restarted*****************************/




void outlineMoveEndWord() {
  orow *row = &O.row[O.fr];
  if (O.fc == row->size - 1) return;
  for (;;) {
    if (row->chars[O.fc + 1] < 48) O.fc++;
    else break;
    if (O.fc == row->size - 1) return;
  }

  int j;
  for (j = O.fc + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }

  O.fc = j - 1;
}

void outlineGetWordUnderCursor(){
  orow *row = &O.row[O.fr];
  if (row->chars[O.fc] < 48) return;

  int i,j,n,x;

  for (i = O.fc - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  for (j = O.fc + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }

  for (x = i + 1, n = 0; x < j; x++, n++) {
      search_string[n] = row->chars[x];
  }

  search_string[n] = '\0';
  outlineSetMessage("word under cursor: <%s>", search_string); 

}

void outlineFindNextWord() {
  int y, x;
  char *z;
  y = O.fr;
  x = O.fc + 1; //in case sitting on beginning of the word
 
  /*n counter so we can exit for loop if there are  no matches for command 'n'*/
  for ( int n=0; n < O.numrows; n++ ) {
    orow *row = &O.row[y];
    z = strstr(&(row->chars[x]), search_string);
    if ( z != NULL ) {
      O.fr = y;
      O.fc = z - row->chars;
      break;
    }
    y++;
    x = 0;
    if ( y == O.numrows ) y = 0;
  }

    outlineSetMessage("x = %d; y = %d", x, y); 
}

/*** slz testing stuff ***/

/*void getcharundercursor() {
  orow *row = &O.row[O.cy];
  char d = row->chars[O.cx];
  outlineSetMessage("character under cursor at position %d of %d: %c", O.cx, row->size, d); 
}
*/

void editorScroll(void) {

  if (!E.row) return;

  /* this is the money shot -- derive E.cx and E.cy from file row and char/*/
  int *screeny_screenx = editorGetScreenPosFromRowCharPosWW(E.fr, E.fc); 
  E.cx = screeny_screenx[1];
  E.cy = screeny_screenx[0];

  if (E.cy > E.screenlines - 1){
    E.cy--;
    E.line_offset++;
  }

  if (E.cy < 0) {
     E.line_offset+=E.cy;
     E.cy = 0;
     //if (E.fr == 0) E.line_offset = 0; //? necessary - doubt it -02212019
  }

  // vim seems to want full rows to be displayed although I am not sure
  // it's either helpful or worthit but this is a placeholder for the idea
}

void editorDrawRows(struct abuf *ab) {
  int y = 0;
  int len, n;
  int j,k; // to swap E.highlitgh[0] and E.highlight[1] if necessary
  char offset_lf_ret[20];
  snprintf(offset_lf_ret, sizeof(offset_lf_ret), "\r\n\x1b[%dC\x1b[%dB", EDITOR_LEFT_MARGIN, TOP_MARGIN); 

  // this is the first visible row on the screen given E.line_offset
  // seems like it will always then display all top row's lines
  int filerow = editorGetFileRowByLineWW(0); 

  for (;;){
    if (y >= E.screenlines) break; //somehow making this >= made all the difference

    // if there is still screen real estate but we've run out of text rows (E.numrows)
    //drawing '~' below: first escape is red color (31m) and K erases rest of line
    if (filerow > E.numrows - 1) { 

      //may not be worth this if else to not draw ~ in first row
      //and probably there is a better way to do it
      if (y) abAppend(ab, "\x1b[31m~\x1b[K", 9); 
      else abAppend(ab, "\x1b[K", 3); 
      abAppend(ab, offset_lf_ret, 7);
      y++;

    // this else is the main drawing code
    } else {

      int lines = editorGetLinesInRowWW(E.fr);

      // below is trying to emulate what vim does when it can't show an entire row (which will be multi-screen-line)
      // at the bottom of the screen because of where the screen scroll is.  It shows nothing of the row
      // rather than show a partial row.  May not be worth it.
      if ((y + lines) > E.screenlines) {
          for (n=0; n < (E.screenlines - y);n++) {
            abAppend(ab, "@", 2);
            abAppend(ab, "\x1b[K", 3); 
            abAppend(ab, offset_lf_ret, 7);
          }
        break;
      }      

    erow *row = &E.row[filerow];
    char *start,*right_margin;
    int left, width;
    bool more_lines = true;

    left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
    start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
    width = E.screencols; //wrapping width
    
    //while(*start) //exit when hit the end of the string '\0' - #1
    while(more_lines) {

        if (left <= width) {//after creating whatever number of lines if remainer <= width: get out
          len = left;
          more_lines = false;
        } else {
          right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces

          while(!isspace(*right_margin)) { //#2
            right_margin--;
            if( right_margin == start) {// situation in which there were no spaces to break the link
              right_margin += width - 1;
              break; 
            }

          } //end #2

          len = right_margin - start + 1;
          left -= right_margin-start+1;      /* +1 for the space */
          //start = [see below]
        }
        y++;

        if (E.mode == VISUAL_LINE) { 

          // below in case E.highlight[1] < E.highlight[0]
          k = (E.highlight[1] > E.highlight[0]) ? 1 : 0;
          j =!k;
         
          if (filerow >= E.highlight[j] && filerow <= E.highlight[k]) {
            abAppend(ab, "\x1b[48;5;242m", 11);
            abAppend(ab, start, len);
            abAppend(ab, "\x1b[49m", 5); //return background to normal
          } else abAppend(ab, start, len);

        } else if (E.mode == VISUAL && filerow == E.fr) {

          // below in case E.highlight[1] < E.highlight[0]
          k = (E.highlight[1] > E.highlight[0]) ? 1 : 0;
          j =!k;

          char *Ehj = &(row->chars[E.highlight[j]]);
          char *Ehk = &(row->chars[E.highlight[k]]);

          if ((Ehj >= start) && (Ehj < start + len)) {
            abAppend(ab, start, Ehj - start);
            abAppend(ab, "\x1b[48;5;242m", 11);
            if ((Ehk - start) > len) {
              abAppend(ab, Ehj, len - (Ehj - start));
              abAppend(ab, "\x1b[49m", 5); //return background to normal
            } else {
              abAppend(ab, Ehj, E.highlight[k] - E.highlight[j]);
              abAppend(ab, "\x1b[49m", 5); //return background to normal
              abAppend(ab, Ehk, start + len - Ehk);
            }
          } else if ((Ehj < start) && (Ehk > start)) {
              abAppend(ab, "\x1b[48;5;242m", 11);

            if ((Ehk - start) > len) {
              abAppend(ab, start, len);
              abAppend(ab, "\x1b[49m", 5); //return background to normal
            } else {  
              abAppend(ab, start, Ehk - start);
              abAppend(ab, "\x1b[49m", 5); //return background to normal
              abAppend(ab, Ehk, start + len - Ehk);
            }  

          } else abAppend(ab, start, len);
        } else abAppend(ab, start, len);
    
      // "\x1b[K" erases the part of the line to the right of the cursor in case the
      // new line i shorter than the old
      abAppend(ab, "\x1b[K", 3); 

      abAppend(ab, offset_lf_ret, 7);
      abAppend(ab, "\x1b[0m", 4); //slz return background to normal

      start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
      }

      filerow++;
    } // end of main drawing else block
   abAppend(ab, "\x1b[0m", 4); //slz return background to normal
  } // end of top for loop
}

//status bar has inverted colors
void editorDrawStatusBar(struct abuf *ab) {

  int efr = (E.row) ? E.fr : -1;

  orow *row = &O.row[O.fr];

  // position the cursor at the beginning of the editor status bar at correct indent
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenlines + TOP_MARGIN + 1,
                                            EDITOR_LEFT_MARGIN);//+1 
  abAppend(ab, buf, strlen(buf));

  abAppend(ab, "\x1b[K", 3); //cursor should be in middle of screen?? now explicit above

  abAppend(ab, "\x1b[7m", 4); //switches to inverted colors
  char status[80], rstatus[80];
  //char truncated_title[20];
  //strncpy(truncated_title, orow->chars, 19);
  //truncated_title[20] = '\0'; // <- that should have been 19

  int len = (row->size < 20) ? row->size : 19;
  char *truncated_title = malloc(len + 1);
  memcpy(truncated_title, row->chars, len);
  truncated_title[len] = '\0'; // if title is shorter than 19, should be fine

  len = snprintf(status, sizeof(status), "%.20s - %d line %s %s",
    O.context ? O.context : "[No Name]", E.numrows,
    truncated_title,
    E.dirty ? "(modified)" : "");
    //"*");
  int rlen = snprintf(rstatus, sizeof(rstatus), "Status bar %d/%d",
    efr + 1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);
  
  /* add spaces until you just have enough room
     left to print the status message  */

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); //switches back to normal formatting
  free(truncated_title);
}

void editorDrawMessageBar(struct abuf *ab) {
  // Position cursor on last row and mid-screen
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenlines + TOP_MARGIN + 2,
                                            EDITOR_LEFT_MARGIN);  //+1
  abAppend(ab, buf, strlen(buf));

  abAppend(ab, "\x1b[K", 3); // will erase midscreen -> R; cursor doesn't move after erase
  int msglen = strlen(E.message);
  if (msglen > E.screencols) msglen = E.screencols;
  abAppend(ab, E.message, msglen);
}

void editorRefreshScreen(void) {
  char buf[32];
  editorScroll(); 

    /* struct abuf {
      char *b;
      int len;
    };
    */

  if (DEBUG) {
    if (E.row){
      int *screeny_screenx = editorGetScreenPosFromRowCharPosWW(E.fr, E.fc); 
      int line = editorGetLineInRowWW();
      int line_char_count = editorGetLineCharCountWW(E.fr, line); 

      editorSetMessage("row(0)=%d line(1)=%d char(0)=%d line-char-count=%d screenx(0)=%d, E.screencols=%d", E.fr, line, E.fc, line_char_count, screeny_screenx[1], E.screencols);
    } else
      editorSetMessage("E.row is NULL, E.cx = %d, E.cy = %d,  E.numrows = %d, E.line_offset = %d", E.cx, E.cy, E.numrows, E.line_offset); 
  }
  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  //char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); 
  abAppend(&ab, buf, strlen(buf));
  //abAppend(&ab, "\x1b[H", 3);  //sends the cursor home


  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // the lines below position the cursor where it should go
  if (E.mode != COMMAND_LINE){
    //char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + TOP_MARGIN + 1, E.cx + EDITOR_LEFT_MARGIN + 1);
    abAppend(&ab, buf, strlen(buf));
  }

  if (E.dirty == 1) {
    //The below needs to be in a function that takes the color as a parameter
    write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode

    for (int k = OUTLINE_LEFT_MARGIN + O.screencols + 1; k < screencols ;k++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
      write(STDOUT_FILENO, buf, strlen(buf));
      //write(STDOUT_FILENO, "\x1b[31;1mq", 8); //horizontal line
      write(STDOUT_FILENO, "\x1b[31mq", 6); //horizontal line
    }
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, O.screencols + OUTLINE_LEFT_MARGIN + 1);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[31mw", 6); //'T' corner
    write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
    write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
  }  


  abAppend(&ab, "\x1b[?25h", 6); //shows the cursor

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab); 
}


/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

void editorSetMessage(const char *fmt, ...) {
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  vsnprintf(E.message, sizeof(E.message), fmt, ap);
  va_end(ap); //free a va_list
}

void editorMoveCursor(int key) {

  if (!E.row) return; //could also be !E.numrows

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (E.fc > 0) E.fc--; 
      break;

    case ARROW_RIGHT:
    case 'l':
      E.fc++;
      break;

    case ARROW_UP:
    case 'k':
      if (E.fr > 0) E.fr--;
      break;

    case ARROW_DOWN:
    case 'j':
      if (E.fr < E.numrows - 1) E.fr++;
      break;
  }

  int row_size = E.row[E.fr].size;
  if (E.fc >= row_size) E.fc = row_size - (E.mode != INSERT); 
}

// calls readKey()
void editorProcessKeypress(void) {
  int i, start, end, command;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  int c = readKey();

  switch (E.mode) {

    case INSERT:

      switch (c) {
    
        case '\r':
          editorCreateSnapshot();
          editorInsertReturn();
          break;
    
        /*
        case CTRL_KEY('s'):
          editorSave();
          break;
        */

        case HOME_KEY:
          editorMoveCursorBOL();
          break;
    
        case END_KEY:
          editorMoveCursorEOL();
          editorMoveCursor(ARROW_RIGHT);
          break;
    
        case BACKSPACE:
          editorCreateSnapshot();
          editorBackspace();
          break;
    
        case DEL_KEY:
          editorCreateSnapshot();
          editorDelChar();
          break;
    
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          editorMoveCursor(c);
          break;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);
          break;
    
        case CTRL_KEY('z'):
          E.smartindent = (E.smartindent == 1) ? 0 : 1;
          editorSetMessage("E.smartindent = %d", E.smartindent); 
          break;
    
        case '\x1b':
          E.mode = NORMAL;
          E.continuation = 0; // right now used by backspace in multi-line filerow
          if (E.fc > 0) E.fc--;
          // below - if the indent amount == size of line then it's all blanks
          if (E.row[E.fr].size) {
            int n = editorIndentAmount(E.fr);
            if (n == E.row[E.fr].size) {
              for (int i = 0; i < n; i++) {
                editorDelChar();
              }
            }
          }
          editorSetMessage("");
          return;
    
        default:
          editorCreateSnapshot();
          editorInsertChar(c);
          return;
     
      } //end inner switch for outer case NORMAL 

      return;

    case NORMAL: 
 
        if (c == '\x1b') {
          E.command[0] = '\0';
          E.repeat = 0;
          return;
        }  
      /*leading digit is a multiplier*/
      if (isdigit(c)) { //equiv to if (c > 47 && c < 58) 
        if ( E.repeat == 0 ){
    
          //if c=48=>0 then it falls through to move to beginning of line
          if ( c != 48 ) { 
            E.repeat = c - 48;
            return;
          }  
    
        } else { 
          E.repeat = E.repeat*10 + c - 48;
          return;
        }
      }
    
      if ( E.repeat == 0 ) E.repeat = 1;
    
      
      int n = strlen(E.command);
      E.command[n] = c;
      E.command[n+1] = '\0';
      command = (n && c < 128) ? keyfromstring(E.command) : c;
    
      switch (command) {
    
        case SHIFT_TAB:
          editor_mode = false;
          E.fc = E.fr = 0;
          return;
    
        case 'i':
          E.mode = INSERT;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 's':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelChar();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
          return;
    
        case 'x':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelChar();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
        
        case 'r':
          E.mode = 5;
          E.command[0] = '\0';
          return;
    
        case '~':
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorChangeCase();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'a':
          E.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
          editorMoveCursor(ARROW_RIGHT);
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'A':
          editorMoveCursorEOL();
          E.mode = 1; //needs to be here for movecursor to work at EOLs
          editorMoveCursor(ARROW_RIGHT);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'w':
          editorMoveNextWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'b':
          editorMoveBeginningWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'e':
          editorMoveEndWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '0':
          editorMoveCursorBOL();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '$':
          editorMoveCursorEOL();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case 'I':
          editorMoveCursorBOL();
          E.fc = editorIndentAmount(E.fr);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'o':
          editorCreateSnapshot();
          editorInsertNewline(1);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'O':
          editorCreateSnapshot();
          editorInsertNewline(0);
          E.mode = 1;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case 'G':
          E.fc = 0;
          E.fr = E.numrows - 1;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
      
        case ':':
          E.mode = COMMAND_LINE;
          E.command[0] = ':';
          E.command[1] = '\0';
          editorSetMessage(":"); 
          return;
    
        case 'V':
          E.mode = VISUAL_LINE;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fr;
          editorSetMessage("\x1b[1m-- VISUAL LINE --\x1b[0m");
          return;
    
        case 'v':
          E.mode = VISUAL;
          E.command[0] = '\0';
          E.repeat = 0;
          E.highlight[0] = E.highlight[1] = E.fc;
          editorSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
          return;
    
        case 'p':  
          editorCreateSnapshot();
          if (strlen(string_buffer)) editorPasteString();
          else editorPasteLine();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case '*':  
          getWordUnderCursor();
          editorFindNextWord(); 
          E.command[0] = '\0';
          return;
    
        case 'n':
          editorFindNextWord();
          E.command[0] = '\0';
          return;
    
        case 'u':
          editorRestoreSnapshot();
          return;
    
        case '^':
        ;
          orow *outline_row = &O.row[O.fr];
          view_html(outline_row->id);

          /*
          not getting error messages with qutebrowser
          so below not necessary (for the moment)
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          outlineRefreshScreen();
          editorRefreshScreen();
          */

          outlineRefreshScreen(); //to get outline message updated (could just update that last row??)
          O.command[0] = '\0';
          return;

        case CTRL_KEY('z'):
          E.smartindent = (E.smartindent == 4) ? 0 : 4;
          editorSetMessage("E.smartindent = %d", E.smartindent); 
          return;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateWord(c);
          return;

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            E.fr -= E.screenlines;
            if (E.fr < 0) E.fr = 0;
          } else if (c == PAGE_DOWN) {
            E.fr += E.screenlines;
            if (E.fr > E.numrows - 1) E.fr = E.numrows - 1;
          }
          return;
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          E.repeat = 0;
          return;
    
        // for testing purposes I am using CTRL-h in normal mode
        case CTRL_KEY('h'):
          editorMarkupLink(); 
          return;
    
        case C_daw:
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelWord();
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_dw:
          editorCreateSnapshot();
          for (int j = 0; j < E.repeat; j++) {
            start = E.fc;
            editorMoveEndWord2();
            end = E.fc;
            E.fc = start;
            for (int j = 0; j < end - start + 2; j++) editorDelChar();
          }
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_de:
          editorCreateSnapshot();
          start = E.fc;
          editorMoveEndWord(); //correct one to use to emulate vim
          end = E.fc;
          E.fc = start; 
          for (int j = 0; j < end - start + 1; j++) editorDelChar();
          E.fc = (start < E.row[E.cy].size) ? start : E.row[E.cy].size -1;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_dd:
          ;
          int fr = E.fr;
          if (E.numrows != 0) {
            int r = E.numrows - fr;
            E.repeat = (r >= E.repeat) ? E.repeat : r ;
            editorCreateSnapshot();
            editorYankLine(E.repeat);
            for (int i = 0; i < E.repeat ; i++) editorDelRow(fr);
          }
          E.fc = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_d$:
          editorCreateSnapshot();
          editorDeleteToEndOfLine();
          if (E.numrows != 0) {
            int r = E.numrows - E.fr;
            E.repeat--;
            E.repeat = (r >= E.repeat) ? E.repeat : r ;
            //editorYankLine(E.repeat); //b/o 2 step won't really work right
            for (int i = 0; i < E.repeat ; i++) editorDelRow(E.fr);
          }
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        //tested with repeat on one line
        case C_cw:
          editorCreateSnapshot();
          for (int j = 0; j < E.repeat; j++) {
            start = E.fc;
            editorMoveEndWord();
            end = E.fc;
            E.fc = start;
            for (int j = 0; j < end - start + 1; j++) editorDelChar();
          }
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        //tested with repeat on one line
        case C_caw:
          editorCreateSnapshot();
          for (int i = 0; i < E.repeat; i++) editorDelWord();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 1;
          editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;
    
        case C_indent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) { //i defined earlier - need outside block
            editorIndentRow();
            E.cy++;}
          E.cy-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_unindent:
          editorCreateSnapshot();
          for ( i = 0; i < E.repeat; i++ ) { //i defined earlier - need outside block
            editorUnIndentRow();
            E.cy++;}
          E.cy-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          return;
    
        case C_gg:
         E.fc = E.line_offset = 0;
         E.fr = E.repeat-1;
         E.command[0] = '\0';
         E.repeat = 0;
         return;
    
       case C_yy:  
         editorYankLine(E.repeat);
         E.command[0] = '\0';
         E.repeat = 0;
         return;
    
       default:
          // if a single char or sequence of chars doesn't match then
          // do nothing - the next char may generate a match
          return;
    
      } // end of keyfromstring switch under case NORMAL 

      return; // end of case NORMAL (don't think it can be reached)

    case COMMAND_LINE:

      switch (c) {

        case '\x1b':

          E.mode = 0;
          E.command[0] = '\0';
          editorSetMessage(""); 

          return;
  
        case '\r':
          // probably easier to maintain if this was same as case '\r'
          // in outline mode/COMMAND_LINE mode/case '\r'
          // but with only single char commands except q!
          // probably not worth it
          switch (E.command[1]) {

            case 'w':
              (*update_note)();
              E.mode = NORMAL;
              E.command[0] = '\0';
              editorSetMessage("");
              editorRefreshScreen();

              //The below needs to be in a function that takes the color as a parameter
              {
              char buf[32];
              write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
          
              for (int k=OUTLINE_LEFT_MARGIN+O.screencols+1; k < screencols ;k++) {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
              }
              snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, O.screencols + OUTLINE_LEFT_MARGIN + 1);
              write(STDOUT_FILENO, buf, strlen(buf));
              write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner
              write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
              write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
              }
              return;
  
            case 'x':
              (*update_note)();
              E.mode = NORMAL;
              E.command[0] = '\0';
              editor_mode = false;
              editorSetMessage("");
              editorRefreshScreen();

              //The below needs to be in a function that takes the color as a parameter
              {
              char buf[32];
              write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
          
              for (int k=OUTLINE_LEFT_MARGIN+O.screencols+1; k < screencols ;k++) {
                snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
                write(STDOUT_FILENO, buf, strlen(buf));
                write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
              }
              snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, O.screencols + OUTLINE_LEFT_MARGIN + 1);
              write(STDOUT_FILENO, buf, strlen(buf));
              write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner
              write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
              write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
              }
              return;
  
            case 'q':
              if (E.dirty) {
                if (strlen(E.command) == 3 && E.command[2] == '!') {
                  E.mode = NORMAL;
                  E.command[0] = '\0';
                  editor_mode = false;
                } else {
                  E.mode = 0;
                  E.command[0] = '\0';
                  editorSetMessage("No write since last change");
                }
              } else {
                editorSetMessage("");
                editor_mode = false;
              }
              editorRefreshScreen();
              return;

          } // end of case '\r' switch
     
          return;
  
        default:
          ;
          int n = strlen(E.command);
          if (c == DEL_KEY || c == BACKSPACE) {
            E.command[n-1] = '\0';
          } else {
            E.command[n] = c;
            E.command[n+1] = '\0';
          }

          editorSetMessage(E.command);
      } 
  
      return;

    case VISUAL_LINE:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.highlight[1] = E.fr;
          return;
    
        case 'x':
          if (E.numrows != 0) {
            editorCreateSnapshot();
            E.repeat = E.highlight[1] - E.highlight[0] + 1;
            E.fr = E.highlight[0]; 
            editorYankLine(E.repeat);
    
            for (int i = 0; i < E.repeat; i++) editorDelRow(E.highlight[0]);
          }
          E.fc = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case 'y':  
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          editorYankLine(E.repeat);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case '>':
          editorCreateSnapshot();
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fr = E.highlight[0];
          for ( i = 0; i < E.repeat; i++ ) {
            editorIndentRow();
            E.fr++;}
          E.fr-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case '<':
          editorCreateSnapshot();
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.cy = E.highlight[0];
          for ( i = 0; i < E.repeat; i++ ) {
            editorUnIndentRow();
            E.cy++;}
          E.cy-=i;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return;
    
        default:
          return;
      }

      return;

    case VISUAL:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          editorMoveCursor(c);
          E.highlight[1] = E.fc;
          return;
    
        case 'x':
          editorCreateSnapshot();
          E.repeat = abs(E.highlight[1] - E.highlight[0]) + 1;
          //editorYankString();  /// *** causing segfault

          // the delete below requires positioning the cursor
          E.fc = (E.highlight[1] > E.highlight[0]) ? E.highlight[0] : E.highlight[1];
    
          for (int i = 0; i < E.repeat; i++) {
            editorDelChar2(E.fr, E.fc);
          }
          if (E.fc) E.fc--;
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case 'y':  
          E.repeat = E.highlight[1] - E.highlight[0] + 1;
          E.fc = E.highlight[0];
          editorYankString();
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          editorCreateSnapshot();
          editorDecorateVisual(c);
          E.command[0] = '\0';
          E.repeat = 0;
          E.mode = 0;
          editorSetMessage("");
          return;
    
        case '\x1b':
          E.mode = 0;
          E.command[0] = '\0';
          E.repeat = 0;
          editorSetMessage("");
          return;
    
        default:
          return;
      }
    
      return;

    case REPLACE:

      editorCreateSnapshot();
      for (int i = 0; i < E.repeat; i++) {
        editorDelChar();
        editorInsertChar(c);
      }
      E.repeat = 0;
      E.command[0] = '\0';
      E.mode = 0;
      return;

  }  // End of main switch that deals with modes like NORMAL, INSERT etc.
} 

/********************************************************** WW stuff *****************************************/
// used by editorDrawRows to figure out the first row to draw
int editorGetFileRowByLineWW(int y){
  /*
  y is the actual screenline (E.cy)
  the display may be scrolled so below add offset
  */

  int screenrow = -1;
  int n = 0;
  int linerows;

  y+= E.line_offset; 

  if (y == 0) return 0;
  for (;;) {
    linerows = editorGetLinesInRowWW(n);
    screenrow+= linerows;
    if (screenrow >= y) break;
    n++;
  }
  return n;
}

/****************************ESSENTIAL*****************************/
int editorGetLinesInRowWW(int r) {
  erow *row = &E.row[r];
  char *start,*right_margin;
  int left, width, num;  //, len;
  bool more_lines = true;

  left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  num = 0;
  while(more_lines) { 

    if(left <= width) { //after creating whatever number of lines if remainer <= width: get out
      more_lines = false;
      num++; 
          
    } else {
      right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
      while(!isspace(*right_margin)) { 
        right_margin--;
        if(right_margin == start) { // situation in which there were no spaces to break the link
          right_margin += width - 1;
          break; 
        }    
      } 
      left -= right_margin-start+1;      /* +1 for the space */
      start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
      num++;
    }
  }
  return num;
}
/****************************ESSENTIAL (above) *****************************/

// right now only in use for debugging
int editorGetLineInRowWW(void) {
  int screenrow = -1;
  int linerows;
  int r = 0;
  int y = E.cy + E.line_offset; 

  if (y == 0) return 1;

  for (;;) {
    linerows = editorGetLinesInRowWW(r);
    screenrow += linerows;
    if (screenrow >= y) break;
    r++;
  }

  return linerows - screenrow + y;

}
// returns row, line in row and column
// now only useful (possibly) for debugging
int *editorGetRowLineCharWW(void) {
  int screenrow = -1;
  int r = 0;

  //if not use static then it's a variable local to function
  static int row_line_char[3];

  int linerows;
  int y = E.cy + E.line_offset; 
  if (y == 0) {
    row_line_char[0] = 0;
    row_line_char[1] = 1;
    row_line_char[2] = E.cx;
    return row_line_char;
  }
  for (;;) {
    linerows = editorGetLinesInRowWW(r);
    screenrow += linerows;
    if (screenrow >= y) break;
    r++;
  }

  // right now this is necesssary for backspacing in a multiline filerow
  // no longer seems necessary for insertchar
  if (E.continuation) r--;

  row_line_char[0] = r;
  row_line_char[1] = linerows - screenrow + y;
  row_line_char[2] = editorGetCharInRowWW(r, linerows - screenrow + y);

  return row_line_char;
}

int editorGetCharInRowWW(int rsr, int line) {
  erow *row = &E.row[rsr];
  char *start,*right_margin;
  int left, width, num, len, length;

  left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;
  num = 1; ////// Don't see how this works for line = 1
  for (;;) {
    if (left <= width) return length + E.cx; /////////////////////////////////////////////////////////////////////////////02182019 9:41 am 
    right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= right_margin-start+1;      // +1 for the space //
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    if (num == line) break;
    num++;
    length += len;
  }
  return length + E.cx;
}

int editorGetLineCharCountWW(int rsr, int line) {

  erow *row = &E.row[rsr];
  if (row->size == 0) return 0;

  char *start,*right_margin;
  int width, num, len, left;  //length left

  left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  //length = 0;

  if (row->size == 0) return 0;

  num = 1; ////// Don't see how this works for line = 1
  for (;;) {
    if (left <= width) return left; // <
    right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= right_margin - start+1;      // +1 for the space //
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    //length += len;
    if (num == line) break;
    num++;
    //length += len;
  }
  return len;
}

int editorGetScreenXFromRowCharPosWW(int r, int c) {

  erow *row = &E.row[r];
  char *start,*right_margin;
  int width, len, left, length;  //num 

  left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;

  if (row->size == 0) return 0;

  //num = 1; ////// Don't see how this works for line = 1
  for (;;) {
    if (left < width) {
      length += left;
      len = left;
      break;
    }
    right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= len;
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    length += len;
    if (c < length) break; //<= segfaults with either
    //num++;
    //length += len;
  }
  return c - length + len;
}

// debugging
int *editorGetRowLineScreenXFromRowCharPosWW(int r, int c) {

  static int rowline_screenx[2]; //if not use static then it's a variable local to function
  erow *row = &E.row[r];
  char *start,*right_margin;
  int width, len, left, length, num; //, prev_length;  

  left = row->size; //although maybe time to use strlen(preamble); //not fixed -- this is decremented as each line is created
  start = row->chars; //char * to string that is going to be wrapped ? better named remainder?
  width = E.screencols; //wrapping width
  
  length = 0;

  // not 100% sure where this if should be maybe editorScroll /********************************************/
  if (row->size == 0) {
    //E.fc = -1;
    E.fc = 0;
    rowline_screenx[1] = 0;
    rowline_screenx[0] = 1;
    return rowline_screenx;
  } //else if (c == -1 || E.fc == -1) E.fc = c = 0;

 
  num = 1; 
  for (;;) {
    if (left < width + 1) { //// didn't have the + 1 and + 1 seems better 02182019
      length += left;
      len = left;
      break;
    }
    right_margin = start+width - 1; //each time start pointer moves you are adding the width to it and checking for spaces
    while(!isspace(*right_margin)) { //#2
      right_margin--;
      if( right_margin == start) { // situation in which there were no spaces to break the link
        right_margin += width - 1;
        break; 
      }    
    } 
    len = right_margin - start + 1;
    left -= right_margin - start+1;      // +1 for the space //
    start = right_margin + 1; //move the start pointer to the beginning of what will be the next line
    length += len;
    if (c < length) break; // changing from <= to < fixed a problem in editorMoveNextWork - no idea if it introduced new issues !! 02182019
    num++;
  }
  rowline_screenx[1] = c - length + len;
  rowline_screenx[0] = num;
  return rowline_screenx;
}

/*********************************ESENTIAL*****************************/
int *editorGetScreenPosFromRowCharPosWW(int r, int c) { //, int fc){
  static int screeny_screenx[2]; //if not use static then it's a variable local to function
  int screenline = 0;
  int n = 0;

  for (n=0; n < r; n++) { 
    screenline+= editorGetLinesInRowWW(n);
  }

  screenline -= E.line_offset;
  // below seems like a total kluge and (barely tested) but actually seems to work
  //- ? should be in editorScroll - I did try to put a version in editorScroll but
  // it didn't work and I didn't investigate why so here it will remain at least  for the moment
  if (screenline<=0 && r==0) {
    E.line_offset = 0; 
    screenline = 0;
    }
  // since E.cx should be less than E.row[].size (since E.cx counts from zero and E.row[].size from 1
  // this can put E.cx one farther right than it should be but editorMoveCursor checks and moves it back if not in insert mode
  int *rowline_screenx = editorGetRowLineScreenXFromRowCharPosWW(r, c);
  screeny_screenx[0] = screenline + rowline_screenx[0] - 1; //new -1
  screeny_screenx[1] = rowline_screenx[1];
  return screeny_screenx;
}
/************************************* ESSENTIAL (above)  ************************************************/
/************************************* end of WW ************************************************/

void editorCreateSnapshot(void) {
  if ( E.numrows == 0 ) return; //don't create snapshot if there is no text
  for (int j = 0 ; j < E.prev_numrows ; j++ ) {
    free(E.prev_row[j].chars);
  }
  E.prev_row = realloc(E.prev_row, sizeof(erow) * E.numrows );
  for ( int i = 0 ; i < E.numrows ; i++ ) {
    int len = E.row[i].size;
    E.prev_row[i].chars = malloc(len + 1);
    E.prev_row[i].size = len;
    memcpy(E.prev_row[i].chars, E.row[i].chars, len);
    E.prev_row[i].chars[len] = '\0';
  }
  E.prev_numrows = E.numrows;
}

void editorRestoreSnapshot(void) {
  for (int j = 0 ; j < E.numrows ; j++ ) {
    free(E.row[j].chars);
  } 
  E.row = realloc(E.row, sizeof(erow) * E.prev_numrows );
  for (int i = 0 ; i < E.prev_numrows ; i++ ) {
    int len = E.prev_row[i].size;
    E.row[i].chars = malloc(len + 1);
    E.row[i].size = len;
    memcpy(E.row[i].chars, E.prev_row[i].chars, len);
    E.row[i].chars[len] = '\0';
  }
  E.numrows = E.prev_numrows;
}

void editorChangeCase(void) {
  erow *row = &E.row[E.fr];
  char d = row->chars[E.fc];
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    editorMoveCursor(ARROW_RIGHT);
    return;
  }
  editorDelChar();
  editorInsertChar(d);
}

void editorYankLine(int n){
  for (int i=0; i < 10; i++) {
    free(line_buffer[i]);
    line_buffer[i] = NULL;
    }


  for (int i=0; i < n; i++) {
    int len = E.row[E.fr + i].size;
    line_buffer[i] = malloc(len + 1);
    memcpy(line_buffer[i], E.row[E.fr + i].chars, len);
    line_buffer[i][len] = '\0';
  }
  // set string_buffer to "" to signal should paste line and not chars
  string_buffer[0] = '\0';
}

void editorYankString(void) {
  // doesn't cross rows right now
  int n,x;

  erow *row = &E.row[E.fr];
  for (x = E.highlight[0], n = 0; x < E.highlight[1]+1; x++, n++) {
      string_buffer[n] = row->chars[x];
  }

  string_buffer[n] = '\0';
}

void editorPasteString(void) {

  erow *row = &E.row[E.fr];
  int len = strlen(string_buffer);

  row->chars = realloc(row->chars, row->size + len); 

  /* moving all the chars at the current x cursor position on char
     farther down the char string to make room for the new character
     Maybe a clue from editorInsertRow - it's memmove is below
     memmove(&E.row[r + 1], &E.row[r], sizeof(erow) * (E.numrows - r));
  */

  memmove(&row->chars[E.fc + len], &row->chars[E.fc], row->size - E.fc);

  for (int i = 0; i < len; i++) {
    row->size++;
    row->chars[E.fc] = string_buffer[i];
    E.fc++;
  }

  E.dirty++;
}

void editorPasteLine(void){
  if (E.numrows == 0) editorInsertRow(0, "", 0);

  for (int i=0; i < 10; i++) {
    if (line_buffer[i] == NULL) break;

    int len = strlen(line_buffer[i]);
    E.fr++;
    editorInsertRow(E.fr, line_buffer[i], len);
  }
}

void editorIndentRow(void) {

  erow *row = &E.row[E.fr];
  if (row->size == 0) return;
  E.fc = editorIndentAmount(E.fr);
  for (int i = 0; i < E.indent; i++) editorInsertChar(' ');
  E.dirty++;
}

void editorUnIndentRow(void) {

  erow *row = &E.row[E.fr];
  if (row->size == 0) return;
  E.fc = 0;
  for (int i = 0; i < E.indent; i++) {
    if (row->chars[0] == ' ') {
      editorDelChar();
    }
  }
  E.dirty++;
}

int editorIndentAmount(int r) {
  int i;

  if (E.row == NULL) return 0;

  erow *row = &E.row[r];
  //if (row->size == 0) return 0; //below should catch this

  for ( i = 0; i < row->size; i++) {
    if (row->chars[i] != ' ') break;}

  return i;
}

// called by caw and daw
void editorDelWord(void) {

  erow *row = &E.row[E.fr];
  if (row->chars[E.fc] < 48) return;

  int i,j,x;
  for (i = E.fc; i > -1; i--){
    if (row->chars[i] < 48) break;
    }
  for (j = E.fc; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }

  for (x = 0 ; x < j-i; x++) {
      editorDelChar();
  }
  E.dirty++;
  //editorSetMessage("i = %d, j = %d", i, j ); 
}

void editorDeleteToEndOfLine(void) {

  erow *row = &E.row[E.fr];
  row->size = E.fc;

  row->chars = realloc(row->chars, E.fc + 1); 
  row->chars[E.fc] = '\0';
}

void editorMoveCursorBOL(void) {
  if (E.row == NULL) return;
  E.fc = 0;
}

void editorMoveCursorEOL(void) {

  erow row = E.row[E.fr];
  if (row.size) E.fc = row.size - 1;
}

// not same as 'e' but moves to end of word or stays put if already
//on end of word - used by dw
void editorMoveEndWord2() {
  int j;

  erow row = E.row[E.fr];

  for (j = E.fc + 1; j < row.size ; j++) {
    if (row.chars[j] < 48) break;
  }

  E.fc = j - 1;

}

// used by 'w'
void editorMoveNextWord(void) {
// doesn't handle multiple white space characters at EOL
  int i,j;

  erow row = E.row[E.fr];

  if (row.chars[E.fc] < 48) j = E.fc;
  else {
    for (j = E.fc + 1; j < row.size; j++) { 
      if (row.chars[j] < 48) break;
    }
  } 

  if (j >= row.size - 1) { // at end of line was ==

    if (E.fr == E.numrows - 1) return; // no more rows
    
    for (;;) {
      E.fr++;
      row = E.row[E.fr];
      if (row.size == 0 && E.fr == E.numrows - 1) return;
      if (row.size) break;
      }
  
    if (row.chars[0] > 47) {
      E.fc = 0;
      return;
    } else {
      for (j = E.fc + 1; j < row.size; j++) { 
        if (row.chars[j] < 48) break;
      }
    }
  }

  for (i = j + 1; j < row.size ; i++) { //+1
    if (row.chars[i] > 48) {
      E.fc = i;
      break;
    }
  }
}

// normal mode 'b'
void editorMoveBeginningWord(void) {

  erow *row = &E.row[E.fr];
  int j = E.fc;

  if (E.fc == 0 || (E.fc == 1 && row->chars[0] < 48)) { 
    if (E.fr == 0) return;
    for (;;) {
      E.fr--;
      row = &E.row[E.fr];
      if (E.fr == 0 && row->size == 0) return;
      if (row->size == 0) continue;
      if (row->size) {
        j = row->size - 1;
        break;
      }
    } 
  }

  for (;;) {
    if (j > 1 && row->chars[j - 1] < 48) j--;
    else break;
  }

  int i;
  for (i = j - 1; i > -1; i--){
    if (i == 0) { 
      if (row->chars[0] > 47) { 
        E.fc = 0;
        break;
      } else return;
    }
    if (row->chars[i] < 48) {
      E.fc = i + 1;
      break;
    }
  }
}

// normal mode 'e' - seems to handle all corner cases
void editorMoveEndWord(void) {

  erow *row = &E.row[E.fr];

  int j = (row->chars[E.fc + 1] < 48) ? E.fc + 1 : E.fc;

  for(;;) {

    j++;

    if (j > row->size - 1) { //>=

      for (;;) {
        if (E.fr == E.numrows - 1) return; // no more rows
        E.fr++;
        row = &E.row[E.fr];
        if (row->size == 0 && E.fr == E.numrows - 1) return;
        if (row->size) {
          j = 0;
          break;
        }
      }
    }
    if (j == row->size - 1) {
      E.fc = j;
      break;
    }
    if (row->chars[j] < 48 && (j < row->size - 1) && row->chars[j - 1] > 48) {
      E.fc = j-1;
      break;
    }
 
  }
}

void editorDecorateWord(int c) {

  erow *row = &E.row[E.fr];
  char cc;
  if (row->chars[E.fc] < 48) return;

  int i, j;

  /*Note to catch ` would have to be row->chars[i] < 48 || row-chars[i] == 96 - may not be worth it*/

  for (i = E.fc - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  for (j = E.fc + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }
  
  if (row->chars[i] != '*' && row->chars[i] != '`'){
    cc = (c == CTRL_KEY('b') || c ==CTRL_KEY('i')) ? '*' : '`';
    E.fc = i + 1;
    editorInsertChar(cc);
    E.fc = j + 1;
    editorInsertChar(cc);

    if (c == CTRL_KEY('b')) {
      E.fc = i + 1;
      editorInsertChar('*');
      E.fc = j + 2;
      editorInsertChar('*');
    }
  } else {
    E.fc = i;
    editorDelChar();
    E.fc = j - 1;
    editorDelChar();

    if (c == CTRL_KEY('b')) {
      E.fc = i - 1;
      editorDelChar();
      E.fc = j - 2;
      editorDelChar();
    }
  }
}

void editorDecorateVisual(int c) {
    E.fc = E.highlight[0];
  if (c == CTRL_KEY('b')) {
    editorInsertChar('*');
    editorInsertChar('*');
    E.fc = E.highlight[1]+3;
    editorInsertChar('*');
    editorInsertChar('*');
  } else {
    char cc = (c ==CTRL_KEY('i')) ? '*' : '`';
    editorInsertChar(cc);
    E.fc = E.highlight[1]+2;
    editorInsertChar(cc);
  }
}

void getWordUnderCursor(void){

  erow *row = &E.row[E.fr];
  if (row->chars[E.fc] < 48) return;

  int i,j,n,x;

  for (i = E.fc - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  for (j = E.fc + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }

  for (x = i + 1, n = 0; x < j; x++, n++) {
      search_string[n] = row->chars[x];
  }

  search_string[n] = '\0';
  editorSetMessage("word under cursor: <%s>", search_string); 

}

// needs a little work and needs to wrap back on itself something odd about wrapping matches
void editorFindNextWord(void) {
  int y, x;
  char *z;

  y = E.fr;
  x = E.fc + 10;
  //x = E.fc;
  erow *row;
 
  /*n counter so we can exit for loop if there are  no matches for command 'n'*/
  for (;;) {
    row = &E.row[y];
    z = strstr(&(row->chars[x]), search_string);
    if (z != NULL) break;
    y++;
    x = 0;
    if (y == E.numrows) y = 0;
  }

  E.fc = z - row->chars;
  E.fr = y;

  editorSetMessage("x = %d; y = %d", x, y); 
}

void editorMarkupLink(void) {
  int y, numrows, j, n, p;
  char *z;
  char *http = "http";
  char *bracket_http = "[http";
  numrows = E.numrows;
  n = 1;


  for ( n=1; E.row[numrows-n].chars[0] == '[' ; n++ );


  for (y=0; y<numrows; y++){
    erow *row = &E.row[y];
    if (row->chars[0] == '[') continue;
    if (strstr(row->chars, bracket_http)) continue;

    z = strstr(row->chars, http);
    if (z==NULL) continue;
    E.fr = y;
    p = z - row->chars;

    //url including http:// must be at least 10 chars you'd think
    for (j = p + 10; j < row->size ; j++) { 
      if (row->chars[j] == 32) break;
    }

    int len = j-p;
    char *zz = malloc(len + 1);
    memcpy(zz, z, len);
    zz[len] = '\0';

    E.fc = p;
    editorInsertChar('[');
    E.fc = j+1;
    editorInsertChar(']');
    editorInsertChar('[');
    editorInsertChar(48+n);
    editorInsertChar(']');

    if ( E.row[numrows-1].chars[0] != '[' ) {
      E.fr = E.numrows - 1; //check why need - 1 otherwise seg faults
      E.fc = 0;
      editorInsertNewline(1);
      }

    editorInsertRow(E.numrows, zz, len); 
    free(zz);
    E.fc = 0;
    E.fr = E.numrows - 1;
    editorInsertChar('[');
    editorInsertChar(48+n);
    editorInsertChar(']');
    editorInsertChar(':');
    editorInsertChar(' ');
    editorSetMessage("z = %u and beginning position = %d and end = %d and %u", z, p, j,zz); 
    n++;
  }
  E.dirty++;
}

/* below was used for testing
void getcharundercursor(void) {
  erow *row = &E.row[E.cy];
  char d = row->chars[E.cx];
  editorSetMessage("character under cursor at position %d of %d: %c", E.cx, row->size, d); 
}
*/

/*** init ***/

void initOutline() {
  O.cx = 0; //cursor x position
  O.cy = 0; //cursor y position
  O.fc = 0; //file x position
  O.fr = 0; //file y position
  O.rowoff = 0;  //number of rows scrolled off the screen
  O.coloff = 0;  //col the user is currently scrolled to  
  O.numrows = 0; //number of rows of text
  O.row = NULL; //pointer to the orow structure 'array'
  O.context = "todo"; 
  O.show_deleted = false;
  O.show_completed = true;
  O.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  O.highlight[0] = O.highlight[1] = -1;
  O.mode = NORMAL; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  O.command[0] = '\0';
  O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  if (getWindowSize(&screenlines, &screencols) == -1) die("getWindowSize");
  O.screenlines = screenlines - 2 - TOP_MARGIN;
  O.screencols = -3 + screencols/2; //this can be whatever you want but will affect note editor
}

void initEditor(void) {
  E.cx = 0; //cursor x position
  E.cy = 0; //cursor y position
  E.fc = 0; //file x position
  E.fr = 0; //file y position
  E.line_offset = 0;  //the number of lines of text at the top scrolled off the screen
  //E.coloff = 0;  //should always be zero because of line wrap
  E.numrows = 0; //number of rows (lines) of text delineated by a return
  E.row = NULL; //pointer to the erow structure 'array'
  E.prev_numrows = 0; //number of rows of text in snapshot
  E.prev_row = NULL; //prev_row is pointer to snapshot for undoing
  E.dirty = 0; //has filed changed since last save
  E.filename = NULL;
  E.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  E.highlight[0] = E.highlight[1] = -1;
  E.mode = 0; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  E.command[0] = '\0';
  E.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y
  E.indent = 4;
  E.smartindent = 1; //CTRL-z toggles - don't want on what pasting from outside source
  E.continuation = 0; //circumstance when a line wraps

  if (getWindowSize(&E.screenlines, &E.screencols) == -1) die("getWindowSize");
  E.screenlines = screenlines - 2 - TOP_MARGIN;
  E.screencols = -2 + screencols/2;
  EDITOR_LEFT_MARGIN = screencols/2 + 1;
}

int main(int argc, char** argv) { 

  if (argc > 1 && argv[1][0] == 's') {
    get_data = &get_data_sqlite;
    get_solr_data = &get_solr_data_sqlite;
    get_note = &get_note_sqlite;
    update_note = &update_note_sqlite;
    toggle_star = &toggle_star_sqlite;
    toggle_completed = &toggle_completed_sqlite;
    toggle_deleted = &toggle_deleted_sqlite;
    insert_row = &insert_row_sqlite;
    update_rows = &update_rows_sqlite;
    update_row = &update_row_sqlite;
    update_context = &update_context_sqlite;
    display_item_info = &display_item_info_sqlite;
    which_db = "sqlite";
  } else {
    get_conn();
    get_data = &get_data_pg;
    get_solr_data = &get_solr_data_pg;
    get_note = &get_note_pg;
    update_note = &update_note_pg;
    toggle_star = &toggle_star_pg;
    toggle_completed = &toggle_completed_pg;
    toggle_deleted = &toggle_deleted_pg;
    insert_row = &insert_row_pg;
    update_rows = &update_rows_pg;
    update_row = &update_row_pg;
    update_context = &update_context_pg;
    display_item_info = &display_item_info_pg;
    which_db = "postgres";
  }

  int j;
  enableRawMode();
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  initOutline();
  initEditor();
  int pos = screencols/2;
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (j=1; j < screenlines + 1;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, pos);
    write(STDOUT_FILENO, buf, strlen(buf));
    //write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
    //below x = 0x78 vertical line and q = 0x71 is horizontal
    write(STDOUT_FILENO, "\x1b[37;1mx", 8); //31 = red; 37 = white; 1m = bold (only need last 'm')
}

  for (j=1; j < O.screencols + OUTLINE_LEFT_MARGIN + 1;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, j);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line q = 0x71
  }

  //write(STDOUT_FILENO, "\x1b[37;1mk", 8); //corner
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  for (int k=j+1; k < screencols ;k++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, k);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
  }


  write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
  write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode

  (*get_data)(O.context, MAX); //? brings back deleted/completed-type data
  // I need to look at below when incorrect queries were bringing back nother
  // without the guard segfault and even now searches could bring back nothing
  if (O.row)
  (*get_note)(O.row[0].id);
   //editorRefreshScreen(); //in get_note
  
 // PQfinish(conn); // this should happen when exiting

  O.fc = O.fr = O.rowoff = 0; 
  //outlineSetMessage("HELP: Ctrl-S = save | Ctrl-Q = quit"); //slz commented this out
  outlineSetMessage("rows: %d  cols: %d orow size: %d int: %d char*: %d bool: %d", O.screenlines, O.screencols, sizeof(orow), sizeof(int), sizeof(char*), sizeof(bool)); //for display screen dimens

  // putting this here seems to speed up first search but still slow
  // might make sense to do the module imports here too
  // assume the reimports are essentially no-ops
  //Py_Initialize(); 

  while (1) {
    if (editor_mode){
      editorScroll();
      editorRefreshScreen();
      editorProcessKeypress();
    } else {
      outlineScroll();
      outlineRefreshScreen();
      outlineProcessKeypress();
    }
  }
  return 0;
}
