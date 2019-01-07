/***  includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#define KILO_QUIT_TIMES 1
#define CTRL_KEY(k) ((k) & 0x1f)
#define OUTLINE 0
#define EDIT 1

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

/*** defines ***/

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

PGresult *get_data(char *context, int n) {
  //PGresult *res = PQexec(conn, "SELECT * FROM task LIMIT 5");//<-this works    
  char query[200];
  //sprintf(query, "SELECT * FROM task LIMIT %d", n); //<-this works
  //sprintf(query, "UPDATE task SET title=\'%s\', "
  sprintf(query, "SELECT * FROM task JOIN context ON context.id = task.context_tid "
                    //"WHERE context.title = 'test' LIMIT %d", n);
                    //"WHERE context.title = 'test' ORDER BY task.modified DESC LIMIT %d", n);
                    "WHERE context.title = \'%s\' ORDER BY task.modified DESC LIMIT %d", context, n);

  PGresult *res = PQexec(conn, query);    
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {

    printf("No data retrieved\n");        
    PQclear(res);
    do_exit(conn);
  }    

  return res;
}

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
  PAGE_DOWN
};

enum Command {
  C_caw,
  C_cw,
  C_daw,
  C_dw,
  C_de,
  C_d$,
  C_dd,
  C_c$,
  C_gg,
  C_yy
};

/*** data ***/

typedef struct erow {
  int size; //the number of characters in the line
  char *chars; //points at the character array of a row - mem assigned by malloc

  int id; //listmanager db id of the row
  bool star;
  bool deleted;
  bool completed;
  bool dirty;
  
} erow;

struct termios orig_termios;

int screenrows, screencols;

struct outlineConfig {
  int cx, cy; //cursor x and y position
  int rx; //index into the render field - only nec b/o tabs
  int rowoff; //row the user is currently scrolled to
  int coloff; //column user is currently scrolled to
  int screenrows; //number of rows in the display
  int screencols;  //number of columns in the display
  int numrows; // the number of rows of text so last text row is always row numrows
  erow *row; //(e)ditorrow stores a pointer to a contiguous collection of erow structures 
  int prev_numrows; // the number of rows of text so last text row is always row numrows
  erow *prev_row; //for undo purposes
  int dirty; //file changes since last save
  char *context;
  char statusmsg[80]; //status msg is a character array max 80 char
  //time_t statusmsg_time;
  //struct termios orig_termios;
  int highlight[2];
  int mode;
  char command[20]; //needs to accomodate file name ?malloc heap array
  int repeat;
};

struct outlineConfig O;

char search_string[30] = {'\0'}; //used for '*' and 'n' searches

// buffers below for yanking
char *line_buffer[20] = {NULL}; //yanking lines
char string_buffer[50] = {'\0'}; //yanking chars

/*below is for multi-character commands*/
typedef struct { char *key; int val; } t_symstruct;
static t_symstruct lookuptable[] = {
  {"caw", C_caw},
  {"cw", C_cw},
  {"daw", C_daw},
  {"dw", C_dw},
  {"de", C_de},
  {"dd", C_dd},
  {"gg", C_gg},
  {"yy", C_yy},
  {"d$", C_d$}
};

#define NKEYS ((int) (sizeof(lookuptable)/sizeof(lookuptable[0])))

/*** prototypes ***/

void outlineSetMessage(const char *fmt, ...);
void outlineRefreshScreen();
void outlineRefreshLine();
void getcharundercursor();
void outlineDecorateWord(int c);
void outlineDecorateVisual(int c);
void outlineDelWord();
int outlineIndentAmount(int y);
void outlineMoveCursor(int key);
void outlineBackspace();
void outlineDelChar();
void outlineDeleteToEndOfLine();
void outlineYankLine(int n);
void outlinePasteLine();
void outlinePasteString();
void outlineYankString();
void outlineMoveCursorEOL();
void outlineMoveBeginningWord();
void outlineMoveEndWord(); 
void outlineMoveEndWord2(); //not 'e' but just moves to end of word even if on last letter
void outlineMoveNextWord();
void outlineMarkupLink();
void getWordUnderCursor();
void outlineFindNextWord();
void outlineChangeCase();
void outlineRestoreSnapshot(); 
void outlineCreateSnapshot(); 
int get_filerow(void);
int get_filecol(void);
int get_id(int fr);
void update_row(void);
void update_rows(void);

int keyfromstring(char *key)
{
    int i;
    for (i=0; i <  NKEYS; i++) {
        if (strcmp(lookuptable[i].key, key) == 0)
          return lookuptable[i].val;
    }

    //nothing should match -1
    return -1;
}
/*** terminal ***/

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

int outlineReadKey() {
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

/*** row operations ***/

//at is the row number of the row to insert
// not in use
void outlineInsertRow(int at, char *s, size_t len) {

  /*O.row is a pointer to an array of erow structures
  The array of erows that O.row points to needs to have its memory enlarged when
  you add a row. Note that erow structues are just a size and a char pointer*/

  O.row = realloc(O.row, sizeof(erow) * (O.numrows + 1));

  /*
  memmove(dest, source, number of bytes to move?)
  moves the line at at to at+1 and all the other erow structs until the end
  when you insert into the last row O.numrows==at then no memory is moved
  apparently ok if there is no O.row[at+1] if number of bytes = 0
  so below we are moving the row structure currently at *at* to x+1
  and all the rows below *at* to a new location to make room at *at*
  to create room for the line that we are inserting
  */

  memmove(&O.row[at + 1], &O.row[at], sizeof(erow) * (O.numrows - at));

  // section below creates an erow struct for the new row
  O.row[at].size = len;
  O.row[at].chars = malloc(len + 1);
  memcpy(O.row[at].chars, s, len);
  O.row[at].chars[len] = '\0'; //each line is made into a c-string (maybe for searching)
  O.numrows++;
  O.dirty++;
}

void outlineFreeRow(erow *row) {
  free(row->chars);
}

void outlineDelRow(int at) {
  //outlineSetMessage("Row to delete = %d; O.numrows = %d", at, O.numrows); 
  if (O.numrows == 0) return; // some calls may duplicate this guard
  outlineFreeRow(&O.row[at]);
  if ( O.numrows != 1) { 
    memmove(&O.row[at], &O.row[at + 1], sizeof(erow) * (O.numrows - at - 1));
  } else {
    O.row = NULL;
  }
  O.numrows--;
  if (O.cy == O.numrows && O.cy > 0) O.cy--; 
  O.dirty++;
  //outlineSetMessage("Row deleted = %d; O.numrows after deletion = %d O.cx = %d O.row[at].size = %d", at, O.numrows, O.cx, O.row[at].size); 
}

void outlineInsertRow2(int at, char *s, size_t len, int id, bool star, bool deleted, bool completed) {

  /*O.row is a pointer to an array of erow structures
  The array of erows that O.row points to needs to have its memory enlarged when
  you add a row. Note that erow structues are just a size and a char pointer*/

  O.row = realloc(O.row, sizeof(erow) * (O.numrows + 1));

  /*
  memmove(dest, source, number of bytes to move?)
  moves the line at at to at+1 and all the other erow structs until the end
  when you insert into the last row O.numrows==at then no memory is moved
  apparently ok if there is no O.row[at+1] if number of bytes = 0
  so below we are moving the row structure currently at *at* to x+1
  and all the rows below *at* to a new location to make room at *at*
  to create room for the line that we are inserting
  */

  memmove(&O.row[at + 1], &O.row[at], sizeof(erow) * (O.numrows - at));

  // section below creates an erow struct for the new row
  O.row[at].size = len;
  O.row[at].chars = malloc(len + 1);
  O.row[at].id = id;
  O.row[at].star = star;
  O.row[at].deleted = deleted;
  O.row[at].completed = completed;
  O.row[at].dirty = false;
  memcpy(O.row[at].chars, s, len);
  O.row[at].chars[len] = '\0'; //each line is made into a c-string (maybe for searching)
  O.numrows++;
  //O.dirty++;
}

void outlineRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  // is there any reason to realloc for one character?
  // row->chars = realloc(row->chars, row->size -1); 
  //have to realloc when adding but I guess no need to realloc for one character
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  O.dirty++;
}

