#include "listmanager.h"
#include "listmanager_vars.h"
#include "Organizer.h"
#include "Editor.h"
#include "Dbase.h"
#include "session.h"
#include "pstream.h"
#include <cstdarg> //va_start etc.
#include <mkdio.h>
#include <stop_token>
#include <string_view>
#include <zmq.hpp>
#include <thread>
#include <algorithm>
#include <ranges>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/cfg/env.h>

#include "outline_commandline_functions.h"
#include "outline_normal_functions.h"
#include "editor_function_map.h"

#include <filesystem>
#include <time.h>

using namespace redi;
using json = nlohmann::json;

// Below this instantiates it - with () it looks like a function definition with type Editor
//Session sess; //contains std::vector<Editor*> editors *inlined in listmanager.h*
//Editor *p;
//Session sess = {};
//std::shared_ptr<Editor> p(nullptr);

//typedef void (Editor::*efunc)(void);
//typedef void (Editor::*eefunc)(int);
//std::vector<Editor *> editors;
//Organizer O;

auto logger = spdlog::basic_logger_mt("lm_logger", "lm_log"); //logger name, file name

zmq::context_t context(1);
zmq::socket_t publisher(context, ZMQ_PUB);

bool run = true; //main while loop
void readsome(pstream &, int);
std::string title_search_string; //word under cursor works with *, n, N etc.

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
std::string prevfilename;
std::vector<std::string> completions;
std::string prefix;
int completion_index;

/******************lsp**********************************/

struct Lsp {
  std::jthread thred;
  std::string name{};
  std::string file_name{};
  std::string client_uri{};
  std::string language{};
  std::atomic<bool> code_changed = false;
  std::atomic<bool> closed = true;
};

// the vector that holds pointers to lsp structs
std::vector<Lsp *> lsp_v;

void readsome(pstream &ls, int i) {

  //for logging purposes
  std::size_t pos = ls.command().find(' ');
  std::string_view cmd = ls.command();
  cmd = cmd.substr(0, pos);
  char buf[8192]={}; //char buf[1024]{};
  std::streamsize n;
  std::string s{}; //used for body of server message
  std::string h{}; //used to log header of server message

  while ((n = ls.out().readsome(buf, sizeof(buf))) > 0) {
    // n will always be zero eventually
     s += std::string{buf, static_cast<size_t>(n)};
  }

  if (s.empty()) return;
  if (s.size() > 8000) {
    logger->warn("Message {} returned is greater than 8000 chars!", cmd);
    return;
  }

  //logger->info("Entering readsome's for loop: {}", i);
  //There may be more than one message to read
  int nn = 0;
  //logger->info("RECEIVED (RAW): {}", s);
  for (;;) {
    nn++;
    if (s.empty()) return;
    size_t pos = s.find("\r\n\r\n");
    if (pos == std::string::npos) return;
    h = s.substr(0, pos + 4); //second param is length
    pos = h.find(":");
    std::string length = h.substr(pos + 2, h.size()-4 -2 - pos); //length 2 awat from colon
    logger->info("Incoming messages - total length: {}", stoi(length));
    std::string ss = s.substr(h.size(), stoi(length));
    logger->info("read {} message {} nn {}:\n{}\n{}\n", cmd, i, nn, h, ss);

    json js;
    try {
      js = json::parse(ss);
    } catch(const json::parse_error &) {
      logger->info("PARSE ERROR!");
    return;
    }

    std::string sss;
    std::string hhh;
    if (js.contains("method")) {
      if (js["method"] == "textDocument/publishDiagnostics") {
        json diagnostics = js["params"]["diagnostics"];
        if (sess.p) sess.p->decorate_errors(diagnostics); //not sure need if (p)
      } else if (js["method"] == "workspace/configuration") {
        //s = R"({"jsonrpc": "2.0", "result": [{"caseSensitiveCompletion": true}, null], "id": 1})";
        //caseSensitiveCompletion is deprecated, use \"matcher\" instead
        sss = R"({"jsonrpc": "2.0", "result": [], "id": 1})";
        hhh = fmt::format("Content-Length: {}\r\n\r\n", sss.size());
        sss = hhh + sss;
        ls.write(sss.c_str(), sss.size()).flush();
        logger->info("sent workspace/configuration message to {}:\n{}\n", cmd, sss);
      } else if (js["method"] == "client/registerCapability") {
        int id = js["id"];
        sss = R"({"jsonrpc": "2.0", "result": {}, "id": 2})";
        js = json::parse(sss);
        js["id"] = id;
        sss = js.dump();
        hhh = fmt::format("Content-Length: {}\r\n\r\n", sss.size());
        sss = hhh + sss;
        ls.write(sss.c_str(), sss.size()).flush();
        logger->info("sent client/registerCapability message to {}:\n{}\n", cmd, sss);
      }
    }
    s = s.substr(h.size() + stoi(length));
  }
}

/****************************************************/
void lsp_thread(std::stop_token st) {
  Lsp *lsp = lsp_v.back();
  const pstreams::pmode mode = pstreams::pstdout|pstreams::pstdin;
  pstream lang_server;
  if (lsp->name == "clangd") lang_server = pstream("clangd --log=error", mode);
  else lang_server = pstream("gopls serve -rpc.trace -logfile /home/slzatz/gopls_log", mode);

  std::string s;
  json js;
  std::string header;
  int pid = ::getpid();

  s = R"({"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {"processId": 0, "rootPath": null, "rootUri": "file:///", "initializationOptions": null, "capabilities": {"offsetEncoding": ["utf-8"], "textDocument": {"codeAction": {"dynamicRegistration": true}, "codeLens": {"dynamicRegistration": true}, "colorProvider": {"dynamicRegistration": true}, "completion": {"completionItem": {"commitCharactersSupport": true, "documentationFormat": ["markdown", "plaintext"], "snippetSupport": true}, "completionItemKind": {"valueSet": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]}, "contextSupport": true, "dynamicRegistration": true}, "definition": {"dynamicRegistration": true}, "documentHighlight": {"dynamicRegistration": true}, "documentLink": {"dynamicRegistration": true}, "documentSymbol": {"dynamicRegistration": true, "symbolKind": {"valueSet": [1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26]}}, "formatting": {"dynamicRegistration": true}, "hover": {"contentFormat": ["markdown", "plaintext"], "dynamicRegistration": true}, "implementation": {"dynamicRegistration": true}, "onTypeFormatting": {"dynamicRegistration": true}, "publishDiagnostics": {"relatedInformation": true}, "rangeFormatting": {"dynamicRegistration": true}, "references": {"dynamicRegistration": true}, "rename": {"dynamicRegistration": true}, "signatureHelp": {"dynamicRegistration": true, "signatureInformation": {"documentationFormat": ["markdown", "plaintext"]}}, "synchronization": {"didSave": true, "dynamicRegistration": true, "willSave": true, "willSaveWaitUntil": true}, "typeDefinition": {"dynamicRegistration": true}}, "workspace": {"applyEdit": true, "configuration": true, "didChangeConfiguration": {"dynamicRegistration": true}, "didChangeWatchedFiles": {"dynamicRegistration": true}, "executeCommand": {"dynamicRegistration": true}, "symbol": {"dynamicRegistration": true, "symbolKind": {"valueSet": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26]}}, "workspaceEdit": {"documentChanges": true}, "workspaceFolders": true}}, "trace": "off", "workspaceFolders": [{"name": "listmanager", "uri": "file:///"}]}})";

  js = json::parse(s);
  js["params"]["processId"] = pid + 1;
  js["params"]["rootUri"] = lsp->client_uri;
  js["params"]["workspaceFolders"][0]["uri"] = lsp->client_uri;
  s = js.dump();

  header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
  s = header + s;
  lang_server.write(s.c_str(), s.size()).flush();
  logger->info("sent initialization message to {}:\n{}\n", lsp->name, s);


  //initialization from client produces a capabilities response
  //from the server which is read below
  readsome(lang_server, 1); //this could block
  
  //Client sends initialized response
  s = R"({"jsonrpc": "2.0", "method": "initialized", "params": {}})";
  header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
  s = header + s;
  lang_server.write(s.c_str(), s.size()).flush();
  logger->info("sent initialized message to {}:\n{}\n", lsp->name, s);
  
  //client sends didOpen notification
  s = R"({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "file:///home/slzatz/clangd_examples/test.cpp", "languageId": "cpp", "version": 1, "text": ""}}})";
  js = json::parse(s);
  js["params"]["textDocument"]["text"] = " "; //text ? if it escapes automatically
  js["params"]["textDocument"]["uri"] = lsp->client_uri + lsp->file_name; //text ? if it escapes automatically
  js["params"]["textDocument"]["languageId"] = lsp->language; //text ? if it escapes automatically
  s = js.dump();
  header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
  s = header + s;
  lang_server.write(s.c_str(), s.size()).flush();
  logger->info("sent didOpen message to {}:\n{}\n", lsp->name, s);
  
  readsome(lang_server, 2); //reads initial diagnostics
  
  //int j = 2;

  for (int i=3; i<6;i++) { 
    logger->info("Reading response from {} - {}:\n", lsp->name, i);
    readsome(lang_server, i);
  }
  int j = 5;
  
  s = R"({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {"textDocument": {"uri": "file:///", "version": 2}, "contentChanges": [{"text": ""}]}})";

  js = json::parse(s);
  
  while (!st.stop_requested()) {
    if (lsp->code_changed) {
      js["params"]["contentChanges"][0]["text"] = sess.p->code; //text ? if it escapes automatically
      js["params"]["textDocument"]["version"] = ++j; 
      js["params"]["textDocument"]["uri"] = lsp->client_uri + lsp->file_name; //text ? if it escapes automatically
      s = js.dump();
      header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
      s = header + s;
      lang_server.write(s.c_str(), s.size()).flush();
      logger->info("sent didChange message to {}:\n{}\n", lsp->name, s);
  
      //readsome(lang_server, j);
      lsp->code_changed = false;
    }
    readsome(lang_server, j);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  s = R"({"jsonrpc": "2.0", "id": 1, "method": "shutdown", "params": {}})";
  header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
  s = header + s;
  lang_server.write(s.c_str(), s.size()).flush();
  logger->info("sent shutdown to {}:\n\n", lsp->name);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  s = R"({"jsonrpc": "2.0", "method": "exit", "params": {}})";
  header = fmt::format("Content-Length: {}\r\n\r\n", s.size());
  s = header + s;
  lang_server.write(s.c_str(), s.size()).flush();
  logger->info("sent exit to {}:\n\n", lsp->name);

  lang_server.close();
  lsp->closed = true;
}
  
void lsp_start(Lsp * lsp) {
  /*
  if (!lsp.empty) {
    lsp.thred.request_stop();
    //if lsp_thread crashed then lsp_closed will never become true and
    //that's why there is also a timer below
    time_t time0 = time(nullptr);
    while (!lsp_closed) {
      if (difftime(time(nullptr), time0) > 3.0) break;
      continue;
    }
  }
  */

  lsp->closed = false;
  lsp_v.push_back(lsp);
  lsp->thred = std::jthread(lsp_thread);
}

void lsp_shutdown(const std::string s) {
  //if (lsp_map.empty()) return;
  if (lsp_v.empty()) return;

  // assuming all for the moment
  //for (auto & [k,lsp] : lsp_map) {
  for (auto & lsp : lsp_v) {
    lsp->thred.request_stop();
    time_t time0 = time(nullptr);
    while (!lsp->closed) {
      if (difftime(time(nullptr), time0) > 3.0) break;
      continue;
    }
    lsp->thred.join();
    delete lsp;
  //lsp.empty = true;
  //should delete lsp_map item
  //lsp_closed = true;//set to false when started
  }
}

void do_exit(PGconn *conn) {
    PQfinish(conn);
    exit(1);
}

void signalHandler(int signum) {
  sess.getWindowSize();
  //that percentage should be in session
  // so right now this reverts back if it was changed during session
  sess.moveDivider(c.ed_pct);
}

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

void load_meta(void) {
  std::ifstream f(META_FILE);
  std::string line;
  static std::stringstream text;

  while (getline(f, line)) {
    text << line << '\n';
  }
  sess.meta = text.str();
  f.close();
}

// typedef char * (*mkd_callback_t)(const char*, const int, void*);
// needed by markdown code in update_html_file
char * (url_callback)(const char *x, const int y, void *z) {
  link_id++;
  sprintf(link_text,"id=\"%d\"", link_id);
  return link_text;
}  

/* this version of update_html_file uses mkd_document
 * and only writes to the file once
 */
void update_html_file(std::string &&fn) {
  std::string note;
  if (sess.editor_mode) note = sess.p->editorRowsToString();
  else note = sess.O.outlinePreviewRowsToString();
  std::stringstream text;
  std::stringstream html;
  char *doc = nullptr;
  std::string title = sess.O.rows.at(sess.O.fr).title;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(sess.meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  //MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), MKD_FENCEDCODE);//11-16-2020
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, MKD_FENCEDCODE); //did something
  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";
  
  int fd;
  //if ((fd = open(fn.c_str(), O_RDWR|O_CREAT, 0666)) != -1) {
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    sess.lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &sess.lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    sess.lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &sess.lock);
    } else sess.showOrgMessage("Couldn't lock file");
  } else sess.showOrgMessage("Couldn't open file");

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 
  //
  mkd_cleanup(blob);
  link_id = 0;
}

/* this zeromq version works but there is a problem on the ultralight
 * side -- LoadHTML doesn't seem to use the style sheet.  Will check on slack
 * if this is my mistake or intentional
 * */
void update_html_zmq(std::string &&fn) {
  std::string note = sess.O.outlinePreviewRowsToString();
  std::stringstream text;
  std::stringstream html;
  std::string title = sess.O.rows.at(sess.O.fr).title;
  char *doc = nullptr;
  text << "# " << title << "\n\n" << note;

  // inserting title to tell if note displayed by ultralight 
  // has changed to know whether to preserve scroll
  std::string meta_(sess.meta);
  std::size_t p = meta_.find("</title>");
  meta_.insert(p, title);
  //
  //MKIOT blob(const char *text, int size, mkd_flag_t flags)
  MMIOT *blob = mkd_string(text.str().c_str(), text.str().length(), 0);
  mkd_e_flags(blob, url_callback);
  mkd_compile(blob, 0); 

  mkd_document(blob, &doc);
  html << meta_ << doc << "</article></body><html>";

  zmq::message_t message(html.str().size()+1);

  // probably don't need snprint to get html into message
  snprintf ((char *) message.data(), html.str().size()+1, "%s", html.str().c_str()); 

  publisher.send(message, zmq::send_flags::dontwait);

  /* don't know if below is correct or necessary - I don't think so*/
  //mkd_free_t x; 
  //mkd_e_free(blob, x); 

  mkd_cleanup(blob);
  link_id = 0;
}

// right now only called when previewing a code file
void update_html_code_file(std::string &&fn) {
  std::string note;
  std::ofstream myfile;
  note = sess.O.outlinePreviewRowsToString();
  myfile.open("code_file");
  myfile << note;
  myfile.close();

  std::stringstream html;
  std::string line;
  int tid = get_folder_tid(sess.O.rows.at(sess.O.fr).id);
  ipstream highlight(fmt::format("highlight code_file --out-format=html "
                             "--style=gruvbox-dark-hard-slz --syntax={}",
                             (tid == 18) ? "cpp" : "go"));

  while(getline(highlight, line)) { html << line << '\n';}
 
  int fd;
  if ((fd = open(fn.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666)) != -1) {
    sess.lock.l_type = F_WRLCK;  
    if (fcntl(fd, F_SETLK, &sess.lock) != -1) {
    write(fd, html.str().c_str(), html.str().size());
    sess.lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &sess.lock);
    } else sess.showOrgMessage("Couldn't lock file");
  } else sess.showOrgMessage("Couldn't open file");
}

// this is for local compilation and running
/* PROBLEM: if no lsp activated should still be able to update code file for compilation */
void update_code_file(void) {
  std::ofstream myfile;
  std::string file_path;
  std::string lsp_name;
  int tid = get_folder_tid(sess.p->id);

  //if (!lsp.empty) file_path = lsp.client_uri.substr(7) + lsp.file_name;
  if (tid == 18) {
    file_path  = "/home/slzatz/clangd_examples/test.cpp";
    lsp_name = "clangd";
  } else {
    file_path = "/home/slzatz/go/src/example/main.go";
    lsp_name = "gopls";
  }

  /*
  if (!lsp_v.empty()) {
    auto it = std::ranges::find_if(lsp_v, [&lsp_name](auto & lsp){return lsp->name == lsp_name;});
    if (it != lsp_v.end()) (*it)->code_changed = true;
  }
  */
  myfile.open(file_path); ///////////////////////////////////////////////////////
  myfile << sess.p->code;
  myfile.close();

  if (!lsp_v.empty()) {
    auto it = std::ranges::find_if(lsp_v, [&lsp_name](auto & lsp){return lsp->name == lsp_name;});
    if (it != lsp_v.end()) (*it)->code_changed = true;
  }
}

