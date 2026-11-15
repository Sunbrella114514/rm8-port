/* Path functions normally provided by Modules/getpath.c: the port sets sys.path itself (rm8_py_set_path). */
#include "Python.h"

static char rm8_path[4096] = ".";
static char rm8_prog[] = "rm8";
static char rm8_prefix[] = "";

void rm8_py_set_path(const char *p){ strncpy(rm8_path, p, sizeof rm8_path - 1); }
char *Py_GetPath(void){ return rm8_path; }
char *Py_GetPrefix(void){ return rm8_prefix; }
char *Py_GetExecPrefix(void){ return rm8_prefix; }
char *Py_GetProgramFullPath(void){ return rm8_prog; }
