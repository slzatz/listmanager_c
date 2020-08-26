#include "listmanager.h"
#include "Editor.h"

/* Basic Editor actions */
void Editor::editorInsertReturn(void) { // right now only used for editor->INSERT mode->'\r'
  if (rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
    editorInsertRow(0, std::string());
    fc = 0;
    fr = 1;
    return;
  }
    
  std::string& current_row = rows.at(fr);
  std::string new_row1(current_row.begin(), current_row.begin() + fc);
  std::string new_row2(current_row.begin() + fc, current_row.end());

  int indent = (smartindent) ? editorIndentAmount(fr) : 0;

  fr++;
  current_row = new_row1;
  rows.insert(rows.begin() + fr, new_row2);

  fc = 0;
  for (int j=0; j < indent; j++) editorInsertChar(' ');
}

void Editor::editorInsertRow(int fr, std::string s) {
  auto pos = rows.begin() + fr;
  rows.insert(pos, s);
  dirty++;
}

int Editor::editorIndentAmount(int r) {
  if (rows.empty()) return 0;
  int i;
  std::string& row = rows.at(r);

  for (i=0; i<row.size(); i++) {
    if (row[i] != ' ') break;}

  return i;
}

void Editor::editorInsertNewline(int direction) {
  /* note this func does position fc and fr*/
  if (rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
    return;
  }

  if (fr == 0 && direction == 0) { // this is for 'O'
    editorInsertRow(0, std::string());
    fc = 0;
    return;
  }
    
  int indent = (smartindent) ? editorIndentAmount(fr) : 0;

  std::string spaces;
  for (int j=0; j<indent; j++) {
      spaces.push_back(' ');
  }
  fc = indent;

  fr += direction;
  editorInsertRow(fr, spaces);
}

void Editor::editorDelChar(void) {
  if (rows.empty()) return; // creation of NO_ROWS may make this unnecessary
  std::string& row = rows.at(fr);
  if (row.empty() || fc > static_cast<int>(row.size()) - 1) return;
  row.erase(row.begin() + fc);
  dirty++;
}

void Editor::editorMoveCursorBOL(void) {
  if (rows.empty()) return;
  fc = 0;
}

void Editor::editorMoveCursorEOL(void) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);
  if (row.size()) fc = row.size() - 1;
}
//
// normal mode 'e'
void Editor::editorMoveEndWord(void) {

if (rows.empty()) return;

if (rows.at(fr).empty() || fc == rows.at(fr).size() - 1) {
  if (fr+1 > rows.size() -1) {return;}
  else {fr++; fc = 0;}
} else fc++;

  int r = fr;
  int c = fc;
  int pos;
  std::string delimiters = " <>,.;?:()[]{}&#~'";
  std::string delimiters_without_space = "<>,.;?:()[]{}&#~'";

  for(;;) {

    if (r > rows.size() - 1) {return;}

    std::string &row = rows.at(r);

    if (row.empty()) {r++; c = 0; continue;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric
      if (c == row.size()-1 || ispunct(row.at(c+1))) {fc = c; fr = r; return;}

      pos = row.find_first_of(delimiters, c);
      if (pos == std::string::npos) {fc = row.size() - 1; return;}
      else {fr = r; fc = pos - 1; return;}

    // we started on punct or space
    } else {
      if (row.at(c) == ' ') {
        if (c == row.size() - 1) {r++; c = 0; continue;}
        else {c++; continue;}

      } else {
        pos = row.find_first_not_of(delimiters_without_space, c);
        if (pos != std::string::npos) {fc = pos - 1; return;}
        else {fc = row.size() -1; return;}
      }
    }
  }
}

void Editor::editorMoveNextWord(void) {

  if (rows.empty()) return;

  // like with 'b', we are incrementing by one to start
  if (fc == rows.at(fr).size() - 1) {
      if (fr+1 > rows.size() - 1) {return;}
      fr++;
      fc = 0;
  } else fc++;

  int r = fr;
  int c = fc;
  int pos;
  std::string delimiters = " <>,.;?:()[]{}&#~'";
  std::string delimiters_without_space = "<>,.;?:()[]{}&#~'";

  for(;;) {

    if (r > rows.size() - 1) {return;}

    std::string &row = rows.at(r);

    if (row.empty()) {fr = r; fc = 0; return;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric

        if (c == 0) {fc = 0; fr = r; return;}
        if (ispunct(row.at(c-1))) {fc =c; fr = r; return;}

      pos = row.find_first_of(delimiters, c);
      // found punctuation or space after starting on alphanumeric
      if (pos != std::string::npos) {
          if (row.at(pos) == ' ') {c = pos; continue;}
          else {fc = pos; return;}
      }

      //did not find punctuation or space after starting on alphanumeric
      r++; c = 0; continue;
    }

    // we started on punctuation or space
    if (row.at(c) == ' ') {
       pos = row.find_first_not_of(' ', c);
       if (pos == std::string::npos) {r++; c = 0; continue;}
       else {fc = pos; fr = r; return;}
    // we started on punctuation and need to find first alpha
    } else {
        if (isalnum(row.at(c-1))) {fc = c; return;}
        c++;
        if (c > row.size() - 1) {r++; c=0; continue;}
        pos = row.find_first_not_of(delimiters_without_space, c); //this is equiv of searching for space or alphanumeric
        if (pos != std::string::npos) {
            if (row.at(pos) == ' ') {c = pos; continue;}
            else {fc = pos; return;}
        }
        // this just says that if you couldn't find a space or alpha (you're sitting on punctuation) go to next line
        r++; c = 0;
    }
  }
}

// normal mode 'b'
// not well tested but seems identical to vim including hanlding of runs of punctuation
void Editor::editorMoveBeginningWord(void) {
  if (rows.empty()) return;
  if (fr == 0 && fc == 0) return;

   //move back one character

  if (fc == 0) { // this should also cover an empty row
    fr--;
    fc = rows.at(fr).size() - 1;
    if (fc == -1) {fc = 0; return;}
  } else fc--;

  int r = fr;
  int c = fc;
  int pos;
  std::string delimiters = " ,.;?:()[]{}&#~'";

  for(;;) {

    std::string &row = rows.at(r);

    if (row.empty()) {fr = r; fc = 0; return;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric
      pos = row.find_last_of(delimiters, c);
      if (pos != std::string::npos) {fc = pos + 1; fr = r; return;}
      else {fc = 0; fr = r; return;}
    }

   // If we get here we started on punctuation or space
    if (row.at(c) == ' ') {
      if (c == 0) {
        r--;
        c = rows.at(r).size() - 1;
        if (c == -1) {fc = 0; fr = r; return;}
      } else {c--; continue;}
    }

    if (c == 0 || row.at(c-1) == ' ' || isalnum(row.at(c-1))) {fc = c; fr = r; return;}

    c--;
   }
}

