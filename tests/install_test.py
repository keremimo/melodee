#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""tools/fm1_install.py against a simulated FM-1 (no hardware, no mido).
The fake device is the one in web/test_web.mjs: identity on the handshake,
then "device asks, host answers" reads of the logical image. Run from the repo root:
  python3 tests/install_test.py
Also checks logical_image/product_of against web/fm1pkg.js (needs node and
build/melodee.fwsc, skipped otherwise), the SMK-37 identities (exactly the model), and the 20 / 36 block
deinterleave against the stock SMK-37 Pro packages (a directory named by SMK_STOCK_DIR, skipped when unset)."""
import io
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

I.DELAY.update(open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1,
               idle_check=0.4, idle_write=0.4, wait_loader=2, wait_reboot=2)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond


# ---------------------------------------------------------- simulated FM-1 ---

class FakeLink:
    def __init__(self, dev, gen):
        self.dev, self.gen, self.q, self.closed = dev, gen, queue.Queue(), False

    @property
    def lost(self):
        return self.gen != self.dev.gen or not self.dev.connected

    def send(self, pkt):
        if self.lost:
            return False
        self.dev.sent.append(bytes(pkt))
        self.dev.rx(bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.closed = True


class FakeFM1:
    """a MIDI backend with one FM-1 on it"""

    def __init__(self, image, identity="FM-1_015", name="FM-1", unplug_after=None, after_write="FM-1_900",
                 stall_after=None, bad_addr=None, loader=I.MELODEE_LOADER):
        self.image, self.unplug_after, self.after_write = image, unplug_after, after_write
        self.loader = loader        # the loader step 1 starts (the package's own)
        self.stall_after, self.bad_addr = stall_after, bad_addr
        self.served = self.bad = self.upgrades = 0
        self.sent, self.links, self.gen, self.lock = [], [], 0, threading.Lock()
        self.boot(identity, name)

    def boot(self, identity, name):
        with self.lock:
            self.gen += 1
            self.identity, self.name, self.connected = identity, name, True
            self.waiting, self.queue = None, []

    def input_names(self):
        return [self.name] if self.connected else []

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name != self.name or not self.connected:
            raise IOError("no such port")
        link = FakeLink(self, self.gen)
        self.links.append(link)
        return link

    def tx(self, pkt):
        for link in self.links:
            if link.gen == self.gen and not link.closed:
                link.q.put(bytes(pkt))

    def rx(self, d):
        if d == I.HS_QUERY:
            t = self.identity.encode()
            self.tx(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
        elif d == I.UPGRADE:
            self.upgrades += 1
            first = self.bad_addr if self.bad_addr is not None else 0
            self.queue = ([(first + k * 512, 512) for k in range(6)] + [(I.FINISH_WRITE, 8)]
                          if self.identity.startswith("ota-")
                          else [(0, 64), (0x40, 160), (0x1000, 512), (I.FINISH_CHECK, 8)])
            self.next()
        elif self.waiting:
            u = I.unpack7(d[1:-1])
            addr, n = self.waiting
            fin = addr >= I.FINISH_CHECK
            want = b"success\0" if fin else self.image[addr:addr + n]
            if u[14:14 + len(want)] != want or len(u) != 15 + len(want):
                self.bad += 1
            self.waiting = None
            self.served += 1
            if self.unplug_after and self.served >= self.unplug_after:
                self.connected = False
                return
            if self.stall_after and self.served >= self.stall_after:
                return
            if addr == I.FINISH_CHECK:
                threading.Timer(0.05, self.boot, (self.loader, "Melodee Update")).start()
            elif addr == I.FINISH_WRITE:
                threading.Timer(0.05, self.boot, (self.after_write, "Melodee")).start()
            else:
                self.next()

    def next(self):
        if not self.queue:
            return
        self.waiting = addr, n = self.queue.pop(0)
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        self.tx(b"\xF0" + I.pack7(u) + b"\xF7")


# ----------------------------------------------------------------- helpers ---

def package(product="FM-1_900", marker=True, size=0x2000, damage=False, blocks=20, seed=7):
    """a package with a valid header and file list (one file: 0x400 to the end), as fm1pkg_make writes"""
    img = bytearray((i * seed) & 0xFF for i in range(size))
    img[0x1800:0x1800 + 16] = I.LOADER_MARK if marker else bytes(16)
    data = bytes(img[0x400:])
    e = bytearray(0x50)
    e[0:16] = (0).to_bytes(2, "little") + (0).to_bytes(2, "little") + I._crc16(data).to_bytes(2, "little") + bytes(2) + \
        (0x400).to_bytes(4, "little") + len(data).to_bytes(4, "little")
    e[0x40:0x49] = b"flash.bin"
    lst = I._dec(bytes(e))
    hdr = bytearray(0x40)
    hdr[4:8] = size.to_bytes(4, "little")
    hdr[8:10] = (1).to_bytes(2, "little")
    hdr[2:4] = I._crc16(lst).to_bytes(2, "little")
    hdr[0:2] = I._crc16(bytes(hdr[2:0x40])).to_bytes(2, "little")
    img[0:0x40] = I._dec(bytes(hdr))
    img[0x40:0x90] = lst
    if damage:
        img[0x1000] ^= 1
    raw = bytearray()
    for i in range(blocks):
        m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
        raw += img[i * I.KEEP:(i + 1) * I.KEEP] + bytes([m])
    raw += img[blocks * I.KEEP:]
    return bytes(raw)


TMP = Path(tempfile.mkdtemp(prefix="melodee-install-"))


def pkgfile(name, raw):
    p = TMP / name
    p.write_bytes(raw)
    return str(p)


def cli(args, dev, answer=True):
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        rc = I.main(args, backend=dev, out=out, ask=lambda _p: answer)
    finally:
        sys.stderr = old
    return rc, out.getvalue(), err.getvalue()


# ------------------------------------------------------------------- tests ---

def wire():
    data = bytes(range(256)) * 3
    ok(I.unpack7(I.pack7(data))[:len(data)] == data, "pack7 / unpack7 round trip")
    pkt = I.response(0x12345, b"\x01\x02\x03", fl=5)
    u = I.unpack7(pkt[1:-1])
    ok(u[:3] == b"\x00\x59\x30" and int.from_bytes(u[7:11], "little") == 0x12345 and u[14:17] == b"\x01\x02\x03"
       and ~sum(u[6:17]) & 0xFF == u[17], "response: header, address, data, checksum")
    raw = package("FM-1_906")
    ok(I.product_of(raw) == "FM-1_906" and len(I.logical_image(raw)) == len(raw) - 20, "product_of / logical_image (synthetic)")


def installs():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and dev.served == 11 and "done: the FM-1 runs FM-1_900" in out
       and "100%" in out, f"install: running -> loader -> Melodee ({dev.served} reads)")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Melodee Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 1 and "update mode" in out, "install: device already in update mode -> finishes the write")

    dev = FakeFM1(image)
    rc, out, err = cli([p], dev, answer=False)
    ok(rc == 1 and dev.upgrades == 0 and "cancelled" in out, "install: answer no -> nothing sent but the handshake")

    dev = FakeFM1(image, identity="FM-1_905", name="Melodee")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "FM-1_905" in out and "running" in out and dev.upgrades == 0, "--info: identity of the connected FM-1")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Melodee Update")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "update loader" in out, "--info: device in update mode")


def errors():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    rc, out, err = cli([p, "--yes"], FakeFM1(image, name="IAC Driver Bus 1"))
    ok(rc == 3 and "not found" in err and "IAC Driver" in err, "no FM-1 port -> exit 3, lists the MIDI inputs")
    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes", "--port", "Melodee"], dev)
    ok(rc == 3 and dev.upgrades == 0, "--port that matches nothing -> exit 3")
    dev = FakeFM1(image, name="My Interface")
    rc, out, err = cli(["--info", "--port", "my int"], dev)
    ok(rc == 0 and "FM-1_015" in out, "--port picks a port the default match skips")

    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, unplug_after=2))
    ok(rc == 4 and "disconnected" in err and "nothing was written" in err and time.monotonic() - t0 < 3,
       "unplugged in step 1 -> exit 4 'lost', nothing written")
    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Melodee Update", unplug_after=3))
    ok(rc == 4 and "run the install again" in err and time.monotonic() - t0 < 3, "unplugged during the write -> exit 4 at once")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Melodee Update", stall_after=3))
    ok(rc == 4 and "stopped answering" in err, "loader stops answering -> exit 4 after the idle time")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Melodee Update", bad_addr=len(image)))
    ok(rc == 4 and "outside the package" in err, "read past the package -> exit 4")

    rc, out, err = cli([p, "--yes"], FakeFM1(image, after_write="FM-1_015"))
    ok(rc == 6 and "reports FM-1_015" in err, "another identity after the restart -> exit 6")
    dev = FakeFM1(image, identity="XY-9_001", name="usb-midi")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "another model -> exit 6, nothing sent")

    class Stuck(FakeFM1):          # the loader never shows up
        def boot(self, identity, name):
            super().boot(identity, name)
            if identity.startswith("ota-"):
                self.connected = False
    rc, out, err = cli([p, "--yes"], Stuck(image))
    ok(rc == 5 and "loader did not appear" in err, "no loader after step 1 -> exit 5")

    plain = pkgfile("plain.fwsc", package(marker=False))
    dev = FakeFM1(image)
    rc, out, err = cli([plain, "--yes"], dev)
    ok(rc == 2 and "no Melodee update loader" in err and dev.sent == [], "package without the loader marker -> exit 2, no MIDI")
    dev = FakeFM1(I.logical_image(package(marker=False)))
    rc, out, err = cli([plain, "--yes", "--force"], dev)
    ok(rc == 0 and dev.bad == 0, "... installs with --force")
    dev = FakeFM1(image, identity=I.MELODEE_LOADER, name="Melodee Update")
    rc, out, err = cli([plain, "--yes", "--force"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "nothing was sent" in err, "... but never resumes in an update loader")
    rc, out, err = cli([pkgfile("short.fwsc", b"\0" * 100), "--yes"], FakeFM1(image))
    ok(rc == 2 and "too short" in err, "not a package -> exit 2")
    rc, out, err = cli([str(TMP / "missing.fwsc"), "--yes"], FakeFM1(image))
    ok(rc == 2, "missing file -> exit 2")


def loaders():
    """each package goes only to its own update loader: the official one would write a Melodee
    package's head over the boot data"""
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)
    dev = FakeFM1(image, identity=I.STOCK_LOADER, name="FM-1")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "nothing was sent" in err and "official FM-1.fwsc" in err and "device:" not in out,
       "Melodee package, FM-1 in the official loader -> exit 6 before asking, nothing sent")
    dev = FakeFM1(image, identity="ota-FM-1_123", name="Melodee Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "report" in err, "an unknown loader -> exit 6, nothing sent")
    dev = FakeFM1(image, loader=I.STOCK_LOADER)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 1 and dev.served == 4 and dev.identity == I.STOCK_LOADER,
       "step 1 starts the official loader -> exit 6, nothing sent to it")


