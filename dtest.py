import struct, array, os
print(os.uname().version)
def rw(tc):
    a = array.array(tc, [0.0])
    a[0] = 3.5
    return a[0]
T = [
 ("calcsize <d", lambda: struct.calcsize("<d")),
 ("calcsize @d", lambda: struct.calcsize("@d")),
 ("pack <d", lambda: struct.pack("<d", 1.5)),
 ("unpack <d", lambda: struct.unpack("<d", b"\x00\x00\x00\x00\x00\x00\xf8?")),
 ("unpack @d", lambda: struct.unpack("@d", b"\x00\x00\x00\x00\x00\x00\xf8?")),
 ("array d", lambda: array.array("d", [1.5, -2.25])),
 ("array d rw", lambda: rw("d")),
 ("array f rw", lambda: rw("f")),
 ("memoryview d", lambda: memoryview(bytearray(8)).cast("d")[0] if hasattr(memoryview, "cast") else "no cast"),
 ("pack <f", lambda: struct.pack("<f", 1.5)),
 ("unpack <f", lambda: struct.unpack("<f", struct.pack("<f", 1.5))),
 ("array f", lambda: array.array("f", [1.5, -2.25])),
]
for name, fn in T:
    try:
        print("OK  ", name, repr(fn()))
    except Exception as e:
        print("EXC ", name, repr(e))
print("DONE")
