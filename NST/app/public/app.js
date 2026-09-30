const form = document.querySelector('#runForm');
const durationValue = document.querySelector('#durationValue');
const durationUnit = document.querySelector('#durationUnit');
const startButton = document.querySelector('#startButton');
const stopButton = document.querySelector('#stopButton');
const shutdownButton = document.querySelector('#shutdownButton');
const loadReportButton = document.querySelector('#loadReportButton');
const message = document.querySelector('#message');
const statePill = document.querySelector('#statePill');
const progressText = document.querySelector('#progressText');
const progressBar = document.querySelector('#progressBar');
const progressTrack = document.querySelector('.progress-track');
const elapsedText = document.querySelector('#elapsedText');
const remainingText = document.querySelector('#remainingText');
const probeSection = document.querySelector('#probeSection');
const probeTable = document.querySelector('#probeTable');
const reportSection = document.querySelector('#reportSection');
const reportBox = document.querySelector('#reportBox');
const diagnosisFreshness = document.querySelector('#diagnosisFreshness');
const networkDiagnosis = document.querySelector('#networkDiagnosis');
const networkRatingMode = document.querySelector('#networkRatingMode');
const networkScore = document.querySelector('#networkScore');
const networkGrade = document.querySelector('#networkGrade');
const networkMeter = document.querySelector('#networkMeter');
const networkMeterBar = document.querySelector('#networkMeterBar');
const networkDiagnosisDetail = document.querySelector('#networkDiagnosisDetail');
const networkMeta = document.querySelector('#networkMeta');
const wifiDiagnosis = document.querySelector('#wifiDiagnosis');
const wifiName = document.querySelector('#wifiName');
const wifiSignal = document.querySelector('#wifiSignal');
const wifiGrade = document.querySelector('#wifiGrade');
const wifiMeter = document.querySelector('#wifiMeter');
const wifiMeterBar = document.querySelector('#wifiMeterBar');
const wifiDiagnosisDetail = document.querySelector('#wifiDiagnosisDetail');
const wifiMeta = document.querySelector('#wifiMeta');
const wifiPermissionRequest = document.querySelector('#wifiPermissionRequest');
const wifiPermissionTitle = document.querySelector('#wifiPermissionTitle');
const wifiPermissionText = document.querySelector('#wifiPermissionText');
const openLocationSettingsButton = document.querySelector('#openLocationSettingsButton');

let reportAvailable = false;
let lastPayload = null;
let pollingTimer = null;
let serverShuttingDown = false;
let requestInFlight = false;
let statusRequest = null;
let locationSettingsOpened = false;

function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return '0s';
  const rounded = Math.round(seconds);
  const hours = Math.floor(rounded / 3600);
  const minutes = Math.floor((rounded % 3600) / 60);
  const secs = rounded % 60;
  const parts = [];
  if (hours) parts.push(`${hours}h`);
  if (minutes) parts.push(`${minutes}m`);
  if (secs || parts.length === 0) parts.push(`${secs}s`);
  return parts.join(' ');
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function compactStatus(value) {
  const status = String(value ?? '');
  return status.length > 80 ? `${status.slice(0, 77)}...` : status;
}

function levelName(label) {
  return String(label || 'waiting').toLowerCase().replaceAll(' ', '-');
}

function updateDiagnosisMeter(meter, bar, value, valueText) {
  const percent = Number.isFinite(value) ? Math.max(0, Math.min(100, value)) : 0;
  bar.style.width = `${percent}%`;
  meter.setAttribute('aria-valuenow', String(Math.round(percent)));
  meter.setAttribute('aria-valuetext', valueText);
}

