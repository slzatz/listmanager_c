#define LEFT_MARGIN 2
#define TOP_MARGIN 1 // Editor.cpp
#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include <unistd.h>
#include "session.h"
#include "Organizer.h"
#include "Common.h"
#include <sstream>

void Organizer::outlineDrawRows(std::string& ab) {
  int j, k; //to swap highlight if O.highlight[1] < O.highlight[0]
  char buf[32];
  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  if (rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);

  for (y = 0; y < sess.textlines; y++) {
    int frr = y + rowoff;
    if (frr > rows.size() - 1) return;
    orow& row = rows[frr];

    // if a line is long you only draw what fits on the screen
    //below solves problem when deleting chars from a scrolled long line
    int len = (frr == fr) ? row.title.size() - coloff : row.title.size(); //can run into this problem when deleting chars from a scrolled log line
    if (len > titlecols) len = titlecols;

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    //else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    else if (row.deleted) ab.append(COLOR_1); //red (specific color depends on theme)
    if (frr == fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    //if (row.mark) ab.append("\x1b[46m", 5); //cyan background
    if (marked_entries.find(row.id) != marked_entries.end()) ab.append("\x1b[46m", 5);

    // below - only will get visual highlighting if it's the active
    // then also deals with column offset
    if (mode == VISUAL && frr == fr) {

       // below in case O.highlight[1] < O.highlight[0]
      k = (highlight[1] > highlight[0]) ? 1 : 0;
      j =!k;
      ab.append(&(row.title[coloff]), highlight[j] - coloff);
      ab.append("\x1b[48;5;242m", 11);
      ab.append(&(row.title[highlight[j]]), highlight[k]
                                             - highlight[j]);
      ab.append("\x1b[49m", 5); // return background to normal
      ab.append(&(row.title[highlight[k]]), len - highlight[k] + coloff);

    } else {
        // current row is only row that is scrolled if O.coloff != 0
        ab.append(&row.title[((frr == fr) ? coloff : 0)], len);
    }

    // the spaces make it look like the whole row is highlighted
    //note len can't be greater than titlecols so always positive
    ab.append(titlecols - len + 1, ' ');

    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, O.divider - TIME_COL_WIDTH + 2); // + offset
    // believe the +2 is just to give some space from the end of long titles
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + TOP_MARGIN + 1, sess.divider - TIME_COL_WIDTH + 2); // + offset
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append("\x1b[0m"); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
}

void Organizer::outlineDrawFilters(std::string& ab) {

  if (rows.empty()) return;

  char lf_ret[16];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1);
  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%dG", sess.divider + 2); 
  ab.append(buf); 

  for (int y = 0; y < sess.textlines; y++) {
    int frr = y + rowoff;
    if (frr > rows.size() - 1) return;

    orow& row = rows[frr];

    size_t len = (row.title.size() > titlecols) ? titlecols : row.title.size();

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    //? do this after everything drawn
    if (frr == fr) ab.append("\x1b[48;5;236m"); // 236 is a grey

    ab.append(&row.title[0], len);
    int spaces = titlecols - len; //needs to change but reveals stuff being written
    std::string s(spaces, ' '); 
    ab.append(s);
    ab.append("\x1b[0m"); // return background to normal /////
    ab.append(lf_ret);
  }
}

void Organizer::outlineDrawSearchRows(std::string& ab) {

  if (rows.empty()) return;

  char buf[32];
  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);
  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  int spaces;

  for (y = 0; y < sess.textlines; y++) {
    int frr = y + rowoff;
    if (frr > static_cast<int>(rows.size()) - 1) return;
    orow& row = rows[frr];
    int len;

    //if (row.star) ab.append("\x1b[1m"); //bold
    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  

    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground

    //if (fr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey but gets stopped as soon as it hits search highlight

    //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";

    // I think the following blows up if there are multiple search terms hits in a line longer than O.titlecols

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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, sess.divider - TIME_COL_WIDTH + 2); //wouldn't need offset
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
}

