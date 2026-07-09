#!/usr/bin/env python3
"""cbackup Web GUI (EX-09).

A thin Flask "glue" layer. All business logic stays in the C++ ``cbackup``
binary; this server only:
  * renders the control dashboard, and
  * invokes ``cbackup`` via ``subprocess.Popen`` (shell disabled) while
    streaming its combined stdout/stderr back to the browser using
    Server-Sent Events (SSE) to emulate a live terminal.

Security notes:
  * ``shell=False`` + an explicit argv list => no shell command injection.
  * The subcommand is validated against an allow-list.
  * Only known flags are forwarded; unknown keys are ignored.
"""

import json
import os
import shlex
import subprocess
import sys

from flask import Flask, Response, request, render_template

app = Flask(__name__)

# Resolve the compiled C++ binary. Override with the CBACKUP_BIN env var.
CBACKUP_BIN = os.environ.get(
    "CBACKUP_BIN",
    os.path.join(os.path.dirname(__file__), "..", "build", "cbackup"),
)

# Allow-listed subcommands.
SUBCOMMANDS = {"backup", "restore", "pack", "unpack", "cron", "daemon"}


def build_argv(form):
    """Translate a validated form dict into a safe argv list."""
    sub = form.get("subcmd", "")
    if sub not in SUBCOMMANDS:
        raise ValueError("invalid subcommand: %r" % sub)

    argv = [CBACKUP_BIN, sub]

    # Simple string options: form-key -> CLI flag.
    str_opts = {
        "source": "--source",
        "dest": "--dest",
        "file": "--file",
        "watch": "--watch",
        "sync_to": "--sync-to",
        "log": "--log",
        "schedule": "--schedule",
        "keep": "--keep",
        "compress": "--compress",
        "encrypt": "--encrypt",
        "password": "-p",
        "include": "--include",
        "exclude": "--exclude",
        "type": "--type",
        "mtime": "--mtime",
        "size": "--size",
        "owner": "--owner",
    }
    for key, flag in str_opts.items():
        val = (form.get(key) or "").strip()
        if val:
            argv += [flag, val]

    # Boolean flags.
    if form.get("metadata") in ("1", "true", "on", "yes"):
        argv.append("-m")
    if form.get("verbose") in ("1", "true", "on", "yes"):
        argv.append("--verbose")

    return argv


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/stream")
def stream():
    """SSE endpoint. Runs cbackup and streams each output line as an event."""
    try:
        argv = build_argv(request.args)
    except ValueError as exc:
        def err_gen(msg):
            yield "event: error\ndata: %s\n\n" % json.dumps(str(msg))
            yield "event: done\ndata: 1\n\n"
        return Response(err_gen(exc), mimetype="text/event-stream")

    def generate(cmd):
        # Announce the exact command being executed (for transparency).
        yield "event: cmd\ndata: %s\n\n" % json.dumps(" ".join(shlex.quote(a) for a in cmd))
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=1,
                universal_newlines=True,
                shell=False,
            )
        except FileNotFoundError:
            yield "event: error\ndata: %s\n\n" % json.dumps(
                "cbackup binary not found: %s" % CBACKUP_BIN)
            yield "event: done\ndata: 1\n\n"
            return

        for line in iter(proc.stdout.readline, ""):
            yield "data: %s\n\n" % json.dumps(line.rstrip("\n"))
        proc.stdout.close()
        code = proc.wait()
        yield "event: done\ndata: %d\n\n" % code

    return Response(generate(argv), mimetype="text/event-stream")


if __name__ == "__main__":
    port = int(os.environ.get("PORT", "8080"))
    # Bind to 0.0.0.0 so the container port mapping works from the host.
    app.run(host="0.0.0.0", port=port, threaded=True)
