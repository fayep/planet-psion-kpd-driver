#!/bin/bash
# Toggle Wi-Fi on/off via NetworkManager.
# Bound to Meta+F (XF86RFKill) via xbindkeys — see ../autostart/xbindkeys.desktop
if [ "$(nmcli radio wifi)" = "enabled" ]; then
    nmcli radio wifi off
else
    nmcli radio wifi on
fi
