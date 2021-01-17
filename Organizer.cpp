#define LEFT_MARGIN 2
#define TOP_MARGIN 1 // Editor.cpp
#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define UNUSED(x) (void)(x)

#include <unistd.h>
#include "session.h"
#include "Organizer.h"
#include "Common.h"
#include <sstream>
#include <fstream>

std::vector<std::string> Organizer::preview_rows = {};//static
std::vector<std::vector<int>> Organizer::word_positions = {};//static
std::map<std::string, int> Organizer::folder_map = {}; //static - filled in by map_folder_titles_[db]
std::map<std::string, int> Organizer::context_map = {}; //static filled in by map_context_titles_[db]

Organizer org = Organizer(); //global; extern Session sess in session.h

std::string Organizer::outlinePreviewRowsToString(void) {

  std::string z = "";
  for (auto i: preview_rows) {
      z += i;
      z += '\n';
  }
  if (!z.empty()) z.pop_back(); //pop last return that we added
  return z;
}

void Organizer::outlineDelWord() {

  orow& row = rows.at(fr);
  if (row.title[fc] < 48) return;

  int i,j,x;
  for (i = fc; i > -1; i--){
    if (row.title[i] < 48) break;
    }
  for (j = fc; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  fc = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  row.dirty = true;
  //sess.showOrgMessage("i = %d, j = %d", i, j ); 
}
//
//Note: outlineMoveCursor worries about moving cursor beyond the size of the row
//OutlineScroll worries about moving cursor beyond the screen
void Organizer::outlineMoveCursor(int key) {

  if (rows.empty()) return;

  switch (key) {
    case ARROW_LEFT:
    case 'h':
      if (fc > 0) fc--; 
      break;

    case ARROW_RIGHT:
    case 'l':
    {
      fc++;
      break;
    }
    case ARROW_UP:
    case 'k':
      if (fr > 0) fr--; 
      fc = coloff = 0; 

      if (view == TASK) {
        get_preview(rows.at(fr).id); //if id == -1 does not try to retrieve note

      } else display_container_info(rows.at(fr).id);
      break;

    case ARROW_DOWN:
    case 'j':
      if (fr < rows.size() - 1) fr++;
      fc = coloff = 0;
      if (view == TASK) {
        get_preview(rows.at(fr).id); //if id == -1 does not try to retrieve note
      } else display_container_info(rows.at(fr).id);
      break;
  }

  orow& row = rows.at(fr);
  if (fc >= row.title.size()) fc = row.title.size() - (mode != INSERT);
  if (row.title.empty()) fc = 0;
}

void Organizer::outlineBackspace(void) {
  orow& row = rows.at(fr);
  if (rows.empty() || row.title.empty() || fc == 0) return;
  row.title.erase(row.title.begin() + fc - 1);
  row.dirty = true;
  fc--;
}

void Organizer::outlineDelChar(void) {

  orow& row = rows.at(fr);

  if (rows.empty() || row.title.empty()) return;

  row.title.erase(row.title.begin() + fc);
  row.dirty = true;
}

void Organizer::outlineDeleteToEndOfLine(void) {
  orow& row = rows.at(fr);
  row.title.resize(fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  row.dirty = true;
}

void Organizer::outlinePasteString(void) {
  orow& row = rows.at(fr);

  if (rows.empty() || string_buffer.empty()) return;

  row.title.insert(row.title.begin() + fc, string_buffer.begin(), string_buffer.end());
  fc += string_buffer.size();
  row.dirty = true;
}

void Organizer::outlineYankString() {
  orow& row = rows.at(fr);
  string_buffer.clear();

  std::string::const_iterator first = row.title.begin() + highlight[0];
  std::string::const_iterator last = row.title.begin() + highlight[1];
  string_buffer = std::string(first, last);
}

void Organizer::outlineMoveCursorEOL() {
  fc = rows.at(fr).title.size() - 1;  //if O.cx > O.titlecols will be adjusted in EditorScroll
}

void Organizer::outlineMoveBeginningWord() {
  orow& row = rows.at(fr);
  if (fc == 0) return;
  for (;;) {
    if (row.title[fc - 1] < 48) fc--;
    else break;
    if (fc == 0) return;
  }
  int i;
  for (i = fc - 1; i > -1; i--){
    if (row.title[i] < 48) break;
  }
  fc = i + 1;
}

void Organizer::outlineMoveEndWord() {
  orow& row = rows.at(fr);
  if (fc == row.title.size() - 1) return;
  for (;;) {
    if (row.title[fc + 1] < 48) fc++;
    else break;
    if (fc == row.title.size() - 1) return;
  }
  int j;
  for (j = fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  fc = j - 1;
}

// not same as 'e' but moves to end of word or stays put if already on end of word
void Organizer::outlineMoveEndWord2() {
  int j;
  orow& row = rows.at(fr);

  for (j = fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  fc = j - 1;
}

void Organizer::outlineGetWordUnderCursor(){
  std::string& title = rows.at(fr).title;
  if (title[fc] < 48) return;

  title_search_string.clear();
  int i,j,x;

  for (i = fc - 1; i > -1; i--){
    if (title[i] < 48) break;
  }

  for (j = fc + 1; j < title.size() ; j++) {
    if (title[j] < 48) break;
  }

  for (x=i+1; x<j; x++) {
      title_search_string.push_back(title.at(x));
  }
  sess.showOrgMessage("word under cursor: <%s>", title_search_string.c_str());
}

void Organizer::outlineFindNextWord() {

  int y, x;
  //y = O.fr;
  y = (fr < rows.size() -1) ? fr + 1 : 0;
  //x = O.fc + 1; //in case sitting on beginning of the word
  x = 0; //you've advanced y since not worried about multiple same words in one title
   for (unsigned int n=0; n < rows.size(); n++) {
     std::string& title = rows.at(y).title;
     auto res = std::search(std::begin(title) + x, std::end(title), std::begin(title_search_string), std::end(title_search_string));
     if (res != std::end(title)) {
         fr = y;
         fc = res - title.begin();
         break;
     }
     y++;
     x = 0;
     if (y == rows.size()) y = 0;
   }

    sess.showOrgMessage("x = %d; y = %d", x, y); 
}

void Organizer::outlineChangeCase() {
  orow& row = rows.at(fr);
  char d = row.title.at(fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    outlineMoveCursor(ARROW_RIGHT);
    return;
  }
  outlineDelChar();
  outlineInsertChar(d);
}

void Organizer::outlineInsertRow(int at, std::string&& s, bool star, bool deleted, bool completed, std::string&& modified) {
  /* note since only inserting blank line at top, don't really need at, s and also don't need size_t*/

  orow row;

  row.title = s;
  row.id = -1;
  row.star = star;
  row.deleted = deleted;
  row.completed = completed;
  row.dirty = true;
  row.modified = modified;

  row.mark = false;

  auto pos = rows.begin() + at;
  rows.insert(pos, row);
}

// positions the cursor ( O.cx and O.cy) and O.coloff and O.rowoff
void Organizer::outlineScroll(void) {

  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  if(rows.empty()) {
      fr = fc = coloff = cx = cy = 0;
      return;
  }

  if (fr > sess.textlines + rowoff - 1) {
    rowoff =  fr - sess.textlines + 1;
  }

  if (fr < rowoff) {
    rowoff =  fr;
  }

  if (fc > titlecols + coloff - 1) {
    coloff =  fc - titlecols + 1;
  }

  if (fc < coloff) {
    coloff =  fc;
  }


  cx = fc - coloff;
  cy = fr - rowoff;
}

void Organizer::outlineSave(const std::string& fname) {
  if (rows.empty()) return;

  std::ofstream f;
  f.open(fname);
  f << outlineRowsToString();
  f.close();

  //sess.showOrgMessage("Can't save! I/O error: %s", strerror(errno));
  sess.showOrgMessage("saved to outline.txt");
}

void Organizer::get_preview(int id) {
  std::stringstream query;
  preview_rows.clear();
  query << "SELECT note FROM task WHERE id = " << id;
  if (!sess.db_query(sess.S.db, query.str().c_str(), preview_callback, nullptr, &sess.S.err_msg, __func__)) return;

  if (taskview != BY_FIND) draw_preview();
  else {
    word_positions.clear(); 
    get_search_positions(id);
    draw_search_preview();
  }
  //draw_preview();

  if (sess.lm_browser) {
    int folder_tid = sess.get_folder_tid(rows.at(fr).id);
    if (!(folder_tid == 18 || folder_tid == 14)) sess.update_html_file("assets/" + CURRENT_NOTE_FILE);
    else sess.update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
}

void Organizer::draw_preview(void) {

  char buf[50];
  std::string ab;
  int width = sess.totaleditorcols - 10;
  int length = sess.textlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, sess.divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  char lf_ret[10];
  // \x1b[NC moves cursor forward by N columns
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 6);
  //ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  sess.divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", sess.totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  sess.divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  //snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;44$r\x1b[*x", 
  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, sess.divider+7, TOP_MARGIN+4+length, sess.divider+7+width);
  if (preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }

  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  ab.append(generateWWString(preview_rows, width, length, lf_ret));
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

void Organizer::draw_search_preview(void) {
  //need to bring back the note with some marker around the words that
  //we search and replace or retrieve the note with the actual
  //escape codes and not worry that the word wrap will be messed up
  //but it shouldn't ever split an escaped word.  Would start
  //with escapes and go from there
 //fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE fts MATCH '" << search_terms << "' ORDER BY rank";
 //fts_query << "SELECT highlight(fts, ??1, '\x1b[48;5;17m', '\x1b[49m') FROM fts WHERE lm_id=? AND fts MATCH '" << search_terms << "' ORDER BY rank";

  char buf[50];
  std::string ab;
  int width = sess.totaleditorcols - 10;
  int length = sess.textlines - 10;
  //hide the cursor
  ab.append("\x1b[?25l");
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 6, sess.divider + 6);
  ab.append(buf, strlen(buf));
  //std::string abs = "";
 
  // \x1b[NC moves cursor forward by N columns
  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 6);

  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf0;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf0 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  sess.divider + 7 << "H";
  ab.append(buf0.str());

  //erase set number of chars on each line
  char erase_chars[10];
  snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", sess.totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  std::stringstream buf2;
  buf2 << "\x1b[" << TOP_MARGIN + 6 << ";" <<  sess.divider + 7 << "H";
  ab.append(buf2.str()); //reposition cursor

  //snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;44$r\x1b[*x", 
  snprintf(buf, sizeof(buf), "\x1b[2*x\x1b[%d;%d;%d;%d;48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, sess.divider+7, TOP_MARGIN+4+length, sess.divider+7+width);
  if (preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }
  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  std::string t = generateWWString(preview_rows, width, length, "\f");
  //ab.append(generateWWString(O.preview_rows, width, length, lf_ret));
  highlight_terms_string(t);

  size_t p = 0;
  for (;;) {
    if (p > t.size()) break;
    p = t.find('\f', p);
    if (p == std::string::npos) break;
    t.replace(p, 1, lf_ret);
    p +=7;
   }

  ab.append(t);
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

std::string Organizer::draw_preview_box(int width, int length) {
  std::string ab;
  fmt::memory_buffer move_cursor;
  fmt::format_to(move_cursor, "\x1b[{}C", width);
  ab.append("\x1b(0"); // Enter line drawing mode
  fmt::memory_buffer buf;
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, sess.divider + 6); 
  ab.append(buf.data(), buf.size());
  buf.clear();
  ab.append("\x1b[37;1ml"); //upper left corner
  for (int j=1; j<length; j++) { //+1
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5 + j, sess.divider + 6); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    // x=0x78 vertical line (q=0x71 is horizontal) 37=white; 1m=bold (only need 1 m)
    ab.append("\x1b[37;1mx");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mx");
  }
  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 4 + length, sess.divider + 6));
  ab.append("\x1b[1B");
  ab.append("\x1b[37;1mm"); //lower left corner

  move_cursor.clear();
  fmt::format_to(move_cursor, "\x1b[1D\x1b[{}B", length);
  for (int j=1; j<width+1; j++) {
    fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, sess.divider + 6 + j); 
    ab.append(buf.data(), buf.size());
    buf.clear();
    ab.append("\x1b[37;1mq");
    ab.append(move_cursor.data(), move_cursor.size());
    ab.append("\x1b[37;1mq");
  }
  ab.append("\x1b[37;1mj"); //lower right corner
  fmt::format_to(buf, "\x1b[{};{}H", TOP_MARGIN + 5, sess.divider + 7 + width); 
  ab.append(buf.data(), buf.size());
  ab.append("\x1b[37;1mk"); //upper right corner

  //exit line drawing mode
  ab.append("\x1b(B");
  ab.append("\x1b[0m");
  ab.append("\x1b[?25h", 6); //shows the cursor
  return ab;
}

