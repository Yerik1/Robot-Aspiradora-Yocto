/**
 * app.js - Main Client Dashboard Controller
 * Connects to JSON-RPC over WebSocket, manages encrypted auth,
 * controls the robot, and renders real-time telemetry and 2D route map.
 */

// Initialize JSON-RPC WebSocket
const rpc = new JsonRpcWebSocket();

// Application State
const state = {
    authenticated: false,
    token: null,
    username: null,
    mode: 'manual',
    vacuum: false,
    speed: 60,
    radius: 50,
    currentDirection: 'stop',
    showGrid: true,
    gridWidth: 25,
    gridHeight: 25,
    grid: new Array(25 * 25).fill(0),
    robot: { x: 12, y: 12, heading: 0 },
    logs: []
};

// DOM Elements
const E = (id) => document.getElementById(id);

// Canvas Route Map Setup
const canvas = E('map-canvas');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
    const container = canvas.parentElement;
    const size = Math.min(container.clientWidth - 32, 500);
    canvas.width = size;
    canvas.height = size;
    renderMap();
}

window.addEventListener('resize', resizeCanvas);

// Render Route Map on HTML5 Canvas
function renderMap() {
    if (!ctx) return;
    const w = canvas.width;
    const h = canvas.height;
    const cellW = w / state.gridWidth;
    const cellH = h / state.gridHeight;

    // Background
    ctx.fillStyle = '#111827';
    ctx.fillRect(0, 0, w, h);

    // Draw Grid Cells
    for (let y = 0; y < state.gridHeight; y++) {
        for (let x = 0; x < state.gridWidth; x++) {
            const idx = y * state.gridWidth + x;
            const val = state.grid[idx];

            if (val === 2) {
                // OBSTACLE: Painted vibrant RED
                ctx.fillStyle = '#ef4444';
                ctx.fillRect(x * cellW, y * cellH, cellW, cellH);
            } else if (val === 1) {
                // VISITED: Cleaned trail (Cyan / Emerald)
                ctx.fillStyle = '#065f46';
                ctx.fillRect(x * cellW, y * cellH, cellW, cellH);
            } else {
                // UNKNOWN / UNEXPLORED
                ctx.fillStyle = '#1e293b';
                ctx.fillRect(x * cellW, y * cellH, cellW, cellH);
            }

            if (state.showGrid) {
                ctx.strokeStyle = '#334155';
                ctx.lineWidth = 0.5;
                ctx.strokeRect(x * cellW, y * cellH, cellW, cellH);
            }
        }
    }

    // Draw Robot Cursor
    const rx = state.robot.x * cellW + cellW / 2;
    const ry = state.robot.y * cellH + cellH / 2;
    const rRadius = Math.max(cellW * 0.8, 8);

    ctx.save();
    ctx.translate(rx, ry);
    ctx.rotate((state.robot.heading * Math.PI) / 180);

    // Robot Body
    ctx.beginPath();
    ctx.arc(0, 0, rRadius, 0, Math.PI * 2);
    ctx.fillStyle = '#0ea5e9';
    ctx.fill();
    ctx.lineWidth = 2;
    ctx.strokeStyle = '#ffffff';
    ctx.stroke();

    // Direction Wedge / Arrow
    ctx.beginPath();
    ctx.moveTo(rRadius * 0.9, 0);
    ctx.lineTo(-rRadius * 0.5, -rRadius * 0.6);
    ctx.lineTo(-rRadius * 0.2, 0);
    ctx.lineTo(-rRadius * 0.5, rRadius * 0.6);
    ctx.closePath();
    ctx.fillStyle = '#ffffff';
    ctx.fill();

    ctx.restore();
}

// Log message to virtual terminal
function appendLog(category, text, type = 'info') {
    const logBox = E('event-log');
    if (!logBox) return;

    const time = new Date().toLocaleTimeString();
    const entry = document.createElement('div');
    entry.className = `log-entry log-${type}`;
    entry.innerHTML = `<span class="log-time">[${time}]</span> <span class="log-cat">[${category}]</span> ${text}`;
    
    logBox.appendChild(entry);
    logBox.scrollTop = logBox.scrollHeight;

    // Keep log max 150 items
    while (logBox.children.length > 150) {
        logBox.removeChild(logBox.firstChild);
    }
}