// decorates, undecorates, redecorates
void Editor::editorDecorateWord(int c) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);
  if (row.at(fc) == ' ') return;

  //find beginning of word
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  //find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (row.substr(beg, 2) == "**") {
    row.erase(beg, 2);
    end -= 4;
    row.erase(end, 2);
    fc -=2;
    if (c == CTRL_KEY('b')) return;
  } else if (row.at(beg) == '*') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
    if (c == CTRL_KEY('i')) return;
  } else if (row.at(beg) == '`') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
    if (c == CTRL_KEY('e')) return;
  }

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  switch(c) {
    case CTRL_KEY('b'):
      row.insert(beg, "**");
      row.insert(end+2, "**"); //
      fc +=2;
      return;
    case CTRL_KEY('i'):
      row.insert(beg, "*");
      row.insert(end+1 , "*");
      fc++;
      return;
    case CTRL_KEY('e'):
      row.insert(beg, "`");
      row.insert(end+1 , "`");
      fc++;
      return;
  }
}

void Editor::editorCreateSnapshot(void) {
  if (rows.empty()) return; //don't create snapshot if there is no text
  prev_rows = rows;
}
void Editor::editorRestoreSnapshot(void) {
    if (prev_rows.empty()) return;
    rows = prev_rows;
}


// only decorates which I think makes sense
void Editor::editorDecorateVisual(int c) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);

  int beg = highlight[0];
  int end = highlight[1] + 1;

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  switch(c) {
    case CTRL_KEY('b'):
      row.insert(beg, "**");
      row.insert(end+2, "**"); //
      fc += 2;
      return;
    case CTRL_KEY('i'):
      row.insert(beg, "*");
      row.insert(end+1 , "*");
      fc++;
      return;
    case CTRL_KEY('e'):
      row.insert(beg, "`");
      row.insert(end+1 , "`");
      fc++;
      return;
  }
}
void Editor::editorSetMessage(const char *fmt, ...) {
  va_list ap; //type for iterating arguments
  va_start(ap, fmt); // start iterating arguments with a va_list


  /* vsnprint from <stdio.h> writes to the character string str
     vsnprint(char *str,size_t size, const char *format, va_list ap)*/

  vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap); //free a va_list
}

void Editor::editorSpellCheck(void) {

  if (rows.empty()) return;

  pos_mispelled_words.clear();

  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  //auto sugs = std::vector<std::string>();
  auto dict = nuspell::Dictionary::load_from_path(path);

  std::string delimiters = " -<>!$,.;?:()[]{}&#~^";

  for (int n=first_visible_row; n<=last_visible_row; n++) {
    int end = -1;
    int start;
    std::string &row = rows.at(n);
    if (row.empty()) continue;
    for (;;) {
      if (end >= static_cast<int>(row.size()) - 1) break;
      start = end + 1;
      end = row.find_first_of(delimiters, start);
      if (end == std::string::npos)
        end = row.size();

      if (!dict.spell(row.substr(start, end-start))) {
        pos_mispelled_words.push_back(std::make_pair(n, start)); 
        editorHighlightWord(n, start, end-start);
      }
    }
  }

  //reposition the cursor back to where it belongs
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + TOP_MARGIN + 1, cx + left_margin + 1); //03022019
  write(STDOUT_FILENO, buf, strlen(buf));
}


void Editor::editorHighlightWord(int r, int c, int len) {
  std::string &row = rows.at(r);
  int x = editorGetScreenXFromRowColWW(r, c) + left_margin + 1;
  int y = editorGetScreenYFromRowColWW(r, c) + TOP_MARGIN + 1 - line_offset; // added line offset 12-25-2019
  std::stringstream s;
  s << "\x1b[" << y << ";" << x << "H" << "\x1b[48;5;31m"
    << row.substr(c, len)
    << "\x1b[0m";

  write(STDOUT_FILENO, s.str().c_str(), s.str().size());
}

void Editor::editorReadFileIntoNote(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  rows.clear();
  fr = fc = cy = cx = line_offset = prev_line_offset = first_visible_row = last_visible_row = 0;

  while (getline(f, line)) {
    //replace(line.begin(), line.end(), '\t', "  ");
    size_t pos = line.find('\t');
    while(pos != std::string::npos) {
      line.replace(pos, 1, "  "); // number is number of chars to replace
      pos = line.find('\t');
    }
    rows.push_back(line);
  }
  f.close();

  dirty = true;
  editor_mode = true;
  editorRefreshScreen(true);
  return;
}
void Editor::editorSaveNoteToFile(const std::string &filename) {
  std::ofstream myfile;
  myfile.open(filename); //filename
  myfile << editorRowsToString();
  editorSetMessage("wrote file");
  myfile.close();
}

void Editor::editorMoveCursor(int key) {

  if (rows.empty()) return; //could also be !numrows

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (fc > 0) fc--;
      break;

    case ARROW_RIGHT:
    case 'l':
      fc++;
      break;

    case ARROW_UP:
    case 'k':
      if (fr > 0) fr--;
      break;

    case ARROW_DOWN:
    case 'j':
      if (fr < rows.size() - 1) fr++;
      break;
  }
}

void Editor::editorBackspace(void) {

  if (fc == 0 && fr == 0) return;

  std::string &row = rows.at(fr);
  if (fc > 0) {
    row.erase(row.begin() + fc - 1);
    fc--;
  } else if (row.size() > 1){
    rows.at(fr-1) = rows.at(fr-1) + row;
    editorDelRow(fr); //05082020
    fr--;
    fc = rows.at(fr).size();
  } else {
    editorDelRow(fr);
    fr--;
    fc = rows.at(fr).size();
}
  dirty++;
}

std::string Editor::editorRowsToString(void) {

  std::string z = "";
  for (auto i: rows) {
      z += i;
      z += '\n';
  }
  if (!z.empty()) z.pop_back(); //pop last return that we added
  return z;
}

void Editor::editorInsertChar(int chr) {
  if (rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
  }
  std::string &row = rows.at(fr);
  row.insert(row.begin() + fc, chr); // this works if row empty
  //row.at(fc) = chr; // this doesn't work if row is empty
  dirty++;
  fc++;
}

void Editor::editorDelRow(int r) {
  //editorSetMessage("Row to delete = %d; numrows = %d", fr, numrows); 
  if (rows.empty()) return; // creation of NO_ROWS may make this unnecessary

  rows.erase(rows.begin() + r);
  if (rows.size() == 0) {
    fr = fc = cy = cx = line_offset = prev_line_offset = first_visible_row = last_visible_row = 0;

    mode = NO_ROWS;
    return;
  }

  dirty++;
  //editorSetMessage("Row deleted = %d; numrows after deletion = %d cx = %d row[fr].size = %d", fr,
  //numrows, cx, row[fr].size); 
}

// called by caw and daw
// doesn't handle punctuation correctly
// but vim pretty wierd for punctuation
void Editor::editorDelWord(void) {

  if (rows.empty()) return;
  std::string& row = rows.at(fr);
  if (row[fc] < 48) return;

//below is finding beginning of word
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

//below is finding end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

// below is deleting between beginning and end
  row.erase(beg, end-beg+1);
  fc = beg;

  dirty++;
  editorSetMessage("beg = %d, end = %d", beg, end);
}

