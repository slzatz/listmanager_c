#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <Python.h>
#include "session.h"
//#include "common.h" //in header
#include <string>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fstream>
#include "pstream.h"
#include "organizer.h"
#include <cstdlib>
#include "inipp.h" // https://github.com/mcmtroffaes/inipp

extern "C" {
#include <mkdio.h>
}

//#define TOP_MARGIN 1
//#define LEFT_MARGIN 2

const std::string DB_INI = "db.ini";
zmq::context_t Session::context = zmq::context_t(1);

using namespace redi; //pstream

std::string generateWWString(const std::string &text, int width, int length, std::string ret);
void highlight_terms_string(std::string &text, std::vector<std::vector<int>> word_positions);
std::string readNoteIntoString(int id);
std::vector<std::vector<int>> getNoteSearchPositions(int id);
int getFolderTid(int id);
int getId(void);
std::pair<std::string, std::vector<std::string>> getTaskKeywords(int id);
void outlineProcessKeypress(int c);

int Session::link_id = 0;
char Session::link_text[20] = {};

Session sess = Session(); //global; extern Session sess in session.h

void Session::parseIniFile(std::string ini_name) {
  inipp::Ini<char> ini;
  std::ifstream is(ini_name);
  ini.parse(is);
  inipp::extract(ini.sections["ini"]["user"], cfg.user);
  inipp::extract(ini.sections["ini"]["password"], cfg.password);
  inipp::extract(ini.sections["ini"]["dbname"], cfg.dbname);
  inipp::extract(ini.sections["ini"]["hostaddr"], cfg.hostaddr);
  inipp::extract(ini.sections["ini"]["port"], cfg.port);
  inipp::extract(ini.sections["editor"]["ed_pct"], cfg.ed_pct);
}

void Session::doExit(PGconn *conn) {
    PQfinish(conn);
    exit(1);
}

//pg ini stuff
void Session::getConn(void) {
  char conninfo[250];
  parseIniFile(DB_INI);
  
  sprintf(conninfo, "user=%s password=%s dbname=%s hostaddr=%s port=%d", 
          cfg.user.c_str(), cfg.password.c_str(), cfg.dbname.c_str(), cfg.hostaddr.c_str(), cfg.port);

  conn = PQconnectdb(conninfo);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
      fprintf(stderr, "Connection to database failed: %s\n",
      PQerrorMessage(conn));
      doExit(conn);
    }
  } 
}


void Session::eraseScreenRedrawLines(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Erase the screen
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (int j = 1; j < screenlines + 1; j++) {

    /* this was getting overwritten and really just need the "T"
    // First vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, divider - TIME_COL_WIDTH + 1); //don't think need offset
    write(STDOUT_FILENO, buf, strlen(buf));
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    write(STDOUT_FILENO, "\x1b[37;1mx", 8);
    */

    // Second vertical line - the divider
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, divider);
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mx", 8); 
  }

  write(STDOUT_FILENO, "\x1b[1;1H", 6);
  for (int k=1; k < screencols ;k++) {
    // note: cursor advances automatically so don't need to 
    // do that explicitly
    write(STDOUT_FILENO, "\x1b[37;1mq", 8); //horizontal line
  }

  // draw first column's 'T' corner - time column "T"
  if (divider > 10) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, divider - TIME_COL_WIDTH + 1); //may not need offset
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner
  }

  // draw next column's 'T' corner - divider
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, divider);
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  write(STDOUT_FILENO, "\x1b[0m", 4); // return background to normal (? necessary)
  write(STDOUT_FILENO, "\x1b(B", 3); //exit line drawing mode
}

