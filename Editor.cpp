#include "listmanager.h"
#include "Editor.h"

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
