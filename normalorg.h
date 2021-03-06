#ifndef NORMALORG_H
#define NORMALORG_H

#include <unordered_map>
#include <string>
#define CTRL_KEY(k) ((k) & 0x1f) // 0x1f is 31; first ascii is 32 space anding removes all higher bits Editor.cpp needs this

using zfunc = void (*)(void);

/* OUTLINE mode NORMAL functions */
void insert_N(void);
void goto_editor_N(void); //should this be in case NORMAL as ctrl_key('l')?
void return_N(void);
void w_N(void);
void s_N(void);
void x_N(void);
void r_N(void);
void tilde_N(void);
void a_N(void);
void A_N(void);
void b_N(void);
void e_N(void);
void zero_N(void);
void dollar_N(void);
void I_N(void);
void G_N(void);
//void O_N(void); probably better just to use :n
void colon_N(void);
void v_N(void);
void p_N(void);
void asterisk_N(void);
void m_N(void);
void n_N(void);
void u_N(void);
void dd_N(void);
void star_N(void); //toggleStar
void completed_N(void);
void daw_N(void);
void caw_N(void);
void dw_N(void);
void cw_N(void);
void de_N(void);
void d$_N(void);
void gg_N(void);
void gt_N(void);
void info_N(void); //ctrl-i in NORMAL mode 0x9

/* OUTLINE NORMAL mode command lookup */
const std::unordered_map<std::string, zfunc> n_lookup {
  //{{0xC}, goto_editor_N}, //also works
  {{CTRL_KEY('l')}, goto_editor_N},
  {{0x17,0x17}, goto_editor_N},
  {"\r", return_N}, //return_O
  {"i", insert_N},
  {"s", s_N},
  {"~", tilde_N},
  {"r", r_N},
  {"a", a_N},
  {"A", A_N},
  {"x", x_N},
  {"w", w_N},

  {"daw", daw_N},
  {"dw", dw_N},
  {"daw", caw_N},
  {"dw", cw_N},
  {"de", de_N},
  {"d$", d$_N},

  {"gg", gg_N},

  {"gt", gt_N},

  {{CTRL_KEY('i')}, info_N}, //{{0x9}}
  {"b", b_N},
  {"e", e_N},
  {"0", zero_N},
  {"$", dollar_N},
  {"I", I_N},
  {"G", G_N},
  //{"O", O_N}, //probably better just to use :n
  {":", colon_N},
  {"v", v_N},
  {"p", p_N},
  {"*", asterisk_N},
  {"m", m_N},
  {"n", n_N},
  {"u", u_N},
  {"dd", dd_N},
  {{0x4}, dd_N}, //ctrl-d
  {{0x2}, star_N}, //ctrl-b -probably want this go backwards (unimplemented) and use ctrl-e for this
  {{0x18}, completed_N} //ctrl-x
};
#endif
