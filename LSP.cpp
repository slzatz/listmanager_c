#include "LSP.h"
//#include "pstream.h" //in header
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/cfg/env.h>
//#include "Common.h"
#include "session.h"

//using namespace redi; in header!
using json = nlohmann::json;
using namespace redi;

auto logger = spdlog::basic_logger_mt("lm_logger", "lm_log"); //logger name, file name

void initLogger(void) {
spdlog::flush_every(std::chrono::seconds(5)); //////
spdlog::set_level(spdlog::level::info); //warn, error, info, off, debug
}

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
  Lsp *lsp = sess.lsp_v.back();
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
  
//void lsp_start(Lsp * lsp) {
void lspStart(std::string &name) {
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
    return;
  }

  logger->info("********************** New Launch: {}**************************\n\n", lsp->name);
  lsp->closed = false;
  sess.lsp_v.push_back(lsp);
  lsp->thred = std::jthread(lsp_thread);
}

void lsp_shutdown(const std::string s) {
  //if (lsp_map.empty()) return;
  if (sess.lsp_v.empty()) return;

  // assuming all for the moment
  //for (auto & [k,lsp] : lsp_map) {
  for (auto & lsp : sess.lsp_v) {
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