function renderDiagnosis(payload) {
  const rating = payload.networkRating || {};
  const wifi = payload.wifi || {};
  const score = Number(rating.score);
  const hasScore = rating.score !== null && rating.score !== undefined && Number.isFinite(score);

  networkScore.textContent = hasScore ? String(Math.round(score)) : '--';
  networkGrade.textContent = hasScore ? rating.label : 'Waiting';
  networkRatingMode.textContent = rating.isLive
    ? (rating.sampleCount > 0 ? `Live - ${rating.sampleCount} samples` : 'Warming up')
    : (hasScore ? 'Last test' : 'Start a test');
  networkDiagnosisDetail.textContent = rating.summary || 'Start a test to calculate network quality.';
  networkMeta.textContent = hasScore
    ? `Loss ${rating.failurePercent ?? '--'}% | Latency ${Number.isFinite(rating.averageLatencyMs) ? `${rating.averageLatencyMs} ms` : 'unavailable'} | ${rating.sampleCount || 0} probes`
    : 'Loss -- | Latency -- | 0 probes';
  networkDiagnosis.dataset.level = levelName(hasScore ? rating.label : 'waiting');
  updateDiagnosisMeter(networkMeter, networkMeterBar, hasScore ? score : null, hasScore ? `${rating.label}, ${Math.round(score)} out of 100` : 'Waiting for network samples');

  const signal = Number(wifi.signalPercent);
  const hasSignal = wifi.signalPercent !== null && wifi.signalPercent !== undefined && Number.isFinite(signal);
  wifiSignal.textContent = hasSignal ? String(Math.round(signal)) : '--';
  wifiGrade.textContent = hasSignal ? wifi.quality : (wifi.permissionBlocked ? 'Permission needed' : wifi.connected ? 'Unavailable' : 'Not connected');
  wifiName.textContent = wifi.ssid || wifi.adapterName || (wifi.available ? 'Wi-Fi adapter' : 'No Wi-Fi adapter');
  wifiDiagnosisDetail.textContent = wifi.diagnosis || 'Reading Windows wireless status.';
  wifiDiagnosisDetail.hidden = Boolean(wifi.permissionBlocked);
  wifiDiagnosis.dataset.level = levelName(hasSignal ? wifi.quality : 'waiting');
  updateDiagnosisMeter(wifiMeter, wifiMeterBar, hasSignal ? signal : null, hasSignal ? `${wifi.quality}, ${Math.round(signal)} percent` : wifiGrade.textContent);

  wifiPermissionRequest.hidden = !wifi.permissionBlocked;
  if (wifi.permissionBlocked) {
    wifiPermissionTitle.textContent = locationSettingsOpened ? 'Waiting for Windows permission' : 'Allow Wi-Fi signal access';
    wifiPermissionText.textContent = locationSettingsOpened
      ? 'Settings opened. Turn on Location services and desktop-app access; this panel will update automatically.'
      : 'Windows requires Location services and desktop-app access before it shares signal strength.';
  } else {
    locationSettingsOpened = false;
  }

  const wifiDetails = [];
  if (Number.isFinite(wifi.estimatedDbm)) wifiDetails.push(`Approx. ${wifi.estimatedDbm} dBm`);
  if (wifi.radioType) wifiDetails.push(wifi.radioType);
  if (wifi.channel) wifiDetails.push(`Channel ${wifi.channel}`);
  if (Number.isFinite(wifi.receiveRateMbps)) wifiDetails.push(`Rx ${wifi.receiveRateMbps} Mbps`);
  if (Number.isFinite(wifi.transmitRateMbps)) wifiDetails.push(`Tx ${wifi.transmitRateMbps} Mbps`);
  wifiMeta.textContent = wifiDetails.join(' | ');
  wifiMeta.hidden = wifiDetails.length === 0;

  const updatedAt = wifi.updatedAt || rating.updatedAt;
  diagnosisFreshness.textContent = updatedAt
    ? `Updated ${new Date(updatedAt).toLocaleTimeString()}`
    : 'Waiting for status';
}

function syncDurationConstraints() {
  const min = durationUnit.value === 'seconds' ? 5 : 1;
  const max = durationUnit.value === 'seconds' ? 86400 : durationUnit.value === 'hours' ? 24 : 1440;
  durationValue.min = String(min);
  durationValue.max = String(max);
  if (Number(durationValue.value) < min) {
    durationValue.value = String(min);
  }
  if (Number(durationValue.value) > max) {
    durationValue.value = String(max);
  }
}

function layerStats(progress, layer) {
  return (progress?.layers || []).find(item => item.layer === layer || item.name === layer);
}

