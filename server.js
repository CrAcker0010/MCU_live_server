const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
app.use(cors());
app.use(express.json());

const PORT = process.env.PORT || 3000;

// Last reported state from ESP device
let deviceStatus = {
    relay: false,
    mode: 'manual',
    remaining: 0,
    total: 0,
    ip: 'offline',
    rssi: 0
};

// Timestamp of the last poll from the ESP device
let lastPollTimestamp = 0;

// Buffer to store the next command to send to the ESP device
let pendingCommand = { command: 'none' };

// Serve static front-end files
app.use(express.static(path.join(__dirname, 'public')));

// Device Polling Endpoint - called by ESP device every second
app.post('/api/device/poll', (req, res) => {
    // Invert the relay state and mode logic to match inverted relay hardware
    let invertedMode = req.body.mode;
    if (req.body.mode === 'countdown_off') {
        invertedMode = 'countdown_on';
    } else if (req.body.mode === 'countdown_on') {
        invertedMode = 'countdown_off';
    } else if (req.body.mode === 'cycle_on') {
        invertedMode = 'cycle_off';
    } else if (req.body.mode === 'cycle_off') {
        invertedMode = 'cycle_on';
    }

    // Cache the status reported by the device (with inverted relay and mode states)
    deviceStatus = {
        relay: !req.body.relay,
        mode: invertedMode,
        remaining: req.body.remaining,
        total: req.body.total,
        ip: req.body.ip || req.ip,
        rssi: req.body.rssi
    };
    lastPollTimestamp = Date.now();
    
    // Respond with any pending command and clear it
    res.json(pendingCommand);
    pendingCommand = { command: 'none' };
});

// Browser API: Fetch current status
app.get('/api/status', (req, res) => {
    // If the device has not polled in the last 6 seconds, mark it as offline
    const isOffline = (Date.now() - lastPollTimestamp) > 6000;
    if (isOffline) {
        deviceStatus.ip = 'offline';
        deviceStatus.rssi = 0;
    }
    res.json(deviceStatus);
});

// Browser API: Manual toggle
app.get('/api/manual', (req, res) => {
    const { state } = req.query;
    // Invert the target state sent to the physical device
    const targetDeviceState = (state === 'on') ? 'off' : 'on';
    pendingCommand = { command: 'manual', state: targetDeviceState };
    res.json({ success: true, pending: pendingCommand });
});

// Browser API: Start OFF timer (turns ON immediately, counts down to OFF)
app.get('/api/timer/off', (req, res) => {
    const { duration } = req.query;
    // Inverted: Turn ON (physical OFF) and countdown to OFF (physical ON)
    pendingCommand = { command: 'timer_on', duration: parseInt(duration) || 0 };
    res.json({ success: true, pending: pendingCommand });
});

// Browser API: Start ON timer (turns OFF immediately, counts down to ON)
app.get('/api/timer/on', (req, res) => {
    const { duration } = req.query;
    // Inverted: Turn OFF (physical ON) and countdown to ON (physical OFF)
    pendingCommand = { command: 'timer_off', duration: parseInt(duration) || 0 };
    res.json({ success: true, pending: pendingCommand });
});

// Browser API: Start cycle loop
app.get('/api/timer/cycle', (req, res) => {
    const { on, off } = req.query;
    // Inverted: swap the ON and OFF phase durations for the physical device
    pendingCommand = { 
        command: 'timer_cycle', 
        on: parseInt(off) || 0, 
        off: parseInt(on) || 0 
    };
    res.json({ success: true, pending: pendingCommand });
});

// Browser API: Stop and reset
app.get('/api/stop', (req, res) => {
    pendingCommand = { command: 'stop' };
    res.json({ success: true, pending: pendingCommand });
});

// Wildcard routing to serve dashboard for all other requests
app.get('*', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

app.listen(PORT, () => {
    console.log(`Server running on port ${PORT}`);
});
