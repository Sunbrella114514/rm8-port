/* Hand-written pyconfig.h for the RichMan8 port's embedded CPython 2.4.6 (static, no threads, no dynamic
 * loading, no site). Targets: Android arm64 (bionic, clang) and Windows x86_64 (llvm-mingw, UCRT).
 * Windows is built as a POSIX-like platform (MS_WINDOWS left undefined): only the interpreter core and a few
 * builtin modules are compiled, none of which needs the Win32 API. */
#ifndef Py_PYCONFIG_H
#define Py_PYCONFIG_H

#define PLATFORM "rm8"
#if defined(_WIN32)
#define DELIM ';'	/* sys.path separator (drive letters contain colons) */
#endif

/* sizes */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#if defined(_WIN32)
#define SIZEOF_LONG 4
#define SIZEOF_WCHAR_T 2
#else
#define SIZEOF_LONG 8
#define SIZEOF_WCHAR_T 4
#endif
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_VOID_P 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_UINTPTR_T 8
#define SIZEOF_OFF_T 8
#define SIZEOF_TIME_T 8
#define SIZEOF_FPOS_T 8
#define SIZEOF_PID_T 4
#define SIZEOF__BOOL 1
#define HAVE_LONG_LONG 1
#define HAVE_UINTPTR_T 1
#define HAVE_INTPTR_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT64_T 1
#define HAVE_INT32_T 1
#define HAVE_INT64_T 1
#define HAVE_SSIZE_T 1
#define HAVE_C99_BOOL 1
#define PY_FORMAT_LONG_LONG "ll"
#define PY_FORMAT_SIZE_T "z"

/* floating point */
#define DOUBLE_IS_LITTLE_ENDIAN_IEEE754 1
#define HAVE_DECL_ISINF 1
#define HAVE_DECL_ISNAN 1
#define HAVE_DECL_ISFINITE 1
#define HAVE_HYPOT 1
#define HAVE_COPYSIGN 1
#define HAVE_ROUND 1
#define HAVE_ACOSH 1
#define HAVE_ASINH 1
#define HAVE_ATANH 1
#define HAVE_LOG1P 1
#define HAVE_EXPM1 1
#define HAVE_ERF 1
#define HAVE_ERFC 1
#define HAVE_LGAMMA 1
#define HAVE_TGAMMA 1
#define HAVE_FINITE 1
#define HAVE_GAMMA 0
#undef HAVE_GAMMA

/* unicode: UCS-2 everywhere */
#define Py_USING_UNICODE 1
#define Py_UNICODE_SIZE 2
#define PY_UNICODE_TYPE unsigned short

/* headers and functions */
#define STDC_HEADERS 1
#define HAVE_STDARG_PROTOTYPES 1
#define HAVE_PROTOTYPES 1
#define HAVE_UNISTD_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDDEF_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_LOCALE_H 1
#define HAVE_WCHAR_H 1
#define HAVE_STAT 1
#define HAVE_FSTAT 1
#define HAVE_STRDUP 1
#define HAVE_STRFTIME 1
#define HAVE_CLOCK 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_MKTIME 1
#define HAVE_FTIME 0
#undef HAVE_FTIME
#define HAVE_ALTZONE 0
#undef HAVE_ALTZONE

/* features */
#define WITH_DOC_STRINGS 1
#define WITH_PYMALLOC 1
#undef WITH_THREAD
#undef HAVE_DYNAMIC_LOADING
#undef Py_DEBUG

#endif
