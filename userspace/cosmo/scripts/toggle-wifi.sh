#!/bin/bash
# Toggle Wi-Fi on/off via NetworkManager.
# Bound to Meta+F (XF86RFKill) via xbindkeys — see ../autostart/xbindkeys.desktop
if nmcli radio wifi | grep -q "enabled"; then
    nmcli radio wifi off
else
    nmcli radio wifi on
    # Re-activate the first available saved connection after radio comes back
    nmcli connection up "$(nmcli -t -f NAME,TYPE connection show --active 2>/dev/null | grep wifi | head -1 | cut -d: -f1)" 2>/dev/null || \
        nmcli connection up "$(nmcli -t -f NAME,TYPE connection show | grep wifi | head -1 | cut -d: -f1)" 2>/dev/null || true
fi
