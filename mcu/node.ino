#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
ESP8266WebServer server(80);
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
WebServer server(80);
#else
#error "This code is designed for ESP8266 or ESP32."
#endif

#include <WiFiClientSecure.h>

// ==========================================
// CONFIGURATION
// ==========================================
const char* ssid = "ESP32";         // Replace with your WiFi SSID
const char* password = "PASSWORD"; // Replace with your WiFi Password

// Set this to your hosted Render URL domain (e.g. "my-sprinkler.onrender.com")
// Set to "" (empty string) to run in local-only mode without cloud integration.
const char* cloudHost = "mcu-live-server.onrender.com"; 

#define RELAY_PIN 5  // Change this to your relay pin (e.g., D1 is usually GPIO5 on ESP8266)

// By default, many relay modules are Active LOW (turn ON when signal is LOW).
// If yours is Active HIGH, leave this defined. If Active LOW, comment it out.
#define RELAY_ACTIVE_HIGH 

#ifdef RELAY_ACTIVE_HIGH
  #define RELAY_ON HIGH
  #define RELAY_OFF LOW
#else
  #define RELAY_ON LOW
  #define RELAY_OFF HIGH
#endif

// ==========================================
// GLOBALS & STATE MACHINE
// ==========================================
enum State {
    STATE_MANUAL,
    STATE_COUNTDOWN_OFF, // Relay is ON, counting down to OFF
    STATE_COUNTDOWN_ON,  // Relay is OFF, counting down to ON
    STATE_CYCLE_ON,      // Cycle mode, currently ON
    STATE_CYCLE_OFF      // Cycle mode, currently OFF
};
State currentState = STATE_MANUAL;

unsigned long timerStartTime = 0;
unsigned long timerDurationMs = 0;

unsigned long cycleOnDurationMs = 0;
unsigned long cycleOffDurationMs = 0;

