from flask import Flask, render_template_string
import sqlite3
import json
from datetime import datetime
import paho.mqtt.client as mqtt

MQTT_HOST = "10.161.234.114"
MQTT_PORT = 1883
MQTT_USERNAME = "door_user"
MQTT_PASSWORD = "door_pass"
MQTT_TOPIC_EVENT = "door/door_001/event"

DB_PATH = "cloud_records.db"

app = Flask(__name__)


HTML = """
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta http-equiv="refresh" content="3">
    <title>智能门禁云平台</title>
    <style>
        body {
            font-family: Arial, "Microsoft YaHei", sans-serif;
            margin: 40px;
            background: #f5f5f5;
        }
        h1 {
            color: #333;
        }
        .tip {
            color: #666;
            margin-bottom: 16px;
        }
        table {
            border-collapse: collapse;
            width: 100%;
            background: white;
        }
        th, td {
            border: 1px solid #ddd;
            padding: 8px 10px;
            text-align: left;
        }
        th {
            background: #333;
            color: white;
        }
        .alarm {
            color: red;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <h1>智能门禁历史数据查询</h1>
    <div class="tip">页面每 3 秒自动刷新一次。</div>

    <table>
        <tr>
            <th>本地记录ID</th>
            <th>设备ID</th>
            <th>事件类型</th>
            <th>用户ID</th>
            <th>认证方式</th>
            <th>报警</th>
            <th>设备时间</th>
            <th>云平台接收时间</th>
        </tr>
        {% for r in records %}
        <tr>
            <td>{{ r["record_id"] }}</td>
            <td>{{ r["device_id"] }}</td>
            <td>{{ r["event_type"] }}</td>
            <td>{{ r["user_id"] }}</td>
            <td>{{ r["method"] }}</td>
            <td class="{{ 'alarm' if r['alarm'] else '' }}">
                {{ r["alarm"] }}
            </td>
            <td>{{ r["time"] }}</td>
            <td>{{ r["cloud_time"] }}</td>
        </tr>
        {% endfor %}
    </table>
</body>
</html>
"""


def init_db():
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()

    cur.execute("""
        CREATE TABLE IF NOT EXISTS records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            record_id INTEGER,
            device_id TEXT,
            event_type TEXT,
            user_id TEXT,
            method TEXT,
            alarm INTEGER,
            time TEXT,
            uptime_ms INTEGER,
            cloud_time TEXT,
            raw_json TEXT
        )
    """)

    conn.commit()
    conn.close()


def insert_record(data):
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()

    cur.execute("""
        INSERT INTO records
        (record_id, device_id, event_type, user_id, method, alarm, time, uptime_ms, cloud_time, raw_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        data.get("record_id", 0),
        data.get("device_id", ""),
        data.get("event_type", ""),
        data.get("user_id", ""),
        data.get("method", ""),
        1 if data.get("alarm", False) else 0,
        data.get("time", ""),
        data.get("uptime_ms", 0),
        datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        json.dumps(data, ensure_ascii=False)
    ))

    conn.commit()
    conn.close()


def on_connect(client, userdata, flags, rc):
    print("MQTT connected, rc =", rc)

    if rc == 0:
        result, mid = client.subscribe(MQTT_TOPIC_EVENT, qos=1)
        print("Subscribe result =", result, "mid =", mid, "topic =", MQTT_TOPIC_EVENT)
    else:
        print("MQTT connect failed, rc =", rc)


def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8", errors="ignore")

    print("=" * 80)
    print("MQTT received")
    print("topic:", msg.topic)
    print("payload:", payload)

    try:
        data = json.loads(payload)
    except Exception as e:
        print("JSON parse failed:", e)
        return

    try:
        insert_record(data)
        print("Event saved to cloud DB, record_id =", data.get("record_id"))
    except Exception as e:
        print("Insert cloud DB failed:", e)


@app.route("/")
def index():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    cur.execute("""
        SELECT * FROM records
        ORDER BY id DESC
        LIMIT 200
    """)

    records = cur.fetchall()
    conn.close()

    return render_template_string(HTML, records=records)


def start_mqtt():
    client = mqtt.Client()

    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

    client.on_connect = on_connect
    client.on_message = on_message

    print("Connecting MQTT broker:", MQTT_HOST, MQTT_PORT)
    print("Subscribing topic:", MQTT_TOPIC_EVENT)

    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_start()

    return client


if __name__ == "__main__":
    init_db()
    mqtt_client = start_mqtt()

    app.run(host="0.0.0.0", port=5000, debug=False, use_reloader=False)