/*** outline operations ***/
void outlineInsertChar(int c) {

  // O.cy == O.numrows == 0 when you start program or delete all lines
  if ( O.numrows == 0 ) {
    outlineInsertRow(0, "", 0); //outlineInsertRow will insert '\0'
  }

  //erow *row = &O.row[O.cy];
  erow *row = &O.row[O.cy+O.rowoff];
  int fc = get_filecol();
  //if (O.cx < 0 || O.cx > row->size) O.cx = row->size; //can either of these be true? ie is check necessary?
  row->chars = realloc(row->chars, row->size + 1); //******* was size + 2

  /* moving all the chars at the current x cursor position on char
     farther down the char string to make room for the new character
     Maybe a clue from outlineInsertRow - it's memmove is below
     memmove(&O.row[at + 1], &O.row[at], sizeof(erow) * (O.numrows - at));
  */

  //memmove(&row->chars[O.cx + 1], &row->chars[O.cx], row->size - O.cx); //****was O.cx + 1
  memmove(&row->chars[fc + 1], &row->chars[fc], row->size - fc); //****was O.cx + 1

  row->size++;
  row->chars[fc] = c;
  row->dirty = true;
  O.dirty++;
  O.cx++;
}

/* uses VLA */
void outlineInsertNewline(int direction) {
  if (O.numrows == 0) {
    outlineInsertRow(O.numrows, "", 0); //outlineInsertRow will also insert another '\0'
  }
  erow *row = &O.row[O.cy];
  int i;
  if (O.cx == 0 || O.cx == row->size) {
    i = 0;
    char spaces[i + 1]; //VLA
    for (int j=0; j<i; j++) {
      spaces[j] = ' ';
    }
    spaces[i] = '\0';
    O.cy+=direction;
    outlineInsertRow(O.cy, spaces, i);
    O.cx = i;
    
      
  }
  else {
    outlineInsertRow(O.cy + 1, &row->chars[O.cx], row->size - O.cx);
    row = &O.row[O.cy];
    row->size = O.cx;
    row->chars[row->size] = '\0';
    i = 0;
    O.cy++;
    row = &O.row[O.cy];
    O.cx = 0;
    for (;;){
      if (row->chars[0] != ' ') break;
      outlineDelChar();
    }

  for ( int j=0; j < i; j++ ) outlineInsertChar(' ');
  O.cx = i;
  }
}

void outlineDelChar() {
  //erow *row = &O.row[O.cy];
  erow *row = &O.row[get_filerow()];

  /* row size = 1 means there is 1 char; size 0 means 0 chars */
  /* Note that row->size does not count the terminating '\0' char*/
  // note below order important because row->size undefined if O.numrows = 0 because O.row is NULL
  if (O.numrows == 0 || row->size == 0 ) return; 

  memmove(&row->chars[O.cx], &row->chars[O.cx + 1], row->size - O.cx);
  row->size--;

  if (O.numrows == 1 && row->size == 0) {
    O.numrows = 0;
    free(O.row);
    //outlineFreeRow(&O.row[at]);
    O.row = NULL;
  }
  else if (O.cx == row->size && O.cx) O.cx = row->size - 1; 

  O.dirty++;
  row->dirty = true;

}