void Session::eraseRightScreen(void) {

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor

  //below positions cursor such that top line is erased the first time through
  //for loop although ? could really start on second line since need to redraw
  //horizontal line anyway
  fmt::memory_buffer buf;
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN, divider + 1); //need +1 or erase top T
  ab.append(buf.data(), buf.size());

  //erase the screen
  fmt::memory_buffer lf_ret;
  //note org.divider -1 would take out center divider line and don't want to do that
  fmt::format_to(lf_ret, "\r\n\x1b[{}C", divider);
  for (int i=0; i < screenlines - TOP_MARGIN; i++) { 
    ab.append("\x1b[K");
    ab.append(lf_ret.data(), lf_ret.size());
  }
    ab.append("\x1b[K"); //added 09302020 to erase the last line (message line)

  // redraw top horizontal line which has t's and was erased above
  // ? if the individual editors draw top lines do we need to just
  // erase but not draw
  ab.append("\x1b(0"); // Enter line drawing mode
  for (int j=1; j<totaleditorcols+1; j++) { //added +1 0906/2020
    std::string buf2 = fmt::format("\x1b[{};{}H", TOP_MARGIN, divider + j);
    ab.append(buf2);
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white;
    // 1m = bold (note only need one 'm'
    ab.append("\x1b[37;1mq");
  }
  //exit line drawing mode
  ab.append("\x1b(B");

  std::string buf3 = fmt::format("\x1b[{};{}H", TOP_MARGIN + 1, divider + 2);
  ab.append(buf3);
  ab.append("\x1b[0m"); // needed or else in bold mode from line drawing above

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::positionEditors(void) {
  int editor_slots = 0;
  for (auto z : editors) {
    if (!z->is_below) editor_slots++;
  }

  int s_cols = -1 + (screencols - divider)/editor_slots;
  int i = -1; //i = number of columns of editors -1
  for (auto z : editors) {
    if (!z->is_below) i++;
    z->left_margin = divider + i*s_cols + i;
    z->screencols = s_cols;
    z->setLinesMargins();
  }
}

void Session::drawEditors(void) {
  std::string ab;
  for (auto &e : editors) {
  //for (size_t i=0, max=editors.size(); i!=max; ++i) {
    //Editor *&e = editors.at(i);
    e->editorRefreshScreen(true);
    std::string buf;
    ab.append("\x1b(0"); // Enter line drawing mode
    for (int j=1; j<e->screenlines+1; j++) {
      buf = fmt::format("\x1b[{};{}H", e->top_margin - 1 + j, e->left_margin + e->screencols+1);
      ab.append(buf);
      // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
      // only need one 'm'
      ab.append("\x1b[37;1mx");
    }
    //if (!e->is_subeditor) {
    if (!e->is_below) {
      //'T' corner = w or right top corner = k
      buf = fmt::format("\x1b[{};{}H", e->top_margin - 1, e->left_margin + e->screencols+1); 
      ab.append(buf);
      if (e->left_margin + e->screencols > screencols - 4) ab.append("\x1b[37;1mk"); //draw corner
      else ab.append("\x1b[37;1mw");
    }
    //exit line drawing mode
    ab.append("\x1b(B");
  }
  ab.append("\x1b[?25h", 6); //shows the cursor
  ab.append("\x1b[0m"); //or else subsequent editors are bold
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

int Session::getWindowSize(void) {

  // ioctl(), TIOCGWINXZ and struct windsize come from <sys/ioctl.h>
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    screencols = ws.ws_col;
    screenlines = ws.ws_row;

    return 0;
  }
}

// right now doesn't include get_preview
// only matters if in navigation mode (editor_mode == false)
void Session::moveDivider(int pct) {
  // note below only necessary if window resized or font size changed
  textlines = screenlines - 2 - TOP_MARGIN;

  if (pct == 100) divider = 1;
  else divider = screencols - pct * screencols/100;
  totaleditorcols = screencols - divider - 2; //? OUTLINE MARGINS?

  eraseScreenRedrawLines();

  if (divider > 10) {   //////////////////////////////////////////////////////
    refreshOrgScreen();
    drawOrgStatusBar();
  }

  if (editor_mode) {
    positionEditors();
    eraseRightScreen(); //erases editor area + statusbar + msg
    drawEditors();
    Editor::editorSetMessage("rows: %d  cols: %d  divider: %d", screenlines, screencols, divider);
  } else {
    showOrgMessage("rows: %d  cols: %d  divider: %d", screenlines, screencols, divider);
    if (org.view == TASK && org.mode != NO_ROWS)
      drawPreviewWindow(org.rows.at(org.fr).id); //get_preview
  }
  /*
  if (org.view == TASK && org.mode != NO_ROWS && !editor_mode)
    drawPreviewWindow(org.rows.at(org.fr).id); //get_preview
  */

  returnCursor();
}

