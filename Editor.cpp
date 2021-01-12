#include "listmanager.h"
#include "Editor.h"
#include "session.h"
#include "Dbase.h"
#include <fstream>
#include <cstdarg> //va_start etc
#include <nuspell/finder.hxx>
#include <string_view>
#include <unordered_set>
//#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <array>
#include <unordered_map>
#include "base64.hpp"
#include "pstream.h"

#define UNUSED(x) (void)(x)
#define LEFT_MARGIN_OFFSET 4

using namespace redi;

// static members of Editor class
std::vector<std::string> Editor::line_buffer = {}; 
std::string Editor::string_buffer = {}; 
//int Editor::total_screenlines = 0; 
//int Editor::origin = 0;
char Editor::message[120]{};

std::unordered_set<std::string> line_commands = {"I", "i", "A", "a", "s", "cw", "caw", "x", "d$", "daw", "dw", "r", "~"};

// used by %
std::pair<int,int> Editor::move_to_right_brace(char left_brace) {
  int r = fr;
  int c = fc + 1;
  int count = 1;
  int max = rows.size();

  const std::unordered_map<char,char> m{{'{','}'}, {'(',')'}, {'[',']'}};
  char right_brace = m.at(left_brace);

  for (;;) {

  std::string &row = rows.at(r);

    // right now this function only called from NORMAL mode by typing '%'
    // note that function that deals with INSERT needs  c >= row.size() because
    // brace could be at end of line and fc could be row.size() before doing fc + 1
    if (c == row.size()) { 
      r++;
      if (r == max) {
        editorSetMessage("Couldn't find matching brace");
        return std::make_pair(fr,fc);
      }
      c = 0;
      continue;
    }

    if (row.at(c) == right_brace) {
      count -= 1;
      if (count == 0) return std::make_pair(r,c);
    } else if (row.at(c) == left_brace) count += 1;

    c++;
  }
}

// used by %
std::pair<int,int> Editor::move_to_left_brace(char right_brace) {
  int r = fr;
  int c = fc - 1;
  int count = 1;

  const std::unordered_map<char,char> m{{'}','{'}, {')','('}, {']','['}};
  char left_brace = m.at(right_brace);

  std::string row = rows.at(r);

  for (;;) {

    if (c == -1) { //fc + 1 can be greater than row.size on first pass from INSERT if { at end of line
      r--;
      if (r == -1) {
        editorSetMessage("Couldn't find matching brace");
        return std::make_pair(fr,fc);
      }
      row = rows.at(r);
      c = row.size() - 1;
      continue;
    }

    if (row.at(c) == left_brace) {
      count -= 1;
      if (count == 0) return std::make_pair(r,c);
    } else if (row.at(c) == right_brace) count += 1;

    c--;
  }
}

//triggered by % in NORMAL mode
void Editor::E_move_to_matching_brace(int repeat) {
  std::pair<int,int> pos;
  char c = rows.at(fr).at(fc);
  const std::string left = "{([";
  auto it = left.find(c);
  if (it != std::string::npos) { 
    pos = move_to_right_brace(c);
    fr = pos.first;
    fc = pos.second;
  } else { 
    const std::string right = "})]";
    it = right.find(c);
    if (it != std::string::npos) {
      pos = move_to_left_brace(c);
      fr = pos.first;
      fc = pos.second;
    }
  }  
}

//'automatically' happens in NORMAL and INSERT mode
//return true -> redraw; false -> don't redraw
bool Editor::find_match_for_left_brace(char left_brace, bool back) {
  int r = fr;
  int c = fc + 1;
  int count = 1;
  int max = rows.size();

  const std::unordered_map<char,char> m{{'{','}'}, {'(',')'}};
  char right_brace = m.at(left_brace);

  //editorSetMessage("left brace: {}", left_brace);
  for (;;) {

    std::string &row = rows.at(r);

    // need >= because brace could be at end of line and in INSERT mode
    // fc could be row.size() [ie beyond the last char in the line
    // and so doing fc + 1 above leads to c > row.size()
    if (c >= row.size()) {
      r++;
      if (r == max) {
        editorSetMessage("Couldn't find matching brace");
        return false;
      }
      c = 0;
      continue;
    }

    if (row.at(c) == right_brace) {
      count -= 1;
      if (count == 0) break;
    } else if (row.at(c) == left_brace) count += 1;

    c++;
  }
  int y = editorGetScreenYFromRowColWW(r, c) - line_offset; 
  //if (y == screenlines) return false;
  if (y >= screenlines) return false;

  int x = editorGetScreenXFromRowColWW(r, c) + left_margin + left_margin_offset + 1;
  std::stringstream s;
  s << "\x1b[" << y + top_margin << ";" << x << "H" << "\x1b[48;5;244m"
    << right_brace;
    //<< "\x1b[0m";

  x = editorGetScreenXFromRowColWW(fr, fc-back) + left_margin + left_margin_offset + 1;
  y = editorGetScreenYFromRowColWW(fr, fc-back) + top_margin - line_offset; // added line offset 12-25-2019
  s << "\x1b[" << y << ";" << x << "H" << "\x1b[48;5;244m" //"\x1b[48;5;31m"
    << left_brace
    << "\x1b[0m";
  write(STDOUT_FILENO, s.str().c_str(), s.str().size());
  editorSetMessage("r = %d   c = %d", r, c);
  return true;
}


//'automatically' happens in NORMAL and INSERT mode
bool Editor::find_match_for_right_brace(char right_brace, bool back) {
  int r = fr;
  int c = fc - 1 - back;
  int count = 1;

  std::string row = rows.at(r);
  //char left_brace = (right_brace == '}') ? '{' : '(';
  const std::unordered_map<char,char> m{{'}','{'}, {')','('}};
  char left_brace = m.at(right_brace);

  for (;;) {

    if (c == -1) { //fc + 1 can be greater than row.size on first pass from INSERT if { at end of line
      r--;
      if (r == -1) {
        editorSetMessage("Couldn't find matching brace");
        return false;
      }
      row = rows.at(r);
      c = row.size() - 1;
      continue;
    }

    if (row.at(c) == left_brace) {
      count -= 1;
      if (count == 0) break;
    } else if (row.at(c) == right_brace) count += 1;

    c--;
  }
  int y = editorGetScreenYFromRowColWW(r, c) - line_offset; 
  if (y < 0) return false;

  int x = editorGetScreenXFromRowColWW(r, c) + left_margin + left_margin_offset + 1;
  std::stringstream s;
  s << "\x1b[" << y + top_margin << ";" << x << "H" << "\x1b[48;5;244m"
    << left_brace;
    //<< "\x1b[0m";

  x = editorGetScreenXFromRowColWW(fr, fc-back) + left_margin + left_margin_offset + 1;
  y = editorGetScreenYFromRowColWW(fr, fc-back) + top_margin - line_offset; // added line offset 12-25-2019
  s << "\x1b[" << y << ";" << x << "H" << "\x1b[48;5;244m"
    << right_brace
    << "\x1b[0m";
  write(STDOUT_FILENO, s.str().c_str(), s.str().size());
  editorSetMessage("r = %d   c = %d", r, c);
  return true;
}

void Editor::draw_highlighted_braces(void) {

  // below is code to automatically find matching brace - should be in separate member function
  std::string braces = "{}()";
  char c;
  bool back;
  //if below handles case when in insert mode and brace is last char
  //in a row and cursor is beyond that last char (which is a brace)
  if (fc == rows.at(fr).size()) {
    c = rows.at(fr).at(fc-1); 
    back = true;
  } else {
    c = rows.at(fr).at(fc);
    back = false;
  }
  size_t pos = braces.find(c);
  if ((pos != std::string::npos)) {
    switch(c) {
      case '{':
      case '(':  
        redraw = find_match_for_left_brace(c, back);
        return;
      case '}':
      case ')':
        redraw = find_match_for_right_brace(c, back);
        return;
      //case '(':  
      default://should not need this
        return;
    }
  } else if (fc > 0 && mode == INSERT) {
      char c = rows.at(fr).at(fc-1);
      size_t pos = braces.find(c);
      if ((pos != std::string::npos)) {
        switch(rows.at(fr).at(fc-1)) {
          case '{':
          case '(':  
            redraw = find_match_for_left_brace(c, true);
            return;
          case '}':
          case ')':
            redraw = find_match_for_right_brace(c, true);
            return;
          //case '(':  
          default://should not need this
            return;
      }        
    } else {redraw = false;}
  } else {redraw = false;}

}

void Editor::setLinesMargins(void) { //also sets top margin

  if(linked_editor) {
    if (is_subeditor) {
      if (is_below) {
        screenlines = LINKED_NOTE_HEIGHT;
        top_margin = sess.textlines - LINKED_NOTE_HEIGHT + 2;
      } else {
        screenlines = sess.textlines;
        top_margin =  TOP_MARGIN + 1;
      }
    } else {
      if (linked_editor->is_below) {
        screenlines = sess.textlines - LINKED_NOTE_HEIGHT - 1;
        top_margin =  TOP_MARGIN + 1;
      } else {
        screenlines = sess.textlines;
        top_margin =  TOP_MARGIN + 1;
      }
    }
  } else {
    screenlines = sess.textlines;
    top_margin =  TOP_MARGIN + 1;
  }
}

