#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#include "session.h"
#include "Common.h"
#include <string>
#include <fmt/core.h>
#include <fmt/format.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define TOP_MARGIN 1
#define LEFT_MARGIN 2

Session sess = Session(); //global; extern Session sess in session.h

void Session::eraseScreenRedrawLines(void) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // Erase the screen
  char buf[32];
  write(STDOUT_FILENO, "\x1b(0", 3); // Enter line drawing mode
  for (int j = 1; j < screenlines + 1; j++) {

    // First vertical line
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + j, divider - TIME_COL_WIDTH + 1); //don't think need offset
    write(STDOUT_FILENO, buf, strlen(buf));
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
    write(STDOUT_FILENO, "\x1b[37;1mx", 8);

    // Second vertical line
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

  // draw first column's 'T' corner
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN, divider - TIME_COL_WIDTH + 1); //may not need offset
  write(STDOUT_FILENO, buf, strlen(buf));
  write(STDOUT_FILENO, "\x1b[37;1mw", 8); //'T' corner

  // draw next column's 'T' corner
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
  //note O.divider -1 would take out center divider line and don't want to do that
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
  //for (int j=1; j<screencols/2; j++) {
  //for (int j=1; j<O.divider; j++) {
  for (int j=1; j<totaleditorcols+1; j++) { //added +1 0906/2020
    //fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN, O.divider + j);
    std::string buf2 = fmt::format("\x1b[{};{}H", TOP_MARGIN, divider + j);
    ab.append(buf2);
    // below x = 0x78 vertical line (q = 0x71 is horizontal) 37 = white; 1m = bold (note
    // only need one 'm'
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

  divider = screencols - pct * screencols/100;
  totaleditorcols = screencols - divider - 2; //? OUTLINE MARGINS?

  eraseScreenRedrawLines();

  if (editor_mode) {
    positionEditors();
    eraseRightScreen(); //erases editor area + statusbar + msg
    drawEditors();
  }

  O.outlineRefreshScreen();
  //O.outlineDrawStatusBar();
  drawOrgStatusBar();

  if (editor_mode)
      Editor::editorSetMessage("rows: %d  cols: %d ", screenlines, screencols);
  else 
      O.outlineShowMessage("rows: %d  cols: %d ", screenlines, screencols);

  /* need to think about this
  if (O.view == TASK && O.mode != NO_ROWS && !editor_mode)
    get_preview(O.rows.at(O.fr).id);
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
    if (O.mode == ADD_CHANGE_FILTER){
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, divider + 1); 
      ab.append(buf, strlen(buf));
    } else if (O.mode == FIND) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;34m>", O.cy + TOP_MARGIN + 1, LEFT_MARGIN); //blue
      ab.append(buf, strlen(buf));
    } else if (O.mode != COMMAND_LINE) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1;31m>", O.cy + TOP_MARGIN + 1, LEFT_MARGIN);
      ab.append(buf, strlen(buf));
      // below restores the cursor position based on O.cx and O.cy + margin
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH", O.cy + TOP_MARGIN + 1, O.cx + LEFT_MARGIN + 1); /// ****
      ab.append(buf, strlen(buf));
      //ab.append("\x1b[?25h", 6); // show cursor 
  // no 'caret' if in COMMAND_LINE and want to move the cursor to the message line
    } else { //O.mode == COMMAND_LINE
      snprintf(buf, sizeof(buf), "\x1b[%d;%ldH", textlines + 2 + TOP_MARGIN, O.command_line.size() + LEFT_MARGIN); /// ****
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
  Organizer & O = sess.O;
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
  switch (O.view) {
    case TASK:
      switch (O.taskview) {
        case BY_FIND:
          s =  "search"; 
          break;
        case BY_FOLDER:
          s = O.folder + "[f]";
          break;
        case BY_CONTEXT:
          s = O.context + "[c]";
          break;
        case BY_RECENT:
          s = "recent";
          break;
        case BY_JOIN:
          s = O.context + "[c] + " + O.folder + "[f]";
          break;
        case BY_KEYWORD:
          s = O.keyword + "[k]";
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

  if (!O.rows.empty()) {

    orow& row = O.rows.at(O.fr);
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
                              s.c_str(), (O.taskview == BY_FIND)  ? " - " : "",
                              (O.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());

    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %s  %d %d/%zu %s",
                              s.c_str(), (O.taskview == BY_FIND)  ? " - " : "",
                              (O.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              truncated_title.c_str(), keywords.c_str(), row.id, O.fr + 1, O.rows.size(), mode_text[O.mode].c_str());

  } else {

    snprintf(status, sizeof(status),
                              "\x1b[1m%s%s%s\x1b[0;7m %.15s... %d %d/%zu \x1b[1;42m%s\x1b[49m",
                              s.c_str(), (O.taskview == BY_FIND)  ? " - " : "",
                              (O.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
    
    // klugy way of finding length of string without the escape characters
    len = snprintf(status0, sizeof(status0),
                              "%s%s%s %.15s... %d %d/%zu %s",
                              s.c_str(), (O.taskview == BY_FIND)  ? " - " : "",
                              (O.taskview == BY_FIND) ? fts_search_terms.c_str() : "\0",
                              "     No Results   ", -1, 0, O.rows.size(), mode_text[O.mode].c_str());
  }

  int rlen = snprintf(rstatus, sizeof(rstatus), " %s",  TOSTRING(GIT_BRANCH));

  if (len > divider) {
    ab.append(status0, divider);
  } else if (len + rlen > divider) {
    ab.append(status);
    ab.append(rstatus, divider - len);
  } else {
    ab.append(status);
    ab.append(divider - len - rlen - 1, ' ');
    ab.append(rstatus);
  }

  ab.append("\x1b[0m"); //switches back to normal formatting
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Session::getNote(int id) {
  if (id ==-1) return; // id given to new and unsaved entries

  Query q(db, "SELECT note FROM task WHERE id = {}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    O.outlineShowMessage3("Problem retrieving note from itemi {}: {}", id, res);
    return;
  }
  std::string note = q.column_text(0);
  std::erase(note, '\r'); //c++20
  std::stringstream sNote(note);
  std::string s;
  while (getline(sNote, s, '\n')) {
    p->editorInsertRow(p->rows.size(), s);
  }
  p->dirty = 0; //assume editorInsertRow increments dirty so this needed
  if (!p->linked_editor) return;

  p->linked_editor->rows = std::vector<std::string>{" "};

  /* below works but don't think we want to store output_window
  db.query("SELECT subnote FROM task WHERE id = {}", id);
  db.callback = note_callback;
  db.pArg = p->linked_editor;
  run_sql();
  */

}
void Session::generateContextMap(void) {
  // note it's tid because it's sqlite
  Query q(db, "SELECT tid, title FROM context;"); 
  /*
  if (q.result != SQLITE_OK) {
    outlineShowMessage3("Problem in 'map_context_titles'; result code: {}", q.result);
    return;
  }
  */

  while (q.step() == SQLITE_ROW) {
    O.context_map[q.column_text(1)] = q.column_int(0);
  }
}

