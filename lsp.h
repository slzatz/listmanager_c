#ifndef LSP_H
#define LSP_H

#include <string>
#include <thread>
#include "pstream.h"

//using namespace redi;

struct Lsp {
  std::jthread thred;
  std::string name{};
  std::string file_name{};
  std::string client_uri{};
  std::string language{};
  std::atomic<bool> code_changed = false;
  std::atomic<bool> closed = true;

};

void lsp_thread(std::stop_token st);
void lsp_start(Lsp * lsp);
void readsome(redi::pstream &ls, int i);
void lspStart(std::string &name);
void lsp_shutdown(const std::string s);
void initLogger(void);
#endif