// does not work
void Editor::editorFindNextWord(void) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);

  int y = fr;
  int x = fc;
  size_t found;

  // make sure you're not sitting on search word to start
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  // find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (search_string == row.substr(beg, end-beg+1)) x++;
  int passes = 0;
  for(;;) {
    std::string& row = rows.at(y);
    found = row.find(search_string, x);
    if (found != std::string::npos)
       break;
    y++;
    if (y == rows.size()) {
      passes++;
      if (passes > 1) break;
      y = 0;
    }
    x = 0;
  }
  if (passes <= 1) {
    fc = found;
    fr = y;
  }
}

// called by get_note (and others) and in main while loop
// redraw == true means draw the rows
void Editor::editorRefreshScreen(bool redraw) {
  char buf[32];
  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, EDITOR_LEFT_MARGIN + 1); //03022019 added len
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, left_margin + 1); //03022019 added len
  ab.append(buf, strlen(buf));

  if (redraw) {

    // erase the screen
    char lf_ret[10];
    // \x1b[NC moves cursor forward by N columns
    //int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", EDITOR_LEFT_MARGIN);
    int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin);
    for (int i=0; i < total_screenlines; i++) {
      ab.append("\x1b[K");
      ab.append(lf_ret, nchars);
    }

    //Temporary kluge tid for code folder = 18
    if (get_folder_tid(id) == 18 && !(mode == VISUAL || mode == VISUAL_LINE || mode == VISUAL_BLOCK)) editorDrawCodeRows(ab);
    //if (highlight_syntax == true) editorDrawCodeRows(ab);
    else editorDrawRows(ab);
  }
  editorDrawStatusBar(ab);
  editorDrawMessageBar(ab);

  // the lines below position the cursor where it should go
  if (mode != COMMAND_LINE){
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + TOP_MARGIN + 1, cx + left_margin + 1); //03022019
    ab.append(buf, strlen(buf));
  }

  ab.append("\x1b[?25h", 6); //shows the cursor
  write(STDOUT_FILENO, ab.c_str(), ab.size());

  // can't do this until ab is written or will just overwite highlights
  if (redraw) {
    if (spellcheck) editorSpellCheck();
    /* somehow from outline mode (not edit mode) mode = SEARCH needs to be set <-I did this */
    else if (mode == SEARCH) editorHighlightWordsByPosition();
  }
}

void Editor::editorDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  // we want this on the left margin
  buf  << "\x1b[" << total_screenlines + TOP_MARGIN + 2 << ";" << EDITOR_LEFT_MARGIN << "H";
  ab += buf.str();
  ab += "\x1b[K"; // will erase midscreen -> R; cursor doesn't move after erase
  int msglen = strlen(message);
  if (msglen > screencols) msglen = screencols;
  ab.append(message, msglen);
}

void Editor::editorDrawStatusBar(std::string& ab) {
  int len;
  char status[200];
  // position the cursor at the beginning of the editor status bar at correct indent
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screenlines + TOP_MARGIN + 1,
                                            left_margin);
  ab.append(buf, strlen(buf));
  ab.append("\x1b[K"); //cursor in middle of screen and doesn't move on erase
  ab.append("\x1b[7m"); //switches to inverted colors
  ab.append(" ");
  if (!rows.empty()){
    int line = editorGetLineInRowWW(fr, fc);
    int line_char_count = editorGetLineCharCountWW(fr, line);
    int lines = editorGetLinesInRowWW(fr);
    //fr, fc, cx, cy, line chars start at zero; line, lines line 
    len = snprintf(status,
                   sizeof(status), "fr=%d lines=%d line=%d fc=%d line offset=%d initial row=%d last row=%d line chrs="
                                   "%d  cx=%d cy=%d scols(1)=%d, left_margin=%d rows.size=%ld",
                                   fr, lines, line, fc, line_offset, first_visible_row, last_visible_row, line_char_count, cx, cy, screencols, left_margin, rows.size());
  } else {
    len =  snprintf(status, sizeof(status), "E.row is NULL E.cx = %d E.cy = %d  E.numrows = %ld E.line_offset = %d",
                                      cx, cy, rows.size(), line_offset);
  }
  if (len > screencols) len = screencols;
  ab.append(status, len);
  while (len < screencols) {
      ab.append(" ", 1);
      len++;
    }
  ab.append("\x1b[0m"); //switches back to normal formatting
}

void Editor::editorDrawRows(std::string &ab) {

  /* for visual modes */
  int begin = 0;
  std::string abs = "";
 
  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf << "\x1b[" << TOP_MARGIN + 1 << ";" <<  left_margin + 1 << "H";
  ab.append(buf.str());

  /* testing moving into editorRefreshScreen
  // erase the screen
  for (int i=0; i < screenlines; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret, nchars);
  }
  */

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 1 << ";" <<  left_margin + 1 << "H";
  ab.append(buf2.str()); //reposition cursor

  if (rows.empty()) return;

  int h_light[2] = {0,0};
  bool visual = false;
  bool visual_block = false;
  bool visual_line = false;
  if (mode == VISUAL || mode == VISUAL_LINE) {
    if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
      h_light[1] = highlight[0];
      h_light[0] = highlight[1];
    } else {
      h_light[0] = highlight[0];
      h_light[1] = highlight[1];
    }
  }

  int y = 0;
  int filerow = first_visible_row;
  bool flag = false;

  for (;;){
    if (flag) break;
    if (filerow == rows.size()) {last_visible_row = filerow - 1; break;}
    std::string row = rows.at(filerow);

    if (mode == VISUAL_LINE && filerow == h_light[0]) {
      ab.append("\x1b[48;5;242m", 11);
      visual_line = true;
    }

    if (mode == VISUAL && filerow == fr) {
      /* zz counting the number of additional chars generated by '\n' at end of line*/
      int zz = h_light[0]/screencols;
      begin = ab.size() + h_light[0] + zz;
      visual = true;
    }

    /**************************************/
    if (mode == VISUAL_BLOCK && (filerow > (vb0[1] - 1) && filerow < (fr + 1))) { //highlight[3] top row highlight[4] lower (higher #)
      int zz = vb0[0]/screencols;
      begin = ab.size() + vb0[0] + zz;
      visual_block = true;
    } else visual_block = false;
    /**************************************/

    if (row.empty()) {
      if (y == screenlines - 1) break;
      //ab.append(lf_ret, nchars);
      ab.append(1, '\f');
      filerow++;
      y++;

      // pretty ugly that this has to appear here but need to take into account empty rows  
      if (visual_line) {
        if (filerow == h_light[1] + 1){ //could catch VISUAL_LINE starting on last row outside of for
          ab.append("\x1b[0m", 4); //return background to normal
          visual_line = false;
        }
      }
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      /* this is needed because it deals where the end of the line doesn't have a space*/
      if (row.substr(pos+1).size() <= screencols) {
        ab.append(row, pos+1, screencols);
        abs.append(row, pos+1, screencols);
        if (y == screenlines - 1) {flag=true; break;}
        ab.append(1, '\f');
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+screencols);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + screencols;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = screencols - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      abs.append(row, prev_pos+1, pos-prev_pos);
      if (y == screenlines - 1) {flag=true; break;}
      ab.append(1, '\f');
      y++;
    }

    /* The VISUAL mode code that actually does the writing */
    if (visual) { 
      std::string visual_snippet = ab.substr(begin, h_light[1]-h_light[0]); 
      int pos = -1;
      int n = 0;
      for (;;) {
        pos += 1;
        pos = visual_snippet.find('\f', pos);
        if (pos == std::string::npos) break;
        n += 1;
      }
      ab.insert(begin + h_light[1] - h_light[0] + n + 1, "\x1b[0m");
      ab.insert(begin, "\x1b[48;5;242m");
      visual = false;
    }

    if (visual_block) { 
      std::string visual_snippet = ab.substr(begin, fc-vb0[0]); //fc = highlight[1] ; vb0[0] = highlight[0] 
      int pos = -1;
      int n = 0;
      for (;;) {
        pos += 1;
        pos = visual_snippet.find('\f', pos);
        if (pos == std::string::npos) break;
        n += 1;
      }
      ab.insert(begin + fc - vb0[0] + n + 1, "\x1b[0m");
      ab.insert(begin, "\x1b[48;5;242m");
    }

    if (visual_line) {
      if (filerow == h_light[1] + 1){ //could catch VISUAL_LINE starting on last row outside of for
        ab.append("\x1b[0m", 4); //return background to normal
        visual_line = false;
      }
    }

  }
  last_visible_row = filerow - 1; // note that this is not exactly true - could be the whole last row is visible
  ab.append("\x1b[0m", 4); //return background to normal - would catch VISUAL_LINE starting and ending on last row

  size_t p = 0;
  for (;;) {
      if (p > ab.size()) break;
      p = ab.find('\f', p);
      if (p == std::string::npos) break;
      ab.replace(p, 1, lf_ret);
      p += 7;
  }
}

