from flask import Flask, request, jsonify
from websocket_client.main import connect_all_machines, send_mode_change, MODES, websocket_connections
import json
import threading
import time

app = Flask(__name__)

# Load config
with open("config.json", "r") as f:
    config = json.load(f)

WS_URLS = config["urls"]


@app.route('/mode/<code>')
def set_mode(code):
    if len(code) != 2:
        return "Invalid code format", 400  # Machine (1 digit) + Mode (1 digit) = 2 characters

    try:
        machine_index = int(code[0])  # First digit for machine (convert to integer)
        mode_index = int(code[1])  # Second digit for mode (convert to integer)
    except ValueError:
        return "Invalid code format", 400  # Non-numeric input

    print(f"Decoded: Machine {machine_index}, Mode {mode_index}")  # Debugging

    if machine_index in websocket_connections and 0 <= mode_index < len(MODES):
        send_mode_change(machine_index, mode_index)
        return f"Changed machine {machine_index} to {MODES[mode_index]} mode", 200
    else:
        return "Invalid machine or mode index", 400  # If out of range or not found



# Start WebSocket connections in a separate thread
def start_websockets():
    time.sleep(2)
    connect_all_machines()


if __name__ == "__main__":
    ws_thread = threading.Thread(target=start_websockets, daemon=True)
    ws_thread.start()
    app.run(host="0.0.0.0", port=5000, debug=True)
