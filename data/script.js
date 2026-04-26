// Functions to load pages
function loadWiFiManager() {
  fetch('/wifi-manager')
    .then(response => response.text())
    .then(html => {
      document.body.innerHTML = html;
      updateSSIDList(); // Call the function after updating the HTML
    });
}

function loadProgramEditor() {
  fetch('/program-editor')
    .then(response => response.text())
    .then(html => document.body.innerHTML = html);
}

function loadInfluxDbManager() {
  fetch('/influxdb-manager')
    .then(response => response.text())
    .then(html => {
      document.body.innerHTML = html;
      prefillInfluxDbForm();
    });
}

var _otaPollInterval = null;

function loadFirmwareUpdate() {
  fetch('/getFirmwareStatus')
    .then(r => r.json())
    .then(data => {
      var cv = document.getElementById('currentVersion');
      if (cv) cv.textContent = data.currentVersion || '?';
      _applyFirmwareStatus(data);
    })
    .catch(() => {});
}

function checkForUpdate() {
  document.getElementById('statusMsg').textContent = 'Checking…';
  document.getElementById('checkBtn').disabled = true;
  document.getElementById('updateBtn').style.display = 'none';
  fetch('/checkFirmwareUpdate', { method: 'POST' })
    .then(r => r.json())
    .then(() => { _startOtaPoll(); })
    .catch(e => {
      document.getElementById('statusMsg').textContent = 'Error: ' + e;
      document.getElementById('checkBtn').disabled = false;
    });
}

function performOtaUpdate() {
  document.getElementById('statusMsg').textContent = 'Installing update… device will restart.';
  document.getElementById('updateBtn').disabled = true;
  fetch('/performOTA', { method: 'POST' })
    .then(r => r.json())
    .then(() => { _startOtaPoll(); })
    .catch(e => {
      document.getElementById('statusMsg').textContent = 'Error: ' + e;
    });
}

function _startOtaPoll() {
  if (_otaPollInterval) clearInterval(_otaPollInterval);
  _otaPollInterval = setInterval(function () {
    fetch('/getFirmwareStatus')
      .then(r => r.json())
      .then(data => { _applyFirmwareStatus(data); })
      .catch(() => {});
  }, 2000);
}

function _applyFirmwareStatus(data) {
  var statusEl = document.getElementById('statusMsg');
  var latestEl = document.getElementById('latestVersion');
  var checkBtn = document.getElementById('checkBtn');
  var updateBtn = document.getElementById('updateBtn');
  if (!statusEl) return;

  if (latestEl && data.latestVersion) latestEl.textContent = data.latestVersion;

  switch (data.status) {
    case 'checking':
      statusEl.textContent = 'Checking for updates…';
      break;
    case 'up_to_date':
      statusEl.textContent = '✓ Firmware is up to date.';
      if (checkBtn) checkBtn.disabled = false;
      if (updateBtn) updateBtn.style.display = 'none';
      if (_otaPollInterval) { clearInterval(_otaPollInterval); _otaPollInterval = null; }
      break;
    case 'update_available':
      statusEl.textContent = 'Update available: ' + (data.latestVersion || data.latestTag);
      if (checkBtn) checkBtn.disabled = false;
      if (updateBtn) { updateBtn.style.display = ''; updateBtn.disabled = false; }
      if (_otaPollInterval) { clearInterval(_otaPollInterval); _otaPollInterval = null; }
      break;
    case 'updating':
      statusEl.textContent = 'Installing… do not power off.';
      break;
    case 'error':
      statusEl.textContent = '✗ Error. Check that the device is connected to the internet.';
      if (checkBtn) checkBtn.disabled = false;
      if (_otaPollInterval) { clearInterval(_otaPollInterval); _otaPollInterval = null; }
      break;
    default:
      break;
  }
}

// Exit function
function exit() {
  fetch('/exit')
    .then(response => response.text())
    .then(html => document.body.innerHTML = html);
    // .then(result => console.log(result));
}

// Function to update SSID list values dynamically
function updateSSIDList() {
  fetch('/getSSIDList')
    .then(response => response.json())
    .then(data => {
      for (var key in data) {
        if (data.hasOwnProperty(key)) {
          var option = document.getElementById(key);
          if (option) {
            option.value = data[key];
            option.textContent = data[key];
          }
        }
      }
    })
}

// Call the function on page load — only when on the WiFi manager page
window.onload = function () {
  if (document.getElementById('SSID1')) {
    updateSSIDList();
    setInterval(updateSSIDList, 15000); // Update every 15 seconds
  }
};