void Editor::editorDrawCodeRows(std::string &ab) {
  //save the current file to code_file with correct extension
  std::ofstream myfile;
  myfile.open("code_file"); 
  myfile << editorGenerateWWString();
  myfile.close();

  std::stringstream display;
  std::string line;

  // below is a quick hack folder tid = 18 -> code
  if (get_folder_tid(id) == 18) {
   procxx::process highlight("highlight", "code_file", "--out-format=xterm256", 
                             "--style=gruvbox-dark-hard-slz", "--syntax=cpp");
   // procxx::process highlight("bat", "code_file", "--style=plain", "--paging=never", "--color=always", "--language=cpp", "--theme=gruvbox");
    highlight.exec();
    while(getline(highlight.output(), line)) { display << line << '\n';}
  } else {
    procxx::process highlight("bat", "code_file", "--style=plain", "--paging=never", 
                               "--color=always", "--language=md.hbs", "--italic-text=always",
                               "--theme=gruvbox-markdown");
    highlight.exec();
    while(getline(highlight.output(), line)) { display << line << '\n';}
  }

  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf << "\x1b[" << TOP_MARGIN + 1 << ";" <<  left_margin + 1 << "H";
  ab.append(buf.str());

  /* testing moving into editorRefreshScreen
  // erase the screen
  for (int i=0; i < screenlines; i++) {
    ab.append("\x1b[K");
    ab.append(lf_ret, nchars);
  }
  */

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 1 << ";" <<  left_margin + 1 << "H";
  ab.append(buf2.str()); //reposition cursor

  //std::string line;
  display.clear();
  display.seekg(0, std::ios::beg);
  int n = 0;
  while(std::getline(display, line, '\n')) {
    if (n >= line_offset) {
      ab.append(line);
      ab.append(lf_ret);
    }
    n++;
  }
  ab.append("\x1b[0m");

}

// uses sqlite offsets contained in word_positions
// currently just highlights the words in rows that are visible on the screen
// You can't currently scroll because when you edit the search highlights disappear
// ie text can't be scrolled unless you enter editor_mode which doesn't highlight
// while this works ? better to use find_if as per
// https://stackoverflow.com/questions/9333333/c-split-string-with-space-and-punctuation-chars
void Editor::editorHighlightWordsByPosition(void) {

  if (rows.empty()) return;

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\\"; //removed period?? since it is in list?
  for (auto v: word_positions) {
    int word_num = -1;
    auto pos = v.begin();
    auto prev_pos = pos;
    for (int n=0; n<=last_visible_row; n++) {
      int end = -1; //this became a problem in comparing -1 to unsigned int (always larger)
      int start;
      std::string &row = rows.at(n);
      if (row.empty()) continue;
      for (;;) {
        if (end >= static_cast<int>(row.size()) - 1) break;
        start = end + 1;
        end = row.find_first_of(delimiters, start);
        if (end == std::string::npos) {
          end = row.size();
        }
        if (end != start) word_num++;
  
        if (n < first_visible_row) continue;
        // start the search from the last match? 12-23-2019
        pos = std::find(pos, v.end(), word_num);
        if (pos != v.end()) {
          prev_pos = pos;
          editorHighlightWord(n, start, end-start);
        } else pos = prev_pos;
      }
    }
  }
}

void Editor::editorYankLine(int n) {
  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(rows.at(fr+i));
  }
  string_buffer.clear();
}

void Editor::editorYankString(void) {
  // doesn't cross rows right now
  if (rows.empty()) return;

  std::string& row = rows.at(fr);
  string_buffer.clear();

  int h_light[2] = {0,0};

  if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
    h_light[1] = highlight[0];
    h_light[0] = highlight[1];
  } else {
    h_light[0] = highlight[0];
    h_light[1] = highlight[1];
  }

  std::string::const_iterator first = row.begin() + h_light[0];
  std::string::const_iterator last = row.begin() + h_light[1] + 1;
  string_buffer = std::string(first, last);
}


void Editor::editorPasteString(void) {
  if (rows.empty() || string_buffer.empty()) return;
  std::string& row = rows.at(fr);

  row.insert(row.begin() + fc, string_buffer.begin(), string_buffer.end());
  fc += string_buffer.size();
  dirty++;
}

void Editor::editorPasteStringVisual(void) {
  if (rows.empty() || string_buffer.empty()) return;
  std::string& row = rows.at(fr);

  int h_light[2] = {0,0};
  if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
    h_light[1] = highlight[0];
    h_light[0] = highlight[1];
  } else {
    h_light[0] = highlight[0];
    h_light[1] = highlight[1];
  }

  row.replace(row.begin() + h_light[0], row.begin() + h_light[1] + 1, string_buffer);
  fc = h_light[0] + string_buffer.size();
  dirty++;
}

void Editor::editorPasteLine(void){
  if (rows.empty())  editorInsertRow(0, std::string());

  for (size_t i=0; i < line_buffer.size(); i++) {
    //int len = (line_buffer[i].size());
    fr++;
    editorInsertRow(fr, line_buffer[i]);
  }
}

