//#include "listmanager.h"
#include "listman.h"
//#include "Organizer.h" //in listman.h
#include "common.h"
#include "session.h"
#include <string_view>
#include <algorithm>
#include <ranges>
#include "cmdorg.h" //cmd_lookup
#include "normalorg.h" //n_lookup
#include "editorfuncmap.h" //e_lookup and E_lookup_C
#include <filesystem>

#define TOP_MARGIN 1
#define MAX 500 // max rows to bring back
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this

void signalHandler(int signum);

std::unordered_set<int> navigation = {
         ARROW_UP,
         ARROW_DOWN,
         ARROW_LEFT,
         ARROW_RIGHT,
         'h',
         'j',
         'k',
         'l'
};

autocomplete ac;

int readKey() {
  int nread;
  char c;

  /* read is from <unistd.h> - not sure why read is used and not getchar <stdio.h>
   prototype is: ssize_t read(int fd, void *buf, size_t count); 
   On success, the number of bytes read is returned (zero indicates end of file)
   So the while loop below just keeps cycling until a byte is read
   it does check to see if there was an error (nread == -1)*/

   /*Note that ctrl-key maps to ctrl-a => 1, ctrl-b => 2 etc.*/

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) sess.die("read");
  }

  /* if the character read was an escape, need to figure out if it was
     a recognized escape sequence or an isolated escape to switch from
     insert mode to normal mode or to reset in normal mode
  */

  if (c == '\x1b') {
    char seq[3];
    //sess.showOrgMessage("You pressed %d", c); //slz
    // the reads time out after 0.1 seconds
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    // Assumption is that seq[0] == '[' 
    if (seq[1] >= '0' && seq[1] <= '9') {
      if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; //need 4 bytes
      if (seq[2] == '~') {
        //sess.showOrgMessage("You pressed %c%c%c", seq[0], seq[1], seq[2]); //slz
        switch (seq[1]) {
          case '1': return HOME_KEY; //not being issued
          case '3': return DEL_KEY; //<esc>[3~
          case '4': return END_KEY;  //not being issued
          case '5': return PAGE_UP; //<esc>[5~
          case '6': return PAGE_DOWN;  //<esc>[6~
          case '7': return HOME_KEY; //not being issued
          case '8': return END_KEY;  //not being issued
        }
      }
    } else {
        //sess.showOrgMessage("You pressed %c%c", seq[0], seq[1]); //slz
        switch (seq[1]) {
          case 'A': return ARROW_UP; //<esc>[A
          case 'B': return ARROW_DOWN; //<esc>[B
          case 'C': return ARROW_RIGHT; //<esc>[C
          case 'D': return ARROW_LEFT; //<esc>[D
          case 'H': return HOME_KEY; // <esc>[H - this one is being issued
          case 'F': return END_KEY;  // <esc>[F - this one is being issued
          case 'Z': return SHIFT_TAB; //<esc>[Z
      }
    }

    return '\x1b'; // if it doesn't match a known escape sequence like ] ... or O ... just return escape
  
  } else {
    //sess.showOrgMessage("You pressed %d", c); //slz
    return c;
  }
}

