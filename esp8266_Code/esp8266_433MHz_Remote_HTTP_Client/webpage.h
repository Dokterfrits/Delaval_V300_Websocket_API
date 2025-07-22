#ifndef WEBPAGE_H
#define WEBPAGE_H

// store in flash so it doesn’t eat RAM
const char INDEX_HTML[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8" />
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <title>433MHz Code Editor</title><style>
        body {
          font-family: "Segoe UI", sans-serif;
          margin: 2em;
          background-color: #f0f2f5;
          color: #333;
          line-height: 1.6;
        }
        h1, h2, h3 {
          color: #2c3e50;
        }
        .section {
          margin-bottom: 2.5em;
          padding-bottom: 1em;
          border-bottom: 1px solid #ccc;
        }
        .grid {
          display: grid;
          grid-template-columns: 1fr 1fr;
          gap: 2em;
        }
        @media (max-width: 768px) {
          .grid {
            grid-template-columns: 1fr;
          }
        }
        textarea, pre {
          width: 100%;
          font-family: monospace;
          font-size: 0.95em;
          background: #fff;
          border: 1px solid #ccc;
          border-radius: 6px;
          padding: 0.75em;
          box-sizing: border-box;
        }
        textarea {
          resize: vertical;
          min-height: 150px;
        }
        button {
          background-color: #007bff;
          color: white;
          border: none;
          padding: 0.6em 1.5em;
          font-size: 1em;
          border-radius: 6px;
          margin-top: 0.5em;
          cursor: pointer;
          transition: background-color 0.2s ease;
        }
        button:hover {
          background-color: #0056b3;
        }
        pre {
          background: #f8f9fa;
          overflow-x: auto;
        }

        input[type="text"], input[type="number"] {
          width: 100%;
          padding: 0.5em;
          font-size: 1em;
          border-radius: 4px;
          border: 1px solid #ccc;
          box-sizing: border-box;
        }
      </style>
      

    </head>
    <body>
      <h1>433mhz remote configuration</h1>
      <div class="section">
        <h2>General Configuration</h2>
        <form id="configForm">
          <label>
            Server IP: <br>
            <details>
              <summary style="cursor: pointer; color: #007bff;">Explain</summary>
              <div>
                This is the ip adress where the python server runs, not the ip of this receiver ESP8266.
                Example: <code>192.168.1.100</code>
              </div>
            </details>
            <input type="text" id="serverIP" name="serverIP" required>
          </label><br><br>
        
          <label>
            Number of AMS Stations: <br>
            <details>
              <summary style="cursor: pointer; color: #007bff;">Explain</summary>
              <div>
                Input the number of AMS stations you would like to control. Make sure your json below has codes and station numbers matching below. Hook up an RGB LED (type WS2812 in a string) to your esp for each station. 

                Lock functionality: To prevent accidental button presses the remote can be locked with a longpress. The button used for this is registerd in the json below, as the number of stations you input here +1. For 3 AMS stations, button 4 will be the lock/unlock button. Which button suits best for this function on the layout of your remote, can be set in the json too. The code of your favorite lock button should be matched to '4' in this example with 3 stations, as the first 3 numbers are matched to button-codes that you want to use to control the stations.
              </div>
            </details>
            <input type="number" id="numberofStations" name="numberofStations" required>
          </label><br><br>
        
          <label>
            Long Press Threshold: <br>
            <details>
              <summary style="cursor: pointer; color: #007bff;">Explain</summary>
              <div>
                 Commonly remotes send the same code repeatedly when a button is pressed longer. Even a short press can sometimes send 2 or 3 repeated codes, as you can test out below. This threshold sets the number of repeated codes before the series of repeated codes will be registerd as a long press. With an interval of 80ms or so between repeated codes a threshold of 6 seems fine. Shorter will trigger false long presses when you meant to send a short press and held it a bit too long. Longer will slow down the reaction time before a longpress is registerd and executed.</div>
            </details>
            <input type="number" id="longPressThreshold" name="longPressThreshold" required>
          </label><br><br>
        
          <label>
            Repeat Interval (ms):  <br>
            <details>
              <summary style="cursor: pointer; color: #007bff;">Explain</summary>
              <div>
                When a button is quickly pressed two times, it might trigger a longpress mistakenly. This happens when the longpress threshold was set to 6 for example, and 5 repetitions were registed before letting go of the button press. A short press directly afterwards (of the same button) could accidentaly trigger a long press this way, as it reaches the threshold of 6 that was set. This value basically sets the cooldown in ms between consecutive button presses. Whitin this time a second press counts towards the longpress thershold, slower and a new button press duration will be meassured.
              </div>
            </details>
            <input type="number" id="repeatInterval" name="repeatInterval" required>
          </label><br><br>
        
          <button type="submit">Save Config</button>
        </form>
      </div>
    
      <h1>433MHz Remote Code Editor</h1>

      <div class="section">
        <h2>What is this?</h2>
        <p>This page lets you manage how 433MHz RF codes from your remote control map to the AMS station numbers that you would like to control.</p>
      </div>

      <div class="section grid">
        <div>
          <h2>Current Mappings (Editable)</h2>
          <p>This is what your JSON currently looks like:</p>
          <pre id="json">(loading...)</pre>
        </div>

        <div>
          <h2>Example Format</h2>
          <p>This is how your JSON should look:</p>
          <pre>{
  "5358760": 1,
  "5358756": 2,
  "5358753": 3,
  "5358754": 4,
  "10826945": 1,
  "10826946": 2,
  "10826948": 3,
  "10826952": 4
}</pre>
          <p>Use the "Recent Codes" section below to copy new values.</p>
        </div>
      </div>

      <div class="section">
        <h3>Edit and Save</h3>
        <textarea id="input" rows="10"></textarea>
        <button onclick="saveJson()">Save Changes</button>
      </div>
      
      <div class="section grid">
        <div>
          <h2>Recent 433MHz Codes</h2>
          <p>This list updates every second with the last few codes received.</p>
          <pre id="codes">(waiting...)</pre>
        </div>
  
        <div>
          <h2>Recent 433MHz Codes Time Delta</h2>
          <p>This is the time(ms) between the previous and current 433MHz code..</p>
          <pre id="codetimes">(waiting...)</pre>
        </div>
      </div>

      <script>
        function refreshJson() {
          fetch('/get').then(r => r.text()).then(t => {
            json.innerText = t;
            input.value = t;
          });
        }

        function refreshCodes() {
          fetch('/recent').then(r => r.text()).then(t => {
            codes.innerText = t.trim();
          });
        }

        function refreshTimes() {
          fetch('/recenttimes').then(r => r.text()).then(t => {
            codetimes.innerText = t.trim();
          });
        }


        function saveJson() {
          fetch('/set', {
            method: 'POST',
            body: input.value
          }).then(res => {
            if (res.ok) {
              alert("Saved!");
              refreshJson();
            } else {
              alert("Error saving: check your JSON format.");
            }
          });
        }

        function refreshConfig() {
          fetch('/getconfig').then(r => r.json()).then(cfg => {
            document.getElementById("serverIP").value = cfg.serverIP || "";
            document.getElementById("numberofStations").value = cfg.numberofStations || 4;
            document.getElementById("longPressThreshold").value = cfg.longPressThreshold || 6;
            document.getElementById("repeatInterval").value = cfg.repeatInterval || 250;
          });
        }
        
        document.getElementById("configForm").addEventListener("submit", function(e) {
          e.preventDefault();
          const cfg = {
            serverIP: document.getElementById("serverIP").value,
            numberofStations: parseInt(document.getElementById("numberofStations").value),
            longPressThreshold: parseInt(document.getElementById("longPressThreshold").value),
            repeatInterval: parseInt(document.getElementById("repeatInterval").value)
          };
          fetch('/saveconfig', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(cfg)
          }).then(() => alert("Config saved!"));
        });

        refreshJson();
        refreshConfig();
        setInterval(refreshCodes, 1000);
        setInterval(refreshTimes, 1000);
      </script>
    </body>
    </html>
  )rawliteral";
#endif