void Editor::editorIndentRow(void) {
  if (rows.empty()) { // creation of NO_ROWS may make this unnecessary
    editorInsertRow(0, std::string());
  }
  std::string &row = rows.at(fr);
  row.insert(0, indent, ' ');
  fc = indent;
  dirty++;
}

void Editor::editorUnIndentRow(void) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);
  if (row.empty()) return;
  fc = 0;
  for (int i = 0; i < indent; i++) {
    if (row.empty()) break;
    if (row[0] == ' ') {
      editorDelChar();
    }
  }
  dirty++;
}
//
//handles punctuation
std::string Editor::editorGetWordUnderCursor(void) {

  if (rows.empty()) return "";
  std::string &row = rows.at(fr);
  if (row[fc] < 48) return "";

  std::string delimiters = " ,.;?:()[]{}&#";

  // find beginning of word
  auto beg = row.find_last_of(delimiters, fc);
  if (beg == std::string::npos) beg = 0;
  else beg++;

  // find end of word
  auto end = row.find_first_of(delimiters, beg);
  if (end == std::string::npos) {end = row.size();}

  return row.substr(beg, end-beg);

  //editorSetMessage("beg = %d, end = %d  word = %s", beg, end, search_string.c_str());
}

void Editor::editorSpellingSuggestions(void) {
  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  auto sugs = std::vector<std::string>();
  auto dict = nuspell::Dictionary::load_from_path(path);

  std::string word;
  std::stringstream s;
  word = editorGetWordUnderCursor();
  if (word.empty()) return;

  if (dict.spell(word)) {
      editorSetMessage("%s is spelled correctly", word.c_str());
      return;
  }

  dict.suggest(word, sugs);
  if (sugs.empty()) {
      editorSetMessage("No suggestions");
  } else {
    for (auto &sug : sugs) s << sug << ' ';
    editorSetMessage("Suggestions for %s: %s", word.c_str(), s.str().c_str());
  }
}

void Editor::editorChangeCase(void) {
  if (rows.empty()) return;
  std::string& row = rows.at(fr);
  char d = row.at(fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    editorMoveCursor(ARROW_RIGHT);
    return;
  }
  editorDelChar();
  editorInsertChar(d);
}

void Editor::editorDeleteToEndOfLine(void) {
  std::string& row = rows.at(fr);
  row.resize(fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  dirty++;
}

// returns true if display needs to scroll and false if it doesn't
bool Editor::editorScroll(void) {

  if (rows.empty()) {
    fr = fc = cy = cx = line_offset = prev_line_offset = first_visible_row = last_visible_row = 0;
    return true;
  }

  if (fr >= rows.size()) fr = rows.size() - 1;

  int row_size = rows.at(fr).size();
  if (fc >= row_size) fc = row_size - (mode != INSERT); 

  if (fc < 0) fc = 0;

  cx = editorGetScreenXFromRowColWW(fr, fc);
  int cy_ = editorGetScreenYFromRowColWW(fr, fc);

  //my guess is that if you wanted to adjust line_offset to take into account that you wanted
  // to only have full rows at the top (easier for drawing code) you would do it here.
  // something like screenlines goes from 4 to 5 so that adjusts cy
  // it's complicated and may not be worth it.

  //deal with scroll insufficient to include the current line
  if (cy_ > screenlines + line_offset - 1) {
    line_offset = cy_ - screenlines + 1; ////
    int line_offset_ = line_offset;
    first_visible_row = editorGetInitialRow(line_offset_);
    line_offset = line_offset_;
  }

 //let's check if the current line_offset is causing there to be an incomplete row at the top

  // this may further increase line_offset so we can start
  // at the top with the first line of some row
  // and not start mid-row which complicates drawing the rows

  //deal with scrol where current line wouldn't be visible because we're scrolled too far
  if (cy_ < line_offset) {
    line_offset = cy_;
    first_visible_row = editorGetInitialRow(line_offset, SCROLL_UP);
  }
  if (line_offset == 0) first_visible_row = 0; 

  cy = cy_ - line_offset;

  // vim seems to want full rows to be displayed although I am not sure
  // it's either helpful or worth it but this is a placeholder for the idea

  // returns true if display needs to scroll and false if it doesn't
  if (line_offset == prev_line_offset) return false;
  else {prev_line_offset = line_offset; return true;}
}

// used by editorScroll to figure out the first row to draw
// there should be only ONE of these (see below)
int Editor::editorGetInitialRow(int &line_offset) {

  if (line_offset == 0) return 0;

  int r = 0;
  int lines = 0;

  for (;;) {
    lines += editorGetLinesInRowWW(r);
    r++;

    // there is no need to adjust line_offset
    // if it happens that we start
    // on the first line of row r
    if (lines == line_offset) break;

    // need to adjust line_offset
    // so we can start on the first
    // line of row r
    if (lines > line_offset) {
      line_offset = lines;
      break;
    }
  }
  return r;
}

// used by editorScrolls to figure out the first row to draw
int Editor::editorGetInitialRow(int &line_offset, int direction) {

  if (line_offset == 0) return 0;

  int r = 0;
  int lines = 0;

  for (;;) {
    lines += editorGetLinesInRowWW(r);
    r++;

    // there is no need to adjust line_offset
    // if it happens that we start
    // on the first line of row r
    if (lines == line_offset) {
        line_offset = lines;
        return r;
    }

    // need to adjust line_offset
    // so we can start on the first
    // line of row r
      if (lines > line_offset) {
        line_offset = lines;
        break;
    }
  }
  lines = 0;
  int n = 0;
  for (n=0; n<r-1; n++) {
      lines += editorGetLinesInRowWW(n);
  }
  line_offset = lines;
  return n;
}

/* this is dot */
void Editor::editorDotRepeat(int repeat) {

  //repeat not implemented

  //case 'i': case 'I': case 'a': case 'A': 
  if (cmd_map1.count(last_command)) {
    (this->*cmd_map1[last_command])(last_repeat);

    for (int n=0; n<last_repeat; n++) {
      for (char const &c : last_typed) {
        if (c == '\r') editorInsertReturn();
        else editorInsertChar(c);
      }
    }
    return;
  }

  //case 'o': case 'O':
  if (cmd_map2.count(last_command)) {
    (this->*cmd_map2[last_command])(last_repeat);
    return;
  }

  //case 'x': case C_dw: case C_daw: case C_dd: case C_de: case C_dG: case C_d$:
  if (cmd_map3.count(last_command)) {
    (this->*cmd_map3[last_command])(last_repeat);
    return;
  }

  //case C_cw: case C_caw: case 's':
  if (cmd_map4.count(last_command)) {
      (this->*cmd_map4[last_command])(last_repeat);

    for (char const &c : last_typed) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
    return;
  }

  if (last_command == "~") {
    E_change_case(last_repeat);
    return;
  }

  if (last_command == "r") {
    E_replace(last_repeat);
    return;
  }
}

void Editor::editorPageUpDown(int key) {

  if (key == PAGE_UP) {
    if(fr==0) return;
    int lines = 0;
    int r = fr - 1;
    for(;;) {
        lines += editorGetLinesInRowWW(r);
        if (r == 0) {
            break;
        }
        if (lines > screenlines) {
            r++;
            break;
        }
        r--;
    }
    fr = r;
  } else {
    int lines = 0;
    int r = fr;
    for(;;) {
        lines += editorGetLinesInRowWW(r);
        if (r == rows.size() - 1) {
            break;
        }
        if (lines > screenlines) {
            r--;
            break;
        }
        r++;
    }
    fr = r;
  }
  return;
}

void Editor::editorDeleteVisual(void){
  if (rows.empty()) return;

  int h_light[2] = {0,0};
  if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
    h_light[1] = highlight[0];
    h_light[0] = highlight[1];
  } else {
    h_light[0] = highlight[0];
    h_light[1] = highlight[1];
  }
  rows.at(fr).erase(h_light[0], h_light[1] - h_light[0] + 1);
  fc = h_light[0];
}