std::pair<std::string, std::vector<std::string>> get_task_keywords_pg(int tid) {

  std::stringstream query;
  query << "SELECT keyword.name "
           "FROM task_keyword LEFT OUTER JOIN keyword ON keyword.id = task_keyword.keyword_id "
           "WHERE " << tid << " =  task_keyword.task_id;";

  PGresult *res = PQexec(conn, query.str().c_str());

  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    sess.showOrgMessage("Problem in get_task_keywords_pg!");
    PQclear(res);
    return std::make_pair(std::string(), std::vector<std::string>());
  }

  int rows = PQntuples(res);
  std::vector<std::string> task_keywords = {};
  for(int i=0; i<rows; i++) {
    task_keywords.push_back(PQgetvalue(res, i, 0));
  }
   std::string delim = "";
   std::string s = "";
   for (const auto &kw : task_keywords) {
     s += delim += kw;
     delim = ",";
   }
  PQclear(res);
  return std::make_pair(s, task_keywords);
  // PQfinish(conn);
}

void display_item_info_pg(int id) {

  if (id ==-1) return;

  std::stringstream query;
  query << "SELECT * FROM task WHERE id = " << id;

  PGresult *res = PQexec(conn, query.str().c_str());
    
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    sess.showOrgMessage("Postgres Error: %s", PQerrorMessage(conn)); 
    PQclear(res);
    return;
  }    

  char lf_ret[10];
  snprintf(lf_ret, sizeof(lf_ret), "\r\n\x1b[%dC", sess.divider + 1);

  std::string s;

  //set background color to blue
  s.append("\n\n");
  s.append("\x1b[44m", 5);
  char str[300];

  sprintf(str,"\x1b[1mid:\x1b[0;44m %s", PQgetvalue(res, 0, 0));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mtitle:\x1b[0;44m %s", PQgetvalue(res, 0, 3));
  s.append(str);
  s.append(lf_ret);

  int context_tid = atoi(PQgetvalue(res, 0, 6));
  auto it = std::find_if(std::begin(sess.O.context_map), std::end(sess.O.context_map),
                         [&context_tid](auto& p) { return p.second == context_tid; }); //auto&& also works

  sprintf(str,"\x1b[1mcontext:\x1b[0;44m %s", it->first.c_str());
  s.append(str);
  s.append(lf_ret);

  int folder_tid = atoi(PQgetvalue(res, 0, 5));
  auto it2 = std::find_if(std::begin(sess.O.folder_map), std::end(sess.O.folder_map),
                         [&folder_tid](auto& p) { return p.second == folder_tid; }); //auto&& also works
  sprintf(str,"\x1b[1mfolder:\x1b[0;44m %s", it2->first.c_str());
  s.append(str);
  s.append(lf_ret);

  sprintf(str,"\x1b[1mstar:\x1b[0;44m %s", (*PQgetvalue(res, 0, 8) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mdeleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 14) == 't') ? "true" : "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mcompleted:\x1b[0;44m %s", (*PQgetvalue(res, 0, 10)) ? "true": "false");
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1mmodified:\x1b[0;44m %s", PQgetvalue(res, 0, 16));
  s.append(str);
  s.append(lf_ret);
  sprintf(str,"\x1b[1madded:\x1b[0;44m %s", PQgetvalue(res, 0, 9));
  s.append(str);
  s.append(lf_ret);

  std::string ss = get_task_keywords_pg(id).first;
  sprintf(str,"\x1b[1mkeywords:\x1b[0;44m %s", ss.c_str());
  s.append(str);
  s.append(lf_ret);

  //sprintf(str,"\x1b[1mtag:\x1b[0;44m %s", PQgetvalue(res, 0, 4));
  //s.append(str);
  //s.append(lf_ret);

  s.append("\x1b[0m");

  write(STDOUT_FILENO, s.c_str(), s.size());

  PQclear(res);
}
// end of pg functions

std::string now(void) {
  std::time_t t = std::time(nullptr);
  return fmt::format("{:%H:%M}", fmt::localtime(t));
}

std::string time_delta(std::string t) {
  struct std::tm tm = {};
  std::istringstream iss;
  iss.str(t);
  iss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
  auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));

  //auto now = std::chrono::utc_clock::now(); //unfortunately c++20 and not available yet
  auto now = std::chrono::system_clock::now(); //this needs to be utc
  std::chrono::duration<double> elapsed_seconds = now-tp; //in seconds but with stuff to right of decimal

  /* this didn't work as a kluge
  //from https://stackoverflow.com/questions/63501664/current-utc-time-point-in-c
  auto now =std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()); 
  std::chrono::duration<double> elapsed_seconds = now.time_since_epoch()-tp.time_since_epoch(); //in seconds but with stuff to right of decimal
  */

  auto int_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed_seconds);
  //int adj_secs = (int)int_secs.count() + 3600; //time zone adjustment of 1 hour works - no idea why!
  int adj_secs = (int)int_secs.count() + 18000; //kluge that requires tz adjustment; need utc_clock

  std::string s;

  if (adj_secs <= 120) s = fmt::format("{} seconds ago", adj_secs);
  else if (adj_secs <= 60*120) s = fmt::format("{} minutes ago", adj_secs/60); // <120 minutes we report minutes
  else if (adj_secs <= 48*60*60) s = fmt::format("{} hours ago", adj_secs/3600); // <48 hours report hours
  else if (adj_secs <= 24*60*60*60) s = fmt::format("{} days ago", adj_secs/3600/24); // <60 days report days
  else if (adj_secs <= 24*30*24*60*60) s = fmt::format("{} months ago", adj_secs/3600/24/30); // <24 months report months
  else s = fmt::format("{} years ago", adj_secs/3600/24/30/12);
 
  return s;
}

/********************Beginning sqlite************************************/

//Sqlite db(SQLITE_DB);
//Sqlite fts(FTS_DB);

void run_sql(void) {
  if (!sess.db.run()) {
    sess.showOrgMessage("SQL error: %s", sess.db.errmsg);
    return;
  }  
}

void db_open(void) {
  int rc = sqlite3_open(SQLITE_DB.c_str(), &S.db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.db);
    exit(1);
  }

  rc = sqlite3_open(FTS_DB.c_str(), &S.fts_db);
  if (rc != SQLITE_OK) {
    sqlite3_close(S.fts_db);
    exit(1);
  }
}

bool db_query(sqlite3 *db, std::string sql, sq_callback callback, void *pArg, char **errmsg) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     sess.showOrgMessage("SQL error: %s", errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

bool db_query(sqlite3 *db, const std::string& sql, sq_callback callback, void *pArg, char **errmsg, const char *func) {
   int rc = sqlite3_exec(db, sql.c_str(), callback, pArg, errmsg);
   if (rc != SQLITE_OK) {
     sess.showOrgMessage("SQL error in %s: %s", func, errmsg);
     sqlite3_free(errmsg);
     return false;
   }
   return true;
}

void F_copy_entry(int) {

  int id = sess.O.rows.at(sess.O.fr).id;
  Query q(sess.db, "SELECT * FROM task WHERE id={}", id);
  if (int res = q.step(); res != SQLITE_ROW) {
    sess.showOrgMessage3("Problem retrieving entry info in copy_entry: {}", res);
    return;
  }
  int priority = q.column_int(2);
  std::string title = "Copy of " + q.column_text(3);
  int folder_tid = q.column_int(5);
  int context_tid = q.column_int(6);
  bool star = q.column_bool(8);

  size_t pos;
  pos = title.find('\'');
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find('\'', pos + 2);
    }

  std::string note = q.column_text(12);
  pos = note.find("'");
  while(pos != std::string::npos) {
    note.replace(pos, 1, "''");
    pos = note.find("'", pos + 2);
  }

  Query q1(sess.db, "INSERT INTO task (priority, title, folder_tid, context_tid, "
                                   "star, added, note, deleted, created, modified) "
                                   "VALUES ({0}, '{1}', {2}, {3}, {4}, date(), '{5}', False, "
                                   "datetime('now', '-{6} hours'), "
                                   "datetime('now'));", 
                                   priority,
                                   title,
                                   folder_tid,
                                   context_tid,
                                   star,
                                   note,
                                   TZ_OFFSET);

  if (int res = q1.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem inserting in copy_entry: {}", res);
    return;
  }

  int new_id =  sqlite3_last_insert_rowid(sess.db.db);

  Query q2(sess.db, "SELECT task_keyword.keyword_id FROM task_keyword WHERE task_keyword.task_id={};",
              id);

  std::vector<int> task_keyword_ids = {}; 
  while (q2.step() == SQLITE_ROW) {
    task_keyword_ids.push_back(q2.column_int(0));
  }

  for (const int &k : task_keyword_ids) {
    add_task_keyword(k, new_id, false); //don't update fts
  }

  /***************fts virtual table update*********************/
  std::string tag = get_task_keywords(new_id).first;
  Query q3(sess.fts, "INSERT INTO fts (title, note, tag, lm_id) VALUES ('{}', '{}', '{}', {});", 
               title, note, tag, new_id); 

  if (int res = q3.step(); res != SQLITE_DONE) {
    sess.showOrgMessage3("Problem inserting in fts in copy_entry: {}", res);
    return;
  }
  get_items(MAX);
}

/* sess.generateContextMap
void map_context_titles(void) {

  // note it's tid because it's sqlite
  Query q(sess.db, "SELECT tid, title FROM context;"); 
  //if (q.result != SQLITE_OK) {
  //  outlineShowMessage3("Problem in 'map_context_titles'; result code: {}", q.result);
  //  return;
 // }

  while (q.step() == SQLITE_ROW) {
    sess.O.context_map[q.column_text(1)] = q.column_int(0);
  }
}

void map_folder_titles(void) {

  // note it's tid because it's sqlite
  Query q(sess.db, "SELECT tid,title FROM folder;"); 
 // if (q.result != SQLITE_OK) {
 //   outlineShowMessage3("Problem in 'map_folder_titles'; result code: {}", q.result);
 //   return;
 // }

  while (q.step() == SQLITE_ROW) {
    sess.O.folder_map[q.column_text(1)] = q.column_int(0);
  }
}
*/

void get_containers(void) {

  sess.O.rows.clear();
  sess.O.fc = sess.O.fr = sess.O.rowoff = 0;

  std::string table;
  std::string column = "title"; //only needs to be change for keyword
  int (*callback)(void *, int, char **, char **);
  switch (sess.O.view) {
    case CONTEXT:
      table = "context";
      callback = context_callback;
      break;
    case FOLDER:
      table = "folder";
      callback = folder_callback;
      break;
    case KEYWORD:
      table = "keyword";
      column = "name";
      callback = keyword_callback;
      break;
    default:
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }
  
  sess.db.query("SELECT * FROM {} ORDER BY {} COLLATE NOCASE ASC;", table, column);
  bool no_rows = true;
  sess.db.params(callback, &no_rows);
  run_sql();

  if (no_rows) {
    sess.showOrgMessage("No results were returned");
    sess.O.mode = NO_ROWS;
  } else {
    //O.mode = NORMAL;
    sess.O.mode = sess.O.last_mode;
    display_container_info(sess.O.rows.at(sess.O.fr).id);
  }

  sess.O.context = sess.O.folder = sess.O.keyword = ""; // this makes sense if you are not in an O.view == TASK
}

int context_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

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

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //"default"
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.modified = time_delta(std::string(argv[9], 16));

  row.completed = false;
  row.dirty = false;
  row.mark = false;

  sess.O.rows.push_back(row);

  return 0;
}

