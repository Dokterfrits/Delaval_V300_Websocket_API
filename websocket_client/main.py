import requests
import websocket
import json
import time
import ssl
import threading
import os
from datetime import datetime

# URLs
SALT_URL = "https://amssc.vms.delaval.com:8445/get_salt"
LOGIN_URL = "https://amssc.vms.delaval.com:8445/login"
UUID_URL = "wss://amssc.vms.delaval.com:8443/ws"

MODES = ["auto", "manual", "activatedelayedrel", "activatemanualclosedstall"]

BASE_DIR = os.path.dirname(os.path.abspath(__file__))  # Get the current script's directory
CONFIG_PATH = os.path.join(BASE_DIR, "../config.json")  # Adjust the path to find config.json

with open(CONFIG_PATH, "r") as f:
    config = json.load(f)

# User credentials
USERNAME = config["username"]
PASSWORD = config["password"]
WS_URLS = config["urls"]
WS_URLS.insert(0, UUID_URL)

# User session data (to be filled after login)
SESSION_USER = {}
AUTH_TOKEN = ""

websocket_connections = {}  # Store active WebSocket connections
connection_start_times = {}  # Store start times for each WebSocket
machine_states = {}  # Store latest state per machine


def get_salt(username):
    """Fetches salt and hashing parameters for the user."""
    response = requests.post(SALT_URL, json={"username": username, "application": "VmsFarm"})
    print("Asking for salt")
    if response.ok:
        return response.json()
    print("Error getting salt:", response.text)
    return None


def compute_scrypt(password, salt, params):
    """Computes the scrypt hash with the given parameters."""
    import hashlib
    import binascii

    key = hashlib.scrypt(
        password.encode(),
        salt=bytes([(x + 256) % 256 for x in salt]),  # Convert signed to unsigned bytes
        n=params["N"],
        r=params["r"],
        p=params["p"],
        dklen=params["keyLen"],
    )
    return list(key)


def login(username, password):
    """Logs in and retrieves session details."""
    global AUTH_TOKEN

    salt_data = get_salt(username)
    if not salt_data:
        return None

    hashed_passwords = [
        {
            "key": compute_scrypt(password, entry["salt"], entry["params"]),
            "salt": entry["salt"],
            "params": entry["params"],
            "isHashed": True,
        }
        for entry in salt_data
    ]

    login_payload = {
        "username": username,
        "password1": json.dumps(hashed_passwords[0]),
        "password2": json.dumps(hashed_passwords[1]),
        "password4": "",
        "ttl": 999999,
        "application": "VmsFarm",
        "log": "Automated Login",
    }

    response = requests.post(LOGIN_URL, json=login_payload, verify=False)  # Disable SSL for HTTP requests

    if response.ok:
        print("Login successful!")
        AUTH_TOKEN = response.text.strip()  # Extract token directly from the raw response text
        print("Token extracted:", AUTH_TOKEN)
        return True
    else:
        print("Login failed:", response.text)
        return False


def generate_rc_session_user(auth_response):
    """Extracts user data from authentication response and formats rcSessionUser."""
    user_data = auth_response.get("user", {})

    return {
        "firstName": user_data.get("firstName", ""),
        "lastName": user_data.get("lastName", ""),
        "username": user_data.get("username", ""),
        "uuid": user_data.get("uuid", ""),
        "roles": user_data.get("roles", []),
        "language": user_data.get("language", ""),
        "sessionCreated": int(time.time() * 1000),  # Current timestamp in milliseconds
        "rcLocked": False
    }


def on_message(ws, message, machine_index):
    """Handles incoming WebSocket messages."""
    global SESSION_USER
    try:
        data = json.loads(message)

        if str(data).startswith("{'isOk': True, 'user': {'uuid':") and not SESSION_USER:
            SESSION_USER = generate_rc_session_user(data)
            print(f"{datetime.now().strftime('%m-%d %H:%M:%S')} [Machine {machine_index}] :  Updated SESSION_USER:", SESSION_USER)

        elif not str(data).startswith("{'ms': {'stall': {'orientation': ") and data.get("messType") != "IdlePoll":
            print(f"{datetime.now().strftime('%m-%d %H:%M:%S')} [Machine {machine_index}] :", data)

        # Extract and store the mode if present
        if "ms" in data:
            main_mode = data["ms"].get("mainMode")  # Extract mainMode
            closed_stall = data["ms"].get("stall", {}).get("manualClosedStall")  # Check stall state

            if closed_stall == "active":
                machine_states[machine_index] = "activatemanualclosedstall"
            elif main_mode:
                machine_states[machine_index] = main_mode

    except json.JSONDecodeError:
        print(f"[Machine {machine_index}] Failed to decode message:", message)


