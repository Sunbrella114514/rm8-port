// save / load hooks of python.cpp (included at the end of python.cpp): the state of Python's `random` module. The AI scripts (AI_NN.py, CardPy_NN.py) draw their
// decisions from it (`from random import *`), so a loaded game only continues the way the saved one would with this stream restored. random.getstate() in Python 2.4
// is (version, a tuple of 625 integers, gauss_next).
#include "save.h"

void rm8SavePyRandomWrite(Rm8SaveW &w)
{
	std::vector<unsigned> mt; double gauss = 0; bool hasGauss = false; int version = 1;
	if(g_ready){
		PyObject *m = PyImport_ImportModule((char*)"random");
		PyObject *st = m ? PyObject_CallMethod(m, (char*)"getstate", nullptr) : nullptr;
		if(st && PyTuple_Check(st) && PyTuple_Size(st) == 3){
			PyObject *v = PyTuple_GetItem(st, 0), *t = PyTuple_GetItem(st, 1), *g = PyTuple_GetItem(st, 2);
			if(PyInt_Check(v)) version = (int)PyInt_AsLong(v);
			if(PyTuple_Check(t) && PyTuple_Size(t) == 625){
				for(int i = 0; i < 625; i++){
					PyObject *e = PyTuple_GetItem(t, i);
					mt.push_back(PyLong_Check(e) ? (unsigned)PyLong_AsUnsignedLongMask(e) : (unsigned)PyInt_AsUnsignedLongMask(e));
				}
			}
			if(g && g != Py_None && PyFloat_Check(g)){ hasGauss = true; gauss = PyFloat_AsDouble(g); }
		}
		PyErr_Clear();
		Py_XDECREF(st); Py_XDECREF(m);
	}
	w.i32(mt.size() == 625 ? 1 : 0); w.i32(version);
	if(mt.size() == 625){ for(unsigned v : mt) w.u32(v); w.i32(hasGauss ? 1 : 0); w.raw(&gauss, 8); }
}

void rm8SavePyRandomRead(Rm8SaveR &r)
{
	int have = r.i32(), version = r.i32();
	if(!have || r.bad || !g_ready) return;
	PyObject *t = PyTuple_New(625);
	for(int i = 0; i < 625; i++){	// the state words are C longs of the interpreter (32 bit on Windows: negative above 2^31; 64 bit on Android: positive)
		unsigned v = r.u32();
		PyTuple_SetItem(t, i, PyInt_FromLong(sizeof(long) == 4 ? (long)(int)v : (long)v));
	}
	int hasGauss = r.i32(); double gauss = 0; r.raw(&gauss, 8);
	if(r.bad){ Py_DECREF(t); return; }
	PyObject *g = hasGauss ? PyFloat_FromDouble(gauss) : (Py_INCREF(Py_None), Py_None);
	PyObject *st = Py_BuildValue((char*)"(iOO)", version, t, g);
	Py_DECREF(t); Py_DECREF(g);
	PyObject *m = PyImport_ImportModule((char*)"random");
	PyObject *res = m && st ? PyObject_CallMethod(m, (char*)"setstate", (char*)"(O)", st) : nullptr;
	if(!res) logError("random.setstate");
	Py_XDECREF(res); Py_XDECREF(st); Py_XDECREF(m);
}

// test hook RM8_SEED=<n>: every new game seeds Python's random module (reproducible AI decisions); see rm8SaveGameNewGame
void rm8SavePySeed(unsigned seed)
{
	if(!g_ready) return;
	PyObject *m = PyImport_ImportModule((char*)"random");
	PyObject *res = m ? PyObject_CallMethod(m, (char*)"seed", (char*)"(l)", (long)seed) : nullptr;
	if(!res) logError("random.seed");
	Py_XDECREF(res); Py_XDECREF(m);
}

static void savePyExtWrite(Rm8SaveW &w){ rm8SavePyRandomWrite(w); }
static void savePyExtRead(Rm8SaveR &r){ rm8SavePyRandomRead(r); }
RM8_SAVE_EXT(PYRN, 5, savePyExtWrite, savePyExtRead);
