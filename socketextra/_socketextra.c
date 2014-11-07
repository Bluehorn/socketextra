#include <Python.h>
#include <sys/socket.h>

struct cmsg_tuple {
    int level;
    int type;
    Py_buffer data;
};

static int extract_fd(PyObject *socket);
static int buffers_to_iovec(PyObject *buffers, struct msghdr *msghdr, Py_buffer **buffer_views_ref);
static void free_iovec(struct msghdr *msghdr, Py_buffer **buffer_views_ref);
static int ancdata_to_cmsg(PyObject *ancdata, struct msghdr *msghdr);
static void free_cmsg(struct msghdr *msghdr, struct cmsg_tuple **cmsg_array_ref);
static PyObject *ancillary_data_list(struct msghdr *msghdr);

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
    Py_buffer *buffer_views = NULL;
    int flags = 0;
    PyObject *address = NULL;
    PyObject *result = NULL;
    int sockfd = -1;
    struct msghdr msghdr = {0};
    ssize_t sendmsg_retval = -1;

    if (!PyArg_ParseTuple(args, "OO|OiO:sendmsg",
                          &socket, &buffers, &ancdata, &flags, &address))
        goto finally;

    if (address && address != Py_None) {
        PyErr_Format(PyExc_NotImplementedError, "sendmsg: the address argument is not implemented");
        goto finally;
    }

    sockfd = extract_fd(socket);
    if (sockfd == -1)
        goto finally;

    if (!buffers_to_iovec(buffers, &msghdr, &buffer_views))
        goto finally;
    if (!ancdata_to_cmsg(ancdata, &msghdr))
        goto finally;

    Py_BEGIN_ALLOW_THREADS;
    sendmsg_retval = sendmsg(sockfd, &msghdr, flags);
    Py_END_ALLOW_THREADS;
    if (sendmsg_retval == -1) {
        PyErr_SetFromErrno(PyExc_OSError);
        goto finally;
    }
    result = PyInt_FromSsize_t(sendmsg_retval);

finally:
    free_iovec(&msghdr, &buffer_views);
    return result;
}

