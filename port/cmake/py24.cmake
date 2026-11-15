# Embedded CPython 2.4.6 (static library py24): the same Python version as the game's Python24.dll, so the original
# Pysrc\*.pyc bytecode (magic 62061) runs unchanged (AI, cards, cutscenes).
# Hand-written source list and pyconfig.h (extern/cpython24/pyconfig.h): interpreter core + a few builtin modules.
set(PY24 ${CMAKE_CURRENT_LIST_DIR}/../extern/cpython24)
set(PY24_PARSER acceler bitset firstsets grammar grammar1 listnode metagrammar myreadline node parser parsetok tokenizer intrcheck)
set(PY24_PYTHON bltinmodule exceptions ceval compile codecs errors frozen future getargs getcompiler getcopyright
    getmtime getplatform getversion graminit import marshal modsupport mystrtoul mysnprintf pyfpe pystate pythonrun
    structmember symtable sysmodule traceback getopt pystrtod sigcheck dynload_stub)
set(PY24_OBJECTS abstract boolobject bufferobject cellobject classobject cobject complexobject descrobject dictobject
    enumobject fileobject floatobject frameobject funcobject genobject intobject iterobject listobject longobject
    methodobject moduleobject object obmalloc rangeobject setobject sliceobject stringobject structseq tupleobject
    typeobject unicodectype unicodeobject weakrefobject)
set(PY24_MODULES getbuildinfo gcmodule _randommodule mathmodule timemodule operator _codecsmodule errnomodule)
set(PY24_SRC ${PY24}/rm8_config.c ${PY24}/rm8_getpath.c)
foreach(f ${PY24_PARSER})
  list(APPEND PY24_SRC ${PY24}/Parser/${f}.c)
endforeach()
foreach(f ${PY24_PYTHON})
  list(APPEND PY24_SRC ${PY24}/Python/${f}.c)
endforeach()
foreach(f ${PY24_OBJECTS})
  list(APPEND PY24_SRC ${PY24}/Objects/${f}.c)
endforeach()
foreach(f ${PY24_MODULES})
  list(APPEND PY24_SRC ${PY24}/Modules/${f}.c)
endforeach()
add_library(py24 STATIC ${PY24_SRC})
target_include_directories(py24 PUBLIC ${PY24}/Include ${PY24})
target_compile_definitions(py24 PRIVATE Py_BUILD_CORE NDEBUG)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  # old C code: silence the noise, keep the semantics of the original build (no strict aliasing, wrapping ints)
  target_compile_options(py24 PRIVATE -w -fno-strict-aliasing -fwrapv -std=gnu89)
endif()
if(NOT WIN32)
  target_link_libraries(py24 PUBLIC m)
endif()