void Session::returnCursor() {
  std::string ab;
  char buf[32];

  if (editor_mode) {
  // the lines below position the cursor where it should go
    if (p->mode != COMMAND_LINE){
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", p->cy + TOP_MARGIN + 1, p->cx + p->left_margin + 1); //03022019
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", p->cy + p->top_margin, p->cx + p->left_margin + p->left_margin_offset + 1); //03022019
      ab.append(buf, strlen(buf));
    } else { //E.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", textlines + TOP_MARGIN + 2, p->command_line.size() + divider + 2); 
      ab.append(buf, strlen(buf));
      ab.append("\x1b[?25h"); // show cursor
    }
  } else {
    if (org.mode == ADD_CHANGE_FILTER){
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", org.cy + TOP_MARGIN + 1, divider + 1); 
      ab.append(buf, strlen(buf));
    } else if (org.mode == FIND) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;34m>", org.cy + TOP_MARGIN + 1, LEFT_MARGIN); //blue
      ab.append(buf, strlen(buf));
    } else if (org.mode != COMMAND_LINE) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;31m>", org.cy + TOP_MARGIN + 1, LEFT_MARGIN);
      ab.append(buf, strlen(buf));
      // below restores the cursor position based on org.cx and org.cy + margin
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", org.cy + TOP_MARGIN + 1, org.cx + LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      //ab.append("\x1b[?25h", 6); // show cursor 
  // no 'caret' if in COMMAND_LINE and want to move the cursor to the message line
    } else { //org.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", textlines + 2 + TOP_MARGIN, org.command_line.size() + LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      //ab.append("\x1b[?25h", 6); // show cursor 
    }
  }
  ab.append("\x1b[0m"); //return background to normal
  ab.append("\x1b[?25h"); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

