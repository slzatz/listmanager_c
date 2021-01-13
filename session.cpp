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
