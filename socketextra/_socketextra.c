#include <Python.h>
#include <sys/socket.h>

static int extract_fd(PyObject *socket);

static PyObject *
socketextra_CMSG_LEN(PyObject *self, PyObject *args)
{
    Py_ssize_t nbytes;

    if (!PyArg_ParseTuple(args, "n:CMSG_LEN", &nbytes))
        return NULL;
    return PyLong_FromSize_t(CMSG_LEN(nbytes));
}

static PyObject *
socketextra_CMSG_SPACE(PyObject *self, PyObject *args)
{
    Py_ssize_t nbytes;

    if (!PyArg_ParseTuple(args, "n:CMSG_SPACE", &nbytes))
        return NULL;
    return PyLong_FromSize_t(CMSG_SPACE(nbytes));
}

PyDoc_STRVAR(socketextra_sendmsg_doc,
"sendmsg(socket, buffers[, ancdata[, flags[, address]]]) -> count\n\
\n\
Provides access to the system call sendmsg.\n\
This is the equivalent of `socket.sendmsg(buffers, ancdata, flags, address)\n\
in Python3. Note that the address parameter is currently not supported,\n\
as the python library does not expose the needed functionality.");

static PyObject *
socketextra_sendmsg(PyObject *self, PyObject *args)
{
    PyObject *socket = NULL, *buffers = NULL, *ancdata = NULL;
    int flags = 0;
    PyObject *address = NULL;
    int sockfd = -1;

    if (!PyArg_ParseTuple(args, "OO|OiO:sendmsg",
                          &socket, &buffers, &ancdata, &flags, &address))
        return NULL;

    sockfd = extract_fd(socket);
    if (sockfd == -1)
        return NULL;

    return PyErr_Format(PyExc_NotImplementedError, "sendmsg");
}

static PyObject *
socketextra_recvmsg(PyObject *self, PyObject *args)
{
    PyObject *socket = NULL;
    Py_ssize_t bufsize = 0, ancbufsize = 0;
    int flags = 0, sockfd = -1;

    if (!PyArg_ParseTuple(args, "On|ni:recvmsg", &bufsize, &ancbufsize, &flags))
        return NULL;

    sockfd = extract_fd(socket);
    if (sockfd == -1)
        return NULL;

    return PyErr_Format(PyExc_NotImplementedError, "recvmsg");
}


/* Return socket.fileno as int. On error, return -1 and set Python error */
static int extract_fd(PyObject *socket)
{
    long fileno_c;
    PyObject *fileno = PyObject_CallMethod(socket, "fileno", NULL);

    if (!fileno)
        return -1;
    fileno_c = PyInt_AsLong(fileno);
    if (fileno_c == -1 && PyErr_Occurred())
        return -1;
    if (fileno_c < INT_MIN || fileno_c > INT_MAX) {
        PyErr_Format(PyExc_OverflowError, "socket.fileno() returned non-integer %ld", fileno_c);
        return -1;
    }
    return fileno_c;
}

static PyMethodDef socketextra_methods[] = {
    {"CMSG_LEN",          socketextra_CMSG_LEN, METH_VARARGS, NULL},
    {"CMSG_SPACE",        socketextra_CMSG_SPACE, METH_VARARGS, NULL},
    {"sendmsg",           socketextra_sendmsg, METH_VARARGS, socketextra_sendmsg_doc},
    {"recvmsg",           socketextra_recvmsg, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


PyMODINIT_FUNC
init_socketextra(void)
{
    Py_InitModule("socketextra._socketextra", socketextra_methods);
}
