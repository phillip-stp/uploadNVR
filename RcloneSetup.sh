#!/bin/bash

echo "Starting Google Drive Setup for CloudSync..."

if [ "$EUID" -eq 0 ]; then
    echo "[ERROR] Please do not run this script with sudo."
    exit 1
fi

if ! command -v rclone &> /dev/null
then
    echo "[INFO] rclone not found. Installing rclone..."
    curl https://rclone.org/install.sh | sudo bash
else
    echo "[INFO] rclone is already installed."
fi

if rclone listremotes 2>/dev/null | grep -q "^gdrive:$"; then
    echo "[INFO] A remote named 'gdrive' already exists."
    echo "[INFO] If you want to reconfigure it, run 'rclone config' manually, or delete it first with 'rclone config delete gdrive'."
else
    echo "========================================================"
    echo "Google Drive API Setup"
    echo "To avoid rate limits and connection drops, please enter"
    echo "your custom Google Drive Client ID and Client Secret."
    echo "(Leave blank to use rclone's default, but it may fail)"
    echo "========================================================"

    read -p "Client ID: " GOOGLE_CLIENT_ID
    read -p "Client Secret: " GOOGLE_CLIENT_SECRET

    echo "[INFO] Configuring Google Drive remote named 'gdrive'..."

    echo "========================================================"
    echo "HEADLESS AUTHENTICATION REQUIRED"
    echo "Since this server might not have a web browser, we will"
    echo "use rclone's headless authentication."
    echo "========================================================"

    if [[ -z "$GOOGLE_CLIENT_ID" || -z "$GOOGLE_CLIENT_SECRET" ]]; then
        echo "[WARNING] No Client ID provided. Using default shared rclone keys."
        rclone config create gdrive drive scope drive config_is_local false
    else
        echo "[INFO] Using custom Client ID for dedicated bandwidth."
        rclone config create gdrive drive scope drive client_id "$GOOGLE_CLIENT_ID" client_secret "$GOOGLE_CLIENT_SECRET" config_is_local false
    fi

    echo "========================================================"
    echo "IMPORTANT NEXT STEPS:"
    echo "If rclone asks 'Use auto config?', type 'n' and press Enter."
    echo "It will give you an 'rclone authorize' command."
    echo "Copy that command, run it in the terminal on your personal laptop (Mac/Windows),"
    echo "log into Google in your browser, copy the token it gives you,"
    echo "and paste it back into this terminal."
    echo "========================================================"
fi

echo "[INFO] Testing connection and ensuring 'cameras' folder exists..."

if rclone mkdir gdrive:cameras; then
    echo "[SUCCESS] Connected to Google Drive!"
    echo "[SUCCESS] The 'gdrive:cameras' path is ready for your C++ application."
else
    echo "[ERROR] Failed to connect to Google Drive."
fi