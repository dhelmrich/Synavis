# Create a script that continuously syncs the clock
#!/usr/bin/env bash
while true; do
    if command -v ntpdate >/dev/null 2>&1; then
        sudo ntpdate -u pool.ntp.org || true
    elif command -v timedatectl >/dev/null 2>&1; then
        sudo timedatectl set-ntp true || true
    fi
    sleep 5  # Sync every minute during the build
done

# Run this in the background alongside your build