// this is what needs to be done to undo the cmd that was entered
enum Undo_method {
  CHANGE_ROW, //x,s,i,a,A,c,d
  ADD_ROWS, //dd
  DELETE_ROWS, //o,O,p,P
  CHANGE_ROW_AND_DELETE_ROWS, //i,I,c,s where the person not only inserts text into current line but adds lines via typing return  
  REPLACE_NOTE = 99//when in doubt
};

// used by push_current to get the num_rows in inserted text
int Editor::get_num_rows(std::string & str) {
  int n = 1;
  for (int i=0; (i=str.find('\r', i)) != std::string::npos; i++) {
    n++;
  }
  return n;
}

std::vector<std::string> Editor::str2vecWW(std::string str, bool ascii_only) {
  std::vector<std::string> vec;
  int pos = 0;
  int prev_pos = 0;
  std::string s;

  pos = str.find('\t');
  while(pos != std::string::npos) {
    str.replace(pos, 1, "  "); // number is number of chars to replace
    pos = str.find('\t');
  }

  for(;;) {
    pos = str.find('\n', prev_pos);  

    if (pos == std::string::npos) {
      s = str.substr(prev_pos);
      for(;;) {
        if (s.size() < screencols - left_margin_offset) {
          vec.push_back(s);
          break;
        } else {
          vec.push_back(s.substr(0, screencols - left_margin_offset - 1));
          s = s.substr(screencols - left_margin_offset - 1); 
        }
      }  
      break;
    }

      s = str.substr(prev_pos, pos - prev_pos); //2nd parameter is length!
      for(;;) {
        if (s.size() < screencols - left_margin_offset) {
          vec.push_back(s);
          break;
        } else {
          vec.push_back(s.substr(0, screencols - left_margin_offset - 1));
          s = s.substr(screencols - left_margin_offset - 1); 
        }
      }  

    if (pos == str.size() - 1) {
      vec.push_back(std::string());
      break;
    }
    prev_pos = pos + 1;
  }

  if (!ascii_only) return vec;

  for (auto &s :  vec) {
    for (int i=0; i< s.size(); ++i) {if (!isprint(s[i])) s[i] = '?';}
  }
  return vec;
}

/*
std::vector<std::string> Editor::str2vecWW(std::string str) {
  std::vector<std::string> vec;
  int pos = 0;
  int prev_pos = 0;
  std::string s;
  for(;;) {
    pos = str.find('\n', prev_pos);  

    if (pos == std::string::npos) {
      s = str.substr(prev_pos);
      for(;;) {
        if (s.size() < screencols - left_margin_offset) {
          vec.push_back(s);
          break;
        } else {
          vec.push_back(s.substr(0, screencols - left_margin_offset - 1));
          s = s.substr(screencols - left_margin_offset - 1); 
        }
      }  
      return vec;
    }

      s = str.substr(prev_pos, pos - prev_pos); //2nd parameter is length!
      for(;;) {
        if (s.size() < screencols - left_margin_offset) {
          vec.push_back(s);
          break;
        } else {
          vec.push_back(s.substr(0, screencols - left_margin_offset - 1));
          s = s.substr(screencols - left_margin_offset - 1); 
        }
      }  

    if (pos == str.size() - 1) {
      vec.push_back(std::string());
      return vec;
    }
    prev_pos = pos + 1;
  }
  return vec;
}
*/

