# Sprinkler Relay Proxy Server

This is the proxy server backend designed to be deployed on [Render](https://render.com) to allow remote access to your NodeMCU (ESP8266 or ESP32) relay controller from anywhere in the world.

## How It Works
1. The **NodeMCU** connects to your home WiFi. It periodically sends a lightweight HTTP POST request containing its status (relay state, active timers, WiFi signal strength, etc.) to this server.
2. The **Web Dashboard** is hosted by this server and can be loaded in any browser via your public Render URL (e.g. `https://your-app.onrender.com`).
3. When you trigger an action (toggle relay, set timer) on the Web Dashboard, the command is placed in a temporary queue. The next time the NodeMCU polls the server (every 1 second), it retrieves the command, runs it locally, and reports the updated state.

---

## Deployment Steps

### Step 1: Initialize a Git Repository
Render deploys apps directly from GitHub or GitLab. You need to push this `server/` directory to a GitHub repository:
1. Open terminal in the `server/` directory.
2. Run the following commands:
   ```bash
   git init
   git add .
   git commit -m "Initial commit"
   ```
3. Create a new repository on your GitHub account (e.g., `sprinkler-relay-server`).
4. Link your local repo to GitHub and push:
   ```bash
   git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPO_NAME.git
   git branch -M main
   git push -u origin main
   ```

### Step 2: Deploy to Render
1. Create a free account at [Render](https://render.com).
2. Click **New +** in the dashboard and select **Web Service**.
3. Connect your GitHub account and select your `sprinkler-relay-server` repository.
4. Set the following settings:
   - **Name:** `sprinkler-relay-pro` (or any unique name)
   - **Runtime:** `Node`
   - **Build Command:** `npm install`
   - **Start Command:** `npm start`
   - **Instance Type:** `Free`
5. Click **Deploy Web Service**.
6. Once deployed, Render will provide a public URL like `https://sprinkler-relay-pro.onrender.com`.

### Step 3: Configure the NodeMCU C++ Code
1. Open your Arduino sketch `node.ino`.
2. Locate the line:
   ```cpp
   const char* cloudHost = "";
   ```
3. Change it to your Render domain (without `https://` or trailing slashes):
   ```cpp
   const char* cloudHost = "sprinkler-relay-pro.onrender.com";
   ```
4. Flash the code to your NodeMCU.

Once flashed, your NodeMCU will start polling the Render server. Open your Render URL in your phone or PC browser, and you will be able to control the relay remotely!