void Session::generateFolderMap(void) {

  // note it's tid because it's sqlite
  Query q(db, "SELECT tid,title FROM folder;"); 
  /*
  if (q.result != SQLITE_OK) {
    outlineShowMessage3("Problem in 'map_folder_titles'; result code: {}", q.result);
    return;
  }
  */

  while (q.step() == SQLITE_ROW) {
    O.folder_map[q.column_text(1)] = q.column_int(0);
  }
}

//this is Organizer::outlinedrawRows
void Session::drawOrgRows(std::string& ab) {
  int j, k; //to swap highlight if O.highlight[1] < O.highlight[0]
  char buf[32];
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  if (O.rows.empty()) return;

  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);

  for (y = 0; y < textlines; y++) {
    int frr = y + O.rowoff;
    if (frr > O.rows.size() - 1) return;
    orow& row = O.rows[frr];

    // if a line is long you only draw what fits on the screen
    //below solves problem when deleting chars from a scrolled long line
    int len = (frr == O.fr) ? row.title.size() - O.coloff : row.title.size(); //can run into this problem when deleting chars from a scrolled log line
    if (len > titlecols) len = titlecols;

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    if (row.completed && row.deleted) ab.append("\x1b[32m", 5); //green foreground
    else if (row.completed) ab.append("\x1b[33m", 5); //yellow foreground
    //else if (row.deleted) ab.append("\x1b[31m", 5); //red foreground
    else if (row.deleted) ab.append(COLOR_1); //red (specific color depends on theme)
    if (frr == O.fr) ab.append("\x1b[48;5;236m", 11); // 236 is a grey
    if (row.dirty) ab.append("\x1b[41m", 5); //red background
    //if (row.mark) ab.append("\x1b[46m", 5); //cyan background
    if (O.marked_entries.find(row.id) != O.marked_entries.end()) ab.append("\x1b[46m", 5);

    // below - only will get visual highlighting if it's the active
    // then also deals with column offset
    if (O.mode == VISUAL && frr == O.fr) {

       // below in case O.highlight[1] < O.highlight[0]
      k = (O.highlight[1] > O.highlight[0]) ? 1 : 0;
      j =!k;
      ab.append(&(row.title[O.coloff]), O.highlight[j] - O.coloff);
      ab.append("\x1b[48;5;242m", 11);
      ab.append(&(row.title[O.highlight[j]]), O.highlight[k]
                                             - O.highlight[j]);
      ab.append("\x1b[49m", 5); // return background to normal
      ab.append(&(row.title[O.highlight[k]]), len - O.highlight[k] + O.coloff);

    } else {
        // current row is only row that is scrolled if O.coloff != 0
        ab.append(&row.title[((frr == O.fr) ? O.coloff : 0)], len);
    }

    // the spaces make it look like the whole row is highlighted
    //note len can't be greater than titlecols so always positive
    ab.append(titlecols - len + 1, ' ');

    //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, O.divider - TIME_COL_WIDTH + 2); // + offset
    // believe the +2 is just to give some space from the end of long titles
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + TOP_MARGIN + 1, divider - TIME_COL_WIDTH + 2); // + offset
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append("\x1b[0m"); // return background to normal ////////////////////////////////
    ab.append(lf_ret, nchars);
  }
}

