/*
 * C Extension module to test Python internal C APIs (Include/internal).
 */

#ifndef Py_BUILD_CORE_BUILTIN
#  define Py_BUILD_CORE_MODULE 1
#endif

/* Always enable assertions */
#undef NDEBUG

#define PY_SSIZE_T_CLEAN

#include "Python.h"
#include "pycore_atomic_funcs.h" // _Py_atomic_int_get()
#include "pycore_bitutils.h"     // _Py_bswap32()
#include "pycore_compile.h"      // _PyCompile_OptimizeCfg()
#include "pycore_fileutils.h"    // _Py_normpath
#include "pycore_frame.h"        // _PyInterpreterFrame
#include "pycore_gc.h"           // PyGC_Head
#include "pycore_hashtable.h"    // _Py_hashtable_new()
#include "pycore_initconfig.h"   // _Py_GetConfigsAsDict()
#include "pycore_pathconfig.h"   // _PyPathConfig_ClearGlobal()
#include "pycore_interp.h"       // _PyInterpreterState_GetConfigCopy()
#include "pycore_pyerrors.h"     // _Py_UTF8_Edit_Cost()
#include "pycore_pystate.h"      // _PyThreadState_GET()
#include "osdefs.h"              // MAXPATHLEN

#include "clinic/_testinternalcapi.c.h"

/*[clinic input]
module _testinternalcapi
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=7bb583d8c9eb9a78]*/
static PyObject *
get_configs(PyObject *self, PyObject *Py_UNUSED(args))
{
    return _Py_GetConfigsAsDict();
}


static PyObject*
get_recursion_depth(PyObject *self, PyObject *Py_UNUSED(args))
{
    PyThreadState *tstate = _PyThreadState_GET();

    /* subtract one to ignore the frame of the get_recursion_depth() call */

    return PyLong_FromLong(tstate->recursion_limit - tstate->recursion_remaining - 1);
}


static PyObject*
test_bswap(PyObject *self, PyObject *Py_UNUSED(args))
{
    uint16_t u16 = _Py_bswap16(UINT16_C(0x3412));
    if (u16 != UINT16_C(0x1234)) {
        PyErr_Format(PyExc_AssertionError,
                     "_Py_bswap16(0x3412) returns %u", u16);
        return NULL;
    }

    uint32_t u32 = _Py_bswap32(UINT32_C(0x78563412));
    if (u32 != UINT32_C(0x12345678)) {
        PyErr_Format(PyExc_AssertionError,
                     "_Py_bswap32(0x78563412) returns %lu", u32);
        return NULL;
    }

    uint64_t u64 = _Py_bswap64(UINT64_C(0xEFCDAB9078563412));
    if (u64 != UINT64_C(0x1234567890ABCDEF)) {
        PyErr_Format(PyExc_AssertionError,
                     "_Py_bswap64(0xEFCDAB9078563412) returns %llu", u64);
        return NULL;
    }

    Py_RETURN_NONE;
}


static int
check_popcount(uint32_t x, int expected)
{
    // Use volatile to prevent the compiler to optimize out the whole test
    volatile uint32_t u = x;
    int bits = _Py_popcount32(u);
    if (bits != expected) {
        PyErr_Format(PyExc_AssertionError,
                     "_Py_popcount32(%lu) returns %i, expected %i",
                     (unsigned long)x, bits, expected);
        return -1;
    }
    return 0;
}


static PyObject*
test_popcount(PyObject *self, PyObject *Py_UNUSED(args))
{
#define CHECK(X, RESULT) \
    do { \
        if (check_popcount(X, RESULT) < 0) { \
            return NULL; \
        } \
    } while (0)

    CHECK(0, 0);
    CHECK(1, 1);
    CHECK(0x08080808, 4);
    CHECK(0x10000001, 2);
    CHECK(0x10101010, 4);
    CHECK(0x10204080, 4);
    CHECK(0xDEADCAFE, 22);
    CHECK(0xFFFFFFFF, 32);
    Py_RETURN_NONE;

#undef CHECK
}


static int
check_bit_length(unsigned long x, int expected)
{
    // Use volatile to prevent the compiler to optimize out the whole test
    volatile unsigned long u = x;
    int len = _Py_bit_length(u);
    if (len != expected) {
        PyErr_Format(PyExc_AssertionError,
                     "_Py_bit_length(%lu) returns %i, expected %i",
                     x, len, expected);
        return -1;
    }
    return 0;
}


static PyObject*
test_bit_length(PyObject *self, PyObject *Py_UNUSED(args))
{
#define CHECK(X, RESULT) \
    do { \
        if (check_bit_length(X, RESULT) < 0) { \
            return NULL; \
        } \
    } while (0)

    CHECK(0, 0);
    CHECK(1, 1);
    CHECK(0x1000, 13);
    CHECK(0x1234, 13);
    CHECK(0x54321, 19);
    CHECK(0x7FFFFFFF, 31);
    CHECK(0xFFFFFFFF, 32);
    Py_RETURN_NONE;

#undef CHECK
}