std::string Organizer::generateWWString(std::vector<std::string> &rows, int width, int length, std::string ret) {
  if (rows.empty()) return "";

  std::string ab = "";
  //int y = -line_offset; **set to zero because always starting previews at line 0**
  int y = 0;
  int filerow = 0;

  for (;;) {
    //if (filerow == rows.size()) {last_visible_row = filerow - 1; return ab;}
    if (filerow == rows.size()) return ab;

    std::string_view row = rows.at(filerow);
    
    if (row.empty()) {
      if (y == length - 1) return ab;
      ab.append(ret);
      filerow++;
      y++;
      continue;
    }

    size_t pos;
    size_t prev_pos = 0; //this should really be called prev_pos_plus_one
    for (;;) {
      // if remainder of line is less than screen width
      if (prev_pos + width > row.size() - 1) {
        ab.append(row.substr(prev_pos));

        if (y == length - 1) return ab;
        ab.append(ret);
        y++;
        filerow++;
        break;
      }

      pos = row.find_last_of(' ', prev_pos + width - 1);
      if (pos == std::string::npos || pos == prev_pos - 1) {
        pos = prev_pos + width - 1;
      }
      ab.append(row.substr(prev_pos, pos - prev_pos + 1));
      if (y == length - 1) return ab; //{last_visible_row = filerow - 1; return ab;}
      ab.append(ret);
      y++;
      prev_pos = pos + 1;
    }
  }
}

