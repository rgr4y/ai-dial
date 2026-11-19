#!/bin/sh
# Print the dial's IPv4 address, found by its mDNS name (ESPHome node name). Exit 1 if it isn't on the network.
name=${1:-claude-buddy}.local
echo "find-dial: resolving $name" >&2
ip=$(dscacheutil -q host -a name "$name" | awk '/^ip_address: [0-9.]+$/ { print $2; exit }')
[ -n "$ip" ] || { echo "find-dial: $name not found" >&2; exit 1; }
echo "$ip"
