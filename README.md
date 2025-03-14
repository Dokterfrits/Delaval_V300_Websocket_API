# Delaval v300 AMS automatic webui authentication and remote control

## Overview

A python program to authenticate at the webui of the Delaval v300 automatic milking station AMS and establish a websocket connection to interact with the machine. It automates authentication and WebSocket communication with multiple AMS machines. It logs in using hashed credentials, establishes WebSocket connections, and can send control messages to specific machines.

Example code for a 433MHz remote control for the Delaval Milking Robot was added for an esp8266 with a generic 433MHz receiver, requesting server urls.

## Installation

1. **Clone the repository:**
   ```sh
   git clone https://github.com/Dokterfrits/Delaval_V300_Websocket_API
   cd Delaval_V300_Websocket_API

   ```
2. **Create a virtual environment (optional but recommended):**
   ```sh
   python -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   ```
3. **Install dependencies:**
   ```sh
   pip install -r requirements.txt
   ```
4. **Configuration:**

   Rename **config_template.json** to **config.json** and update the credentials and urls:
   ```json
   {
       "username": "your_username",
       "password": "your_password",
       "urls": [
          "wss://192.168.168.1/ws", // Your AMS on network. Variations on "wss://vms_1.vms.delaval.com/ws" might also work
          "wss://192.168.168.2/ws" // Second machine, add more lines if necessary
       ]
   }
   ```
5. **Run Server:**
   ```sh
   python run_server.py
   ```
   
## Usage

The Flask server exposes a URL endpoint that accepts mode change requests with machine and mode indices.
**Example URL:**

    localhost:5000/mode/10


Where:
- `1`: The machine index (machine 1 in this case).
- `0`: The mode index (0 refers to the first mode, e.g., "auto").

### Mode Options

The available modes are:
1. `"auto"`
2. `"manual"`
3. `"activatedelayedrel"`
4. `"activatemanualclosedstall"`

### Example Request

To change the mode of machine 2 to "manual", you would call:

    localhost:5000/mode/21

## Features

- Secure authentication with hashed credentials
- WebSocket connection to multiple machines
- 433MHz remote control implementation using esp8266 and generic receiver module
- Sends control messages dynamically to a selected machine
- Logs responses and identifies which machine sent them

## Troubleshooting

- If authentication fails, check the credentials in `config.json`.
- If SSL errors occur, try setting `verify=False` in requests.
- If no messages are received, ensure WebSocket URLs are correct.

## License

MIT License