def smk():
    """SMK-37: identities, port names, the 36-block stock package, the SMK loader, exactly the model"""
    ok(I.PORT_RE.search("SMK-37 Pro Midi") and I.PORT_RE.search("SMK-37 Pro-MCP 2") and I.PORT_RE.search("FM-1")
       and not I.PORT_RE.search("IAC Driver Bus 1"), "port names: SMK-37 Pro ports probed, FM-1 too, other gear not")
    ident = I.parse_identity(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + b"SMK-37 Pro_016" + bytes(14)) + b"\xF7")
    ok(ident and ident.model == "SMK-37 Pro" and ident.version == 16 and I.model_of("ota-SMK-37 Pro_900") == "SMK-37 Pro",
       "identity SMK-37 Pro_016 parses (model SMK-37 Pro, the space kept)")

    # synthetic 36-block package (as the stock v016): detected, CRCs agree
    raw36 = package("SMK-37 Pro_900", blocks=36, size=0x3000)
    image36, n = I.deinterleave(raw36)
    ok(n == 36 and I.block_count(raw36) == 36 and len(image36) == len(raw36) - 36 and not I.package_damage(image36)
       and I.product_of(raw36) == "SMK-37 Pro_900", "36 blocks (synthetic): detected, logical image valid")
    ok(I.package_damage(I.logical_image(raw36)) is not None, "... a fixed 20-block deinterleave of it fails the CRCs")
    # a 20-block package whose next data byte is 0x7D by chance: the CRC check falls back to 20
    for seed in range(1, 256):
        raw = package("FM-1_900", seed=seed)
        if raw[20 * I.BLK + I.KEEP] == 0x7D:
            break
    image, n = I.deinterleave(raw)
    ok(I.block_count(raw) > 20 and n == 20 and image == I.logical_image(raw) and not I.package_damage(image),
       f"20 blocks with a 0x7D data byte at the 21st marker (seed {seed}): falls back to 20")

    # install on a simulated SMK-37 Pro (stock 016 -> Melodee's SMK loader -> SMK-37 Pro_900)
    p = pkgfile("smk.fwsc", package("SMK-37 Pro_900"))
    image = I.logical_image(package("SMK-37 Pro_900"))
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi", after_write="SMK-37 Pro_900",
                  loader="ota-SMK-37 Pro_900")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and "runs SMK-37 Pro_900" in out,
       "install on an SMK-37 Pro: running -> loader -> SMK-37 Pro_900")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_900", name="Melodee Update", after_write="SMK-37 Pro_900")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.upgrades == 1, "SMK in Melodee's update mode -> finishes the write")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_016", name="SMK-37 Pro")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "nothing was sent" in err and "official package of this model" in err,
       "Melodee SMK package, the SMK-37 Pro in the official loader -> exit 6, nothing sent")
    ok(I.package_loader("SMK-37 Elite_900", False, True) == "ota-SMK-37 Elite_900" and
       I.package_loader("FM-1_906", False, True) == I.MELODEE_LOADER and I.package_loader("FM-1_015", True, False) == I.STOCK_LOADER,
       "each package's own loader: under its model (the FM-1's as before)")
    raw36d = package("SMK-37 Pro_900", blocks=36, size=0x3000, damage=True)
    ok(I.deinterleave(raw36d)[1] == 20, "36 blocks whose files fail the CRCs: not taken, 20 blocks")
    # exactly the model: the SMK-37 Pro, SMK-37 Elite and MKE-P37 builds each on their own model only
    ok(all(I.PORT_RE.search(n) for n in ("SMK-37 Elite Midi", "MKE-P37 MIDI", "Starrykey", "Melodee"))
       and not any(I.PORT_RE.search(n) for n in ("IAC Driver Bus 1", "Launchpad X MIDI", "Digitakt")),
       "port names: the SMK models and Melodee probed, other gear not")
    for model, other in (("SMK-37 Elite", "SMK-37 Pro"), ("MKE-P37", "SMK-37 Pro"), ("SMK-37 Pro", "SMK-37 Elite")):
        pm = pkgfile(model.replace(" ", "") + ".fwsc", package(model + "_900"))
        im = I.logical_image(package(model + "_900"))
        dev = FakeFM1(im, identity=other + "_016", name=other + " Midi")
        rc, out, err = cli([pm, "--yes"], dev)
        ok(rc == 6 and dev.upgrades == 0, f"{model} package on an {other} -> exit 6, nothing sent")
        dev = FakeFM1(im, identity="ota-" + other + "_900", name="Melodee Update")
        rc, out, err = cli([pm, "--yes"], dev)
        ok(rc == 6 and dev.served == 0, f"... on an {other} in update mode -> exit 6, nothing written")
        dev = FakeFM1(im, identity=model + "_016", name=model + " Midi", after_write=model + "_900",
                      loader="ota-" + model + "_900")
        rc, out, err = cli([pm, "--yes"], dev)
        ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2, f"{model} package on an {model}: installed")
    fm1 = pkgfile("fm1.fwsc", package("FM-1_900"))
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi")
    rc, out, err = cli([fm1, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "FM-1 package on an SMK-37 Pro -> exit 6 (wrong model), nothing sent")
    dev = FakeFM1(image, identity="FM-1_015", name="FM-1")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "SMK package on an FM-1 -> exit 6, nothing sent")

    # the stock packages (outside this repo; read only)
    stock = __import__("pathlib").Path(__import__("os").environ.get("SMK_STOCK_DIR") or ROOT / "no-stock-packages")
    v016 = stock / "SMK-37_Pro_016.fwsc"
    if not v016.exists():
        print("stock SMK-37 Pro packages: skipped (SMK_STOCK_DIR not set)")
    else:
        raw = v016.read_bytes()
        image, n = I.deinterleave(raw)
        ok(n == 36 and I.product_of(raw) == "SMK-37 Pro_016" and len(raw) == 705252 and not I.package_damage(image),
           "stock SMK-37_Pro_016.fwsc: 36 blocks, identity SMK-37 Pro_016, CRCs ok")
        ok(I.package_damage(I.logical_image(raw)) is not None, "... a 20-block deinterleave of it is damaged")
        product, image2, loader = I.load_package(str(v016), False)
        ok(product == "SMK-37 Pro_016" and image2 == image and loader == "ota-SMK-37 Pro_016",
           "... load_package accepts it without --force (the official file, by SHA-256), for its official loader")
        modified = bytearray(raw); modified[-1] ^= 1
        p16 = pkgfile("smk016_modified.fwsc", bytes(modified))
        rc, out, err = cli([p16, "--yes"], FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro Midi"))
        ok(rc == 2 and "no Melodee update loader" in err, "... a modified copy without --force is refused (exit 2)")
        for old in sorted(stock.glob("SMK-37_Pro_01[1-5].fwsc")):
            raw = old.read_bytes()
            image, n = I.deinterleave(raw)
            ok(not I.package_damage(image) and I.product_of(raw) == "SMK-37 Pro_" + old.stem[-3:],
               f"stock {old.name}: {n} blocks, identity {I.product_of(raw)}, CRCs ok")
    clean = ROOT / "build/melodee.fwsc"
    if clean.exists():
        raw = clean.read_bytes()
        image, n = I.deinterleave(raw)
        ok(n == 20 and image == I.logical_image(raw) and not I.package_damage(image) and I.product_of(raw).startswith("FM-1_"),
           f"FM-1 package {clean.name} still parses: 20 blocks, {I.product_of(raw)}")


def against_js():
    clean = ROOT / "build/melodee.fwsc"
    if not shutil.which("node") or not clean.exists():
        print("logical image vs fm1pkg.js: skipped (needs node and build/melodee.fwsc)")
        return
    js = ("import { logicalImage, productOf } from %r; import { readFileSync } from 'node:fs';"
          "const p = readFileSync(process.argv[1]); process.stderr.write(productOf(p));"
          "process.stdout.write(logicalImage(p));") % str(ROOT / "web/fm1pkg.js")
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(clean)], capture_output=True, check=True)
    raw = clean.read_bytes()
    ok(I.logical_image(raw) == r.stdout and I.product_of(raw) == r.stderr.decode(),
       f"logical_image / product_of == fm1pkg.js ({clean.name}, {I.product_of(raw)})")
    ok(I.LOADER_MARK in raw, f"{clean.name} carries the Melodee loader marker")


