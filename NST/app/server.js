const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile, spawn } = require('child_process');

const workspace = path.resolve(__dirname, '..');
const publicDir = path.join(__dirname, 'public');
const workDir = path.join(workspace, 'work');
const outputsDir = path.join(workspace, 'outputs');
const probeScript = path.join(workDir, 'router-network-stability-test.ps1');
const port = Number(process.env.PORT || 3877);
const host = process.env.HOST || '127.0.0.1';
const maxBodyBytes = 64 * 1024;

if (!Number.isInteger(port) || port < 1 || port > 65535) {
  console.error('PORT must be a number between 1 and 65535.');
  process.exit(1);
}

fs.mkdirSync(workDir, { recursive: true });
fs.mkdirSync(outputsDir, { recursive: true });

let activeRun = null;
let lastRun = null;
let shuttingDown = false;
let wifiCache = null;
let wifiCacheExpiresAt = 0;
let wifiRequest = null;

function json(res, status, body) {
  const data = Buffer.from(JSON.stringify(body, null, 2));
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': data.length,
    'Cache-Control': 'no-store',
  });
  res.end(data);
}

function text(res, status, body, contentType = 'text/plain; charset=utf-8') {
  const data = Buffer.from(body);
  res.writeHead(status, {
    'Content-Type': contentType,
    'Content-Length': data.length,
    'Cache-Control': 'no-store',
  });
  res.end(data);
}

function methodNotAllowed(res, allowed) {
  res.writeHead(405, {
    Allow: allowed,
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
  });
  res.end(JSON.stringify({ error: `Method not allowed. Use ${allowed}.` }, null, 2));
}

function readJsonFile(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8').replace(/^\uFEFF/, ''));
  } catch {
    return null;
  }
}

function ratingLabel(score) {
  if (!Number.isFinite(score)) return 'Waiting';
  if (score >= 90) return 'Excellent';
  if (score >= 75) return 'Good';
  if (score >= 55) return 'Fair';
  if (score >= 35) return 'Poor';
  return 'Critical';
}

function wifiQuality(signalPercent) {
  if (!Number.isFinite(signalPercent)) return 'Unavailable';
  if (signalPercent >= 80) return 'Excellent';
  if (signalPercent >= 60) return 'Good';
  if (signalPercent >= 40) return 'Fair';
  if (signalPercent >= 20) return 'Weak';
  return 'Very weak';
}

function activeWifiAdapterName() {
  const interfaces = os.networkInterfaces();
  return Object.keys(interfaces).find(name => {
    if (!/(wi-?fi|wireless|wlan)/i.test(name)) return false;
    return (interfaces[name] || []).some(address => !address.internal && address.family === 'IPv4');
  }) || null;
}

function netshField(output, field) {
  const match = output.match(new RegExp(`^\\s*${field}\\s*:\\s*(.+)$`, 'mi'));
  return match ? match[1].trim() : null;
}

function queryWifiStatus() {
  const adapterName = activeWifiAdapterName();
  return new Promise(resolve => {
    execFile('netsh.exe', ['wlan', 'show', 'interfaces'], {
      encoding: 'utf8',
      timeout: 4000,
      windowsHide: true,
    }, (error, stdout = '', stderr = '') => {
      const output = `${stdout}\n${stderr}`;
      const signalMatch = output.match(/^\s*Signal\s*:\s*(\d+)%/mi) || output.match(/:\s*(\d+)%\s*$/m);
      const signalPercent = signalMatch ? Math.max(0, Math.min(100, Number(signalMatch[1]))) : null;
      const state = netshField(output, 'State');
      const permissionBlocked = /error\s*5|access denied|requires elevation/i.test(output);
      const noWirelessAdapter = /no wireless interface/i.test(output);
      const connected = /connected/i.test(state || '') || Boolean(adapterName && !noWirelessAdapter);
      const available = !noWirelessAdapter && (Boolean(adapterName) || /interfaces? on the system/i.test(output) || signalPercent !== null);
      let diagnosis;
      if (permissionBlocked) {
        diagnosis = 'Windows blocked signal details. Turn on Location services to enable live signal strength.';
      } else if (!available) {
        diagnosis = 'No active Wi-Fi adapter was detected.';
      } else if (!connected) {
        diagnosis = 'Wi-Fi is available but not connected.';
      } else if (signalPercent === null) {
        diagnosis = 'Wi-Fi is connected, but Windows did not report signal strength.';
      } else if (signalPercent < 40) {
        diagnosis = 'Weak Wi-Fi can cause latency spikes and dropped packets.';
      } else if (signalPercent < 60) {
        diagnosis = 'Usable signal, but distance or interference may affect stability.';
      } else {
        diagnosis = 'Signal strength is suitable for a stable connection.';
      }

      const receiveRateText = netshField(output, 'Receive rate \\(Mbps\\)');
      const transmitRateText = netshField(output, 'Transmit rate \\(Mbps\\)');
      const receiveRate = receiveRateText === null ? NaN : Number(receiveRateText);
      const transmitRate = transmitRateText === null ? NaN : Number(transmitRateText);
      resolve({
        available,
        connected,
        permissionBlocked,
        adapterName,
        ssid: netshField(output, 'SSID'),
        signalPercent,
        estimatedDbm: signalPercent === null ? null : Math.round((signalPercent / 2) - 100),
        quality: wifiQuality(signalPercent),
        radioType: netshField(output, 'Radio type'),
        channel: netshField(output, 'Channel'),
        receiveRateMbps: Number.isFinite(receiveRate) ? receiveRate : null,
        transmitRateMbps: Number.isFinite(transmitRate) ? transmitRate : null,
        diagnosis,
        error: error && !permissionBlocked ? error.message : null,
        updatedAt: new Date().toISOString(),
      });
    });
  });
}

