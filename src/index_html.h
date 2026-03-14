const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Roller Blind Control</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 20px;}
    button { padding: 10px 20px; margin: 5px; font-size: 1em; }
    #pos { font-size: 1.2em; margin-top: 10px; }
  </style>
</head>
<body>
  <h2>Roller Blind</h2>
  <div id="pos">Position: --%</div>

  <button onclick="sendCmd('top')">Top</button>
  <button onclick="sendCmd('bottom')">Bottom</button>
  </div>
  
  <div style="margin-top:10px;">
  <button onclick="sendCmd('stepUp')">Up</button>
  <button onclick="sendCmd('stop')">Stop</button>
  <button onclick="sendCmd('stepDown')">Down</button>
  </div>
  
  <h3>Calibration</h3>
  <button onclick="sendCmd('setTop')">Set Top</button>
  <button onclick="sendCmd('setBott')">Set Bottom</button>

  <h3>Motor Position</h3>
  <button onclick="sendCmd('motorLeft')">Motor on Left</button>
  <button onclick="sendCmd('motorRight')">Motor on Right</button>
  
  <script>
    let ws = new WebSocket('ws://' + location.host + '/ws');

    ws.onmessage = (event) => {
      try {
        let data = JSON.parse(event.data);
        document.getElementById('pos').textContent =
          'Position: ' + (data.percent ?? '--') + '%';
      } catch (e) {
        console.error('Invalid:', event.data);
      }
    };

    function sendCmd(cmd) {
      fetch('/' + cmd).catch(console.error);
    }

  </script>
</body>
</html>
)rawliteral";