// depends on readKey()
void outlineProcessKeypress(int c) { //prototype has int = 0  

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  c = (!c) ? readKey() : c; // not sure smart since 99.99% want the char that user typed
  switch (org.mode) {
  size_t n;
  //switch (int c = readKey(); O.mode)  //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          colon_N();

          //org.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          //org.command_line.clear();
          //sess.showOrgMessage(":");
          //org.mode = COMMAND_LINE;

          return;

        case '\x1b':
          org.command[0] = '\0';
          org.repeat = 0;
          return;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
          org.outlineInsertRow(0, "", true, false, false, BASE_DATE);
          org.mode = INSERT;
          org.command[0] = '\0';
          org.repeat = 0;
          sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        default:
          if (c < 33 || c > 127) sess.showOrgMessage("<%d> doesn't do anything in NO_ROWS mode", c);
          else sess.showOrgMessage("<%c> doesn't do anything in NO_ROWS mode", c);
          org.command[0] = '\0';
          org.repeat = 0;
          return;
      }
      return; //end swith in NO_ROWS

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
         {
          orow & row = org.rows.at(org.fr);
          std::string msg;
          if (org.view == TASK)  {
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
          org.command[0] = '\0'; 
          org.mode = NORMAL;
          row.dirty = false;
          sess.showOrgMessage3(msg, row.id, row.title);
         }    
          sess.refreshOrgScreen();
          if (org.fc > 0) org.fc--;
          return;

        case HOME_KEY:
          org.fc = 0;
          return;

        case END_KEY:
          {
            orow& row = org.rows.at(org.fr);
          if (row.title.size()) org.fc = row.title.size(); // mimics vim to remove - 1;
          return;
          }

        case BACKSPACE:
          org.outlineBackspace();
          return;

        case DEL_KEY:
          org.outlineDelChar();
          return;

        case '\t':
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          org.outlineMoveCursor(c);
          return;

        case CTRL_KEY('z'):
          // not in use
          return;

        case '\x1b':
          org.command[0] = '\0';
          org.mode = NORMAL;
          if (org.fc > 0) org.fc--;
          sess.showOrgMessage("");
          return;

        default:
          org.outlineInsertChar(c);
          return;
      } //end of switch inside INSERT

     // return; //End of case INSERT: No need for a return at the end of INSERT because we insert the characters that fall through in switch default:

    case NORMAL:  

      if (c == '\x1b') {
        if (org.view == TASK) {
          sess.drawPreviewWindow(org.rows.at(org.fr).id);
        }  
        sess.showOrgMessage("");
        org.command[0] = '\0';
        org.repeat = 0;
        return;
      }
 
      /*leading digit is a multiplier*/
      //if (isdigit(c))  //equiv to if (c > 47 && c < 58)

      if ((c > 47 && c < 58) && (strlen(org.command) == 0)) {

        if (org.repeat == 0 && c == 48) {

        } else if (org.repeat == 0) {
          org.repeat = c - 48;
          return;
        }  else {
          org.repeat = org.repeat*10 + c - 48;
          return;
        }
      }

      if (org.repeat == 0) org.repeat = 1;

      n = strlen(org.command);
      org.command[n] = c;
      org.command[n+1] = '\0';

      if (n_lookup.count(org.command)) {
        n_lookup.at(org.command)();
        org.command[0] = '\0';
        org.repeat = 0;
        return;
      }

      //also means that any key sequence ending in something
      //that matches below will perform command

      // needs to be here because needs to pick up repeat
      //Arrows + h,j,k,l
      if (navigation.count(c)) {
          for (int j = 0;j < org.repeat;j++) org.outlineMoveCursor(c);
          org.command[0] = '\0'; 
          org.repeat = 0;
          return;
      }

      if ((c == PAGE_UP) || (c == PAGE_DOWN)) {
        sess.navigatePageHx(c);
        org.command[0] = '\0';
        org.repeat = 0;
        return;
      }
        
      return; // end of case NORMAL 

    case COMMAND_LINE:

      if (c == '\x1b') {
          org.mode = NORMAL;
          sess.showOrgMessage(""); 
          return;
      }

      if ((c == ARROW_UP) || (c == ARROW_DOWN)) {
        sess.navigateCmdHx(c);
        return;
      }  

      if (c == '\r') {
        std::size_t pos = org.command_line.find(' ');
        std::string cmd = org.command_line.substr(0, pos);
        if (cmd_lookup.count(cmd)) {
          if (pos == std::string::npos) pos = 0;
          cmd_lookup.at(cmd)(pos);
          return;
        }

        sess.showOrgMessage("\x1b[41mNot an outline command: %s\x1b[0m", cmd.c_str());
        org.mode = NORMAL;
        return;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!org.command_line.empty()) org.command_line.pop_back();
      } else {
        org.command_line.push_back(c);
      }

      sess.showOrgMessage(":%s", org.command_line.c_str());
      return; //end of case COMMAND_LINE

    case FIND:  
      switch (c) {

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            org.fr = (sess.textlines > org.fr) ? 0 : org.fr - sess.textlines; //O.fr and sess.textlines are unsigned ints
          } else if (c == PAGE_DOWN) {
             org.fr += sess.textlines;
             if (org.fr > org.rows.size() - 1) org.fr = org.rows.size() - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
        case 'h':
        case 'l':
          org.outlineMoveCursor(c);
          return;

        //TAB and SHIFT_TAB moves from FIND to OUTLINE NORMAL mode but SHIFT_TAB gets back
        case '\t':  
        case SHIFT_TAB:  
          org.fc = 0; 
          org.mode = NORMAL;
          sess.drawPreviewWindow(org.rows.at(org.fr).id);
          sess.showOrgMessage("");
          return;

        default:
          org.mode = NORMAL;
          org.command[0] = '\0'; 

          // Using outlineProcessKeypress here means almost any key puts you into NORMAL
          // mode and processes that key
          outlineProcessKeypress(c); 
          //if (c < 33 || c > 127) sess.showOrgMessage("<%d> doesn't do anything in FIND mode", c);
          //else sess.showOrgMessage("<%c> doesn't do anything in FIND mode", c);
          return;
      } // end of switch(c) in case FIND

    case VISUAL:
  
      switch (c) {
  
        //case ARROW_UP:
        //case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        //case 'j':
        //case 'k':
        case 'l':
          org.outlineMoveCursor(c);
          org.highlight[1] = org.fc; //this needs to be getFileCol
          return;
  
        case 'x':
          org.repeat = abs(org.highlight[1] - org.highlight[0]) + 1;
          org.outlineYankString(); //reportedly segfaults on the editor side

          // the delete below requires positioning the cursor
          org.fc = (org.highlight[1] > org.highlight[0]) ? org.highlight[0] : org.highlight[1];

          for (int i = 0; i < org.repeat; i++) {
            org.outlineDelChar(); //uses editorDeleteChar2! on editor side
          }
          if (org.fc) org.fc--; 
          org.command[0] = '\0';
          org.repeat = 0;
          org.mode = 0;
          sess.showOrgMessage("");
          return;
  
        case 'y':  
          org.repeat = org.highlight[1] - org.highlight[0] + 1;
          org.fc = org.highlight[0];
          org.outlineYankString();
          org.command[0] = '\0';
          org.repeat = 0;
          org.mode = 0;
          sess.showOrgMessage("");
          return;
  
        case '\x1b':
          org.mode = NORMAL;
          org.command[0] = '\0';
          org.repeat = 0;
          sess.showOrgMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      //return; //end of case VISUAL (return here would not be executed)

    case REPLACE: 

      if (org.repeat == 0) org.repeat = 1; //10062020

      if (c == '\x1b') {
        org.command[0] = '\0';
        org.repeat = 0;
        org.mode = NORMAL;
        return;
      }

      for (int i = 0; i < org.repeat; i++) {
        org.outlineDelChar();
        org.outlineInsertChar(c);
      }

      org.repeat = 0;
      org.command[0] = '\0';
      org.mode = NORMAL;

      return; //////// end of outer case REPLACE

    case ADD_CHANGE_FILTER:

      switch(c) {

        case '\x1b':
          {
          org.mode = COMMAND_LINE;
          size_t temp = sess.page_hx_idx;  
          sess.showOrgMessage(":%s", sess.page_history.at(sess.page_hx_idx).c_str());
          org.command_line = sess.page_history.at(sess.page_hx_idx);
          outlineProcessKeypress('\r');
          org.mode = NORMAL;
          org.command[0] = '\0';
          org.command_line.clear();
          sess.page_history.pop_back();
          sess.page_hx_idx = temp;
          org.repeat = 0;
          org.current_task_id = -1; //not sure this is right
          }
          return;

        case '\r':
          {
          orow& row = org.rows.at(org.fr); //currently highlighted keyword
          if (org.marked_entries.empty()) {
            switch (org.view) {
              case KEYWORD:
                //add_task_keyword(row.id, org.current_task_id);
                addTaskKeyword(row.id, org.current_task_id);
                sess.showOrgMessage("No tasks were marked so added keyword %s to current task",
                                   row.title.c_str());
                break;
              case FOLDER:
                //org.update_task_folder(row.title, org.current_task_id);
                updateTaskFolder(row.title, org.current_task_id);
                sess.showOrgMessage("No tasks were marked so current task had folder changed to %s",
                                   row.title.c_str());
                break;
              case CONTEXT:
                //org.update_task_context(row.title, org.current_task_id);
                updateTaskContext(row.title, org.current_task_id);
                sess.showOrgMessage("No tasks were marked so current task had context changed to %s",
                                   row.title.c_str());
                break;
            }
          } else {
            for (const auto& task_id : org.marked_entries) {
              switch (org.view) {
                case KEYWORD:
                  //add_task_keyword(row.id, task_id);
                  addTaskKeyword(row.id, task_id);
                  sess.showOrgMessage("Marked tasks had keyword %s added",
                                     row.title.c_str());
                break;
                case FOLDER:
                  //org.update_task_folder(row.title, task_id);
                  updateTaskFolder(row.title, task_id);
                  sess.showOrgMessage("Marked tasks had folder changed to %s",
                                     row.title.c_str());
                break;
                case CONTEXT:
                  //org.update_task_context(row.title, task_id);
                  updateTaskContext(row.title, task_id);
                  sess.showOrgMessage("Marked tasks had context changed to %s",
                                     row.title.c_str());
                break;
              }
            }
          }
          }

          org.command[0] = '\0'; //might not be necessary
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
          org.outlineMoveCursor(c);
          //O.command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          //O.repeat = 0;
          return;

        default:
          if (c < 33 || c > 127) sess.showOrgMessage("<%d> doesn't do anything in ADD_CHANGE_FILTER mode", c);
          else sess.showOrgMessage("<%c> doesn't do anything in ADD_CHANGE_FILTER mode", c);
          return;
      }

      return; //end  ADD_CHANGE_FILTER - do nothing if no c match

    case FILE_DISPLAY: 

      switch (c) {
  
        case ARROW_UP:
        case 'k':
          sess.initial_file_row--;
          sess.initial_file_row = (sess.initial_file_row < 0) ? 0: sess.initial_file_row;
          break;

        case ARROW_DOWN:
        case 'j':
          sess.initial_file_row++;
          break;

        case PAGE_UP:
          sess.initial_file_row = sess.initial_file_row - sess.textlines;
          sess.initial_file_row = (sess.initial_file_row < 0) ? 0: sess.initial_file_row;
          break;

        case PAGE_DOWN:
          sess.initial_file_row = sess.initial_file_row + sess.textlines;
          break;

        case ':':
          sess.showOrgMessage(":");
          org.command[0] = '\0';
          org.command_line.clear();
          //O.last_mode was set when entering file mode
          org.mode = COMMAND_LINE;
          return;

        case '\x1b':
          org.mode = org.last_mode;
          sess.eraseRightScreen();
          if (org.view == TASK) {
            sess.drawPreviewWindow(org.rows.at(org.fr).id);
          } else {
            Container c = getContainerInfo(org.rows.at(org.fr).id);
            if (c.id != 0) 
              sess.displayContainerInfo(c);
          }
          org.command[0] = '\0';
          org.repeat = 0;
          sess.showOrgMessage("");
          return;
      }

      sess.displayFile();

      return;
  } //end of outer switch(O.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
} //end outlineProcessKeypress

