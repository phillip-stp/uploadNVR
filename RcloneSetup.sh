#!/bin/bash

echo "Starting Google Drive Setup for CloudSync..."

# 0. Safety check: Prevent running as root
if [ "$EUID" -eq 0 ]; then
    echo "[ERROR] Please do not run this script with sudo."
    exit 1
fi

# 1. Install rclone if missing
if ! command -v rclone &> /dev/null
then
    echo "[INFO] rclone not found. Installing rclone..."
    curl https://rclone.org/install.sh | sudo bash
else
    echo "[INFO] rclone is already installed."
fi

# 2. Check for existing 'gdrive' remote
if rclone listremotes 2>/dev/null | grep -q "^gdrive:$"; then
    echo "[INFO] A remote named 'gdrive' already exists."
    echo "[INFO] If you want to reconfigure it, run 'rclone config' manually, or delete it first with 'rclone config delete gdrive'."
else
    # 3. Create the Google Drive remote with Custom Client ID
    echo "========================================================"
    echo "Google Drive API Setup"
    echo "To avoid rate limits and connection drops, please enter"
    echo "your custom Google Drive Client ID and Client Secret."
    echo "(Leave blank to use rclone's default, but it may fail)"
    echo "========================================================"

    read -p "Client ID: " GOOGLE_CLIENT_ID
    read -p "Client Secret: " GOOGLE_CLIENT_SECRET

    echo "[INFO] Configuring Google Drive remote named 'gdrive'..."
    echo "[INFO] A web browser will now open asking you to log into your Google Account."

    if [[ -z "$GOOGLE_CLIENT_ID" || -z "$GOOGLE_CLIENT_SECRET" ]]; then
        echo "[WARNING] No Client ID provided. Using default shared rclone keys."
        rclone config create gdrive drive scope drive
    else
        echo "[INFO] Using custom Client ID for dedicated bandwidth."
        rclone config create gdrive drive scope drive client_id "$GOOGLE_CLIENT_ID" client_secret "$GOOGLE_CLIENT_SECRET"
    fi
fi

# 4. Create the base 'cameras' folder and test the connection
echo "[INFO] Testing connection and ensuring 'cameras' folder exists..."

if rclone mkdir gdrive:cameras; then
    echo "[SUCCESS] Connected to Google Drive!"
    echo "[SUCCESS] The 'gdrive:cameras' path is ready for your C++ application."
else
    echo "[ERROR] Failed to connect to Google Drive."
fi