// UI Event Bindings
function setupUI() {
    resizeCanvas();

    // Reconnection & Status Handlers
    rpc.onStatus((status) => {
        const badge = E('conn-status');
        if (status === 'connected') {
            badge.className = 'status-badge online';
            badge.textContent = 'ONLINE (WS)';
            appendLog('WS', 'Connected to robot server', 'success');
            // Fetch initial map
            rpc.call('map.get').then(handleFullMap).catch(console.error);
        } else if (status === 'connecting') {
            badge.className = 'status-badge connecting';
            badge.textContent = 'CONNECTING...';
        } else {
            badge.className = 'status-badge offline';
            badge.textContent = 'OFFLINE';
            appendLog('WS', 'Connection lost', 'error');
        }
    });

    // Handle Telemetry Push Notifications (5 Hz)
    rpc.on('robot.telemetry', (telemetry) => {
        updateTelemetryUI(telemetry);
    });

    // Handle Incremental Map Push Notifications
    rpc.on('robot.map_update', (data) => {
        if (data.robot) {
            state.robot = data.robot;
        }
        if (data.updates && Array.isArray(data.updates)) {
            data.updates.forEach(u => {
                const idx = u.y * state.gridWidth + u.x;
                state.grid[idx] = u.val;
            });
        }
        renderMap();
    });

    // Auth Tabs
    E('tab-login').onclick = () => {
        E('tab-login').classList.add('active');
        E('tab-register').classList.remove('active');
        E('form-login').style.display = 'block';
        E('form-register').style.display = 'none';
        E('auth-error').textContent = '';
        E('auth-success').textContent = '';
    };

    E('tab-register').onclick = () => {
        E('tab-register').classList.add('active');
        E('tab-login').classList.remove('active');
        E('form-register').style.display = 'block';
        E('form-login').style.display = 'none';
        E('auth-error').textContent = '';
        E('auth-success').textContent = '';
    };

    // Login Form Submit (Encrypted Transit)
    E('btn-do-login').onclick = async () => {
        const user = E('login-username').value.trim();
        const pass = E('login-password').value.trim();
        const errEl = E('auth-error');
        const succEl = E('auth-success');
        errEl.textContent = '';
        succEl.textContent = '';

        if (!user || !pass) {
            errEl.textContent = 'Please enter both username and password';
            return;
        }

        try {
            E('btn-do-login').disabled = true;
            E('btn-do-login').textContent = 'Encrypting & Verifying...';

            // Encrypt credentials client-side with AES-256-CBC
            const { encrypted, iv } = await encryptCredentials(user, pass);
            appendLog('AUTH', `Encrypted credentials with fresh IV: [${iv.substring(0, 10)}...]`, 'info');

            const res = await rpc.call('auth.login', { encrypted, iv });
            state.authenticated = true;
            state.token = res.token;
            state.username = res.username;

            E('user-display').textContent = `User: ${res.username}`;
            E('auth-modal').style.display = 'none';
            E('dashboard-view').style.display = 'grid';

            appendLog('AUTH', `Authenticated as '${res.username}'. Token: ${res.token.substring(0, 8)}...`, 'success');
        } catch (err) {
            errEl.textContent = err.message || 'Login failed';
            appendLog('AUTH', `Login error: ${err.message}`, 'error');
        } finally {
            E('btn-do-login').disabled = false;
            E('btn-do-login').textContent = 'Iniciar Sesión';
        }
    };

    // Register Form Submit (Encrypted Transit)
    E('btn-do-register').onclick = async () => {
        const user = E('reg-username').value.trim();
        const pass = E('reg-password').value.trim();
        const confirm = E('reg-confirm').value.trim();
        const errEl = E('auth-error');
        const succEl = E('auth-success');
        errEl.textContent = '';
        succEl.textContent = '';

        if (!user || !pass) {
            errEl.textContent = 'Please fill out all fields';
            return;
        }
        if (pass !== confirm) {
            errEl.textContent = 'Passwords do not match';
            return;
        }

        try {
            E('btn-do-register').disabled = true;
            E('btn-do-register').textContent = 'Encrypting & Registering...';

            const { encrypted, iv } = await encryptCredentials(user, pass);
            appendLog('AUTH', `Sending encrypted registration payload`, 'info');

            const res = await rpc.call('auth.register', { encrypted, iv });
            succEl.textContent = res.message || 'Account registered! You can now log in.';
            appendLog('AUTH', `Registered user '${user}' successfully`, 'success');

            // Switch to login tab
            setTimeout(() => {
                E('tab-login').click();
                E('login-username').value = user;
                E('login-password').value = '';
            }, 1200);
        } catch (err) {
            errEl.textContent = err.message || 'Registration failed';
            appendLog('AUTH', `Registration error: ${err.message}`, 'error');
        } finally {
            E('btn-do-register').disabled = false;
            E('btn-do-register').textContent = 'Registrarse';
        }
    };

    // Logout
    E('btn-logout').onclick = async () => {
        if (state.token) {
            await rpc.call('auth.logout', { token: state.token }).catch(console.error);
        }
        state.authenticated = false;
        state.token = null;
        state.username = null;
        E('auth-modal').style.display = 'flex';
        E('dashboard-view').style.display = 'none';
        appendLog('AUTH', 'User logged out', 'info');
    };

    // Mode Switcher (Autonomous / Manual)
    E('btn-mode-auto').onclick = () => setRobotMode('autonomous');
    E('btn-mode-manual').onclick = () => setRobotMode('manual');

    // Suction Motor (Vacuum) Toggle
    E('switch-vacuum').onchange = (e) => {
        const on = e.target.checked;
        rpc.call('robot.vacuum', { enabled: on }).then(() => {
            appendLog('VACUUM', `Suction motor ${on ? 'ON' : 'OFF'}`, 'info');
        }).catch(err => appendLog('VACUUM', err.message, 'error'));
    };

    // Speed & Radius Sliders
    E('slider-speed').oninput = (e) => {
        state.speed = parseInt(e.target.value);
        E('val-speed').textContent = `${state.speed}%`;
        if (state.currentDirection !== 'stop') {
            sendMove(state.currentDirection);
        }
    };

    E('slider-radius').oninput = (e) => {
        state.radius = parseInt(e.target.value);
        E('val-radius').textContent = `${state.radius}%`;
        if (state.currentDirection !== 'stop') {
            sendMove(state.currentDirection);
        }
    };

    // D-Pad Directional Controls
    E('dpad-up').onclick = () => sendMove('forward');
    E('dpad-down').onclick = () => sendMove('backward');
    E('dpad-left').onclick = () => sendMove('left');
    E('dpad-right').onclick = () => sendMove('right');
    E('dpad-stop').onclick = () => sendMove('stop');

    // Keyboard Shortcuts
    window.addEventListener('keydown', (e) => {
        if (!state.authenticated || state.mode === 'autonomous') return;
        if (['INPUT', 'TEXTAREA'].includes(document.activeElement.tagName)) return;

        switch (e.key) {
            case 'ArrowUp':
            case 'w':
            case 'W':
                sendMove('forward');
                break;
            case 'ArrowDown':
            case 's':
            case 'S':
                sendMove('backward');
                break;
            case 'ArrowLeft':
            case 'a':
            case 'A':
                sendMove('left');
                break;
            case 'ArrowRight':
            case 'd':
            case 'D':
                sendMove('right');
                break;
            case ' ':
                sendMove('stop');
                break;
        }
    });

    // Audio Playback Controls
    E('btn-audio-play').onclick = () => {
        const track = E('audio-playlist').value;
        rpc.call('audio.play', { file: track }).then(() => {
            appendLog('AUDIO', `Play: ${track}`, 'info');
        }).catch(err => appendLog('AUDIO', err.message, 'error'));
    };

    E('btn-audio-pause').onclick = () => {
        rpc.call('audio.pause').then(() => appendLog('AUDIO', 'Paused', 'info')).catch(console.error);
    };

    E('btn-audio-resume').onclick = () => {
        rpc.call('audio.resume').then(() => appendLog('AUDIO', 'Resumed', 'info')).catch(console.error);
    };

    E('btn-audio-stop').onclick = () => {
        rpc.call('audio.stop').then(() => appendLog('AUDIO', 'Stopped', 'info')).catch(console.error);
    };

    E('slider-volume').oninput = (e) => {
        const vol = parseInt(e.target.value);
        E('val-volume').textContent = `${vol}%`;
        rpc.call('audio.volume', { volume: vol }).catch(console.error);
    };

    // Notification Sound Triggers (Task 3.3)
    E('notif-start').onclick = () => triggerNotif(0, 'System Startup');
    E('notif-auto').onclick = () => triggerNotif(1, 'Autonomous Mode Start');
    E('notif-obs').onclick = () => triggerNotif(2, 'Obstacle Alert');
    E('notif-manual').onclick = () => triggerNotif(3, 'Manual Mode Active');

    // Map Controls
    E('btn-reset-map').onclick = () => {
        rpc.call('map.reset').then(() => {
            state.grid.fill(0);
            renderMap();
            appendLog('MAP', 'Route map reset', 'info');
        }).catch(console.error);
    };

    E('btn-toggle-grid').onclick = () => {
        state.showGrid = !state.showGrid;
        renderMap();
    };

    // Clear Terminal Log
    E('btn-clear-log').onclick = () => {
        E('event-log').innerHTML = '';
    };

    // Load available songs into playlist
    rpc.call('audio.list_files').then(files => {
        const sel = E('audio-playlist');
        sel.innerHTML = '';
        files.forEach(f => {
            const opt = document.createElement('option');
            opt.value = f;
            opt.textContent = f;
            sel.appendChild(opt);
        });
    }).catch(console.error);
}