void outlineBackspace() {
  int fr = get_filerow();
  int fc = get_filecol();

  if (fc == 0) return;

  erow *row = &O.row[fr];

  //memmove(dest, source, number of bytes to move?)
  memmove(&row->chars[O.cx - 1], &row->chars[O.cx], row->size - O.cx + 1);
  row->size--;
  O.cx--; //if O.cx goes negative outlineScroll should handle it
 
  O.dirty++;
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
/*
void outlineOpen(char *context) {
  free(O.context);
  O.context = strdup(context);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    outlineInsertRow(O.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  O.dirty = 0;
}
*/

/*void outlineSave() {
  if (O.context == NULL) return;
  int len;
  char *buf = outlineRowsToString(&len);

  int fd = open(O.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        O.dirty = 0;
        outlineSetMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  outlineSetMessage("Can't save! I/O error: %s", strerror(errno));
}
*/

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

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

/*** output ***/

int outlineScroll() {
 
  if(!O.row) return 0;

  if (O.cy >= O.screenrows) {
    O.rowoff++;
    O.cy = O.screenrows - 1;
    return 1;
  } 

  if (O.cy < 0) {
    O.rowoff+=O.cy;
    O.cy = 0;
    return 1;
  }

  if (O.cx >= O.screencols) {
    O.coloff = O.coloff + O.cx - O.screencols + 1;
    O.cx = O.screencols - 1;
    return 1;
  }
  if (O.cx < 0) {
    O.coloff+=O.cx;
    O.cx = 0;
    return 1;
  }
  /*if (O.cx < 0) {
    if (O.coloff > 0){
    O.coloff+=O.cx;
    O.cx = 0;
    return 1;
    } else O.cx = 0;
  */

/*  if (O.cy < O.rowoff) {
    O.rowoff = O.cy;
  }
  if (O.cy >= O.rowoff + O.screenrows) {
    O.rowoff = O.cy - O.screenrows + 1;
  }
  if (O.cx < O.coloff) {
    O.coloff = O.cx;
  }
  if (O.cx >= O.coloff + O.screencols) {
    O.coloff = O.cx - O.screencols + 1;
  }*/

  return 0;
}

// "drawing" rows really means updating the ab buffer
// filerow conceptually is the row/column of the written to file text
void outlineDrawRows(struct abuf *ab) {
  int y;
  //move the cursor to midscreen in each row and erase back to the left
  char buf[32];
  for (int j; j < O.screenrows;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", j, O.screencols);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[1k", 4);
  }

  for (y = 0; y < O.screenrows; y++) {
    int filerow = y + O.rowoff;

    // len is how many characters of a given line will be seen given
    // that a long line may have caused the display to scroll
    int len = O.row[filerow].size - O.coloff;

    // below means when you scrolled far because of a long line
    // then you are going to draw nothing as opposed to negative characters
    // the line is very necessary or we segfault
    if (len < 0) len = 0; 

    // below says that if a line is long you only draw what fits on the screen
    if (len > O.screencols) len = O.screencols;
    
    if (O.mode == 3 && filerow >= O.highlight[0] && filerow <= O.highlight[1]) {
        abAppend(ab, "\x1b[48;5;242m", 11);
        abAppend(ab, &O.row[filerow].chars[O.coloff], len);
        abAppend(ab, "\x1b[0m", 4); //slz return background to normal
      
    } else if (O.mode == 4 && filerow == O.cy) {
        abAppend(ab, &O.row[filerow].chars[0], O.highlight[0] - O.coloff);
        abAppend(ab, "\x1b[48;5;242m", 11);
        abAppend(ab, &O.row[filerow].chars[O.highlight[0]], O.highlight[1]
                                              - O.highlight[0] - O.coloff);
        abAppend(ab, "\x1b[0m", 4); //slz return background to normal
        abAppend(ab, &O.row[filerow].chars[O.highlight[1]], len - O.highlight[1]);
      
    } //else abAppend(ab, &O.row[filerow].chars[O.coloff], len);
        else {
          if (O.row[filerow].star) abAppend(ab, "\x1b[1m", 4); //bold
          if (O.row[filerow].completed) abAppend(ab, "\x1b[33m", 5); //yellow
          if (O.row[filerow].deleted) abAppend(ab, "\x1b[31m", 5); //red
          abAppend(ab, &O.row[filerow].chars[O.coloff], len);
          //abAppend(ab, "\x1b[0m", 4); //slz return background to normal
    }
    
    //"\x1b[K" erases the part of the line to the right of the cursor in case the
    // new line i shorter than the old

   // abAppend(ab, "\x1b[K", 3);  //new testing *****
   // looks like "\x1b[4X" - will erase 4 chars so looks like in
   // erasing lines can't use "...[K" but could calculate "...[nX"
    abAppend(ab, "\r\n", 2);
    abAppend(ab, "\x1b[0m", 4); //slz return background to normal
  }
}

void outlineDrawRow(struct abuf *ab) {

  int filerow = get_filerow();

  // len is how many characters of a given line will be seen given
  // that a long line may have caused the display to scroll
  int len = O.row[filerow].size - O.coloff;

  // below means when you scrolled far because of a long line
  // then you are going to draw nothing as opposed to negative characters
  // the line is very necessary or we segfault
   if (len < 0) len = 0; 

   // below says that if a line is long you only draw what fits on the screen
   if (len > O.screencols) len = O.screencols;
      
   if (O.mode == 3 && filerow >= O.highlight[0] && filerow <= O.highlight[1]) {
       abAppend(ab, "\x1b[48;5;242m", 11);
       abAppend(ab, &O.row[filerow].chars[O.coloff], len);
       abAppend(ab, "\x1b[0m", 4); //slz return background to normal
        
   } else if (O.mode == 4 && filerow == O.cy) {
       abAppend(ab, &O.row[filerow].chars[0], O.highlight[0] - O.coloff);
       abAppend(ab, "\x1b[48;5;242m", 11);
       abAppend(ab, &O.row[filerow].chars[O.highlight[0]], O.highlight[1]
                                                - O.highlight[0] - O.coloff);
       abAppend(ab, "\x1b[0m", 4); //slz return background to normal
       abAppend(ab, &O.row[filerow].chars[O.highlight[1]], len - O.highlight[1]);
        
   } else {
         if (O.row[filerow].star) abAppend(ab, "\x1b[1m", 4); //bold
         if (O.row[filerow].completed) abAppend(ab, "\x1b[33m", 5); //red
         if (O.row[filerow].deleted) abAppend(ab, "\x1b[31m", 5); //red
         abAppend(ab, &O.row[filerow].chars[O.coloff], len);
            //abAppend(ab, "\x1b[0m", 4); //slz return background to normal
      }
    
    //"\x1b[K" erases the part of the line to the right of the cursor in case the
    // new line i shorter than the old

   // abAppend(ab, "\x1b[K", 3); //erase whole screen dealt with in refresh line
    
    //abAppend(ab, "\r\n", 2);
    abAppend(ab, "\x1b[0m", 4); //slz return background to normal
}

//status bar has inverted colors
void outlineDrawStatusBar(struct abuf *ab) {

  int fr = get_filerow();
  erow *row = &O.row[fr];

  abAppend(ab, "\x1b[7m", 4); //switches to inverted colors
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "|||%.20s - %d lines %s",
    O.context ? O.context : "[No Name]", O.numrows,
    //O.dirty ? "(modified)" : "");
    row->dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d Status bar %d/%d",
    //O.row[get_filerow()].id, get_filerow() + 1, O.numrows);
    row->id, fr + 1, O.numrows);
  if (len > O.screencols) len = O.screencols;
  abAppend(ab, status, len);
  
  /* add spaces until you just have enough room
     left to print the status message  */

  while (len < O.screencols) {
    if (O.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); //switches back to normal formatting
  abAppend(ab, "\r\n", 2);
}

void outlineDrawMessageBar(struct abuf *ab) {
  /*void outlineSetMessage(const char *fmt, ...) is where the message is created/set*/

  //"\x1b[K" erases the part of the line to the right of the cursor in case the
  // new line i shorter than the old

  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(O.statusmsg);
  if (msglen > O.screencols) msglen = O.screencols;
  //if (msglen && time(NULL) - O.statusmsg_time < 1000) //time
    abAppend(ab, O.statusmsg, msglen);
}