static PyObject *
socketextra_recvmsg(PyObject *self, PyObject *args)
{
    PyObject *socket = NULL, *result = NULL, *data_buffer = NULL;
    Py_ssize_t bufsize = 0, ancbufsize = 0;
    void *ancbuffer = NULL;
    ssize_t bytes_received = -1;
    int flags = 0, sockfd = -1;
    struct msghdr msghdr = {0};
    struct iovec iovec = {0};

    if (!PyArg_ParseTuple(args, "On|ni:recvmsg", &socket, &bufsize, &ancbufsize, &flags))
        return NULL;

    sockfd = extract_fd(socket);
    if (sockfd == -1)
        goto finally;

    if (bufsize < 0) {
        PyErr_SetString(PyExc_ValueError, "negative buffer size in recvmsg()");
        goto finally;
    }
    if (ancbufsize < 0) {
        PyErr_SetString(PyExc_ValueError, "negative ancillary buffer size in recvmsg()");
        goto finally;
    }

    data_buffer = PyString_FromStringAndSize(NULL, bufsize);
    if (!data_buffer)
        goto finally;

    msghdr.msg_iov = &iovec;
    msghdr.msg_iovlen = 1;
    iovec.iov_base = PyString_AS_STRING(data_buffer);
    iovec.iov_len = bufsize;

    if (ancbufsize > 0) {
        ancbuffer = PyMem_Malloc(ancbufsize);
        if (!ancbuffer) {
            PyErr_NoMemory();
            goto finally;
        }
        msghdr.msg_control = ancbuffer;
        msghdr.msg_controllen = ancbufsize;
    }

    Py_BEGIN_ALLOW_THREADS;
    bytes_received = recvmsg(sockfd, &msghdr, flags);
    Py_END_ALLOW_THREADS;

    if (bytes_received == -1) {
        PyErr_SetFromErrno(PyExc_OSError);
    } else {
        if (_PyString_Resize(&data_buffer, bytes_received) == 0) {
            /* success */
            result = Py_BuildValue("ONiO",
                    data_buffer,
                    ancillary_data_list(&msghdr),
                    (int) msghdr.msg_flags,
                    Py_None);
        }
    }

finally:
    Py_XDECREF(data_buffer);
    PyMem_Free(ancbuffer);
    return result;
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
   On error, return false and have PyErr set accordingly. On success, the caller
   becomes responsible to call free_iovec(msghdr, buffer_views_ref). */
static int buffers_to_iovec(PyObject *buffers, struct msghdr *msghdr, Py_buffer **buffer_views_ref)
{
    int success = 0;
    Py_ssize_t buffer_count = 0, index = 0;
    Py_buffer *py_buffers = NULL;
    struct iovec *iovec = NULL;
    PyObject *buffer_seq = NULL;

    if (PyString_Check(buffers) || PyUnicode_Check(buffers)) {
        // for compatibility with Python3, which does not accept a bytes instance.
        PyErr_Format(PyExc_TypeError, "sendmsg: buffers must not be a string/unicode object");
        goto finally;
    }
    buffer_seq = PySequence_Fast(buffers, "sendmsg: buffers must be an iterable of buffers");
    if (!buffer_seq)
        return 0;

    buffer_count = PySequence_Fast_GET_SIZE(buffer_seq);
    msghdr->msg_iovlen = buffer_count;
    msghdr->msg_iov = PyMem_New(struct iovec, buffer_count);
    if (!msghdr->msg_iov) {
        PyErr_NoMemory();
        goto finally;
    }
    py_buffers = PyMem_New(Py_buffer, buffer_count);
    if (!py_buffers) {
        PyErr_NoMemory();
        goto finally;
    }
    memset(py_buffers, 0, sizeof(Py_buffer) * buffer_count);
    *buffer_views_ref = py_buffers;

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

    success = 1;
finally:
    Py_CLEAR(buffer_seq);
    if (!success)
        free_iovec(msghdr, buffer_views_ref);
    return success;
}

/* Free the buffers allocated by buffers_to_iovec. */
static void free_iovec(struct msghdr *msghdr, Py_buffer **buffer_views_ref)
{
    Py_buffer *buffer_views = *buffer_views_ref;
    Py_ssize_t buffer_count = msghdr->msg_iovlen;
    Py_ssize_t i;

    *buffer_views_ref = NULL;

    if (buffer_views) {
        for (i = 0; i < buffer_count; i++)
            if (buffer_views[i].buf)
                PyBuffer_Release(&buffer_views[i]);
        PyMem_Free(buffer_views);
    }

    PyMem_Free(msghdr->msg_iov);
    msghdr->msg_iov = NULL;
}


static int ancdata_to_cmsg(PyObject *ancdata, struct msghdr *msghdr)
{
    int success = 0;
    PyObject *cmsg_seq = NULL;
    Py_ssize_t cmsg_count = 0, i;
    size_t controllen = 0;
    struct cmsghdr *cmsg = NULL;
    struct cmsg_tuple *cmsg_array = NULL;

    /* Nothing to do if ancdata is None or was not passed. */
    if (!ancdata || ancdata == Py_None)
        return 1;

    if (PyString_Check(ancdata) || PyUnicode_Check(ancdata)) {
        PyErr_Format(PyExc_TypeError, "sendmsg: ancdata must be an iterable of tuples");
        return 0;
    }

    cmsg_seq = PySequence_Fast(ancdata, "sendmsg: ancdata must be an iterable of tuples");
    if (!cmsg_seq)
        return 0;

    cmsg_count = PySequence_Fast_GET_SIZE(cmsg_seq);
    cmsg_array = PyMem_New(struct cmsg_tuple, cmsg_count);
    if (!cmsg_array) {
        PyErr_NoMemory();
        goto finally;
    }
    memset(cmsg_array, 0, sizeof(struct cmsg_tuple) * cmsg_count);

    for (i = 0; i < cmsg_count; i++) {
        if (!PyArg_Parse(PySequence_Fast_GET_ITEM(cmsg_seq, i),
                         "(iis*):[sendmsg(): ancillary data item]",
                         &cmsg_array[i].level,
                         &cmsg_array[i].type,
                         &cmsg_array[i].data))
            goto finally;
        controllen += CMSG_SPACE(cmsg_array[i].data.len);
    }

    msghdr->msg_control = PyMem_Malloc(controllen);
    if (!msghdr->msg_control) {
        PyErr_NoMemory();
        goto finally;
    }
    msghdr->msg_controllen = controllen;
    memset(msghdr->msg_control, 0, controllen);

    cmsg = NULL;
    for (i = 0; i < cmsg_count; i++) {
        cmsg = cmsg ? CMSG_NXTHDR(msghdr, cmsg) : CMSG_FIRSTHDR(msghdr);
        assert(cmsg);
        cmsg->cmsg_type = cmsg_array[i].type;
        cmsg->cmsg_level = cmsg_array[i].level;
        cmsg->cmsg_len = CMSG_LEN(cmsg_array[i].data.len);
        memcpy(CMSG_DATA(cmsg), cmsg_array[i].data.buf, cmsg_array[i].data.len);
    }

    success = 1;

finally:
    Py_CLEAR(cmsg_seq);
    return success;
}


static void free_cmsg(struct msghdr *msghdr, struct cmsg_tuple **cmsg_array_ref)
{
}


static PyObject *ancillary_data_list(struct msghdr *msghdr)
{
    Py_ssize_t overhead = CMSG_DATA((struct cmsghdr*) NULL) - (unsigned char *)NULL;
    PyObject *result = PyList_New(0);
    if (!result)
        goto bail;

    if (msghdr->msg_controllen > 0) {
        struct cmsghdr *cmsg;
        for (cmsg = CMSG_FIRSTHDR(msghdr);
                cmsg; cmsg = CMSG_NXTHDR(msghdr, cmsg))
        {
            int rc;
            PyObject *entry = Py_BuildValue("iiN",
                    (int) cmsg->cmsg_level,
                    (int) cmsg->cmsg_type,
                    PyString_FromStringAndSize((char*) CMSG_DATA(cmsg), cmsg->cmsg_len - overhead));
            if (!entry)
                goto bail;

            rc = PyList_Append(result, entry);
            Py_DECREF(entry);
            if (rc)
                goto bail;
        }
    }

    return result;

bail:
    Py_XDECREF(result);
    return NULL;
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
    PyObject *mod = Py_InitModule("socketextra._socketextra", socketextra_methods);
    if (!mod)
        return;

    if (!PyModule_AddIntMacro(mod, SCM_RIGHTS))
        return;
}