function setState(label, running, issue = false, stopRequested = false) {
  statePill.textContent = label ? `${label.charAt(0).toUpperCase()}${label.slice(1)}` : 'Idle';
  statePill.classList.toggle('running', running);
  statePill.classList.toggle('issue', issue);
  startButton.disabled = running || requestInFlight || serverShuttingDown;
  stopButton.disabled = !running || stopRequested || requestInFlight || serverShuttingDown;
  shutdownButton.disabled = serverShuttingDown;
}

function setProgress(percent) {
  const safePercent = Number.isFinite(percent) ? Math.max(0, Math.min(100, percent)) : 0;
  progressText.textContent = `${safePercent.toFixed(safePercent % 1 === 0 ? 0 : 1)}%`;
  progressBar.style.width = `${safePercent}%`;
  progressTrack.setAttribute('aria-valuenow', String(Math.round(safePercent)));
}

function setReportAvailability(isAvailable) {
  reportAvailable = Boolean(isAvailable);
  reportSection.hidden = !reportAvailable;
  loadReportButton.disabled = requestInFlight || serverShuttingDown || !reportAvailable;
}

function setControlsBusy(isBusy) {
  requestInFlight = isBusy;
  const running = Boolean(lastPayload?.active?.isRunning);
  const stopRequested = Boolean(lastPayload?.active?.stopRequested);
  startButton.disabled = isBusy || running || serverShuttingDown;
  stopButton.disabled = isBusy || !running || stopRequested || serverShuttingDown;
  loadReportButton.disabled = isBusy || serverShuttingDown || !reportAvailable;
  shutdownButton.disabled = isBusy || serverShuttingDown;
}

function renderEmpty() {
  setState('Idle', false);
  setProgress(0);
  elapsedText.textContent = 'Elapsed 0s';
  remainingText.textContent = 'Remaining 0s';
  probeSection.hidden = true;
  probeTable.innerHTML = '<tr><td colspan="7" class="empty">No active data yet.</td></tr>';
}

function renderPendingRun(run) {
  const stopRequested = Boolean(run.stopRequested);
  setState(stopRequested ? 'Stopping' : 'Starting', true, false, stopRequested);
  setProgress(0);
  elapsedText.textContent = 'Elapsed 0s';
  remainingText.textContent = `Remaining ${formatDuration(run.durationSeconds)}`;
  probeSection.hidden = false;
  probeTable.innerHTML = '<tr><td colspan="7" class="empty">Starting the first probe batch.</td></tr>';
  setReportAvailability(false);
  message.textContent = stopRequested ? 'Stop requested. Finalizing the partial report.' : 'Starting the network probes...';
}