void Organizer::outlineDrawStatusBar(void) {

  std::string ab;  
  int len;
  /*
  so the below should 1) position the cursor on the status
  bar row and midscreen and 2) erase previous statusbar
  r -> l and then put the cursor back where it should be
  at LEFT_MARGIN
  */

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K\x1b[%d;%dH",
                             sess.textlines + TOP_MARGIN + 1,
                             sess.divider,
                             sess.textlines + TOP_MARGIN + 1,
                             1); //status bar comes right out to left margin

  ab.append(buf, strlen(buf));

  ab.append("\x1b[7m"); //switches to inverted colors
  char status[300], status0[300], rstatus[80];

  std::string s;
  switch (view) {
    case TASK:
      switch (taskview) {
        case BY_FIND:
          s =  "search"; 
          break;
        case BY_FOLDER:
          s = folder + "[f]";
          break;
        case BY_CONTEXT:
          s = context + "[c]";
          break;
        case BY_RECENT:
          s = "recent";
          break;
        case BY_JOIN:
          s = context + "[c] + " + folder + "[f]";
          break;
        case BY_KEYWORD:
          s = keyword + "[k]";
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

  if (!rows.empty()) {

    orow& row = rows.at(fr);
    // note the format is for 15 chars - 12 from substring below and "[+]" when needed
    std::string truncated_title = row.title.substr(0, 12);
    
    //if (p->dirty) truncated_title.append( "[+]"); /****this needs to be in editor class*******/

    // needs to be here because O.rows could be empty
    //std::string keywords = (view == TASK) ? get_task_keywords(row.id).first : ""; // see before and in switch
    std::string keywords = "Not Looking";

    // because video is reversted [42 sets text to green and 49 undoes it
    // also [0;35;7m -> because of 7m it reverses background and foreground
    // I think the [0;7m is revert to normal and reverse video
    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s...\x1b[0;35;7m %s \x1b[0;7m %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (taskview == BY_FIND)  ? " - " : "",
                              (taskview == BY_FIND) ? sess.fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, fr + 1, rows.size(), mode_text[mode].c_str());

    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %s  %d %d/%zu %s",
                              s.c_str(), (taskview == BY_FIND)  ? " - " : "",
                              (taskview == BY_FIND) ? sess.fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, fr + 1, rows.size(), mode_text[mode].c_str());

  } else {

    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (taskview == BY_FIND)  ? " - " : "",
                              (taskview == BY_FIND) ? sess.fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, rows.size(), mode_text[mode].c_str());
    
    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %d %d/%zu %s",
                              s.c_str(), (taskview == BY_FIND)  ? " - " : "",
                              (taskview == BY_FIND) ? sess.fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, rows.size(), mode_text[mode].c_str());
  }

  int rlen = snprintf(rstatus, sizeof(rstatus), " %s",  TOSTRING(GIT_BRANCH));

  if (len > sess.divider) {
    ab.append(status0, sess.divider);
  } else if (len + rlen > sess.divider) {
    ab.append(status);
    ab.append(rstatus, sess.divider - len);
  } else {
    ab.append(status);
    ab.append(sess.divider - len - rlen, ' ');
    ab.append(rstatus);
  }

  ab.append("\x1b[0m"); //switches back to normal formatting
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Organizer::outlineRefreshScreen(void) {

  std::string ab;
  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  //if (O.view != KEYWORD) {
  if (mode != ADD_CHANGE_FILTER) {
    for (unsigned int j=TOP_MARGIN; j < sess.textlines + 1; j++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
      titlecols + LEFT_MARGIN + 17); 
      ab.append(buf, strlen(buf));
    }
  }
  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , LEFT_MARGIN + 1); // *****************
  ab.append(buf, strlen(buf));

  if (mode == FIND) outlineDrawSearchRows(ab);
  else if (mode == ADD_CHANGE_FILTER) outlineDrawFilters(ab);
  else  outlineDrawRows(ab);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

/*va_list, va_start(), and va_end() come from <stdarg.h> and vsnprintf() is
from <stdio.h> and time() is from <time.h>.  stdarg.h allows functions to accept a
variable number of arguments and are declared with an ellipsis in place of the last parameter.*/

// this should probably be in session since doesn't use any Organizer vars
void Organizer::outlineShowMessage(const char *fmt, ...) {
  char message[100];  
  std::string ab;
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  std::vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap); //free a va_list

  std::stringstream buf;

  // Erase from mid-screen to the left and then place cursor all the way left
  buf << "\x1b[" << sess.textlines + 2 + TOP_MARGIN << ";"
      //<< screencols/2 << "H" << "\x1b[1K\x1b["
      << sess.divider << "H" << "\x1b[1K\x1b["
      << sess.textlines + 2 + TOP_MARGIN << ";" << 1 << "H";

  ab = buf.str();
  //ab.append("\x1b[0m"); //checking if necessary

  int msglen = strlen(message);
  //if (msglen > screencols/2) msglen = screencols/2;
  if (msglen > sess.divider) msglen = sess.divider;
  ab.append(message, msglen);
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Organizer::outlineShowMessage2(const std::string &s) {
  std::string buf = fmt::format("\x1b[{};{}H\x1b[1K\x1b[{}1H",
                                 sess.textlines + 2 + TOP_MARGIN,
                                 sess.divider,
                                 sess.textlines + 2 + TOP_MARGIN);

  if (s.length() > sess.divider) buf.append(s, sess.divider) ;
  else buf.append(s);

  write(STDOUT_FILENO, buf.c_str(), buf.size());
}