function setRobotMode(mode) {
    rpc.call('robot.set_mode', { mode }).then(() => {
        state.mode = mode;
        updateModeUI();
        appendLog('MODE', `Mode changed to ${mode.toUpperCase()}`, 'info');
    }).catch(err => appendLog('MODE', err.message, 'error'));
}

function updateModeUI() {
    const isAuto = (state.mode === 'autonomous');
    const badge = E('mode-badge');
    badge.textContent = isAuto ? 'MODO AUTÓNOMO' : 'MODO MANUAL';
    badge.className = `mode-badge ${isAuto ? 'mode-auto' : 'mode-manual'}`;

    E('btn-mode-auto').classList.toggle('active', isAuto);
    E('btn-mode-manual').classList.toggle('active', !isAuto);

    // Lock/Unlock manual controls
    const manualCard = E('manual-controls-card');
    manualCard.classList.toggle('disabled-overlay', isAuto);
}

function sendMove(direction) {
    if (state.mode === 'autonomous') return;
    state.currentDirection = direction;

    rpc.call('robot.move', {
        direction: direction,
        speed: state.speed,
        radius: state.radius
    }).then(() => {
        appendLog('TRACTION', `Move ${direction.toUpperCase()} @ ${state.speed}%`, 'info');
    }).catch(err => appendLog('TRACTION', err.message, 'error'));
}