function renderStatus(payload) {
  lastPayload = payload;
  renderDiagnosis(payload);
  if (payload.shuttingDown) {
    serverShuttingDown = true;
    setState('Shutting down', false);
    startButton.disabled = true;
    stopButton.disabled = true;
    loadReportButton.disabled = true;
    shutdownButton.disabled = true;
    openLocationSettingsButton.disabled = true;
    message.textContent = 'Server is shutting down. You can close this browser tab.';
    return;
  }

  const run = payload.active || payload.last;
  if (!run) {
    renderEmpty();
    if (payload.latestReport) {
      setReportAvailability(true);
      reportBox.textContent = `Latest saved report: ${payload.latestReport.name}\nClick Load Report to view it.`;
    } else {
      setReportAvailability(false);
    }
    return;
  }

  if (!run.progress) {
    if (run.error) {
      renderEmpty();
      setState('Failed', false, true);
      message.textContent = `The test could not start: ${run.error}`;
    } else {
      renderPendingRun(run);
    }
    return;
  }

  const progress = run.progress;
  probeSection.hidden = false;
  const isRunning = Boolean(payload.active?.isRunning);
  const progressValue = Number(progress.progressPct);
  const percent = Number.isFinite(progressValue) ? Math.max(0, Math.min(100, progressValue)) : 0;
  const elapsed = Number(progress.elapsedSeconds || 0);
  const duration = Number(progress.durationSeconds || run.durationSeconds || 0);
  const remaining = Math.max(0, duration - elapsed);
  const router = layerStats(progress, 'router');
  const network = layerStats(progress, 'network');
  const hasFailures = Number(router?.failed || 0) + Number(network?.failed || 0) > 0;

  const stopRequested = Boolean(run.stopRequested);
  const stateLabel = isRunning
    ? (stopRequested ? 'Stopping' : 'Running')
    : (progress.state === 'running' ? 'Finishing' : progress.state || 'Complete');
  setState(stateLabel, isRunning, !isRunning && hasFailures, stopRequested);
  if (isRunning) {
    message.textContent = stopRequested ? 'Stop requested. Finalizing the partial report.' : 'Test running. Progress updates automatically.';
  } else if (progress.state === 'complete') {
    message.textContent = hasFailures
      ? 'Test complete. Review the failures below and load the report for details.'
      : router
        ? 'Test complete. Router and network checks finished cleanly.'
        : 'Test complete. Upstream checks finished cleanly; no default gateway was available for a direct router check.';
  } else if (!isRunning && progress.state === 'stopped') {
    message.textContent = 'Test stopped. A partial report is available.';
  } else if (run.error || Number(run.exitCode) !== 0) {
    message.textContent = run.error ? `Test failed: ${run.error}` : 'Test ended unexpectedly. Check the error log for details.';
  }
  setProgress(percent);
  elapsedText.textContent = `Elapsed ${formatDuration(elapsed)}`;
  remainingText.textContent = isRunning ? `Remaining ${formatDuration(remaining)}` : 'Remaining 0s';
  const reportFinished = !isRunning && ['complete', 'stopped'].includes(progress.state);
  setReportAvailability(isRunning
    ? false
    : (reportFinished
      ? Boolean(run.reportAvailable || payload.latestReport)
      : Boolean(payload.latestReport)));

  const probes = progress.probes || [];
  if (probes.length === 0) {
    probeTable.innerHTML = '<tr><td colspan="7" class="empty">Waiting for first probe batch.</td></tr>';
    return;
  }

  probeTable.innerHTML = probes.map(probe => {
    const failed = Number(probe.failed || 0);
    const failurePct = Number(probe.failurePct || 0);
    const failureClass = failed > 0 ? (failurePct > 1 ? 'bad' : 'warn') : '';
    return `
      <tr>
        <td>${escapeHtml(probe.name)}</td>
        <td class="layer-${escapeHtml(probe.layer)}">${escapeHtml(probe.layer)}</td>
        <td>${escapeHtml(probe.sent)}</td>
        <td class="${failureClass}">${escapeHtml(probe.failed)}</td>
        <td class="${failureClass}">${escapeHtml(probe.failurePct)}%</td>
        <td>${escapeHtml(probe.avgMs ?? '')}</td>
        <td title="${escapeHtml(probe.lastStatus)}">${escapeHtml(compactStatus(probe.lastStatus))}</td>
      </tr>
    `;
  }).join('');
}

async function fetchStatus() {
  if (statusRequest) return statusRequest;
  statusRequest = (async () => {
    const response = await fetch('/api/status');
    if (!response.ok) throw new Error('Could not load status.');
    const payload = await response.json();
    renderStatus(payload);
    return payload;
  })();
  try {
    return await statusRequest;
  } finally {
    statusRequest = null;
  }
}

async function readJsonResponse(response) {
  try {
    return await response.json();
  } catch {
    return {};
  }
}

async function startRun(event) {
  event.preventDefault();
  if (requestInFlight || serverShuttingDown) return;
  setControlsBusy(true);
  setReportAvailability(false);
  message.textContent = 'Starting test...';
  try {
    const response = await fetch('/api/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        value: durationValue.value,
        unit: durationUnit.value,
      }),
    });
    const payload = await readJsonResponse(response);
    if (!response.ok) {
      message.textContent = payload.error || 'Could not start test.';
      return;
    }
    reportBox.textContent = 'Run started. The final report will appear here when it finishes.';
    message.textContent = 'Test running. Progress updates automatically.';
    renderStatus({
      active: payload.run,
      wifi: lastPayload?.wifi,
      networkRating: {
        score: null,
        label: 'Waiting',
        isLive: true,
        sampleCount: 0,
        summary: 'Collecting the first network samples.',
      },
    });
  } catch {
    message.textContent = 'Could not reach the local server.';
  } finally {
    setControlsBusy(false);
  }
}