// would replace outlineDrawStatusBar
// not sure whether in Organizer or Session makes more sense
void Session::drawOrgStatusBar(void) {

  std::string ab;  
  int len;
  //Organizer & O = sess.O;
  /*
  so the below should 1) position the cursor on the status
  bar row and midscreen and 2) erase previous statusbar
  r -> l and then put the cursor back where it should be
  at LEFT_MARGIN
  */

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH",
                             textlines + TOP_MARGIN + 1,
                             divider,
                             textlines + TOP_MARGIN + 1,
                             1); //status bar comes right out to left margin

  ab.append(buf, strlen(buf));

  ab.append("\x1b[7m"); //switches to inverted colors
  char status[300], status0[300], rstatus[80];

  std::string s;
  switch (org.view) {
    case TASK:
      switch (org.taskview) {
        case BY_FIND:
          s =  "search"; 
          break;
        case BY_FOLDER:
          s = org.folder + "[f]";
          break;
        case BY_CONTEXT:
          s = org.context + "[c]";
          break;
        case BY_RECENT:
          s = "recent";
          break;
        case BY_JOIN:
          s = org.context + "[c] + " + org.folder + "[f]";
          break;
        case BY_KEYWORD:
          s = org.keyword + "[k]";
          break;
      }    
      break;
    case CONTEXT:
      s = "Contexts";
      break;
    case FOLDER:
      s = "Folders";
      break;
    case KEYWORD:  
      s = "Keywords";
      break;
  }

  if (!org.rows.empty()) {

    orow& row = org.rows.at(org.fr);
    // note the format is for 15 chars - 12 from substring below and "[+]" when needed
    std::string truncated_title = row.title.substr(0, 12);
    
    //if (p->dirty) truncated_title.append( "[+]"); /****this needs to be in editor class*******/

    // needs to be here because org.rows could be empty
    std::string keywords = (org.view == TASK) ? getTaskKeywords(row.id).first : ""; // see before and in switch
    //std::string keywords = "Not Looking";

    // because video is reversted [42 sets text to green and 49 undoes it
    // also [0;35;7m -> because of 7m it reverses background and foreground
    // I think the [0;7m is revert to normal and reverse video
    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s...\x1b[0;35;7m %s \x1b[0;7m %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (org.taskview == BY_FIND)  ? " - " : "",
                              (org.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, org.fr + 1, org.rows.size(), mode_text[org.mode].c_str());

    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %s  %d %d/%zu %s",
                              s.c_str(), (org.taskview == BY_FIND)  ? " - " : "",
                              (org.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, org.fr + 1, org.rows.size(), mode_text[org.mode].c_str());

  } else {

    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (org.taskview == BY_FIND)  ? " - " : "",
                              (org.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, org.rows.size(), mode_text[org.mode].c_str());
    
    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %d %d/%zu %s",
                              s.c_str(), (org.taskview == BY_FIND)  ? " - " : "",
                              (org.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, org.rows.size(), mode_text[org.mode].c_str());
  }

  int rlen = snprintf(rstatus, sizeof(rstatus), " %s",  TOSTRING(GIT_BRANCH));

  if (len > divider) {
    ab.append(status0, divider);
  } else if (len + rlen > divider) {
    ab.append(status);
    ab.append(rstatus, divider - len);
  } else {
    ab.append(status);
    //ab.append(divider - len - rlen - 1, ' '); //01192021
    ab.append(divider - len - rlen, ' ');
    ab.append(rstatus);
  }

  ab.append("\x1b[0m"); //switches back to normal formatting
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

//this is Organizer::outlinedrawRows
void Session::drawOrgRows() {
  std::string ab;
  int j, k; //to swap highlight if org.highlight[1] < org.highlight[0]
  char buf[32];
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  if (org.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);

  for (y = 0; y < textlines; y++) {
    int frr = y + org.rowoff;
    if (frr > org.rows.size() - 1) break;
    orow& row = org.rows[frr];

    // if a line is long you only draw what fits on the screen
    //below solves problem when deleting chars from a scrolled long line
    int len = (frr == org.fr) ? row.title.size() - org.coloff : row.title.size(); //can run into this problem when deleting chars from a scrolled log line
    if (len > titlecols) len = titlecols;

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    //else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    else if (row.deleted) ab.append(COLOR_1); //red (specific color depends on theme)
    if (frr == org.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    //if (row.mark) ab.append("\x1b[46m", 5); //cyan background
    if (org.marked_entries.find(row.id) != org.marked_entries.end()) ab.append("\x1b[46m", 5);

    // below - only will get visual highlighting if it's the active
    // then also deals with column offset
    if (org.mode == VISUAL && frr == org.fr) {

       // below in case org.highlight[1] < org.highlight[0]
      k = (org.highlight[1] > org.highlight[0]) ? 1 : 0;
      j =!k;
      ab.append(&(row.title[org.coloff]), org.highlight[j] - org.coloff);
      ab.append("\x1b[48;5;242m", 11);
      ab.append(&(row.title[org.highlight[j]]), org.highlight[k]
                                             - org.highlight[j]);
      ab.append("\x1b[49m", 5); // return background to normal
      ab.append(&(row.title[org.highlight[k]]), len - org.highlight[k] + org.coloff);

    } else {
        // current row is only row that is scrolled if org.coloff != 0
        ab.append(&row.title[((frr == org.fr) ? org.coloff : 0)], len);
    }

    // the spaces make it look like the whole row is highlighted
    //note len can't be greater than titlecols so always positive
    ab.append(titlecols - len + 1, ' ');

    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, org.divider - TIME_COL_WIDTH + 2); // + offset
    // believe the +2 is just to give some space from the end of long titles
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + TOP_MARGIN + 1, divider - TIME_COL_WIDTH + 2); // + offset
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append("\x1b[0m"); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::drawOrgFilters() {

  if (org.rows.empty()) return;

  std::string ab;

  char lf_ret[16];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 1);
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%dG", divider + 2); 
  ab.append(buf); 

  for (int y = 0; y < textlines; y++) {
    int frr = y + org.rowoff;
    if (frr > org.rows.size() - 1) break;

    orow& row = org.rows[frr];

    size_t len = (row.title.size() > titlecols) ? titlecols : row.title.size();

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    //? do this after everything drawn
    if (frr == org.fr) ab.append("\x1b[48;5;236m"); // 236 is a grey

    ab.append(&row.title[0], len);
    int spaces = titlecols - len; //needs to change but reveals stuff being written
    std::string s(spaces, ' '); 
    ab.append(s);
    ab.append("\x1b[0m"); // return background to normal /////
    ab.append(lf_ret);
  }
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}
void Session::drawOrgSearchRows(void) {

  std::string ab;

  if (org.rows.empty()) return;

  char buf[32];
  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  int spaces;

  for (y = 0; y < textlines; y++) {
    int frr = y + org.rowoff;
    if (frr > static_cast<int>(org.rows.size()) - 1) break;
    orow& row = org.rows[frr];
    int len;

    //if (row.star) ab.append("\x1b[1m"); //bold
    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  

    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground

    //if (fr == org.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey but gets stopped as soon as it hits search highlight

    //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";

    // I think the following blows up if there are multiple search terms hits in a line longer than org.titlecols

    if (row.title.size() <= titlecols) // we know it fits
      ab.append(row.fts_title.c_str(), row.fts_title.size());
    else {
      size_t pos = row.fts_title.find("\x1b[49m");
      if (pos < titlecols + 10) //length of highlight escape
        ab.append(row.fts_title.c_str(), titlecols + 15); // length of highlight escape + remove formatting escape
      else
        ab.append(row.title.c_str(), titlecols);
    }
    len = (row.title.size() <= titlecols) ? row.title.size() : titlecols;
    spaces = titlecols - len;
    for (int i=0; i < spaces; i++) ab.append(" ", 1);
    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, screencols/2 - TIME_COL_WIDTH + 2); //wouldn't need offset
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, divider - TIME_COL_WIDTH + 2); //wouldn't need offset
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::refreshOrgScreen(void) {

  std::string ab;
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  ab.append("\x1b[?25l"); //hides the cursor

  //char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  //if (org.view != KEYWORD) {
  if (org.mode != ADD_CHANGE_FILTER) {
    for (unsigned int j=TOP_MARGIN; j < textlines + 1; j++) {
      //snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
      //titlecols + LEFT_MARGIN + 17); 
      //ab.append(buf, strlen(buf));
      ab.append(fmt::sprintf("\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN, titlecols + LEFT_MARGIN + 17));
    }
  }
  // put cursor at upper left after erasing
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , LEFT_MARGIN + 1); // *****************
  //ab.append(buf, strlen(buf));
  ab.append(fmt::sprintf("\x1b[%d;%dH", TOP_MARGIN + 1 , LEFT_MARGIN + 1));
  write(STDOUT_FILENO, ab.c_str(), ab.size());

  if (org.mode == FIND) drawOrgSearchRows();
  else if (org.mode == ADD_CHANGE_FILTER) drawOrgFilters();
  else  drawOrgRows();

}

void Session::displayEntryInfo(Entry &e) {
  std::string s{};
  int width = totaleditorcols - 10;
  int length = textlines - 10;

  // \x1b[NC moves cursor forward by N columns
  std::string lf_ret = fmt::format("\r\n\x1b[{}C", divider + 6);
  s.append(fmt::format("id: {}{}", e.id, lf_ret));

  s.append(fmt::format("tid: {}{}", e.tid, lf_ret));
  
  std::string title = fmt::format("title: {}", e.title);
  if (title.size() > width) {
    title = title.substr(0, width - 3).append("...");
  }
  //coloring labels will take some work b/o gray background
  //s.append(fmt::format("{}title:{} {}{}", COLOR_1, "\x1b[m", title, lf_ret));
  s.append(fmt::format("{}{}", title, lf_ret));


  auto it = std::ranges::find_if(org.context_map, [&e](auto& z) {return z.second == e.context_tid;});
  s.append(fmt::format("context: {}{}", it->first, lf_ret));

  auto it2 = std::ranges::find_if(org.folder_map, [&e](auto& z) {return z.second == e.folder_tid;});
  s.append(fmt::format("folder: {}{}", it2->first, lf_ret));

  s.append(fmt::format("star: {}{}", e.star, lf_ret));
  s.append(fmt::format("deleted: {}{}", e.deleted, lf_ret));
  s.append(fmt::format("completed: {}{}",  (e.completed != "") ? true : false, lf_ret));
  s.append(fmt::format("modified: {}{}", e.modified, lf_ret));
  s.append(fmt::format("added: {}{}", e.added, lf_ret));

  s.append(fmt::format("keywords: {}", getTaskKeywords(getId()).first, lf_ret));

  std::string ab{};
  //hide the cursor
  ab.append("\x1b[?25l");
  //ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, O.divider + 6));
 
  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, divider + 7));

  //erase set number of chars on each line
  std::string erase_chars = fmt::format("\x1b[{}X", totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, divider + 7));

  ab.append(fmt::format("\x1b[2*x\x1b[{};{};{};{};48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, divider+7, TOP_MARGIN+4+length, divider+7+width));
  ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
  ab.append(s);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
  
  // display_item_info_pg needs to be updated if it is going to be used
  //if (tid) display_item_info_pg(tid); //// ***** remember to remove this guard
}
void Session::displayContainerInfo(Container &c) {

  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %d", c.id);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %d", c.tid);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", c.title.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  if (org.view == CONTEXT || org.view == FOLDER) {
    if (org.view == CONTEXT) {
      auto it = std::ranges::find_if(org.context_map, [&c](auto& z) {return z.second == c.tid;});
      if (it != org.context_map.end()) //happens if new and not synced yet
        sprintf(str,"context: %s", it->first.c_str());
      else
        sprintf(str,"context: %s", "(not synced yet)");
    } else if (org.view == FOLDER) {
      auto it = std::ranges::find_if(org.folder_map, [&c](auto& z) {return z.second == c.tid;});
      if (it != org.folder_map.end()) //happens if new and not synced yet
        sprintf(str,"folder: %s", it->first.c_str());
      else
        sprintf(str,"folder: %s", "(not synced yet)");
    }
    ab.append(str, strlen(str));
    ab.append(lf_ret, nchars);

    sprintf(str,"created: %s", c.created.c_str());
    ab.append(str, strlen(str));
    ab.append(lf_ret, nchars);
  }

  sprintf(str,"star: %s", (c.star) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (c.deleted) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", c.modified.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", c.count);
  ab.append(str, strlen(str));

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}
void Session::showOrgMessage(const char *fmt, ...) {
  char message[200];  
  std::string ab;
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  std::vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap); //free a va_list

  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << textlines + 2 + TOP_MARGIN << ";"
      //<< screencols/2 << "H" << "\x1b[1K\x1b["
      << divider << "H" << "\x1b[1K\x1b["
      << textlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab = buf.str();
  //ab.append("\x1b[0m"); //checking if necessary

  int msglen = strlen(message);
  //if (msglen > screencols/2) msglen = screencols/2;
  if (msglen > divider) msglen = divider;
  ab.append(message, msglen);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::showOrgMessage2(const std::string &s) {

  if (sess.divider < 10) return;

  std::string buf = fmt::format("\x1b[{};{}H\x1b[1K\x1b[{}1H",
                                 textlines + 2 + TOP_MARGIN,
                                 divider,
                                 textlines + 2 + TOP_MARGIN);

  if (s.length() > divider) buf.append(s, divider) ;
  else buf.append(s);

  write(STDOUT_FILENO, buf.c_str(), buf.size());
}

