#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Ed25519 (RFC 8032) for synth modules' SIGN chunk: the reference arithmetic of RFC 8032 section 6, in Python.

    mod_sign.py --key KEYFILE --public          the public key (hex) of a 32-byte seed file (hex)
    mod_sign.py --new KEYFILE                   a new seed (os.urandom) into KEYFILE, its public key printed

A module signed by one of Melodee's keys installs without the firmware's warning (firmware/src/mod_sign.c). The
builder (mod_build.py) signs when it is given a key (MELODEE_SIGN_KEY, else ~/.config/melodee/module-sign.key if it
is there); an outside author's modules are unsigned and install with the warning."""
import hashlib
import os
import sys

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
I = pow(2, (P - 1) // 4, P)


def _xrecover(y):
    xx = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    x = pow(xx, (P + 3) // 8, P)
    if (x * x - xx) % P:
        x = x * I % P
    return P - x if x % 2 else x


_BY = 4 * pow(5, P - 2, P) % P
_B = (_xrecover(_BY), _BY, 1, _xrecover(_BY) * _BY % P)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _enc(p):
    zi = pow(p[2], P - 2, P)
    x, y = p[0] * zi % P, p[1] * zi % P
    return (y | (x & 1) << 255).to_bytes(32, "little")


def _sha(*b):
    return int.from_bytes(hashlib.sha512(b"".join(b)).digest(), "little")


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public(seed):
    a, _ = _expand(seed)
    return _enc(_mul(a, _B))


def sign(seed, msg):
    a, prefix = _expand(seed)
    pk = _enc(_mul(a, _B))
    r = _sha(prefix, msg) % L
    rb = _enc(_mul(r, _B))
    s = (r + _sha(rb, pk, msg) * a) % L
    return rb + s.to_bytes(32, "little")


def load(path):
    """a key file: 64 hex digits (the seed), 32 bytes"""
    seed = bytes.fromhex(open(path).read().strip())
    if len(seed) != 32:
        raise SystemExit(f"mod_sign: {path}: not a 32-byte seed in hex")
    return seed


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--new":
        if os.path.exists(sys.argv[2]):
            raise SystemExit(f"mod_sign: {sys.argv[2]} exists")
        seed = os.urandom(32)
        os.makedirs(os.path.dirname(os.path.abspath(sys.argv[2])), exist_ok=True)
        fd = os.open(sys.argv[2], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        os.write(fd, (seed.hex() + "\n").encode())
        os.close(fd)
        print(public(seed).hex())
    elif len(sys.argv) == 4 and sys.argv[1] == "--key" and sys.argv[3] == "--public":
        print(public(load(sys.argv[2])).hex())
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
