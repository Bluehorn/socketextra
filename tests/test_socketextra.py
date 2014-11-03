# coding: utf-8

import pytest

import os
import array
import socket
import sysconfig

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
    # write into received pipe end
    # read from pipe_read and get result