/* this version of update_html_file uses mkd_document
 * and only writes to the file once
 */
void Session::updateHTMLFile(std::string &&fn) {
  //std::string note;
  //if (editor_mode) note = p->editorRowsToString();
  //else note = org.outlinePreviewRowsToString();
  std::string note = readNoteIntoString(org.rows.at(org.fr).id);
  std::stringstream text;
  std::stringstream html;
  char *doc = nullptr;
  std::string title = org.rows.at(org.fr).title;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  //MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), MKD_FENCEDCODE);//11-16-2020
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, MKD_FENCEDCODE); //did something
  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";
  
  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else showOrgMessage("Couldn't lock file");
  } else showOrgMessage("Couldn't open file");

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 
  //
  mkd_cleanup(blob);
  link_id = 0;
}

char * Session::url_callback(const char *x, const int y, void *z) {
  link_id++;
  sprintf(link_text,"id=\"%d\"", link_id);
  return link_text;
}  

void Session::updateHTMLCodeFile(std::string &&fn) {
  //std::string note;
  std::ofstream myfile;
  //note = org.outlinePreviewRowsToString();
  std::string note = readNoteIntoString(org.rows.at(org.fr).id);
  myfile.open("code_file");
  myfile << note;
  myfile.close();

  std::stringstream html;
  std::string line;
  int tid = getFolderTid(org.rows.at(org.fr).id);
  ipstream highlight(fmt::format("highlight code_file --out-format=html "
                             "--style=gruvbox-dark-hard-slz --syntax={}",
                             (tid == 18) ? "cpp" : "go"));

  while(getline(highlight, line)) { html << line << '\n';}
 
  int fd;
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    } else showOrgMessage("Couldn't lock file");
  } else showOrgMessage("Couldn't open file");
}

