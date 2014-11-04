# coding: utf-8

import pytest

import os
import array
import socket
try:
    import sysconfig
except ImportError:
    # sysconfig is new in Python 2.7 but seems to be available from distutils
    # in 2.6 at least.
    from distutils import sysconfig

import socketextra

@pytest.mark.skipif("sysconfig.get_config_var('SIZEOF_INT') == sysconfig.get_config_var('SIZEOF_LONG')")
def test_invalid_fileno():
    """sendmsg et al check if fileno() returns a valid int."""
    class BadSocket(object):
        def fileno(self):
            return 1 << 8*sysconfig.get_config_var('SIZEOF_INT')
    with pytest.raises(OverflowError):
        socketextra.sendmsg(BadSocket(), [])


def test_address_error():
    """for now the address argument is not supported"""
    sock, _ = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    with pytest.raises(NotImplementedError):
        socketextra.sendmsg(sock, [], [], 0, ("127.0.0.1", "200"))


def test_sendmsg_scather_gather():
    """Passing multiple buffers to sendmsg will send them as a single message."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    socketextra.sendmsg(sender, ["vec1", "vec2", "vec3"])
    assert receiver.recv(400) == "vec1vec2vec3"


def test_recvmsg_data_only():
    """recvmsg without ancbuffer should work like recv."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    sender.send("hello world")
    reply = socketextra.recvmsg(receiver, 1000)
    assert len(reply) == 4 and isinstance(reply, tuple)
    assert reply[0] == "hello world"
    assert reply[1] == []
    assert reply[3] is None


def test_sendmsg_non_buffer():
    """buffers must be a list of buffer objects, check error checking."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, 42)
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, ["a string", 42])
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, "string is bad")


def test_sendmsg_bad_ancdata():
    """ancdata must be a list of tuples."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, [], "string is not allowed")
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, [], ["list must have tuples"])
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, [], [(0, 0, 42)])
    with pytest.raises(TypeError):
        socketextra.sendmsg(sender, [], [(0, 0)])

def test_send_recv_rights():
    """Tests sending open files as SCM_RIGHTS ancillary data."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    pipe_read, pipe_write = os.pipe()
    fds = [pipe_write]
    socketextra.sendmsg(sender, [], [(socket.SOL_SOCKET, socketextra.SCM_RIGHTS, array.array("i", fds))])
    (data, ancdata, msg_flags, address) = socketextra.recvmsg(receiver, 128, 1024, 0)
    assert data == ""
    assert ancdata
    assert ancdata[0][:2] == (socket.SOL_SOCKET, socketextra.SCM_RIGHTS)
    received_fds = array.array("i")
    received_fds.fromstring(ancdata[0][2])
    pipe_write_received, = received_fds
    os.write(pipe_write_received, "hello world")
    assert os.read(pipe_read, 1000) == "hello world"

def test_scatter_gather_ancillary():
    """Tests sending multiple files via SCM_RIGHTS."""
    sender, receiver = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    fds = []
    for x in range(3):
        fds.extend(os.pipe())
    socketextra.sendmsg(sender,
        ["this is ", "an elaborate", " request"],
        [(socket.SOL_SOCKET, socketextra.SCM_RIGHTS, array.array("i", fds[:1])),
         (socket.SOL_SOCKET, socketextra.SCM_RIGHTS, array.array("i", fds[1:4])),
         (socket.SOL_SOCKET, socketextra.SCM_RIGHTS, array.array("i", fds[4:]))])
    (data, ancdata, msg_flags, address) = socketextra.recvmsg(receiver, 128, 1024, 0)
    assert data == "this is an elaborate request"
    received = array.array("i")
    for entry in ancdata:
        if entry[:2] == (socket.SOL_SOCKET, socketextra.SCM_RIGHTS):
            received.fromstring(entry[2])
    assert len(received) == len(fds)