void Editor::editorPasteLineVisual(void){
  if (rows.empty())  editorInsertRow(0, std::string());

  /* could just call editorDeleteVisual below*/
  int h_light[2] = {0,0};
  if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
    h_light[1] = highlight[0];
    h_light[0] = highlight[1];
  } else {
    h_light[0] = highlight[0];
    h_light[1] = highlight[1];
  }

  rows.at(fr).erase(h_light[0], h_light[1] - h_light[0] + 1);
  fc = h_light[0];
  /* could just call editorDeleteVisual above*/

  editorInsertReturn();
  fr--;

  for (size_t i=0; i < line_buffer.size(); i++) {
    fr++;
    editorInsertRow(fr, line_buffer[i]);
  }
}

//Beginning WW functions
/**************can't take substring of row because absolute position matters**************************/
//called by editorScroll to get cx
int Editor::editorGetScreenXFromRowColWW(int r, int c) {
  // can't use reference to row because replacing blanks to handle corner case
  std::string row = rows.at(r);

  /* pos is the position of the last char in the line
   * and pos+1 is the position of first character of the next row
   */

  if (row.size() <= screencols ) return c; //seems obvious but not added until 03022019

  int pos = -1;
  int prev_pos;
  for (;;) {

  if (row.substr(pos+1).size() <= screencols) {
    prev_pos = pos;
    break;
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+screencols);

  if (pos == std::string::npos) {
      pos = prev_pos + screencols;
  } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + screencols;
  }
    /*
    else
      replace(row.begin()+prev_pos+1, row.begin()+pos+1, ' ', '+');
    */

  if (pos >= c) break;
  }
  return c - prev_pos - 1;
}

/* called by editorScroll to get cy
line_offset is taken into account in editorScroll*/
int Editor::editorGetScreenYFromRowColWW(int r, int c) {
  int screenline = 0;

  for (int n = 0; n < r; n++)
    screenline+= editorGetLinesInRowWW(n);

  screenline = screenline + editorGetLineInRowWW(r, c) - 1;
  return screenline;
}

// used in editorGetScreenYFromRowColWW
/**************can't take substring of row because absolute position matters**************************/
int Editor::editorGetLineInRowWW(int r, int c) {
  // can't use reference to row because replacing blanks to handle corner case
  std::string row = rows.at(r);

  if (row.size() <= screencols ) return 1; //seems obvious but not added until 03022019

  /* pos is the position of the last char in the line
   * and pos+1 is the position of first character of the next row
   */

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= screencols) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+screencols);

    if (pos == std::string::npos) {
        pos = prev_pos + screencols;

   // only replace if you have enough characters without a space to trigger this
   // need to start at the beginning each time you hit this
   // unless you want to save the position which doesn't seem worth it
    } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + screencols;
    }

    lines++;
    if (pos >= c) break;
  }
  return lines;
}
//used by editorGetInitialRow
//used by editorGetScreenYFromRowColWW
int Editor::editorGetLinesInRowWW(int r) {
  std::string_view row(rows.at(r));

  if (row.size() <= screencols) return 1; //seems obvious but not added until 03022019

  int lines = 0;
  int pos = -1; //pos is the position of the last character in the line (zero-based)
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= screencols) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+screencols);

    //note npos when signed = -1
    //order can be reversed of if, else if and can drop prev_pos != -1: see editorDrawRows
    if (pos == std::string::npos) {
      pos = prev_pos + screencols;
    } else if (pos == prev_pos) {
      row = row.substr(pos+1);
      //prev_pos = -1; 12-27-2019
      pos = screencols - 1;
    }
    lines++;
  }
  return lines;
}

/* this exists to create a text file that has the proper
 * line breaks based on screen width for syntax highlighters
 * to operate on 
 * Produces a text string that starts at the first line of the
 * file and ends on the last visible line
 */
std::string Editor::editorGenerateWWString(void) {
  if (rows.empty()) return "";

  std::string ab = "";
  int y = -line_offset;
  int filerow = 0;

  for (;;) {
    if (filerow == rows.size()) {last_visible_row = filerow - 1; return ab;}

    std::string row = rows.at(filerow);
    
    if (row.empty()) {
      if (y == screenlines - 1) return ab;
      ab.append("\n");
      filerow++;
      y++;
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      /* this is needed because it deals where the end of the line doesn't have a space*/
      if (row.substr(pos+1).size() <= screencols) {
        ab.append(row, pos+1, screencols);
        if (y == screenlines - 1) {last_visible_row = filerow - 1; return ab;}
        ab.append("\n");
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+screencols);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + screencols;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = screencols - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      if (y == screenlines - 1) {last_visible_row = filerow - 1; return ab;}
      ab.append("\n");
      y++;
    }
  }
}
//used in status bar because interesting but not essential
int Editor::editorGetLineCharCountWW(int r, int line) {
  //This should be a string view and use substring like lines in row
  std::string row = rows.at(r);
  if (row.empty()) return 0;

  if (row.size() <= screencols) return row.size();

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

  // we know the first time around this can't be true
  // could add if (line > 1 && row.substr(pos+1).size() ...);
  if (row.substr(pos+1).size() <= screencols) {
    return row.substr(pos+1).size();
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+screencols);

  if (pos == std::string::npos) {
    pos = prev_pos + screencols;

  // only replace if you have enough characters without a space to trigger this
  // need to start at the beginning each time you hit this
  // unless you want to save the position which doesn't seem worth it
  } else if (pos == prev_pos) {
    replace(row.begin(), row.begin()+pos+1, ' ', '+');
    pos = prev_pos + screencols;
  }

  lines++;
  if (lines == line) break;
  }
  return pos - prev_pos;
}