void Session::drawPreviewWindow(int id) { //get_preview

  if (org.taskview != BY_FIND) drawPreviewText();
  else  drawSearchPreview();
  drawPreviewBox();

  if (lm_browser) {
    int folder_tid = getFolderTid(org.rows.at(org.fr).id);
    if (!(folder_tid == 18 || folder_tid == 14)) updateHTMLFile("assets/" + CURRENT_NOTE_FILE);
    else updateHTMLCodeFile("assets/" + CURRENT_NOTE_FILE);
  }   
}

void Session::drawPreviewText(void) { //draw_preview

  char buf[50];
  std::string ab;
  int width = totaleditorcols - 10;
  int length = textlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 6);
  //ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, divider+7, TOP_MARGIN+4+length, divider+7+width);

  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  std::string note = readNoteIntoString(org.rows.at(org.fr).id);
  if (note != "")
    ab.append(generateWWString(note, width, length, lf_ret));
  //ab.append(drawPreviewBox(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::drawSearchPreview(void) {
  //need to bring back the note with some marker around the words that
  //we search and replace or retrieve the note with the actual
  //escape codes and not worry that the word wrap will be messed up
  //but it shouldn't ever split an escaped word.  Would start
  //with escapes and go from there
 //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";
 //fts_query << "SELECT highlight(fts, ??1, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE lm_id=? AND fts MATCH '" << search_terms << "' ORDER BY rank";

  char buf[50];
  std::string ab;
  int width = totaleditorcols - 10;
  int length = textlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  // \x1b[NC moves cursor forward by N columns
  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 6);

  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  //snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;44$r\x1b[*x", 
  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, divider+7, TOP_MARGIN+4+length, divider+7+width);
  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  //std::string t = generateWWString(org.preview_rows, width, length, "\f");
  std::string note = readNoteIntoString(org.rows.at(org.fr).id);
  std::string t;
  if (note != ""){
    t = generateWWString(note, width, length, "\f"); //note the '\f'
    std::vector<std::vector<int>> wp = getNoteSearchPositions(org.rows.at(org.fr).id);
    highlight_terms_string(t, wp);
  }

  size_t p = 0;
  for (;;) {
    if (p > t.size()) break;
    p = t.find('\f', p);
    if (p == std::string::npos) break;
    t.replace(p, 1, lf_ret);
    p +=7;
   }

  ab.append(t);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