// calls readKey()
bool editorProcessKeypress(void) {
  //int start, end;
  int i;

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  switch (int c = readKey(); sess.p->mode) {

    case NO_ROWS:

      switch(c) {

        case '\x1b':
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          return false;

        case ':':
          sess.p->mode = COMMAND_LINE;
          sess.p->command_line.clear();
          sess.p->command[0] = '\0';
          sess.p->editorSetMessage(":");
          return false;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
        case 'O':
        case 'o':
          //p->editorInsertRow(0, std::string());
          sess.p->mode = INSERT;
          sess.p->last_command = "i"; //all the commands equiv to i
          sess.p->prev_fr = 0;
          sess.p->prev_fc = 0;
          sess.p->last_repeat = 1;
          sess.p->snapshot.clear();
          sess.p->snapshot.push_back("");
          sess.p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          //p->command[0] = '\0';
          //p->repeat = 0;
          // ? p->redraw = true;
          return true;

        case CTRL_KEY('h'):
          sess.p->command[0] = '\0';
          if (sess.editors.size() == 1) {
            sess.editor_mode = false;
            sess.drawPreviewWindow(org.rows.at(org.fr).id);
            return false;
          }
          {
            if (sess.p->is_subeditor) sess.p = sess.p->linked_editor;
            auto v = sess.editors | std::ranges::views::filter([](auto e){return !e->is_subeditor;});
            std::vector<Editor *> temp{std::begin(v), std::end(v)};    
            auto it = std::find(temp.begin(), temp.end(), sess.p);
            int index = std::distance(temp.begin(), it);
            Editor::editorSetMessage("index: %d; length: %d", index, temp.size());
            if (index) {
              sess.p = temp[index - 1];
              if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
              else sess.p->mode = NORMAL;
              return false;
            } else {sess.editor_mode = false;
              sess.drawPreviewWindow(org.rows.at(org.fr).id);
              return false;
            }
          }
      
        case  CTRL_KEY('l'):
          sess.p->command[0] = '\0';
          {
            if (sess.p->is_subeditor) sess.p = sess.p->linked_editor;
            auto v = sess.editors | std::ranges::views::filter([](auto e){return !e->is_subeditor;});
            std::vector<Editor *> temp{std::begin(v), std::end(v)};    
            auto it = std::find(temp.begin(), temp.end(), sess.p);
            int index = std::distance(temp.begin(), it);
            Editor::editorSetMessage("index: %d; length: %d", index, temp.size());
            if (index < temp.size() - 1) sess.p = temp[index + 1];
            if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
            else sess.p->mode = NORMAL;
          }
          return false;

        case CTRL_KEY('k'):  
        case CTRL_KEY('j'):  
          Editor::editorSetMessage("Editor <-> subEditor");
          sess.p->command[0] = '\0';
          if (sess.p->linked_editor) sess.p = sess.p->linked_editor;
          else return false;

          if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
          else sess.p->mode = NORMAL;

          return false;
      }

      return false;

    case INSERT:

      switch (c) {

        case '\r':
          sess.p->editorInsertReturn();
          sess.p->last_typed += c;
          return true;

        // not sure this is in use
        case CTRL_KEY('s'):
          sess.p->editorSaveNoteToFile("lm_temp");
          return false;

        case HOME_KEY:
          sess.p->editorMoveCursorBOL();
          return false;

        case END_KEY:
          sess.p->editorMoveCursorEOL();
          sess.p->editorMoveCursor(ARROW_RIGHT);
          return false;

        case BACKSPACE:
          sess.p->editorBackspace();

          //not handling backspace correctly
          //when backspacing deletes more than currently entered text
          //A common case would be to enter insert mode  and then just start backspacing
          //because then dotting would actually delete characters
          //I could record a \b and then handle similar to handling \r
          if (!sess.p->last_typed.empty()) sess.p->last_typed.pop_back();
          return true;
    
        case DEL_KEY:
          sess.p->editorDelChar();
          return true;
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          sess.p->editorMoveCursor(c);
          return false;
    
        case CTRL_KEY('b'):
        //case CTRL_KEY('i'): CTRL_KEY('i') -> 9 same as tab
        case CTRL_KEY('e'):
          sess.p->push_current(); //p->editorCreateSnapshot();
          sess.p->editorDecorateWord(c);
          return true;
    
        // this should be a command line command
        case CTRL_KEY('z'):
          sess.p->smartindent = (sess.p->smartindent) ? 0 : SMARTINDENT;
          sess.p->editorSetMessage("smartindent = %d", sess.p->smartindent); 
          return false;
    
        case '\x1b':

          /*
           * below deals with certain NORMAL mode commands that
           * cause entry to INSERT mode includes dealing with repeats
           */

          //i,I,a,A - deals with repeat
          if(cmd_map1.contains(sess.p->last_command)) { 
            sess.p->push_current(); //
            for (int n=0; n<sess.p->last_repeat-1; n++) {
              for (char const &c : sess.p->last_typed) {sess.p->editorInsertChar(c);}
            }
          }

          //cmd_map2 -> E_o_escape and E_O_escape - here deals with deals with repeat > 1
          if (cmd_map2.contains(sess.p->last_command)) {
            (sess.p->*cmd_map2.at(sess.p->last_command))(sess.p->last_repeat - 1);
            sess.p->push_current();
          }

          //cw, caw, s
          if (cmd_map4.contains(sess.p->last_command)) {
            sess.p->push_current();
          }
          //'I' in VISUAL BLOCK mode
          if (sess.p->last_command == "VBI") {
            for (int n=0; n<sess.p->last_repeat-1; n++) {
              for (char const &c : sess.p->last_typed) {sess.p->editorInsertChar(c);}
            }
            int temp = sess.p->fr;

            for (sess.p->fr=sess.p->fr+1; sess.p->fr<sess.p->vb0[1]+1; sess.p->fr++) {
              for (int n=0; n<sess.p->last_repeat; n++) { //NOTICE not p->last_repeat - 1
                sess.p->fc = sess.p->vb0[0]; 
                for (char const &c : sess.p->last_typed) {sess.p->editorInsertChar(c);}
              }
            }
            sess.p->fr = temp;
            sess.p->fc = sess.p->vb0[0];
          }

          //'A' in VISUAL BLOCK mode
          if (sess.p->last_command == "VBA") {
            for (int n=0; n<sess.p->last_repeat-1; n++) {
              for (char const &c : sess.p->last_typed) {sess.p->editorInsertChar(c);}
            }
            //{ 12302020
            int temp = sess.p->fr;

            for (sess.p->fr=sess.p->fr+1; sess.p->fr<sess.p->vb0[1]+1; sess.p->fr++) {
              for (int n=0; n<sess.p->last_repeat; n++) { //NOTICE not p->last_repeat - 1
                int size = sess.p->rows.at(sess.p->fr).size();
                if (sess.p->vb0[2] > size) sess.p->rows.at(sess.p->fr).insert(size, sess.p->vb0[2]-size, ' ');
                sess.p->fc = sess.p->vb0[2];
                for (char const &c : sess.p->last_typed) {sess.p->editorInsertChar(c);}
              }
            }
            sess.p->fr = temp;
            sess.p->fc = sess.p->vb0[0];
          //} 12302020
          }

          /*Escape whatever else happens falls through to here*/
          sess.p->mode = NORMAL;
          sess.p->repeat = 0;

          //? redundant - see 10 lines below
          sess.p->last_typed = std::string(); 

          if (sess.p->fc > 0) sess.p->fc--;

          // below - if the indent amount == size of line then it's all blanks
          // can hit escape with p->row == NULL or p->row[p->fr].size == 0
          if (!sess.p->rows.empty() && sess.p->rows[sess.p->fr].size()) {
            int n = sess.p->editorIndentAmount(sess.p->fr);
            if (n == sess.p->rows[sess.p->fr].size()) {
              sess.p->fc = 0;
              for (int i = 0; i < n; i++) {
                sess.p->editorDelChar();
              }
            }
          }
          sess.p->editorSetMessage(""); // commented out to debug push_current
          //editorSetMessage(p->last_typed.c_str());
          sess.p->last_typed.clear();//////////// 09182020
          return true; //end case x1b:
    
        // deal with tab in insert mode - was causing segfault  
        case '\t':
          for (int i=0; i<4; i++) sess.p->editorInsertChar(' ');
          return true;  

        default:
          sess.p->editorInsertChar(c);
          sess.p->last_typed += c;
          return true;
     
      } //end inner switch for outer case INSERT

      return true; // end of case INSERT: - should not be executed

    case NORMAL: 

      switch(c) {
 
        case '\x1b':
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          return false;

        case ':':
          sess.p->mode = COMMAND_LINE;
          sess.p->command_line.clear();
          sess.p->command[0] = '\0';
          sess.p->editorSetMessage(":");
          return false;

        case '/':
          sess.p->mode = SEARCH;
          sess.p->command_line.clear();
          sess.p->command[0] = '\0';
          sess.p->editorSetMessage("/");
          return false;

        case 'u':
          sess.p->command[0] = '\0';
          sess.p->undo();
          return true;

        case CTRL_KEY('r'):
          sess.p->command[0] = '\0';
          sess.p->redo();
          return true;

          /*
        case CTRL_KEY('w'):
          p->E_resize(1);
          p->command[0] = '\0';
          p->repeat = 0;
          return true;
         */

        case CTRL_KEY('h'):
          sess.p->command[0] = '\0';
          if (sess.editors.size() == 1) {
            //sess.editor_mode = false;

            if (sess.divider < 10) {
              sess.cfg.ed_pct = 80;
              sess.moveDivider(80);
            }  

            sess.editor_mode = false; //needs to be here

            sess.drawPreviewWindow(org.rows.at(org.fr).id);
            org.mode = NORMAL;
            sess.returnCursor(); 
            return false;
          }
          {
            if (sess.p->is_subeditor) sess.p = sess.p->linked_editor;
            auto v = sess.editors | std::ranges::views::filter([](auto e){return !e->is_subeditor;});
            std::vector<Editor *> temp{std::begin(v), std::end(v)};    
            auto it = std::find(temp.begin(), temp.end(), sess.p);
            int index = std::distance(temp.begin(), it);
            Editor::editorSetMessage("index: %d; length: %d", index, temp.size());
            if (index) {
              sess.p = temp[index - 1];
              if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
              else sess.p->mode = NORMAL;
              return false;
            } else {
              //sess.editor_mode = false;

            if (sess.divider < 10) {
              sess.cfg.ed_pct = 80;
              sess.moveDivider(80);
            }  

              sess.editor_mode = false; //needs to be here

              sess.drawPreviewWindow(org.rows.at(org.fr).id);
              org.mode = NORMAL;
              sess.returnCursor(); 
              return false;
            }
          }
      
        case  CTRL_KEY('l'):
          sess.p->command[0] = '\0';
          {
            if (sess.p->is_subeditor) sess.p = sess.p->linked_editor;
            auto v = sess.editors | std::ranges::views::filter([](auto e){return !e->is_subeditor;});
            std::vector<Editor *> temp{std::begin(v), std::end(v)};    
            auto it = std::find(temp.begin(), temp.end(), sess.p);
            int index = std::distance(temp.begin(), it);
            Editor::editorSetMessage("index: %d; length: %d", index, temp.size());
            //p->editorSetMessage("index: %d", index);
            //p->editorRefreshScreen(false); // needs to be here because p moves!
            if (index < temp.size() - 1) sess.p = temp[index + 1];
            if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
            else sess.p->mode = NORMAL;
          }
          return false;

        case  CTRL_KEY('k'):
        case  CTRL_KEY('j'):
          Editor::editorSetMessage("Editor <-> subEditor");
          sess.p->command[0] = '\0';
          if (sess.p->linked_editor) sess.p=sess.p->linked_editor;
          else return false;

          if (sess.p->rows.empty()) sess.p->mode = NO_ROWS;
          else sess.p->mode = NORMAL;

          return false;

      } //end switch

      /*leading digit is a multiplier*/

      if ((c > 47 && c < 58) && (strlen(sess.p->command) == 0)) {

        if (sess.p->repeat == 0 && c == 48) {

        } else if (sess.p->repeat == 0) {
          sess.p->repeat = c - 48;
          // return false because command not complete
          return false;
        } else {
          sess.p->repeat = sess.p->repeat*10 + c - 48;
          // return false because command not complete
          return false;
        }
      }
      if ( sess.p->repeat == 0 ) sess.p->repeat = 1;
      {
        int n = strlen(sess.p->command);
        sess.p->command[n] = c;
        sess.p->command[n+1] = '\0';
      }

      /* this and next if should probably be dropped
       * and just use CTRL_KEY('w') to toggle
       * size of windows and right now can't reach
       * them given CTRL('w') above
       */

      //if (std::string_view(p->command) == std::string({0x17,'='})) 
      //if (p->command == std::string({0x17,'='})) 
      if (sess.p->command == std::string_view("\x17" "=")) {
        sess.p->E_resize(0);
        sess.p->command[0] = '\0';
        sess.p->repeat = 0;
        return false;
      }

      //if (std::string_view(p->command) == std::string({0x17,'_'})) 
      //if (p->command == std::string({0x17,'_'})) 
      if (sess.p->command == std::string_view("\x17" "_")) {
        sess.p->E_resize(0);
        sess.p->command[0] = '\0';
        sess.p->repeat = 0;
        return false;
      }

      if (e_lookup.contains(sess.p->command)) {

        sess.p->prev_fr = sess.p->fr;
        sess.p->prev_fc = sess.p->fc;

        sess.p->snapshot = sess.p->rows; ////////////////////////////////////////////09182020

        (sess.p->*e_lookup.at(sess.p->command))(sess.p->repeat); //money shot

        if (insert_cmds.count(sess.p->command)) {
          sess.p->mode = INSERT;
          sess.p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
          sess.p->last_repeat = sess.p->repeat;
          sess.p->last_command = sess.p->command; //p->last_command must be a string
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          return true;
        } else if (move_only.count(sess.p->command)) {
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          return false; //note text did not change
        } else {
          if (sess.p->command[0] != '.') {
            sess.p->last_repeat = sess.p->repeat;
            sess.p->last_command = sess.p->command; //p->last_command must be a string
            sess.p->push_current();
            sess.p->command[0] = '\0';
            sess.p->repeat = 0;
          } else {//if dot
            //if dot then just repeast last command at new location
            sess.p->push_previous();
          }
        }    
      }

      // needs to be here because needs to pick up repeat
      //Arrows + h,j,k,l
      if (navigation.count(c)) {
          for (int j=0; j<sess.p->repeat; j++) sess.p->editorMoveCursor(c);
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          return false;
      }

      if ((c == PAGE_UP) || (c == PAGE_DOWN)) {
          sess.p->editorPageUpDown(c);
          sess.p->command[0] = '\0'; //arrow does reset command in vim although left/right arrow don't do anything = escape
          sess.p->repeat = 0;
          return false;
      }

      return true;// end of case NORMAL - there are breaks that can get to code above

    case COMMAND_LINE:

      if (c == '\x1b') {
        sess.p->mode = NORMAL;
        sess.p->command[0] = '\0';
        sess.p->repeat = sess.p->last_repeat = 0;
        sess.p->editorSetMessage(""); 
        return false;
      }
      // prevfilename is everything after :readfile 
      if (c == '\t') {
        std::size_t pos = sess.p->command_line.find(' ');
        std::string cmd = sess.p->command_line.substr(0, pos);
        if (file_cmds.contains(cmd)) { // can't use string_view here 
          std::string s = sess.p->command_line.substr(pos+1);
          // should to deal with s being empty
          if (s.front() == '~') s = fmt::format("{}/{}", getenv("HOME"), s.substr(2));

          // finding new set of tab completions because user typed or deleted something  
          // which means we need a new set of completion possibilities
          if (s != ac.prevfilename) {
            ac.completions.clear();
            ac.completion_index = 0;
            std::string path;
            if (s.front() == '/') {
              size_t pos = s.find_last_of('/');
              ac.prefix = s.substr(0, pos+1);
              path = ac.prefix;
              s = s.substr(pos+1);
              //assume below we want current_directory if what's typed isn't ~/.. or /..
            } else path = std::filesystem::current_path().string(); 

            std::filesystem::path pathToShow(path);  
            for (const auto& entry : std::filesystem::directory_iterator(pathToShow)) {
              const auto filename = entry.path().filename().string();
              if (cmd.starts_with("save") && !entry.is_directory()) continue; ///////////// 11-21-2020 
              if (filename.starts_with(s)) {
                if (entry.is_directory()) ac.completions.push_back(filename+'/');
                else ac.completions.push_back(filename);
              }
            }  
          }  
          // below is where we present/cycle through completions  
          if (!ac.completions.empty())  {
            if (ac.completion_index == ac.completions.size()) ac.completion_index = 0;
            ac.prevfilename = ac.prefix + ac.completions.at(ac.completion_index++);
            //p->command_line = "readfile " + prevfilename;
            sess.p->command_line = fmt::format("{} {}", cmd, ac.prevfilename);
            sess.p->editorSetMessage(":%s", sess.p->command_line.c_str());
          }
        } else {
        sess.p->editorSetMessage("tab"); 
        }
        return false;
      }

      if (c == '\r') {

        // right now only command that has a space is readfile
        std::size_t pos = sess.p->command_line.find(' ');
        std::string cmd = sess.p->command_line.substr(0, pos);

        // note that right now we are not calling editor commands like E_write_close_C
        // and E_quit_C and E_quit0_C
        if (quit_cmds.count(cmd)) {
          if (cmd == "x") {
            if (sess.p->is_subeditor) {
              sess.p->mode = NORMAL;
              sess.p->command[0] = '\0';
              sess.p->command_line.clear();
              sess.p->editorSetMessage("You can't save the contents of the Output Window");
              return false;
            }
            //update_note(false, true); //should be p->E_write_C(); closing_editor = true;
            updateNote(); //should be p->E_write_C(); closing_editor = true;
          } else if (cmd == "q!" || cmd == "quit!") {
            // do nothing = allow editor to be closed
          } else if (sess.p->dirty) {
              sess.p->mode = NORMAL;
              sess.p->command[0] = '\0';
              sess.p->command_line.clear();
              sess.p->editorSetMessage("No write since last change");
              return false;
          }

          //eraseRightScreen(); //moved below on 10-24-2020

          std::erase(sess.editors, sess.p); //c++20
          if (sess.p->linked_editor) {
             std::erase(sess.editors, sess.p->linked_editor); //c++20
             delete sess.p->linked_editor;
          }

          delete sess.p; //p given new value below

          if (!sess.editors.empty()) {

            sess.p = sess.editors[0]; //kluge should move in some logical fashion
            sess.positionEditors();
            sess.eraseRightScreen(); //moved down here on 10-24-2020
            sess.drawEditors();

          } else { // we've quit the last remaining editor(s)
            sess.p = nullptr;
            sess.editor_mode = false;
            sess.eraseRightScreen();

            if (sess.divider < 10) {
              sess.cfg.ed_pct = 80;
              sess.moveDivider(80);
            }  

            sess.drawPreviewWindow(org.rows.at(org.fr).id);
            sess.returnCursor(); //because main while loop if started in editor_mode -- need this 09302020
          }

          return false;
        } //end quit_cmds

        if (E_lookup_C.count(cmd)) {
          (sess.p->*E_lookup_C.at(cmd))();

          sess.p->mode = NORMAL;
          sess.p->command[0] = '\0';
          sess.p->command_line.clear();

          return true; //note spellcheck and cmd require redraw but not all command line commands (e.g. w)
        }

        sess.p->editorSetMessage("\x1b[41mNot an editor command: %s\x1b[0m", sess.p->command_line.c_str());
        sess.p->mode = NORMAL;
        return false;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!sess.p->command_line.empty()) sess.p->command_line.pop_back();
      } else {
        sess.p->command_line.push_back(c);
      }

      sess.p->editorSetMessage(":%s", sess.p->command_line.c_str());
      return false; //end of case COMMAND_LINE

    case SEARCH:

      if (c == '\x1b') {
        sess.p->mode = NORMAL;
        sess.p->command[0] = '\0';
        sess.p->repeat = sess.p->last_repeat = 0;
        sess.p->editorSetMessage(""); 
        return false;
      }

      if (c == '\r') {
        sess.p->mode = NORMAL;
        sess.p->command[0] = '\0';
        sess.p->search_string = sess.p->command_line;
        sess.p->command_line.clear();
        sess.p->editorFindNextWord();
        return false;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!sess.p->command_line.empty()) sess.p->command_line.pop_back();
      } else if (c != '\t') { //ignore tabs
        sess.p->command_line.push_back(c);
      }

      Editor::editorSetMessage("/%s", sess.p->command_line.c_str());
      return false; //end of case COMMAND_LINE

    case VISUAL_LINE:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          sess.p->editorMoveCursor(c);
          sess.p->highlight[1] = sess.p->fr;
          return true;
    
        case 'x':
          if (!sess.p->rows.empty()) {
            sess.p->push_current(); //p->editorCreateSnapshot();
            sess.p->repeat = sess.p->highlight[1] - sess.p->highlight[0] + 1;
            sess.p->fr = sess.p->highlight[0]; 
            sess.p->editorYankLine(sess.p->repeat);
    
            for (int i=0; i < sess.p->repeat; i++) sess.p->editorDelRow(sess.p->highlight[0]);
          }

          sess.p->fc = 0;
          sess.p->command[0] = '\0';
          sess.p->repeat = sess.p->last_repeat = 0;
          if (sess.p->mode != NO_ROWS) sess.p->mode = NORMAL;
          sess.p->editorSetMessage("");
          return true;
    
        case 'y':  
          sess.p->repeat = sess.p->highlight[1] - sess.p->highlight[0] + 1;
          sess.p->fr = sess.p->highlight[0];
          sess.p->editorYankLine(sess.p->repeat);
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        case CTRL_KEY('c'):  
          {
          int fr = sess.p->highlight[0];
          int n = sess.p->highlight[1] - fr + 1;
          std::vector<std::string>clipboard_buffer{};

          for (int i=0; i < n; i++) {
            clipboard_buffer.push_back(sess.p->rows.at(fr+i)+'\n');
          }

          Editor::convert2base64(clipboard_buffer); 
          }
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;

        case '>':
          sess.p->push_current(); //p->editorCreateSnapshot();
          sess.p->repeat = sess.p->highlight[1] - sess.p->highlight[0] + 1;
          sess.p->fr = sess.p->highlight[0];
          for ( i = 0; i < sess.p->repeat; i++ ) {
            sess.p->editorIndentRow();
            sess.p->fr++;}
          sess.p->fr-=i;
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        // changed to p->fr on 11-26-2019
        case '<':
          sess.p->push_current(); //p->editorCreateSnapshot();
          sess.p->repeat = sess.p->highlight[1] - sess.p->highlight[0] + 1;
          sess.p->fr = sess.p->highlight[0];
          for ( i = 0; i < sess.p->repeat; i++ ) {
            sess.p->editorUnIndentRow();
            sess.p->fr++;}
          sess.p->fr-=i;
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        case '\x1b':
          sess.p->mode = 0;
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL_BLOCK:

      switch (c) {
    
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'j':
        case 'k':
        case 'l':
          sess.p->editorMoveCursor(c);
          //p->highlight[1] = E.fr;
          return true;
    
        case '$':
          sess.p->editorMoveCursorEOL();
          sess.p->command[0] = '\0';
          sess.p->repeat = sess.p->last_repeat = 0;
          sess.p->editorSetMessage("");
          return true;

        case 'x':
          if (!sess.p->rows.empty()) {
            //p->editorCreateSnapshot();
    
          for (int i = sess.p->vb0[1]; i < sess.p->fr + 1; i++) {
            sess.p->rows.at(i).erase(sess.p->vb0[0], sess.p->fc - sess.p->vb0[0] + 1); //needs to be cleaned up for p->fc < p->vb0[0] ? abs
          }

          sess.p->fc = sess.p->vb0[0];
          sess.p->fr = sess.p->vb0[1];
          }
          sess.p->command[0] = '\0';
          sess.p->repeat = sess.p->last_repeat = 0;
          sess.p->mode = NORMAL;
          sess.p->editorSetMessage("");
          return true;
    
        case 'I':
          if (!sess.p->rows.empty()) {
            //p->editorCreateSnapshot();
      
          //p->repeat = p->fr - p->vb0[1];  
            {
          int temp = sess.p->fr; //p->fr is wherever cursor Y is    
          //p->vb0[2] = p->fr;
          sess.p->fc = sess.p->vb0[0]; //vb0[0] is where cursor X was when ctrl-v happened
          sess.p->fr = sess.p->vb0[1]; //vb0[1] is where cursor Y was when ctrl-v happened
          sess.p->vb0[1] = temp; // resets p->vb0 to last cursor Y position - this could just be p->vb0[2]
          //cmd_map1[c](p->repeat);
          //command = -1;
          sess.p->repeat = 1;
          sess.p->mode = INSERT;
          sess.p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          sess.p->last_repeat = sess.p->repeat;
          sess.p->last_typed.clear();
          //p->last_command = command;
          sess.p->last_command = "VBI";
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          //editorSetMessage("command = %d", command);
          return true;

        case 'A':
          if (!sess.p->rows.empty()) {
            //p->editorCreateSnapshot();
      
          //p->repeat = p->fr - p->vb0[1];  
            {
          int temp = sess.p->fr;    
          sess.p->fr = sess.p->vb0[1];
          sess.p->vb0[1] = temp;
          sess.p->fc++;
          sess.p->vb0[2] = sess.p->fc;
          //int last_row_size = p->rows.at(p->vb0[1]).size();
          int first_row_size = sess.p->rows.at(sess.p->fr).size();
          if (sess.p->vb0[2] > first_row_size) sess.p->rows.at(sess.p->fr).insert(first_row_size, sess.p->vb0[2]-first_row_size, ' ');
          //cmd_map1[c](p->repeat);
          //command = -2;
          sess.p->repeat = 1;
          sess.p->mode = INSERT;
          sess.p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
            }

          }

          sess.p->last_repeat = sess.p->repeat;
          sess.p->last_typed.clear();
          //p->last_command = command;
          sess.p->last_command = "VBA";
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          //editorSetMessage("command = %d", command);
          return true;

        case '\x1b':
          sess.p->mode = 0;
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }

      return false;

    case VISUAL:

      switch (c) {
    
        //case ARROW_UP:
        //case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        //case 'j':
        //case 'k':
        case 'l':
          sess.p->editorMoveCursor(c);
          sess.p->highlight[1] = sess.p->fc;
          return true;
    
        case 'x':
          if (!sess.p->rows.empty()) {
            sess.p->push_current(); //p->editorCreateSnapshot();
            sess.p->editorYankString(); 
            sess.p->editorDeleteVisual();
          }
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        case 'y':
          sess.p->fc = sess.p->highlight[0];
          sess.p->editorYankString();
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        case 'p':
          sess.p->push_current();
          if (!Editor::string_buffer.empty()) sess.p->editorPasteStringVisual();
          else sess.p->editorPasteLineVisual();
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = NORMAL;
          return true;

        case CTRL_KEY('b'):
        case CTRL_KEY('i'):
        case CTRL_KEY('e'):
          sess.p->push_current(); //p->editorCreateSnapshot();
          sess.p->editorDecorateVisual(c);
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;
    
        case CTRL_KEY('c'):  
          sess.p->fc = sess.p->highlight[0];
          sess.p->copyStringToClipboard();
          sess.p->command[0] = '\0';
          sess.p->repeat = 0;
          sess.p->mode = 0;
          sess.p->editorSetMessage("");
          return true;

        case '\x1b':
          sess.p->mode = NORMAL;
          sess.p->command[0] = '\0';
          sess.p->repeat = sess.p->last_repeat = 0;
          sess.p->editorSetMessage("");
          return true;
    
        default:
          return false;
      }
    
      return false;

    case REPLACE:

      if (c == '\x1b') {
        sess.p->command[0] = '\0';
        sess.p->repeat = sess.p->last_repeat = 0;
        sess.p->last_command = "";
        sess.p->last_typed.clear();
        sess.p->mode = NORMAL;
        return true;
      }

      //editorCreateSnapshot();
      for (int i = 0; i < sess.p->last_repeat; i++) {
        sess.p->editorDelChar();
        sess.p->editorInsertChar(c);
        sess.p->last_typed.clear();
        sess.p->last_typed += c;
      }
      //other than p->mode = NORMAL - all should go
      sess.p->last_command = "r";
      sess.p->push_current();
      //p->last_repeat = p->repeat;
      sess.p->repeat = 0;
      sess.p->command[0] = '\0';
      sess.p->mode = NORMAL;
      return true;

  }  //end of outer switch(p->mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
  return true; // this should not be reachable but was getting an error
} //end of editorProcessKeyPress

