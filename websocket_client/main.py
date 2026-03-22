import requests
import websocket
import json
import time
import ssl
import threading
import os
from datetime import datetime
from websocket_client.data_analysis import extract_data

BASE_DIR = os.path.dirname(os.path.abspath(__file__))  # Get the current script's directory
CONFIG_PATH = os.path.join(BASE_DIR, "../config.json")  # Adjust the path to find config.json

with open(CONFIG_PATH, "r") as f:
    config = json.load(f)

USERNAME = config["username"]
PASSWORD = config["password"]
WS_URLS = config["urls"]
SALT_URL = config["SALT_URL"]
LOGIN_URL = config["LOGIN_URL"]
UUID_URL = config["UUID_URL"]
WS_URLS.insert(0, UUID_URL)

# User session data (to be filled after login)
SESSION_USER = {}
AUTH_TOKEN = ""

websocket_connections = {}  # Store active WebSocket connections
connection_start_times = {}  # Store start times for each WebSocket


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
        return response.text.strip()  # return token
    else:
        print("Login failed:", response.text)
        return None


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
        # prints anything thats not normal traffic
        if str(data).startswith("{'isOk': True, 'user': {'uuid':") and not SESSION_USER:
            SESSION_USER = generate_rc_session_user(data)
            print(f"{datetime.now().strftime('%m-%d %H:%M:%S')} [Machine {machine_index}] :  Updated SESSION_USER:", SESSION_USER)

        elif not str(data).startswith("{'ms': {'stall': {'orientation': ") and data.get("messType") != "IdlePoll":
            print(f"{datetime.now().strftime('%m-%d %H:%M:%S')} [Machine {machine_index}] :", data)

        extract_data(data, machine_index)


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


def on_open(ws, machine_index, token):
    """Sends authentication messages when WebSocket is opened."""
    print(f"WebSocket {machine_index} connected. Sending authentication messages...")

    messages = [
        {"messType": "WebMuuiSubscribeMsModelReq"},
        {"messType": "AuthorizeReq", "token": token, "isPromise": True, "rcSessionUser": SESSION_USER},
        {"messType": "AuthorizeReq", "token": token, "isPromise": False, "rcSessionUser": SESSION_USER},
        {"messType": "WebMuuiMsModelReq"},
    ]

    for msg in messages:
        time.sleep(1)
        ws.send(json.dumps(msg))
        # print(f"\n Sent to Machine {machine_index}:", msg)



def send_idle_poll():
    """Continuously sends IdlePoll messages to all connected machines every 30 seconds."""
    while True:
        time.sleep(30)
        for machine_index, ws in websocket_connections.items():
            idle_poll_message = {"messType": "IdlePoll"}
            ws.send(json.dumps(idle_poll_message))




#
# def create_ws_connection(machine_index):
#     """Create WebSocket connection for a specific machine."""
#     ws_url = WS_URLS[machine_index]
#
#     ws = websocket.WebSocketApp(
#         ws_url,
#         header=[
#             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:134.0) Gecko/20100101 Firefox/134.0",
#             "Origin: https://vms.delaval.com",
#             "Sec-WebSocket-Version: 13",
#         ],
#         on_error=on_error,
#         on_close=on_close,
#         on_open=lambda ws: on_open(ws, machine_index),
#         on_message=lambda ws, msg: on_message(ws, msg, machine_index)
#     )
#
#     ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE})


def connect_all_machines():
    print("Starting machine workers...")
    for index in range(len(WS_URLS)):
        t = threading.Thread(target=machine_worker, args=(index,), daemon=True)
        t.start()


def machine_worker(machine_index):
    """Runs a persistent connection loop for a single machine."""
    global SESSION_USER

    while True:
        try:
            print(f"[Machine {machine_index}] Starting new session...")

            # Get fresh auth token
            token = login(USERNAME, PASSWORD)
            if not token:
                print(f"[Machine {machine_index}] Login failed. Retrying in 10s...")
                time.sleep(10)
                continue

            SESSION_USER = {}  # reset for fresh session

            ws_url = WS_URLS[machine_index]

            ws = websocket.WebSocketApp(
                ws_url,
                header=[
                    "User-Agent: Mozilla/5.0",
                    "Origin: https://vms.delaval.com",
                ],
                on_open=lambda ws: on_open(ws, machine_index, token),
                on_message=lambda ws, msg: on_message(ws, msg, machine_index),
                on_error=lambda ws, err: on_error(ws, err),
                on_close=lambda ws, code, msg: print(
                    f"{datetime.now().strftime('%m-%d %H:%M:%S')} [Machine {machine_index}] Closed"
                ),
            )

            websocket_connections[machine_index] = ws
            connection_start_times[machine_index] = time.time()

            # Run connection (blocks until it breaks)
            ws.run_forever(sslopt={"cert_reqs": ssl.CERT_NONE})

        except Exception as e:
            print(f"[Machine {machine_index}] Crash: {e}")

        # Cleanup after disconnect/crash
        websocket_connections.pop(machine_index, None)

        print(f"[Machine {machine_index}] Reconnecting in 5s...\n")
        time.sleep(5)

# **RUN EVERYTHING**
print("Starting system...")

# Start machine workers (they handle login internally)
connect_all_machines()

# Start idle poll thread
idle_poll_thread = threading.Thread(target=send_idle_poll, daemon=True)
idle_poll_thread.start()
