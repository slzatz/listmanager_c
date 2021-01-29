#include "normalorg.h"
#include "organizer.h"
#include "session.h"

#define MAX 500

void updateTitle(void);
int getFolderTid(int id);
void updateContainerTitle(void);
void updateKeywordTitle(void);
void getItems(int);
void toggleDeleted(void);
void toggleStar(void);
void toggleCompleted(void);
int getId(void);
Entry getEntryInfo(int id);

/*
namespace normal {
}
*/

//case 'i':
void insert_N(void){
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 's':
void s_N(void){
  orow& row = org.rows.at(org.fr);
  row.title.erase(org.fc, org.repeat);
  row.dirty = true;
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
}          

void return_N(void) {
  orow& row = org.rows.at(org.fr);
  std::string msg;

  if(row.dirty){
    if (org.view == TASK) {
      updateTitle();
      msg = "Updated id {} to {} (+fts)";

      if (sess.lm_browser) {
        int folder_tid = getFolderTid(org.rows.at(org.fr).id);
        if (!(folder_tid == 18 || folder_tid == 14)) sess.updateHTMLFile("assets/" + CURRENT_NOTE_FILE);
      }
    } else if (org.view == CONTEXT || org.view == FOLDER) {
      updateContainerTitle();
      msg = "Updated id {} to {}";
    } else if (org.view == KEYWORD) {
      updateKeywordTitle();
      msg = "Updated id {} to {}";
    }  
    org.command[0] = '\0'; //11-26-2019
    org.mode = NORMAL;
    if (org.fc > 0) org.fc--;
    row.dirty = false; 
    sess.showOrgMessage3(msg, row.id, row.title);
    return;
  }

  // return means retrieve items by context or folder
  // do this in database mode
  if (org.view == CONTEXT) {
    org.context = row.title;
    org.folder = "";
    org.taskview = BY_CONTEXT;
    sess.showOrgMessage("\'%s\' will be opened", org.context.c_str());
    org.command_line = "o " + org.context;
  } else if (org.view == FOLDER) {
    org.folder = row.title;
    org.context = "";
    org.taskview = BY_FOLDER;
    sess.showOrgMessage("\'%s\' will be opened", org.folder.c_str());
    org.command_line = "o " + org.folder;
  } else if (org.view == KEYWORD) {
    org.keyword = row.title;
    org.folder = "";
    org.context = "";
    org.taskview = BY_KEYWORD;
    sess.showOrgMessage("\'%s\' will be opened", org.keyword.c_str());
    org.command_line = "ok " + org.keyword;
  }

  sess.command_history.push_back(org.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, org.command_line);
  org.marked_entries.clear();

  getItems(MAX);
}

//case 'x':
void x_N(void){
  orow& row = org.rows.at(org.fr);
  row.title.erase(org.fc, org.repeat);
  row.dirty = true;
}        

void daw_N(void) {
  for (int i = 0; i < org.repeat; i++) org.outlineDelWord();
}

void caw_N(void) {
  for (int i = 0; i < org.repeat; i++) org.outlineDelWord();
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void dw_N(void) {
  for (int j = 0; j < org.repeat; j++) {
    int start = org.fc;
    org.outlineMoveEndWord2();
    int end = org.fc;
    org.fc = start;
    orow& row = org.rows.at(org.fr);
    row.title.erase(org.fc, end - start + 2);
  }
}

void cw_N(void) {
  for (int j = 0; j < org.repeat; j++) {
    int start = org.fc;
    org.outlineMoveEndWord2();
    int end = org.fc;
    org.fc = start;
    orow& row = org.rows.at(org.fr);
    row.title.erase(org.fc, end - start + 2);
  }
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void de_N(void) {
  int start = org.fc;
  org.outlineMoveEndWord(); //correct one to use to emulate vim
  int end = org.fc;
  org.fc = start; 
  for (int j = 0; j < end - start + 1; j++) org.outlineDelChar();
  org.fc = (start < org.rows.at(org.fr).title.size()) ? start : org.rows.at(org.fr).title.size() -1;
}

void d$_N(void) {
  org.outlineDeleteToEndOfLine();
}
//case 'r':
void r_N(void) {
  org.mode = REPLACE;
}

//case '~'
void tilde_N(void) {
  for (int i = 0; i < org.repeat; i++) org.outlineChangeCase();
}

//case 'a':
void a_N(void){
  org.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
  org.outlineMoveCursor(ARROW_RIGHT);
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'A':
void A_N(void) {
  org.outlineMoveCursorEOL();
  org.mode = INSERT; //needs to be here for movecursor to work at EOLs
  org.outlineMoveCursor(ARROW_RIGHT);
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'b':
void b_N(void) {
  org.outlineMoveBeginningWord();
}

//case 'e':
void e_N(void) {
  org.outlineMoveEndWord();
}

//case '0':
void zero_N(void) {
  if (!org.rows.empty()) org.fc = 0; // this was commented out - not sure why but might be interfering with O.repeat
}

//case '$':
void dollar_N(void) {
  org.outlineMoveCursorEOL();
}

//case 'I':
void I_N(void) {
  if (!org.rows.empty()) {
    org.fc = 0;
    org.mode = 1;
    sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
  }
}

void gg_N(void) {
  org.fc = org.rowoff = 0;
  org.fr = org.repeat-1; //this needs to take into account O.rowoff
  if (org.view == TASK) { 
    sess.drawPreviewWindow(org.rows.at(org.fr).id);
  } else {
    Container c = getContainerInfo(org.rows.at(org.fr).id);
    if (c.id != 0) 
      sess.displayContainerInfo(c);
  }
}

//case 'G':
void G_N(void) {
  org.fc = 0;
  org.fr = org.rows.size() - 1;
  if (org.view == TASK) {
    sess.drawPreviewWindow(org.rows.at(org.fr).id);
  } else {
    Container c = getContainerInfo(org.rows.at(org.fr).id);
    if (c.id != 0) 
      sess.displayContainerInfo(c);
  }
}

void gt_N(void) {
  std::map<std::string, int>::iterator it;

  if ((org.view == TASK && org.taskview == BY_FOLDER) || org.view == FOLDER) {
    if (!org.folder.empty()) {
      it = org.folder_map.find(org.folder);
      it++;
      if (it == org.folder_map.end()) it = org.folder_map.begin();
    } else {
      it = org.folder_map.begin();
    }
    org.folder = it->first;
    sess.showOrgMessage("\'%s\' will be opened", org.folder.c_str());
  } else {
    if (org.context.empty() || org.context == "search") {
      it = org.context_map.begin();
    } else {
      it = org.context_map.find(org.context);
      it++;
      if (it == org.context_map.end()) it = org.context_map.begin();
    }
    org.context = it->first;
    sess.showOrgMessage("\'%s\' will be opened", org.context.c_str());
  }
  getItems(MAX);
}

//case ':':
void colon_N(void) {
  sess.showOrgMessage(":");
  org.command_line.clear();
  org.last_mode = org.mode;
  org.mode = COMMAND_LINE;
}

//case 'v':
void v_N(void) {
  org.mode = VISUAL;
  org.highlight[0] = org.highlight[1] = org.fc;
  sess.showOrgMessage("\x1b[1m-- VISUAL --\x1b[0m");
}

//case 'p':  
void p_N(void) {
  if (!org.string_buffer.empty()) org.outlinePasteString();
}

//case '*':  
void asterisk_N(void) {
  org.outlineGetWordUnderCursor();
  org.outlineFindNextWord(); 
}

//case 'm':
void m_N(void) {
  org.rows.at(org.fr).mark = !org.rows.at(org.fr).mark;
  if (org.rows.at(org.fr).mark) {
    org.marked_entries.insert(org.rows.at(org.fr).id);
  } else {
    org.marked_entries.erase(org.rows.at(org.fr).id);
  }  
  sess.showOrgMessage("Toggle mark for item %d", org.rows.at(org.fr).id);
}

//case 'n':
void n_N(void) {
  org.outlineFindNextWord();
}

//case 'u':
void u_N(void) {
  //could be used to update solr - would use U
}

//dd and 0x4 -> ctrl-d
void dd_N(void) {
  toggleDeleted();
}

//0x2 -> ctrl-b
void star_N(void) {
  toggleStar();
}

//0x18 -> ctrl-x
void completed_N(void) {
  toggleCompleted();
}

//void outlineMoveNextWord() {
void w_N(void) {
  int j;
  orow& row = org.rows.at(org.fr);

  for (j = org.fc + 1; j < row.title.size(); j++) {
    if (row.title[j] < 48) break;
  }

  org.fc = j - 1;

  for (j = org.fc + 1; j < row.title.size() ; j++) { //+1
    if (row.title[j] > 48) break;
  }
  org.fc = j;

  org.command[0] = '\0';
  org.repeat = 0;
}

void info_N(void) {
  Entry e = getEntryInfo(getId());
  sess.displayEntryInfo(e);
  sess.drawPreviewBox();
}

void goto_editor_N(void) {
  if (sess.editors.empty()) {
    sess.showOrgMessage("There are no active editors");
    return;
  }

  sess.eraseRightScreen();
  sess.drawEditors();

  sess.editor_mode = true;
}
