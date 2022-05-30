import sqlite3
import redis
import json
from flask import Flask, request, abort, make_response, jsonify
from config import config

app = Flask(__name__, static_url_path='/static')

db = redis.Redis(host=config['zdb-host'], port=config['zdb-port'])
print(db.info())

@app.route('/proof/verify/<nodeid>/<target>', methods=['POST'])
def proof_verify(nodeid, target):
    print(f"Verifying node {nodeid} target {target}")
    db.execute_command("SELECT storage-pool-request")

    challenge = db.get(f"node-{nodeid}-disk-{target}")
    payload = json.loads(challenge.decode("utf-8"))
    length = len(payload["results"])
    verify = request.json

    print("Comparing client response with internal data")

    valid = 0

    for entry in payload["results"]:
        if payload["results"][entry] == verify[entry]:
            valid += 1

    print(f"Confirmed values: {valid} / {length}")

    return jsonify({"valid": valid, "length": length})

@app.route('/proof/challenge/<nodeid>/<target>')
def proof_challenge(nodeid, target):
    print(f"Challenging node {nodeid} target {target}")
    db.execute_command("SELECT storage-pool-request")

    request = db.get(f"node-{nodeid}-disk-{target}")
    payload = json.loads(request.decode("utf-8"))

    offsets = list(payload["results"].keys())
    return jsonify(offsets)

def picksize(scan, size):
    for entry in scan:
        key = entry[0].decode("utf-8")
        skey = key.split("-")
        entrysize = int(skey[1])

        if size <= entrysize:
            print(f"Requested size {size} fits in {entrysize}")
            return skey, key

    return None, None

@app.route('/proof/request/<nodeid>/<target>/<size>')
def proof_request(nodeid, target, size):
    print("Looking into the pool for size %s" % size)

    size = int(size)

    db.execute_command("SELECT storage-pool")
    scan = db.execute_command("SCANX")

    while True:
        skey, key = picksize(scan[1], size)
        if key is not None:
            break

        try:
            scan = db.execute_command("SCANX", scan[0])

        except Exception:
            return "Pool unavailable, please try again later\n"

    print(key)
    seed = skey[2]

    payload = db.get(key)
    try:
        db.execute_command("DEL", key)
    except Exception:
        pass

    db.execute_command("SELECT storage-pool-request")
    db.execute_command("SET", f"node-{nodeid}-disk-{target}", payload)

    return jsonify({"seed": f"0x{seed}"})


@app.route('/')
def index():
    return "Capacity Proof Certifier\n"

print("[+] listening")
app.run(host="0.0.0.0", port=config['http-port'], debug=config['debug'], threaded=True)

