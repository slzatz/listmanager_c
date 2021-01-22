//#include "listmanager.h"
#include "listman.h"
//#include "Organizer.h" //in listman.h
#include "Common.h"
#include "session.h"
#include <string_view>
#include <zmq.hpp>
#include <algorithm>
#include <ranges>
#include "outline_commandline_functions.h"
#include "outline_normal_functions.h"
#include "editor_function_map.h"
#include <filesystem>
#include "inipp.h" // https://github.com/mcmtroffaes/inipp

#define TOP_MARGIN 1

//using namespace redi;
//using json = nlohmann::json;

//auto logger = spdlog::basic_logger_mt("lm_logger", "lm_log"); //logger name, file name

zmq::context_t context(1);
zmq::socket_t publisher(context, ZMQ_PUB);

bool run = true; //main while loop

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

void do_exit(PGconn *conn) {
    PQfinish(conn);
    exit(1);
}

config c;
void parse_ini_file(std::string ini_name)
{
  inipp::Ini<char> ini;
  std::ifstream is(ini_name);
  ini.parse(is);
  inipp::extract(ini.sections["ini"]["user"], c.user);
  inipp::extract(ini.sections["ini"]["password"], c.password);
  inipp::extract(ini.sections["ini"]["dbname"], c.dbname);
  inipp::extract(ini.sections["ini"]["hostaddr"], c.hostaddr);
  inipp::extract(ini.sections["ini"]["port"], c.port);
  inipp::extract(ini.sections["editor"]["ed_pct"], c.ed_pct);
}

void signalHandler(int signum) {
  sess.getWindowSize();
  //that percentage should be in session
  // so right now this reverts back if it was changed during session
  sess.moveDivider(c.ed_pct);
}

//pg ini stuff
void get_conn(void) {
  char conninfo[250];
  parse_ini_file(DB_INI);
  
  sprintf(conninfo, "user=%s password=%s dbname=%s hostaddr=%s port=%d", 
          c.user.c_str(), c.password.c_str(), c.dbname.c_str(), c.hostaddr.c_str(), c.port);

  conn = PQconnectdb(conninfo);

  if (PQstatus(conn) != CONNECTION_OK){
    if (PQstatus(conn) == CONNECTION_BAD) {
      fprintf(stderr, "Connection to database failed: %s\n",
      PQerrorMessage(conn));
      do_exit(conn);
    }
  } 
}

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

/**** Outline COMMAND mode functions ****/
void F_open(int pos) { //C_open - by context
  std::string_view cl = org.command_line;
  if (pos) {
    bool success = false;
    //structured bindings
    for (const auto & [k,v] : org.context_map) {
      if (k.rfind(cl.substr(pos + 1), 0) == 0) {
      //if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        org.context = k;
        success = true;
        break;
      }
    }

    if (!success) {
      sess.showOrgMessage2(fmt::format("{} is not a valid  context!", cl.substr(pos + 1)));
      org.mode = NORMAL;
      return;
    }

  } else {
    sess.showOrgMessage2("You did not provide a context!");
    org.mode = NORMAL;
    return;
  }
  sess.showOrgMessage3("'{}' will be opened, Steve", org.context.c_str());
  sess.command_history.push_back(org.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, org.command_line);

  org.marked_entries.clear();
  org.folder = "";
  org.taskview = BY_CONTEXT;
  getItems(MAX);
  org.mode = NORMAL;
  return;
}

void F_openfolder(int pos) {
  std::string_view cl = org.command_line;
  if (pos) {
    bool success = false;
    for (const auto & [k,v] : org.folder_map) {
      if (k.rfind(cl.substr(pos + 1), 0) == 0) {
      //if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        org.folder = k;
        success = true;
        break;
      }
    }
    if (!success) {
      sess.showOrgMessage2(fmt::format("{} is not a valid  folder!", cl.substr(pos + 1)));
      org.mode = NORMAL;
      return;
    }

  } else {
    sess.showOrgMessage("You did not provide a folder!");
    org.mode = NORMAL;
    return;
  }
  sess.showOrgMessage("\'%s\' will be opened", org.folder.c_str());
  sess.command_history.push_back(org.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, org.command_line);
  org.marked_entries.clear();
  org.context = "";
  org.taskview = BY_FOLDER;
  getItems(MAX);
  org.mode = NORMAL;
  return;
}

void F_openkeyword(int pos) {
  if (!pos) {
    sess.showOrgMessage("You need to provide a keyword");
    org.mode = NORMAL;
    return;
  }
 
  //O.keyword = O.command_line.substr(pos+1);
  std::string keyword = org.command_line.substr(pos+1);
  if (keywordExists(keyword) == -1) {
    org.mode = org.last_mode;
    sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
    return;
  }

  org.keyword = keyword;  
  sess.showOrgMessage("\'%s\' will be opened", org.keyword.c_str());
  sess.command_history.push_back(org.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, org.command_line);
  org.marked_entries.clear();
  org.context = "";
  org.folder = "";
  org.taskview = BY_KEYWORD;
  getItems(MAX);
  org.mode = NORMAL;
  return;
}