void outlineRefreshLine() {
  //outlineScroll();

  /*  struct abuf {
      char *b;
      int len;
    };*/

  if (O.row)
  //if (0)
    outlineSetMessage("length = %d, O.cx = %d, O.cy = %d, O.filerows = %d row id = %d", O.row[O.cy].size, O.cx, O.cy, get_filerow(), get_id(-1));

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0
  char buf[32];

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  // move the cursor to mid-screen, erase to left and move cursor back to begging of line
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH", O.cy+1, O.screencols, O.cy+1, 1);
  abAppend(&ab, buf, strlen(buf));

  outlineDrawRow(&ab);

  // move the cursor to the bottom of the screen
  // to 'draw' status bar and message bar
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.screenrows+1, 1);
  abAppend(&ab, buf, strlen(buf));
  outlineDrawStatusBar(&ab);
  outlineDrawMessageBar(&ab);

  // the lines below position the cursor where it should go
  if (O.mode != 2){
 //Below important: this is how to position the cursor
 //Will be needed if try to split the screen (not sure I want to do that)
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy+1, O.cx+1);
  abAppend(&ab, buf, strlen(buf));
}
  abAppend(&ab, "\x1b[?25h", 6); //shows the cursor

  write(STDOUT_FILENO, ab.b, ab.len);

  abFree(&ab);
}

void outlineRefreshScreen() {
  //outlineScroll();

  /*  struct abuf {
      char *b;
      int len;
    };*/

  if (O.row)
  //if (0)
    outlineSetMessage("length = %d, O.cx = %d, O.cy = %d, O.filerows = %d row id = %d", O.row[O.cy].size, O.cx, O.cy, get_filerow(), get_id(-1));

  struct abuf ab = ABUF_INIT; //abuf *b = NULL and int len = 0

  abAppend(&ab, "\x1b[?25l", 6); //hides the cursor
  abAppend(&ab, "\x1b[H", 3);  //sends the cursor home
  char buf[32];
  for (int j=0; j < O.screenrows;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j, O.screencols + 1); //erase from cursor to left
    abAppend(&ab, buf, strlen(buf));
  }

  abAppend(&ab, "\x1b[H", 3);  //sends the cursor home
  outlineDrawRows(&ab);

  outlineDrawStatusBar(&ab);
  outlineDrawMessageBar(&ab);

  // the lines below position the cursor where it should go
  if (O.mode != 2){
  char buf[32];
 //Below important: this is how to position the cursor
 //Will be needed if try to split the screen (not sure I want to do that)
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy+1, O.cx+1);
  abAppend(&ab, buf, strlen(buf));
}
  abAppend(&ab, "\x1b[?25h", 6); //shows the cursor

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

  vsnprintf(O.statusmsg, sizeof(O.statusmsg), fmt, ap);
  va_end(ap); //free a va_list
  //O.statusmsg_time = time(NULL);
}

