#define LEFT_MARGIN 2
#define TOP_MARGIN 1 // Editor.cpp
#define TIME_COL_WIDTH 18 // need this if going to have modified col
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define UNUSED(x) (void)(x)

#include "session.h"
#include "organizer.h"
//#include "common.h" //in organizer.h
#include <sstream>
#include <fstream>

std::map<std::string, int> Organizer::folder_map = {}; //static - filled in by map_folder_titles_[db]
std::map<std::string, int> Organizer::context_map = {}; //static filled in by map_context_titles_[db]

Organizer org = Organizer(); //global; extern Session sess in session.h


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
        sess.drawPreviewWindow(rows.at(fr).id); //if id == -1 does not try to retrieve note
      } else {
        Container c = getContainerInfo(rows.at(fr).id);
        if (c.id != 0)
          sess.displayContainerInfo(c);
      }
      break;

    case ARROW_DOWN:
    case 'j':
      if (fr < rows.size() - 1) fr++;
      fc = coloff = 0;
      if (view == TASK) {
        sess.drawPreviewWindow(rows.at(fr).id); //if id == -1 does not try to retrieve note
      } else {
        Container c = getContainerInfo(rows.at(fr).id);
        if (c.id != 0) 
          sess.displayContainerInfo(c);
      }
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