void F_addkeyword(int pos) {
  if (!pos) {
    org.current_task_id = org.rows.at(org.fr).id;
    sess.eraseRightScreen();
    org.view = KEYWORD;
    sess.command_history.push_back(org.command_line);
    getContainers(); //O.mode = NORMAL is in get_containers
    if (org.mode != NO_ROWS) {
      //Container c = getContainerInfo(org.rows.at(org.fr).id);
      //sess.displayContainerInfo(c);
      org.mode = ADD_CHANGE_FILTER;
      sess.showOrgMessage("Select keyword to add to marked or current entry!");
    }
    return;
  }

  // only do this if there was text after C_addkeyword
  if (org.last_mode == NO_ROWS) return;

  //{
  std::string keyword = org.command_line.substr(pos+1);
  if (keywordExists(keyword) == -1) {
    org.mode = org.last_mode;
    sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
    return;
  }

  if (org.marked_entries.empty()) {
    addTaskKeyword(keyword, org.rows.at(org.fr).id);
    sess.showOrgMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : org.marked_entries) {
      addTaskKeyword(keyword, id);
    }
    sess.showOrgMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  //}
  org.mode = org.last_mode;
  return;
}

void F_keywords(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    org.view = KEYWORD;
    sess.command_history.push_back(org.command_line); 
    getContainers(); //O.mode = NORMAL is in get_containers
    if (org.mode != NO_ROWS) {
      // two lines below show first folder's info
      Container c = getContainerInfo(org.rows.at(org.fr).id);
      sess.displayContainerInfo(c);
      org.mode = NORMAL;
      sess.showOrgMessage("Retrieved keywords");
    }
    return;
  }  

  // only do this if there was text after C_keywords
  if (org.last_mode == NO_ROWS) return;

  {
  std::string keyword = org.command_line.substr(pos+1);
  if (keywordExists(keyword) == -1) {
      org.mode = org.last_mode;
      sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
      return;
  }

  if (org.marked_entries.empty()) {
    //add_task_keyword(keyword, org.rows.at(org.fr).id);
    addTaskKeyword(keyword, org.rows.at(org.fr).id);
    sess.showOrgMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : org.marked_entries) {
      //add_task_keyword(keyword, id);
      addTaskKeyword(keyword, id);
    }
    sess.showOrgMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  }
  org.mode = org.last_mode;
  return;
}

void F_write(int) {
  if (org.view == TASK) updateRows();
  org.mode = org.last_mode;
  org.command_line.clear();
}

void F_x(int) {
  if (org.view == TASK) updateRows();
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //sends cursor home (upper left)
  exit(0);
}

void F_refresh(int) {
  if (org.view == TASK) {
    sess.showOrgMessage("Entries will be refreshed");
    if (org.taskview == BY_FIND)
      searchDB(sess.fts_search_terms);
    else
      getItems(MAX);
  } else {
    sess.showOrgMessage("contexts/folders will be refreshed");
    getContainers();
    if (org.mode != NO_ROWS) {
      Container c = getContainerInfo(org.rows.at(org.fr).id);
      sess.displayContainerInfo(c);
    }
  }
  org.mode = org.last_mode;
}

void F_new(int) {
  org.outlineInsertRow(0, "", true, false, false, now());
  org.fc = org.fr = org.rowoff = 0;
  org.command[0] = '\0';
  org.repeat = 0;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
  sess.eraseRightScreen(); //erases the note area
  org.mode = INSERT;

  int fd;
  std::string fn = "assets/" + CURRENT_NOTE_FILE;
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    sess.lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &sess.lock) != -1) {
    write(fd, " ", 1);
    sess.lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &sess.lock);
    } else sess.showOrgMessage("Couldn't lock file");
  } else sess.showOrgMessage("Couldn't open file");
}

