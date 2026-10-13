#!/usr/bin/env python3
"""Build a fake machine for screenshots/GIFs: build/demo/ (or the dir given).

    source build/demo/env.sh     # points magips at the fake /proc, apps, docker
    magips                       # nothing real is shown, and nothing real is killed

The processes belong to your own uid (magips shows *your* apps), but live only
in the fake /proc. With MAGIPS_PROC set, magips never sends real signals.
"""
import os, shutil, stat, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "build", "demo"))
PROC = os.path.join(OUT, "proc")
ME = os.getuid()
HOME = "/home/alex"
HZ = os.sysconf("SC_CLK_TCK")
UPTIME = 26 * 3600                               # "booted" 26 hours ago

USER = f"/user.slice/user-{ME}.slice"
SESSION = USER + "/session-1.scope"
UNIT = USER + f"/user@{ME}.service"
SYS = "/system.slice"

procs = []           # dicts
sockets = []         # (pid or None, inode, hexaddr:port line, v6)
inode = [40000]


def p(pid, ppid, comm, uid=ME, rss=0, age=UPTIME - 60, cgroup=SESSION, exe="", argv=None,
      sid=0, cwd=None):
    procs.append(dict(pid=pid, ppid=ppid, comm=comm, uid=uid, rss=rss, age=age, cgroup=cgroup,
                      exe=exe, argv=argv if argv is not None else [comm], sid=sid, cwd=cwd))


def listen(pid, addr, port):
    """addr: '127.0.0.1', '0.0.0.0' or '::'. pid None = owner not visible (root)."""
    inode[0] += 1
    if addr == "::":
        hexaddr, v6 = "0" * 32, True
    else:
        a = [int(x) for x in addr.split(".")]
        hexaddr, v6 = "%02X%02X%02X%02X" % (a[3], a[2], a[1], a[0]), False
    sockets.append((pid, inode[0], f"{hexaddr}:{port:04X}", v6))


H = 3600
# --- kernel + system
p(1, 0, "systemd", 0, 12000, UPTIME, "/init.scope", argv=["/sbin/init"])
p(2, 0, "kthreadd", 0, 0, UPTIME, "/", argv=[])
for i, k in enumerate(["kworker/0:1", "ksoftirqd/0", "rcu_preempt"]):
    p(10 + i, 2, k, 0, 0, UPTIME, "/", argv=[])
p(300, 1, "NetworkManager", 0, 21000, UPTIME, SYS + "/NetworkManager.service")
p(301, 1, "dbus-broker-lau", 81, 4000, UPTIME, SYS + "/dbus-broker.service")
p(302, 1, "systemd-journal", 0, 30000, UPTIME, SYS + "/systemd-journald.service")
p(400, 1, "sshd", 0, 9000, UPTIME, SYS + "/sshd.service", argv=["sshd: /usr/bin/sshd -D"])
listen(None, "0.0.0.0", 22)
p(410, 1, "dockerd", 0, 98000, UPTIME, SYS + "/docker.service")
p(411, 1, "containerd", 0, 61000, UPTIME, SYS + "/containerd.service")
p(412, 411, "containerd-shim", 0, 13000, 3 * H, SYS + "/containerd.service")
p(413, 411, "containerd-shim", 0, 13000, 3 * H, SYS + "/containerd.service")
p(414, 410, "docker-proxy", 0, 8000, 3 * H, SYS + "/docker.service")
listen(None, "127.0.0.1", 6379)                  # published by the redis container
p(440, 412, "redis-server", 999, 14000, 3 * H, SYS + "/docker-a1b2c3.scope")
p(441, 413, "mongod", 999, 190000, 3 * H, SYS + "/docker-d4e5f6.scope")
p(420, 1, "postgres", 970, 31000, UPTIME, SYS + "/postgresql.service",
  argv=["/usr/bin/postgres", "-D", "/var/lib/postgres/data"])
for i in range(3):
    p(421 + i, 420, "postgres", 970, 9000, UPTIME, SYS + "/postgresql.service")
listen(None, "127.0.0.1", 5432)