/* Basic Editor actions */
void Editor::editorInsertReturn(void) { // right now only used for editor->INSERT mode->'\r'
  std::string& current_row = rows.at(fr);
  std::string new_row1(current_row.begin(), current_row.begin() + fc);
  std::string new_row2(current_row.begin() + fc, current_row.end());

  int indent = (smartindent) ? editorIndentAmount(fr) : 0;

  fr++;
  current_row = new_row1;
  rows.insert(rows.begin() + fr, new_row2);

  if (fc==0) return; //01082020

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
  if (fr + 1 > rows.size() - 1) {return;}
  else {fr++; fc = 0;}
} else fc++;

  int r = fr;
  int c = fc;
  int pos;
  std::string delimiters = " *%!^<>,.;?:()[]{}&#~'\"";
  std::string delimiters_without_space = "*%!^<>,.;?:()[]{}&#~'\"";

  for(;;) {

    if (r > rows.size() - 1) {return;}

    std::string &row = rows.at(r);

    if (row.empty()) {r++; c = 0; continue;}

    if (isalnum(row.at(c))) { //we are on an alphanumeric
      if (c == row.size() - 1 || ispunct(row.at(c + 1))) {fc = c; fr = r; return;}

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

/*
void Editor::push_base(void) {
  Diff d;
  d.fr = 0; // not sure this is right - might be just d.fr = fr

  d.fc = undo_deque.at(0).fc;
  d.repeat = 1;
  d.command = command;

  if (undo_deque.at(0).command == "o") { //previous
    for (int r=undo_deque.at(0).fr; r<5; r++) {
      if (r > (int)rows.size()-1) break;
      d.changed_rows.push_back(std::make_pair(r, rows.at(r)));
    }
  }

  for (auto [r, s] : undo_deque.at(0).changed_rows) {
    d.changed_rows.push_back(std::make_pair(r, rows.at(r)));
  }

  //d.changed_rows.push_back(std::make_pair(d.fr, rows.at(d.fr)));
  undo_deque.push_front(d);
}
*/

void Editor::push_current(void) {
  // it might be better to create a single empty row in this circumstance
  if (rows.empty()) return; //don't create snapshot if there is no text

  // probably not reason to include last_typed as always "" at this point
  Diff d = {prev_fr, prev_fc, last_repeat, last_command};
  d.inserted_text = last_typed;
  d.num_rows = get_num_rows(last_typed);

  if (line_commands.contains(d.command)) {
    if (d.num_rows == 1) d.undo_method = CHANGE_ROW;
    else d.undo_method = CHANGE_ROW_AND_DELETE_ROWS;
    d.rows.push_back(snapshot.at(d.fr));
  } else if (d.command == "dd") {
      d.undo_method = ADD_ROWS;
      d.rows.insert(d.rows.begin(), snapshot.begin()+d.fr, snapshot.begin()+d.fr+repeat);
  } else if (d.command == "p") {
      if (!line_buffer.empty()) {
        d.undo_method = DELETE_ROWS;
        d.rows = line_buffer; //this is to redo the paste
        d.num_rows = line_buffer.size();
        //d.fr++;
      } else {
        d.undo_method = CHANGE_ROW;
        d.num_rows = 1;
        d.rows.push_back(snapshot.at(d.fr));
      }
  } else if (d.command == "o" || d.command == "O") {
      d.undo_method = DELETE_ROWS;
  } else {
      d.undo_method = REPLACE_NOTE;
      d.rows = snapshot;
  }

  if (d_index != 0) undo_deque.clear(); //if you haven't redone everything, undo/redo starts again

  undo_deque.push_front(d);
  d_index = 0;
  //undo_mode = false;
  //snapshot = rows; //this is the snapshot used to pick a row or the whole thing

  std::string temp_str = d.inserted_text;

  // \r is present and \n is not present
  size_t pos = temp_str.find('\r');
  while(pos != std::string::npos) {
    temp_str.replace(pos, 1, "\\r");
    pos = temp_str.find('\r', pos + 2);
  }

  /*Useful for debugging
  editorSetMessage("index: %d; cmd: %s; repeat: %d; fr: %d; fc: %d rows.size %d undo method: %d; text: %s", 
                      d_index,
                      d.command.c_str(), 
                      d.repeat, 
                      d.fr,
                      d.fc,
                      d.rows.size(),
                      d.undo_method,
                      temp_str.c_str());
  */
}

// right now only used if dot command issued in editor NORMAL mode
void Editor::push_previous(void) {
  if (rows.empty()) return; //don't create snapshot if there is no text

  //really need a copy constructor although maybe we have one
  //Diff d = undo_deque.at(0)  
  Diff &last_d = undo_deque.at(0);
  Diff d = {prev_fr, prev_fc, last_d.repeat, last_d.command};
  d.inserted_text = last_d.inserted_text;
  d.num_rows = last_d.num_rows;
  //d.rows = last_d.rows;
  d.undo_method = last_d.undo_method;

  if (line_commands.count(d.command)) {
    d.rows.push_back(snapshot.at(d.fr));
  } else if (d.command == "dd") {
      d.rows.insert(d.rows.begin(), snapshot.begin()+d.fr, snapshot.begin()+d.fr+repeat);
  } else if (d.command == "p") {
      d.rows = line_buffer; //this is to redo the paste
      d.fr++;
  } else {
      d.undo_method = REPLACE_NOTE;
      d.rows = snapshot; //this is old snapshot
  }
  undo_deque.push_front(d);
  d_index = 0;
  //undo_mode = false;
  //snapshot = rows; //this is the snapshot used to pick a row or the whole thing

  std::string temp_str = d.inserted_text;

  // \r is present and \n is not present
  size_t pos = temp_str.find('\r');
  while(pos != std::string::npos) {
    temp_str.replace(pos, 1, "\\r");
    pos = temp_str.find('\r', pos + 2);
  }

  editorSetMessage("index: %d; cmd: %s; repeat: %d; fr: %d; fc: %d rows.size %d undo method: %d; text: %s", 
                      d_index,
                      d.command.c_str(), 
                      d.repeat, 
                      d.fr,
                      d.fc,
                      d.rows.size(),
                      d.undo_method,
                      temp_str.c_str());
}

void Editor::undo(void) {
  if (undo_deque.empty()) return;

  if (d_index == (int)undo_deque.size()) { //with no push base
    editorSetMessage("Already at oldest change");
    return;
  }

  Diff d = undo_deque.at(d_index);

  if (d.undo_method == CHANGE_ROW) {
    rows.at(d.fr) = d.rows.at(0);
  } else if (d.undo_method == ADD_ROWS) {
    rows.insert(rows.begin()+d.fr, d.rows.begin(), d.rows.end());
  } else if (d.undo_method == DELETE_ROWS) {
      //? use num_rows and not rows.size because no reason to create rows
      if (d.command == "O") rows.erase(rows.begin()+d.fr, rows.begin()+d.fr+d.num_rows);
      else rows.erase(rows.begin()+d.fr+1, rows.begin()+d.fr+d.num_rows+1);
  } else if (d.undo_method == CHANGE_ROW_AND_DELETE_ROWS) {
    rows.at(d.fr) = d.rows.at(0);
    rows.erase(rows.begin()+d.fr+1, rows.begin()+d.fr+d.num_rows);//not +1 in case of paste
  } else {
    rows = d.rows;
  }

  fr = d.fr;
  fc = d.fc;
  //snapshot = rows; // this might be necessary in redo? //or apply the 'patches' to snaphot too??? removed 11-19-2020

  editorSetMessage("d_index: %d undo_deque.size(): %d; command: %s; undo method: %d", d_index, undo_deque.size(), d.command.c_str(), d.undo_method);
  d_index++;
}

void Editor::redo(void) {

  if (d_index > 0) {
    d_index--;
  } else {
    editorSetMessage("Already at newest change");
    return;
  }

  Diff &d = undo_deque.at(d_index);
  fr = d.fr;
  fc = d.fc;
  last_typed = d.inserted_text; //? needed if you dot the redo - can you do that
  //rows = d.rows;

  // i I a A 
  if (cmd_map1.count(d.command)) {
    (this->*cmd_map1.at(d.command))(d.repeat);

    for (int n=0; n<d.repeat; n++) {
      for (char const &c : d.inserted_text) {
        if (c == '\r') editorInsertReturn();
        else editorInsertChar(c);
      }
    }

  // o O cmd_map2 -> E_o_escape E_O_escape
  } else if (cmd_map2.count(d.command)) {
    (this->*cmd_map2.at(d.command))(d.repeat);

  // x dw daw dd de dG d$
  } else if (cmd_map3.count(d.command)) {
    (this->*cmd_map3.at(d.command))(d.repeat);

  // cw caw s
  } else if (cmd_map4.count(d.command)) {
      (this->*cmd_map4.at(d.command))(d.repeat);

    for (char const &c : d.inserted_text) {
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }

  } else if (last_command == "~") {
    E_change_case(d.repeat);

  } else if (last_command == "r") {
    E_replace(d.repeat);

  } else if (last_command =="p") {
    E_paste(d.repeat);
    return;
  }
  // they may have been changed by the actions above
  fr = d.fr;
  fc = d.fc;

  editorSetMessage("d_index: %d undo_deque.size(): %d; command: %s; undo method: %d inserted_text %s", 
                    d_index,
                    undo_deque.size(),
                    d.command.c_str(),
                    d.undo_method,
                    d.inserted_text.c_str());
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

// if only use of last_visible_row
// ? just use
// int y = editorGetScreenYFromRowColWW(n, 0) + top_margin - line_offset; 
void Editor::editorSpellCheck(void) {

  if (rows.empty()) return;

  pos_mispelled_words.clear();

  /*
  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  auto dict = nuspell::Dictionary::load_from_path(path);
 */

  /*
  auto dict_list = std::vector<std::pair<std::string, std::string>>{};
  nuspell::search_default_dirs_for_dicts(dict_list);
  auto dict_name_and_path = nuspell::find_dictionary(dict_list, "en_US");
  auto & dict_path = dict_name_and_path->second;
  auto dict = nuspell::Dictionary::load_from_path(dict_path);
  */

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
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + top_margin, cx + left_margin + left_margin_offset + 1); //03022019
  write(STDOUT_FILENO, buf, strlen(buf));
}

// called by editorSpellCheck
void Editor::editorHighlightWord(int r, int c, int len) {
  std::string &row = rows.at(r);
  int x = editorGetScreenXFromRowColWW(r, c) + left_margin + left_margin_offset + 1;
  int y = editorGetScreenYFromRowColWW(r, c) + top_margin - line_offset; // added line offset 12-25-2019
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
    for (int i=0; i<line.size(); ++i) if (!isascii(line[i])) line[i] = '?';
    rows.push_back(line);
  }
  f.close();

  dirty = true;
  sess.editor_mode = true;
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
  for (auto i : rows) {
      //if (!i.empty()) z += i; //10-21-2020
      z += i;
      z += '\n';
  }
  if (!z.empty()) z.pop_back(); //pop last return that we added
  return z;
}

void Editor::editorInsertChar(int chr) {
  // does not handle returns which must be intercepted before calling this function
  // necessary even with NO_ROWS because putting new entries into insert mode
  if (rows.empty()) { 
    editorInsertRow(0, std::string{});
  }
  std::string &row = rows.at(fr);
  row.insert(row.begin() + fc, chr); // works if row is empty
  dirty++;
  fc++;
}

void Editor::editorDelRow(int r) {
  //editorSetMessage("Row to delete = %d; numrows = %d", fr, numrows); 
  if (rows.empty()) return; // creation of NO_ROWS may make this unnecessary

  rows.erase(rows.begin() + r);
  if (rows.empty()) {
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

void Editor::editorFindNextWord(void) {
  if (rows.empty()) return;

  std::string& row = rows.at(fr);
  int x = fc;
  int y = fr;
  size_t found;

  // are we currently sitting on a match?
  // if so we need to move one char forward
  // need to check if at end of line and end of lines ...
  if (search_string == row.substr(x, search_string.size())) { 
    if (row.size() - 1 != x) x++;
    else {
      x = 0;
      if (rows.size() - 1 != y) y++;
      else y = 0;
    }
  }

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
void Editor::editorRefreshScreen(bool draw) {
  char buf[32];
  std::string ab;
  int tid{0};

  ab.append("\x1b[?25l", 6); //hides the cursor
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", top_margin, left_margin + 1); //03022019 added len
  ab.append(buf, strlen(buf));

  if (draw) {
    // erase the screen - problem erases everything to the right - is there a rectangular erase??????
    char lf_ret[10];
    char erase_chars[10];
    // \x1b[NC moves cursor forward by N columns
    snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin);
    snprintf(erase_chars, sizeof(erase_chars), "\x1b[%dX", screencols);//09062020 added the -1: keeps lines from being erased
    for (int i=0; i < screenlines; i++) {
      ab.append(erase_chars);
      ab.append(lf_ret);
    }

    tid = get_folder_tid(id);
    if ((tid == 18 || tid == 14) && !(is_subeditor)) editorDrawCodeRows(ab);
    else editorDrawRows(ab);
  }

  editorDrawStatusBar(ab);
  editorDrawMessageBar(ab);

  // the lines below position the cursor where it should go
  if (mode != COMMAND_LINE){
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + top_margin, cx + left_margin + left_margin_offset + 1); //03022019
    ab.append(buf, strlen(buf));
  }

  write(STDOUT_FILENO, ab.c_str(), ab.size());

  // can't do the below until ab is written or will just overwite highlights
  if (draw && spellcheck) editorSpellCheck();

  if (rows.empty() || rows.at(fr).empty()) return;

  if (!tid) tid = get_folder_tid(id);
  if ((tid == 18 || tid == 14) && !(is_subeditor)) draw_highlighted_braces();;
}

void Editor::editorDrawMessageBar(std::string& ab) {
  std::stringstream buf;

  // only use of EDITOR_LEFT_MARGIN in Editor.cpp
  //buf  << "\x1b[" << total_screenlines + TOP_MARGIN + 2 << ";" << EDITOR_LEFT_MARGIN << "H";
  buf  << "\x1b[" << sess.textlines + top_margin + 1 << ";" << sess.divider + 1 << "H";
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
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screenlines + TOP_MARGIN + 1, left_margin);
  //snprintf(buf, sizeof(buf), "\x1b[%d;%dH", total_screenlines + top_margin, left_margin);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", screenlines + top_margin, left_margin + 1); //+1
  ab.append(buf);

  //erase from start of an Editor's status bar to the end of the Editor's status bar
  //ab.append("\x1b[K"); //erases from cursor to end of screen on right - not what we want
  snprintf(buf, sizeof(buf), "\x1b[%dX", screencols);
  ab.append(buf);

  ab.append("\x1b[7m"); //switches to inverted colors
  ab.append(" ");
  if (DEBUG) {
    if (!rows.empty()){
      int line = editorGetLineInRowWW(fr, fc);
      int line_char_count = editorGetLineCharCountWW(fr, line);
      int lines = editorGetLinesInRowWW(fr);
      //fr, fc, cx, cy, line chars start at zero; line, lines line 
      len = snprintf(status, sizeof(status),
                                    "fr=%d lines=%d line=%d fc=%d line offset=%d initial row=%d last row=%d line chrs="
                                     "%d  cx=%d cy=%d scols(1)=%d, left_margin=%d rows.size=%ld",
                                     fr, lines, line, fc, line_offset, first_visible_row, last_visible_row, line_char_count,
                                     cx, cy, screencols, left_margin, rows.size());
    } else {
      len =  snprintf(status, sizeof(status),
                                     "E.row is NULL E.cx = %d E.cy = %d  E.numrows = %ld E.line_offset = %d",
                                      cx, cy, rows.size(), line_offset);
    }
  } else {
    std::string title = get_title(id);
    std::string truncated_title = title.substr(0, 30);
    if (dirty) truncated_title.append("[+]"); 
    len = snprintf(status, sizeof(status), "%d - %s ... %s", id, truncated_title.c_str(), (is_subeditor) ? "subeditor" : "");
    }

  if (len > screencols - 1) len = screencols - 1;
  ab.append(status, len);
  while (len < screencols - 1) {
      ab.append(" ", 1);
      len++;
    }
  ab.append("\x1b[0m"); //switches back to normal formatting
}

void Editor::editorDrawRows(std::string &ab) {

  std::string abs = ""; 
 
  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin);
  ab.append("\x1b[?25l"); //hides the cursor

  std::stringstream buf;
  // format for positioning cursor is "\x1b[%d;%dH"
  buf << "\x1b[" << top_margin << ";" <<  left_margin + 1 << "H";
  ab.append(buf.str());

  if (rows.empty()) return;

  int y = 0;
  int filerow = first_visible_row;
  bool flag = false;

  for (;;){
    if (flag) break;
    if (filerow == rows.size()) {last_visible_row = filerow - 1; break;}
    std::string row = rows.at(filerow);

    if (row.empty()) {
      if (y == screenlines - 1) break;
      //ab.append(lf_ret, nchars);
      ab.append(lf_ret);
      filerow++;
      y++;
      continue;
    }

    int pos = -1;
    int prev_pos;
    for (;;) {
      /* this is needed because it deals where the end of the line doesn't have a space*/
      if (row.substr(pos+1).size() <= screencols - left_margin_offset) {
        ab.append(row, pos+1, screencols - left_margin_offset);
        abs.append(row, pos+1, screencols - left_margin_offset);
        if (y == screenlines - 1) {flag=true; break;}
        ab.append(lf_ret);
        y++;
        filerow++;
        break;
      }

      prev_pos = pos;
      pos = row.find_last_of(' ', pos+screencols - left_margin_offset);

      //note npos when signed = -1 and order of if/else may matter
      if (pos == std::string::npos) {
        pos = prev_pos + screencols - left_margin_offset;
      } else if (pos == prev_pos) {
        row = row.substr(pos+1);
        prev_pos = -1;
        pos = screencols - left_margin_offset - 1;
      }

      ab.append(row, prev_pos+1, pos-prev_pos);
      abs.append(row, prev_pos+1, pos-prev_pos);
      if (y == screenlines - 1) {flag=true; break;}
      ab.append(lf_ret);
      y++;
    }
  }
  // ? only used so spellcheck stops at end of visible note
  last_visible_row = filerow - 1; // note that this is not exactly true - could be the whole last row is visible

  draw_visual(ab);
}

