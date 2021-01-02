#ifndef SESSION_H
#define SESSION_H
#include "Editor.h"
#include <vector>

struct Session {
  int screencols;
  int screenlines;
  int divider;
  int totaleditorcols;
  std::vector<Editor*> editors;

  void eraseRightScreen(void);
  void draw_editors(void);
};

//inline Session sess;
extern Session sess;
#endif