void Organizer::highlight_terms_string(std::string &text) {

  std::string delimiters = " |,.;?:()[]{}&#/`-'\"—_<>$~@=&*^%+!\t\f\\"; //must have \f if using as placeholder

  for (auto v: word_positions) { //v will be an int vector of word positions like 15, 30, 70
    int word_num = -1;
    auto pos = v.begin(); //pos = word count of the next word
    auto prev_pos = pos;
    int end = -1; //this became a problem in comparing -1 to unsigned int (always larger)
    int start;
    for (;;) {
      if (end >= static_cast<int>(text.size()) - 1) break;
      //if (word_num > v.back()) break; ///////////
      start = end + 1;
      end = text.find_first_of(delimiters, start);
      if (end == std::string::npos) end = text.size() - 1;
      
      if (end != start) word_num++;

      // start the search from the last match? 12-23-2019
      pos = std::find(pos, v.end(), word_num); // is this saying if word_num is in the vector you have a match
      if (pos != v.end()) {
        //editorHighlightWord(n, start, end-start); put escape codes or [at end and start]
        text.insert(end, "\x1b[48;5;235m"); //49m"); //48:5:235
        text.insert(start, "\x1b[48;5;31m");
        if (pos == v.end() - 1) break;
        end += 21;
        pos++;
        prev_pos = pos;
      } else pos = prev_pos; //pos == v.end() => the current word position was not in v[n]
    }
  }
}