async function stopRun() {
  if (requestInFlight || serverShuttingDown) return;
  setControlsBusy(true);
  message.textContent = 'Stopping test...';
  try {
    const response = await fetch('/api/stop', { method: 'POST' });
    const payload = await readJsonResponse(response);
    if (!response.ok) {
      message.textContent = payload.error || 'Could not stop the test.';
      return;
    }
    message.textContent = payload.stopped ? 'Stop requested. The report will finalize shortly.' : 'No active test to stop.';
    await fetchStatus();
  } catch {
    message.textContent = 'Could not reach the local server.';
  } finally {
    setControlsBusy(false);
  }
}

async function shutdownApp() {
  if (requestInFlight || serverShuttingDown) return;
  const activeRun = lastPayload?.active?.isRunning;
  if (activeRun && !window.confirm('A test is running. Shut down the app and stop the active test?')) {
    return;
  }

  serverShuttingDown = true;
  setState('Shutting down', false);
  message.textContent = 'Shutting down server. You can close this tab after the request completes.';
  startButton.disabled = true;
  stopButton.disabled = true;
  loadReportButton.disabled = true;
  shutdownButton.disabled = true;
  openLocationSettingsButton.disabled = true;

  try {
    const response = await fetch('/api/shutdown', { method: 'POST' });
    const payload = await readJsonResponse(response);
    if (!response.ok) throw new Error(payload.error || 'Shutdown request failed.');
    message.textContent = payload.stoppedRun
      ? 'Server is shutting down and the active test was stopped.'
      : 'Server is shutting down. You can close this browser tab.';
  } catch {
    message.textContent = 'Server stopped. You can close this browser tab.';
  } finally {
    if (pollingTimer) clearInterval(pollingTimer);
  }
}

async function openLocationSettings() {
  if (serverShuttingDown || openLocationSettingsButton.disabled) return;
  openLocationSettingsButton.disabled = true;
  wifiPermissionText.textContent = 'Opening Windows Location settings...';
  try {
    const response = await fetch('/api/open-location-settings', { method: 'POST' });
    const payload = await readJsonResponse(response);
    if (!response.ok) {
      throw new Error(payload.error || 'Could not open Windows Location settings.');
    }
    locationSettingsOpened = true;
    wifiPermissionTitle.textContent = 'Waiting for Windows permission';
    wifiPermissionText.textContent = 'Settings opened. Turn on Location services and desktop-app access; this panel will update automatically.';
  } catch (error) {
    wifiPermissionText.textContent = error.message || 'Could not open Windows Location settings.';
  } finally {
    openLocationSettingsButton.disabled = false;
  }
}

async function loadReport() {
  if (!reportAvailable) {
    message.textContent = 'No report is available yet.';
    return;
  }
  if (requestInFlight || serverShuttingDown) return;
  setControlsBusy(true);
  try {
    const response = await fetch('/api/report');
    const body = await response.text();
    reportBox.textContent = response.ok ? body : 'Report is not ready yet.';
    message.textContent = response.ok ? 'Report loaded.' : 'The report is not ready yet.';
  } catch {
    message.textContent = 'Could not reach the local server.';
  } finally {
    setControlsBusy(false);
  }
}

form.addEventListener('submit', startRun);
durationUnit.addEventListener('change', syncDurationConstraints);
stopButton.addEventListener('click', stopRun);
shutdownButton.addEventListener('click', shutdownApp);
openLocationSettingsButton.addEventListener('click', openLocationSettings);
loadReportButton.addEventListener('click', () => loadReport().catch(error => { message.textContent = error.message; }));

pollingTimer = setInterval(() => {
  if (serverShuttingDown) return;
  fetchStatus()
    .then(payload => {
      const run = payload.active || payload.last;
      if (run?.progress?.state === 'complete' || run?.progress?.state === 'stopped') {
        if (reportAvailable && reportBox.textContent.includes('final report will appear')) {
          loadReport().catch(() => {});
        }
      }
    })
    .catch(error => {
      if (!serverShuttingDown) message.textContent = error.message;
    });
}, 1000);

syncDurationConstraints();
fetchStatus().catch(() => {
  renderEmpty();
  message.textContent = 'Could not load status from the local server.';
});
