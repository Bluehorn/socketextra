#include <Python.h>

static PyMethodDef socketextra_methods[] = {
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyMODINIT_FUNC
init_socketextra(void)
{
    Py_InitModule("socketextra._socketextra", socketextra_methods);
}
