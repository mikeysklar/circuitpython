import serial, sys, time
tty, script = sys.argv[1], open(sys.argv[2]).read()
s = serial.Serial(tty, 115200, timeout=0.2, write_timeout=5)
def readuntil(tok, t=15):
    buf = b""; end = time.time() + t
    while time.time() < end:
        buf += s.read(512)
        if tok in buf: return buf
    raise SystemExit("timeout waiting for %r, got %r" % (tok, buf[-300:]))
time.sleep(10)  # let boot finish
for _ in range(3):
    s.write(b"\x03"); time.sleep(0.3)
s.write(b"\r\n"); readuntil(b">>> ")
s.reset_input_buffer()
s.write(b"\x05"); readuntil(b"paste mode")
for line in script.splitlines():
    s.write(line.encode() + b"\r"); time.sleep(0.03)
s.write(b"\x04")
out = readuntil(b"\nDONE", 30).decode(errors="replace")
print(out)