async function getWifiStatus() {
  if (wifiCache && Date.now() < wifiCacheExpiresAt) return wifiCache;
  if (wifiRequest) return wifiRequest;
  wifiRequest = queryWifiStatus();
  try {
    wifiCache = await wifiRequest;
    wifiCacheExpiresAt = Date.now() + 2000;
    return wifiCache;
  } finally {
    wifiRequest = null;
  }
}

function isLocalOrigin(req) {
  const origin = req.headers.origin;
  if (!origin) return true;
  try {
    const originUrl = new URL(origin);
    return ['127.0.0.1', 'localhost', '[::1]'].includes(originUrl.hostname)
      && Number(originUrl.port || 80) === port;
  } catch {
    return false;
  }
}

function openWindowsLocationSettings() {
  if (process.env.NST_DISABLE_SETTINGS_LAUNCH === '1') {
    return Promise.resolve({ opened: true, simulated: true });
  }
  return new Promise((resolve, reject) => {
    const child = spawn('explorer.exe', ['ms-settings:privacy-location'], {
      detached: true,
      stdio: 'ignore',
      windowsHide: true,
    });
    child.once('error', reject);
    child.once('spawn', () => {
      child.unref();
      resolve({ opened: true, simulated: false });
    });
  });
}

function latencyComponent(averageLatencyMs) {
  if (!Number.isFinite(averageLatencyMs)) return 70;
  if (averageLatencyMs <= 40) return 100;
  if (averageLatencyMs <= 80) return 85;
  if (averageLatencyMs <= 150) return 65;
  if (averageLatencyMs <= 300) return 40;
  return 15;
}