void outlineMoveCursor(int key) {
  int fr = get_filerow();
  erow *row = &O.row[fr];

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      // note O.cx might be zero but filecol positive because of O.coloff
      // then O.cx goes negative
      // dealt with in EditorScroll
      if (get_filecol() > 0) O.cx--; 
      break;

    case ARROW_RIGHT:
    case 'l':
      if (row && get_filecol() < row->size - 1)  O.cx++;  //segfaults on opening if you arrow right w/o row
      if (row)  O.cx++;  //segfaults on opening if you arrow right w/o row
      break;

    case ARROW_UP:
    case 'k':
      // note O.cy might be zero but filerow positive because of O.rowoff
      // then O.cy goes negative
      // dealt with in EditorScroll
      if (get_filerow() > 0) O.cy--; 
      break;

    case ARROW_DOWN:
    case 'j':
      if (get_filerow() < O.numrows - 1) O.cy++;
      break;
  }

  /* Below deals with moving cursor up and down from longer rows to shorter rows 
     row has to be calculated again because this is the new row you've landed on */
  if(key==ARROW_UP || key==ARROW_DOWN){
    fr = get_filerow();
    row = &O.row[fr];
    int rowlen = row ? row->size : 0;
    if (rowlen == 0) O.cx = 0;
  //else if (O.mode == 1 && O.cx >= rowlen) O.cx = rowlen;
    else if (O.mode == 1 && (O.cx+O.coloff) >= rowlen) O.cx = rowlen-O.coloff;
    else if (get_filecol() >= rowlen) O.cx = rowlen - O.coloff -1;
  }
}
// higher level outline function depends on outlineReadKey()
void outlineProcessKeypress() {
  static int quit_times = KILO_QUIT_TIMES;
  int start, end;

  /* outlineReadKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  int c = outlineReadKey();

/*************************************** 
 * This is where you enter insert mode* 
 * O.mode = 1
 ***************************************/

  if (O.mode == 1){

  switch (c) {

    case '\r':
      outlineCreateSnapshot();
      outlineInsertNewline(1);
      break;

    case CTRL_KEY('q'):
      if (O.dirty && quit_times > 0) {
        outlineSetMessage("WARNING!!! Database has not been updated. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
      write(STDOUT_FILENO, "\x1b[H", 3); //cursor goes home, which is to first char
      exit(0);
      break;

    case CTRL_KEY('s'):
      //outlineSave();
      break;

    case HOME_KEY:
      O.cx = 0;
      break;

    case END_KEY:
      if (O.cy < O.numrows)
        O.cx = O.row[O.cy].size;
      break;

    case BACKSPACE:
      outlineCreateSnapshot();
      outlineBackspace();
      break;

    case DEL_KEY:
      outlineCreateSnapshot();
      outlineDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      
      if (c == PAGE_UP) {
        O.cy = O.rowoff;
      } else if (c == PAGE_DOWN) {
        O.cy = O.rowoff + O.screenrows - 1;
        if (O.cy > O.numrows) O.cy = O.numrows;
      }

        int times = O.screenrows;
        while (times--){
          outlineMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
          } 
      
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      outlineMoveCursor(c);
      break;

    case CTRL_KEY('b'):
    case CTRL_KEY('i'):
    case CTRL_KEY('e'):
      outlineCreateSnapshot();
      outlineDecorateWord(c);
      break;

    case CTRL_KEY('z'):
      //O.smartindent = (O.smartindent == 1) ? 0 : 1;
      //outlineSetMessage("O.smartindent = %d", O.smartindent); 
      break;

    case '\x1b':
      O.mode = 0;
      if (O.cx > 0) O.cx--;
      // below - if the indent amount == size of line then it's all blanks
      /*int n = outlineIndentAmount(O.cy);
      if (n == O.row[O.cy].size) {
        O.cx = 0;
        for (int i = 0; i < n; i++) {
          outlineDelChar();
        }
      }
      outlineSetMessage("");*/

      //update_row(); //was used when testing but should go

      return;

    default:
      outlineCreateSnapshot();
      outlineInsertChar(c);
      return;
 
 } 
  quit_times = KILO_QUIT_TIMES;

/*************************************** 
 * This is where you enter normal mode* 
 * O.mode = 0
 ***************************************/

 } else if (O.mode == 0){
 
  /*leading digit is a multiplier*/
  if (isdigit(c)) { //equiv to if (c > 47 && c < 58) 
    if ( O.repeat == 0 ){

      //if c = 48 => 0 then it falls through to 0 move to beginning of line
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

  switch (c) {

    case 'i':
      //This probably needs to be generalized when a letter is a single char command
      //but can also appear in multi-character commands too
      if (O.command[0] == '\0') { 
        O.mode = 1;
        O.command[0] = '\0';
        O.repeat = 0;
        outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;
      }
      break;

    case 's':
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) outlineDelChar();
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 1;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
      return;

    case 'x':
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) outlineDelChar();
      O.command[0] = '\0';
      O.repeat = 0;
      return;
    
    case 'r':
      O.mode = 5;
      return;

    case '~':
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) outlineChangeCase();
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case 'a':
      if (O.command[0] == '\0') { 
        O.mode = 1; //this has to go here for MoveCursor to work right at EOLs
        outlineMoveCursor(ARROW_RIGHT);
        O.command[0] = '\0';
        O.repeat = 0;
        outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;
      }
      break;

    case 'A':
      outlineMoveCursorEOL();
      O.mode = 1; //needs to be here for movecursor to work at EOLs
      outlineMoveCursor(ARROW_RIGHT);
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 1;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    case 'w':
      if (O.command[0] == '\0') { 
        outlineMoveNextWord();
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
      break;

    case 'b':
      outlineMoveBeginningWord();
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case 'e':
      if (O.command[0] == '\0') { 
        outlineMoveEndWord();
        O.command[0] = '\0';
        O.repeat = 0;
        return;
        }
      break;

    case '0':
      //O.coloff = 0; //unlikely to work
      //O.cx = 0;
      O.cx = -O.coloff; //surprisingly seems to work
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case '$':
      if (O.command[0] == '\0') { 
        outlineMoveCursorEOL();
        O.command[0] = '\0';
        O.repeat = 0;
        return;
      }
      break;

    case 'I':
      O.cx = outlineIndentAmount(O.cy);
      O.mode = 1;
      O.command[0] = '\0';
      O.repeat = 0;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    case 'o':
      outlineCreateSnapshot();
      O.cx = 0;
      outlineInsertNewline(1);
      O.mode = 1;
      O.command[0] = '\0';
      O.repeat = 0;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    case 'O':
      outlineCreateSnapshot();
      O.cx = 0;
      outlineInsertNewline(0);
      O.mode = 1;
      O.command[0] = '\0';
      O.repeat = 0;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    case 'G':
      O.cx = 0;
      O.cy = O.numrows-1;
      O.command[0] = '\0';
      O.repeat = 0;
      return;
  
    case ':':
      O.mode = 2;
      O.command[0] = ':';
      O.command[1] = '\0';
      outlineSetMessage(":"); 
      return;

    case 'V':
      O.mode = 3;
      O.command[0] = '\0';
      O.repeat = 0;
      O.highlight[0] = O.highlight[1] = O.cy;
      outlineSetMessage("\x1b[1m-- VISUAL LINE --\x1b[0m");
      return;

    case 'v':
      O.mode = 4;
      O.command[0] = '\0';
      O.repeat = 0;
      O.highlight[0] = O.highlight[1] = O.cx;
      outlineSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
      return;

    case 'p':  
      outlineCreateSnapshot();
      if (strlen(string_buffer)) outlinePasteString();
      else outlinePasteLine();
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case '*':  
      getWordUnderCursor();
      outlineFindNextWord(); 
      return;

    case 'n':
      outlineFindNextWord();
      return;

    case 'u':
      outlineRestoreSnapshot();
      return;

    case CTRL_KEY('z'):
      //O.smartindent = (O.smartindent == 4) ? 0 : 4;
      //outlineSetMessage("O.smartindent = %d", O.smartindent); 
      return;

    case CTRL_KEY('b'):
    case CTRL_KEY('i'):
    case CTRL_KEY('e'):
      outlineCreateSnapshot();
      outlineDecorateWord(c);
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
      outlineMarkupLink(); 
      return;

    case '\x1b':
    // Leave in O.mode = 0 -> normal mode
      O.command[0] = '\0';
      O.repeat = 0;
      return;
  }

  // don't want a default case just want it to fall through
  // if it doesn't match switch above
  // presumption is it's a multicharacter command

  int n = strlen(O.command);
  O.command[n] = c;
  O.command[n+1] = '\0';

  switch (keyfromstring(O.command)) {
    
    case C_daw:
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) outlineDelWord();
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case C_dw:
      outlineCreateSnapshot();
      for (int j = 0; j < O.repeat; j++) {
        start = O.cx;
        outlineMoveEndWord2();
        end = O.cx;
        O.cx = start;
        for (int j = 0; j < end - start + 2; j++) outlineDelChar();
      }
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case C_de:
      outlineCreateSnapshot();
      start = O.cx;
      outlineMoveEndWord(); //correct one to use to emulate vim
      end = O.cx;
      O.cx = start; 
      for (int j = 0; j < end - start + 1; j++) outlineDelChar();
      O.cx = (start < O.row[O.cy].size) ? start : O.row[O.cy].size -1;
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case C_dd:
      if (O.numrows != 0) {
        int r = O.numrows - O.cy;
        O.repeat = (r >= O.repeat) ? O.repeat : r ;
        outlineCreateSnapshot();
        outlineYankLine(O.repeat);
        for (int i = 0; i < O.repeat ; i++) outlineDelRow(O.cy);
      }
      O.cx = 0;
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    case C_d$:
      outlineCreateSnapshot();
      outlineDeleteToEndOfLine();
      if (O.numrows != 0) {
        int r = O.numrows - O.cy;
        O.repeat--;
        O.repeat = (r >= O.repeat) ? O.repeat : r ;
        //outlineYankLine(O.repeat); //b/o 2 step won't really work right
        for (int i = 0; i < O.repeat ; i++) outlineDelRow(O.cy);
      }
      O.command[0] = '\0';
      O.repeat = 0;
      return;

    //tested with repeat on one line
    case C_cw:
      outlineCreateSnapshot();
      for (int j = 0; j < O.repeat; j++) {
        start = O.cx;
        outlineMoveEndWord();
        end = O.cx;
        O.cx = start;
        for (int j = 0; j < end - start + 1; j++) outlineDelChar();
      }
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 1;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    //tested with repeat on one line
    case C_caw:
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) outlineDelWord();
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 1;
      outlineSetMessage("\x1b[1m-- INSERT --\x1b[0m");
      return;

    case C_gg:
     O.cx = 0;
     O.cy = O.repeat-1;
     O.command[0] = '\0';
     O.repeat = 0;
     return;

   case C_yy:  
     outlineYankLine(O.repeat);
     O.command[0] = '\0';
     O.repeat = 0;
     return;

    default:
      return;

    } 

  /************************************   
   *command line mode below O.mode = 2*
   ************************************/

  } else if (O.mode == 2) {

    if (c == '\x1b') {
      O.mode = 0;
      O.command[0] = '\0';
      outlineSetMessage(""); 
      return;}

    if (c == '\r') {

      if (O.command[1] == 'w') {
        update_rows();
        O.mode = 0;
        O.command[0] = '\0';
      }

      else if (O.command[1] == 'e') {
        if (strlen(O.command) > 3) {
          O.context = strdup(&O.command[3]);
          outlineSetMessage("\"%s\" will be opened", O.context);
        }
        else outlineSetMessage("You need to provide a context");

        O.mode = 0;
        O.command[0] = '\0';
      }
      else if (O.command[1] == 'x') {
        update_rows();
        write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
        write(STDOUT_FILENO, "\x1b[H", 3); //cursor goes home, which is to first char
        exit(0);
      }  

      else if (O.command[1] == 'q') {
        if (O.dirty) {
          if (strlen(O.command) == 3 && O.command[2] == '!') {
            write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
            write(STDOUT_FILENO, "\x1b[H", 3); //cursor goes home, which is to first char
            exit(0);
          }  
          else {
            O.mode = 0;
            O.command[0] = '\0';
            outlineSetMessage("No db write since last change");
          }
        }
       
        else {
          write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
          write(STDOUT_FILENO, "\x1b[H", 3); //cursor goes home, which is to first char
          exit(0);
        }
      }
    }

    else {
      int n = strlen(O.command);
      if (c == DEL_KEY || c == BACKSPACE) {
        O.command[n-1] = '\0';
      } else {
        O.command[n] = c;
        O.command[n+1] = '\0';
      }
      outlineSetMessage(O.command);
    }
  /********************************************
   * visual line mode O.mode = 3
   ********************************************/

  } else if (O.mode == 3) {


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
      O.highlight[1] = O.cy;
      return;

    case 'x':
      if (O.numrows != 0) {
        outlineCreateSnapshot();
        O.repeat = O.highlight[1] - O.highlight[0] + 1;
        O.cy = O.highlight[0];
        outlineYankLine(O.repeat);

        for (int i = 0; i < O.repeat; i++) outlineDelRow(O.cy);
      }
      O.cx = 0;
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 0;
      outlineSetMessage("");
      return;

    case 'y':  
      O.repeat = O.highlight[1] - O.highlight[0] + 1;
      O.cy = O.highlight[0];
      outlineYankLine(O.repeat);
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 0;
      outlineSetMessage("");
      return;

    case '\x1b':
      O.mode = 0;
      O.command[0] = '\0';
      O.repeat = 0;
      outlineSetMessage("");
      return;

    default:
      return;
    }

  } else if (O.mode == 4) {

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
      O.highlight[1] = O.cx;
      return;

    case 'x':
      outlineCreateSnapshot();
      O.repeat = O.highlight[1] - O.highlight[0] + 1;
      O.cx = O.highlight[0];
      outlineYankString(); 

      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar(O.cx);
      }

      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 0;
      outlineSetMessage("");
      return;

    case 'y':  
      O.repeat = O.highlight[1] - O.highlight[0] + 1;
      O.cx = O.highlight[0];
      outlineYankString();
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 0;
      outlineSetMessage("");
      return;

    case CTRL_KEY('b'):
    case CTRL_KEY('i'):
    case CTRL_KEY('e'):
      outlineCreateSnapshot();
      outlineDecorateVisual(c);
      O.command[0] = '\0';
      O.repeat = 0;
      O.mode = 0;
      outlineSetMessage("");
      return;

    case '\x1b':
      O.mode = 0;
      O.command[0] = '\0';
      O.repeat = 0;
      outlineSetMessage("");
      return;

    default:
      return;
    }
  } else if (O.mode == 5) {
      outlineCreateSnapshot();
      for (int i = 0; i < O.repeat; i++) {
        outlineDelChar();
        outlineInsertChar(c);
      }
      O.repeat = 0;
      O.command[0] = '\0';
      O.mode = 0;
  }
}