void Editor::editorDrawCodeRows(std::string &ab) {
  //save the current file to code_file with correct extension
  std::ofstream myfile;
  myfile.open("code_file"); 
  myfile << editorGenerateWWString();
  myfile.close();

  std::stringstream display;
  std::string line;

  int tid = get_folder_tid(id);
  ipstream highlight(fmt::format("highlight code_file --out-format=xterm256 "
                             "--style=gruvbox-dark-hard-slz --syntax={}",
                             (tid == 18) ? "cpp" : "go"));
  

  while(getline(highlight, line)) {display << line << '\n';}

  /* if we did markdown
  highlight("bat", "code_file", "--style=plain", "--paging=never", 
                               "--color=always", "--language=md.hbs", "--italic-text=always",
                               "--theme=gruvbox-markdown");
  */

  // \x1b[NC moves cursor forward by N columns
  std::string lf_ret = fmt::format("\r\n\x1b[{}C", left_margin);
  ab.append("\x1b[?25l"); //hides the cursor
  ab.append(fmt::format("\x1b[{};{}H", top_margin, left_margin + 1));

  // below draws the line number 'rectangle' only matters for the word-wrapped lines
  ab.append(fmt::format("\x1b[2*x\x1b[{};{};{};{};48;5;235$r\x1b[*x", 
               top_margin, left_margin, top_margin + screenlines, left_margin + left_margin_offset));
  // In the code below `\t' are the returns in lines that word wrap - last return is always '\n'
  int n = 0;
  while(std::getline(display, line, '\n')) {
    //if (n >= line_offset) {
    if (n >= first_visible_row) { //substituted for above on 12312020
      //ab.append(fmt::format("{:>2} ", n));
      ab.append(fmt::format("\x1b[48;5;235m\x1b[38;5;245m{:>3} \x1b[0m", n));
      if (line.find('\t') != std::string::npos) {
        for (;;) {
          size_t pos = line.find('\t');
          if (pos == std::string::npos) break; 
          ab.append(line, 0, pos);
          ab.append(lf_ret);
          ab.append(fmt::format("\x1b[{}C", left_margin_offset));
          line = line.substr(pos + 1);
        }
      }  
      ab.append(line);
      ab.append(lf_ret);
    }
    n++;
  }
  draw_visual(ab);
}

void Editor::draw_visual(std::string &ab) {

  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin + left_margin_offset);

  if (mode == VISUAL_LINE) {

    // \x1b[NC moves cursor forward by N columns
    snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", left_margin + left_margin_offset);

    int h_light[2] = {0,0};
    if (highlight[1] < highlight[0]) { 
      h_light[1] = highlight[0];
      h_light[0] = highlight[1];
    } else {
      h_light[0] = highlight[0];
      h_light[1] = highlight[1];
    }

    int x = left_margin + left_margin_offset + 1;
    //int y = editorGetScreenYFromRowColWW(h_light[0], 0) + top_margin - line_offset; 
    int y = editorGetScreenYFromRowColWW(h_light[0], 0) - line_offset;
    std::stringstream ss;
    if (y >= 0)
      ss << "\x1b[" << y + top_margin << ";" << x << "H" << "\x1b[48;5;244m";
    else
      ss << "\x1b[" <<  top_margin << ";" << x << "H" << "\x1b[48;5;244m";
    ab.append(ss.str());

    for (int n=0; n < (h_light[1]-h_light[0] + 1); ++n) {
      int row_num = h_light[0] + n;
      int pos = 0;
      for (int line=1; line <= editorGetLinesInRowWW(row_num); ++line) {
        if (y < 0) {
          y += 1;
          continue;
        }
        if (y == screenlines) break; //out for should be done (theoretically) - 1
        int line_char_count = editorGetLineCharCountWW(row_num, line);
        ab.append(rows.at(row_num).substr(pos, line_char_count));
        ab.append(lf_ret);    
        y += 1;
        pos += line_char_count;
      }
    }
  }

  if (mode == VISUAL) {

    int h_light[2] = {0,0};
    if (highlight[1] < highlight[0]) { 
      h_light[1] = highlight[0];
      h_light[0] = highlight[1];
    } else {
      h_light[0] = highlight[0];
      h_light[1] = highlight[1];
    }

    // someday cleanup the multiple calls to editorGetLineInRow
    int pos = editorGetScreenXFromRowColWW(fr, h_light[0]);
    int x = pos + left_margin + left_margin_offset + 1;
    int y = editorGetScreenYFromRowColWW(fr, h_light[0]) + top_margin - line_offset; 
    std::string fragment = rows.at(fr).substr(h_light[0], h_light[1] - h_light[0] + 1);
    std::stringstream ss;
    ss << "\x1b[" << y << ";" << x << "H" << "\x1b[48;5;244m";
    ab.append(ss.str());
    int lines_in_row = editorGetLinesInRowWW(fr);
    if (lines_in_row == 1) {
      ab.append(fragment);
      editorSetMessage("In only one line in the row");
    } else if (editorGetLineInRowWW(fr, h_light[0]) == editorGetLineInRowWW(fr, h_light[1])) {
      ab.append(fragment);
      editorSetMessage("multiline but beginning and end in same line");
    } else {
      int start_line = editorGetLineInRowWW(fr, h_light[0]);
      int line_char_count = editorGetLineCharCountWW(fr, start_line);
      //editorSetMessage("fragment = %s; pos = %d; lcc = %d", fragment.c_str(), pos, line_char_count);
      ab.append(fragment, 0, line_char_count - pos);
      ab.append(lf_ret);    
      fragment = fragment.substr(line_char_count - pos);
      editorSetMessage("fragment = %s; pos = %d; lcc = %d", fragment.c_str(), pos, line_char_count);
      for (int line=1+start_line; line <= editorGetLinesInRowWW(fr); ++line) {
        int line_char_count = editorGetLineCharCountWW(fr, line);
        if (fragment.size() < line_char_count) {
          ab.append(fragment);
          break;
        } else {
          ab.append(fragment.substr(0, line_char_count));
          ab.append(lf_ret);    
          fragment = fragment.substr(line_char_count);
        }
      }
    }  
  }

  if (mode == VISUAL_BLOCK) {
    int x = editorGetScreenXFromRowColWW(vb0[1], vb0[0]) + left_margin + left_margin_offset + 1;
    int y = editorGetScreenYFromRowColWW(vb0[1], vb0[0]) + top_margin - line_offset; 
    ab.append("\x1b[48;5;244m");
    for (int n=0; n < (fr-vb0[1] + 1);++n) {
      ab.append(fmt::format("\x1b[{};{}H", y + n, x));
      if (rows.at(vb0[1] + n).empty() || rows.at(vb0[1] + n).size() < vb0[0] + 1) continue;
      ab.append(rows.at(vb0[1] + n).substr(vb0[0], fc - vb0[0] + 1));
    }
  }
  ab.append("\x1b[0m");
}

