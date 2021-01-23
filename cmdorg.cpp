#include "cmdorg.h"
#include "organizer.h"
#include "session.h"
#include <Python.h>
#include <zmq.hpp>

#define MAX 500
#define TOP_MARGIN 1
#define LEFT_MARGIN_OFFSET 4

void getItems(int);
int getId(void);
int keywordExists(const std::string &);
void getContainers(void);
void addTaskKeyword(std::string &kws, int id);
void updateRows(void);
void searchDB(const std::string & st, bool help=false);
int getFolderTid(int id);
void readNoteIntoEditor(int id); //if id == -1 does not try to retrieve note
void updateTaskContext(std::string &new_context, int id);
void updateTaskFolder(std::string &new_folder, int id);
void copyEntry(void);
void deleteKeywords(int id);
void synchronize(int);
void generateContextMap(void);
void generateFolderMap(void);
void readFile(const std::string &);
//void displayFile(void);
void openInVim(void);
void signalHandler(int signum);

std::string now(void);

void F_open(int pos) { //C_open - by context
  std::string_view cl = org.command_line;
  if (pos) {
    bool success = false;
    //structured bindings
    for (const auto & [k,v] : org.context_map) {
      if (k.rfind(cl.substr(pos + 1), 0) == 0) {
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
  sess.displayFile();//put them in the command mode case synch
}

void F_sync_test(int) {
  synchronize(1); //1 -> report_only
  sess.initial_file_row = 0; //for arrowing or displaying files
  org.mode = FILE_DISPLAY; // needs to appear before displayFile
  sess.showOrgMessage("Testing synching local db and server and displaying results");
  readFile("log");
  sess.displayFile();//put them in the command mode case synch
}

void F_updatecontext(int) {
  org.current_task_id = org.rows.at(org.fr).id;
  sess.eraseRightScreen();
  org.view = CONTEXT;
  sess.command_history.push_back(org.command_line); 
  getContainers(); //O.mode = NORMAL is in get_containers
  if (org.mode != NO_ROWS) {
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
  sess.displayFile();//put them in the command mode case synch
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
    sess.displayFile();
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
    sess.run = false;

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
  sess.run = false;
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
  sess.publisher.send(message, zmq::send_flags::dontwait);
  sess.lm_browser = false;
  org.mode = NORMAL;
}

void F_resize(int pos) {
  std::string s = org.command_line;
  if (pos) {
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
    sess.cfg.ed_pct = pct;
  } else {
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      org.mode = NORMAL;
      return;
  }
  org.mode = NORMAL;
  signalHandler(0);
}

