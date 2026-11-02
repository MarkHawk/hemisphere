"""Shared test set-up for tools/nowplaying.

The network guard: every test runs with sockets limited to this machine
(127.0.0.1, ::1, localhost). Anything else, the amp included, fails the
test at once, so the tests can never reach a real device.
"""
import pathlib
import socket
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))          # the tool's own modules

LOOPBACK = {"127.0.0.1", "::1", "localhost", ""}
FIXTURES = HERE / "fixtures"


class LiveNetworkCall(AssertionError):
    pass


def _host(address):
    if isinstance(address, tuple) and address:
        return str(address[0])
    return str(address)


def _check(address, what):
    host = _host(address)
    if host not in LOOPBACK:
        raise LiveNetworkCall(f"live network call blocked in tests: {what} {host}")


@pytest.fixture(autouse=True)
def network_guard(monkeypatch):
    real_connect = socket.socket.connect
    real_connect_ex = socket.socket.connect_ex
    real_sendto = socket.socket.sendto
    real_create = socket.create_connection
    real_getaddrinfo = socket.getaddrinfo

    def connect(self, address):
        _check(address, "connect")
        return real_connect(self, address)

    def connect_ex(self, address):
        _check(address, "connect")
        return real_connect_ex(self, address)

    def sendto(self, data, *args):
        _check(args[-1], "sendto")
        return real_sendto(self, data, *args)

    def create_connection(address, *args, **kwargs):
        _check(address, "connect")
        return real_create(address, *args, **kwargs)

    def getaddrinfo(host, *args, **kwargs):
        _check(host if host is not None else "", "lookup")
        return real_getaddrinfo(host, *args, **kwargs)

    monkeypatch.setattr(socket.socket, "connect", connect)
    monkeypatch.setattr(socket.socket, "connect_ex", connect_ex)
    monkeypatch.setattr(socket.socket, "sendto", sendto)
    monkeypatch.setattr(socket, "create_connection", create_connection)
    monkeypatch.setattr(socket, "getaddrinfo", getaddrinfo)
    yield


@pytest.fixture
def fixtures():
    return FIXTURES
