"""
Cactus Server — HTTPS-сервер для M5Stick.

Запуск:
    pip install -r requirements.txt
    python server.py

Первый запуск автоматически создаёт cert.pem / key.pem.
Пропиши в M5Stick config.h:
    #define SERVER_URL "https://<твой-ip>:8443/api/cactus"
"""

import os
import json
import sqlite3
import datetime
import ipaddress

from flask import Flask, request, jsonify, render_template_string

import config

app = Flask(__name__)

# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------

def get_db():
    db = sqlite3.connect(config.DB_FILE)
    db.row_factory = sqlite3.Row
    return db


def init_db():
    with get_db() as db:
        db.execute("""
            CREATE TABLE IF NOT EXISTS readings (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                ts          INTEGER NOT NULL,
                humidity    INTEGER NOT NULL,
                light       INTEGER NOT NULL,
                pump        INTEGER NOT NULL,
                received_at TEXT    NOT NULL
            )
        """)
        db.execute("""
            CREATE TABLE IF NOT EXISTS commands (
                key   TEXT PRIMARY KEY,
                value TEXT
            )
        """)


# ---------------------------------------------------------------------------
# Pending command queue (stored in DB so survives restart)
# ---------------------------------------------------------------------------

def _get_cmd(key):
    with get_db() as db:
        row = db.execute("SELECT value FROM commands WHERE key=?", (key,)).fetchone()
        return row["value"] if row else None


def _set_cmd(key, value):
    with get_db() as db:
        if value is None:
            db.execute("DELETE FROM commands WHERE key=?", (key,))
        else:
            db.execute("INSERT OR REPLACE INTO commands(key,value) VALUES(?,?)", (key, value))


def _pop_cmd(key):
    """Return pending command and clear it atomically."""
    val = _get_cmd(key)
    if val:
        _set_cmd(key, None)
    return val


# ---------------------------------------------------------------------------
# Self-signed certificate generation
# ---------------------------------------------------------------------------

def ensure_cert():
    if os.path.exists(config.CERT_FILE) and os.path.exists(config.KEY_FILE):
        return

    print("Generating self-signed certificate...")
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, "cactus-server"),
    ])

    san = [x509.DNSName("localhost")]
    for ip_str in config.CERT_IPS:
        san.append(x509.IPAddress(ipaddress.IPv4Address(ip_str)))

    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime.utcnow())
        .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=3650))
        .add_extension(x509.SubjectAlternativeName(san), critical=False)
        .sign(key, hashes.SHA256())
    )

    with open(config.KEY_FILE, "wb") as f:
        f.write(key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption(),
        ))
    with open(config.CERT_FILE, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))

    print(f"  {config.CERT_FILE} and {config.KEY_FILE} created (valid 10 years)")


# ---------------------------------------------------------------------------
# API — M5Stick endpoint
# ---------------------------------------------------------------------------

@app.route("/api/cactus", methods=["POST"])
def cactus_post():
    data = request.get_json(force=True, silent=True)
    if not data:
        return jsonify({"error": "bad json"}), 400

    humidity = int(data.get("humidity", 0))
    light    = bool(data.get("light", False))
    pump     = bool(data.get("pump", False))
    ts       = int(data.get("ts", 0))
    received_at = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")

    with get_db() as db:
        db.execute(
            "INSERT INTO readings(ts,humidity,light,pump,received_at) VALUES(?,?,?,?,?)",
            (ts, humidity, int(light), int(pump), received_at),
        )

    # Build response — only include fields that have pending values
    resp = {}
    for key in ("cmd_light", "cmd_pump"):
        val = _pop_cmd(key)
        if val:
            resp[key] = val

    return jsonify(resp)


# ---------------------------------------------------------------------------
# API — dashboard / control
# ---------------------------------------------------------------------------

@app.route("/api/command", methods=["POST"])
def set_command():
    """Queue a command to be sent to M5Stick on the next poll.
    Body: {"cmd_light": "on"} or {"cmd_pump": "off"}
    """
    data = request.get_json(force=True, silent=True)
    if not data:
        return jsonify({"error": "bad json"}), 400

    queued = {}
    for key in ("cmd_light", "cmd_pump"):
        if key in data:
            val = data[key]
            if val not in ("on", "off", None):
                return jsonify({"error": f"invalid value for {key}"}), 400
            _set_cmd(key, val)
            queued[key] = val

    return jsonify({"ok": True, "queued": queued})


@app.route("/api/readings")
def api_readings():
    limit = min(int(request.args.get("limit", 100)), 1000)
    with get_db() as db:
        rows = db.execute(
            "SELECT id,ts,humidity,light,pump,received_at FROM readings ORDER BY id DESC LIMIT ?",
            (limit,),
        ).fetchall()
    return jsonify([dict(r) for r in rows])


@app.route("/api/pending")
def api_pending():
    return jsonify({
        "cmd_light": _get_cmd("cmd_light"),
        "cmd_pump":  _get_cmd("cmd_pump"),
    })


# ---------------------------------------------------------------------------
# Dashboard
# ---------------------------------------------------------------------------

