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

// Exit function
function exit() {
  fetch('/exit')
    .then(response => response.text())
    .then(result => console.log(result));
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

// Call the function on page load
window.onload = function () {
  updateSSIDList();
  setInterval(updateSSIDList, 15000); // Update every 15 seconds
};

// Send WiFi manager form
function sendWifiManagerForm() {
  const form = document.getElementById('wifiManagerForm');
  const formData = new FormData(form);

  fetch('/wifi-manager', {
    method: 'POST',
    body: formData
  })
    .then(response => response.text())
    .then(result => {
      console.log(result);
      // Redirect to the home page (index.html)
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