/************************************* end of WW ************************************************/
/* EDITOR COMMAND_LINE mode functions */
void Editor::E_write_C(void) {
  update_note();
  mode = NORMAL;
  command[0] = '\0';
  command_line.clear();
  if (lm_browser) { //lm_browser is global and O below is global
    //if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    if (get_folder_tid(id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
  //auto it = html_files.find(O.rows.at(O.fr).id); // O is global and so html_files but not sure needed
  auto it = html_files.find(id); //O is global and so is html_files but not sure needed
  if (it != html_files.end()) update_html_file("assets/" + it->second);
  editorSetMessage("");
}

void Editor::E_write_close_C(void) {
  update_note();
  mode = NORMAL;
  command[0] = '\0';
  command_line.clear();
  editor_mode = false; //global
  if (lm_browser) { //lm_browser is global and O below is global
    //if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    if (get_folder_tid(id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
  //auto it = html_files.find(O.rows.at(O.fr).id); //O is global and so is html_files but not sure needed
  auto it = html_files.find(id); //O is global and so is html_files but not sure needed
  if (it != html_files.end()) update_html_file("assets/" + it->second);
  editorSetMessage("");
}

//case 'q':
void Editor::E_quit_C(void) {
  if (dirty) {
      mode = NORMAL;
      command[0] = '\0';
      command_line.clear();
      editorSetMessage("No write since last change");
  } else {
    editorSetMessage("");
    fr = fc = cy = cx = line_offset = 0; //added 11-26-2019 but may not be necessary having restored this in get_note.
    editor_mode = false; //global
  }
  editorRefreshScreen(false); // don't need to redraw rows
}

void Editor::E_quit0_C(void) {
  mode = NORMAL;
  command[0] = '\0';
  command_line.clear();
  editor_mode = false; //global
}

void Editor::E_open_in_vim_C(void) {
  open_in_vim(); //send you into editor mode
  mode = NORMAL;
}

#ifdef NUSPELL
void Editor::E_spellcheck_C(void) {
  spellcheck = !spellcheck;
  if (spellcheck) editorSpellCheck();
  else editorRefreshScreen(true);
  mode = NORMAL;
  command[0] = '\0';
  command_line.clear();
  editorSetMessage("Spellcheck %s", (spellcheck) ? "on" : "off");
}
#else
void E_spellcheck_C(void) {
  editorSetMessage("Nuspell is not available in this build");
}
#endif

void Editor::E_persist_C(void) {
  //generate_persistent_html_file(O.rows.at(O.fr).id); //global
  generate_persistent_html_file(id); //global
  //command[0] = '\0';
  //command_line.clear();
  mode = NORMAL;
}

// EDITOR NORMAL mode functions
void Editor::E_cw(int repeat) {
  for (int j = 0; j < repeat; j++) {
    int start = fc;
    editorMoveEndWord();
    int end = fc;
    fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
    // text repeats once
  }
}

void Editor::E_caw(int repeat) {
   for (int i=0; i < repeat; i++)  editorDelWord();
    // text repeats once
}

void Editor::E_de(int repeat) {
  // should this use editorDelWord?
  for (int j = 0; j < repeat; j++) {
    if (fc == rows.at(fr).size() - 1) {
      editorDelChar();
      //start--;
      return;
    }
    int start = fc;
    editorMoveEndWord(); //correct one to use to emulate vim
    int end = fc;
    fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
  }
}

void Editor::E_dw(int repeat) {
  // should this use editorDelWord?
  for (int j = 0; j < repeat; j++) {
    //start = fc;
    if (fc == rows.at(fr).size() - 1) {
      editorDelChar();
      //start--;
      return;
    }
    if (rows.at(fr).at(fc + 1) == ' ') {
      //fc = start + 1;
      // this is not correct - it removes all spaces
      // woud need to use the not a find_first_not_of space and delete all of them
      editorDelChar();
      editorDelChar();
      continue;
    }
    int start = fc;
    editorMoveEndWord();
    int end = fc + 1;
    end = (rows.at(fr).size() > end) ? end : rows.at(fr).size() - 1;
    fc = start;
    for (int j = 0; j < end - start + 1; j++) editorDelChar();
  }
  //fc = start; 
}

void Editor::E_daw(int repeat) {
   for (int i=0; i < repeat; i++) editorDelWord();
}

void Editor::E_dG(int repeat) {
  if (rows.empty()) return;
  rows.erase(rows.begin() + fr, rows.end());
  if (rows.empty()) {
    fr = fc = cy = cx = line_offset = 0;
    mode = NO_ROWS;
  } else {
     fr--;
     fc = 0;
  }
}

void Editor::E_s(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorDelChar();
}

void Editor::E_x(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorDelChar();
}

void Editor::E_dd(int repeat) {
  //editorCreateSnapshot();
  int r = rows.size() - fr;
  repeat = (r >= repeat) ? repeat : r ;
  editorYankLine(repeat);
  for (int i=0; i<repeat ; i++) editorDelRow(fr);
  editorSetMessage("Howdy");
}

void Editor::E_d$(int repeat) {
  editorDeleteToEndOfLine();
  if (!rows.empty()) {
    int r = rows.size() - fr;
    repeat--;
    repeat = (r >= repeat) ? repeat : r ;
    //editorYankLine(repeat); //b/o 2 step won't really work right
    for (int i = 0; i < repeat ; i++) editorDelRow(fr);
    }
}

void Editor::E_change_case(int repeat) {
  //editorCreateSnapshot();
  for (int i = 0; i < repeat; i++) editorChangeCase();
}

void Editor::E_goto_outline(int repeat) {
  editor_mode = false;
}

void Editor::E_replace(int repeat) {
  for (int i = 0; i < repeat; i++) {
    editorDelChar();
    editorInsertChar(last_typed[0]);
  }
}

void Editor::E_i(int repeat) {}

void Editor::E_I(int repeat) {
  editorMoveCursorBOL();
  fc = editorIndentAmount(fr);
}

void Editor::E_a(int repeat) {
  editorMoveCursor(ARROW_RIGHT);
}

void Editor::E_A(int repeat) {
  editorMoveCursorEOL();
  editorMoveCursor(ARROW_RIGHT); //works even though not in INSERT mode
}

void Editor::E_o(int repeat) {
  for (int n=0; n<repeat; n++) {
    editorInsertNewline(1);
    for (char const &c : last_typed) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
  }
}

void Editor::E_O(int repeat) {
  for (int n=0; n<repeat; n++) {
    editorInsertNewline(0);
    for (char const &c : last_typed) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
  }
}

//lands on punctuation, lands on blank lines ...
void Editor::E_w(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveNextWord();
  }
}

void Editor::E_b(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveBeginningWord();
  }
}

void Editor::E_e(int repeat) {
  for (int i=0; i<repeat; i++) {
     editorMoveEndWord();
  }
}

void Editor::E_0(int repeat) {
  editorMoveCursorBOL();
}

void Editor::E_$(int repeat) {
  editorMoveCursorEOL();
  for (int i=0; i<repeat-1; i++) {
    if (fr < rows.size() - 1) {
      fr++;
      editorMoveCursorEOL();
    } else break;  
  }
}

void Editor::E_tilde(int repeat) {
  E_change_case(repeat);
}