#define TO_PTR(ch) ((void*)(uintptr_t)ch)
#define FROM_PTR(ptr) ((uintptr_t)ptr)
#define VALUE(key) (1 + ((int)(key) - 'a'))

static Py_uhash_t
hash_char(const void *key)
{
    char ch = (char)FROM_PTR(key);
    return ch;
}


static int
hashtable_cb(_Py_hashtable_t *table,
             const void *key_ptr, const void *value_ptr,
             void *user_data)
{
    int *count = (int *)user_data;
    char key = (char)FROM_PTR(key_ptr);
    int value = (int)FROM_PTR(value_ptr);
    assert(value == VALUE(key));
    *count += 1;
    return 0;
}


static PyObject*
test_hashtable(PyObject *self, PyObject *Py_UNUSED(args))
{
    _Py_hashtable_t *table = _Py_hashtable_new(hash_char,
                                               _Py_hashtable_compare_direct);
    if (table == NULL) {
        return PyErr_NoMemory();
    }

    // Using an newly allocated table must not crash
    assert(table->nentries == 0);
    assert(table->nbuckets > 0);
    assert(_Py_hashtable_get(table, TO_PTR('x')) == NULL);

    // Test _Py_hashtable_set()
    char key;
    for (key='a'; key <= 'z'; key++) {
        int value = VALUE(key);
        if (_Py_hashtable_set(table, TO_PTR(key), TO_PTR(value)) < 0) {
            _Py_hashtable_destroy(table);
            return PyErr_NoMemory();
        }
    }
    assert(table->nentries == 26);
    assert(table->nbuckets > table->nentries);

    // Test _Py_hashtable_get_entry()
    for (key='a'; key <= 'z'; key++) {
        _Py_hashtable_entry_t *entry = _Py_hashtable_get_entry(table, TO_PTR(key));
        assert(entry != NULL);
        assert(entry->key == TO_PTR(key));
        assert(entry->value == TO_PTR(VALUE(key)));
    }

    // Test _Py_hashtable_get()
    for (key='a'; key <= 'z'; key++) {
        void *value_ptr = _Py_hashtable_get(table, TO_PTR(key));
        assert((int)FROM_PTR(value_ptr) == VALUE(key));
    }

    // Test _Py_hashtable_steal()
    key = 'p';
    void *value_ptr = _Py_hashtable_steal(table, TO_PTR(key));
    assert((int)FROM_PTR(value_ptr) == VALUE(key));
    assert(table->nentries == 25);
    assert(_Py_hashtable_get_entry(table, TO_PTR(key)) == NULL);

    // Test _Py_hashtable_foreach()
    int count = 0;
    int res = _Py_hashtable_foreach(table, hashtable_cb, &count);
    assert(res == 0);
    assert(count == 25);

    // Test _Py_hashtable_clear()
    _Py_hashtable_clear(table);
    assert(table->nentries == 0);
    assert(table->nbuckets > 0);
    assert(_Py_hashtable_get(table, TO_PTR('x')) == NULL);

    _Py_hashtable_destroy(table);
    Py_RETURN_NONE;
}


static PyObject *
test_get_config(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(args))
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    if (_PyInterpreterState_GetConfigCopy(&config) < 0) {
        PyConfig_Clear(&config);
        return NULL;
    }
    PyObject *dict = _PyConfig_AsDict(&config);
    PyConfig_Clear(&config);
    return dict;
}


static PyObject *
test_set_config(PyObject *Py_UNUSED(self), PyObject *dict)
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    if (_PyConfig_FromDict(&config, dict) < 0) {
        goto error;
    }
    if (_PyInterpreterState_SetConfig(&config) < 0) {
        goto error;
    }
    PyConfig_Clear(&config);
    Py_RETURN_NONE;

error:
    PyConfig_Clear(&config);
    return NULL;
}


static PyObject *
test_reset_path_config(PyObject *Py_UNUSED(self), PyObject *Py_UNUSED(arg))
{
    _PyPathConfig_ClearGlobal();
    Py_RETURN_NONE;
}


static PyObject*
test_atomic_funcs(PyObject *self, PyObject *Py_UNUSED(args))
{
    // Test _Py_atomic_size_get() and _Py_atomic_size_set()
    Py_ssize_t var = 1;
    _Py_atomic_size_set(&var, 2);
    assert(_Py_atomic_size_get(&var) == 2);
    Py_RETURN_NONE;
}