function triggerNotif(eventId, name) {
    rpc.call('audio.notification', { event: eventId }).then(() => {
        appendLog('AUDIO NOTIF', `Sound played: ${name}`, 'success');
    }).catch(console.error);
}

function handleFullMap(data) {
    state.gridWidth = data.width;
    state.gridHeight = data.height;
    state.robot = data.robot;
    state.grid = data.grid;
    renderMap();
}

function updateTelemetryUI(t) {
    // Mode
    state.mode = t.mode;
    updateModeUI();

    // Suction motor
    state.vacuum = t.vacuum;
    E('switch-vacuum').checked = t.vacuum;

    // Motors & Speed
    E('telemetry-left-speed').textContent = `${t.motors.left}%`;
    E('telemetry-right-speed').textContent = `${t.motors.right}%`;
    E('telemetry-direction').textContent = t.direction.toUpperCase();

    // Sensors
    updateSensorGauge('front', t.sensors.front_cm, t.sensors.obs_front);
    updateSensorGauge('left', t.sensors.left_cm, t.sensors.obs_left);
    updateSensorGauge('right', t.sensors.right_cm, t.sensors.obs_right);

    // Obstacle Alert Banner
    const anyObs = t.sensors.obs_front || t.sensors.obs_left || t.sensors.obs_right;
    E('obstacle-alert-banner').style.display = anyObs ? 'flex' : 'none';

    // LEDs
    updateLED('led-auto', t.leds.autonomous);
    updateLED('led-manual', t.leds.manual);
    updateLED('led-obs', t.leds.obstacle_alert);
    updateLED('led-power', t.leds.system_on);

    // Audio status
    E('audio-track-display').textContent = `${t.audio.track} (${t.audio.state.toUpperCase()})`;

    // Robot pose
    state.robot = t.robot;
    renderMap();
}

function updateSensorGauge(name, cm, isObs) {
    const valEl = E(`sensor-${name}-val`);
    const barEl = E(`sensor-${name}-bar`);

    valEl.textContent = `${cm.toFixed(1)} cm`;
    const percent = Math.min(100, Math.max(0, (cm / 90) * 100));
    barEl.style.width = `${percent}%`;

    if (cm < 18) {
        barEl.className = 'sensor-fill danger';
    } else if (cm < 35) {
        barEl.className = 'sensor-fill warning';
    } else {
        barEl.className = 'sensor-fill safe';
    }
}

function updateLED(id, active) {
    const el = E(id);
    if (el) {
        el.classList.toggle('active', active);
    }
}

// Start Application
window.addEventListener('DOMContentLoaded', () => {
    setupUI();
    rpc.connect();
});