void Editor::editorYankLine(int n) {
  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(rows.at(fr+i));
  }
  string_buffer.clear(); //static
  editorSetMessage("line_buffer.size() = %d string_buffer.size() = %d", line_buffer.size(), string_buffer.size());
}

void Editor::editorYankString(void) {
  // doesn't cross rows right now
  if (rows.empty()) return;

  line_buffer.clear();
  std::string& row = rows.at(fr);
  string_buffer.clear(); //static

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
  string_buffer = std::string(first, last); //static
}


void Editor::editorPasteString(void) {
  if (rows.empty() || string_buffer.empty()) return; //static
  std::string& row = rows.at(fr);

  row.insert(row.begin() + fc, string_buffer.begin(), string_buffer.end()); //static
  fc += string_buffer.size(); //static
  dirty++;
}

void Editor::editorPasteStringVisual(void) {
  if (rows.empty() || string_buffer.empty()) return; //static
  std::string& row = rows.at(fr);

  int h_light[2] = {0,0};
  if (highlight[1] < highlight[0]) { //note highlight[1] should == fc
    h_light[1] = highlight[0];
    h_light[0] = highlight[1];
  } else {
    h_light[0] = highlight[0];
    h_light[1] = highlight[1];
  }

  row.replace(row.begin() + h_light[0], row.begin() + h_light[1] + 1, string_buffer); //static
  fc = h_light[0] + string_buffer.size(); //static
  dirty++;
}

