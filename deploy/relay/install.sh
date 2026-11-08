#!/usr/bin/env bash
# Builds and installs the NexLevel relay on a Debian/Ubuntu server (x86 or ARM) and starts it
# as a service on TCP port 47800. Run from the uploaded folder: bash deploy/relay/install.sh
#   --behind-tunnel   the server is reached through a tunnel such as playit.gg (no public IP needed)
set -euo pipefail
cd "$(dirname "$0")/../.."
EXTRA_ARGS=""
if [[ "${1:-}" == "--behind-tunnel" ]]; then EXTRA_ARGS=" --behind-tunnel"; fi

echo "== installing build tools"
sudo apt-get update -y
sudo apt-get install -y g++ cmake ninja-build ca-certificates curl

# Small servers (1 GB RAM, e.g. AWS t3.micro) can run out of memory while compiling: add swap.
MEM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
if [[ "$MEM_KB" -lt 2000000 ]] && ! swapon --show | grep -q .; then
    echo "== adding 2 GB swap (small server)"
    sudo fallocate -l 2G /swapfile || sudo dd if=/dev/zero of=/swapfile bs=1M count=2048
    sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
    grep -q '/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab >/dev/null
fi

echo "== building nexlevel_relay (a few minutes)"
cmake -S core -B build/relay -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEXLEVEL_BUILD_TESTS=OFF -DNEXLEVEL_BUILD_TOOLS=ON
cmake --build build/relay --target nexlevel_relay -j 1

echo "== installing the service"
sudo install -m 755 build/relay/nexlevel_relay /usr/local/bin/nexlevel_relay
sed "s|--port 47800|--port 47800${EXTRA_ARGS}|" deploy/relay/nexlevel-relay.service | sudo tee /etc/systemd/system/nexlevel-relay.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable nexlevel-relay
sudo systemctl restart nexlevel-relay

echo "== opening TCP port 47800 on this machine"
# Oracle Cloud Ubuntu images reject incoming traffic with iptables rules by default.
if command -v iptables >/dev/null 2>&1; then
    sudo iptables -C INPUT -p tcp --dport 47800 -j ACCEPT 2>/dev/null || sudo iptables -I INPUT -p tcp --dport 47800 -j ACCEPT
    if command -v netfilter-persistent >/dev/null 2>&1; then sudo netfilter-persistent save; fi
fi
if command -v ufw >/dev/null 2>&1 && sudo ufw status | grep -q "Status: active"; then
    sudo ufw allow 47800/tcp
fi

sleep 1
if systemctl is-active --quiet nexlevel-relay; then
    echo
    echo "NexLevel relay is RUNNING on port 47800."
    echo "Public address of this server: $(curl -s --max-time 5 https://api.ipify.org || echo 'see your provider panel')"
    echo "Put that address in the mod setting 'Relay server'."
else
    echo "The relay did not start. Logs:"
    sudo journalctl -u nexlevel-relay --no-pager | tail -20
    exit 1
fi