// ==========================================
// HTML PAGE (Dashboard for Local Server)
// ==========================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Relay Smart Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-gradient: linear-gradient(135deg, #0b0f19 0%, #111827 100%);
            --card-bg: rgba(17, 24, 39, 0.7);
            --card-border: rgba(255, 255, 255, 0.08);
            --text-primary: #f3f4f6;
            --text-secondary: #9ca3af;
            --color-on: #10b981;
            --color-on-glow: rgba(16, 185, 129, 0.4);
            --color-off: #ef4444;
            --color-off-glow: rgba(239, 68, 68, 0.3);
            --accent-cyan: #06b6d4;
            --accent-purple: #8b5cf6;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Outfit', sans-serif;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            background: var(--bg-gradient);
            color: var(--text-primary);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 20px;
            overflow-x: hidden;
        }

        .container {
            width: 100%;
            max-width: 1000px;
            margin: 0 auto;
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 20px 10px;
            margin-bottom: 20px;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
        }

        h1 {
            font-size: 28px;
            font-weight: 800;
            background: linear-gradient(to right, #38bdf8, #818cf8);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.5px;
        }

        .connection-status {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 14px;
            font-weight: 600;
            padding: 6px 12px;
            background: rgba(255, 255, 255, 0.05);
            border-radius: 20px;
            border: 1px solid var(--card-border);
        }

        .dot {
            width: 10px;
            height: 10px;
            border-radius: 50%;
            background-color: var(--color-off);
            box-shadow: 0 0 8px var(--color-off-glow);
        }

        .dot.connected {
            background-color: var(--color-on);
            box-shadow: 0 0 8px var(--color-on-glow);
            animation: pulse-green 2s infinite;
        }

        @keyframes pulse-green {
            0% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
            70% { box-shadow: 0 0 0 10px rgba(16, 185, 129, 0); }
            100% { box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }

        .dashboard-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }

        @media (max-width: 768px) {
            .dashboard-grid {
                grid-template-columns: 1fr;
            }
        }

        .card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 24px;
            padding: 24px;
            backdrop-filter: blur(20px);
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
            transition: all 0.3s ease;
        }

        .card:hover {
            border-color: rgba(255, 255, 255, 0.15);
        }

        .card-title {
            font-size: 18px;
            font-weight: 600;
            margin-bottom: 20px;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .status-container {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            height: 100%;
            min-height: 250px;
        }

        .power-ring {
            width: 140px;
            height: 140px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            background: rgba(255, 255, 255, 0.02);
            border: 3px solid rgba(255, 255, 255, 0.05);
            cursor: pointer;
            position: relative;
            transition: all 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            margin-bottom: 20px;
        }

        .power-ring::after {
            content: '';
            position: absolute;
            width: 100%;
            height: 100%;
            border-radius: 50%;
            border: 3px solid transparent;
            top: -3px;
            left: -3px;
            transition: all 0.4s ease;
        }

        .power-ring.on {
            border-color: var(--color-on);
            box-shadow: 0 0 30px var(--color-on-glow);
            background: rgba(16, 185, 129, 0.05);
        }

        .power-ring.on svg {
            fill: var(--color-on);
            filter: drop-shadow(0 0 8px var(--color-on-glow));
        }

        .power-ring.off {
            border-color: var(--color-off);
            box-shadow: 0 0 20px var(--color-off-glow);
            background: rgba(239, 68, 68, 0.02);
        }

        .power-ring.off svg {
            fill: var(--color-off);
            filter: drop-shadow(0 0 6px var(--color-off-glow));
        }

        .power-ring svg {
            width: 50px;
            height: 50px;
            fill: var(--text-secondary);
            transition: all 0.3s ease;
        }

        .power-ring:active {
            transform: scale(0.95);
        }

        .status-badge {
            font-size: 20px;
            font-weight: 800;
            padding: 8px 24px;
            border-radius: 30px;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-top: 10px;
        }

        .status-badge.on {
            background: rgba(16, 185, 129, 0.15);
            color: var(--color-on);
            border: 1px solid rgba(16, 185, 129, 0.3);
        }

        .status-badge.off {
            background: rgba(239, 68, 68, 0.15);
            color: var(--color-off);
            border: 1px solid rgba(239, 68, 68, 0.3);
        }

        /* Tabs System */
        .tabs {
            display: flex;
            background: rgba(0, 0, 0, 0.2);
            border-radius: 12px;
            padding: 4px;
            margin-bottom: 20px;
            border: 1px solid rgba(255, 255, 255, 0.03);
        }

        .tab-btn {
            flex: 1;
            padding: 10px;
            background: transparent;
            border: none;
            color: var(--text-secondary);
            font-size: 14px;
            font-weight: 600;
            border-radius: 8px;
            cursor: pointer;
            transition: all 0.3s ease;
        }

        .tab-btn.active {
            background: rgba(255, 255, 255, 0.08);
            color: var(--text-primary);
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
        }

        .tab-content {
            display: none;
        }

        .tab-content.active {
            display: block;
            animation: fadeIn 0.4s ease;
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(5px); }
            to { opacity: 1; transform: translateY(0); }
        }

        .timer-setup {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }

        .input-group {
            display: flex;
            flex-direction: column;
            gap: 8px;
        }

        .input-label {
            font-size: 14px;
            font-weight: 600;
            color: var(--text-secondary);
        }

        .number-spinners {
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .time-box {
            display: flex;
            align-items: center;
            background: rgba(0, 0, 0, 0.2);
            border: 1px solid var(--card-border);
            border-radius: 12px;
            padding: 5px 10px;
            flex: 1;
        }

        .time-box input {
            width: 100%;
            background: transparent;
            border: none;
            color: var(--text-primary);
            font-size: 24px;
            font-weight: 600;
            text-align: center;
            outline: none;
        }

        .time-box span {
            color: var(--text-secondary);
            font-size: 12px;
            font-weight: 600;
            margin-right: 5px;
        }

        /* Remove spin buttons */
        input::-webkit-outer-spin-button,
        input::-webkit-inner-spin-button {
            -webkit-appearance: none;
            margin: 0;
        }
        input[type=number] {
            -moz-appearance: textfield;
        }

        .preset-buttons {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin-top: 5px;
        }

        .preset-btn {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid var(--card-border);
            color: var(--text-primary);
            padding: 6px 12px;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
        }

        .preset-btn:hover {
            background: rgba(255, 255, 255, 0.1);
            border-color: rgba(255, 255, 255, 0.2);
        }

        .btn {
            width: 100%;
            padding: 14px;
            border-radius: 12px;
            border: none;
            font-size: 16px;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
            margin-top: 10px;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }

        .btn-primary {
            background: linear-gradient(135deg, var(--accent-cyan), #3b82f6);
            color: white;
            box-shadow: 0 4px 15px rgba(6, 182, 212, 0.3);
        }

        .btn-primary:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(6, 182, 212, 0.4);
        }

        .btn-danger {
            background: linear-gradient(135deg, #ef4444, #b91c1c);
            color: white;
            box-shadow: 0 4px 15px rgba(239, 68, 68, 0.25);
        }

        .btn-danger:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(239, 68, 68, 0.35);
        }

        .btn:active {
            transform: translateY(0);
        }

        /* Timer Active / Status View */
        .active-timer-view {
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            padding: 10px 0;
            text-align: center;
        }

        .circular-progress {
            position: relative;
            width: 180px;
            height: 180px;
            border-radius: 50%;
            background: conic-gradient(var(--accent-cyan) 0%, rgba(255, 255, 255, 0.05) 0%);
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 20px;
            box-shadow: inset 0 0 20px rgba(0,0,0,0.5);
            transition: background 0.3s ease;
        }

        .circular-progress::after {
            content: '';
            position: absolute;
            width: 160px;
            height: 160px;
            border-radius: 50%;
            background: #111827;
        }

        .progress-content {
            position: relative;
            z-index: 10;
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .timer-mode-badge {
            font-size: 11px;
            font-weight: 700;
            letter-spacing: 1px;
            text-transform: uppercase;
            color: var(--accent-cyan);
            background: rgba(6, 182, 212, 0.1);
            padding: 3px 8px;
            border-radius: 12px;
            margin-bottom: 5px;
        }

        .countdown-time {
            font-size: 32px;
            font-weight: 800;
            letter-spacing: -0.5px;
            color: var(--text-primary);
        }

        .timer-info-text {
            font-size: 14px;
            color: var(--text-secondary);
            margin-bottom: 20px;
            max-width: 250px;
        }

        /* Info Card */
        .info-card {
            grid-column: span 2;
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px 24px;
            font-size: 14px;
            color: var(--text-secondary);
        }

        @media (max-width: 768px) {
            .info-card {
                grid-column: span 1;
                flex-direction: column;
                gap: 10px;
                text-align: center;
            }
        }

        .info-item {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .info-item span {
            font-weight: 600;
            color: var(--text-primary);
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Sprinkler Relay Pro</h1>
            <div class="connection-status">
                <div class="dot connected" id="networkDot"></div>
                <span id="networkText">Connected</span>
            </div>
        </header>

        <div class="dashboard-grid">
            <!-- Relay Status Card -->
            <div class="card">
                <div class="card-title">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect><line x1="9" y1="3" x2="9" y2="21"></line></svg>
                    Manual Relay Control
                </div>
                <div class="status-container">
                    <div class="power-ring off" id="powerBtn">
                        <svg viewBox="0 0 24 24">
                            <path d="M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4M11,6H13V12H11V6Z" />
                        </svg>
                    </div>
                    <div class="status-badge off" id="statusBadge">Relay: OFF</div>
                </div>
            </div>

            <!-- Timer Configuration Card -->
            <div class="card">
                <div class="card-title">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 16 14"></polyline></svg>
                    Timer & Automation
                </div>
                
                <!-- Controls Setup / Config Mode -->
                <div id="timerConfigMode">
                    <div class="tabs">
                        <button class="tab-btn active" onclick="switchTab('off-timer')">Relay OFF Timer</button>
                        <button class="tab-btn" onclick="switchTab('on-timer')">Relay ON Timer</button>
                        <button class="tab-btn" onclick="switchTab('cycle-timer')">Cycle Loop</button>
                    </div>

                    <!-- Tab: Relay OFF Timer -->
                    <div id="tab-off-timer" class="tab-content active">
                        <div class="timer-setup">
                            <div class="input-group">
                                <div class="input-label">Set Countdown to Turn Relay OFF (Relay turns ON immediately)</div>
                                <div class="number-spinners">
                                    <div class="time-box">
                                        <span>MIN</span>
                                        <input type="number" id="offTimerMin" min="0" max="60" value="0">
                                    </div>
                                    <div class="time-box">
                                        <span>SEC</span>
                                        <input type="number" id="offTimerSec" min="0" max="59" value="30">
                                    </div>
                                </div>
                            </div>
                            <div class="preset-buttons">
                                <button class="preset-btn" onclick="addTime('offTimer', 1)">+1m</button>
                                <button class="preset-btn" onclick="addTime('offTimer', 5)">+5m</button>
                                <button class="preset-btn" onclick="addTime('offTimer', 10)">+10m</button>
                                <button class="preset-btn" onclick="clearTime('offTimer')">Clear</button>
                            </div>
                            <button class="btn btn-primary" onclick="startCountdownTimer('off')">
                                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                                Start OFF Timer
                            </button>
                        </div>
                    </div>

                    <!-- Tab: Relay ON Timer -->
                    <div id="tab-on-timer" class="tab-content">
                        <div class="timer-setup">
                            <div class="input-group">
                                <div class="input-label">Set Countdown to Turn Relay ON (Relay turns OFF immediately)</div>
                                <div class="number-spinners">
                                    <div class="time-box">
                                        <span>MIN</span>
                                        <input type="number" id="onTimerMin" min="0" max="60" value="0">
                                    </div>
                                    <div class="time-box">
                                        <span>SEC</span>
                                        <input type="number" id="onTimerSec" min="0" max="59" value="30">
                                    </div>
                                </div>
                            </div>
                            <div class="preset-buttons">
                                <button class="preset-btn" onclick="addTime('onTimer', 1)">+1m</button>
                                <button class="preset-btn" onclick="addTime('onTimer', 5)">+5m</button>
                                <button class="preset-btn" onclick="addTime('onTimer', 10)">+10m</button>
                                <button class="preset-btn" onclick="clearTime('onTimer')">Clear</button>
                            </div>
                            <button class="btn btn-primary" onclick="startCountdownTimer('on')">
                                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                                Start ON Timer
                            </button>
                        </div>
                    </div>

                    <!-- Tab: Cycle Timer -->
                    <div id="tab-cycle-timer" class="tab-content">
                        <div class="timer-setup">
                            <div class="input-group">
                                <div class="input-label">ON Duration</div>
                                <div class="number-spinners">
                                    <div class="time-box">
                                        <span>MIN</span>
                                        <input type="number" id="cycleOnMin" min="0" max="60" value="0">
                                    </div>
                                    <div class="time-box">
                                        <span>SEC</span>
                                        <input type="number" id="cycleOnSec" min="0" max="59" value="10">
                                    </div>
                                </div>
                            </div>
                            <div class="input-group">
                                <div class="input-label">OFF Duration</div>
                                <div class="number-spinners">
                                    <div class="time-box">
                                        <span>MIN</span>
                                        <input type="number" id="cycleOffMin" min="0" max="60" value="0">
                                    </div>
                                    <div class="time-box">
                                        <span>SEC</span>
                                        <input type="number" id="cycleOffSec" min="0" max="59" value="10">
                                    </div>
                                </div>
                            </div>
                            <button class="btn btn-primary" onclick="startCycleTimer()">
                                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                                Start Cycle Loop
                            </button>
                        </div>
                    </div>
                </div>

                <!-- Timer Running / Active State -->
                <div id="timerActiveMode" style="display: none;">
                    <div class="active-timer-view">
                        <div class="circular-progress" id="progressCircle">
                            <div class="progress-content">
                                <div class="timer-mode-badge" id="activeModeBadge">ON Countdown</div>
                                <div class="countdown-time" id="activeCountdownTime">00:00</div>
                            </div>
                        </div>
                        <div class="timer-info-text" id="activeInfoText">Relay is active and will turn off automatically.</div>
                        <button class="btn btn-danger" onclick="cancelTimer()">
                            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"></rect></svg>
                            Cancel Timer
                        </button>
                    </div>
                </div>
            </div>

            <!-- Metadata Info Card -->
            <div class="card info-card">
                <div class="info-item">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect><rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect><line x1="6" y1="6" x2="6.01" y2="6"></line><line x1="6" y1="18" x2="6.01" y2="18"></line></svg>
                    Device IP: <span id="deviceIp">...</span>
                </div>
                <div class="info-item">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0"></path><path d="M1.42 9a16 16 0 0 1 21.16 0"></path><path d="M8.53 16.11a6 6 0 0 1 6.95 0"></path><line x1="12" y1="20" x2="12.01" y2="20"></line></svg>
                    Signal: <span id="wifiSignal">Connecting...</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        // Set to false for actual ESP8266/ESP32 web server deployment
        const SIMULATION_MODE = false;

        // Local simulation state / actual status
        let simState = {
            relay: false,
            mode: 'manual', // manual, countdown_off, countdown_on, cycle_on, cycle_off
            remaining: 0,
            total: 0,
            ip: '...',
            rssi: 0
        };

        // UI elements
        const powerBtn = document.getElementById('powerBtn');
        const statusBadge = document.getElementById('statusBadge');
        const timerConfigMode = document.getElementById('timerConfigMode');
        const timerActiveMode = document.getElementById('timerActiveMode');
        const progressCircle = document.getElementById('progressCircle');
        const activeModeBadge = document.getElementById('activeModeBadge');
        const activeCountdownTime = document.getElementById('activeCountdownTime');
        const activeInfoText = document.getElementById('activeInfoText');
        const networkDot = document.getElementById('networkDot');
        const networkText = document.getElementById('networkText');

        // Initialize UI
        function init() {
            powerBtn.addEventListener('click', toggleRelay);
            updateUI();
            
            // Poll status
            setInterval(fetchStatus, 1000);
            fetchStatus(); // First fetch
        }

        // Tab Switching
        function switchTab(tabId) {
            document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
            
            // Set current tab active
            if (event) {
                event.currentTarget.classList.add('active');
            }
            document.getElementById('tab-' + tabId).classList.add('active');
        }

        // Helper time functions
        function addTime(prefix, amount) {
            const minInput = document.getElementById(prefix + 'Min');
            let currentVal = parseInt(minInput.value) || 0;
            minInput.value = currentVal + amount;
        }

        // Toggle manual relay state
        function toggleRelay() {
            const targetState = simState.relay ? 'off' : 'on';
            fetch(`/api/manual?state=${targetState}`)
                .then(res => res.json())
                .then(data => {
                    simState = data;
                    updateUI();
                })
                .catch(err => console.error("Error toggling relay:", err));
        }

        // Start Countdown Timer
        function startCountdownTimer(type) {
            let min, sec;
            if (type === 'off') {
                min = parseInt(document.getElementById('offTimerMin').value) || 0;
                sec = parseInt(document.getElementById('offTimerSec').value) || 0;
            } else {
                min = parseInt(document.getElementById('onTimerMin').value) || 0;
                sec = parseInt(document.getElementById('onTimerSec').value) || 0;
            }

            const totalSec = (min * 60) + sec;
            if (totalSec <= 0) {
                alert("Please specify a duration greater than 0.");
                return;
            }

            fetch(`/api/timer/${type}?duration=${totalSec}`)
                .then(res => res.json())
                .then(data => {
                    simState = data;
                    updateUI();
                })
                .catch(err => console.error("Error starting timer:", err));
        }

        // Start Cycle Timer
        function startCycleTimer() {
            const onMin = parseInt(document.getElementById('cycleOnMin').value) || 0;
            const onSec = parseInt(document.getElementById('cycleOnSec').value) || 0;
            const offMin = parseInt(document.getElementById('cycleOffMin').value) || 0;
            const offSec = parseInt(document.getElementById('cycleOffSec').value) || 0;

            const onDuration = (onMin * 60) + onSec;
            const offDuration = (offMin * 60) + offSec;

            if (onDuration <= 0 || offDuration <= 0) {
                alert("Please specify positive durations for both ON and OFF phases.");
                return;
            }

            fetch(`/api/timer/cycle?on=${onDuration}&off=${offDuration}`)
                .then(res => res.json())
                .then(data => {
                    simState = data;
                    updateUI();
                })
                .catch(err => console.error("Error starting cycle:", err));
        }

        // Cancel Active Timer
        function cancelTimer() {
            fetch('/api/stop')
                .then(res => res.json())
                .then(data => {
                    simState = data;
                    updateUI();
                })
                .catch(err => console.error("Error stopping timer:", err));
        }

        // Fetch Status from server
        function fetchStatus() {
            fetch('/api/status')
                .then(res => res.json())
                .then(data => {
                    simState = data;
                    networkDot.classList.add('connected');
                    networkText.innerText = "Connected";
                    updateUI();
                })
                .catch(err => {
                    console.error("Network error:", err);
                    networkDot.classList.remove('connected');
                    networkText.innerText = "Disconnected";
                });
        }

        // Update User Interface
        function updateUI() {
            // Update manual button state
            if (simState.relay) {
                powerBtn.className = "power-ring on";
                statusBadge.className = "status-badge on";
                statusBadge.innerText = "Relay: ON";
            } else {
                powerBtn.className = "power-ring off";
                statusBadge.className = "status-badge off";
                statusBadge.innerText = "Relay: OFF";
            }

            // Update Device Info
            document.getElementById('deviceIp').innerText = simState.ip || '...';
            
            let signalText = "Unknown";
            if (simState.rssi === 0) {
                signalText = "AP Mode";
            } else if (simState.rssi >= -50) {
                signalText = "Excellent (" + simState.rssi + " dBm)";
            } else if (simState.rssi >= -70) {
                signalText = "Good (" + simState.rssi + " dBm)";
            } else if (simState.rssi >= -85) {
                signalText = "Fair (" + simState.rssi + " dBm)";
            } else {
                signalText = "Weak (" + simState.rssi + " dBm)";
            }
            document.getElementById('wifiSignal').innerText = signalText;

            // Update timer display modes
            if (simState.mode === 'manual') {
                timerConfigMode.style.display = "block";
                timerActiveMode.style.display = "none";
            } else {
                timerConfigMode.style.display = "none";
                timerActiveMode.style.display = "block";

                // Format countdown text
                const m = Math.floor(simState.remaining / 60);
                const s = simState.remaining % 60;
                activeCountdownTime.innerText = `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;

                // Mode descriptions & colors
                let modeText = "";
                let infoText = "";
                let colorVal = "";

                if (simState.mode === 'countdown_off') {
                    modeText = "Relay OFF Countdown";
                    infoText = "Relay is currently ON and will automatically turn OFF when countdown finishes.";
                    colorVal = "var(--color-on)";
                    activeModeBadge.style.color = "var(--color-on)";
                    activeModeBadge.style.backgroundColor = "rgba(16, 185, 129, 0.1)";
                } else if (simState.mode === 'countdown_on') {
                    modeText = "Relay ON Countdown";
                    infoText = "Relay is currently OFF and will automatically turn ON when countdown finishes.";
                    colorVal = "var(--color-off)";
                    activeModeBadge.style.color = "var(--color-off)";
                    activeModeBadge.style.backgroundColor = "rgba(239, 68, 68, 0.1)";
                } else if (simState.mode === 'cycle_on') {
                    modeText = "Cycle Loop: ON Phase";
                    infoText = "Auto-cycle running. Relay will switch OFF after the countdown.";
                    colorVal = "var(--accent-cyan)";
                    activeModeBadge.style.color = "var(--accent-cyan)";
                    activeModeBadge.style.backgroundColor = "rgba(6, 182, 212, 0.1)";
                } else if (simState.mode === 'cycle_off') {
                    modeText = "Cycle Loop: OFF Phase";
                    infoText = "Auto-cycle running. Relay will switch ON after the countdown.";
                    colorVal = "var(--accent-purple)";
                    activeModeBadge.style.color = "var(--accent-purple)";
                    activeModeBadge.style.backgroundColor = "rgba(139, 92, 246, 0.1)";
                }

                activeModeBadge.innerText = modeText;
                activeInfoText.innerText = infoText;

                // Update circular progress bar using conic gradient
                const percentage = simState.total > 0 ? (simState.remaining / simState.total) * 100 : 0;
                progressCircle.style.background = `conic-gradient(${colorVal} ${percentage}%, rgba(255, 255, 255, 0.05) ${percentage}%)`;
            }
        }

        window.onload = init;
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// API JSON STATUS SENDER
// ==========================================
void sendStatusJson() {
    bool relayOn = (digitalRead(RELAY_PIN) == RELAY_ON);
    String modeStr = "manual";
    unsigned long remaining = 0;
    unsigned long total = 0;
    unsigned long now = millis();
    
    if (currentState == STATE_COUNTDOWN_OFF) {
        modeStr = "countdown_off";
        unsigned long elapsed = now - timerStartTime;
        remaining = (timerDurationMs > elapsed) ? (timerDurationMs - elapsed) / 1000 : 0;
        total = timerDurationMs / 1000;
    } else if (currentState == STATE_COUNTDOWN_ON) {
        modeStr = "countdown_on";
        unsigned long elapsed = now - timerStartTime;
        remaining = (timerDurationMs > elapsed) ? (timerDurationMs - elapsed) / 1000 : 0;
        total = timerDurationMs / 1000;
    } else if (currentState == STATE_CYCLE_ON) {
        modeStr = "cycle_on";
        unsigned long elapsed = now - timerStartTime;
        remaining = (cycleOnDurationMs > elapsed) ? (cycleOnDurationMs - elapsed) / 1000 : 0;
        total = cycleOnDurationMs / 1000;
    } else if (currentState == STATE_CYCLE_OFF) {
        modeStr = "cycle_off";
        unsigned long elapsed = now - timerStartTime;
        remaining = (cycleOffDurationMs > elapsed) ? (cycleOffDurationMs - elapsed) / 1000 : 0;
        total = cycleOffDurationMs / 1000;
    }
    
    String ipStr = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    
    if (WiFi.status() != WL_CONNECTED) {
        rssi = 0;
        ipStr = WiFi.softAPIP().toString();
    }
    
    String json = "{";
    json += "\"relay\":" + String(relayOn ? "true" : "false") + ",";
    json += "\"mode\":\"" + modeStr + "\",";
    json += "\"remaining\":" + String(remaining) + ",";
    json += "\"total\":" + String(total) + ",";
    json += "\"ip\":\"" + ipStr + "\",";
    json += "\"rssi\":" + String(rssi);
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ==========================================
// WEB SERVER HANDLERS
// ==========================================
void handleRoot() {
    server.send(200, "text/html", htmlPage);
}

void handleStatus() {
    sendStatusJson();
}

void handleManual() {
    if (server.hasArg("state")) {
        String stateArg = server.arg("state");
        currentState = STATE_MANUAL; // Reset active timers on manual change
        if (stateArg == "on") {
            digitalWrite(RELAY_PIN, RELAY_ON);
            Serial.println("Manual override: Relay turned ON");
        } else if (stateArg == "off") {
            digitalWrite(RELAY_PIN, RELAY_OFF);
            Serial.println("Manual override: Relay turned OFF");
        }
    }
    sendStatusJson();
}

void handleTimerOff() {
    if (server.hasArg("duration")) {
        long durationSec = server.arg("duration").toInt();
        if (durationSec > 0) {
            currentState = STATE_COUNTDOWN_OFF;
            timerDurationMs = (unsigned long)durationSec * 1000UL;
            timerStartTime = millis();
            digitalWrite(RELAY_PIN, RELAY_ON); // Turn ON immediately
            Serial.print("Countdown OFF started: ");
            Serial.print(durationSec);
            Serial.println(" seconds.");
        }
    }
    sendStatusJson();
}

void handleTimerOn() {
    if (server.hasArg("duration")) {
        long durationSec = server.arg("duration").toInt();
        if (durationSec > 0) {
            currentState = STATE_COUNTDOWN_ON;
            timerDurationMs = (unsigned long)durationSec * 1000UL;
            timerStartTime = millis();
            digitalWrite(RELAY_PIN, RELAY_OFF); // Turn OFF immediately
            Serial.print("Countdown ON started: ");
            Serial.print(durationSec);
            Serial.println(" seconds.");
        }
    }
    sendStatusJson();
}

void handleTimerCycle() {
    if (server.hasArg("on") && server.hasArg("off")) {
        long onSec = server.arg("on").toInt();
        long offSec = server.arg("off").toInt();
        if (onSec > 0 && offSec > 0) {
            currentState = STATE_CYCLE_ON;
            cycleOnDurationMs = (unsigned long)onSec * 1000UL;
            cycleOffDurationMs = (unsigned long)offSec * 1000UL;
            timerStartTime = millis();
            digitalWrite(RELAY_PIN, RELAY_ON); // Start with ON phase
            Serial.print("Cycle Loop started: ON = ");
            Serial.print(onSec);
            Serial.print("s, OFF = ");
            Serial.print(offSec);
            Serial.println("s.");
        }
    }
    sendStatusJson();
}

void handleStop() {
    currentState = STATE_MANUAL;
    digitalWrite(RELAY_PIN, RELAY_OFF); // Turn OFF relay on stop
    Serial.println("Timer Stopped. Relay turned OFF.");
    sendStatusJson();
}

// ==========================================
// PARSE CLOUD SERVER RESPONSES
// ==========================================
void handleServerResponse(String json) {
    if (json.indexOf("\"command\":\"none\"") >= 0 || json.length() == 0) {
        return; // No pending command
    }
    
    Serial.print("Executing Cloud Command: ");
    
    if (json.indexOf("\"command\":\"manual\"") >= 0) {
        currentState = STATE_MANUAL;
        if (json.indexOf("\"state\":\"on\"") >= 0) {
            digitalWrite(RELAY_PIN, RELAY_ON);
            Serial.println("Manual ON");
        } else if (json.indexOf("\"state\":\"off\"") >= 0) {
            digitalWrite(RELAY_PIN, RELAY_OFF);
            Serial.println("Manual OFF");
        }
    } 
    else if (json.indexOf("\"command\":\"timer_off\"") >= 0) {
        int durIndex = json.indexOf("\"duration\":");
        if (durIndex >= 0) {
            int valStart = durIndex + 11;
            int valEnd = json.indexOf("}", valStart);
            if (valEnd < 0) valEnd = json.indexOf(",", valStart);
            if (valEnd >= 0) {
                long durationSec = json.substring(valStart, valEnd).toInt();
                if (durationSec > 0) {
                    currentState = STATE_COUNTDOWN_OFF;
                    timerDurationMs = (unsigned long)durationSec * 1000UL;
                    timerStartTime = millis();
                    digitalWrite(RELAY_PIN, RELAY_ON);
                    Serial.print("Countdown OFF started: ");
                    Serial.print(durationSec);
                    Serial.println("s");
                }
            }
        }
    }
    else if (json.indexOf("\"command\":\"timer_on\"") >= 0) {
        int durIndex = json.indexOf("\"duration\":");
        if (durIndex >= 0) {
            int valStart = durIndex + 11;
            int valEnd = json.indexOf("}", valStart);
            if (valEnd < 0) valEnd = json.indexOf(",", valStart);
            if (valEnd >= 0) {
                long durationSec = json.substring(valStart, valEnd).toInt();
                if (durationSec > 0) {
                    currentState = STATE_COUNTDOWN_ON;
                    timerDurationMs = (unsigned long)durationSec * 1000UL;
                    timerStartTime = millis();
                    digitalWrite(RELAY_PIN, RELAY_OFF);
                    Serial.print("Countdown ON started: ");
                    Serial.print(durationSec);
                    Serial.println("s");
                }
            }
        }
    }
    else if (json.indexOf("\"command\":\"timer_cycle\"") >= 0) {
        int onIndex = json.indexOf("\"on\":");
        int offIndex = json.indexOf("\"off\":");
        if (onIndex >= 0 && offIndex >= 0) {
            int onStart = onIndex + 5;
            int onEnd = json.indexOf(",", onStart);
            int offStart = offIndex + 6;
            int offEnd = json.indexOf("}", offStart);
            if (offEnd < 0) offEnd = json.indexOf(",", offStart);
            
            if (onEnd >= 0 && offEnd >= 0) {
                long onSec = json.substring(onStart, onEnd).toInt();
                long offSec = json.substring(offStart, offEnd).toInt();
                if (onSec > 0 && offSec > 0) {
                    currentState = STATE_CYCLE_ON;
                    cycleOnDurationMs = (unsigned long)onSec * 1000UL;
                    cycleOffDurationMs = (unsigned long)offSec * 1000UL;
                    timerStartTime = millis();
                    digitalWrite(RELAY_PIN, RELAY_ON);
                    Serial.print("Cycle Loop started: ON=");
                    Serial.print(onSec);
                    Serial.print(", OFF=");
                    Serial.println(offSec);
                }
            }
        }
    }
    else if (json.indexOf("\"command\":\"stop\"") >= 0) {
        currentState = STATE_MANUAL;
        digitalWrite(RELAY_PIN, RELAY_OFF);
        Serial.println("Stop and Reset");
    }
}

// ==========================================
// CLOUD SERVER POLLING
// ==========================================
void pollServer() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    WiFiClientSecure client;
    client.setInsecure(); // Bypass SSL verification for Let's Encrypt certificate rotations
    
    HTTPClient http;
    String url = "https://" + String(cloudHost) + "/api/device/poll";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    // Prepare status JSON to send to cloud
    bool relayOn = (digitalRead(RELAY_PIN) == RELAY_ON);
    String modeStr = "manual";
    unsigned long remaining = 0;
    unsigned long total = 0;
    unsigned long now = millis();
    
    if (currentState == STATE_COUNTDOWN_OFF) {
        modeStr = "countdown_off";
        unsigned long elapsed = now - timerStartTime;
        remaining = (timerDurationMs > elapsed) ? (timerDurationMs - elapsed) / 1000 : 0;
        total = timerDurationMs / 1000;
    } else if (currentState == STATE_COUNTDOWN_ON) {
        modeStr = "countdown_on";
        unsigned long elapsed = now - timerStartTime;
        remaining = (timerDurationMs > elapsed) ? (timerDurationMs - elapsed) / 1000 : 0;
        total = timerDurationMs / 1000;
    } else if (currentState == STATE_CYCLE_ON) {
        modeStr = "cycle_on";
        unsigned long elapsed = now - timerStartTime;
        remaining = (cycleOnDurationMs > elapsed) ? (cycleOnDurationMs - elapsed) / 1000 : 0;
        total = cycleOnDurationMs / 1000;
    } else if (currentState == STATE_CYCLE_OFF) {
        modeStr = "cycle_off";
        unsigned long elapsed = now - timerStartTime;
        remaining = (cycleOffDurationMs > elapsed) ? (cycleOffDurationMs - elapsed) / 1000 : 0;
        total = cycleOffDurationMs / 1000;
    }
    
    String ipStr = WiFi.localIP().toString();
    int rssi = WiFi.RSSI();
    
    String jsonPayload = "{";
    jsonPayload += "\"relay\":" + String(relayOn ? "true" : "false") + ",";
    jsonPayload += "\"mode\":\"" + modeStr + "\",";
    jsonPayload += "\"remaining\":" + String(remaining) + ",";
    jsonPayload += "\"total\":" + String(total) + ",";
    jsonPayload += "\"ip\":\"" + ipStr + "\",";
    jsonPayload += "\"rssi\":" + String(rssi);
    jsonPayload += "}";
    
    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
        String response = http.getString();
        handleServerResponse(response);
    } else {
        Serial.print("Cloud poll failed, code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF); // Ensure relay is OFF initially

    Serial.println();
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

    // Try to connect to WiFi STA for up to 10 seconds
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("WiFi connected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("");
        Serial.println("WiFi connection failed. Starting SoftAP mode...");
        WiFi.softAP("Sprinkler-Relay", "12345678");
        Serial.print("Access Point IP: ");
        Serial.println(WiFi.softAPIP());
    }

    // Set up Local Web Server routes
    server.on("/", handleRoot);
    server.on("/api/status", handleStatus);
    server.on("/api/manual", handleManual);
    server.on("/api/timer/off", handleTimerOff);
    server.on("/api/timer/on", handleTimerOn);
    server.on("/api/timer/cycle", handleTimerCycle);
    server.on("/api/stop", handleStop);

    server.begin();
    Serial.println("Local HTTP server started.");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
    server.handleClient();

    unsigned long now = millis();

    // Timer State Machine
    if (currentState == STATE_COUNTDOWN_OFF) {
        if (now - timerStartTime >= timerDurationMs) {
            digitalWrite(RELAY_PIN, RELAY_OFF);
            currentState = STATE_MANUAL;
            Serial.println("Relay OFF countdown complete. Relay set to OFF.");
        }
    } 
    else if (currentState == STATE_COUNTDOWN_ON) {
        if (now - timerStartTime >= timerDurationMs) {
            digitalWrite(RELAY_PIN, RELAY_ON);
            currentState = STATE_MANUAL;
            Serial.println("Relay ON countdown complete. Relay set to ON.");
        }
    } 
    else if (currentState == STATE_CYCLE_ON) {
        if (now - timerStartTime >= cycleOnDurationMs) {
            digitalWrite(RELAY_PIN, RELAY_OFF);
            currentState = STATE_CYCLE_OFF;
            timerStartTime = now;
            Serial.println("Cycle: Switching to OFF state.");
        }
    } 
    else if (currentState == STATE_CYCLE_OFF) {
        if (now - timerStartTime >= cycleOffDurationMs) {
            digitalWrite(RELAY_PIN, RELAY_ON);
            currentState = STATE_CYCLE_ON;
            timerStartTime = now;
            Serial.println("Cycle: Switching to ON state.");
        }
    }

    // Cloud Server Polling (Non-blocking every 1 second)
    if (cloudHost != nullptr && strlen(cloudHost) > 0 && WiFi.status() == WL_CONNECTED) {
        static unsigned long lastPollTime = 0;
        if (now - lastPollTime >= 1000) {
            lastPollTime = now;
            pollServer();
        }
    }
}