//this is the main event - right now only way to initiate editing an entry
void F_edit(int id) {
  
  if (!(org.view == TASK)) {
    org.command[0] = '\0';
    org.mode = NORMAL;
    sess.showOrgMessage("Only tasks have notes to edit!");
    return;
  }

  //pos is zero if no space and command modifier
  if (id == 0) id = getId();
  if (id == -1) {
    sess.showOrgMessage("You need to save item before you can create a note");
    org.command[0] = '\0';
    org.mode = NORMAL;
    return;
  }

  sess.showOrgMessage("Edit note %d", id);
  //org.outlineRefreshScreen();
  sess.refreshOrgScreen();
  sess.editor_mode = true;

  if (!sess.editors.empty()){
    auto it = std::find_if(std::begin(sess.editors), std::end(sess.editors),
                       [&id](auto& ls) { return ls->id == id; }); //auto&& also works

    if (it == sess.editors.end()) {
      sess.p = new Editor;
      Editor* & p = sess.p;
      sess.editors.push_back(p);
      sess.p->id = id;
      sess.p->top_margin = TOP_MARGIN + 1;

      int folder_tid = getFolderTid(org.rows.at(org.fr).id);
      if (folder_tid == 18 || folder_tid == 14) {
        sess.p->linked_editor = new Editor;
        Editor * & p = sess.p;
        sess.editors.push_back(p->linked_editor);
        p->linked_editor->id = id;
        p->linked_editor->is_subeditor = true;
        p->linked_editor->is_below = true;
        p->linked_editor->linked_editor = p;
        p->left_margin_offset = LEFT_MARGIN_OFFSET;
      } 
      readNoteIntoEditor(id); //if id == -1 does not try to retrieve note
      
    } else {
      sess.p = *it;
    }    
  } else {
    sess.p = new Editor;
    Editor* & p = sess.p;
    sess.editors.push_back(p);
    p->id = id;
    p->top_margin = TOP_MARGIN + 1;

    int folder_tid = getFolderTid(org.rows.at(org.fr).id);
    if (folder_tid == 18 || folder_tid == 14) {
      sess.p->linked_editor = new Editor;
      Editor * & p = sess.p;
      sess.editors.push_back(p->linked_editor);
      p->linked_editor->id = id;
      p->linked_editor->is_subeditor = true;
      p->linked_editor->is_below = true;
      p->linked_editor->linked_editor = p;
      p->left_margin_offset = LEFT_MARGIN_OFFSET;
    }
    readNoteIntoEditor(id); //if id == -1 does not try to retrieve note
 }
  sess.positionEditors();
  sess.eraseRightScreen(); //erases editor area + statusbar + msg
  sess.drawEditors();

  if (sess.p->rows.empty()) {
    Editor* & p = sess.p;
    // note editorInsertChar inserts the row
    p->mode = INSERT;
    // below all for undo
    p->last_command = "i";
    p->prev_fr = 0;
    p->prev_fc = 0;
    p->last_repeat = 1;
    p->snapshot.push_back("");
    p->editorSetMessage("\x1b[1m-- INSERT --\x1b[0m");
  } else {
    sess.p->mode = NORMAL;
  }

  org.command[0] = '\0';
  org.mode = NORMAL;
}

void F_contexts(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    org.view = CONTEXT;
    sess.command_history.push_back(org.command_line); 
    getContainers();
    if (org.mode != NO_ROWS) {
      // two lines below show first context's info
      Container c = getContainerInfo(org.rows.at(org.fr).id);
      sess.displayContainerInfo(c);
      org.mode = NORMAL;
      sess.showOrgMessage("Retrieved contexts");
    }
    return;
  } else {

    std::string new_context;
    bool success = false;
    if (org.command_line.size() > 5) { //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : org.context_map) {
        if (strncmp(&org.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_context = k;
          success = true;
          break;
        }
      }
      if (!success) {
        sess.showOrgMessage("What you typed did not match any context");
        org.mode = NORMAL;
        return;
      }

    } else {
      sess.showOrgMessage("You need to provide at least 3 characters "
                        "that match a context!");

      org.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : org.rows) {
      if (it.mark) {
        //org.update_task_context(new_context, it.id);
        updateTaskContext(new_context, it.id);
        success = true;
      }
    }

    if (success) {
      sess.showOrgMessage("Marked tasks moved into context %s", new_context.c_str());
    } else {
      //org.update_task_context(new_context, org.rows.at(org.fr).id);
      updateTaskContext(new_context, org.rows.at(org.fr).id);
      sess.showOrgMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
    }
    org.mode = org.last_mode;
    return;
  }
}

void F_folders(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    org.view = FOLDER;
    sess.command_history.push_back(org.command_line); 
    getContainers();
    if (org.mode != NO_ROWS) {
      // two lines below show first folder's info
      Container c = getContainerInfo(org.rows.at(org.fr).id);
      sess.displayContainerInfo(c);
      org.mode = NORMAL;
      sess.showOrgMessage("Retrieved folders");
    }
    return;
  } else {

    std::string new_folder;
    bool success = false;
    if (org.command_line.size() > 5) {  //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : org.folder_map) {
        if (strncmp(&org.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_folder = k;
          success = true;
          break;
        }
      }
      if (!success) {
        sess.showOrgMessage("What you typed did not match any folder");
        org.mode = NORMAL;
        return;
      }

    } else {
      sess.showOrgMessage("You need to provide at least 3 characters "
                        "that match a folder!");

      org.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : org.rows) {
      if (it.mark) {
        //org.update_task_folder(new_folder, it.id);
        updateTaskFolder(new_folder, it.id);
        success = true;
      }
    }

    if (success) {
      sess.showOrgMessage("Marked tasks moved into folder %s", new_folder.c_str());
    } else {
      //org.update_task_folder(new_folder, org.rows.at(org.fr).id);
      updateTaskFolder(new_folder, org.rows.at(org.fr).id);
      sess.showOrgMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
    }
    org.mode = org.last_mode;
    return;
  }
}