_DASHBOARD = """<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cactus Monitor</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: monospace; background: #111; color: #ccc; margin: 0; padding: 16px; }
  h1 { color: #7cfc00; margin: 0 0 16px; }
  .row { display: flex; gap: 16px; flex-wrap: wrap; margin-bottom: 20px; }
  .card { background: #1e1e1e; border: 1px solid #333; border-radius: 6px; padding: 14px; }
  .card h2 { margin: 0 0 10px; font-size: 14px; color: #888; text-transform: uppercase; }
  .big { font-size: 2em; color: #fff; }
  button { padding: 8px 14px; margin: 3px; cursor: pointer; font-family: monospace;
           font-size: 13px; border-radius: 4px; border: 1px solid; }
  .btn-on  { background: #1a3a0a; color: #7cfc00; border-color: #7cfc00; }
  .btn-off { background: #3a0a0a; color: #fc6060; border-color: #fc6060; }
  .btn-on:hover  { background: #2a5a14; }
  .btn-off:hover { background: #5a1414; }
  #status { margin-left: 10px; color: #7cfc00; font-size: 13px; }
  table { border-collapse: collapse; width: 100%; max-width: 860px; font-size: 13px; }
  th, td { border: 1px solid #333; padding: 5px 10px; text-align: left; }
  th { background: #1e1e1e; color: #888; }
  tr:nth-child(even) td { background: #161616; }
  .on  { color: #7cfc00; }
  .off { color: #555; }
  #pending { font-size: 12px; color: #fa0; margin-top: 6px; min-height: 18px; }
</style>
</head>
<body>
<h1>Cactus Monitor</h1>

<div class="row">
  <div class="card">
    <h2>Влажность</h2>
    <div class="big" id="hum">—</div>
  </div>
  <div class="card">
    <h2>Свет</h2>
    <div class="big" id="light">—</div>
  </div>
  <div class="card">
    <h2>Насос</h2>
    <div class="big" id="pump">—</div>
  </div>
  <div class="card">
    <h2>Последний сигнал</h2>
    <div class="big" style="font-size:1em" id="last_seen">—</div>
  </div>
</div>

<div class="card" style="display:inline-block;margin-bottom:20px">
  <h2>Команды (применятся при следующем опросе)</h2>
  <button class="btn-on"  onclick="cmd('cmd_light','on')">Свет ON</button>
  <button class="btn-off" onclick="cmd('cmd_light','off')">Свет OFF</button>
  &nbsp;
  <button class="btn-on"  onclick="cmd('cmd_pump','on')">Насос ON</button>
  <button class="btn-off" onclick="cmd('cmd_pump','off')">Насос OFF</button>
  <span id="status"></span>
  <div id="pending"></div>
</div>

<table>
  <thead><tr><th>Время (UTC)</th><th>Humidity</th><th>Свет</th><th>Насос</th></tr></thead>
  <tbody id="tbody"></tbody>
</table>

<script>
function cmd(key, val) {
  fetch('/api/command', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({[key]: val})
  }).then(r => r.json()).then(() => {
    const s = document.getElementById('status');
    s.textContent = ' ✓ ' + key + '=' + val;
    setTimeout(() => s.textContent = '', 4000);
    refreshPending();
  });
}

function refreshPending() {
  fetch('/api/pending').then(r => r.json()).then(p => {
    const parts = [];
    if (p.cmd_light) parts.push('cmd_light=' + p.cmd_light);
    if (p.cmd_pump)  parts.push('cmd_pump='  + p.cmd_pump);
    document.getElementById('pending').textContent =
      parts.length ? 'В очереди: ' + parts.join(', ') : '';
  });
}

function refresh() {
  fetch('/api/readings?limit=50').then(r => r.json()).then(rows => {
    const tbody = document.getElementById('tbody');
    tbody.innerHTML = '';
    rows.forEach((r, i) => {
      const tr = document.createElement('tr');
      tr.innerHTML =
        '<td>' + r.received_at + '</td>' +
        '<td>' + r.humidity + '%</td>' +
        '<td class="' + (r.light ? 'on' : 'off') + '">' + (r.light ? 'ON' : 'off') + '</td>' +
        '<td class="' + (r.pump  ? 'on' : 'off') + '">' + (r.pump  ? 'ON' : 'off') + '</td>';
      tbody.appendChild(tr);
      if (i === 0) {
        document.getElementById('hum').textContent   = r.humidity + '%';
        document.getElementById('light').className   = 'big ' + (r.light ? 'on' : 'off');
        document.getElementById('light').textContent = r.light ? 'ON' : 'off';
        document.getElementById('pump').className    = 'big ' + (r.pump  ? 'on' : 'off');
        document.getElementById('pump').textContent  = r.pump  ? 'ON' : 'off';
        document.getElementById('last_seen').textContent = r.received_at;
      }
    });
  });
  refreshPending();
}

refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>"""


@app.route("/")
def dashboard():
    return _DASHBOARD


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    ensure_cert()
    init_db()

    print()
    print("=" * 56)
    print(f"  HTTPS server: https://0.0.0.0:{config.PORT}")
    print(f"  Dashboard:    https://localhost:{config.PORT}/")
    print()
    print("  Прожги в M5Stick config.h:")
    print(f'    #define SERVER_URL "https://<IP>:{config.PORT}/api/cactus"')
    print("  Узнать свой IP:  ipconfig  (Windows) / ip a  (Linux)")
    print("=" * 56)
    print()

    app.run(
        host=config.HOST,
        port=config.PORT,
        ssl_context=(config.CERT_FILE, config.KEY_FILE),
        debug=False,
    )