def official():
    """#32: back to the official V15 from Melodee, without --force (only the unmodified file)"""
    v15 = ROOT / "FM-1_v15.fwsc"
    if not v15.exists():
        print("official V15 restore: skipped (needs FM-1_v15.fwsc in the repo root)")
        return
    raw = v15.read_bytes()
    dev = FakeFM1(I.logical_image(raw), identity="FM-1_900", name="Melodee", after_write="FM-1_015", loader=I.STOCK_LOADER)
    rc, out, err = cli([str(v15), "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and "FM-1_015" in out and dev.identity == "FM-1_015",
       "official V15 (FM-1.fwsc) from Melodee: installed without --force, back as FM-1_015")
    dev = FakeFM1(I.logical_image(raw), identity=I.STOCK_LOADER, name="FM-1", after_write="FM-1_015")
    rc, out, err = cli([str(v15), "--yes"], dev)
    ok(rc == 0 and dev.upgrades == 1 and dev.identity == "FM-1_015", "official V15 resumes in the official loader")
    dev = FakeFM1(I.logical_image(raw), identity=I.MELODEE_LOADER, name="Melodee Update", after_write="FM-1_015")
    rc, out, err = cli([str(v15), "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "Melodee package first" in err, "official V15 never resumes in Melodee's loader")
    dev = FakeFM1(I.logical_image(raw), identity="FM-1_900", name="Melodee", after_write="FM-1_015")
    rc, out, err = cli([str(v15), "--yes"], dev)
    ok(rc == 6 and dev.served == 4 and dev.identity == I.MELODEE_LOADER, "official V15 is not written when step 1 starts Melodee's loader")
    bad = bytearray(raw)
    bad[-1] ^= 1
    dev = FakeFM1(I.logical_image(bytes(bad)), identity="FM-1_900", name="Melodee")
    rc, out, err = cli([pkgfile("v15mod.fwsc", bytes(bad)), "--yes"], dev)
    ok(rc == 2 and "official V15" in err and dev.sent == [], "a modified V15 is still refused (no MIDI)")


wire()
installs()
errors()
loaders()
smk()
against_js()
official()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)
