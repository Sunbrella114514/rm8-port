/* Builtin module table for the RichMan8 port's embedded CPython 2.4.6 (replaces Modules/config.c).
 * The game's own modules (Role, StPoint, Terra, ...) are added with PyImport_AppendInittab by the port. */
#include "Python.h"

extern void PyMarshal_Init(void);
extern void initimp(void);
extern void initgc(void);
extern void init_random(void);
extern void initmath(void);
extern void inittime(void);
extern void initoperator(void);
extern void init_codecs(void);
extern void initerrno(void);

struct _inittab _PyImport_Inittab[] = {
	{"marshal", PyMarshal_Init},
	{"imp", initimp},
	{"__main__", NULL},
	{"__builtin__", NULL},
	{"sys", NULL},
	{"exceptions", NULL},
	{"gc", initgc},
	{"_random", init_random},
	{"math", initmath},
	{"time", inittime},
	{"operator", initoperator},
	{"_codecs", init_codecs},
	{"errno", initerrno},
	{0, 0}
};
