# coding: utf-8

import pytest
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