void Editor::E_J(int repeat) {
  fc = rows.at(fr).size();
  repeat = (repeat == 1) ? 2 : repeat;
  for (int i=1; i<repeat; i++) { 
    if (fr == rows.size()- 1) return;
    rows.at(fr) += " " + rows.at(fr+1);
    editorDelRow(fr+1); 
  }  
}

/* 'O' and 'o' need special handling for repeat*/
void Editor::e_o(int repeat) {
  editorCreateSnapshot();
  last_typed.clear();
  E_o(1);
  //mode = INSERT;
  editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void Editor::e_O(int repeat) {
  editorCreateSnapshot();
  last_typed.clear();
  E_O(1);
  //mode = INSERT;
  editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//needs to be special b/o count/repeat
void Editor::e_replace(int repeat) {
  mode = REPLACE;
  editorSetMessage("Howdy");
}

void Editor::E_next_misspelling(int repeat) {
  if (!spellcheck || pos_mispelled_words.empty()) {
    editorSetMessage("Spellcheck is off or no words mispelled");
    return;
  }
  auto &z = pos_mispelled_words;
  auto it = find_if(z.begin(), z.end(), [&](const std::pair<int, int> &p) {return (p.first == fr && p.second > fc);});
  if (it == z.end()) {
    it = find_if(z.begin(), z.end(), [&](const std::pair<int, int> &p) {return (p.first > fr);});
    if (it == z.end()) {fr = z[0].first; fc = z[0].second;
    } else {fr = it->first; fc = it->second;} 
  } else {fc = it->second;}
  editorSetMessage("fr = %d, fc = %d", fr, fc);
}

void Editor::E_prev_misspelling(int repeat) {
  if (!spellcheck || pos_mispelled_words.empty()) {
    editorSetMessage("Spellcheck is off or no words mispelled");
  return;
  }
  auto &z = pos_mispelled_words;
  auto it = find_if(z.rbegin(), z.rend(), [&](const std::pair<int, int> &p) {return (p.first == fr && p.second < fc);});
  if (it == z.rend()) {
    it = find_if(z.rbegin(), z.rend(), [&](const std::pair<int, int> &p) {return (p.first < fr);});
    if (it == z.rend()) {fr = z.back().first; fc = z.back().second;
    } else {fr = it->first; fc = it->second;} 
  } else {fc = it->second;}
  editorSetMessage("fr = %d, fc = %d", fr, fc);
}

void Editor::E_suggestions(int repeat) {
  editorSpellingSuggestions();
}

void Editor::E_change2command_line(int repeat) {
  //where is the cursor position set? In return_cursor.
  editorSetMessage(":");
  command_line.clear();
  mode = COMMAND_LINE;
}

//case 'V':
void Editor::E_change2visual_line(int repeat) {
  mode = VISUAL_LINE;
  highlight[0] = highlight[1] = fr;
  editorSetMessage("\x1b[1m-- VISUAL LINE --\x1b[0m");
}

//case 'v':
void Editor::E_change2visual(int repeat) {
  mode = VISUAL;
  highlight[0] = highlight[1] = fc;
  editorSetMessage("\x1b[1m-- VISUAL --\x1b[0m");
}

//case CTRL_KEY('v'):
void Editor::E_change2visual_block(int repeat) {
  mode = VISUAL_BLOCK;
  vb0[0] = fc;
  vb0[1] = fr;
  editorSetMessage("\x1b[1m-- VISUAL BLOCK --\x1b[0m");
}

//case 'p':  
void Editor::E_paste(int repeat) {
  if (!string_buffer.empty()) editorPasteString();
  else editorPasteLine();
}

//case '*':  
void Editor::E_find(int repeat) {
  // does not clear dot
  search_string = editorGetWordUnderCursor();
  editorFindNextWord();
  editorSetMessage("\x1b[1m-- FINDING ... --\x1b[0m");
}

//case 'n':
void Editor::E_find_next_word(int repeat) {
  // n does not clear dot
  editorFindNextWord(); //does not work
  // in vim the repeat does work with n - it skips that many words
  // not a dot command so should leave last_repeat alone
  editorSetMessage("\x1b[1m-- FINDING NEXT WORD... --\x1b[0m");
}

//case 'u':
void Editor::E_undo(int repeat) {
  editorRestoreSnapshot();
}

void Editor::E_indent(int repeat) {
  int i;
  for (i = 0; i < repeat; i++) { 
    editorIndentRow();
    fr++;
    if (fr == (int)rows.size() - 1) break;
  }
  fr -= i;
}

void Editor::E_unindent(int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
    editorUnIndentRow();
    fr++;
    if (fr == (int)rows.size() - 1) break;
  }
  fr -= i;
}

void Editor::E_gg(int repeat) {
  fc = line_offset = 0;
  fr = repeat - 1;
}

void Editor::E_G(int repeat) {
  fc = 0;
  fr = rows.size() - 1;
}

void Editor::E_toggle_smartindent(int repeat) {
  smartindent = (smartindent) ? 0 : SMARTINDENT;
  editorSetMessage("smartindent = %d", smartindent);
}

void Editor::E_save_note(int repeat) {
  editorSaveNoteToFile("lm_temp");
}

void Editor::E_bold(int repeat) {
  std::string& row = rows.at(fr);
  if (row.at(fc) == ' ') return;

  //find beginning of word
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  //find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (row.substr(beg, 2) == "**") {
    row.erase(beg, 2);
    end -= 4;
    row.erase(end, 2);
    fc -=2;
    return;
  } else if (row.at(beg) == '*' || row.at(beg) == '`') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
  }

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  row.insert(beg, "**");
  row.insert(end+2, "**"); //
  fc +=2;
}

void Editor::E_emphasis(int repeat) {
  std::string& row = rows.at(fr);
  if (row.at(fc) == ' ') return;

  //find beginning of word
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  //find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (row.substr(beg, 2) == "**") {
    row.erase(beg, 2);
    end -= 4;
    row.erase(end, 2);
    fc -=2;
  } else if (row.at(beg) == '*') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
  } else if (row.at(beg) == '`') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
    return;
  }

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  row.insert(beg, "`");
  row.insert(end+1 , "`");
  fc++;
}

void Editor::E_italic(int repeat) {
  std::string& row = rows.at(fr);
  if (row.at(fc) == ' ') return;

  //find beginning of word
  auto beg = row.find_last_of(' ', fc);
  if (beg == std::string::npos ) beg = 0;
  else beg++;

  //find end of word
  auto end = row.find_first_of(' ', beg);
  if (end == std::string::npos) {end = row.size();}

  if (row.substr(beg, 2) == "**") {
    row.erase(beg, 2);
    end -= 4;
    row.erase(end, 2);
    fc -=2;
  } else if (row.at(beg) == '*') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
    return;
  } else if (row.at(beg) == '`') {
    row.erase(beg, 1);
    end -= 2;
    fc -= 1;
    row.erase(end, 1);
  }

  // needed if word at end of row
  if (end == row.size()) row.push_back(' ');

  row.insert(beg, "*");
  row.insert(end+1 , "*");
  fc++;
}