static int
check_edit_cost(const char *a, const char *b, Py_ssize_t expected)
{
    int ret = -1;
    PyObject *a_obj = NULL;
    PyObject *b_obj = NULL;

    a_obj = PyUnicode_FromString(a);
    if (a_obj == NULL) {
        goto exit;
    }
    b_obj = PyUnicode_FromString(b);
    if (b_obj == NULL) {
        goto exit;
    }
    Py_ssize_t result = _Py_UTF8_Edit_Cost(a_obj, b_obj, -1);
    if (result != expected) {
        PyErr_Format(PyExc_AssertionError,
                     "Edit cost from '%s' to '%s' returns %zd, expected %zd",
                     a, b, result, expected);
        goto exit;
    }
    // Check that smaller max_edits thresholds are exceeded.
    Py_ssize_t max_edits = result;
    while (max_edits > 0) {
        max_edits /= 2;
        Py_ssize_t result2 = _Py_UTF8_Edit_Cost(a_obj, b_obj, max_edits);
        if (result2 <= max_edits) {
            PyErr_Format(PyExc_AssertionError,
                         "Edit cost from '%s' to '%s' (threshold %zd) "
                         "returns %zd, expected greater than %zd",
                         a, b, max_edits, result2, max_edits);
            goto exit;
        }
    }
    // Check that bigger max_edits thresholds don't change anything
    Py_ssize_t result3 = _Py_UTF8_Edit_Cost(a_obj, b_obj, result * 2 + 1);
    if (result3 != result) {
        PyErr_Format(PyExc_AssertionError,
                     "Edit cost from '%s' to '%s' (threshold %zd) "
                     "returns %zd, expected %zd",
                     a, b, result * 2, result3, result);
        goto exit;
    }
    ret = 0;
exit:
    Py_XDECREF(a_obj);
    Py_XDECREF(b_obj);
    return ret;
}

static PyObject *
test_edit_cost(PyObject *self, PyObject *Py_UNUSED(args))
{
    #define CHECK(a, b, n) do {              \
        if (check_edit_cost(a, b, n) < 0) {  \
            return NULL;                     \
        }                                    \
    } while (0)                              \

    CHECK("", "", 0);
    CHECK("", "a", 2);
    CHECK("a", "A", 1);
    CHECK("Apple", "Aple", 2);
    CHECK("Banana", "B@n@n@", 6);
    CHECK("Cherry", "Cherry!", 2);
    CHECK("---0---", "------", 2);
    CHECK("abc", "y", 6);
    CHECK("aa", "bb", 4);
    CHECK("aaaaa", "AAAAA", 5);
    CHECK("wxyz", "wXyZ", 2);
    CHECK("wxyz", "wXyZ123", 8);
    CHECK("Python", "Java", 12);
    CHECK("Java", "C#", 8);
    CHECK("AbstractFoobarManager", "abstract_foobar_manager", 3+2*2);
    CHECK("CPython", "PyPy", 10);
    CHECK("CPython", "pypy", 11);
    CHECK("AttributeError", "AttributeErrop", 2);
    CHECK("AttributeError", "AttributeErrorTests", 10);

    #undef CHECK
    Py_RETURN_NONE;
}


static PyObject *
normalize_path(PyObject *self, PyObject *filename)
{
    Py_ssize_t size = -1;
    wchar_t *encoded = PyUnicode_AsWideCharString(filename, &size);
    if (encoded == NULL) {
        return NULL;
    }

    PyObject *result = PyUnicode_FromWideChar(_Py_normpath(encoded, size), -1);
    PyMem_Free(encoded);

    return result;
}

static PyObject *
get_getpath_codeobject(PyObject *self, PyObject *Py_UNUSED(args)) {
    return _Py_Get_Getpath_CodeObject();
}


static PyObject *
encode_locale_ex(PyObject *self, PyObject *args)
{
    PyObject *unicode;
    int current_locale = 0;
    wchar_t *wstr;
    PyObject *res = NULL;
    const char *errors = NULL;

    if (!PyArg_ParseTuple(args, "U|is", &unicode, &current_locale, &errors)) {
        return NULL;
    }
    wstr = PyUnicode_AsWideCharString(unicode, NULL);
    if (wstr == NULL) {
        return NULL;
    }
    _Py_error_handler error_handler = _Py_GetErrorHandler(errors);

    char *str = NULL;
    size_t error_pos;
    const char *reason = NULL;
    int ret = _Py_EncodeLocaleEx(wstr,
                                 &str, &error_pos, &reason,
                                 current_locale, error_handler);
    PyMem_Free(wstr);

    switch(ret) {
    case 0:
        res = PyBytes_FromString(str);
        PyMem_RawFree(str);
        break;
    case -1:
        PyErr_NoMemory();
        break;
    case -2:
        PyErr_Format(PyExc_RuntimeError, "encode error: pos=%zu, reason=%s",
                     error_pos, reason);
        break;
    case -3:
        PyErr_SetString(PyExc_ValueError, "unsupported error handler");
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "unknown error code");
        break;
    }
    return res;
}