# --- your systemd --user and its services
p(500, 1, "systemd", ME, 11000, UPTIME - 50, UNIT + "/init.scope", argv=["/usr/lib/systemd/systemd", "--user"])
p(510, 500, "pipewire", ME, 22000, UPTIME - 45, UNIT + "/session.slice/pipewire.service", "/usr/bin/pipewire")
p(511, 500, "wireplumber", ME, 41000, UPTIME - 45, UNIT + "/session.slice/wireplumber.service", "/usr/bin/wireplumber")
p(512, 500, "xdg-desktop-por", ME, 18000, UPTIME - 45, UNIT + "/session.slice/xdg-desktop-portal.service")
p(513, 500, "syncthing", ME, 52000, UPTIME - 40, UNIT + "/app.slice/syncthing.service", "/usr/bin/syncthing")
listen(513, "127.0.0.1", 8384)

# --- desktop session: compositor and what it launched
p(600, 1, "Hyprland", ME, 240000, UPTIME - 60, SESSION, "/usr/bin/Hyprland")
p(610, 600, "waybar", ME, 95000, UPTIME - 55, SESSION, "/usr/bin/waybar")
p(611, 610, "python3", ME, 12000, UPTIME - 55, SESSION, "/usr/bin/python3.13", ["python3", "weather.py"])
p(612, 600, "mako", ME, 26000, UPTIME - 55, SESSION, "/usr/bin/mako")

p(620, 600, "firefox", ME, 610000, 5 * H, SESSION, "/usr/lib/firefox/firefox")
for i, (name, rss) in enumerate([("forkserver", 30000), ("Socket Process", 60000),
                                 ("Privileged Cont", 210000), ("WebExtensions", 420000),
                                 ("RDD Process", 80000), ("Utility Process", 55000),
                                 ("Isolated Web Co", 820000), ("Isolated Web Co", 540000),
                                 ("Isolated Web Co", 260000), ("Web Content", 100000),
                                 ("Web Content", 98000)]):
    p(621 + i, 620, name, ME, rss, 5 * H - 10, SESSION, "/usr/lib/firefox/firefox")

p(640, 600, "spotify", ME, 420000, 4 * H, SESSION, "/opt/spotify/spotify")
for i, rss in enumerate([75000, 76000, 150000, 290000, 100000, 600000, 118000]):
    p(641 + i, 640, "spotify", ME, rss, 4 * H, SESSION, "/opt/spotify/spotify")
listen(640, "0.0.0.0", 57621)

p(670, 600, "obsidian", ME, 310000, 2 * H, SESSION, "/opt/Obsidian/obsidian")
for i, rss in enumerate([90000, 140000, 60000]):
    p(671 + i, 670, "obsidian", ME, rss, 2 * H, SESSION, "/opt/Obsidian/obsidian")

# terminal 1: editing
p(650, 600, "alacritty", ME, 120000, 3 * H, SESSION, "/usr/bin/alacritty")
p(651, 650, "zsh", ME, 7000, 3 * H, SESSION, "/usr/bin/zsh", sid=651)
p(652, 651, "nvim", ME, 95000, 2 * H, SESSION, "/usr/bin/nvim", ["nvim", "src/cart.ts"], sid=651,
  cwd=HOME + "/code/shop")

# terminal 2: the shop's dev server (npm -> sh -> node vite)
SHOP = HOME + "/code/shop"
p(660, 600, "alacritty", ME, 118000, 2 * H, SESSION, "/usr/bin/alacritty")
p(661, 660, "zsh", ME, 7000, 2 * H, SESSION, "/usr/bin/zsh", sid=661)
p(662, 661, "npm run dev", ME, 60000, 95 * 60, SESSION, "/usr/bin/node", ["npm run dev"], sid=661, cwd=SHOP)
p(663, 662, "sh", ME, 2000, 95 * 60, SESSION, "/usr/bin/bash", ["sh", "-c", "vite"], sid=661, cwd=SHOP)
p(664, 663, "node", ME, 180000, 95 * 60, SESSION, "/usr/bin/node",
  ["/usr/bin/node", SHOP + "/node_modules/.bin/vite"], sid=661, cwd=SHOP)
listen(664, "127.0.0.1", 5173)

# a forgotten API server: its terminal was closed, systemd --user adopted it
API = HOME + "/code/api"
p(700, 500, "python3", ME, 70000, 2 * 86400 // 2, SESSION, "/usr/bin/python3.13",
  ["/usr/bin/python3", "manage.py", "runserver", "8000"], cwd=API)
listen(700, "127.0.0.1", 8000)

# --------------------------------------------------------------------------- write it
shutil.rmtree(OUT, ignore_errors=True)
os.makedirs(os.path.join(PROC, "net"))
with open(os.path.join(PROC, "uptime"), "w") as f:
    f.write(f"{UPTIME}.00 0.00\n")