//std::string Session::drawPreviewBox(int width, int length) {
void Session::drawPreviewBox(void) {
  int width = totaleditorcols - 10;
  int length = textlines - 10;
  std::string ab;
  fmt::memory_buffer move_cursor;
  fmt::format_to(move_cursor, "\x1b[{}C", width);
  ab.append("\x1b(0"); // Enter line drawing mode
  fmt::memory_buffer buf;
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, divider + 6); 
  ab.append(buf.data(), buf.size());
  buf.clear();
  ab.append("\x1b[37;1ml"); //upper left corner
  for (int j=1; j<length; j++) { //+1
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5 + j, divider + 6); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    // x=0x78 vertical line (q=0x71 is horizontal) 37=white; 1m=bold (only need 1 m)
    ab.append("\x1b[37;1mx");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mx");
  }
  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 4 + length, divider + 6));
  ab.append("\x1b[1B");
  ab.append("\x1b[37;1mm"); //lower left corner

  move_cursor.clear();
  fmt::format_to(move_cursor, "\x1b[1D\x1b[{}B", length);
  for (int j=1; j<width+1; j++) {
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, divider + 6 + j); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    ab.append("\x1b[37;1mq");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mq");
  }
  ab.append("\x1b[37;1mj"); //lower right corner
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, divider + 7 + width); 
  ab.append(buf.data(), buf.size());
  ab.append("\x1b[37;1mk"); //upper right corner

  //exit line drawing mode
  ab.append("\x1b(B");
  ab.append("\x1b[0m");
  ab.append("\x1b[?25h", 6); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());
  //return ab;
}

  void Session::die(const char *s) {
  // write is from <unistd.h> 
  //ssize_t write(int fildes, const void *buf, size_t nbytes);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}
void Session::disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &sess.orig_termios) == -1)
    die("tcsetattr");
}

