// save / load hooks of news.cpp (included at the end of news.cpp): the last picks of News_SelectRandomNew ([V] DAT_00604520, a process-wide global of the original: the
// weekly news does not repeat the recent ones) as an extension chunk
#include "save.h"
static void saveNewsExtWrite(Rm8SaveW &w){ for(int v : news::g_hist) w.i32(v); }
static void saveNewsExtRead(Rm8SaveR &r){ for(int &v : news::g_hist) r.opt(v); }
RM8_SAVE_EXT(NEWS, 50, saveNewsExtWrite, saveNewsExtRead);