void Organizer::get_search_positions(int id) {
  std::stringstream query;
  query << "SELECT rowid FROM fts WHERE lm_id = " << id << ";";

  int rowid = -1;
  // callback is *not* called if result (argv) is null
  if (!sess.db_query(sess.S.fts_db, query.str().c_str(), rowid_callback, &rowid, &sess.S.err_msg, __func__)) return;

  // split string into a vector of words
  std::vector<std::string> vec;
  std::istringstream iss(sess.fts_search_terms);
  for(std::string ss; iss >> ss; ) vec.push_back(ss);
  std::stringstream query3;
  int n = 0;
  for(auto v: vec) {
    word_positions.push_back(std::vector<int>{});
    query.str(std::string()); // how you clear a stringstream
    query << "SELECT offset FROM fts_v WHERE doc =" << rowid << " AND term = '" << v << "' AND col = 'note';";
    if (!sess.db_query(sess.S.fts_db, query.str().c_str(), offset_callback, &n, &sess.S.err_msg, __func__)) return;

    n++;
  }

  int ww = (word_positions.at(0).empty()) ? -1 : word_positions.at(0).at(0);
  sess.showOrgMessage("Word position first: %d; id = %d ", ww, id);

  //if (lm_browser) update_html_file("assets/" + CURRENT_NOTE_FILE);
  /*
  if (lm_browser) {
    if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  } 
  */
}