void Editor::paste_line(void){
  if (rows.empty()) {
    rows = line_buffer;
    fr = fc = 0;
  } else {
    // what if fr is the last row - it's ok - can insert beyond last position
    rows.insert(rows.begin()+fr+1, line_buffer.begin(), line_buffer.end());
    fr++; //you just pasted rows so should be able to increment fr
    fc = 0;
  }
  //editorSetMessage("got here"); 
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

  /*
  auto dict_finder = nuspell::Finder::search_all_dirs_for_dicts();
  auto path = dict_finder.get_dictionary_path("en_US");
  auto dict = nuspell::Dictionary::load_from_path(path);
  */

  auto sugs = std::vector<std::string>();
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
  row.resize(fc); // or row.chars.erase(row.chars.begin() + fc, row.chars.end())
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
  // could just be redraw = true or do nothing since don't want to override if already true.
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
  Diff &d = undo_deque.at(0);
  if (cmd_map1.count(last_command)) {
    (this->*cmd_map1.at(last_command))(last_repeat);

    for (int n=0; n<last_repeat; n++) {
      for (char const &c : d.inserted_text) {
        if (c == '\r') editorInsertReturn();
        else editorInsertChar(c);
      }
    }
    return;
  }

  //'o' 'O':
  if (cmd_map2.count(last_command)) {
    (this->*cmd_map2.at(last_command))(last_repeat);
    return;
  }

  //'x' 'dw': case C_daw: case C_dd: case C_de: case C_dG: case C_d$:
  if (cmd_map3.count(last_command)) {
    (this->*cmd_map3.at(last_command))(last_repeat);
    return;
  }

  //case C_cw: case C_caw: case 's':
  if (cmd_map4.count(last_command)) {
      (this->*cmd_map4.at(last_command))(last_repeat);

    Diff &d = undo_deque.at(0);
    for (char const &c : d.inserted_text) {
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

  if (last_command =="p") {
    E_paste(last_repeat);
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

  if (row.size() <= screencols - left_margin_offset ) return c; //seems obvious but not added until 03022019

  int pos = -1;
  int prev_pos;
  for (;;) {

  if (row.substr(pos+1).size() <= screencols - left_margin_offset) {
    prev_pos = pos;
    break;
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+screencols - left_margin_offset);

  if (pos == std::string::npos) {
      pos = prev_pos + screencols - left_margin_offset;
  } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + screencols - left_margin_offset;
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

  if (row.size() <= screencols - left_margin_offset ) return 1; //seems obvious but not added until 03022019

  /* pos is the position of the last char in the line
   * and pos+1 is the position of first character of the next row
   */

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= screencols - left_margin_offset) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+screencols - left_margin_offset);

    if (pos == std::string::npos) {
        pos = prev_pos + screencols - left_margin_offset;

   // only replace if you have enough characters without a space to trigger this
   // need to start at the beginning each time you hit this
   // unless you want to save the position which doesn't seem worth it
    } else if (pos == prev_pos) {
      replace(row.begin(), row.begin()+pos+1, ' ', '+');
      pos = prev_pos + screencols - left_margin_offset;
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

  if (row.size() <= screencols - left_margin_offset) return 1; //seems obvious but not added until 03022019

  int lines = 0;
  int pos = -1; //pos is the position of the last character in the line (zero-based)
  int prev_pos;
  for (;;) {

    // we know the first time around this can't be true
    // could add if (line > 1 && row.substr(pos+1).size() ...);
    if (row.substr(pos+1).size() <= screencols - left_margin_offset) {
      lines++;
      break;
    }

    prev_pos = pos;
    pos = row.find_last_of(' ', pos+screencols - left_margin_offset);

    //note npos when signed = -1
    //order can be reversed of if, else if and can drop prev_pos != -1: see editorDrawRows
    if (pos == std::string::npos) {
      pos = prev_pos + screencols - left_margin_offset;
    } else if (pos == prev_pos) {
      row = row.substr(pos+1);
      //prev_pos = -1; 12-27-2019
      pos = screencols - left_margin_offset - 1;
    }
    lines++;
  }
  return lines;
}

/* below exists to create a text file that has the proper
 * line breaks based on screen width for syntax highlighters
 * to operate on 
 * Produces a text string that starts at the first line of the
 * file and ends on the last visible line
 * Only used by editorDrawCodeRows
 */
std::string Editor::editorGenerateWWString(void) {
  if (rows.empty()) return "";

  std::string ab = "";
  int y = -line_offset;
  int filerow = 0;

  for (;;) {
    if (filerow == rows.size()) {last_visible_row = filerow - 1; return ab;}

    //char ret = '\n';
    char ret = '\t';
    std::string_view row = rows.at(filerow);
    // if you put a \n in the middle of a comment the wrapped portion won't be italic
    //if (row.find("//") != std::string::npos) ret = '\t';
    //ret = '\t';

    if (row.empty()) {
      if (y == screenlines - 1) return ab;
      ab.append("\n");
      filerow++;
      y++;
      continue;
    }

    size_t pos;
    size_t prev_pos = 0; //this should really be called prev_pos_plus_one
    for (;;) {
      // if remainder of line is less than screen width
      if (prev_pos + screencols - left_margin_offset > row.size() - 1) {
        ab.append(row.substr(prev_pos));
        if (y == screenlines - 1) {last_visible_row = filerow - 1; return ab;}
        ab.append(1, '\n');
        y++;
        filerow++;
        break;
      }

      pos = row.find_last_of(' ', prev_pos + screencols - left_margin_offset - 1);
      if (pos == std::string::npos || pos == prev_pos - 1) {
        pos = prev_pos + screencols - left_margin_offset - 1;
      }
      ab.append(row.substr(prev_pos, pos - prev_pos + 1));
      if (y == screenlines - 1) {last_visible_row = filerow - 1; return ab;}
      ab.append(1, ret); //this appears to be the only place intraline take place so make them all \t
      y++;
      prev_pos = pos + 1;
    }
  }
}

//used in status bar because interesting but not essential
int Editor::editorGetLineCharCountWW(int r, int line) {
  //This should be a string view and use substring like lines in row
  std::string row = rows.at(r);
  if (row.empty()) return 0;

  if (row.size() <= screencols - left_margin_offset) return row.size();

  int lines = 0; //1
  int pos = -1;
  int prev_pos;
  for (;;) {

  // we know the first time around this can't be true
  // could add if (line > 1 && row.substr(pos+1).size() ...);
  if (row.substr(pos+1).size() <= screencols - left_margin_offset) {
    return row.substr(pos+1).size();
  }

  prev_pos = pos;
  pos = row.find_last_of(' ', pos+screencols - left_margin_offset);

  if (pos == std::string::npos) {
    pos = prev_pos + screencols - left_margin_offset;

  // only replace if you have enough characters without a space to trigger this
  // need to start at the beginning each time you hit this
  // unless you want to save the position which doesn't seem worth it
  } else if (pos == prev_pos) {
    replace(row.begin(), row.begin()+pos+1, ' ', '+');
    pos = prev_pos + screencols - left_margin_offset;
  }

  lines++;
  if (lines == line) break;
  }
  return pos - prev_pos;
}

/************************************* end of WW ************************************************/
/* EDITOR COMMAND_LINE mode functions */
void Editor::E_write_C(void) {
  if (is_subeditor) {
      editorSetMessage("You can't save the contents of the Output Window");
      return;
  }

  //update_note(is_subeditor);
  update_note(false);
  //problem - can't have dirty apply to both note and subnote
  dirty = 0; //is in update_note but dirty = 0 is probably better here.
  editorSetMessage("");

  if (is_subeditor) return;

  int tid = get_folder_tid(id);
  if (lm_browser && !(tid == 18 || tid == 14)) update_html_file("assets/" + CURRENT_NOTE_FILE);
}

/* the following are not being called and were written for single editor
 * but very possible they should be reworked and called
void Editor::E_write_close_C(void) {
  update_note();
  mode = NORMAL;
  command[0] = '\0';
  command_line.clear();
  editor_mode = false; //global
  if (lm_browser) { //lm_browser is global
    if (get_folder_tid(id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
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
*/

void Editor::E_open_in_vim_C(void) {
  open_in_vim(); //send you into editor mode
  mode = NORMAL;
}

#ifdef NUSPELL
void Editor::E_spellcheck_C(void) {
  spellcheck = !spellcheck;
  if (spellcheck) editorSpellCheck();
  editorSetMessage("Spellcheck %s", (spellcheck) ? "on" : "off");
}
#else
void E_spellcheck_C(void) {
  editorSetMessage("Nuspell is not available in this build");
}
#endif

void Editor::E_readfile_C(void) {
  std::string filename;
  std::size_t pos = command_line.find(' ');
  if (pos) filename = command_line.substr(pos+1);
  else filename = "example.cpp";
  editorReadFileIntoNote(filename);
  editorSetMessage("Note generated from file: %s", filename.c_str());
  //mode = NORMAL;
}
// EDITOR NORMAL mode functions
void Editor::E_cw(int repeat) {
  for (int j = 0; j < repeat; j++) {
    int start = fc;
    editorMoveEndWord();
    int end = fc;
    fc = start;
    //for (int j = 0; j < end - start + 1; j++) editorDelChar();
    std::string &row = rows.at(fr);
    //undo_deque[0].deleted = row.substr(fc, end - start + 1);
    row.erase(fc, end - start + 1);
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
  //for (int i = 0; i < repeat; i++) editorDelChar();

  //if (rows.empty()) return; // creation of NO_ROWS may make this unnecessary
  std::string& row = rows.at(fr);
  //if (row.empty() || fc > static_cast<int>(row.size()) - 1) return;
  if (row.empty()) return; //act like it was an insert
  //undo_deque[0].deleted = row.substr(fc, repeat);
  row.erase(fc, repeat);
  dirty++;
}

void Editor::E_x(int repeat) {
  //for (int i = 0; i < repeat; i++) editorDelChar();
  std::string& row = rows.at(fr);
  if (row.empty()) return; 
  //undo_deque[0].deleted = row.substr(fc, repeat);
  row.erase(fc, repeat);
  dirty++;
}

void Editor::E_dd(int repeat) {
  int r = rows.size() - fr;
  repeat = (r >= repeat) ? repeat : r ;
  editorYankLine(repeat);
  for (int i=0; i<repeat ; i++) editorDelRow(fr);
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
  for (int i = 0; i < repeat; i++) editorChangeCase();
}

void Editor::E_goto_outline(int repeat) {
  sess.editor_mode = false;
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

// see E_o_escape
void Editor::E_o(int repeat) {
  last_typed.clear();
  editorInsertNewline(1);
}

// see E_O_escape
void Editor::E_O(int repeat) { 
  last_typed.clear();
  editorInsertNewline(0);
}

//used in INSERT mode after escape is typed and to deal with dot/E.repeat > 1
//note that when used with a repeat E_o_escape and E_O_escape
//are equivalent (doesn't matter what direction the newline goes in
//but not the same if these functions are used to dot.
//since initial direction of newline matters then.
void Editor::E_o_escape(int repeat) { 
  for (int n=0; n<repeat; n++) {
    editorInsertNewline(1);
    for (char const &c : last_typed) { 
      if (c == '\r') editorInsertReturn();
      else editorInsertChar(c);
    }
  }
}

//used in INSERT mode after escape is typed and to deal with dot/E.repeat > 1
void Editor::E_O_escape(int repeat) {
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

void Editor::E_resize(int flag) {
  if (!linked_editor) return;

  Editor *& z = linked_editor;
  int subnote_height;
  if (flag) {
    subnote_height = (abs(screenlines - z->screenlines) < 5) 
      ? LINKED_NOTE_HEIGHT
      : sess.textlines/2;
  } else {
    char c = command[1];
    subnote_height = (c == '=') ? sess.textlines/2 : LINKED_NOTE_HEIGHT;
  }
  if (!is_subeditor) {
    screenlines = sess.textlines - subnote_height - 1;
    z->screenlines = subnote_height;
    z->top_margin = sess.textlines - subnote_height + 2;
  } else {
    z->screenlines = sess.textlines - subnote_height - 1;
    screenlines = subnote_height;
    top_margin = sess.textlines - subnote_height + 2;
  }
  z->editorRefreshScreen(true);
  editorRefreshScreen(true);
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
  if (!string_buffer.empty()) editorPasteString(); //static
  else paste_line();
}

//case '*':  
void Editor::E_find(int repeat) {
  // does not clear dot
  std::string& row = rows.at(fr);
  size_t pos;
  //vim finds the nearest word if sitting on a space or other non-alphanumeric chars 
  std::string delimiters = " *%!^<>,.;?:()[]{}&#~'\"";
  if (delimiters.find(row.at(fc)) != std::string::npos) {
  //if (row.at(fc) == ' ') {
    //pos = row.find_first_not_of(' ', fc);
    pos = row.find_first_not_of(delimiters, fc);
    if (pos != std::string::npos) fc = pos;
  }
   
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

/* not in use - see NORMAL mode switch
// 'u'
void Editor::E_undo(int repeat) {
  //editorRestoreSnapshot();
  undo();
}

// 'ctrl-r':
void Editor::E_redo(int repeat) {
  redo();
}
*/

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

void Editor::E_save_note(void) {
  std::string filename;
  if (mode == NORMAL) filename = "lm_saved_note.txt";
  else {
    std::size_t pos = command_line.find(' ');
    if (pos) filename = command_line.substr(pos+1);
    else filename = "lm_saved_note.txt";
  }
  editorSaveNoteToFile(filename);
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

// not in use at the moment
void ReplaceStringInPlace(std::string& subject, const std::string& search,
                          const std::string& replace) {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos) {
         subject.replace(pos, search.length(), replace);
         pos += replace.length();
    }
}

using json = nlohmann::json;

//for godbolt - worked - currently not in use
void Editor::E_run_code_C(void) {

  if(is_subeditor) {
    editorSetMessage("You're in the subeditor!");
    return;
  }

  std::string source = editorRowsToString();

//this works with coliru
#if 0  
  json js = {
      //{"cmd", "g++-4.8 main.cpp && ./a.out"},
      {"cmd", "g++ -std=c++20 -O2 -Wall -pedantic -pthread main.cpp && ./a.out"},
      //{"src", "#include <iostream>\nint main(){ std::cout << \"Hello World\" << std::endl; return 0;}"}
      {"src", source}
      };

  auto url = cpr::Url{"http://coliru.stacked-crooked.com/compile"};
  cpr::Response r = cpr::Post(url, cpr::Body{js.dump()},
            cpr::Header{{"Content-Type", "application/json"}}, 
            cpr::Header{{"accept", "application/json"}}
            );
#endif

  //this works with compiler explorer
  json js = {
    {"source", source},
    //{"compiler", "g102"},
    {"options", {
          //{"userArguments", "-O3"},
          {"userArguments", "-std=c++2a"},
          {"compilerOptions", {
                    {"executorRequest", true}
          }},
          {"filters", {
                    {"execute", true},
                    {"directives", true}, //one of both of these needed
                    {"labels", true} // to get stdout

          }},
          {"tools", json::array()},
          {"libraries", json::array({ 
              {{"id", "openssl"}, {"version", "111c"}},
              {{"id", "fmt"},{"version", "trunk"}},
              {{"id", "nlohmann_json"},{"version", "360"}}
          })},
      }},
    {"lang", "c++"},
    {"allowStoreCodeDebug", true}
  };

  cpr::Response r = cpr::Post(cpr::Url{"https://godbolt.org/api/compiler/g102/compile"},
            cpr::Body{js.dump()}, 
            // both headers in Header or doesn't work
            cpr::Header{{"Content-Type", "application/json"}, {"Accept", "application/json"}}, // both headers in Header or doesn't work
            cpr::VerifySsl(0) // cpr issue #445 regarding ssl certificates
            );

  editorSetMessage("status code: %d", r.status_code);

  auto & s_rows = linked_editor->rows; //s_rows -> subnote_rows
  s_rows.clear();
  s_rows.push_back("");
  s_rows.push_back("----------------");;
  s_rows.push_back(r.url); //s
  s_rows.push_back("----------------");;

  std::string str = r.text;
  //ReplaceStringInPlace(str, "\\u001b", "\x1b");
  json js1 = json::parse(str);

  if (js1["buildResult"]["code"] == 0) {
    auto arr = js1["stdout"]; 
  for (const auto i : arr) {
    s_rows.push_back(i["text"]);
  }
  }
  else {
    // something happens when you extract text as below and the
    // escapes seem to be "exposed" somehow
    auto arr = js1["buildResult"]["stderr"];
    for (auto a : arr) {
      s_rows.push_back(a["text"]);
    }
  }

  s_rows.push_back("----------------");;
  linked_editor->fr = 0;
  linked_editor->fc = 0;
  linked_editor->editorRefreshScreen(true);
  linked_editor->dirty++;
}

void Editor::E_compile_C(void) {

  if(is_subeditor) {
    editorSetMessage("You're in the Output Window!");
    return;
  }
  std::stringstream text;
  std::string line;
  if (get_folder_tid(id) == 18) {
    chdir("/home/slzatz/clangd_examples/");
    ipstream make("make");

    int i = 0;
    while(getline(make, line)) {
      text << line << '\n';
      ++i;
      if (i > 10){
        text << "TRUNCATED at 10 lines";
        break;
      }
    }
  } else { 
    chdir("/home/slzatz/go/src/example/");
    const pstreams::pmode mode = pstreams::pstdout|pstreams::pstderr;
    ipstream go("go build main.go", mode);
    int i = 0;
    while(getline(go.err(), line)) {
      text << line << '\n';
      ++i;
      if (i > 10){
        text << "TRUNCATED at 10 lines";
        break;
      }
    }
  }
  if (text.str().empty())   text << "Go build successful";
  std::vector<std::string> zz = str2vecWW(text.str(), false); //ascii_only = false

  auto & s_rows = linked_editor->rows; //s_rows -> subnote_rows output_window
  s_rows.clear();
  s_rows.push_back("----------------");;
  s_rows.insert(s_rows.end(), zz.begin(), zz.end());
  s_rows.push_back("----------------");;

  linked_editor->fr = 0;
  linked_editor->fc = 0;
  linked_editor->editorRefreshScreen(true);
  linked_editor->dirty++;
  chdir("/home/slzatz/listmanager_cpp/");
}

void Editor::E_runlocal_C(void) {

  if(is_subeditor) {
    editorSetMessage("You're in the subeditor!");
    return;
  }
  std::string args, cmd;
  std::size_t pos = command_line.find(' ');
  if (pos != std::string::npos) args = command_line.substr(pos); //include the space
  else args = "";

  if (get_folder_tid(id) == 18) {
      cmd = "/home/slzatz/clangd_examples/test_cpp";
  } else {
      cmd = "/home/slzatz/go/src/example/main";
  }

  const pstreams::pmode mode = pstreams::pstdout|pstreams::pstderr; /////
  ipstream run(cmd + args, mode);
  char buf[8192];
  std::streamsize n;
  bool finished[2] = {false, false};
  std::string s{}; //for stderr
  std::string t{}; //for stdout
  while (!finished[0] || !finished[1]) {
    if (!finished[0]) {
      while ((n = run.err().readsome(buf, sizeof(buf))) > 0) {
        s += std::string{buf, static_cast<size_t>(n)};
      }
      if (run.eof()) {
        finished[0] = true;
        if (!finished[1]) run.clear();
      }
    }

    if (!finished[1]) {
      while ((n = run.out().readsome(buf, sizeof(buf))) > 0) {
          t += std::string{buf, static_cast<size_t>(n)};
      }
      if (run.eof()) {
        finished[1] = true;
        if (!finished[0]) run.clear();
      }
    }
  }

  if (!s.empty()) s = "Error: " + s;
  //std::vector<std::string> zz = str2vecWW(s+t);
  std::vector<std::string> zz = str2vecWW(s+t, false); //ascii_only = false

  auto & s_rows = linked_editor->rows; //s_rows -> subnote_rows
  s_rows.clear();
  s_rows.push_back("----------------");
  s_rows.insert(s_rows.end(), zz.begin(), zz.end());
  s_rows.push_back("----------------");
  linked_editor->fr = 0;
  linked_editor->fc = 0;
  linked_editor->editorRefreshScreen(true);
  linked_editor->dirty++;
}

void Editor::decorate_errors(const json& diagnostics) {

  std::string msg;
  if (!diagnostics.empty()) {
    //std::string s = diagnostics.dump(); //used for debugging

    int start_line = diagnostics[0]["range"]["start"]["line"];
    int start_char = diagnostics[0]["range"]["start"]["character"];
    //int end_line = diagnostics[0]["range"]["end"]["line"]; //currently not used
    int end_char = diagnostics[0]["range"]["end"]["character"];
    msg = diagnostics[0]["message"];
    //editorSetMessage(s.c_str());
    std::string &row = rows.at(start_line);
    std::string fragment;
    int end_of_line = 0;
    if (start_char >= row.size()) {
      start_char = row.size()-1;
      fragment = " ";
      end_of_line = 1;
    } else {
      fragment = row.substr(start_char, end_char - start_char);
    }
    int y = editorGetScreenYFromRowColWW(start_line, start_char) - line_offset; // added line offset 12-25-2019
    // probably should scroll and not return
    // would look like fr = start_line;fc = start_char;editorRefreshScreen;decorate_errors
    if ((y > screenlines-1) || y < 0) { //return;
      fr = start_line;
      fc = start_char;
      editorScroll();
      editorRefreshScreen(true);
      decorate_errors(diagnostics);
      return;
  }

    int x = editorGetScreenXFromRowColWW(start_line, start_char) + left_margin + left_margin_offset + 1 + end_of_line;

    std::stringstream ss;
    ss << "\x1b[" << y + top_margin << ";" << x << "H" << "\x1b[48;5;244m" << fragment << "\x1b[0m";
    write(STDOUT_FILENO, ss.str().c_str(), ss.str().size());
  } else {
    msg = "There were no errors";
  }

  //editorSetMessage("start line: %d, start char: %d, end line: %d, end char: %d", start_line, start_char, end_line, end_char);
  editorSetMessage(msg.c_str());
  editorRefreshScreen(false); //in Normal mode
  // for some reason have to hide the cursor before showing or it doesn't show
  write(STDOUT_FILENO, "\x1b[?25l", 6); //hide the cursor
  write(STDOUT_FILENO, "\x1b[?25h", 6); //show the cursor
}

void Editor::E_CTRL_P(int) {

  std::string s = editorPasteFromClipboard();
  if (s.empty()) return; 
  //std::erase(s, '\r'); //c++20

  if (rows.empty()) editorInsertRow(0, std::string());

  // text cut from vim ends in '\n'
  std::vector<std::string> v = str2vecWW(s); 

  /* Decided to start on new line and not continue current line
  // strategy is to insert first pasted row at cursor and then
  // create new rows for subsequent lines
  std::string& row = rows.at(fr);
  row.insert(row.begin() + fc, v.at(0).begin(), v.at(0).end()); 
  // Note below it is OK to insert into a vector 1 beyond last position
  if (v.size() > 1) rows.insert(rows.begin() + fr + 1, v.begin() + 1, v.end());
  */
  rows.insert(rows.begin()+fr, v.begin(), v.end());
  dirty++;
}

std::string Editor::editorPasteFromClipboard(void) {
  char buf[1024]{};
  int fd = open("/dev/tty", O_RDWR);
  write(fd, "\x1b]52;c;?\x07", 9);
  //ssize_t size = read(fd, &buf, sizeof buf);
  read(fd, &buf, sizeof buf);
  close(fd);
  std::string s{buf};
  std::string s1 = s.substr(7);
  size_t pos = s1.find_last_of('=');
  if (pos != std::string::npos) {
    s1.erase(pos + 1);
  } else {
    for (;;) {
      if (s1.size() % 4 == 0) break;
      s1.erase(s1.size() - 1);
      if (s1.empty()) break;
    }
  }
  auto decoded = base64::decode(s1);
  std::string s2(std::begin(decoded), std::end(decoded));

  /*
  pos = s2.find('\t');
  while(pos != std::string::npos) {
    s2.replace(pos, 1, "  "); // number is number of chars to replace
    pos = s2.find('\t');
  }

  for (int i=0; i<s2.size(); ++i) if (!isascii(s2[i])) s2[i] = '?';
  */

  return s2;
}

void Editor::convert2base64(std::vector<std::string> &cb) {
  std::string s = std::accumulate(cb.begin(), cb.end(), std::string{}); 
  std::vector<unsigned char> data(std::begin(s), std::end(s));
  auto encoded = base64::encode(data);
  int fd = open("/dev/tty", O_RDWR);
  write(fd, "\x1b]52;c;!\x07", 9); //clear buffer if it's concatenating
  std::string s1 = fmt::format("\x1b]52;c;{}\x07", encoded);
  write(fd, s1.c_str(), s1.size());
  close(fd);
}

void Editor::copyStringToClipboard(void) {
  // doesn't cross rows right now
  std::string& row = rows.at(fr);

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
  std::string s{std::string(first, last)};
  std::vector<unsigned char> data(std::begin(s), std::end(s));
  auto encoded = base64::encode(data);
  int fd = open("/dev/tty", O_RDWR);
  write(fd, "\x1b]52;c;!\x07", 9); //clear buffer if it's concatenating; /n does not address vim problem
  std::string s1 = fmt::format("\x1b]52;c;{}\x07", encoded); // /n doesn't address vim problem
  write(fd, s1.c_str(), s1.size());
  close(fd);
}

void Editor::E_move_output_window_right(int) {
  if (!linked_editor) return;
  if (!is_subeditor) return;
  if (!is_below) return;

  //top_margin = TOP_MARGIN + 1;
  //screenlines = total_screenlines - 1;
  is_below = false;

  int editor_slots = 0;
  for (auto z : sess.editors) {
    if (!z->is_below) editor_slots++;
  }

  int s_cols = -1 + (sess.screencols - sess.divider)/editor_slots;
  int i = -1; //i = number of columns of editors -1
  for (auto z : sess.editors) {
    if (!z->is_below) i++;
    z->left_margin = sess.divider + i*s_cols + i;
    z->screencols = s_cols;
    z->setLinesMargins();
  }

  sess.eraseRightScreen(); //moved down here on 10-24-2020
  sess.drawEditors();
  editorSetMessage("top_margin = %d", top_margin);

}

void Editor::E_move_output_window_below(int) {
  if (!linked_editor) return;
  if (!is_subeditor) return;
  if (is_below) return;

  //top_margin = TOP_MARGIN + 1;
  //screenlines = total_screenlines - 1;
  is_below = true;

  int editor_slots = 0;
  for (auto z : sess.editors) {
    if (!z->is_below) editor_slots++;
  }

  int s_cols = -1 + (sess.screencols - sess.divider)/editor_slots;
  int i = -1; //i = number of columns of editors -1
  for (auto z : sess.editors) {
    if (!z->is_below) i++;
    z->left_margin = sess.divider + i*s_cols + i;
    z->screencols = s_cols;
    z->setLinesMargins();
  }

  sess.eraseRightScreen(); //moved down here on 10-24-2020
  sess.drawEditors();
  editorSetMessage("top_margin = %d", top_margin);
}

void Editor::createLink(void) {

  std::unordered_set<int> temp;
  for (const auto z : sess.editors) {
    temp.insert(z->id);
  }
  if (!temp.contains(id)) {
    editorSetMessage("Error: current note's entry should be in the set!");
    mode = NORMAL;
    return;
    }

  if (temp.size() != 2) {
    editorSetMessage("At the moment you can only link two entries at a time");
    mode = NORMAL;
    return;
  }
  std::array<int, 2> task_ids{};
  int i = 0;
  for (const auto z : temp) {
    task_ids[i] = z;
    i++;
  }

  if (task_ids[0] > task_ids[1]) {
    int t = task_ids[0];
    task_ids[0] = task_ids[1];
    task_ids[1] = t;
  }

  Query q(sess.db, "INSERT OR IGNORE INTO link (task_id0, task_id1) VALUES ({}, {});",
              task_ids[0], task_ids[1]);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    editorSetMessage("Problem in 'Editor::createLink': %s", error.c_str());
    mode = NORMAL;
    return;
  }

   Query q1(sess.db,"UPDATE link SET modified = datetime('now') WHERE task_id0={} AND task_id1={};", task_ids[0], task_ids[1]);
   q1.step();

   mode = NORMAL;
}

void Editor::getLinked(void) {

  Query q(sess.db, "SELECT task_id0, task_id1 FROM link WHERE task_id0={} OR task_id1={}", id, id);
  if (int res = q.step(); res != SQLITE_ROW) {
    editorSetMessage("Problem retrieving linked item: %d", res);
    return;
  }
  int task_id0 = q.column_int(0);
  int task_id1 = q.column_int(1);

  int linked_id = (task_id0 == id) ? task_id1 : task_id0;

  auto it = std::find_if(std::begin(sess.editors), std::end(sess.editors),
                       [&linked_id](auto& ls) { return ls->id == linked_id; }); //auto&& also works

  if (it != sess.editors.end()) {
    editorSetMessage("Linked note (%d) is already open", linked_id);
    mode = NORMAL;
    return;
  }

    Editor *p = new Editor;
    sess.editors.push_back(p);
    p->id = linked_id;
    p->top_margin = TOP_MARGIN + 1;
    //p->screenlines = Editor::total_screenlines - LINKED_NOTE_HEIGHT - 1;

    int folder_tid = get_folder_tid(linked_id);
    if (folder_tid == 18 || folder_tid == 14) {
      p->linked_editor = new Editor;
      sess.editors.push_back(p->linked_editor);
      p->linked_editor->id = linked_id;
      //p->linked_editor->top_margin = Editor::total_screenlines - LINKED_NOTE_HEIGHT + 2;
      //p->linked_editor->screenlines = LINKED_NOTE_HEIGHT;
      p->linked_editor->is_subeditor = true;
      p->linked_editor->is_below = true;
      p->linked_editor->linked_editor = p;
      p->left_margin_offset = LEFT_MARGIN_OFFSET;
    }

    sess.db.query("SELECT note FROM task WHERE id = {}", linked_id);
    sess.db.callback = editor_note_callback;
    sess.db.pArg = p;
    if (!sess.db.run()) {
    editorSetMessage("SQL error: %s", sess.db.errmsg);
    return;
  }  

  if (p->linked_editor) { 
    p->linked_editor->rows = std::vector<std::string>{" "};
  }

  sess.positionEditors();
  sess.eraseRightScreen(); //erases editor area + statusbar + msg
  sess.drawEditors();
  mode = NORMAL;
}

int editor_note_callback (void *e, int argc, char **argv, char **azColName) {

  //UNUSED(NotUsed);
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  Editor *editor = static_cast<Editor *>(e);

  if (!argv[0]) return 0; ////////////////////////////////////////////////////////////////////////////
  std::string note(argv[0]);
  //note.erase(std::remove(note.begin(), note.end(), '\r'), note.end());
  std::erase(note, '\r'); //c++20
  std::stringstream snote;
  snote << note;
  std::string s;
  while (getline(snote, s, '\n')) {
    editor->editorInsertRow(editor->rows.size(), s);
  }

  editor->dirty = 0; //assume editorInsertRow increments dirty so this needed
  return 0;
}

// should change name to avoid confustion with sess.moveDivider
void Editor::moveDivider(void) {
  int pos = command_line.find(' ');
  if (pos == std::string::npos) {
      editorSetMessage("You need to provide a number between 10 and 90");
      mode = NORMAL;
      return;
  }
  int p = command_line.find_first_of("0123456789");
  if (p != pos + 1) {
    editorSetMessage("You need to provide a number between 10 and 90");
    mode = NORMAL;
    return;
  }
  int pct = stoi(command_line.substr(p));
  if (pct > 90 || pct < 10) { 
    editorSetMessage("You need to provide a number between 10 and 90");
    mode = NORMAL;
    return;
  }
  sess.moveDivider(pct);
  mode = NORMAL;
}