void Session::enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  std::atexit(disableRawMode);

  termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; // minimum data to receive?
  raw.c_cc[VTIME] = 1; // timeout for read will return 0 if no bytes read

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void Session::loadMeta(void) {
  std::ifstream f(META_FILE);
  std::string line;
  //static std::stringstream text;
  std::stringstream text;

  while (getline(f, line)) {
    text << line << '\n';
  }
  meta = text.str();
  f.close();
}
void Session::displayFile(void) {

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char lf_ret[20];
  int lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider); //note no + 1

  char buf[20];
  //position cursor prior to erase
  int bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, divider + 1);
  ab.append(buf, bufchars); //don't need to give length but will if change to memory_buffer

  //erase the right half of the screen
  for (int i=0; i < textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, lf_chars);
  }

  bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, divider + 2);
  ab.append(buf, bufchars);

  ab.append("\x1b[36m", 5); //this is foreground cyan - we'll see

  std::string row;
  std::string line;
  int row_num = -1;
  int line_num = 0;
  display_text.clear();
  display_text.seekg(0, std::ios::beg);
  while(std::getline(display_text, row, '\n')) {
    if (line_num > textlines - 2) break;
    row_num++;
    if (row_num < initial_file_row) continue;
    if (static_cast<int>(row.size()) < totaleditorcols) {
      ab.append(row);
      ab.append(lf_ret);
      line_num++;
      continue;
    }
    //int n = 0;
    lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 2); //indent text extra space
    int n = row.size()/(totaleditorcols - 1) + ((row.size()%(totaleditorcols - 1)) ? 1 : 0);
    for(int i=0; i<n; i++) {
      line_num++;
      if (line_num > textlines - 2) break;
      line = row.substr(0, totaleditorcols - 1);
      row.erase(0, totaleditorcols - 1);
      ab.append(line);
      ab.append(lf_ret, lf_chars);
    }
  }
  ab.append("\x1b[0m", 4);
  write(STDOUT_FILENO, ab.c_str(), ab.size()); //01012020
}

void Session::synchronize(int report_only) { //using 1 or 0

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
                  showOrgMessage("Problem converting c variable for use in calling python function");
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
              showOrgMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          showOrgMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      showOrgMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  if (report_only) showOrgMessage("Number of tasks/items that would be affected is %d", num);
  else showOrgMessage("Number of tasks/items that were affected is %d", num);
}

void Session::quitApp(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
  Py_FinalizeEx();
  //sqlite3_close(S.db); //something should probably be done here
  PQfinish(conn);
  //t0.join();
  //subscriber.close();
  //context.close(); // doesn't shut down properly if uncommented
  publisher.close(); 
  lsp_shutdown("all");
  //exit(0);
}

void Session::navigatePageHx(int direction) {
  if (page_history.size() == 1 && org.view == TASK) return;

  if (direction == PAGE_UP) {

    // if O.view!=TASK and PAGE_UP - moves back to last page
    if (org.view == TASK) { //if in a container viewa - fall through to previous TASK view page

      if (page_hx_idx == 0) page_hx_idx = page_history.size() - 1;
      else page_hx_idx--;
    }

  } else {
    if (page_hx_idx == (page_history.size() - 1)) page_hx_idx = 0;
    else page_hx_idx++;
  }

  // go into COMMAND_LINE mode
  org.mode = COMMAND_LINE;
  org.command_line = page_history.at(page_hx_idx);
  outlineProcessKeypress('\r');
  org.command_line.clear();

  // return to NORMAL mode 
  org.mode = NORMAL;
  page_history.erase(page_history.begin() + page_hx_idx);
  page_hx_idx--;
  showOrgMessage(":%s", page_history.at(page_hx_idx).c_str());
}

void Session::navigateCmdHx(int direction) {
  if (command_history.empty()) return;

  if (direction == ARROW_UP) {
    if (cmd_hx_idx == 0) cmd_hx_idx = command_history.size() - 1;
    else cmd_hx_idx--;
  } else {
    if (cmd_hx_idx == (command_history.size() - 1)) cmd_hx_idx = 0;
    else cmd_hx_idx++;
  }
  showOrgMessage(":%s", command_history.at(cmd_hx_idx).c_str());
  org.command_line = command_history.at(cmd_hx_idx);
}

/*
//  this zeromq version works but there is a problem on the ultralight
//  side -- LoadHTML doesn't seem to use the style sheet.  Will check on slack
//  if this is my mistake or intentional
void update_html_zmq(std::string &&fn) {
  //std::string note = org.outlinePreviewRowsToString();
  std::string note = readNoteIntoString(org.rows.at(org.fr).id);
  std::stringstream text;
  std::stringstream html;
  std::string title = org.rows.at(org.fr).title;
  char *doc = nullptr;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(sess.meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); 

  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";

  zmq::message_t message(html.str().size()+1);

  // probably don't need snprint to get html into message
  snprintf ((char *) message.data(), html.str().size()+1, "%s", html.str().c_str()); 

  publisher.send(message, zmq::send_flags::dontwait);

  // don't know if below is correct or necessary - I don't think so
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 

  mkd_cleanup(blob);
  link_id = 0;
}
*/