/*** slz additions ***/

void update_row(void) {

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  char query[300] = {'\0'};
  char title[200] = {'\0'};
  int fr = get_filerow();
  strncpy(title, O.row[fr].chars, O.row[fr].size);
  //sprintf(query, "UPDATE task SET title=\'%s\' WHERE id=%d", O.row[fr].chars, get_id(-1));
  //sprintf(query, "UPDATE task SET title=\'%s\' WHERE id=%d", row, get_id(-1));
  sprintf(query, "UPDATE task SET title=\'%s\', "
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                   title, get_id(-1));

  PGresult *res = PQexec(conn, query); 
    
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    outlineSetMessage("UPDATE command failed");
    PQclear(res);
    //do_exit(conn);
  }    
  outlineSetMessage("%s - %s - %d", query, title, O.row[fr].size);

    return;
}

void update_rows(void) {
  if (!O.dirty) return;

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
        
        fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
        do_exit(conn);
    }
  }

  for (int i=0; i < O.numrows;i++) {
    erow *row = &O.row[i];
    if (row->dirty) {
      char query[300] = {'\0'};
      char title[200] = {'\0'};
      strncpy(title, row->chars, row->size);
      sprintf(query, "UPDATE task SET title=\'%s\', "
                   "modified=LOCALTIMESTAMP - interval '5 hours' "
                   "WHERE id=%d",
                   title, row->id);

      PGresult *res = PQexec(conn, query); 
    
      if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        outlineSetMessage("UPDATE command failed");
        PQclear(res);
      } else row->dirty = false;    
    }
  }
  O.dirty = 0;
  return;
}

int get_filerow(void) {
  return O.cy + O.rowoff; ////////
}

int get_filecol(void) {
  return O.cx + O.coloff; ////////
}

int get_id(int fr) {
  if(fr==-1) fr = get_filerow();
  int id = O.row[fr].id;
  return id;
}