def on_error(ws, error):
    """Handles WebSocket errors."""
    print("WebSocket error:", error)


def on_close(ws, machine_index, close_status_code, close_msg):
    """Handles WebSocket closure."""
    print(datetime.now().strftime('%m-%d %H:%M:%S'))
    print("WebSocket closed")
    runtime = time.time() - connection_start_times.get(machine_index, time.time())
    print(f"WebSocket {machine_index} closed after {runtime:.2f} seconds. Attempting reconnect...")
    websocket_connections.pop(machine_index, None)
    time.sleep(5)  # Wait before retrying
    print(f"Reconnecting WebSocket {machine_index}...")
    create_ws_connection(machine_index)# Re-establish connection


def on_open(ws, machine_index):
    """Sends authentication messages when WebSocket is opened."""
    print(f"WebSocket {machine_index} connected. Sending authentication messages...")

    messages = [
        {"messType": "WebMuuiSubscribeMsModelReq"},
        {"messType": "AuthorizeReq", "token": AUTH_TOKEN, "isPromise": True, "rcSessionUser": SESSION_USER},
        {"messType": "AuthorizeReq", "token": AUTH_TOKEN, "isPromise": False, "rcSessionUser": SESSION_USER},
        {"messType": "WebMuuiMsModelReq"},
    ]

    for msg in messages:
        time.sleep(1)
        ws.send(json.dumps(msg))
        # print(f"\n Sent to Machine {machine_index}:", msg)

    # Store WebSocket connection
    websocket_connections[machine_index] = ws
    connection_start_times[machine_index] = time.time()


def send_idle_poll():
    """Continuously sends IdlePoll messages to all connected machines every 30 seconds."""
    while True:
        time.sleep(30)
        for machine_index, ws in websocket_connections.items():
            idle_poll_message = {"messType": "IdlePoll"}
            ws.send(json.dumps(idle_poll_message))


def send_mode_change(machine_index, mode_index):
    """Send a mode change message to a specific machine with selected mode."""
    if machine_index in websocket_connections and 0 <= mode_index < len(MODES):
        ws = websocket_connections[machine_index]  # Get correct WebSocket
        session_user = SESSION_USER.copy()
        mode_message = {
            "messType": "WebMuuiModeReq",
            "rcSessionUser": session_user,
            "mode": MODES[mode_index]
        }

        # Send the complete message to the selected WebSocket machine
        ws.send(json.dumps(mode_message))
        print(f"Sent mode '{MODES[mode_index]}' to machine {machine_index}: ")
        # print(f"{mode_message}")
    else:
        print(f"Invalid machine index or mode index.")


def create_ws_connection(machine_index):
    """Create WebSocket connection for a specific machine."""
    ws_url = WS_URLS[machine_index]

    ws = websocket.WebSocketApp(
        ws_url,
        header=[
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:134.0) Gecko/20100101 Firefox/134.0",
            "Origin: https://vms.delaval.com",
            "Sec-WebSocket-Version: 13",
        ],
        on_error=on_error,
        on_close=on_close,
        on_open=lambda ws: on_open(ws, machine_index),
        on_message=lambda ws, msg: on_message(ws, msg, machine_index)
    )

    ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE})


def connect_all_machines():
    """Start WebSocket connections for all machines."""
    print("Connecting to WebSockets...")
    for index in range(len(WS_URLS)):
        ws_thread = threading.Thread(target=create_ws_connection, args=(index,))
        ws_thread.start()


# **RUN EVERYTHING**
if login(USERNAME, PASSWORD):
    # Start connections
    connect_all_machines()
    # Start sending idle poll messages
    idle_poll_thread = threading.Thread(target=send_idle_poll, daemon=True)
    idle_poll_thread.start()
else:
    print("Login failed. WebSocket will not start.")
