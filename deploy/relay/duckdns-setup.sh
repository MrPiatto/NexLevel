#!/usr/bin/env bash
# Keeps a free DuckDNS name (e.g. nexlevel.duckdns.org) pointing at this home server's public IP,
# which changes from time to time. Get NAME and TOKEN at https://www.duckdns.org
#   bash deploy/relay/duckdns-setup.sh NAME TOKEN
set -euo pipefail
NAME="${1:?usage: duckdns-setup.sh NAME TOKEN}"
TOKEN="${2:?usage: duckdns-setup.sh NAME TOKEN}"
mkdir -p "$HOME/duckdns"
cat > "$HOME/duckdns/update.sh" <<SCRIPT
#!/usr/bin/env bash
curl -s "https://www.duckdns.org/update?domains=${NAME}&token=${TOKEN}&ip=" -o "$HOME/duckdns/last.txt"
SCRIPT
chmod 700 "$HOME/duckdns/update.sh"
( crontab -l 2>/dev/null | grep -v duckdns/update.sh; echo "*/5 * * * * $HOME/duckdns/update.sh" ) | crontab -
"$HOME/duckdns/update.sh"
echo "DuckDNS answer: $(cat "$HOME/duckdns/last.txt") (OK = working)"
echo "Friends can now use ${NAME}.duckdns.org as the relay server."