void Session::drawOrgFilters(std::string& ab) {

  if (O.rows.empty()) return;

  char lf_ret[16];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", divider + 1);
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%dG", divider + 2); 
  ab.append(buf); 

  for (int y = 0; y < textlines; y++) {
    int frr = y + O.rowoff;
    if (frr > O.rows.size() - 1) return;

    orow& row = O.rows[frr];

    size_t len = (row.title.size() > titlecols) ? titlecols : row.title.size();

    if (row.star) {
      ab.append("\x1b[1m"); //bold
      ab.append("\x1b[1;36m");
    }  
    //? do this after everything drawn
    if (frr == O.fr) ab.append("\x1b[48;5;236m"); // 236 is a grey

    ab.append(&row.title[0], len);
    int spaces = titlecols - len; //needs to change but reveals stuff being written
    std::string s(spaces, ' '); 
    ab.append(s);
    ab.append("\x1b[0m"); // return background to normal /////
    ab.append(lf_ret);
  }
}
void Session::drawOrgSearchRows(std::string& ab) {

  if (O.rows.empty()) return;

  char buf[32];
  unsigned int y;
  char lf_ret[16];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", LEFT_MARGIN);
  int titlecols = divider - TIME_COL_WIDTH - LEFT_MARGIN;

  int spaces;

  for (y = 0; y < textlines; y++) {
    int frr = y + O.rowoff;
    if (frr > static_cast<int>(O.rows.size()) - 1) return;
    orow& row = O.rows[frr];
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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 2, divider - TIME_COL_WIDTH + 2); //wouldn't need offset
    ab.append("\x1b[0m", 4); // return background to normal
    ab.append(buf, strlen(buf));
    ab.append(row.modified);
    ab.append(lf_ret, nchars);
    //abAppend(ab, "\x1b[0m", 4); // return background to normal
  }
}

void Session::refreshOrgScreen(void) {

  std::string ab;
  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char buf[20];

  //Below erase screen from middle to left - `1K` below is cursor to left erasing
  //Now erases time/sort column (+ 17 in line below)
  //if (O.view != KEYWORD) {
  if (O.mode != ADD_CHANGE_FILTER) {
    for (unsigned int j=TOP_MARGIN; j < sess.textlines + 1; j++) {
      snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1K", j + TOP_MARGIN,
      titlecols + LEFT_MARGIN + 17); 
      ab.append(buf, strlen(buf));
    }
  }
  // put cursor at upper left after erasing
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1 , LEFT_MARGIN + 1); // *****************
  ab.append(buf, strlen(buf));

  if (O.mode == FIND) drawOrgSearchRows(ab);
  else if (O.mode == ADD_CHANGE_FILTER) drawOrgFilters(ab);
  else  drawOrgRows(ab);

  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