void F_recent(int) {
  sess.showOrgMessage("Will retrieve recent items");
  sess.command_history.push_back(org.command_line);
  sess.page_history.push_back(org.command_line);
  sess.page_hx_idx = sess.page_history.size() - 1;
  org.marked_entries.clear();
  org.context = "No Context";
  org.taskview = BY_RECENT;
  org.folder = "No Folder";
  getItems(MAX);
}


void F_createLink(int) {
  if (sess.editors.empty()) {
    sess.showOrgMessage("There are no entries being edited");
    org.mode = NORMAL;
    return;
  }
  std::unordered_set<int> temp;
  for (const auto z : sess.editors) {
    temp.insert(z->id);
  }

  if (temp.size() != 2) {
    sess.showOrgMessage("At the moment you can only link two entries at a time");
    org.mode = NORMAL;
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
    sess.showOrgMessage3("Problem in 'createLink': {}", error);
    org.mode = NORMAL;
    return;
  }

   Query q1(sess.db,"UPDATE link SET modified = datetime('now') WHERE task_id0={} AND task_id1={};", task_ids[0], task_ids[1]);
   q1.step();

   org.mode = NORMAL;
}

void F_getLinked(int) {
  if (!sess.p) return;

  int id = sess.p->id;

  Query q(sess.db, "SELECT task_id0, task_id1 FROM link WHERE task_id0={} OR task_id1={}", id, id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving linked item: {}", res);
    return;
  }
  int task_id0 = q.column_int(0);
  int task_id1 = q.column_int(1);

  id = (task_id0 == id) ? task_id1 : task_id0;
  F_edit(id);
}

void F_find(int pos) {
  if (org.command_line.size() < 6) {
    sess.showOrgMessage("You need more characters");
    return;
  }  
  org.context = "";
  org.folder = "";
  org.taskview = BY_FIND;
  std::string st = org.command_line.substr(pos+1);
  std::transform(st.begin(), st.end(), st.begin(), ::tolower);
  sess.command_history.push_back(org.command_line); 
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, org.command_line);
  sess.showOrgMessage("Searching for %s", st.c_str());
  sess.fts_search_terms = st;
  searchDB(st);
}

void F_copy_entry(int) {
  copyEntry();
}

void F_deletekeywords(int) {
  deleteKeywords(getId());
  sess.showOrgMessage("Keyword(s) for task %d will be deleted and fts searchdb updated", org.rows.at(org.fr).id);
  org.mode = org.last_mode;
}