void Organizer::outlineInsertChar(int c) {
  if (rows.size() == 0) return;
  orow& row = rows.at(fr);
  if (row.title.empty()) row.title.push_back(c);
  else row.title.insert(row.title.begin() + fc, c);
  row.dirty = true;
  fc++;
}

std::string Organizer::outlineRowsToString(void) {
  std::string s = "";
  for (auto i: rows) {
      s += i.title;
      s += '\n';
  }
  s.pop_back(); //pop last return that we added
  return s;
}

void Organizer::displayContainerInfo(Container &c) {

  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < sess.textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
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

  if (view == CONTEXT || view == FOLDER) {
    if (org.view == CONTEXT) {
      auto it = std::ranges::find_if(context_map, [&c](auto& z) {return z.second == c.tid;});
      sprintf(str,"context: %s", it->first.c_str());
    } else if (org.view == FOLDER) {
      auto it = std::ranges::find_if(folder_map, [&c](auto& z) {return z.second == c.tid;});
      sprintf(str,"folder: %s", it->first.c_str());
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

// not sure if this should be here or not
void Organizer::display_container_info(int id) {
  if (id ==-1) return;

  std::string table;
  std::string count_query;
  int (*callback)(void *, int, char **, char **);

  switch(view) {
    case CONTEXT:
      table = "context";
      callback = context_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN context ON context.tid = task.context_tid WHERE context.id = ";
      break;
    case FOLDER:
      table = "folder";
      callback = folder_info_callback;
      count_query = "SELECT COUNT(*) FROM task JOIN folder ON folder.tid = task.folder_tid WHERE folder.id = ";
      break;
    case KEYWORD:
      table = "keyword";
      callback = keyword_info_callback;
      count_query = "SELECT COUNT(*) FROM task_keyword WHERE keyword_id = ";
      break;
    default:
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }
  std::stringstream query;
  int count = 0;

  query << count_query << id;
    
  // note count obtained here but passed to the next callback so it can be printed
  if (!sess.db_query(sess.S.db, query.str().c_str(), count_callback, &count, &sess.S.err_msg)) return;

  std::stringstream query2;
  query2 << "SELECT * FROM " << table << " WHERE id = " << id;

  // callback is *not* called if result (argv) is null

  if (!sess.db_query(sess.S.db, query2.str().c_str(), callback, &count, &sess.S.err_msg)) return;

}

int Organizer::preview_callback (void *NotUsed, int argc, char **argv, char **azColName) {

  UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  if (!argv[0]) return 0; ////////////////////////////////////////////////////////////////////////////
  std::string note(argv[0]);
  //note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::erase(note, '\r'); //c++20
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    //snote will not contain the '\n'
    preview_rows.push_back(s);
  }
  return 0;
}

int Organizer::rowid_callback (void *rowid, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *rwid = static_cast<int*>(rowid);
  *rwid = atoi(argv[0]);
  return 0;
}

int Organizer::offset_callback (void *n, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  int *nn= static_cast<int*>(n);

  word_positions.at(*nn).push_back(atoi(argv[0]));

  return 0;
}

int Organizer::count_callback (void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *cnt = static_cast<int*>(count);
  *cnt = atoi(argv[0]);
  return 0;
}

int Organizer::context_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: "default" = Boolean ? what this is; sql has to use quotes to refer to column
  4: created = 2016-08-05 23:05:16.256135
  5: deleted => bool
  6: icon => string 32
  7: textcolor, Integer
  8: image, largebinary
  9: modified
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < sess.textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);
  //auto it = std::find_if(std::begin(context_map), std::end(context_map),
  //                       [&tid](auto& p) { return p.second == tid; }); //auto&& also works

  auto it = std::ranges::find_if(context_map, [&tid](auto& z) {return z.second == tid;});

  sprintf(str,"context: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/default: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[9]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int Organizer::folder_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  /*
  0: id => int
  1: tid => int
  2: title = string 32
  3: private = Boolean ? what this is
  4: archived = Boolean ? what this is
  5: "order" = integer
  6: created = 2016-08-05 23:05:16.256135
  7: deleted => bool
  8: icon => string 32
  9: textcolor, Integer
  10: image, largebinary
  11: modified
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  //need to erase the screen
  for (int i=0; i < sess.textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"title: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  int tid = atoi(argv[1]);

  //auto it = std::find_if(std::begin(folder_map), std::end(folder_map),
  //                        [&tid](auto& p) { return p.second == tid; }); //auto&& also works

  auto it = std::ranges::find_if(folder_map, [&tid](auto& z) {return z.second == tid;});

  sprintf(str,"folder: %s", it->first.c_str());
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star/private: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"created: %s", argv[6]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[7]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[11]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m", 4);

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}

int Organizer::keyword_info_callback(void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5: deleted
  */
  char lf_ret[10];
  int nchars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1); 

  std::string ab;

  ab.append("\x1b[?25l"); //hides the cursor
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf);

  //need to erase the screen
  for (int i=0; i < sess.textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, nchars);
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2); 
  ab.append(buf, strlen(buf));

  ab.append(COLOR_6); // Blue depending on theme

  char str[300];
  sprintf(str,"id: %s", argv[0]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"tid: %s", argv[2]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);
  sprintf(str,"name: %s", argv[1]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"star: %s", (atoi(argv[3]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"modified: %s", argv[4]);
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"deleted: %s", (atoi(argv[5]) == 1) ? "true": "false");
  ab.append(str, strlen(str));
  ab.append(lf_ret, nchars);

  sprintf(str,"Task count: %d", *static_cast<int*>(count));
  ab.append(str, strlen(str));
  //ab.append(lf_ret, nchars);

  ///////////////////////////

  ab.append("\x1b[0m");

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  return 0;
}
/*
//Database-related Prototypes
void Organizer::update_task_context(std::string &new_context, int id) {
  int context_tid = context_map.at(new_context);
  std::string query = fmt::format("UPDATE task SET context_tid={}, modified=datetime('now') WHERE id={}",
                                    context_tid, id);
  sess.db_query(sess.S.db, query.c_str(), 0, 0, &sess.S.err_msg);
}

void Organizer::update_task_folder(std::string& new_folder, int id) {
  int folder_tid = folder_map.at(new_folder);
  std::string query = fmt::format("UPDATE task SET folder_tid={}, modified=datetime('now') WHERE id={}",
                                    folder_tid, id);
  sess.db_query(sess.S.db, query.c_str(), 0, 0, &sess.S.err_msg, __func__);
}
*/