function calculateNetworkRating(run, wifi) {
  const progress = run && run.progress;
  const layers = progress && Array.isArray(progress.layers) ? progress.layers : [];
  const sampleCount = Number(progress && progress.probeCount) || 0;
  if (!progress || sampleCount === 0) {
    return {
      score: null,
      label: 'Waiting',
      isLive: Boolean(run && run.isRunning),
      sampleCount,
      summary: run && run.isRunning ? 'Collecting the first network samples.' : 'Start a test to calculate network quality.',
    };
  }

  const sent = layers.reduce((total, layer) => total + (Number(layer.sent) || 0), 0);
  const failed = layers.reduce((total, layer) => total + (Number(layer.failed) || 0), 0);
  const failurePercent = sent > 0 ? (failed / sent) * 100 : 0;
  const availabilityScore = Math.max(0, 100 - Math.min(100, failurePercent * 8));
  const networkLayer = layers.find(layer => layer.layer === 'network' || layer.name === 'network');
  const routerLayer = layers.find(layer => layer.layer === 'router' || layer.name === 'router');
  const averageLatencyValue = networkLayer && networkLayer.avgMs;
  const averageLatencyMs = averageLatencyValue === null || averageLatencyValue === undefined ? NaN : Number(averageLatencyValue);
  const latencyScore = latencyComponent(averageLatencyMs);
  const includeWifi = Boolean(run.isRunning && wifi && wifi.connected && Number.isFinite(wifi.signalPercent));
  const score = Math.round(includeWifi
    ? (availabilityScore * 0.55) + (latencyScore * 0.25) + (wifi.signalPercent * 0.20)
    : (availabilityScore * 0.70) + (latencyScore * 0.30));

  let summary = 'No packet loss or major latency issue is visible in the current samples.';
  if (routerLayer && Number(routerLayer.failed) > 0) {
    summary = 'Router-side failures point to Wi-Fi, Ethernet, or the local gateway path.';
  } else if (networkLayer && Number(networkLayer.failed) > 0) {
    summary = routerLayer
      ? 'The router is responding, but upstream internet checks are failing.'
      : 'Upstream checks are failing; the router path could not be isolated.';
  } else if (Number.isFinite(averageLatencyMs) && averageLatencyMs > 150) {
    summary = 'The connection is responding, but latency is high.';
  } else if (includeWifi && wifi.signalPercent < 40) {
    summary = 'Network checks are responding, but weak Wi-Fi may reduce stability.';
  }

  return {
    score,
    label: ratingLabel(score),
    isLive: Boolean(run.isRunning),
    sampleCount,
    failurePercent: Math.round(failurePercent * 10) / 10,
    averageLatencyMs: Number.isFinite(averageLatencyMs) ? Math.round(averageLatencyMs * 10) / 10 : null,
    components: {
      availability: Math.round(availabilityScore),
      latency: latencyScore,
      wifi: includeWifi ? wifi.signalPercent : null,
    },
    summary,
    updatedAt: progress.updatedAt || null,
  };
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    let bytes = 0;
    let settled = false;
    req.on('data', chunk => {
      if (settled) return;
      bytes += chunk.length;
      if (bytes > maxBodyBytes) {
        settled = true;
        const error = new Error('Request body is too large.');
        error.statusCode = 413;
        reject(error);
        return;
      }
      body += chunk;
    });
    req.on('end', () => {
      if (!settled) {
        settled = true;
        resolve(body);
      }
    });
    req.on('error', error => {
      if (!settled) {
        settled = true;
        reject(error);
      }
    });
  });
}

function isPathInside(parent, target) {
  const relative = path.relative(parent, target);
  return relative === '' || (relative && !relative.startsWith('..') && !path.isAbsolute(relative));
}

function durationLabel(seconds) {
  return seconds % 60 === 0 ? `${seconds / 60}min` : `${seconds}s`;
}

