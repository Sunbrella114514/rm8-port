/* rm8_pytest: smoke test of the embedded CPython 2.4 with the original game scripts (Pysrc*.pyc).
 * usage: rm8_pytest <game_dir>/Pysrc
 * Imports the game's own random.py and AI_01.py against stub Role/StPoint/Terra/Npc/Stock modules and calls a few
 * AI callbacks, checks Python 2 integer division and long arithmetic. */
#include "Python.h"
#include <stdio.h>
#include <string.h>

extern void rm8_py_set_path(const char *p);

static PyObject *ret_int(PyObject *self, PyObject *args){ (void)self; (void)args; return PyInt_FromLong(0); }
static PyObject *cash(PyObject *self, PyObject *args){ (void)self; (void)args; return PyInt_FromLong(12345); }
static PyObject *price(PyObject *self, PyObject *args){ (void)self; (void)args; return PyInt_FromLong(1); }
static PyObject *day(PyObject *self, PyObject *args){ (void)self; (void)args; return PyInt_FromLong(10); }
static PyMethodDef role_methods[] = {
	{"CurRole", ret_int, METH_VARARGS, NULL}, {"Shape", ret_int, METH_VARARGS, NULL},
	{"Cash", cash, METH_VARARGS, NULL}, {"Fund", cash, METH_VARARGS, NULL}, {"HasCard", ret_int, METH_VARARGS, NULL},
	{"PriceIndex", price, METH_VARARGS, NULL}, {"GameDay", day, METH_VARARGS, NULL},
	{NULL, NULL, 0, NULL}
};
static PyMethodDef none_methods[] = {{NULL, NULL, 0, NULL}};
static void initRole(void){ Py_InitModule("Role", role_methods); }
static void initStPoint(void){ Py_InitModule("StPoint", none_methods); }
static void initTerra(void){ Py_InitModule("Terra", none_methods); }
static void initNpc(void){ Py_InitModule("Npc", none_methods); }
static void initStock(void){ Py_InitModule("Stock", none_methods); }

int main(int argc, char **argv)
{
	if(argc < 2){ fprintf(stderr, "usage: %s script_dir\n", argv[0]); return 2; }
	rm8_py_set_path(argv[1]);
	Py_NoSiteFlag = 1; Py_IgnoreEnvironmentFlag = 1; Py_FrozenFlag = 1;
	PyImport_AppendInittab("Role", initRole); PyImport_AppendInittab("StPoint", initStPoint);
	PyImport_AppendInittab("Terra", initTerra); PyImport_AppendInittab("Npc", initNpc); PyImport_AppendInittab("Stock", initStock);
	Py_Initialize();
	int rc = PyRun_SimpleString(
		"import sys\n"
		"print 'python', sys.version.split()[0], 'path', sys.path\n"
		"print '7/2 =', 7/2, ' -7/2 =', -7/2, ' 2**70 =', 2**70\n"
		"import random\n"
		"print 'random from', random.__file__\n"
		"random.seed(1)\n"
		"print 'randint(0,20) x5 after seed(1):', [random.randint(0, 20) for i in range(5)]\n"
		"import AI_01\n"
		"print 'AI_01 from', AI_01.__file__\n"
		"print 'TBuy ->', AI_01.TBuy(0), ' Answer ->', AI_01.Answer(0), ' Access ->', AI_01.Access(0)\n"
		"print 'OK'\n");
	Py_Finalize();
	return rc == 0 ? 0 : 1;
}