void initOutline() {
  org.cx = 0; //cursor x position
  org.cy = 0; //cursor y position
  org.fc = 0; //file x position
  org.fr = 0; //file y position
  org.rowoff = 0;  //number of rows scrolled off the screen
  org.coloff = 0;  //col the user is currently scrolled to  
  org.sort = "modified"; //Entry sort column
  org.show_deleted = false; //not treating these separately right now
  org.show_completed = true;
  org.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  org.highlight[0] = org.highlight[1] = -1;
  org.mode = NORMAL; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  org.last_mode = NORMAL;
  org.command[0] = '\0';
  org.command_line = "";
  org.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  org.view = TASK; // not necessary here since set when searching database
  org.taskview = BY_FOLDER;
  org.folder = "todo";
  org.context = "No Context";
  org.keyword = "";

  // ? where this should be.  Also in signal.
  sess.textlines = sess.screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  sess.divider = sess.screencols - sess.cfg.ed_pct * sess.screencols/100;
  sess.totaleditorcols = sess.screencols - sess.divider - 1; // was 2 
}

int main(int argc, char** argv) { 

  initLogger();

  sess.publisher.bind("tcp://*:5556");

  sess.lock.l_whence = SEEK_SET;
  sess.lock.l_start = 0;
  sess.lock.l_len = 0;
  sess.lock.l_pid = getpid();

  if (argc > 1 && argv[1][0] == '-') sess.lm_browser = false;

  sess.getConn(); //for pg
  sess.loadMeta(); //meta html for lm_browser 

  generateContextMap();
  generateFolderMap();

  sess.getWindowSize();
  sess.enableRawMode();
  initOutline();
  sess.eraseScreenRedrawLines();
  getItems(MAX);
  sess.command_history.push_back("of todo"); //klugy - this could be read from config and generalized
  sess.page_history.push_back("of todo"); //klugy - this could be read from config and generalized
  
  signal(SIGWINCH, signalHandler);

  sess.refreshOrgScreen();
  sess.drawOrgStatusBar();
  sess.showOrgMessage3("rows: {}  columns: {}", sess.screenlines, sess.screencols);
  sess.returnCursor();

  if (sess.lm_browser) std::system("./lm_browser current.html &"); //&=> returns control

  while (sess.run) {
    // just refresh what has changed
    if (sess.editor_mode) {
      bool text_change = editorProcessKeypress(); 

      // editorProcessKeypress can take you out of editor mode (either ctrl-H or closing last editor
      if (!sess.editor_mode) continue;

      bool scroll = sess.p->editorScroll();
      bool redraw = (text_change || scroll || sess.p->redraw); //instead of p->redraw => clear_highlights
      sess.p->editorRefreshScreen(redraw);

      if (sess.lm_browser && scroll) {
        zmq::message_t message(20);
        snprintf ((char *) message.data(), 20, "%d", sess.p->line_offset*25); //25 - complete hack but works ok
        sess.publisher.send(message, zmq::send_flags::dontwait);
      }

    } else if (org.mode != FILE_DISPLAY) { 
      outlineProcessKeypress();
      org.outlineScroll();
      sess.refreshOrgScreen();
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode

    if (sess.divider > 10) sess.drawOrgStatusBar();
    sess.returnCursor();
  }

  sess.quitApp();

  return 0;
}
