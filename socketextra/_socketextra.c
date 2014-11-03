#include <Python.h>
#include <sys/socket.h>

static int extract_fd(PyObject *socket);
static int buffers_to_iovec(PyObject *buffers, struct msghdr *msghdr);

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
    struct msghdr msghdr = {0};
    ssize_t sendmsg_retval = -1;

    if (!PyArg_ParseTuple(args, "OO|OiO:sendmsg",
                          &socket, &buffers, &ancdata, &flags, &address))
        return NULL;

    if (address && address != Py_None)
        return PyErr_Format(PyExc_NotImplementedError, "sendmsg: the address argument is not implemented");

    sockfd = extract_fd(socket);
    if (sockfd == -1)
        return NULL;

    if (!buffers_to_iovec(buffers, &msghdr))
        return NULL;

    sendmsg_retval = sendmsg(sockfd, &msghdr, flags);
    if (sendmsg_retval == -1) {
        return PyErr_SetFromErrno(PyExc_OSError);
    }
    return PyInt_FromSsize_t(sendmsg_retval);
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

/* Fill msghdr->msg_iov and friends from iterable buffers. Return true on success.
   On error, return false and have PyErr set accordingly. */
static int buffers_to_iovec(PyObject *buffers, struct msghdr *msghdr)
{
    Py_ssize_t buffer_count = 0, index = 0;
    Py_buffer *py_buffers = NULL;
    struct iovec *iovec = NULL;
    PyObject *buffer_seq = NULL;

    if (PyString_Check(buffers) || PyUnicode_Check(buffers)) {
        // for compatibility with Python3, which does not accept a bytes instance.
        PyErr_Format(PyExc_TypeError, "sendmsg: buffers must not be a string/unicode object");
        return 0;
    }
    buffer_seq = PySequence_Fast(buffers, "sendmsg: buffers must be an interable of buffers");
    if (!buffer_seq)
        return 0;

    buffer_count = PySequence_Fast_GET_SIZE(buffer_seq);
    msghdr->msg_iovlen = buffer_count;
    msghdr->msg_iov = PyMem_New(struct iovec, buffer_count);
    py_buffers = PyMem_New(Py_buffer, buffer_count);
    if (!msghdr->msg_iov || !py_buffers)
        return 0;

    iovec = msghdr->msg_iov;
    for (index = 0; index < buffer_count; ++index) {
        if (!PyArg_Parse(PySequence_Fast_GET_ITEM(buffer_seq, index),
                "s*;sendmsg() argument 1 must be an iterable of buffers",
                &py_buffers[index]))
            return 0;
        iovec->iov_base = py_buffers[index].buf;
        iovec->iov_len = py_buffers[index].len;
        iovec += 1;
    }

    return 1;
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