void outlineCreateSnapshot() {
  if ( O.numrows == 0 ) return; //don't create snapshot if there is no text
  for (int j = 0 ; j < O.prev_numrows ; j++ ) {
    free(O.prev_row[j].chars);
  }
  O.prev_row = realloc(O.prev_row, sizeof(erow) * O.numrows );
  for ( int i = 0 ; i < O.numrows ; i++ ) {
    int len = O.row[i].size;
    O.prev_row[i].chars = malloc(len + 1);
    O.prev_row[i].size = len;
    memcpy(O.prev_row[i].chars, O.row[i].chars, len);
    O.prev_row[i].chars[len] = '\0';
  }
  O.prev_numrows = O.numrows;
}

void outlineRestoreSnapshot() {
  for (int j = 0 ; j < O.numrows ; j++ ) {
    free(O.row[j].chars);
  } 
  O.row = realloc(O.row, sizeof(erow) * O.prev_numrows );
  for (int i = 0 ; i < O.prev_numrows ; i++ ) {
    int len = O.prev_row[i].size;
    O.row[i].chars = malloc(len + 1);
    O.row[i].size = len;
    memcpy(O.row[i].chars, O.prev_row[i].chars, len);
    O.row[i].chars[len] = '\0';
  }
  O.numrows = O.prev_numrows;
}

void outlineChangeCase() {
  erow *row = &O.row[O.cy];
  char d = row->chars[O.cx];
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
  erow *row = &O.row[O.cy];
  for (x = O.highlight[0], n = 0; x < O.highlight[1]+1; x++, n++) {
      string_buffer[n] = row->chars[x];
  }

  string_buffer[n] = '\0';
}

void outlinePasteString() {
  int fr = get_filerow();
  if (fr == O.numrows) {
    outlineInsertRow(O.numrows, "", 0); //outlineInsertRow will also insert another '\0'
  }

  erow *row = &O.row[fr];
  //if (O.cx < 0 || O.cx > row->size) O.cx = row->size;
  int len = strlen(string_buffer);
  row->chars = realloc(row->chars, row->size + len); 

  /* moving all the chars at the current x cursor position on char
     farther down the char string to make room for the new character
     Maybe a clue from outlineInsertRow - it's memmove is below
     memmove(&O.row[at + 1], &O.row[at], sizeof(erow) * (O.numrows - at));
  */

  memmove(&row->chars[O.cx + len], &row->chars[O.cx], row->size - O.cx); //****was O.cx + 1

  for (int i = 0; i < len; i++) {
    row->size++;
    row->chars[O.cx] = string_buffer[i];
    O.cx++;
  }
  O.dirty++;
  row->dirty = true;
}

void outlinePasteLine(){
  if ( O.numrows == 0 ) outlineInsertRow(0, "", 0);
  for (int i=0; i < 10; i++) {
    if (line_buffer[i] == NULL) break;

    int len = strlen(line_buffer[i]);
    O.cy++;
    outlineInsertRow(O.cy, line_buffer[i], len);
  }
}

int outlineIndentAmount(int y) {
  int i;
  erow *row = &O.row[y];
  if ( !row || row->size == 0 ) return 0; //row is NULL if the row has been deleted or opening app

  for ( i = 0; i < row->size; i++) {
    if (row->chars[i] != ' ') break;}

  return i;
}

void outlineDelWord() {
  int fr = get_filerow();
  int fc = get_filecol();

  erow *row = &O.row[fr];
  if (row->chars[fc] < 48) return;

  int i,j,x;
  for (i = fc; i > -1; i--){
    if (row->chars[i] < 48) break;
    }
  for (j = fc; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }
  O.cx = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  O.dirty++;
  row->dirty = true;
  //outlineSetMessage("i = %d, j = %d", i, j ); 
}

void outlineDeleteToEndOfLine() {
  erow *row = &O.row[O.cy];
  row->size = O.cx;
  //Arguably you don't have to reallocate when you reduce the length of chars
  row->chars = realloc(row->chars, O.cx + 1); //added 10042018 - before wasn't reallocating memory
  row->chars[O.cx] = '\0';
  }

void outlineMoveCursorEOL() {
  int fr = get_filerow();
  O.cx = O.row[fr].size - 1;  //if O.cx > O.screencols will be adjusted in EditorScroll
}

// not same as 'e' but moves to end of word or stays put if already on end of word
void outlineMoveEndWord2() {
  int j;
  int fr = get_filerow();
  erow row = O.row[fr];

  for (j = O.cx + 1; j < row.size ; j++) {
    if (row.chars[j] < 48) break;
  }

  O.cx = j - 1;
}

void outlineMoveNextWord() {
  // below is same is outlineMoveEndWord2
  int j;
  int fr = get_filerow();
  erow row = O.row[fr];

  for (j = O.cx + 1; j < row.size ; j++) {
    if (row.chars[j] < 48) break;
  }

  O.cx = j - 1;
  // end outlineMoveEndWord2

  for (j = O.cx + 1; j < row.size ; j++) { //+1
    if (row.chars[j] > 48) break;
  }
  O.cx = j;
}