void F_sync(int) {
  synchronize(0); // do actual sync
  generateContextMap();
  generateFolderMap();
  sess.initial_file_row = 0; //for arrowing or displaying files
  org.mode = FILE_DISPLAY; // needs to appear before displayFile
  sess.showOrgMessage("Synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_sync_test(int) {
  synchronize(1); //1 -> report_only
  sess.initial_file_row = 0; //for arrowing or displaying files
  org.mode = FILE_DISPLAY; // needs to appear before displayFile
  sess.showOrgMessage("Testing synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_updatecontext(int) {
  org.current_task_id = org.rows.at(org.fr).id;
  sess.eraseRightScreen();
  org.view = CONTEXT;
  sess.command_history.push_back(org.command_line); 
  getContainers(); //O.mode = NORMAL is in get_containers
  if (org.mode != NO_ROWS) {
    //Container c = getContainerInfo(org.rows.at(org.fr).id);
    //sess.displayContainerInfo(c);
    org.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
    sess.showOrgMessage("Select context to add to marked or current entry");
  }
}

void F_updatefolder(int) {
  org.current_task_id = org.rows.at(org.fr).id;
  sess.eraseRightScreen();
  org.view = FOLDER;
  sess.command_history.push_back(org.command_line); 
  getContainers(); //O.mode = NORMAL is in get_containers
  if (org.mode != NO_ROWS) {
    //Container c = getContainerInfo(org.rows.at(org.fr).id);
    //sess.displayContainerInfo(c);
    org.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
    sess.showOrgMessage("Select folder to add to marked or current entry");
  }
}

void F_delmarks(int) {
  for (auto& it : org.rows) {
    it.mark = false;}
  if (org.view == TASK) org.marked_entries.clear(); //why the if??
  org.mode = org.last_mode;
  sess.showOrgMessage("Marks all deleted");
}

// to avoid confusion should only be an editor command line function
void F_savefile(int pos) {
  sess.command_history.push_back(org.command_line);
  std::string filename;
  if (pos) filename = org.command_line.substr(pos+1);
  else filename = "example.cpp";
  sess.p->editorSaveNoteToFile(filename);
  sess.showOrgMessage("Note saved to file: %s", filename.c_str());
  org.mode = NORMAL;
}

//this really needs work - needs to be picked like keywords, folders etc.
void F_sort(int pos) { 
  if (pos && org.view == TASK && org.taskview != BY_FIND) {
    org.sort = org.command_line.substr(pos + 1);
    getItems(MAX);
    sess.showOrgMessage("sorted by \'%s\'", org.sort.c_str());
  } else {
    sess.showOrgMessage("Currently can't sort search, which is sorted on best match");
  }
}

void  F_showall(int) {
  if (org.view == TASK) {
    org.show_deleted = !org.show_deleted;
    org.show_completed = !org.show_completed;
    if (org.taskview == BY_FIND)
      ; //search_db();
    else
      getItems(MAX);
  }
  sess.showOrgMessage((org.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
}

// does not seem to work
void F_syntax(int pos) {
  if (pos) {
    std::string action = org.command_line.substr(pos + 1);
    if (action == "on") {
      sess.p->highlight_syntax = true;
      sess.showOrgMessage("Syntax highlighting will be turned on");
    } else if (action == "off") {
      sess.p->highlight_syntax = false;
      sess.showOrgMessage("Syntax highlighting will be turned off");
    } else {sess.showOrgMessage("The syntax is 'sh on' or 'sh off'"); }
  } else {sess.showOrgMessage("The syntax is 'sh on' or 'sh off'");}
  sess.p->editorRefreshScreen(true);
  org.mode = NORMAL;
}

// set spell | set nospell
// should also only be an editor function
void F_set(int pos) {
  std::string action = org.command_line.substr(pos + 1);
  if (pos) {
    if (action == "spell") {
      sess.p->spellcheck = true;
      sess.showOrgMessage("Spellcheck active");
    } else if (action == "nospell") {
      sess.p->spellcheck = false;
      sess.showOrgMessage("Spellcheck off");
    } else {sess.showOrgMessage("Unknown option: %s", action.c_str()); }
  } else {sess.showOrgMessage("Unknown option: %s", action.c_str());}
  sess.p->editorRefreshScreen(true);
  org.mode = NORMAL;
}

// also should be only editor function
void F_open_in_vim(int) {
  openInVim(); //send you into editor mode
  sess.p->mode = NORMAL;
  //O.command[0] = '\0';
  //O.repeat = 0;
  //O.mode = NORMAL;
}

void F_join(int pos) {
  if (org.view != TASK || org.taskview == BY_JOIN || pos == 0) {
    sess.showOrgMessage("You are either in a view where you can't join or provided no join container");
    org.mode = NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    org.mode = org.last_mode; //NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    return;
  }
  bool success = false;

  if (org.taskview == BY_CONTEXT) {
    for (const auto & [k,v] : org.folder_map) {
      if (strncmp(&org.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        org.folder = k;
        success = true;
        break;
      }
    }
  } else if (org.taskview == BY_FOLDER) {
    for (const auto & [k,v] : org.context_map) {
      if (strncmp(&org.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        org.context = k;
        success = true;
        break;
      }
    }
  }
  if (!success) {
    sess.showOrgMessage("You did not provide a valid folder or context to join!");
    org.command_line.resize(1);
    return;
  }

  sess.showOrgMessage("Will join \'%s\' with \'%s\'", org.folder.c_str(), org.context.c_str());
  org.taskview = BY_JOIN;
  getItems(MAX);
  return;
}

void F_saveoutline(int pos) { 
  if (pos) {
    std::string fname = org.command_line.substr(pos + 1);
    org.outlineSave(fname);
    org.mode = NORMAL;
    sess.showOrgMessage("Saved outline to %s", fname.c_str());
  } else {
    sess.showOrgMessage("You didn't provide a file name!");
  }
}

void F_valgrind(int) {
  sess.initial_file_row = 0; //for arrowing or displaying files
  readFile("valgrind_log_file");
  displayFile();//put them in the command mode case synch
  org.last_mode = org.mode;
  org.mode = FILE_DISPLAY;
}

void F_help(int pos) {
  if (!pos) {             
    /*This needs to be changed to show database text not ext file*/
    sess.initial_file_row = 0;
    org.last_mode = org.mode;
    org.mode = FILE_DISPLAY;
    sess.showOrgMessage("Displaying help file");
    readFile("listmanager_commands");
    displayFile();
  } else {
    std::string st = org.command_line.substr(pos+1);
    org.context = "";
    org.folder = "";
    org.taskview = BY_FIND;
    std::transform(st.begin(), st.end(), st.begin(), ::tolower);
    sess.command_history.push_back(org.command_line); 
    sess.fts_search_terms = st;
    searchDB(st, true);
    sess.showOrgMessage("Will look for help on %s", st.c_str());
    //O.mode = NORMAL;
  }  
}

//case 'q':
void F_quit_app(int) {
  bool unsaved_changes = false;
  for (auto it : org.rows) {
    if (it.dirty) {
      unsaved_changes = true;
      break;
    }
  }
  if (unsaved_changes) {
    org.mode = NORMAL;
    sess.showOrgMessage("No db write since last change");
  } else {
    run = false;

    /* need to figure out if need any of the below
    context.close();
    subscriber.close();
    publisher.close();
    subs_thread.join();
    exit(0);
    */
  }
}

void F_quit_app_ex(int) {
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
  Py_FinalizeEx();
  //sqlite3_close(S.db); //something should probably be done here
  PQfinish(conn);
  //t0.join();
  //subscriber.close();
  context.close();
  publisher.close();
  exit(0);
}

void F_lsp_start(int pos) {

  std::string name = org.command_line;
  if (pos) name = name.substr(pos + 1);
  else {
    sess.showOrgMessage("Which lsp do you want?");
    return;
  }

  lspStart(name);
  org.mode = NORMAL;
}

void F_launch_lm_browser(int) {
  if (sess.lm_browser) {
    sess.showOrgMessage3("There is already an active browser");
    return;
  }
  sess.lm_browser = true; 
  std::system("./lm_browser current.html &"); //&=> returns control
  org.mode = NORMAL;
}

void F_quit_lm_browser(int) {
  zmq::message_t message(20);
  snprintf ((char *) message.data(), 20, "%s", "quit"); //25 - complete hack but works ok
  publisher.send(message, zmq::send_flags::dontwait);
  sess.lm_browser = false;
  org.mode = NORMAL;
}
/* END OUTLINE COMMAND mode functions */

void F_resize(int pos) {
  std::string s = org.command_line;
  if (pos) {
    //s = O.command_line.substr(pos + 1);
    size_t p = s.find_first_of("0123456789");
    if (p != pos + 1) {
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      org.mode = NORMAL;
      return;
    }
    int pct = stoi(s.substr(p));
    if (pct > 90 || pct < 10) { 
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      org.mode = NORMAL;
      return;
    }
    c.ed_pct = pct;
  } else {
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      org.mode = NORMAL;
      return;
  }
  org.mode = NORMAL;
  signalHandler(0);
}

/* OUTLINE NORMAL mode functions */
/*
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

void return_N(void) {
  orow& row = org.rows.at(org.fr);

  if(row.dirty){
    if (org.view == TASK) {
      updateTitle();
      if (sess.lm_browser) {
        int folder_tid = getFolderTid(org.rows.at(org.fr).id);
        if (!(folder_tid == 18 || folder_tid == 14)) sess.updateHTMLFile("assets/" + CURRENT_NOTE_FILE);
      }
    } else if (org.view == CONTEXT || org.view == FOLDER) updateContainer();
    else if (org.view == KEYWORD) updateKeyword();
    org.command[0] = '\0'; //11-26-2019
    org.mode = NORMAL;
    if (org.fc > 0) org.fc--;
    return;
    //sess.showOrgMessage("");
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

//case 'i':
void insert_N(void){
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//se 's':
void s_N(void){
  orow& row = org.rows.at(org.fr);
  row.title.erase(org.fc, org.repeat);
  row.dirty = true;
  org.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
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
*/
void navigate_page_hx(int direction) {
  if (sess.page_history.size() == 1 && org.view == TASK) return;

  if (direction == PAGE_UP) {

    // if O.view!=TASK and PAGE_UP - moves back to last page
    if (org.view == TASK) { //if in a container viewa - fall through to previous TASK view page

      if (sess.page_hx_idx == 0) sess.page_hx_idx = sess.page_history.size() - 1;
      else sess.page_hx_idx--;
    }

  } else {
    if (sess.page_hx_idx == (sess.page_history.size() - 1)) sess.page_hx_idx = 0;
    else sess.page_hx_idx++;
  }

  /* go into COMMAND_LINE mode */
  org.mode = COMMAND_LINE;
  org.command_line = sess.page_history.at(sess.page_hx_idx);
  outlineProcessKeypress('\r');
  org.command_line.clear();

  /* return to NORMAL mode */
  org.mode = NORMAL;
  sess.page_history.erase(sess.page_history.begin() + sess.page_hx_idx);
  sess.page_hx_idx--;
  sess.showOrgMessage(":%s", sess.page_history.at(sess.page_hx_idx).c_str());
}

void navigate_cmd_hx(int direction) {
  if (sess.command_history.empty()) return;

  if (direction == ARROW_UP) {
    if (sess.cmd_hx_idx == 0) sess.cmd_hx_idx = sess.command_history.size() - 1;
    else sess.cmd_hx_idx--;
  } else {
    if (sess.cmd_hx_idx == (sess.command_history.size() - 1)) sess.cmd_hx_idx = 0;
    else sess.cmd_hx_idx++;
  }
  sess.showOrgMessage(":%s", sess.command_history.at(sess.cmd_hx_idx).c_str());
  org.command_line = sess.command_history.at(sess.cmd_hx_idx);
}

/*** outline operations ***/

// currently used for sync log
void readFile(const std::string &filename) {

  std::ifstream f(filename);
  std::string line;

  sess.display_text.str(std::string());
  sess.display_text.clear();

  while (getline(f, line)) {
    sess.display_text << line << '\n';
  }
  f.close();
}

void displayFile(void) {

  std::string ab;

  ab.append("\x1b[?25l", 6); //hides the cursor

  char lf_ret[20];
  int lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider); //note no + 1

  char buf[20];
  //position cursor prior to erase
  int bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 1);
  ab.append(buf, bufchars); //don't need to give length but will if change to memory_buffer

  //erase the right half of the screen
  for (int i=0; i < sess.textlines; i++) {
    ab.append("\x1b[K", 3);
    ab.append(lf_ret, lf_chars);
  }

  bufchars = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", TOP_MARGIN + 1, sess.divider + 2);
  ab.append(buf, bufchars);

  ab.append("\x1b[36m", 5); //this is foreground cyan - we'll see

  std::string row;
  std::string line;
  int row_num = -1;
  int line_num = 0;
  sess.display_text.clear();
  sess.display_text.seekg(0, std::ios::beg);
  while(std::getline(sess.display_text, row, '\n')) {
    if (line_num > sess.textlines - 2) break;
    row_num++;
    if (row_num < sess.initial_file_row) continue;
    if (static_cast<int>(row.size()) < sess.totaleditorcols) {
      ab.append(row);
      ab.append(lf_ret);
      line_num++;
      continue;
    }
    //int n = 0;
    lf_chars = snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 2); //indent text extra space
    int n = row.size()/(sess.totaleditorcols - 1) + ((row.size()%(sess.totaleditorcols - 1)) ? 1 : 0);
    for(int i=0; i<n; i++) {
      line_num++;
      if (line_num > sess.textlines - 2) break;
      line = row.substr(0, sess.totaleditorcols - 1);
      row.erase(0, sess.totaleditorcols - 1);
      ab.append(line);
      ab.append(lf_ret, lf_chars);
    }
  }
  ab.append("\x1b[0m", 4);
  write(STDOUT_FILENO, ab.c_str(), ab.size()); //01012020
}

// should also just be editor command
void open_in_vim(void){
  std::string filename;
  if (getFolderTid(org.rows.at(org.fr).id) != 18) filename = "vim_file.txt";
  else filename = "vim_file.cpp";
  sess.p->editorSaveNoteToFile(filename);
  std::stringstream s;
  s << "vim " << filename << " >/dev/tty";
  system(s.str().c_str());
  sess.p->editorReadFileIntoNote(filename);
}

// depends on readKey()
//void outlineProcessKeypress(void) {
void outlineProcessKeypress(int c) { //prototype has int = 0  

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  c = (!c) ? readKey() : c;
  switch (org.mode) {
  size_t n;
  //switch (int c = readKey(); O.mode)  //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          org.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          org.command_line.clear();
          sess.showOrgMessage(":");
          org.mode = COMMAND_LINE;
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

        case 'O': //Same as C_new in COMMAND_LINE mode
          org.outlineInsertRow(0, "", true, false, false, BASE_DATE);
          org.fc = org.fr = org.rowoff = 0;
          org.command[0] = '\0';
          org.repeat = 0;
          sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
          sess.eraseRightScreen(); //erases the note area
          org.mode = INSERT;
          return;
      }

      return; //in NO_ROWS - do nothing if no command match

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
          if (org.view == TASK)  {
            updateTitle();
            if (sess.lm_browser) {
              int folder_tid = getFolderTid(org.rows.at(org.fr).id);
              if (!(folder_tid == 18 || folder_tid == 14)) sess.updateHTMLFile("assets/" + CURRENT_NOTE_FILE);
            }
          } else if (org.view == CONTEXT || org.view == FOLDER) updateContainer();
          else if (org.view == KEYWORD) updateKeyword();
          org.command[0] = '\0'; //11-26-2019
          org.mode = NORMAL;
          if (org.fc > 0) org.fc--;
          //sess.showOrgMessage("");
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
        navigate_page_hx(c);
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
        navigate_cmd_hx(c);
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

        // could be generalized for folders and contexts too  
        // update_task_folder and update_task_context
        // update_task_context(std::string &, int)
        // maybe make update_task_context(int, int)
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

      displayFile();

      return;
  } //end of outer switch(O.mode) that contains additional switches for sub-modes like NORMAL, INSERT etc.
} //end outlineProcessKeypress

void synchronize(int report_only) { //using 1 or 0

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("synchronize"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "synchronize"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
          pValue = PyLong_FromLong(report_only);
          //pValue = Py_BuildValue("s", search_terms); // **************
          PyTuple_SetItem(pArgs, 0, pValue); // ***********
          pValue = PyObject_CallObject(pFunc, pArgs);
              if (!pValue) {
                  Py_DECREF(pArgs);
                  Py_DECREF(pModule);
                  sess.showOrgMessage("Problem converting c variable for use in calling python function");
          }
          Py_DECREF(pArgs);
          if (pValue != NULL) {
              //Py_ssize_t size; 
              //int len = PyList_Size(pValue);
              num = PyLong_AsLong(pValue);
              Py_DECREF(pValue); 
          } else {
              Py_DECREF(pFunc);
              Py_DECREF(pModule);
              PyErr_Print();
              sess.showOrgMessage("Received a NULL value from synchronize!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          sess.showOrgMessage("Was not able to find the function: synchronize!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      //PyErr_Print();
      sess.showOrgMessage("Was not able to find the module: synchronize!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}
  if (report_only) sess.showOrgMessage("Number of tasks/items that would be affected is %d", num);
  else sess.showOrgMessage("Number of tasks/items that were affected is %d", num);
}
/*
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
*/

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

          /*
        case CTRL_KEY('w'):  
          p->E_resize(1);
          p->command[0] = '\0';
          p->repeat = 0;
          return false;
         */

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
            sess.editor_mode = false;
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
            } else {sess.editor_mode = false;
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

      //if (std::string_view(p->command) == std::string({0x17,'='})) {
      //if (p->command == std::string({0x17,'='})) {
      if (sess.p->command == std::string_view("\x17" "=")) {
        sess.p->E_resize(0);
        sess.p->command[0] = '\0';
        sess.p->repeat = 0;
        return false;
      }

      //if (std::string_view(p->command) == std::string({0x17,'_'})) {
      //if (p->command == std::string({0x17,'_'})) {
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

/*** init ***/

void initOutline() {
  org.cx = 0; //cursor x position
  org.cy = 0; //cursor y position
  org.fc = 0; //file x position
  org.fr = 0; //file y position
  org.rowoff = 0;  //number of rows scrolled off the screen
  org.coloff = 0;  //col the user is currently scrolled to  
  org.sort = "modified";
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
  sess.divider = sess.screencols - c.ed_pct * sess.screencols/100;
  sess.totaleditorcols = sess.screencols - sess.divider - 1; // was 2 
}

int main(int argc, char** argv) { 

  initLogger();

  publisher.bind("tcp://*:5556");
  //publisher.bind("ipc://scroll.ipc"); //10132020 -> not sure why I thought I needed this

  sess.lock.l_whence = SEEK_SET;
  sess.lock.l_start = 0;
  sess.lock.l_len = 0;
  sess.lock.l_pid = getpid();

  if (argc > 1 && argv[1][0] == '-') sess.lm_browser = false;

  get_conn(); //for pg
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

  while (run) {
    // just refresh what has changed
    if (sess.editor_mode) {
      bool text_change = editorProcessKeypress(); 
      //
      // editorProcessKeypress can take you out of editor mode (either ctrl-H or closing last editor
      if (!sess.editor_mode) continue;
      //if (!p) continue; // commented out in favor of the above on 10-24-2020
      //
      bool scroll = sess.p->editorScroll();
      bool redraw = (text_change || scroll || sess.p->redraw); //instead of p->redraw => clear_highlights
      sess.p->editorRefreshScreen(redraw);

      ////////////////////
      if (sess.lm_browser && scroll) {
        zmq::message_t message(20);
        snprintf ((char *) message.data(), 20, "%d", sess.p->line_offset*25); //25 - complete hack but works ok
        publisher.send(message, zmq::send_flags::dontwait);
      }
      ////////////////////

    } else if (org.mode != FILE_DISPLAY) { 
      outlineProcessKeypress();
      org.outlineScroll();
      //org.outlineRefreshScreen(); // now just draws rows
      sess.refreshOrgScreen();
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode

    sess.drawOrgStatusBar();
    sess.returnCursor();
  }
  
  lsp_shutdown("all");


  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
  Py_FinalizeEx();
  //sqlite3_close(S.db); //something should probably be done here
  PQfinish(conn);

  return 0;
}