// Send WiFi manager form and poll for connection result
function sendWifiManagerForm() {
  const form = document.getElementById('wifiManagerForm');
  const formData = new FormData(form);

  // Replace form with a live status message
  const statusEl = document.createElement('p');
  statusEl.id = 'wifiStatus';
  statusEl.textContent = 'Connecting to WiFi…';
  form.replaceWith(statusEl);

  fetch('/wifi-manager', { method: 'POST', body: formData }).then(() => {
    let tries = 0;
    const timer = setInterval(() => {
      tries++;
      fetch('/getWifiStatus')
        .then(r => r.json())
        .then(data => {
          if (data.connected) {
            clearInterval(timer);
            document.getElementById('wifiStatus').innerHTML =
              '✓ Connected! Device IP: <strong>' + data.ip + '</strong>.<br>' +
              'Reconnect to your home network and navigate to ' +
              '<a href="http://' + data.ip + '">http://' + data.ip + '</a>.';
          } else if (tries >= 15) {
            clearInterval(timer);
            document.getElementById('wifiStatus').innerHTML =
              '✗ Connection failed. <a href="/wifi-manager">Try again</a>.';
          }
        })
        .catch(() => {
          // The AP went away — device is likely connected and stopped broadcasting
          clearInterval(timer);
          document.getElementById('wifiStatus').textContent =
            'Device has connected — reconnect to your home network.';
        });
    }, 2000);
  });
}

// Pre-fill InfluxDB form with current saved credentials
function prefillInfluxDbForm() {
  fetch('/getInfluxCredentials')
    .then(response => response.json())
    .then(data => {
      if (data.url)    document.getElementById('influxUrl').value    = data.url;
      if (data.token)  document.getElementById('influxToken').value  = data.token;
      if (data.org)    document.getElementById('influxOrg').value    = data.org;
      if (data.bucket) document.getElementById('influxBucket').value = data.bucket;
      if (data.tzInfo) document.getElementById('influxTzInfo').value = data.tzInfo;
    })
    .catch(() => {}); // no credentials saved yet — leave fields empty
}

// Send InfluxDB manager form
function sendInfluxDbForm() {
  const form = document.getElementById('influxDbForm');
  const formData = new FormData(form);

  fetch('/influxdb-manager', {
    method: 'POST',
    body: formData
  })
    .then(response => response.text())
    .then(result => {
      console.log(result);
      window.location.href = '/index.html';
    })
}

// Send firing program form
function sendFiringProgram() {
  const form = document.getElementById('firingProgramForm');
  const formData = new FormData(form);

  fetch('/program-editor', {
    method: 'POST',
    body: formData
  })
    .then(response => response.text())
    .then(result => {
      console.log(result);
      // Reditrect to home page
      window.location.href = '/index.html'
    })
}

// Dynamically add segments
function addSegment(event) {
  event.preventDefault();
  const segmentsList = document.getElementById('segments');
  const segmentCount = segmentsList.children.length;

  const newSegmentNumber = segmentCount + 1;
  const newSegment = document.createElement('li');
  newSegment.innerHTML = `
      <div class="counter">
          <span>${newSegmentNumber}. </span>
      </div>
      <div>
          <label for="target${newSegmentNumber}">Target (°C):</label>
          <input type="number" name="target${newSegmentNumber}" id="target${newSegmentNumber}" oninput="this.value = this.value.replace(/[^0-9]/g, '');">
      </div>
      <div>
          <label for="speed${newSegmentNumber}">Speed (°C/hr):</label>
          <input type="number" name="speed${newSegmentNumber}" id="speed${newSegmentNumber}" oninput="this.value = this.value.replace(/[^0-9]/g, '');">
      </div>
      <div>
          <label for="hold${newSegmentNumber}">Hold (min):</label>
          <input type="number" name="hold${newSegmentNumber}" id="hold${newSegmentNumber}" oninput="this.value = this.value.replace(/[^0-9]/g, '');">
      </div>
      <button class="button-main" onclick="removeSegment(this)">Remove</button>
  `;
  segmentsList.appendChild(newSegment);
}
// or remove them
function removeSegment(button) {
  const segmentToRemove = button.parentNode;
  segmentToRemove.parentNode.removeChild(segmentToRemove);

  renumberSegments();
}
// and of course renumber them if necessary
function renumberSegments() {
  const segmentsList = document.getElementById('segments');
  Array.from(segmentsList.children).forEach((segment, index) => {
    const segmentNumber = index + 1;
    segment.querySelector('span').textContent = `${segmentNumber}. `;

    const targetInput = segment.querySelector('input[name^="target"]');
    const speedInput = segment.querySelector('input[name^="speed"]');
    const holdInput = segment.querySelector('input[name^="hold"]');

    if (targetInput) {
      const targetId = `target${segmentNumber}`;
      targetInput.name = targetId;
      targetInput.id = targetId;
      segment.querySelector('label[for^="target"]').setAttribute('for', targetId);
    }

    if (speedInput) {
      const speedId = `speed${segmentNumber}`;
      speedInput.name = speedId;
      speedInput.id = speedId;
      segment.querySelector('label[for^="speed"]').setAttribute('for', speedId);
    }

    if (holdInput) {
      const holdId = `hold${segmentNumber}`;
      holdInput.name = holdId;
      holdInput.id = holdId;
      segment.querySelector('label[for^="hold"]').setAttribute('for', holdId);
    }
  });
}


