#!/usr/bin/env python3
"""Regenerate tests/fixtures/proc: a tiny fake /proc with a known process tree.

    1 systemd
    ├── 100 compositor            (launches apps -> its other children are separate)
    │   ├── 200 myapp             app (matches myapp.desktop)
    │   │   └── 201 "My (weird) name"   rolled into myapp; tests ')' in names
    │   ├── 300 term              terminal app
    │   │   └── 301 zsh -> 302 nvim     label "Term → nvim"
    │   └── 500 notify            user service
    ├── 400 bar                   user service
    │   └── 401 python3           rolled into bar
    ├── 600 dockerd               system service (docker.service)
    ├── 601 dbus-daemon           hidden: essential
    ├── 700 mongod                hidden: container
    └── 800 pipewire              user service (pipewire.service)
    2 kthreadd -> 10 kworker      hidden: kernel
"""
import os, shutil

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "proc")
USER = "/user.slice/user-1000.slice"
SESSION = USER + "/session-1.scope"

# pid, ppid, comm, uid, rss_kb, start, cgroup, exe, argv
PROCS = [
    (1, 0, "systemd", 0, 9000, 1, "/init.scope", "", ["/sbin/init"]),
    (2, 0, "kthreadd", 0, 0, 1, "/", "", []),
    (10, 2, "kworker/0:1", 0, 0, 2, "/", "", []),
    (100, 1, "compositor", 1000, 2000, 10, SESSION, "/usr/bin/compositor", ["compositor"]),
    (200, 100, "myapp", 1000, 1000, 20, SESSION, "/usr/bin/myapp", ["myapp", "--flag"]),
    (201, 200, "My (weird) name", 1000, 500, 21, SESSION, "/usr/bin/myapp", ["myapp", "--child"]),
    (300, 100, "term", 1000, 300, 30, SESSION, "/usr/bin/term", ["term"]),
    (301, 300, "zsh", 1000, 50, 31, SESSION, "/usr/bin/zsh", ["zsh"]),
    (302, 301, "nvim", 1000, 70, 32, SESSION, "/usr/bin/nvim", ["nvim", "notes.txt"]),
    (400, 1, "bar", 1000, 100, 40, SESSION, "/usr/bin/bar", ["bar"]),
    (401, 400, "python3", 1000, 20, 41, SESSION, "/usr/bin/python3", ["python3", "w.py"]),
    (500, 100, "notify", 1000, 30, 50, SESSION, "/usr/bin/notify", ["notify"]),
    (600, 1, "dockerd", 0, 800, 5, "/system.slice/docker.service", "", ["/usr/bin/dockerd"]),
    (601, 1, "dbus-daemon", 81, 40, 5, "/system.slice/dbus.service", "", ["dbus-daemon"]),
    (700, 1, "mongod", 999, 600, 60, "/system.slice/docker-abc123.scope", "", ["mongod"]),
    (800, 1, "pipewire", 1000, 60, 15,
     USER + "/user@1000.service/session.slice/pipewire.service", "/usr/bin/pipewire", ["pipewire"]),
]

shutil.rmtree(ROOT, ignore_errors=True)
os.makedirs(os.path.join(ROOT, "net"))
with open(os.path.join(ROOT, "uptime"), "w") as f:
    f.write("1000.00 500.00\n")

for pid, ppid, comm, uid, rss, start, cgroup, exe, argv in PROCS:
    d = os.path.join(ROOT, str(pid))
    os.makedirs(os.path.join(d, "fd"))
    with open(os.path.join(d, "stat"), "w") as f:     # 17 filler fields between ppid and starttime
        f.write(f"{pid} ({comm}) S {ppid} " + " ".join(["0"] * 17) + f" {start} 0 0\n")
    with open(os.path.join(d, "status"), "w") as f:
        f.write(f"Name:\t{comm}\nUid:\t{uid}\t{uid}\t{uid}\t{uid}\n")
        if rss:
            f.write(f"VmRSS:\t{rss:8d} kB\n")
    with open(os.path.join(d, "cmdline"), "wb") as f:
        f.write(b"".join(a.encode() + b"\0" for a in argv))
    with open(os.path.join(d, "cgroup"), "w") as f:
        f.write(f"0::{cgroup}\n")
    if exe:
        os.symlink(exe, os.path.join(d, "exe"))

# myapp listens on 127.0.0.1:8080 and [::1]:9000; port 22 is ESTABLISHED (not listening)
os.symlink("socket:[5555]", os.path.join(ROOT, "200", "fd", "3"))
os.symlink("socket:[7777]", os.path.join(ROOT, "200", "fd", "4"))
os.symlink("/dev/null", os.path.join(ROOT, "200", "fd", "0"))
HDR = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
with open(os.path.join(ROOT, "net", "tcp"), "w") as f:
    f.write(HDR)
    f.write("   0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 5555 1\n")
    f.write("   1: 00000000:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000     0        0 6666 1\n")
with open(os.path.join(ROOT, "net", "tcp6"), "w") as f:
    f.write(HDR)
    f.write("   0: 00000000000000000000000001000000:2328 00000000000000000000000000000000:0000 0A "
            "00000000:00000000 00:00000000 00000000  1000        0 7777 1\n")
print("wrote", ROOT)