function stamp() {
  const now = new Date();
  const pad = value => String(value).padStart(2, '0');
  const milliseconds = String(now.getMilliseconds()).padStart(3, '0');
  return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}-${milliseconds}`;
}

function normalizeDuration(input) {
  if (!input || typeof input !== 'object') {
    throw new Error('Request body must be a JSON object.');
  }
  const value = Number(input.value);
  const unit = input.unit || 'minutes';
  if (!['seconds', 'minutes', 'hours'].includes(unit)) {
    throw new Error('Duration unit must be seconds, minutes, or hours.');
  }
  if (!Number.isFinite(value) || value <= 0) {
    throw new Error('Duration must be greater than zero.');
  }
  const multiplier = unit === 'seconds' ? 1 : unit === 'hours' ? 3600 : 60;
  const seconds = Math.round(value * multiplier);
  if (seconds < 5) {
    throw new Error('Minimum duration is 5 seconds.');
  }
  if (seconds > 24 * 60 * 60) {
    throw new Error('Maximum duration is 24 hours.');
  }
  return seconds;
}

function runSnapshot(run) {
  if (!run) return null;
  const progressFromDisk = readJsonFile(run.progressPath);
  if (progressFromDisk) run.lastProgress = progressFromDisk;
  const storedProgress = progressFromDisk || run.lastProgress || null;
  const progress = storedProgress
    ? Object.fromEntries(Object.entries(storedProgress).filter(([key]) => !['csv', 'summary', 'durationMinutes'].includes(key)))
    : null;
  const isRunning = run.child && run.child.exitCode === null && !run.exited;
  return {
    id: run.id,
    durationSeconds: run.durationSeconds,
    isRunning,
    reportAvailable: fs.existsSync(run.summaryPath),
    exitCode: run.exitCode,
    stopRequested: run.stopRequested,
    error: run.spawnError || null,
    progress,
  };
}

function startRun(durationSeconds) {
  if (!fs.existsSync(probeScript)) {
    throw new Error(`Probe script was not found at ${probeScript}`);
  }

  const label = durationLabel(durationSeconds);
  const id = stamp();
  const progressPath = path.join(workDir, `router-and-network-${label}-progress-${id}.json`);
  const summaryPath = path.join(outputsDir, `router-and-network-${label}-report-${id}.txt`);
  const stopFile = path.join(workDir, `router-and-network-${id}.stop`);
  const stdoutPath = path.join(workDir, `router-and-network-${label}-${id}-stdout.log`);
  const stderrPath = path.join(workDir, `router-and-network-${label}-${id}-stderr.log`);
  try { fs.rmSync(stopFile, { force: true }); } catch {}

  const out = fs.openSync(stdoutPath, 'a');
  const err = fs.openSync(stderrPath, 'a');
  const child = spawn('powershell.exe', [
    '-NoProfile',
    '-ExecutionPolicy',
    'Bypass',
    '-File',
    probeScript,
    '-DurationSeconds',
    String(durationSeconds),
    '-WorkspaceRoot',
    workspace,
    '-StopFile',
    stopFile,
    '-RunId',
    id,
  ], {
    windowsHide: true,
    stdio: ['ignore', out, err],
  });

  const run = {
    id,
    durationSeconds,
    child,
    progressPath,
    summaryPath,
    stopFile,
    exitCode: null,
    exited: false,
    stopRequested: false,
    forceStopTimer: null,
    lastProgress: null,
    spawnError: null,
  };

  const finishRun = (code, error = null) => {
    if (run.exited) return;
    run.exitCode = code;
    run.exited = true;
    run.spawnError = error ? error.message : run.spawnError;
    if (run.forceStopTimer) clearTimeout(run.forceStopTimer);
    try { fs.closeSync(out); } catch {}
    try { fs.closeSync(err); } catch {}
    if (activeRun && activeRun.id === run.id) {
      lastRun = run;
      activeRun = null;
    }
  };

  child.on('exit', code => {
    finishRun(code);
  });

  child.on('error', error => {
    finishRun(1, error);
  });

  activeRun = run;
  lastRun = run;
  return run;
}

function stopRun() {
  const run = activeRun;
  if (!run) return false;

  if (!run.stopRequested) {
    run.stopRequested = true;
    fs.writeFileSync(run.stopFile, new Date().toISOString());
  }
  if (!run.forceStopTimer) {
    run.forceStopTimer = setTimeout(() => {
      if (!run.exited && run.child && run.child.exitCode === null) {
        run.child.kill();
      }
    }, 5000);
    run.forceStopTimer.unref();
  }
  return true;
}

function closeServerProcess() {
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 1500).unref();
}

function waitForRunThenClose(run, deadline) {
  if (!run || run.exited) {
    closeServerProcess();
    return;
  }
  if (Date.now() >= deadline) {
    if (run.child && run.child.exitCode === null) {
      run.child.kill();
    }
    closeServerProcess();
    return;
  }
  setTimeout(() => waitForRunThenClose(run, deadline), 100).unref();
}

function shutdownServer() {
  if (shuttingDown) return false;
  shuttingDown = true;
  const run = activeRun;
  stopRun();

  setTimeout(() => waitForRunThenClose(run, Date.now() + 6500), 250).unref();

  return true;
}

function latestReport() {
  try {
    let newest = null;
    for (const name of fs.readdirSync(outputsDir)) {
      if (!/^router-and-network-.*-report-.*\.txt$/.test(name)) continue;
      const fullPath = path.join(outputsDir, name);
      const stat = fs.statSync(fullPath);
      if (!newest || stat.mtimeMs > newest.modifiedAt) {
        newest = { name, path: fullPath, updatedAt: stat.mtime.toISOString(), size: stat.size, modifiedAt: stat.mtimeMs };
      }
    }
    if (!newest) return null;
    const { modifiedAt, ...report } = newest;
    return report;
  } catch {
    return null;
  }
}

function serveStatic(req, res) {
  if (!['GET', 'HEAD'].includes(req.method)) {
    methodNotAllowed(res, 'GET, HEAD');
    return;
  }
  const requested = new URL(req.url, 'http://127.0.0.1').pathname;
  const relative = requested === '/' ? 'index.html' : requested.replace(/^\/+/, '');
  const filePath = path.resolve(publicDir, relative);
  if (!isPathInside(publicDir, filePath)) {
    text(res, 403, 'Forbidden');
    return;
  }
  const ext = path.extname(filePath).toLowerCase();
  const types = {
    '.html': 'text/html; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
  };
  fs.readFile(filePath, (error, data) => {
    if (error) {
      text(res, 404, 'Not found');
      return;
    }
    res.writeHead(200, {
      'Content-Type': types[ext] || 'application/octet-stream',
      'Content-Length': data.length,
      'Cache-Control': 'no-store',
      'Content-Security-Policy': "default-src 'self'; connect-src 'self'; style-src 'self'; script-src 'self'",
      'Referrer-Policy': 'no-referrer',
      'X-Content-Type-Options': 'nosniff',
    });
    res.end(req.method === 'HEAD' ? undefined : data);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  try {
    if (url.pathname === '/api/status') {
      if (req.method !== 'GET') {
        methodNotAllowed(res, 'GET');
        return;
      }
      const active = runSnapshot(activeRun);
      const last = runSnapshot(lastRun);
      const wifi = await getWifiStatus();
      const newestReport = latestReport();
      json(res, 200, {
        active,
        last,
        latestReport: newestReport ? {
          name: newestReport.name,
          updatedAt: newestReport.updatedAt,
          size: newestReport.size,
        } : null,
        wifi,
        networkRating: calculateNetworkRating(active || last, wifi),
        shuttingDown,
      });
      return;
    }

    if (url.pathname === '/api/start') {
      if (req.method !== 'POST') {
        methodNotAllowed(res, 'POST');
        return;
      }
      if (shuttingDown) {
        json(res, 503, { error: 'Server is shutting down.' });
        return;
      }
      if (activeRun && activeRun.child && activeRun.child.exitCode === null) {
        json(res, 409, { error: 'A test is already running.' });
        return;
      }
      let body;
      try {
        body = JSON.parse(await readBody(req) || '{}');
      } catch (error) {
        const status = error.statusCode === 413 ? 413 : 400;
        json(res, status, { error: status === 413 ? error.message : 'Invalid JSON request body.' });
        return;
      }
      let seconds;
      try {
        seconds = normalizeDuration(body);
      } catch (error) {
        json(res, 400, { error: error.message });
        return;
      }
      const run = startRun(seconds);
      json(res, 201, { run: runSnapshot(run) });
      return;
    }

    if (url.pathname === '/api/open-location-settings') {
      if (req.method !== 'POST') {
        methodNotAllowed(res, 'POST');
        return;
      }
      if (!isLocalOrigin(req)) {
        json(res, 403, { error: 'This action is only available from the local app.' });
        return;
      }
      const result = await openWindowsLocationSettings();
      wifiCacheExpiresAt = 0;
      json(res, 200, result);
      return;
    }

    if (url.pathname === '/api/stop') {
      if (req.method !== 'POST') {
        methodNotAllowed(res, 'POST');
        return;
      }
      const stopped = stopRun();
      json(res, 200, { stopped });
      return;
    }

    if (url.pathname === '/api/shutdown') {
      if (req.method !== 'POST') {
        methodNotAllowed(res, 'POST');
        return;
      }
      const stoppedRun = Boolean(activeRun && activeRun.child && activeRun.child.exitCode === null);
      const accepted = shutdownServer();
      json(res, 200, { shuttingDown: true, accepted, stoppedRun });
      return;
    }

    if (url.pathname === '/api/report') {
      if (req.method !== 'GET') {
        methodNotAllowed(res, 'GET');
        return;
      }
      const newestReport = latestReport();
      const candidates = [lastRun && lastRun.summaryPath, newestReport && newestReport.path].filter(Boolean);
      const reportPath = candidates.find(candidate => isPathInside(outputsDir, candidate) && fs.existsSync(candidate));
      if (!reportPath) {
        text(res, 404, 'Report not found');
        return;
      }
      text(res, 200, fs.readFileSync(reportPath, 'utf8'));
      return;
    }

    if (url.pathname.startsWith('/api/')) {
      json(res, 404, { error: 'API endpoint not found.' });
      return;
    }

    serveStatic(req, res);
  } catch (error) {
    json(res, 500, { error: error.message });
  }
});

server.listen(port, host, () => {
  console.log(`Network stability app running at http://${host}:${port}`);
});

server.on('error', error => {
  if (error.code === 'EADDRINUSE') {
    console.error(`Port ${port} is already in use. Set PORT to another value and launch again.`);
  } else {
    console.error(error.message);
  }
  process.exit(1);
});

process.on('SIGINT', shutdownServer);
process.on('SIGTERM', shutdownServer);
