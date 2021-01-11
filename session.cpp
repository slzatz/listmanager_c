#define TIME_COL_WIDTH 18 // need this if going to have modified col

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

void Session::position_editors(void) {
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

void Session::draw_editors(void) {
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
void Session::moveDivider(int pct) {
  divider = sess.screencols - pct * sess.screencols/100;
  totaleditorcols = screencols - divider - 2; //? OUTLINE MARGINS?

  eraseScreenRedrawLines();

  position_editors();
  eraseRightScreen(); //erases editor area + statusbar + msg
  draw_editors();

  O.outlineRefreshScreen();
  O.outlineDrawStatusBar();
  O.outlineShowMessage("rows: %d  cols: %d ", sess.screenlines, sess.screencols);

  /* need to think about this
  if (O.view == TASK && O.mode != NO_ROWS && !editor_mode)
    get_preview(O.rows.at(O.fr).id);
  */

  return_cursor();
}

void Session::return_cursor() {
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