fd_of = {}
for pr in procs:
    d = os.path.join(PROC, str(pr["pid"]))
    os.makedirs(os.path.join(d, "fd"))
    start = (UPTIME - pr["age"]) * HZ
    with open(os.path.join(d, "stat"), "w") as f:
        f.write(f"{pr['pid']} ({pr['comm']}) S {pr['ppid']} 0 {pr['sid']} "
                + " ".join(["0"] * 15) + f" {start} 0 0\n")
    with open(os.path.join(d, "status"), "w") as f:
        u = pr["uid"]
        f.write(f"Name:\t{pr['comm']}\nUid:\t{u}\t{u}\t{u}\t{u}\n")
        if pr["rss"]:
            f.write(f"VmRSS:\t{pr['rss']:8d} kB\n")
    with open(os.path.join(d, "cmdline"), "wb") as f:
        f.write(b"".join(x.encode() + b"\0" for x in pr["argv"]))
    with open(os.path.join(d, "cgroup"), "w") as f:
        f.write(f"0::{pr['cgroup']}\n")
    if pr["exe"]:
        os.symlink(pr["exe"], os.path.join(d, "exe"))
    if pr["cwd"]:
        os.symlink(pr["cwd"], os.path.join(d, "cwd"))

HDR = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
tcp, tcp6 = [HDR], [HDR]
for n, (pid, ino, local, v6) in enumerate(sockets):
    remote = ("0" * 32 if v6 else "00000000") + ":0000"
    (tcp6 if v6 else tcp).append(f"  {n:3d}: {local} {remote} 0A 00000000:00000000 00:00000000 "
                                 f"00000000  1000        0 {ino} 1\n")
    if pid is not None:
        fd_of[pid] = fd_of.get(pid, 9) + 1
        os.symlink(f"socket:[{ino}]", os.path.join(PROC, str(pid), "fd", str(fd_of[pid])))
for name, lines in (("tcp", tcp), ("tcp6", tcp6)):
    with open(os.path.join(PROC, "net", name), "w") as f:
        f.writelines(lines)

# installed apps
apps = os.path.join(OUT, "share", "applications")
os.makedirs(apps)
for fname, name, exec_, cats in [
    ("firefox", "Firefox", "firefox %u", "Network;WebBrowser;"),
    ("spotify", "Spotify", "spotify --uri=%u", "Audio;Music;"),
    ("obsidian", "Obsidian", "obsidian %u", "Office;"),
    ("Alacritty", "Alacritty", "alacritty", "System;TerminalEmulator;"),
]:
    with open(os.path.join(apps, fname + ".desktop"), "w") as f:
        f.write(f"[Desktop Entry]\nType=Application\nName={name}\nExec={exec_}\nCategories={cats}\n")

# fake docker: `docker ps --format ...` and `docker stop NAME`
bindir = os.path.join(OUT, "bin")
os.makedirs(bindir)
docker = os.path.join(bindir, "docker")
with open(docker, "w") as f:
    f.write(f'''#!/bin/sh
# fake docker for the magips demo: two containers, `docker stop` remembers
stopped="{OUT}/docker-stopped"
touch "$stopped"
case "$1" in
ps)
    printf '%s\\n' \\
        'a1b2c3d4e5f6	shop-redis-1	redis:7	127.0.0.1:6379->6379/tcp	Up 3 hours' \\
        'd4e5f6a1b2c3	shop-mongo-1	mongo:7	27017/tcp	Up 3 hours (healthy)' |
    while IFS= read -r line; do
        name=$(printf '%s' "$line" | cut -f2)
        grep -qxF "$name" "$stopped" || printf '%s\\n' "$line"
    done
    ;;
stop)
    echo "$2" >> "$stopped"
    echo "$2"
    ;;
esac
''')
os.chmod(docker, 0o755)

with open(os.path.join(OUT, "env.sh"), "w") as f:
    f.write(f'''# source me: point magips at the fake machine
export MAGIPS_PROC="{PROC}"
export HOME="{HOME}"
export XDG_CONFIG_HOME="{OUT}/config"
export XDG_DATA_HOME="{OUT}/share"
export XDG_DATA_DIRS="{OUT}/nowhere"
export PATH="{bindir}:{REPO}:$PATH"
export PS1='$ '
''')
print("demo machine in", OUT)