int folder_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

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

  orow row;

  row.title = std::string(argv[2]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; //private
  row.deleted = (atoi(argv[7]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  row.modified = time_delta(std::string(argv[11], 16));
  sess.O.rows.push_back(row);

  return 0;
}

int keyword_callback(void *no_rows, int argc, char **argv, char **azColName) {

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id => int
  1: name = string 25
  2: tid => int
  3: star = Boolean
  4: modified
  5:deleted
  */

  orow row;

  row.title = std::string(argv[1]);
  row.id = atoi(argv[0]); //right now pulling sqlite id not tid
  row.star = (atoi(argv[3]) == 1) ? true: false; 
  row.deleted = (atoi(argv[5]) == 1) ? true: false;
  row.completed = false;
  row.dirty = false;
  row.mark = false;
  row.modified = time_delta(std::string(argv[4], 16));
  sess.O.rows.push_back(row);

  return 0;
}

std::pair<std::string, std::vector<std::string>> get_task_keywords(int id) {

  Query q(sess.db, "SELECT keyword.name FROM task_keyword LEFT OUTER JOIN keyword ON "
              "keyword.id=task_keyword.keyword_id WHERE {}=task_keyword.task_id;",
              id);

  std::vector<std::string> task_keywords = {}; 
  while (q.step() == SQLITE_ROW) {
    task_keywords.push_back(q.column_text(0));
 }

  if (task_keywords.empty()) return std::make_pair(std::string(), std::vector<std::string>());

  std::string s = fmt::format("{}", fmt::join(task_keywords, ","));
  return std::make_pair(s, task_keywords);
}

//overload that takes keyword_id and task_id
void add_task_keyword(int keyword_id, int task_id, bool update_fts) {

  Query q(sess.db, "INSERT OR IGNORE INTO task_keyword (task_id, keyword_id) VALUES ({}, {});",
              //"SELECT {0}, keyword.id FROM keyword WHERE keyword.id={1};",
              task_id, keyword_id);

  if (int res = q.step(); res != SQLITE_DONE) {
    std::string error = (res == 19) ? "SQLITE_CONSTRAINT" : "OTHER SQLITE ERROR";
    sess.showOrgMessage3("Problem in 'add_task_keyword': {}", error);
    return;
  }

   Query q1(sess.db,"UPDATE task SET modified = datetime('now') WHERE id={};", task_id);
   q1.step();
  // *************fts virtual table update**********************
  if (!update_fts) return;
  std::string s = get_task_keywords(task_id).first;
  Query q2(sess.fts, "UPDATE fts SET tag='{}' WHERE lm_id={};", s, task_id);

  if (int res = q2.step(); res != SQLITE_DONE)
        sess.showOrgMessage3("Problem inserting in fts; result code: {}", res);
}

//void add_task_keyword(const std::string &kw, int id) {
//overload that takes keyword name and task_id
void add_task_keyword(std::string &kws, int id) {

  std::stringstream temp(kws);
  std::string phrase;
  std::vector<std::string> keyword_list;
  while(getline(temp, phrase, ',')) {
    keyword_list.push_back(phrase);
  }    

  for (std::string kw : keyword_list) {

    size_t pos = kw.find("'");
    while(pos != std::string::npos)
      {
        kw.replace(pos, 1, "''");
        pos = kw.find("'", pos + 2);
      }

    std::stringstream query;

    /*IF NOT EXISTS(SELECT 1 FROM keyword WHERE name = 'mango') INSERT INTO keyword (name) VALUES ('mango')
     * <- doesn't work for sqlite
     * note you don't have to do INSERT OR IGNORE but could just INSERT since unique constraint
     * on keyword.name but you don't want to trigger an error either so probably best to retain
     * INSERT OR IGNORE there is a default that tid = 0 but let's do it explicity*/

    query <<  "INSERT OR IGNORE INTO keyword (name, tid, star, modified, deleted) VALUES ('"
          <<  kw << "', " << 0 << ", true, datetime('now'), false);"; 

    if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query2;
    query2 << "INSERT OR IGNORE INTO task_keyword (task_id, keyword_id) SELECT " << id << ", keyword.id FROM keyword WHERE keyword.name = '" << kw <<"';";
    if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    std::stringstream query3;
    // updates task modified column so we know that something changed with the task
    query3 << "UPDATE task SET modified = datetime('now') WHERE id =" << id << ";";
    if (!db_query(S.db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

    /**************fts virtual table update**********************/

    std::string s = get_task_keywords(id).first; // 11-10-2020
    std::stringstream query4;
    query4 << "UPDATE fts SET tag='" << s << "' WHERE lm_id=" << id << ";";
    if (!db_query(S.fts_db, query4.str().c_str(), 0, 0, &S.err_msg, __func__)) return;
  }
}

//returns keyword id
int keyword_exists(const std::string &name) {
  Query q(sess.db, "SELECT keyword.id FROM keyword WHERE keyword.name='{}';", name); 
  if (q.result != SQLITE_OK) {
    sess.showOrgMessage3("Problem in 'keyword_exists'; result code: {}", q.result);
    return -1;
  }
  // if there are no rows returned q.step() returns SQLITE_DONE = 101; SQLITE_ROW = 100
  if (q.step() == SQLITE_ROW) return q.column_int(0);
  else return -1;
}
/*
int keyword_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT keyword.id from keyword WHERE keyword.name = '" << name << "';";
  int keyword_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &keyword_id, &S.err_msg, __func__)) return -1;
  return keyword_id;
}
*/

// not in use but might have some use
int folder_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT folder.id from folder WHERE folder.name = '" << name << "';";
  int folder_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &folder_id, &S.err_msg, __func__)) return -1;
  return folder_id;
}

// not in use but might have some use
int context_exists(std::string &name) {  
  std::stringstream query;
  query << "SELECT context.id from context WHERE context.name = '" << name << "';";
  int context_id = 0;
  if (!db_query(S.db, query.str().c_str(), container_id_callback, &context_id, &S.err_msg, __func__)) return -1;
  return context_id;
}

int container_id_callback(void *container_id, int argc, char **argv, char **azColName) {
  int *id = static_cast<int*>(container_id);
  *id = atoi(argv[0]);
  return 0;
}
//void delete_task_keywords(void) {
void F_deletekeywords(int) {

  std::stringstream query;
  query << "DELETE FROM task_keyword WHERE task_id = " << sess.O.rows.at(sess.O.fr).id << ";";
  if (!db_query(S.db, query.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  std::stringstream query2;
  // updates task modified column so know that something changed with the task
  query2 << "UPDATE task SET modified = datetime('now') WHERE id =" << sess.O.rows.at(sess.O.fr).id << ";";
  if (!db_query(S.db, query2.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  /**************fts virtual table update**********************/
  std::stringstream query3;
  query3 << "UPDATE fts SET tag='' WHERE lm_id=" << sess.O.rows.at(sess.O.fr).id << ";";
  if (!db_query(S.fts_db, query3.str().c_str(), 0, 0, &S.err_msg, __func__)) return;

  sess.showOrgMessage("Keyword(s) for task %d will be deleted and fts searchdb updated", sess.O.rows.at(sess.O.fr).id);
  sess.O.mode = sess.O.last_mode;
}

/*
void get_linked_items(int max) {
  int id = O.rows.at(O.fr).id;
  std::vector<std::string> task_keywords = get_task_keywords(id).second;
  if (task_keywords.empty()) return;

  std::stringstream query;
  unique_ids.clear();

  O.rows.clear();
  O.fc = O.fr = O.rowoff = 0;

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

  for (auto it=task_keywords.begin(); it != task_keywords.end() - 1; ++it) {
    query << "keyword.name = '" << *it << "' OR ";
  }
  query << "keyword.name = '" << task_keywords.back() << "')";

  query << ((!O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << O.sort
        << " DESC LIMIT " << max;

    int sortcolnum = sort_map[O.sort];
    if (!db_query(S.db, query.str().c_str(), unique_data_callback, &sortcolnum, &S.err_msg, __func__)) return;

  O.view = TASK;

  if (O.rows.empty()) {
    sess.showOrgMessage("No results were returned");
    O.mode = NO_ROWS;
    eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    O.mode = O.last_mode;
    get_preview(O.rows.at(O.fr).id); //if id == -1 does not try to retrieve note
  }
}
*/

void get_items(int max) {
  std::stringstream query;
  std::vector<std::string> keyword_vec;
  int (*callback)(void *, int, char **, char **);
  callback = data_callback;

  sess.O.rows.clear();
  sess.O.fc = sess.O.fr = sess.O.rowoff = 0;

  if (sess.O.taskview == BY_CONTEXT) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " WHERE context.title = '" << sess.O.context << "' ";
  } else if (sess.O.taskview == BY_FOLDER) {
    query << "SELECT * FROM task JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE folder.title = '" << sess.O.folder << "' ";
  } else if (sess.O.taskview == BY_RECENT) {
    query << "SELECT * FROM task WHERE 1=1";
  } else if (sess.O.taskview == BY_JOIN) {
    query << "SELECT * FROM task JOIN context ON context.tid = task.context_tid"
          << " JOIN folder ON folder.tid = task.folder_tid"
          << " WHERE context.title = '" << sess.O.context << "'"
          << " AND folder.title = '" << sess.O.folder << "'";
  } else if (sess.O.taskview == BY_KEYWORD) {

 // if O.keyword has more than one keyword
    std::string k;
    std::stringstream skeywords;
    skeywords << sess.O.keyword;
    while (getline(skeywords, k, ',')) {
      keyword_vec.push_back(k);
    }

    query << "SELECT * FROM task JOIN task_keyword ON task.id = task_keyword.task_id JOIN keyword ON keyword.id = task_keyword.keyword_id"
          << " WHERE task.id = task_keyword.task_id AND task_keyword.keyword_id = keyword.id AND (";

    for (auto it = keyword_vec.begin(); it != keyword_vec.end() - 1; ++it) {
      query << "keyword.name = '" << *it << "' OR ";
    }
    query << "keyword.name = '" << keyword_vec.back() << "')";

    callback = unique_data_callback;
    //unique_ids.clear();//01072020

  } else {
    sess.showOrgMessage("You asked for an unsupported db query");
    return;
  }

  query << ((!sess.O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        //<< " ORDER BY task."
        << " ORDER BY task.star DESC,"
        << " task."
        << sess.O.sort
        << " DESC LIMIT " << max;

  int sortcolnum = sess.O.sort_map.at(sess.O.sort);
  if (!db_query(S.db, query.str().c_str(), callback, &sortcolnum, &S.err_msg, __func__)) return;

  sess.O.view = TASK;

  if (sess.O.rows.empty()) {
    sess.showOrgMessage("No results were returned");
    sess.O.mode = NO_ROWS;
    sess.eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    sess.O.mode = sess.O.last_mode;
    get_preview(sess.O.rows.at(sess.O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL

  I thought I should be using tid as the "id" for sqlite version but realized
  that would work and mean you could always compare the tid to the pg id
  but for new items created with sqlite, there would be no tid so
  the right thing to use is the id.  At some point might also want to
  store the tid in orow row
  */


  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;

  int *sc = static_cast<int*>(sortcolnum);
  if (argv[*sc] != nullptr) row.modified = time_delta(std::string(argv[*sc], 16));
  else row.modified.assign(15, ' ');

  row.dirty = false;
  row.mark = false;

  sess.O.rows.push_back(row);

  return 0;
}

int unique_data_callback(void *sortcolnum, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  orow row;
  row.id = atoi(argv[0]);
  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;

  int *sc = static_cast<int*>(sortcolnum);
  if (argv[*sc] != nullptr) row.modified = time_delta(std::string(argv[*sc], 16));
  else row.modified.assign(15, ' ');

  row.dirty = false;
  row.mark = false;

  sess.O.rows.push_back(row);

  return 0;
}

// called as part of :find -> search_db -> fts5_callback -> search_db -> get_items_by_id -> by_id_data_callback
void get_items_by_id(std::string query) {
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */

  bool no_rows = true;
  if (!db_query(S.db, query, by_id_data_callback, &no_rows, &S.err_msg, __func__)) return;

  sess.O.view = TASK;

  if (no_rows) {
    sess.showOrgMessage("No results were returned");
    sess.O.mode = NO_ROWS;
    sess.eraseRightScreen(); // in case there was a note displayed in previous view
  } else {
    sess.O.mode = FIND;
    get_preview(sess.O.rows.at(sess.O.fr).id); //if id == -1 does not try to retrieve note
  }
}

int by_id_data_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag =
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL

  I thought I should be using tid as the "id" for sqlite version but realized
  that would work and mean you could always compare the tid to the pg id
  but for new items created with sqlite, there would be no tid so
  the right thing to use is the id.  At some point might also want to
  store the tid in orow row
  */

  orow row;

  row.title = std::string(argv[3]);
  row.id = atoi(argv[0]);
  row.fts_title = sess.O.fts_titles.at(row.id);
  row.star = (atoi(argv[8]) == 1) ? true: false;
  row.deleted = (atoi(argv[14]) == 1) ? true: false;
  row.completed = (argv[10]) ? true: false;

  // we're not giving user choice of time column here but could 
  if (argv[16] != nullptr) row.modified = time_delta(std::string(argv[16], 16));
  else row.modified.assign(15, ' ');

  row.dirty = false;
  row.mark = false;

  sess.O.rows.push_back(row);

  return 0;
}

/*** not used right now ****/
std::string get_title(int id) {
  std::string title;
  std::stringstream query;
  query << "SELECT title FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), title_callback, &title, &S.err_msg, __func__)) return std::string("SQL Problem");
  return title;
}

int title_callback (void *title, int argc, char **argv, char **azColName) {
  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  std::string *t = static_cast<std::string*>(title);
  *t = std::string(argv[0]);
  return 0;
}

/*
// using class version of sqlite code; previous approach is below
// not sure I want to convert to this
void get_note(int id) {
  if (id ==-1) return; // id given to new and unsaved entries

  sess.db.query("SELECT note FROM task WHERE id = {}", id);
  sess.db.callback = note_callback;
  sess.db.pArg = sess.p;
  run_sql();

  if (!sess.p->linked_editor) return;

  sess.p->linked_editor->rows = std::vector<std::string>{" "};
 //  below works but don't think we want to store output_window
 // db.query("SELECT subnote FROM task WHERE id = {}", id);
//  db.callback = note_callback;
//  db.pArg = p->linked_editor;
//  run_sql();
  
}

// doesn't appear to be called if row is NULL
int note_callback (void *e, int argc, char **argv, char **azColName) {

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
    //snote will not contain the '\n'
    //p->editorInsertRow(p->rows.size(), s);
    editor->editorInsertRow(editor->rows.size(), s);
  }

  editor->dirty = 0; //assume editorInsertRow increments dirty so this needed
  return 0;
}
*/

int rowid_callback (void *rowid, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *rwid = static_cast<int*>(rowid);
  *rwid = atoi(argv[0]);
  return 0;
}

int offset_callback (void *n, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);
  int *nn= static_cast<int*>(n);

  word_positions.at(*nn).push_back(atoi(argv[0]));

  return 0;
}

void search_db(const std::string & st ) {

  sess.O.rows.clear();
  sess.O.fc = sess.O.fr = sess.O.rowoff = 0;

  /*
  std::stringstream fts_query;
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            << search_terms << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";
  */

  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  std::string fts_query = fmt::format("SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') "
                                      "FROM fts WHERE fts MATCH '{}' ORDER BY bm25(fts, 2.0, 1.0, 5.0);",
                                      st);

  sess.O.fts_ids.clear();
  sess.O.fts_titles.clear();
  //fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query, fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    sess.showOrgMessage("No results were returned");
    sess.eraseRightScreen(); //note can still return no rows from get_items_by_id if we found rows above that were deleted
    sess.O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.id IN (";

  //for (int i = 0; i < fts_counter-1; i++) {
  int max = sess.O.fts_ids.size() - 1;
  for (int i=0; i < max; i++) {
    query << sess.O.fts_ids[i] << ", ";
  }
  //query << fts_ids[fts_counter-1]
  query << sess.O.fts_ids[max]
        << ")"
        << ((!sess.O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  //for (int i = 0; i < fts_counter-1; i++) {
  for (int i=0; i < max; i++) {
    query << "task.id = " << sess.O.fts_ids[i] << " DESC, ";
  }
  //query << "task.id = " << fts_ids[fts_counter-1] << " DESC";
  query << "task.id = " << sess.O.fts_ids[max] << " DESC";

  get_items_by_id(query.str());
}

//total kluge but just brings back context_tid = 16
void search_db2(const std::string & st) {

  sess.O.rows.clear();
  sess.O.fc = sess.O.fr = sess.O.rowoff = 0;

  std::stringstream fts_query;
  /*
   * Note that since we are not at the moment deleting tasks from the fts db, deleted task ids
   * may be retrieved from the fts db but they will not match when we look for them in the regular db
  */
  fts_query << "SELECT lm_id, highlight(fts, 0, '\x1b[48;5;31m', '\x1b[49m') FROM fts WHERE fts MATCH '"
            << st << "' ORDER BY bm25(fts, 2.0, 1.0, 5.0);";

  sess.O.fts_ids.clear();
  sess.O.fts_titles.clear();
  //fts_counter = 0;

  bool no_rows = true;
  if (!db_query(S.fts_db, fts_query.str().c_str(), fts5_callback, &no_rows, &S.err_msg, __func__)) return;

  if (no_rows) {
    sess.showOrgMessage("No results were returned");
    sess.eraseRightScreen();
    sess.O.mode = NO_ROWS;
    return;
  }
  std::stringstream query;

  // As noted above, if the item is deleted (gone) from the db it's id will not be found if it's still in fts
  query << "SELECT * FROM task WHERE task.context_tid = 16 and task.id IN (";

  //for (int i = 0; i < fts_counter-1; i++) {
  int max = sess.O.fts_ids.size() - 1;
  for (int i=0; i < max; i++) {
    query << sess.O.fts_ids[i] << ", ";
  }
  //query << fts_ids[fts_counter-1]
  query << sess.O.fts_ids[max]
        << ")"
        << ((!sess.O.show_deleted) ? " AND task.completed IS NULL AND task.deleted = False" : "")
        << " ORDER BY ";

  //for (int i = 0; i < fts_counter-1; i++) {
  for (int i=0; i < max; i++) {
    query << "task.id = " << sess.O.fts_ids[i] << " DESC, ";
  }
  //query << "task.id = " << fts_ids[fts_counter-1] << " DESC";
  query << "task.id = " << sess.O.fts_ids[max] << " DESC";

  get_items_by_id(query.str());
}

int fts5_callback(void *no_rows, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  bool *flag = static_cast<bool*>(no_rows);
  *flag = false;

  sess.O.fts_ids.push_back(atoi(argv[0]));
  sess.O.fts_titles[atoi(argv[0])] = std::string(argv[1]);
  //fts_counter++;

  return 0;
}

int get_folder_tid(int id) {

  std::stringstream query;
  query << "SELECT folder_tid FROM task WHERE id = " << id;

  int folder_tid = -1;
  //int rc = sqlite3_exec(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg);
  if (!db_query(S.db, query.str().c_str(), folder_tid_callback, &folder_tid, &S.err_msg, __func__)) return -1;
  return folder_tid;
}

int folder_tid_callback(void *folder_tid, int argc, char **argv, char **azColName) {
  int *f_tid = static_cast<int*>(folder_tid);
  *f_tid = atoi(argv[0]);
  return 0;
}

void display_item_info(void) {
  /*
  0: id = 1
  1: tid = 1
  2: priority = 3
  3: title = Parents refrigerator broken.
  4: tag = 
  5: folder_tid = 1
  6: context_tid = 1
  7: duetime = NULL
  8: star = 0
  9: added = 2009-07-04
  10: completed = 2009-12-20
  11: duedate = NULL
  12: note = new one coming on Monday, June 6, 2009.
  13: repeat = NULL
  14: deleted = 0
  15: created = 2016-08-05 23:05:16.256135
  16: modified = 2016-08-05 23:05:16.256135
  17: startdate = 2009-07-04
  18: remind = NULL
  */

  int id = sess.O.rows.at(sess.O.fr).id;
  std::string s{};
  int width = sess.totaleditorcols - 10;
  int length = sess.textlines - 10;
  Query q(sess.db, "SELECT * FROM task WHERE id={};", id);
  q.step();

  // \x1b[NC moves cursor forward by N columns
  std::string lf_ret = fmt::format("\r\n\x1b[{}C", sess.divider + 6);
  s.append(fmt::format("id: {}{}", q.column_int(0), lf_ret));

  int tid = q.column_int(1);
  //s.append(fmt::format("tid: {}{}", q.column_int(1), lf_ret));
  s.append(fmt::format("tid: {}{}", tid, lf_ret));
  
  std::string title = fmt::format("title: {}", q.column_text(3));
  if (title.size() > width) {
    title = title.substr(0, width - 3).append("...");
  }
  //coloring labels will take some work b/o gray background
  //s.append(fmt::format("{}title:{} {}{}", COLOR_1, "\x1b[m", title, lf_ret));
  s.append(fmt::format("{}{}", title, lf_ret));


  int context_tid = q.column_int(6);
  auto it = std::ranges::find_if(sess.O.context_map, [&context_tid](auto& z) {return z.second == context_tid;});
  s.append(fmt::format("context: {}{}", it->first, lf_ret));

  int folder_tid = q.column_int(5);
  auto it2 = std::ranges::find_if(sess.O.folder_map, [&folder_tid](auto& z) {return z.second == folder_tid;});
  s.append(fmt::format("folder: {}{}", it2->first, lf_ret));

  s.append(fmt::format("star: {}{}", q.column_bool(8), lf_ret));
  s.append(fmt::format("deleted: {}{}", q.column_bool(14), lf_ret));
  s.append(fmt::format("completed: {}{}", q.column_bool(10), lf_ret));

  s.append(fmt::format("modified: {}{}", q.column_text(16), lf_ret));
  s.append(fmt::format("added: {}{}", q.column_text(9), lf_ret));

  s.append(fmt::format("keywords: {}", get_task_keywords(id).first, lf_ret));

  std::string ab{};
  //hide the cursor
  ab.append("\x1b[?25l");
  //ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, O.divider + 6));
 
  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, sess.divider + 7));

  //erase set number of chars on each line
  std::string erase_chars = fmt::format("\x1b[{}X", sess.totaleditorcols - 10);
  for (int i=0; i < length-1; i++) {
    ab.append(erase_chars);
    ab.append(lf_ret);
  }

  ab.append(fmt::format("\x1b[{};{}H", TOP_MARGIN + 6, sess.divider + 7));

  ab.append(fmt::format("\x1b[2*x\x1b[{};{};{};{};48;5;235$r\x1b[*x", 
               TOP_MARGIN+6, sess.divider+7, TOP_MARGIN+4+length, sess.divider+7+width));
  ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
  ab.append(s);
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
  
  // display_item_info_pg needs to be updated if it is going to be used
  //if (tid) display_item_info_pg(tid); //// ***** remember to remove this guard
}

void display_container_info(int id) {
  if (id ==-1) return;

  std::string table;
  std::string count_query;
  int (*callback)(void *, int, char **, char **);

  switch(sess.O.view) {
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
  if (!db_query(S.db, query.str().c_str(), count_callback, &count, &S.err_msg)) return;

  std::stringstream query2;
  query2 << "SELECT * FROM " << table << " WHERE id = " << id;

  // callback is *not* called if result (argv) is null

  if (!db_query(S.db, query2.str().c_str(), callback, &count, &S.err_msg)) return;

}

int count_callback (void *count, int argc, char **argv, char **azColName) {

  UNUSED(argc); //number of columns in the result
  UNUSED(azColName);

  int *cnt = static_cast<int*>(count);
  *cnt = atoi(argv[0]);
  return 0;
}

int context_info_callback(void *count, int argc, char **argv, char **azColName) {

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

  auto it = std::ranges::find_if(sess.O.context_map, [&tid](auto& z) {return z.second == tid;});

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

int folder_info_callback(void *count, int argc, char **argv, char **azColName) {

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

  auto it = std::ranges::find_if(sess.O.folder_map, [&tid](auto& z) {return z.second == tid;});

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

int keyword_info_callback(void *count, int argc, char **argv, char **azColName) {

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

void update_note(bool is_subnote, bool closing_editor) {

  std::string column = (is_subnote) ? "subnote" : "note";
  std::string text = sess.p->editorRowsToString();

  //int folder_tid = get_folder_tid(O.rows.at(O.fr).id);
  int folder_tid = get_folder_tid(sess.p->id);
  //if (!lsp.empty && !is_subnote && !closing_editor && get_folder_tid(O.rows.at(O.fr).id) == 18) {
  if (!is_subnote && !closing_editor && (folder_tid == 18 || folder_tid == 14)) {
    sess.p->code = text;
    //code_changed = true;
    update_code_file();
  }

  // need to escape single quotes with two single quotes
  size_t pos = text.find("'");
  while(pos != std::string::npos) {
    text.replace(pos, 1, "''");
    pos = text.find("'", pos + 2);
  }

  std::string query = fmt::format("UPDATE task SET {}='{}', modified=datetime('now'), "
                                  "startdate=datetime('now', '-{} hours') where id={}",
                                   column, text, TZ_OFFSET, sess.p->id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg)) return;

  if (is_subnote) {
    sess.showOrgMessage("Updated *sub*note for item %d", sess.p->id);
    //sess.O.outlineRefreshScreen();
    sess.refreshOrgScreen();
    return;
  }
  /***************fts virtual table update*********************/
  query = fmt::format("UPDATE fts SET note='{}' WHERE lm_id={}", text, sess.p->id);
  if (!db_query(S.fts_db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  sess.showOrgMessage("Updated note and fts entry for item %d", sess.p->id);
  //sess.O.outlineRefreshScreen();
  sess.refreshOrgScreen();
}

void update_task_context(std::string &new_context, int id) {

  int context_tid = sess.O.context_map.at(new_context);

  std::string query = fmt::format("UPDATE task SET context_tid={}, modified=datetime('now') WHERE id={}",
                                    context_tid, id);

  db_query(S.db, query.c_str(), 0, 0, &S.err_msg);
}

void update_task_folder(std::string& new_folder, int id) {

  int folder_tid = sess.O.folder_map.at(new_folder);
  std::string query = fmt::format("UPDATE task SET folder_tid={}, modified=datetime('now') WHERE id={}",
                                    folder_tid, id);

  db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__);
}

void toggle_completed(void) {

  orow& row = sess.O.rows.at(sess.O.fr);
  int id = get_id();

  std::string query = fmt::format("UPDATE task SET completed={}, modified=datetime('now') WHERE id={}", 
                                  (row.completed) ? "NULL" : "date()", id); 

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  row.completed = !row.completed;
  sess.showOrgMessage("Toggle completed succeeded");
}

void touch(void) {
  int id = get_id();
  std::string query = fmt::format("UPDATE task SET modified=datetime('now') WHERE id={}", 
                                  id); 

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;
    
  sess.showOrgMessage("'Touch' succeeded");
}

void toggle_deleted(void) {

  orow& row = sess.O.rows.at(sess.O.fr);
  int id = get_id();
  std::string table;

  switch(sess.O.view) {
    case TASK:
      table = "task";
      break;
    case CONTEXT:
      table = "context";
      break;
    case FOLDER:
      table = "folder";
      break;
    case KEYWORD:
      table = "keyword";
      break;
    default:
      sess.showOrgMessage("Somehow you are in a view I can't handle");
      return;
  }

  std::string query = fmt::format("UPDATE {} SET deleted={}, modified=datetime('now') WHERE id={}",
                   table, (row.deleted) ? "False" : "True", id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;
    
  sess.showOrgMessage("Toggle deleted succeeded");
  row.deleted = !row.deleted;
}

void toggle_star(void) {

  orow& row = sess.O.rows.at(sess.O.fr);
  int id = get_id();
  std::string table;
  std::string column;

  switch(sess.O.view) {

    case TASK:
      table = "task";
      column = "star";
      break;

    case CONTEXT:
      table = "context";
      column = "\"default\"";
      break;

    case FOLDER:
      table = "folder";
      column = "private";
      break;

    case KEYWORD:
      table = "keyword";
      column = "star";
      break;

    default:
      sess.showOrgMessage("Not sure what you're trying to toggle");
      return;
  }

  std::string query = fmt::format("UPDATE {} SET {}={}, modified=datetime('now') WHERE id={}",
                                   table, column, (row.star) ? "False" : "True", id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  sess.showOrgMessage("Toggle star succeeded");
  row.star = !row.star;
}

void update_title(void) {

  orow& row = sess.O.rows.at(sess.O.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insert_row(row);
    return;
  }

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  std::string query = fmt::format("UPDATE task SET title='{}', modified=datetime('now') WHERE id={}",
                                     title, row.id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;
  row.dirty = false;

  /***************fts virtual table update*********************/
  query = fmt::format("Update fts SET title='{}' WHERE lm_id={}", title, row.id);
  if (!db_query(S.fts_db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  sess.showOrgMessage("Updated title and fts entry for item %d", row.id);
  //sess.O.outlineRefreshScreen();
  sess.refreshOrgScreen();

  // moved here 10262020
  if (lm_browser) {
    int folder_tid = get_folder_tid(sess.O.rows.at(sess.O.fr).id);
    if (!(folder_tid == 18 || folder_tid == 14)) update_html_file("assets/" + CURRENT_NOTE_FILE);
    //else update_html_code_file("assets/" + CURRENT_NOTE_FILE);//don't need to update b/o title
  }   
}

void update_container(void) {

  orow& row = sess.O.rows.at(sess.O.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insert_container(row);
    return;
  }

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  std::string query = fmt::format("UPDATE {} SET title='{}', modified=datetime('now') WHERE id={}",
                                   (sess.O.view == CONTEXT) ? "context" : "folder",
                                    title, row.id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  row.dirty = false;
  sess.showOrgMessage("Successfully updated row %d", row.id);
}

/* note that after changing a keyword's name would really have to update every entry
 * in the fts_db that had a tag that included that keyword
 */
void update_keyword(void) {

  orow& row = sess.O.rows.at(sess.O.fr);

  if (!row.dirty) {
    sess.showOrgMessage("Row has not been changed");
    return;
  }

  if (row.id == -1) {
    insert_keyword(row);
    return;
  }
  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
    title.replace(pos, 1, "''");
    pos = title.find("'", pos + 2);
  }

  std::string query = fmt::format("UPDATE keyword SET name='{}', modified=datetime('now') WHERE id={}",
                                   title, row.id);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return;

  row.dirty = false;
  sess.showOrgMessage("Successfully updated row %d", row.id);
}
/*Inserting a new keyword should not require any fts_db update. Just like any keyword
 *added to an entry - the tag created is entered into fts_db when that keyword is
 *attached to an entry.
*/
int insert_keyword(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos) {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
  }

  //note below that the temp tid is zero for all inserted keywords
  std::string query = fmt::format("INSERT INTO keyword (name, star, deleted, modified, tid) " \
                                   "VALUES ('{}', {}, False, datetime('now'), 0);",
                                   title, row.star);

  if (!db_query(S.db, query.c_str(), 0, 0, &S.err_msg, __func__)) return -1;

  row.id =  sqlite3_last_insert_rowid(S.db);
  row.dirty = false;
  sess.showOrgMessage("Successfully inserted new keyword with id %d and indexed it", row.id);

  return row.id;
}

// sqlite cleanup start here
int insert_row(orow& row) {

  std::string title = row.title;
  size_t pos = title.find('\'');
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find('\'', pos + 2);
    }

  std::string queryx = fmt::format("INSERT INTO task (priority, title, folder_tid, context_tid, " \
                                   "star, added, note, deleted, created, modified) " \
                                   "VALUES (3, '{0}', {1}, {2}, True, date(), '', False, " \
                                   "datetime('now', '-{3} hours'), " \
                                   "datetime('now'));", 
                                   title,
                                   (sess.O.folder == "") ? 1 : sess.O.folder_map.at(sess.O.folder),
                                   (sess.O.context == "") ? 1 : sess.O.context_map.at(sess.O.context),
                                   TZ_OFFSET);

  std::stringstream query;
  query << "INSERT INTO task ("
        << "priority, "
        << "title, "
        << "folder_tid, "
        << "context_tid, "
        << "star, "
        << "added, "
        << "note, "
        << "deleted, "
        << "created, "
        << "modified "
        //<< "startdate "
        << ") VALUES ("
        << " 3," //priority
        << "'" << title << "'," //title
        //<< " 1," //folder_tid
        << ((sess.O.folder == "") ? 1 : sess.O.folder_map.at(sess.O.folder)) << ", "
        //<< ((O.context != "search") ? context_map.at(O.context) : 1) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        //<< ((O.context == "search" || O.context == "recent" || O.context == "") ? 1 : context_map.at(O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << ((sess.O.context == "") ? 1 : sess.O.context_map.at(sess.O.context)) << ", " //context_tid; if O.context == "search" context_id = 1 "No Context"
        << " True," //star
        << "date()," //added
        //<< "'<This is a new note from sqlite>'," //note
        << "''," //note
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now')" // modified
        //<< " date()" //startdate
        << ");"; // RETURNING id;",

  /*
    not used:
    tid,
    tag,
    duetime,
    completed,
    duedate,
    repeat,
    remind
  */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    sess.showOrgMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    sess.showOrgMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  /***************fts virtual table update*********************/

  //should probably create a separate function that is a klugy
  //way of making up for fact that pg created tasks don't appear in fts db
  //"INSERT OR IGNORE INTO fts (title, lm_id) VALUES ('" << title << row.id << ");";

  std::stringstream query2;
  query2 << "INSERT INTO fts (title, lm_id) VALUES ('" << title << "', " << row.id << ")";

  rc = sqlite3_open(FTS_DB.c_str(), &db);

  if (rc != SQLITE_OK) {
    sess.showOrgMessage("Cannot open FTS database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return row.id;
  }

  rc = sqlite3_exec(db, query2.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    sess.showOrgMessage("SQL error doing FTS insert: %s", err_msg);
    sqlite3_free(err_msg);
    return row.id; // would mean regular insert succeeded and fts failed - need to fix this
  }
  sqlite3_close(db);
  sess.showOrgMessage("Successfully inserted new row with id %d and indexed it", row.id);

  return row.id;
}

int insert_container(orow& row) {

  std::string title = row.title;
  size_t pos = title.find("'");
  while(pos != std::string::npos)
    {
      title.replace(pos, 1, "''");
      pos = title.find("'", pos + 2);
    }

  std::stringstream query;
  query << "INSERT INTO "
        //<< context
        << ((sess.O.view == CONTEXT) ? "context" : "folder")
        << " ("
        << "title, "
        << "deleted, "
        << "created, "
        << "modified, "
        << "tid, "
        << ((sess.O.view == CONTEXT) ? "\"default\", " : "private, ") //context -> "default"; folder -> private
      //  << "\"default\", " //folder does not have default
        << "textcolor "
        << ") VALUES ("
        << "'" << title << "'," //title
        << " False," //deleted
        << " datetime('now', '-" << TZ_OFFSET << " hours')," //created
        << " datetime('now')," // modified
        //<< " 99999," //tid
        << " " << sess.temporary_tid << "," //tid originally 100 but that is a legit client tid server id
        << " False," //default for context and private for folder
        << " 10" //textcolor
        << ");"; // RETURNING id;",

  sess.temporary_tid++;      
  /*
   * not used:
     "default" (not sure why in quotes but may be system variable
      tid,
      icon (varchar 32)
      image (blob)
    */

  sqlite3 *db;
  char *err_msg = nullptr; //0

  int rc = sqlite3_open(SQLITE_DB.c_str(), &db);

  if (rc != SQLITE_OK) {

    sess.showOrgMessage("Cannot open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return -1;
    }

  rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

  if (rc != SQLITE_OK ) {
    sess.showOrgMessage("SQL error doing new item insert: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }
  row.id =  sqlite3_last_insert_rowid(db);
  row.dirty = false;

  sqlite3_close(db);

  sess.showOrgMessage("Successfully inserted new context with id %d and indexed it", row.id);

  return row.id;
}

void update_rows(void) {
  int n = 0; //number of updated rows
  int updated_rows[20];

  for (auto row: sess.O.rows) {
    if (!(row.dirty)) continue;
    if (row.id != -1) {
      std::string title = row.title;
      size_t pos = title.find("'");
      while(pos != std::string::npos)
      {
        title.replace(pos, 1, "''");
        pos = title.find("'", pos + 2);
      }

      std::stringstream query;
      query << "UPDATE task SET title='" << title << "', modified=datetime('now') WHERE id=" << row.id;

      sqlite3 *db;
      char *err_msg = 0;
        
      int rc = sqlite3_open(SQLITE_DB.c_str(), &db);
        
      if (rc != SQLITE_OK) {
            
        sess.showOrgMessage("Cannot open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
        }
    
      rc = sqlite3_exec(db, query.str().c_str(), 0, 0, &err_msg);

      if (rc != SQLITE_OK ) {
        sess.showOrgMessage("SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return; // ? should we abort all other rows
      } else {
        row.dirty = false;
        updated_rows[n] = row.id;
        n++;
        sqlite3_close(db);
      }
    
    } else { 
      int id  = insert_row(row);
      updated_rows[n] = id;
      if (id !=-1) n++;
    }  
  }

  if (n == 0) {
    sess.showOrgMessage("There were no rows to update");
    return;
  }

  char msg[200];
  strncpy(msg, "Rows successfully updated: ", sizeof(msg));
  char *put;
  put = &msg[strlen(msg)];

  for (int j=0; j < n;j++) {
    sess.O.rows.at(updated_rows[j]).dirty = false; // 10-28-2019
    put += snprintf(put, sizeof(msg) - (put - msg), "%d, ", updated_rows[j]);
  }

  int slen = strlen(msg);
  msg[slen-2] = '\0'; //end of string has a trailing space and comma 
  sess.showOrgMessage("%s",  msg);
}

/*************************end sql**************************************/

void update_solr(void) {

  PyObject *pName, *pModule, *pFunc;
  PyObject *pArgs, *pValue;

  int num = 0;

  Py_Initialize(); //getting valgrind invalid read error but not sure it's meaningful
  pName = PyUnicode_DecodeFSDefault("update_solr"); //module
  /* Error checking of pName left out */

  pModule = PyImport_Import(pName);
  Py_DECREF(pName);

  if (pModule != NULL) {
      pFunc = PyObject_GetAttrString(pModule, "update_solr"); //function
      /* pFunc is a new reference */

      if (pFunc && PyCallable_Check(pFunc)) {
          pArgs = PyTuple_New(0); //presumably PyTuple_New(x) creates a tuple with that many elements
          //pValue = PyLong_FromLong(1);
          //pValue = Py_BuildValue("s", search_terms); // **************
          //PyTuple_SetItem(pArgs, 0, pValue); // ***********
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
              sess.showOrgMessage("Problem retrieving ids from solr!");
          }
      } else { if (PyErr_Occurred()) PyErr_Print();
          sess.showOrgMessage("Was not able to find the function: update_solr!");
      }

      Py_XDECREF(pFunc);
      Py_DECREF(pModule);

  } else {
      PyErr_Print();
      sess.showOrgMessage("Was not able to find the module: update_solr!");
  }

  //if (Py_FinalizeEx() < 0) {
  //}

  sess.showOrgMessage("%d items were added/updated to solr db", num);
}

[[ noreturn]] void die(const char *s) {
  // write is from <unistd.h> 
  //ssize_t write(int fildes, const void *buf, size_t nbytes);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &sess.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &sess.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = sess.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0; // minimum data to receive?
  raw.c_cc[VTIME] = 1; // timeout for read will return 0 if no bytes read

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
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
    if (nread == -1 && errno != EAGAIN) die("read");
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
  std::string_view cl = sess.O.command_line;
  if (pos) {
    bool success = false;
    //structured bindings
    for (const auto & [k,v] : sess.O.context_map) {
      if (k.rfind(cl.substr(pos + 1), 0) == 0) {
      //if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        sess.O.context = k;
        success = true;
        break;
      }
    }

    if (!success) {
      //outlineShowMessage2(fmt::format("{} is not a valid  context!", cl.substr(pos + 1)));
      sess.showOrgMessage2(fmt::format("{} is not a valid  context!", cl.substr(pos + 1)));
      sess.O.mode = NORMAL;
      return;
    }

  } else {
    //sess.showOrgMessage("You did not provide a context!");
    //outlineShowMessage2("You did not provide a context!");
    sess.showOrgMessage2("You did not provide a context!");
    sess.O.mode = NORMAL;
    return;
  }
  //sess.showOrgMessage("\'%s\' will be opened", O.context.c_str());
  //outlineShowMessage2(fmt::format("'{}' will be opened", O.context.c_str()));
  sess.showOrgMessage3("'{}' will be opened, Steve", sess.O.context.c_str());
  sess.command_history.push_back(sess.O.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, sess.O.command_line);

  sess.O.marked_entries.clear();
  sess.O.folder = "";
  sess.O.taskview = BY_CONTEXT;
  get_items(MAX);
  //O.mode = O.last_mode;
  sess.O.mode = NORMAL;
  return;
}

void F_openfolder(int pos) {
  std::string_view cl = sess.O.command_line;
  if (pos) {
    bool success = false;
    for (const auto & [k,v] : sess.O.folder_map) {
      if (k.rfind(cl.substr(pos + 1), 0) == 0) {
      //if (strncmp(&O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        sess.O.folder = k;
        success = true;
        break;
      }
    }
    if (!success) {
      //sess.showOrgMessage("%s is not a valid  folder!", &O.command_line.c_str()[pos + 1]);
      //outlineShowMessage2(fmt::format("{} is not a valid  folder!", cl.substr(pos + 1)));
      sess.showOrgMessage2(fmt::format("{} is not a valid  folder!", cl.substr(pos + 1)));
      sess.O.mode = NORMAL;
      return;
    }

  } else {
    sess.showOrgMessage("You did not provide a folder!");
    sess.O.mode = NORMAL;
    return;
  }
  sess.showOrgMessage("\'%s\' will be opened", sess.O.folder.c_str());
  sess.command_history.push_back(sess.O.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, sess.O.command_line);
  sess.O.marked_entries.clear();
  sess.O.context = "";
  sess.O.taskview = BY_FOLDER;
  get_items(MAX);
  sess.O.mode = NORMAL;
  return;
}

void F_openkeyword(int pos) {
  if (!pos) {
    sess.showOrgMessage("You need to provide a keyword");
    sess.O.mode = NORMAL;
    return;
  }
 
  //O.keyword = O.command_line.substr(pos+1);
  std::string keyword = sess.O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
    sess.O.mode = sess.O.last_mode;
    sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
    return;
  }

  sess.O.keyword = keyword;  
  sess.showOrgMessage("\'%s\' will be opened", sess.O.keyword.c_str());
  sess.command_history.push_back(sess.O.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, sess.O.command_line);
  sess.O.marked_entries.clear();
  sess.O.context = "";
  sess.O.folder = "";
  sess.O.taskview = BY_KEYWORD;
  get_items(MAX);
  sess.O.mode = NORMAL;
  return;
}

void F_addkeyword(int pos) {
  if (!pos) {
    sess.O.current_task_id = sess.O.rows.at(sess.O.fr).id;
    sess.eraseRightScreen();
    sess.O.view = KEYWORD;
    sess.command_history.push_back(sess.O.command_line);
    get_containers(); //O.mode = NORMAL is in get_containers
    sess.O.mode = ADD_CHANGE_FILTER;
    sess.showOrgMessage("Select keyword to add to marked or current entry");
    return;
  }

  // only do this if there was text after C_addkeyword
  if (sess.O.last_mode == NO_ROWS) return;

  {
  std::string keyword = sess.O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
      sess.O.mode = sess.O.last_mode;
      sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
      return;
  }

  if (sess.O.marked_entries.empty()) {
    add_task_keyword(keyword, sess.O.rows.at(sess.O.fr).id);
    sess.showOrgMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : sess.O.marked_entries) {
      add_task_keyword(keyword, id);
    }
    sess.showOrgMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  }
  sess.O.mode = sess.O.last_mode;
  return;
}

void F_keywords(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    sess.O.view = KEYWORD;
    sess.command_history.push_back(sess.O.command_line); 
    get_containers(); //O.mode = NORMAL is in get_containers
    sess.showOrgMessage("Retrieved keywords");
    return;
  }  

  // only do this if there was text after C_keywords
  if (sess.O.last_mode == NO_ROWS) return;

  {
  std::string keyword = sess.O.command_line.substr(pos+1);
  if (!keyword_exists(keyword)) {
      sess.O.mode = sess.O.last_mode;
      sess.showOrgMessage("keyword '%s' does not exist!", keyword.c_str());
      return;
  }

  if (sess.O.marked_entries.empty()) {
    add_task_keyword(keyword, sess.O.rows.at(sess.O.fr).id);
    sess.showOrgMessage("No tasks were marked so added %s to current task", keyword.c_str());
  } else {
    for (const auto& id : sess.O.marked_entries) {
      add_task_keyword(keyword, id);
    }
    sess.showOrgMessage("Marked tasks had keyword %s added", keyword.c_str());
  }
  }
  sess.O.mode = sess.O.last_mode;
  return;
}

void F_write(int) {
  if (sess.O.view == TASK) update_rows();
  sess.O.mode = sess.O.last_mode;
  sess.O.command_line.clear();
}

void F_x(int) {
  if (sess.O.view == TASK) update_rows();
  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //sends cursor home (upper left)
  exit(0);
}

void F_refresh(int) {
  if (sess.O.view == TASK) {
    sess.showOrgMessage("Entries will be refreshed");
    if (sess.O.taskview == BY_FIND)
      search_db(sess.fts_search_terms);
    else
      get_items(MAX);
  } else {
    sess.showOrgMessage("contexts/folders will be refreshed");
    get_containers();
  }
  sess.O.mode = sess.O.last_mode;
}

void F_new(int) {
  outlineInsertRow(0, "", true, false, false, now());
  sess.O.fc = sess.O.fr = sess.O.rowoff = 0;
  sess.O.command[0] = '\0';
  sess.O.repeat = 0;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
  sess.eraseRightScreen(); //erases the note area
  sess.O.mode = INSERT;

  sess.O.preview_rows.clear(); //10262020 - was pulling old preview rows when saving title (see NORMAL mode)
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
  
  if (!(sess.O.view == TASK)) {
    sess.O.command[0] = '\0';
    sess.O.mode = NORMAL;
    sess.showOrgMessage("Only tasks have notes to edit!");
    return;
  }

  //pos is zero if no space and command modifier
  if (id == 0) id = get_id();
  if (id == -1) {
    sess.showOrgMessage("You need to save item before you can create a note");
    sess.O.command[0] = '\0';
    sess.O.mode = NORMAL;
    return;
  }

  sess.showOrgMessage("Edit note %d", id);
  //sess.O.outlineRefreshScreen();
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

      int folder_tid = get_folder_tid(sess.O.rows.at(sess.O.fr).id);
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
      sess.getNote(id); //if id == -1 does not try to retrieve note
      
    } else {
      sess.p = *it;
    }    
  } else {
    sess.p = new Editor;
    Editor* & p = sess.p;
    sess.editors.push_back(p);
    p->id = id;
    p->top_margin = TOP_MARGIN + 1;

    int folder_tid = get_folder_tid(sess.O.rows.at(sess.O.fr).id);
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
    sess.getNote(id); //if id == -1 does not try to retrieve note
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

  sess.O.command[0] = '\0';
  sess.O.mode = NORMAL;
}

void F_contexts(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    sess.O.view = CONTEXT;
    sess.command_history.push_back(sess.O.command_line); 
    get_containers();
    sess.O.mode = NORMAL;
    sess.showOrgMessage("Retrieved contexts");
    return;
  } else {

    std::string new_context;
    bool success = false;
    if (sess.O.command_line.size() > 5) { //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : sess.O.context_map) {
        if (strncmp(&sess.O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_context = k;
          success = true;
          break;
        }
      }
      if (!success) {
        sess.showOrgMessage("What you typed did not match any context");
        sess.O.mode = NORMAL;
        return;
      }

    } else {
      sess.showOrgMessage("You need to provide at least 3 characters "
                        "that match a context!");

      sess.O.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : sess.O.rows) {
      if (it.mark) {
        update_task_context(new_context, it.id);
        success = true;
      }
    }

    if (success) {
      sess.showOrgMessage("Marked tasks moved into context %s", new_context.c_str());
    } else {
      update_task_context(new_context, sess.O.rows.at(sess.O.fr).id);
      sess.showOrgMessage("No tasks were marked so moved current task into context %s", new_context.c_str());
    }
    sess.O.mode = sess.O.last_mode;
    return;
  }
}

void F_folders(int pos) {
  if (!pos) {
    sess.eraseRightScreen();
    sess.O.view = FOLDER;
    sess.command_history.push_back(sess.O.command_line); 
    get_containers();
    sess.O.mode = NORMAL;
    sess.showOrgMessage("Retrieved folders");
    return;
  } else {

    std::string new_folder;
    bool success = false;
    if (sess.O.command_line.size() > 5) {  //this needs work - it's really that pos+1 to end needs to be > 2
      // structured bindings
      for (const auto & [k,v] : sess.O.folder_map) {
        if (strncmp(&sess.O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
          new_folder = k;
          success = true;
          break;
        }
      }
      if (!success) {
        sess.showOrgMessage("What you typed did not match any folder");
        sess.O.mode = NORMAL;
        return;
      }

    } else {
      sess.showOrgMessage("You need to provide at least 3 characters "
                        "that match a folder!");

      sess.O.mode = NORMAL;
      return;
    }
    success = false;
    for (const auto& it : sess.O.rows) {
      if (it.mark) {
        update_task_folder(new_folder, it.id);
        success = true;
      }
    }

    if (success) {
      sess.showOrgMessage("Marked tasks moved into folder %s", new_folder.c_str());
    } else {
      update_task_folder(new_folder, sess.O.rows.at(sess.O.fr).id);
      sess.showOrgMessage("No tasks were marked so moved current task into folder %s", new_folder.c_str());
    }
    sess.O.mode = sess.O.last_mode;
    return;
  }
}

void F_recent(int) {
  sess.showOrgMessage("Will retrieve recent items");
  sess.command_history.push_back(sess.O.command_line);
  sess.page_history.push_back(sess.O.command_line);
  sess.page_hx_idx = sess.page_history.size() - 1;
  sess.O.marked_entries.clear();
  sess.O.context = "No Context";
  sess.O.taskview = BY_RECENT;
  sess.O.folder = "No Folder";
  get_items(MAX);
}


void F_createLink(int) {
  if (sess.editors.empty()) {
    sess.showOrgMessage("There are no entries being edited");
    sess.O.mode = NORMAL;
    return;
  }
  std::unordered_set<int> temp;
  for (const auto z : sess.editors) {
    temp.insert(z->id);
  }

  if (temp.size() != 2) {
    sess.showOrgMessage("At the moment you can only link two entries at a time");
    sess.O.mode = NORMAL;
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
    sess.showOrgMessage3("Problem in 'add_task_keyword': {}", error);
    sess.O.mode = NORMAL;
    return;
  }

   Query q1(sess.db,"UPDATE link SET modified = datetime('now') WHERE task_id0={} AND task_id1={};", task_ids[0], task_ids[1]);
   q1.step();

   sess.O.mode = NORMAL;
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

/*
void F_linked(int) {
  int id = O.rows.at(O.fr).id;
  std::string keywords = get_task_keywords(id).first;
  if (keywords.empty()) {
    sess.showOrgMessage("The current entry has no keywords");
  } else {
    O.keyword = keywords;
    O.context = "No Context";
    O.folder = "No Folder";
    O.taskview = BY_KEYWORD;
    get_linked_items(MAX);
    sess.command_history.push_back("ok " + keywords); 
  }
}
*/

void F_find(int pos) {
  if (sess.O.command_line.size() < 6) {
    sess.showOrgMessage("You need more characters");
    return;
  }  
  sess.O.context = "";
  sess.O.folder = "";
  sess.O.taskview = BY_FIND;
  //O.mode = FIND; ////// it's in get_items_by_id
  std::string st = sess.O.command_line.substr(pos+1);
  std::transform(st.begin(), st.end(), st.begin(), ::tolower);
  sess.command_history.push_back(sess.O.command_line); 
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, sess.O.command_line);
  sess.showOrgMessage("Searching for %s", st.c_str());
  sess.fts_search_terms = st;
  search_db(st);
}

void F_sync(int) {
  synchronize(0); // do actual sync
  //map_context_titles();
  sess.generateContextMap();
  //map_folder_titles();
  sess.generateFolderMap();
  sess.initial_file_row = 0; //for arrowing or displaying files
  sess.O.mode = FILE_DISPLAY; // needs to appear before displayFile
  sess.showOrgMessage("Synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_sync_test(int) {
  synchronize(1); //1 -> report_only
  sess.initial_file_row = 0; //for arrowing or displaying files
  sess.O.mode = FILE_DISPLAY; // needs to appear before displayFile
  sess.showOrgMessage("Testing synching local db and server and displaying results");
  readFile("log");
  displayFile();//put them in the command mode case synch
}

void F_updatecontext(int) {
  sess.O.current_task_id = sess.O.rows.at(sess.O.fr).id;
  sess.eraseRightScreen();
  sess.O.view = CONTEXT;
  sess.command_history.push_back(sess.O.command_line); 
  get_containers(); //O.mode = NORMAL is in get_containers
  sess.O.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
  sess.showOrgMessage("Select context to add to marked or current entry");
}

void F_updatefolder(int) {
  sess.O.current_task_id = sess.O.rows.at(sess.O.fr).id;
  sess.eraseRightScreen();
  sess.O.view = FOLDER;
  sess.command_history.push_back(sess.O.command_line); 
  get_containers(); //O.mode = NORMAL is in get_containers
  sess.O.mode = ADD_CHANGE_FILTER; //this needs to change to somthing like UPDATE_TASK_MODIFIERS
  sess.showOrgMessage("Select folder to add to marked or current entry");
}

void F_delmarks(int) {
  for (auto& it : sess.O.rows) {
    it.mark = false;}
  if (sess.O.view == TASK) sess.O.marked_entries.clear(); //why the if??
  sess.O.mode = sess.O.last_mode;
  sess.showOrgMessage("Marks all deleted");
}

// to avoid confusion should only be an editor command line function
void F_savefile(int pos) {
  sess.command_history.push_back(sess.O.command_line);
  std::string filename;
  if (pos) filename = sess.O.command_line.substr(pos+1);
  else filename = "example.cpp";
  sess.p->editorSaveNoteToFile(filename);
  sess.showOrgMessage("Note saved to file: %s", filename.c_str());
  sess.O.mode = NORMAL;
}

//this really needs work - needs to be picked like keywords, folders etc.
void F_sort(int pos) { 
  if (pos && sess.O.view == TASK && sess.O.taskview != BY_FIND) {
    sess.O.sort = sess.O.command_line.substr(pos + 1);
    get_items(MAX);
    sess.showOrgMessage("sorted by \'%s\'", sess.O.sort.c_str());
  } else {
    sess.showOrgMessage("Currently can't sort search, which is sorted on best match");
  }
}

void  F_showall(int) {
  if (sess.O.view == TASK) {
    sess.O.show_deleted = !sess.O.show_deleted;
    sess.O.show_completed = !sess.O.show_completed;
    if (sess.O.taskview == BY_FIND)
      ; //search_db();
    else
      get_items(MAX);
  }
  sess.showOrgMessage((sess.O.show_deleted) ? "Showing completed/deleted" : "Hiding completed/deleted");
}

// does not seem to work
void F_syntax(int pos) {
  if (pos) {
    std::string action = sess.O.command_line.substr(pos + 1);
    if (action == "on") {
      sess.p->highlight_syntax = true;
      sess.showOrgMessage("Syntax highlighting will be turned on");
    } else if (action == "off") {
      sess.p->highlight_syntax = false;
      sess.showOrgMessage("Syntax highlighting will be turned off");
    } else {sess.showOrgMessage("The syntax is 'sh on' or 'sh off'"); }
  } else {sess.showOrgMessage("The syntax is 'sh on' or 'sh off'");}
  sess.p->editorRefreshScreen(true);
  sess.O.mode = NORMAL;
}

// set spell | set nospell
// should also only be an editor function
void F_set(int pos) {
  std::string action = sess.O.command_line.substr(pos + 1);
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
  sess.O.mode = NORMAL;
}

// also should be only editor function
void F_open_in_vim(int) {
  open_in_vim(); //send you into editor mode
  sess.p->mode = NORMAL;
  //O.command[0] = '\0';
  //O.repeat = 0;
  //O.mode = NORMAL;
}

void F_join(int pos) {
  if (sess.O.view != TASK || sess.O.taskview == BY_JOIN || pos == 0) {
    sess.showOrgMessage("You are either in a view where you can't join or provided no join container");
    sess.O.mode = NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    sess.O.mode = sess.O.last_mode; //NORMAL; //you are in command_line as long as switch to normal - don't need above two lines
    return;
  }
  bool success = false;

  if (sess.O.taskview == BY_CONTEXT) {
    for (const auto & [k,v] : sess.O.folder_map) {
      if (strncmp(&sess.O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        sess.O.folder = k;
        success = true;
        break;
      }
    }
  } else if (sess.O.taskview == BY_FOLDER) {
    for (const auto & [k,v] : sess.O.context_map) {
      if (strncmp(&sess.O.command_line.c_str()[pos + 1], k.c_str(), 3) == 0) {
        sess.O.context = k;
        success = true;
        break;
      }
    }
  }
  if (!success) {
    sess.showOrgMessage("You did not provide a valid folder or context to join!");
    sess.O.command_line.resize(1);
    return;
  }

  sess.showOrgMessage("Will join \'%s\' with \'%s\'", sess.O.folder.c_str(), sess.O.context.c_str());
  sess.O.taskview = BY_JOIN;
  get_items(MAX);
  return;
}

void F_saveoutline(int pos) { 
  if (pos) {
    std::string fname = sess.O.command_line.substr(pos + 1);
    outlineSave(fname);
    sess.O.mode = NORMAL;
    sess.showOrgMessage("Saved outline to %s", fname.c_str());
  } else {
    sess.showOrgMessage("You didn't provide a file name!");
  }
}

void F_valgrind(int) {
  sess.initial_file_row = 0; //for arrowing or displaying files
  readFile("valgrind_log_file");
  displayFile();//put them in the command mode case synch
  sess.O.last_mode = sess.O.mode;
  sess.O.mode = FILE_DISPLAY;
}

void F_help(int pos) {
  if (!pos) {             
    /*This needs to be changed to show database text not ext file*/
    sess.initial_file_row = 0;
    sess.O.last_mode = sess.O.mode;
    sess.O.mode = FILE_DISPLAY;
    sess.showOrgMessage("Displaying help file");
    readFile("listmanager_commands");
    displayFile();
  } else {
    std::string st = sess.O.command_line.substr(pos+1);
    sess.O.context = "";
    sess.O.folder = "";
    sess.O.taskview = BY_FIND;
    //O.mode = FIND; ////// it's in get_items_by_id
    std::transform(st.begin(), st.end(), st.begin(), ::tolower);
    sess.command_history.push_back(sess.O.command_line); 
    sess.fts_search_terms = st;
    search_db2(st);
    sess.showOrgMessage("Will look for help on %s", st.c_str());
    //O.mode = NORMAL;
  }  
}

//case 'q':
void F_quit_app(int) {
  bool unsaved_changes = false;
  for (auto it : sess.O.rows) {
    if (it.dirty) {
      unsaved_changes = true;
      break;
    }
  }
  if (unsaved_changes) {
    sess.O.mode = NORMAL;
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
  sqlite3_close(S.db);
  PQfinish(conn);
  //t0.join();
  //subscriber.close();
  context.close();
  publisher.close();
  exit(0);
}

void F_lsp_start(int pos) {
  /*
  if (!lsp.empty) {
    lsp_shutdown();
    O.mode = NORMAL;
    outlineShowMessage3("Shutting down {}", lsp.name);
    return;
  }
  */

  std::string_view name = sess.O.command_line;
  if (pos) name = name.substr(pos + 1);
  else {
    sess.showOrgMessage("Which lsp do you want?");
    return;
  }

  Lsp *lsp = new Lsp;
  if (name.rfind("go", 0) == 0) {
    lsp->name = "gopls";
    lsp->client_uri = R"(file:///home/slzatz/go/src/example/)";
    lsp->file_name = "main.go";
    lsp->language = "go";
  } else if (name.rfind("cl", 0) == 0) {
    lsp->name = "clangd";
    lsp->client_uri = R"(file:///home/slzatz/clangd_examples/)";
    lsp->file_name = "test.cpp";
    lsp->language = "cpp";
  } else {
    sess.showOrgMessage3("There is no lsp named {}", name);
    sess.O.mode = NORMAL;
    return;
  }

  sess.showOrgMessage3("Starting {}", lsp->name);
  lsp_start(lsp);
  sess.O.mode = NORMAL;
}

void F_launch_lm_browser(int) {
  if (lm_browser) {
    sess.showOrgMessage3("There is already an active browser");
    return;
  }
  lm_browser = true; 
  std::system("./lm_browser current.html &"); //&=> returns control
  sess.O.mode = NORMAL;
}

void F_quit_lm_browser(int) {
  zmq::message_t message(20);
  snprintf ((char *) message.data(), 20, "%s", "quit"); //25 - complete hack but works ok
  publisher.send(message, zmq::send_flags::dontwait);
  lm_browser = false;
  sess.O.mode = NORMAL;
}
/* END OUTLINE COMMAND mode functions */

/* OUTLINE NORMAL mode functions */
void goto_editor_N(void) {
  if (sess.editors.empty()) {
    sess.showOrgMessage("There are no active editors");
    return;
  }

  sess.eraseRightScreen();
  sess.drawEditors();

  sess.editor_mode = true;
}

void F_resize(int pos) {
  std::string s = sess.O.command_line;
  if (pos) {
    //s = O.command_line.substr(pos + 1);
    size_t p = s.find_first_of("0123456789");
    if (p != pos + 1) {
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      sess.O.mode = NORMAL;
      return;
    }
    int pct = stoi(s.substr(p));
    if (pct > 90 || pct < 10) { 
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      sess.O.mode = NORMAL;
      return;
    }
    c.ed_pct = pct;
  } else {
      sess.showOrgMessage("You need to provide a number between 10 and 90");
      sess.O.mode = NORMAL;
      return;
  }
  sess.O.mode = NORMAL;
  signalHandler(0);
}

void return_N(void) {
  orow& row = sess.O.rows.at(sess.O.fr);

  if(row.dirty){
    if (sess.O.view == TASK) update_title();
    else if (sess.O.view == CONTEXT || sess.O.view == FOLDER) update_container();
    else if (sess.O.view == KEYWORD) update_keyword();
    sess.O.command[0] = '\0'; //11-26-2019
    sess.O.mode = NORMAL;
    if (sess.O.fc > 0) sess.O.fc--;
    return;
    //sess.showOrgMessage("");
  }

  // return means retrieve items by context or folder
  // do this in database mode
  if (sess.O.view == CONTEXT) {
    sess.O.context = row.title;
    sess.O.folder = "";
    sess.O.taskview = BY_CONTEXT;
    sess.showOrgMessage("\'%s\' will be opened", sess.O.context.c_str());
    sess.O.command_line = "o " + sess.O.context;
  } else if (sess.O.view == FOLDER) {
    sess.O.folder = row.title;
    sess.O.context = "";
    sess.O.taskview = BY_FOLDER;
    sess.showOrgMessage("\'%s\' will be opened", sess.O.folder.c_str());
    sess.O.command_line = "o " + sess.O.folder;
  } else if (sess.O.view == KEYWORD) {
    sess.O.keyword = row.title;
    sess.O.folder = "";
    sess.O.context = "";
    sess.O.taskview = BY_KEYWORD;
    sess.showOrgMessage("\'%s\' will be opened", sess.O.keyword.c_str());
    sess.O.command_line = "ok " + sess.O.keyword;
  }

  sess.command_history.push_back(sess.O.command_line);
  sess.page_hx_idx++;
  sess.page_history.insert(sess.page_history.begin() + sess.page_hx_idx, sess.O.command_line);
  sess.O.marked_entries.clear();

  get_items(MAX);
}

//case 'i':
void insert_N(void){
  sess.O.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 's':
void s_N(void){
  orow& row = sess.O.rows.at(sess.O.fr);
  row.title.erase(sess.O.fc, sess.O.repeat);
  row.dirty = true;
  sess.O.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m"); //[1m=bold
}          

//case 'x':
void x_N(void){
  orow& row = sess.O.rows.at(sess.O.fr);
  row.title.erase(sess.O.fc, sess.O.repeat);
  row.dirty = true;
}        

void daw_N(void) {
  for (int i = 0; i < sess.O.repeat; i++) sess.O.outlineDelWord();
}

void caw_N(void) {
  for (int i = 0; i < sess.O.repeat; i++) sess.O.outlineDelWord();
  sess.O.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void dw_N(void) {
  for (int j = 0; j < sess.O.repeat; j++) {
    int start = sess.O.fc;
    sess.O.outlineMoveEndWord2();
    int end = sess.O.fc;
    sess.O.fc = start;
    orow& row = sess.O.rows.at(sess.O.fr);
    row.title.erase(sess.O.fc, end - start + 2);
  }
}

void cw_N(void) {
  for (int j = 0; j < sess.O.repeat; j++) {
    int start = sess.O.fc;
    sess.O.outlineMoveEndWord2();
    int end = sess.O.fc;
    sess.O.fc = start;
    orow& row = sess.O.rows.at(sess.O.fr);
    row.title.erase(sess.O.fc, end - start + 2);
  }
  sess.O.mode = INSERT;
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

void de_N(void) {
  int start = sess.O.fc;
  sess.O.outlineMoveEndWord(); //correct one to use to emulate vim
  int end = sess.O.fc;
  sess.O.fc = start; 
  for (int j = 0; j < end - start + 1; j++) sess.O.outlineDelChar();
  sess.O.fc = (start < sess.O.rows.at(sess.O.fr).title.size()) ? start : sess.O.rows.at(sess.O.fr).title.size() -1;
}

void d$_N(void) {
  sess.O.outlineDeleteToEndOfLine();
}
//case 'r':
void r_N(void) {
  sess.O.mode = REPLACE;
}

//case '~'
void tilde_N(void) {
  for (int i = 0; i < sess.O.repeat; i++) outlineChangeCase();
}

//case 'a':
void a_N(void){
  sess.O.mode = INSERT; //this has to go here for MoveCursor to work right at EOLs
  sess.O.outlineMoveCursor(ARROW_RIGHT);
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'A':
void A_N(void) {
  sess.O.outlineMoveCursorEOL();
  sess.O.mode = INSERT; //needs to be here for movecursor to work at EOLs
  sess.O.outlineMoveCursor(ARROW_RIGHT);
  sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
}

//case 'b':
void b_N(void) {
  sess.O.outlineMoveBeginningWord();
}

//case 'e':
void e_N(void) {
  sess.O.outlineMoveEndWord();
}

//case '0':
void zero_N(void) {
  if (!sess.O.rows.empty()) sess.O.fc = 0; // this was commented out - not sure why but might be interfering with O.repeat
}

//case '$':
void dollar_N(void) {
  sess.O.outlineMoveCursorEOL();
}

//case 'I':
void I_N(void) {
  if (!sess.O.rows.empty()) {
    sess.O.fc = 0;
    sess.O.mode = 1;
    sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
  }
}

void gg_N(void) {
  sess.O.fc = sess.O.rowoff = 0;
  sess.O.fr = sess.O.repeat-1; //this needs to take into account O.rowoff
  if (sess.O.view == TASK) get_preview(sess.O.rows.at(sess.O.fr).id);
  else display_container_info(sess.O.rows.at(sess.O.fr).id);
}

//case 'G':
void G_N(void) {
  sess.O.fc = 0;
  sess.O.fr = sess.O.rows.size() - 1;
  if (sess.O.view == TASK) get_preview(sess.O.rows.at(sess.O.fr).id);
  else display_container_info(sess.O.rows.at(sess.O.fr).id);
}

void gt_N(void) {
  std::map<std::string, int>::iterator it;

  if ((sess.O.view == TASK && sess.O.taskview == BY_FOLDER) || sess.O.view == FOLDER) {
    if (!sess.O.folder.empty()) {
      it = sess.O.folder_map.find(sess.O.folder);
      it++;
      if (it == sess.O.folder_map.end()) it = sess.O.folder_map.begin();
    } else {
      it = sess.O.folder_map.begin();
    }
    sess.O.folder = it->first;
    sess.showOrgMessage("\'%s\' will be opened", sess.O.folder.c_str());
  } else {
    if (sess.O.context.empty() || sess.O.context == "search") {
      it = sess.O.context_map.begin();
    } else {
      it = sess.O.context_map.find(sess.O.context);
      it++;
      if (it == sess.O.context_map.end()) it = sess.O.context_map.begin();
    }
    sess.O.context = it->first;
    sess.showOrgMessage("\'%s\' will be opened", sess.O.context.c_str());
  }
  get_items(MAX);
}

/*
//case 'O': //Same as C_new in COMMAND_LINE mode
void O_N(void) {
  F_new(0); //zero means nothing but need b/o F_new(int)
}
*/

//case ':':
void colon_N(void) {
  sess.showOrgMessage(":");
  sess.O.command_line.clear();
  sess.O.last_mode = sess.O.mode;
  sess.O.mode = COMMAND_LINE;
}

//case 'v':
void v_N(void) {
  sess.O.mode = VISUAL;
  sess.O.highlight[0] = sess.O.highlight[1] = sess.O.fc;
  sess.showOrgMessage("\x1b[1m-- VISUAL --\x1b[0m");
}

//case 'p':  
void p_N(void) {
  if (!sess.O.string_buffer.empty()) sess.O.outlinePasteString();
}

//case '*':  
void asterisk_N(void) {
  sess.O.outlineGetWordUnderCursor();
  outlineFindNextWord(); 
}

//case 'm':
void m_N(void) {
  sess.O.rows.at(sess.O.fr).mark = !sess.O.rows.at(sess.O.fr).mark;
  if (sess.O.rows.at(sess.O.fr).mark) {
    sess.O.marked_entries.insert(sess.O.rows.at(sess.O.fr).id);
  } else {
    sess.O.marked_entries.erase(sess.O.rows.at(sess.O.fr).id);
  }  
  sess.showOrgMessage("Toggle mark for item %d", sess.O.rows.at(sess.O.fr).id);
}

//case 'n':
void n_N(void) {
  outlineFindNextWord();
}

//case 'u':
void u_N(void) {
  //could be used to update solr - would use U
}

//dd and 0x4 -> ctrl-d
void dd_N(void) {
  toggle_deleted();
}

//0x2 -> ctrl-b
void star_N(void) {
  toggle_star();
}

//0x18 -> ctrl-x
void completed_N(void) {
  toggle_completed();
}

void navigate_page_hx(int direction) {
  if (sess.page_history.size() == 1 && sess.O.view == TASK) return;

  if (direction == PAGE_UP) {

    // if O.view!=TASK and PAGE_UP - moves back to last page
    if (sess.O.view == TASK) { //if in a container viewa - fall through to previous TASK view page

      if (sess.page_hx_idx == 0) sess.page_hx_idx = sess.page_history.size() - 1;
      else sess.page_hx_idx--;
    }

  } else {
    if (sess.page_hx_idx == (sess.page_history.size() - 1)) sess.page_hx_idx = 0;
    else sess.page_hx_idx++;
  }

  /* go into COMMAND_LINE mode */
  sess.O.mode = COMMAND_LINE;
  sess.O.command_line = sess.page_history.at(sess.page_hx_idx);
  outlineProcessKeypress('\r');
  sess.O.command_line.clear();

  /* return to NORMAL mode */
  sess.O.mode = NORMAL;
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
  sess.O.command_line = sess.command_history.at(sess.cmd_hx_idx);
}

/*** outline operations ***/

void outlineInsertRow(int at, std::string&& s, bool star, bool deleted, bool completed, std::string&& modified) {
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

  auto pos = sess.O.rows.begin() + at;
  sess.O.rows.insert(pos, row);
}

void outlineInsertChar(int c) {

  if (sess.O.rows.size() == 0) return;

  orow& row = sess.O.rows.at(sess.O.fr);
  if (row.title.empty()) row.title.push_back(c);
  else row.title.insert(row.title.begin() + sess.O.fc, c);
  row.dirty = true;
  sess.O.fc++;
}

/*
void outlineDelChar(void) {

  orow& row = sess.O.rows.at(sess.O.fr);

  if (sess.O.rows.empty() || row.title.empty()) return;

  row.title.erase(row.title.begin() + sess.O.fc);
  row.dirty = true;
}
*/
/*
void outlineBackspace(void) {
  orow& row = sess.O.rows.at(sess.O.fr);
  if (sess.O.rows.empty() || row.title.empty() || sess.O.fc == 0) return;
  row.title.erase(row.title.begin() + sess.O.fc - 1);
  row.dirty = true;
  sess.O.fc--;
}
*/
/*** file i/o ***/

std::string outlineRowsToString() {
  std::string s = "";
  for (auto i: sess.O.rows) {
      s += i.title;
      s += '\n';
  }
  s.pop_back(); //pop last return that we added
  return s;
}

void outlineSave(const std::string& fname) {
  if (sess.O.rows.empty()) return;

  std::ofstream f;
  f.open(fname);
  f << outlineRowsToString();
  f.close();

  //sess.showOrgMessage("Can't save! I/O error: %s", strerror(errno));
  sess.showOrgMessage("saved to outline.txt");
}

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

void get_preview(int id) {
  std::stringstream query;
  sess.O.preview_rows.clear();
  query << "SELECT note FROM task WHERE id = " << id;
  if (!db_query(S.db, query.str().c_str(), preview_callback, nullptr, &S.err_msg, __func__)) return;

  if (sess.O.taskview != BY_FIND) draw_preview();
  else {
    word_positions.clear(); 
    get_search_positions(id);
    draw_search_preview();
  }
  //draw_preview();

  if (lm_browser) {
    int folder_tid = get_folder_tid(sess.O.rows.at(sess.O.fr).id);
    if (!(folder_tid == 18 || folder_tid == 14)) update_html_file("assets/" + CURRENT_NOTE_FILE);
    else update_html_code_file("assets/" + CURRENT_NOTE_FILE);
  }   
}

void get_search_positions(int id) {
  std::stringstream query;
  query << "SELECT rowid FROM fts WHERE lm_id = " << id << ";";

  int rowid = -1;
  // callback is *not* called if result (argv) is null
  if (!db_query(S.fts_db, query.str().c_str(), rowid_callback, &rowid, &S.err_msg, __func__)) return;

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
    if (!db_query(S.fts_db, query.str().c_str(), offset_callback, &n, &S.err_msg, __func__)) return;

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

// doesn't appear to be called if row is NULL
int preview_callback (void *NotUsed, int argc, char **argv, char **azColName) {

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
    sess.O.preview_rows.push_back(s);
  }
  return 0;
}

void draw_preview(void) {

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
  if (sess.O.preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }

  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  ab.append(generateWWString(sess.O.preview_rows, width, length, lf_ret));
  ab.append(draw_preview_box(width, length));
  write(STDOUT_FILENO, ab.c_str(), ab.size());
}

// there is a ? identical editorGenerateWWString
// used by draw_preview and draw_search_preview (it is used by this)
std::string generateWWString(std::vector<std::string> &rows, int width, int length, std::string ret) {
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

void draw_search_preview(void) {
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
  if (sess.O.preview_rows.empty()) {
    ab.append(buf);
    ab.append("\x1b[48;5;235m"); //draws the box lines with same background as above rectangle
    ab.append(draw_preview_box(width, length));
    write(STDOUT_FILENO, ab.c_str(), ab.size());
    return;
  }
  ab.append(buf);
  ab.append("\x1b[48;5;235m");
  std::string t = generateWWString(sess.O.preview_rows, width, length, "\f");
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

void highlight_terms_string(std::string &text) {

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

std::string draw_preview_box(int width, int length) {
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

// should also just be editor command
void open_in_vim(void){
  std::string filename;
  if (get_folder_tid(sess.O.rows.at(sess.O.fr).id) != 18) filename = "vim_file.txt";
  else filename = "vim_file.cpp";
  sess.p->editorSaveNoteToFile(filename);
  std::stringstream s;
  s << "vim " << filename << " >/dev/tty";
  system(s.str().c_str());
  sess.p->editorReadFileIntoNote(filename);
}

/*************************************start outline commands **********************************************/
/*
// positions the cursor ( O.cx and O.cy) and O.coloff and O.rowoff
void outlineScroll(void) {

  int titlecols = sess.divider - TIME_COL_WIDTH - LEFT_MARGIN;

  if(sess.O.rows.empty()) {
      sess.O.fr = sess.O.fc = sess.O.coloff = sess.O.cx = sess.O.cy = 0;
      return;
  }

  if (sess.O.fr > sess.textlines + sess.O.rowoff - 1) {
    sess.O.rowoff =  sess.O.fr - sess.textlines + 1;
  }

  if (sess.O.fr < sess.O.rowoff) {
    sess.O.rowoff =  sess.O.fr;
  }

  if (sess.O.fc > titlecols + sess.O.coloff - 1) {
    sess.O.coloff =  sess.O.fc - titlecols + 1;
  }

  if (sess.O.fc < sess.O.coloff) {
    sess.O.coloff =  sess.O.fc;
  }


  sess.O.cx = sess.O.fc - sess.O.coloff;
  sess.O.cy = sess.O.fr - sess.O.rowoff;
}
*/
/*************************************(maybe end outline commands **********************************************/
// depends on readKey()
//void outlineProcessKeypress(void) {
void outlineProcessKeypress(int c) { //prototype has int = 0  

  /* readKey brings back one processed character that handles
     escape sequences for things like navigation keys */

  //int c = readKey();
  c = (!c) ? readKey() : c;
  switch (sess.O.mode) {
  size_t n;
  //switch (int c = readKey(); O.mode)  //init statement for if/switch

    case NO_ROWS:

      switch(c) {
        case ':':
          sess.O.command[0] = '\0'; // uncommented on 10212019 but probably unnecessary
          sess.O.command_line.clear();
          sess.showOrgMessage(":");
          sess.O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          return;

        case 'i':
        case 'I':
        case 'a':
        case 'A':
        case 's':
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          sess.O.mode = INSERT;
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
          return;

        case 'O': //Same as C_new in COMMAND_LINE mode
          outlineInsertRow(0, "", true, false, false, BASE_DATE);
          sess.O.fc = sess.O.fr = sess.O.rowoff = 0;
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          sess.showOrgMessage("\x1b[1m-- INSERT --\x1b[0m");
          sess.eraseRightScreen(); //erases the note area
          sess.O.mode = INSERT;
          return;
      }

      return; //in NO_ROWS - do nothing if no command match

    case INSERT:  

      switch (c) {

        case '\r': //also does escape into NORMAL mode
          if (sess.O.view == TASK)  {
            update_title();
            //updating lm_browser file because title changed
            // moved to update_title which should be update_title
            //if (lm_browser) {
             // if (get_folder_tid(O.rows.at(O.fr).id) != 18) update_html_file("assets/" + CURRENT_NOTE_FILE);
              ////else update_html_code_file("assets/" + CURRENT_NOTE_FILE);//don't need to update b/o title
            //}   
          } else if (sess.O.view == CONTEXT || sess.O.view == FOLDER) update_container();
          else if (sess.O.view == KEYWORD) update_keyword();
          sess.O.command[0] = '\0'; //11-26-2019
          sess.O.mode = NORMAL;
          if (sess.O.fc > 0) sess.O.fc--;
          //sess.showOrgMessage("");
          return;

        case HOME_KEY:
          sess.O.fc = 0;
          return;

        case END_KEY:
          {
            orow& row = sess.O.rows.at(sess.O.fr);
          if (row.title.size()) sess.O.fc = row.title.size(); // mimics vim to remove - 1;
          return;
          }

        case BACKSPACE:
          sess.O.outlineBackspace();
          return;

        case DEL_KEY:
          sess.O.outlineDelChar();
          return;

        case '\t':
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
          sess.O.outlineMoveCursor(c);
          return;

        case CTRL_KEY('z'):
          // not in use
          return;

        case '\x1b':
          sess.O.command[0] = '\0';
          sess.O.mode = NORMAL;
          if (sess.O.fc > 0) sess.O.fc--;
          sess.showOrgMessage("");
          return;

        default:
          outlineInsertChar(c);
          return;
      } //end of switch inside INSERT

     // return; //End of case INSERT: No need for a return at the end of INSERT because we insert the characters that fall through in switch default:

    case NORMAL:  

      if (c == '\x1b') {
        if (sess.O.view == TASK) get_preview(sess.O.rows.at(sess.O.fr).id); //get out of display_item_info
        sess.showOrgMessage("");
        sess.O.command[0] = '\0';
        sess.O.repeat = 0;
        return;
      }
 
      /*leading digit is a multiplier*/
      //if (isdigit(c))  //equiv to if (c > 47 && c < 58)

      if ((c > 47 && c < 58) && (strlen(sess.O.command) == 0)) {

        if (sess.O.repeat == 0 && c == 48) {

        } else if (sess.O.repeat == 0) {
          sess.O.repeat = c - 48;
          return;
        }  else {
          sess.O.repeat = sess.O.repeat*10 + c - 48;
          return;
        }
      }

      if (sess.O.repeat == 0) sess.O.repeat = 1;

      n = strlen(sess.O.command);
      sess.O.command[n] = c;
      sess.O.command[n+1] = '\0';

      if (n_lookup.count(sess.O.command)) {
        n_lookup.at(sess.O.command)();
        sess.O.command[0] = '\0';
        sess.O.repeat = 0;
        return;
      }

      //also means that any key sequence ending in something
      //that matches below will perform command

      // needs to be here because needs to pick up repeat
      //Arrows + h,j,k,l
      if (navigation.count(c)) {
          for (int j = 0;j < sess.O.repeat;j++) sess.O.outlineMoveCursor(c);
          sess.O.command[0] = '\0'; 
          sess.O.repeat = 0;
          return;
      }

      if ((c == PAGE_UP) || (c == PAGE_DOWN)) {
        navigate_page_hx(c);
        sess.O.command[0] = '\0';
        sess.O.repeat = 0;
        return;
      }
        
      return; // end of case NORMAL 

    case COMMAND_LINE:

      if (c == '\x1b') {
          sess.O.mode = NORMAL;
          sess.showOrgMessage(""); 
          return;
      }

      if ((c == ARROW_UP) || (c == ARROW_DOWN)) {
        navigate_cmd_hx(c);
        return;
      }  

      if (c == '\r') {
        std::size_t pos = sess.O.command_line.find(' ');
        std::string cmd = sess.O.command_line.substr(0, pos);
        if (cmd_lookup.count(cmd)) {
          if (pos == std::string::npos) pos = 0;
          cmd_lookup.at(cmd)(pos);
          return;
        }

        sess.showOrgMessage("\x1b[41mNot an outline command: %s\x1b[0m", cmd.c_str());
        sess.O.mode = NORMAL;
        return;
      }

      if (c == DEL_KEY || c == BACKSPACE) {
        if (!sess.O.command_line.empty()) sess.O.command_line.pop_back();
      } else {
        sess.O.command_line.push_back(c);
      }

      sess.showOrgMessage(":%s", sess.O.command_line.c_str());
      return; //end of case COMMAND_LINE

    case FIND:  
      switch (c) {

        case PAGE_UP:
        case PAGE_DOWN:
          if (c == PAGE_UP) {
            sess.O.fr = (sess.textlines > sess.O.fr) ? 0 : sess.O.fr - sess.textlines; //O.fr and sess.textlines are unsigned ints
          } else if (c == PAGE_DOWN) {
             sess.O.fr += sess.textlines;
             if (sess.O.fr > sess.O.rows.size() - 1) sess.O.fr = sess.O.rows.size() - 1;
          }
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
        case 'h':
        case 'l':
          sess.O.outlineMoveCursor(c);
          return;

        //TAB and SHIFT_TAB moves from FIND to OUTLINE NORMAL mode but SHIFT_TAB gets back
        case '\t':  
        case SHIFT_TAB:  
          sess.O.fc = 0; 
          sess.O.mode = NORMAL;
          get_preview(sess.O.rows.at(sess.O.fr).id); //only needed if previous comand was 'i'
          sess.showOrgMessage("");
          return;

        default:
          sess.O.mode = NORMAL;
          sess.O.command[0] = '\0'; 
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
          sess.O.outlineMoveCursor(c);
          sess.O.highlight[1] = sess.O.fc; //this needs to be getFileCol
          return;
  
        case 'x':
          sess.O.repeat = abs(sess.O.highlight[1] - sess.O.highlight[0]) + 1;
          sess.O.outlineYankString(); //reportedly segfaults on the editor side

          // the delete below requires positioning the cursor
          sess.O.fc = (sess.O.highlight[1] > sess.O.highlight[0]) ? sess.O.highlight[0] : sess.O.highlight[1];

          for (int i = 0; i < sess.O.repeat; i++) {
            sess.O.outlineDelChar(); //uses editorDeleteChar2! on editor side
          }
          if (sess.O.fc) sess.O.fc--; 
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          sess.O.mode = 0;
          sess.showOrgMessage("");
          return;
  
        case 'y':  
          sess.O.repeat = sess.O.highlight[1] - sess.O.highlight[0] + 1;
          sess.O.fc = sess.O.highlight[0];
          sess.O.outlineYankString();
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          sess.O.mode = 0;
          sess.showOrgMessage("");
          return;
  
        case '\x1b':
          sess.O.mode = NORMAL;
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
          sess.showOrgMessage("");
          return;
  
        default:
          return;
      } //end of inner switch(c) in outer case VISUAL

      //return; //end of case VISUAL (return here would not be executed)

    case REPLACE: 

      if (sess.O.repeat == 0) sess.O.repeat = 1; //10062020

      if (c == '\x1b') {
        sess.O.command[0] = '\0';
        sess.O.repeat = 0;
        sess.O.mode = NORMAL;
        return;
      }

      for (int i = 0; i < sess.O.repeat; i++) {
        sess.O.outlineDelChar();
        outlineInsertChar(c);
      }

      sess.O.repeat = 0;
      sess.O.command[0] = '\0';
      sess.O.mode = NORMAL;

      return; //////// end of outer case REPLACE

    case ADD_CHANGE_FILTER:

      switch(c) {

        case '\x1b':
          {
          sess.O.mode = COMMAND_LINE;
          size_t temp = sess.page_hx_idx;  
          sess.showOrgMessage(":%s", sess.page_history.at(sess.page_hx_idx).c_str());
          sess.O.command_line = sess.page_history.at(sess.page_hx_idx);
          outlineProcessKeypress('\r');
          sess.O.mode = NORMAL;
          sess.O.command[0] = '\0';
          sess.O.command_line.clear();
          sess.page_history.pop_back();
          sess.page_hx_idx = temp;
          sess.O.repeat = 0;
          sess.O.current_task_id = -1; //not sure this is right
          }
          return;

        // could be generalized for folders and contexts too  
        // update_task_folder and update_task_context
        // update_task_context(std::string &, int)
        // maybe make update_task_context(int, int)
        case '\r':
          {
          orow& row = sess.O.rows.at(sess.O.fr); //currently highlighted keyword
          if (sess.O.marked_entries.empty()) {
            switch (sess.O.view) {
              case KEYWORD:
                add_task_keyword(row.id, sess.O.current_task_id);
                sess.showOrgMessage("No tasks were marked so added keyword %s to current task",
                                   row.title.c_str());
                break;
              case FOLDER:
                update_task_folder(row.title, sess.O.current_task_id);
                sess.showOrgMessage("No tasks were marked so current task had folder changed to %s",
                                   row.title.c_str());
                break;
              case CONTEXT:
                update_task_context(row.title, sess.O.current_task_id);
                sess.showOrgMessage("No tasks were marked so current task had context changed to %s",
                                   row.title.c_str());
                break;
            }
          } else {
            for (const auto& task_id : sess.O.marked_entries) {
              //add_task_keyword(row.id, task_id);
              switch (sess.O.view) {
                case KEYWORD:
                  add_task_keyword(row.id, task_id);
                  sess.showOrgMessage("Marked tasks had keyword %s added",
                                     row.title.c_str());
                break;
                case FOLDER:
                  update_task_folder(row.title, task_id);
                  sess.showOrgMessage("Marked tasks had folder changed to %s",
                                     row.title.c_str());
                break;
                case CONTEXT:
                  update_task_context(row.title, task_id);
                  sess.showOrgMessage("Marked tasks had context changed to %s",
                                     row.title.c_str());
                break;
              }
            }
          }
          }

          sess.O.command[0] = '\0'; //might not be necessary
          return;

        case ARROW_UP:
        case ARROW_DOWN:
        case 'j':
        case 'k':
          sess.O.outlineMoveCursor(c);
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
          sess.O.command[0] = '\0';
          sess.O.command_line.clear();
          //O.last_mode was set when entering file mode
          sess.O.mode = COMMAND_LINE;
          return;

        case '\x1b':
          sess.O.mode = sess.O.last_mode;
          sess.eraseRightScreen();
          if (sess.O.view == TASK) get_preview(sess.O.rows.at(sess.O.fr).id);
          else display_container_info(sess.O.rows.at(sess.O.fr).id);
          sess.O.command[0] = '\0';
          sess.O.repeat = 0;
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

int get_id(void) { 
  return sess.O.rows.at(sess.O.fr).id;
}

void outlineChangeCase() {
  orow& row = sess.O.rows.at(sess.O.fr);
  char d = row.title.at(sess.O.fc);
  if (d < 91 && d > 64) d = d + 32;
  else if (d > 96 && d < 123) d = d - 32;
  else {
    sess.O.outlineMoveCursor(ARROW_RIGHT);
    return;
  }
  sess.O.outlineDelChar();
  outlineInsertChar(d);
}

/*
void outlineYankLine(int n){

  line_buffer.clear();

  for (int i=0; i < n; i++) {
    line_buffer.push_back(O.rows.at(O.fr+i).title);
  }
  // set string_buffer to "" to signal should paste line and not chars
  O.string_buffer.clear();
}
*/

/*
void outlineYankString() {
  orow& row = sess.O.rows.at(sess.O.fr);
  sess.O.string_buffer.clear();

  std::string::const_iterator first = row.title.begin() + sess.O.highlight[0];
  std::string::const_iterator last = row.title.begin() + sess.O.highlight[1];
  sess.O.string_buffer = std::string(first, last);
}
*/
/*
void outlinePasteString(void) {
  orow& row = sess.O.rows.at(sess.O.fr);

  if (sess.O.rows.empty() || sess.O.string_buffer.empty()) return;

  row.title.insert(row.title.begin() + sess.O.fc, sess.O.string_buffer.begin(), sess.O.string_buffer.end());
  sess.O.fc += sess.O.string_buffer.size();
  row.dirty = true;
}
*/
/*
void outlineDelWord() {

  orow& row = sess.O.rows.at(sess.O.fr);
  if (row.title[sess.O.fc] < 48) return;

  int i,j,x;
  for (i = sess.O.fc; i > -1; i--){
    if (row.title[i] < 48) break;
    }
  for (j = sess.O.fc; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  sess.O.fc = i+1;

  for (x = 0 ; x < j-i; x++) {
      outlineDelChar();
  }
  row.dirty = true;
  //sess.showOrgMessage("i = %d, j = %d", i, j ); 
}
*/
/*
void outlineDeleteToEndOfLine(void) {
  orow& row = sess.O.rows.at(sess.O.fr);
  row.title.resize(sess.O.fc); // or row.chars.erase(row.chars.begin() + O.fc, row.chars.end())
  row.dirty = true;
}
*/

/*
void outlineMoveCursorEOL() {
  sess.O.fc = sess.O.rows.at(sess.O.fr).title.size() - 1;  //if O.cx > O.titlecols will be adjusted in EditorScroll
}
*/

/*
// not same as 'e' but moves to end of word or stays put if already on end of word
void outlineMoveEndWord2() {
  int j;
  orow& row = sess.O.rows.at(sess.O.fr);

  for (j = sess.O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  sess.O.fc = j - 1;
}
*/
//void outlineMoveNextWord() {
void w_N(void) {
  int j;
  orow& row = sess.O.rows.at(sess.O.fr);

  for (j = sess.O.fc + 1; j < row.title.size(); j++) {
    if (row.title[j] < 48) break;
  }

  sess.O.fc = j - 1;

  for (j = sess.O.fc + 1; j < row.title.size() ; j++) { //+1
    if (row.title[j] > 48) break;
  }
  sess.O.fc = j;

  sess.O.command[0] = '\0';
  sess.O.repeat = 0;
}

/*
void outlineMoveBeginningWord() {
  orow& row = sess.O.rows.at(sess.O.fr);
  if (sess.O.fc == 0) return;
  for (;;) {
    if (row.title[sess.O.fc - 1] < 48) sess.O.fc--;
    else break;
    if (sess.O.fc == 0) return;
  }
  int i;
  for (i = sess.O.fc - 1; i > -1; i--){
    if (row.title[i] < 48) break;
  }
  sess.O.fc = i + 1;
}
*/

/*
void outlineMoveEndWord() {
  orow& row = sess.O.rows.at(sess.O.fr);
  if (sess.O.fc == row.title.size() - 1) return;
  for (;;) {
    if (row.title[sess.O.fc + 1] < 48) sess.O.fc++;
    else break;
    if (sess.O.fc == row.title.size() - 1) return;
  }
  int j;
  for (j = sess.O.fc + 1; j < row.title.size() ; j++) {
    if (row.title[j] < 48) break;
  }
  sess.O.fc = j - 1;
}
*/

/*
void outlineGetWordUnderCursor(){
  std::string& title = sess.O.rows.at(sess.O.fr).title;
  if (title[sess.O.fc] < 48) return;

  title_search_string.clear();
  int i,j,x;

  for (i = sess.O.fc - 1; i > -1; i--){
    if (title[i] < 48) break;
  }

  for (j = sess.O.fc + 1; j < title.size() ; j++) {
    if (title[j] < 48) break;
  }

  for (x=i+1; x<j; x++) {
      title_search_string.push_back(title.at(x));
  }
  sess.showOrgMessage("word under cursor: <%s>", title_search_string.c_str());
}
*/

void outlineFindNextWord() {

  int y, x;
  //y = O.fr;
  y = (sess.O.fr < sess.O.rows.size() -1) ? sess.O.fr + 1 : 0;
  //x = O.fc + 1; //in case sitting on beginning of the word
  x = 0; //you've advanced y since not worried about multiple same words in one title
   for (unsigned int n=0; n < sess.O.rows.size(); n++) {
     std::string& title = sess.O.rows.at(y).title;
     auto res = std::search(std::begin(title) + x, std::end(title), std::begin(title_search_string), std::end(title_search_string));
     if (res != std::end(title)) {
         sess.O.fr = y;
         sess.O.fc = res - title.begin();
         break;
     }
     y++;
     x = 0;
     if (y == sess.O.rows.size()) y = 0;
   }

    sess.showOrgMessage("x = %d; y = %d", x, y); 
}

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
            get_preview(sess.O.rows.at(sess.O.fr).id); 
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
              get_preview(sess.O.rows.at(sess.O.fr).id); // with change in while loop should not get overwritten
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
            get_preview(sess.O.rows.at(sess.O.fr).id); 
            sess.O.mode = NORMAL;
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
              get_preview(sess.O.rows.at(sess.O.fr).id);
              sess.O.mode = NORMAL;
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
          if (s != prevfilename) {
            completions.clear();
            completion_index = 0;
            std::string path;
            if (s.front() == '/') {
              size_t pos = s.find_last_of('/');
              prefix = s.substr(0, pos+1);
              path = prefix;
              s = s.substr(pos+1);
              //assume below we want current_directory if what's typed isn't ~/.. or /..
            } else path = std::filesystem::current_path().string(); 

            std::filesystem::path pathToShow(path);  
            for (const auto& entry : std::filesystem::directory_iterator(pathToShow)) {
              const auto filename = entry.path().filename().string();
              if (cmd.starts_with("save") && !entry.is_directory()) continue; ///////////// 11-21-2020 
              if (filename.starts_with(s)) {
                if (entry.is_directory()) completions.push_back(filename+'/');
                else completions.push_back(filename);
              }
            }  
          }  
          // below is where we present/cycle through completions  
          if (!completions.empty())  {
            if (completion_index == completions.size()) completion_index = 0;
            prevfilename = prefix + completions.at(completion_index++);
            //p->command_line = "readfile " + prevfilename;
            sess.p->command_line = fmt::format("{} {}", cmd, prevfilename);
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
            //update_note(p->is_subeditor, true); //should be p->E_write_C(); closing_editor = true;
            update_note(false, true); //should be p->E_write_C(); closing_editor = true;
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

            /*
            std::unordered_set<int> temp;
            for (auto z : sess.editors) {
              temp.insert(z->id);
            }

            int s_cols = -1 + (sess.screencols - sess.divider)/temp.size();
            temp.clear();
            int i = -1;
            for (auto z : sess.editors) {
              auto ret = temp.insert(z->id);
              if (ret.second == true) i++;
              z->left_margin = sess.divider + i*s_cols + i;
              z->screencols = s_cols;
              z->setLinesMargins(); //also sets top margin
            }
            */
            sess.positionEditors();
            sess.eraseRightScreen(); //moved down here on 10-24-2020
            sess.drawEditors();

          } else { // we've quit the last remaining editor(s)
            sess.p = nullptr;
            sess.editor_mode = false;
            sess.eraseRightScreen();
            get_preview(sess.O.rows.at(sess.O.fr).id);
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
  sess.O.cx = 0; //cursor x position
  sess.O.cy = 0; //cursor y position
  sess.O.fc = 0; //file x position
  sess.O.fr = 0; //file y position
  sess.O.rowoff = 0;  //number of rows scrolled off the screen
  sess.O.coloff = 0;  //col the user is currently scrolled to  
  sess.O.sort = "modified";
  sess.O.show_deleted = false; //not treating these separately right now
  sess.O.show_completed = true;
  sess.O.message[0] = '\0'; //very bottom of screen; ex. -- INSERT --
  sess.O.highlight[0] = sess.O.highlight[1] = -1;
  sess.O.mode = NORMAL; //0=normal; 1=insert; 2=command line; 3=visual line; 4=visual; 5='r' 
  sess.O.last_mode = NORMAL;
  sess.O.command[0] = '\0';
  sess.O.command_line = "";
  sess.O.repeat = 0; //number of times to repeat commands like x,s,yy also used for visual line mode x,y

  sess.O.view = TASK; // not necessary here since set when searching database
  sess.O.taskview = BY_FOLDER;
  sess.O.folder = "todo";
  sess.O.context = "No Context";
  sess.O.keyword = "";

  // ? where this should be.  Also in signal.
  sess.textlines = sess.screenlines - 2 - TOP_MARGIN; // -2 for status bar and message bar
  sess.divider = sess.screencols - c.ed_pct * sess.screencols/100;
  sess.totaleditorcols = sess.screencols - sess.divider - 1; // was 2 
}

int main(int argc, char** argv) { 

  spdlog::flush_every(std::chrono::seconds(5)); //////
  spdlog::set_level(spdlog::level::info); //warn, error, info, off, debug
  logger->info("********************** New Launch **************************");

  publisher.bind("tcp://*:5556");
  //publisher.bind("ipc://scroll.ipc"); //10132020 -> not sure why I thought I needed this

  sess.lock.l_whence = SEEK_SET;
  sess.lock.l_start = 0;
  sess.lock.l_len = 0;
  sess.lock.l_pid = getpid();

  if (argc > 1 && argv[1][0] == '-') lm_browser = false;

  db_open(); //for sqlite
  sess.db_open(); //for sqlite
  get_conn(); //for pg
  load_meta(); //meta html for lm_browser 

  //which_db = SQLITE; //this can go since not using postgres on client

  //map_context_titles();
  sess.generateContextMap();
  //map_folder_titles();
  sess.generateFolderMap();

  sess.getWindowSize();
  enableRawMode();
  initOutline();
  sess.eraseScreenRedrawLines();
  //Editor::origin = sess.divider + 1; //only used in Editor.cpp
  get_items(MAX);
  sess.command_history.push_back("of todo"); //klugy - this could be read from config and generalized
  sess.page_history.push_back("of todo"); //klugy - this could be read from config and generalized
  
  signal(SIGWINCH, signalHandler);
  //bool text_change;
  //bool scroll;
  //bool redraw;

  //sess.O.outlineRefreshScreen(); 
  sess.refreshOrgScreen();
  sess.drawOrgStatusBar();
  sess.showOrgMessage3("rows: {}  columns: {}", sess.screenlines, sess.screencols);
  sess.returnCursor();

  if (lm_browser) std::system("./lm_browser current.html &"); //&=> returns control

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
      if (lm_browser && scroll) {
        zmq::message_t message(20);
        snprintf ((char *) message.data(), 20, "%d", sess.p->line_offset*25); //25 - complete hack but works ok
        publisher.send(message, zmq::send_flags::dontwait);
      }
      ////////////////////

    } else if (sess.O.mode != FILE_DISPLAY) { 
      outlineProcessKeypress();
      sess.O.outlineScroll();
      //sess.O.outlineRefreshScreen(); // now just draws rows
      sess.refreshOrgScreen();
    } else outlineProcessKeypress(); // only do this if in FILE_DISPLAY mode

    sess.drawOrgStatusBar();
    sess.returnCursor();
  }
  
  lsp_shutdown("all");


  write(STDOUT_FILENO, "\x1b[2J", 4); //clears the screen
  write(STDOUT_FILENO, "\x1b[H", 3); //send cursor home
  Py_FinalizeEx();
  sqlite3_close(S.db);
  PQfinish(conn);

  return 0;
}