void outlineMoveBeginningWord() {
  int fr = get_filerow();
  erow *row = &O.row[fr];
  if (O.cx == 0) return;
  for (;;) {
    if (row->chars[O.cx - 1] < 48) O.cx--;
    else break;
    if (O.cx == 0) return;
  }

  int i;
  for (i = O.cx - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  O.cx = i + 1;
}

void outlineMoveEndWord() {
  int fr = get_filerow();
  erow *row = &O.row[fr];
  if (O.cx == row->size - 1) return;
  for (;;) {
    if (row->chars[O.cx + 1] < 48) O.cx++;
    else break;
    if (O.cx == row->size - 1) return;
  }

  int j;
  for (j = O.cx + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }

  O.cx = j - 1;
}

void outlineDecorateWord(int c) {
  int fr = get_filerow();
  erow *row = &O.row[fr];
  char cc;
  if (row->chars[O.cx] < 48) return;

  int i, j;

  /*Note to catch ` would have to be row->chars[i] < 48 || row-chars[i] == 96 - may not be worth it*/

  for (i = O.cx - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  for (j = O.cx + 1; j < row->size ; j++) {
    if (row->chars[j] < 48) break;
  }
  
  if (row->chars[i] != '*' && row->chars[i] != '`'){
    cc = (c == CTRL_KEY('b') || c ==CTRL_KEY('i')) ? '*' : '`';
    O.cx = i + 1;
    outlineInsertChar(cc);
    O.cx = j + 1;
    outlineInsertChar(cc);

    if (c == CTRL_KEY('b')) {
      O.cx = i + 1;
      outlineInsertChar('*');
      O.cx = j + 2;
      outlineInsertChar('*');
    }
  } else {
    O.cx = i;
    outlineDelChar();
    O.cx = j-1;
    outlineDelChar();

    if (c == CTRL_KEY('b')) {
      O.cx = i - 1;
      outlineDelChar();
      O.cx = j - 2;
      outlineDelChar();
    }
  }
}

void outlineDecorateVisual(int c) {
  O.cx = O.highlight[0];
  if (c == CTRL_KEY('b')) {
    outlineInsertChar('*');
    outlineInsertChar('*');
    O.cx = O.highlight[1]+3;
    outlineInsertChar('*');
    outlineInsertChar('*');
  } else {
    char cc = (c ==CTRL_KEY('i')) ? '*' : '`';
    outlineInsertChar(cc);
    O.cx = O.highlight[1]+2;
    outlineInsertChar(cc);
  }
}

void getWordUnderCursor(){
  int fr = get_filerow();
  erow *row = &O.row[fr];
  if (row->chars[O.cx] < 48) return;

  int i,j,n,x;

  for (i = O.cx - 1; i > -1; i--){
    if (row->chars[i] < 48) break;
  }

  for (j = O.cx + 1; j < row->size ; j++) {
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
  y = O.cy;
  x = O.cx + 1; //in case sitting on beginning of the word
 
  /*n counter so we can exit for loop if there are  no matches for command 'n'*/
  for ( int n=0; n < O.numrows; n++ ) {
    erow *row = &O.row[y];
    z = strstr(&(row->chars[x]), search_string);
    if ( z != NULL ) {
      O.cy = y;
      O.cx = z - row->chars;
      break;
    }
    y++;
    x = 0;
    if ( y == O.numrows ) y = 0;
  }

    outlineSetMessage("x = %d; y = %d", x, y); 
}

void outlineMarkupLink() {
  int y, numrows, j, n, p;
  char *z;
  char *http = "http";
  char *bracket_http = "[http";
  numrows = O.numrows;
  n = 1;


  for ( n=1; O.row[numrows-n].chars[0] == '[' ; n++ );


  for (y=0; y<numrows; y++){
    erow *row = &O.row[y];
    if (row->chars[0] == '[') continue;
    if (strstr(row->chars, bracket_http)) continue;

    z = strstr(row->chars, http);
    if (z==NULL) continue;
    O.cy = y;
    p = z - row->chars;

    //url including http:// must be at least 10 chars you'd think
    for (j = p + 10; j < row->size ; j++) { 
      if (row->chars[j] == 32) break;
    }

    int len = j-p;
    char *zz = malloc(len + 1);
    memcpy(zz, z, len);
    zz[len] = '\0';

    O.cx = p;
    outlineInsertChar('[');
    O.cx = j+1;
    outlineInsertChar(']');
    outlineInsertChar('[');
    outlineInsertChar(48+n);
    outlineInsertChar(']');

    if ( O.row[numrows-1].chars[0] != '[' ) {
      O.cy = O.numrows - 1; //check why need - 1 otherwise seg faults
      O.cx = 0;
      outlineInsertNewline(1);
      }

    outlineInsertRow(O.numrows, zz, len); 
    free(zz);
    O.cx = 0;
    O.cy = O.numrows - 1;
    outlineInsertChar('[');
    outlineInsertChar(48+n);
    outlineInsertChar(']');
    outlineInsertChar(':');
    outlineInsertChar(' ');
    outlineSetMessage("z = %u and beginning position = %d and end = %d and %u", z, p, j,zz); 
    n++;
  }
}

/*** slz testing stuff ***/

void getcharundercursor() {
  erow *row = &O.row[O.cy];
  char d = row->chars[O.cx];
  outlineSetMessage("character under cursor at position %d of %d: %c", O.cx, row->size, d); 
}

/*** slz testing stuff (above) ***/

/*** init ***/

void initEditor() {
  O.cx = 0; //cursor x position
  O.cy = 0; //cursor y position
  O.rowoff = 0;  //number of rows scrolled off the screen
  O.coloff = 0;  //col the user is currently scrolled to  
  O.numrows = 0; //number of rows of text
  O.row = NULL; //pointer to the erow structure 'array'
  O.prev_numrows = 0; //number of rows of text in snapshot
  O.prev_row = NULL; //prev_row is pointer to snapshot for undoing
  O.dirty = 0; //has filed changed since last save
  O.context = NULL;
  O.statusmsg[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  O.highlight[0] = O.highlight[1] = -1;
  O.mode = 0; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  O.command[0] = '\0';
  O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  //if (getWindowSize(&O.screenrows, &O.screencols) == -1) die("getWindowSize");
  if (getWindowSize(&screenrows, &screencols) == -1) die("getWindowSize");
  O.screenrows = screenrows - 2;
  O.screencols = screencols/2;
}

int main(void) {
  enableRawMode();
  initEditor();
  int pos = O.screencols + 2;
  char buf[32];
  for (int j=1; j < O.screenrows + 1;j++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", j, pos);
    write(STDOUT_FILENO, buf, strlen(buf));
    //write(STDOUT_FILENO, "\x1b[31;1m|", 8); //31m = red; 1m = bold (only need last 'm')
    write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
    //below x = 0x78 vertical line and q = 0x71 is horizontal
    write(STDOUT_FILENO, "\x1b[37;1mx", 8); //31 = red; 37 = white; 1m = bold (only need last 'm')
    write(STDOUT_FILENO, "\x1b[0m", 4); //slz return background to normal (not really nec didn't tough backgound)
    write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode

}
  get_conn();
  PGresult *res = get_data("programming", 200); 
  int rows = PQntuples(res);
  for(int i=0; i<rows; i++) {
    char *z = PQgetvalue(res, i, 3);
    char *zz = PQgetvalue(res, i, 0);
    bool star = (*PQgetvalue(res, i, 8) == 't') ? true: false;
    bool deleted = (*PQgetvalue(res, i, 14) == 't') ? true: false;
    bool completed = (*PQgetvalue(res, i, 10)) ? true: false;
    int id = atoi(zz);
    outlineInsertRow2(O.numrows, z, strlen(z), id, star, deleted, completed); 
    //if(i>=O.screenrows) O.rowoff++;
    //else O.cy = i;
  }

  PQclear(res);
 // PQfinish(conn);

  O.cx = O.cy = O.rowoff = 0;
  //outlineSetMessage("HELP: Ctrl-S = save | Ctrl-Q = quit"); //slz commented this out
  outlineSetMessage("rows: %d  cols: %d", O.screenrows, O.screencols); //for display screen dimens

  outlineRefreshScreen(); 

  while (1) {
    int scroll = outlineScroll();
    if (scroll) outlineRefreshScreen(); 
    else outlineRefreshLine();
    outlineProcessKeypress();
  }
  return 0;
}