static PyObject *
decode_locale_ex(PyObject *self, PyObject *args)
{
    char *str;
    int current_locale = 0;
    PyObject *res = NULL;
    const char *errors = NULL;

    if (!PyArg_ParseTuple(args, "y|is", &str, &current_locale, &errors)) {
        return NULL;
    }
    _Py_error_handler error_handler = _Py_GetErrorHandler(errors);

    wchar_t *wstr = NULL;
    size_t wlen = 0;
    const char *reason = NULL;
    int ret = _Py_DecodeLocaleEx(str,
                                 &wstr, &wlen, &reason,
                                 current_locale, error_handler);

    switch(ret) {
    case 0:
        res = PyUnicode_FromWideChar(wstr, wlen);
        PyMem_RawFree(wstr);
        break;
    case -1:
        PyErr_NoMemory();
        break;
    case -2:
        PyErr_Format(PyExc_RuntimeError, "decode error: pos=%zu, reason=%s",
                     wlen, reason);
        break;
    case -3:
        PyErr_SetString(PyExc_ValueError, "unsupported error handler");
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "unknown error code");
        break;
    }
    return res;
}

static PyObject *record_list = NULL;

static PyObject *
set_eval_frame_default(PyObject *self, PyObject *Py_UNUSED(args))
{
    _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(), _PyEval_EvalFrameDefault);
    Py_CLEAR(record_list);
    Py_RETURN_NONE;
}

static PyObject *
record_eval(PyThreadState *tstate, struct _PyInterpreterFrame *f, int exc)
{
    PyList_Append(record_list, f->f_func->func_name);
    return _PyEval_EvalFrameDefault(tstate, f, exc);
}


static PyObject *
set_eval_frame_record(PyObject *self, PyObject *list)
{
    if (!PyList_Check(list)) {
        PyErr_SetString(PyExc_TypeError, "argument must be a list");
        return NULL;
    }
    Py_CLEAR(record_list);
    Py_INCREF(list);
    record_list = list;
    _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Get(), record_eval);
    Py_RETURN_NONE;
}


/*[clinic input]

_testinternalcapi.optimize_cfg -> object

  instructions: object
  consts: object

Apply compiler optimizations to an instruction list.
[clinic start generated code]*/

static PyObject *
_testinternalcapi_optimize_cfg_impl(PyObject *module, PyObject *instructions,
                                    PyObject *consts)
/*[clinic end generated code: output=5412aeafca683c8b input=7e8a3de86ebdd0f9]*/
{
    return _PyCompile_OptimizeCfg(instructions, consts);
}


static PyMethodDef TestMethods[] = {
    {"get_configs", get_configs, METH_NOARGS},
    {"get_recursion_depth", get_recursion_depth, METH_NOARGS},
    {"test_bswap", test_bswap, METH_NOARGS},
    {"test_popcount", test_popcount, METH_NOARGS},
    {"test_bit_length", test_bit_length, METH_NOARGS},
    {"test_hashtable", test_hashtable, METH_NOARGS},
    {"get_config", test_get_config, METH_NOARGS},
    {"set_config", test_set_config, METH_O},
    {"reset_path_config", test_reset_path_config, METH_NOARGS},
    {"test_atomic_funcs", test_atomic_funcs, METH_NOARGS},
    {"test_edit_cost", test_edit_cost, METH_NOARGS},
    {"normalize_path", normalize_path, METH_O, NULL},
    {"get_getpath_codeobject", get_getpath_codeobject, METH_NOARGS, NULL},
    {"EncodeLocaleEx", encode_locale_ex, METH_VARARGS},
    {"DecodeLocaleEx", decode_locale_ex, METH_VARARGS},
    {"set_eval_frame_default", set_eval_frame_default, METH_NOARGS, NULL},
    {"set_eval_frame_record", set_eval_frame_record, METH_O, NULL},
    _TESTINTERNALCAPI_OPTIMIZE_CFG_METHODDEF
    {NULL, NULL} /* sentinel */
};


static struct PyModuleDef _testcapimodule = {
    PyModuleDef_HEAD_INIT,
    "_testinternalcapi",
    NULL,
    -1,
    TestMethods,
    NULL,
    NULL,
    NULL,
    NULL
};


PyMODINIT_FUNC
PyInit__testinternalcapi(void)
{
    PyObject *module = PyModule_Create(&_testcapimodule);
    if (module == NULL) {
        return NULL;
    }

    if (PyModule_AddObject(module, "SIZEOF_PYGC_HEAD",
                           PyLong_FromSsize_t(sizeof(PyGC_Head))) < 0) {
        goto error;
    }

    return module;

error:
    Py_DECREF(module);
    return NULL;
}
