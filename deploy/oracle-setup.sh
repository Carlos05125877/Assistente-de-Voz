#!/usr/bin/env bash
# =============================================================================
# J.A.R.V.I.S. — instalacao em uma VM Linux gratuita (Oracle Cloud Always Free,
# Raspberry Pi, notebook velho com Ubuntu/Debian...). Roda como servico (systemd),
# reinicia sozinho e guarda os dados em /var/lib/jarvis.
#
# Uso (dentro da VM, na pasta do repositorio clonado):
#   sudo bash deploy/oracle-setup.sh
# Opcional: HTTPS publico gratis sem dominio via Tailscale Funnel:
#   sudo bash deploy/oracle-setup.sh --funnel
# =============================================================================
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR=/opt/jarvis
DATA_DIR=/var/lib/jarvis
ENV_FILE=/etc/jarvis.env

[ "$(id -u)" = 0 ] || { echo "Rode com sudo."; exit 1; }

echo "==> Pacotes do sistema"
apt-get update -y
apt-get install -y python3 python3-venv python3-dev build-essential curl

echo "==> Usuario e pastas"
id jarvis >/dev/null 2>&1 || useradd --system --home "$APP_DIR" --shell /usr/sbin/nologin jarvis
mkdir -p "$APP_DIR" "$DATA_DIR"
cp -r "$REPO_DIR/external_server/." "$APP_DIR/"
python3 -m venv "$APP_DIR/venv"
"$APP_DIR/venv/bin/pip" install --upgrade pip
"$APP_DIR/venv/bin/pip" install -r "$APP_DIR/requirements.txt"
chown -R jarvis:jarvis "$APP_DIR" "$DATA_DIR"

if [ ! -f "$ENV_FILE" ]; then
  echo "==> Criando $ENV_FILE (senha do painel e token do ESP32)"
  read -r -s -p "Senha do painel: " PW; echo
  TOKEN="$(head -c 16 /dev/urandom | od -An -tx1 | tr -d ' \n')"
  cat > "$ENV_FILE" <<ENV
PANEL_PASSWORD=$PW
DEVICE_TOKEN=$TOKEN
DATA_DIR=$DATA_DIR
PORT=8080
# PUBLIC_URL=https://SEU-ENDERECO   (necessario para o callback do Spotify)
ENV
  chmod 600 "$ENV_FILE"
  echo "    DEVICE_TOKEN do ESP32: $TOKEN   (copie para o secrets_local.h)"
fi

echo "==> Servico systemd"
cat > /etc/systemd/system/jarvis.service <<UNIT
[Unit]
Description=J.A.R.V.I.S. servidor (cerebro do assistente de voz)
After=network-online.target
Wants=network-online.target

[Service]
User=jarvis
EnvironmentFile=$ENV_FILE
WorkingDirectory=$APP_DIR
ExecStart=$APP_DIR/venv/bin/python $APP_DIR/jarvis_bridge_server.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now jarvis
sleep 2
systemctl --no-pager --lines=5 status jarvis || true

if [ "${1:-}" = "--funnel" ]; then
  echo "==> Tailscale Funnel (HTTPS publico gratis em https://<maquina>.<sua-rede>.ts.net)"
  command -v tailscale >/dev/null || curl -fsSL https://tailscale.com/install.sh | sh
  tailscale up
  tailscale funnel --bg 8080
  tailscale funnel status
  echo "    Coloque essa URL em PUBLIC_URL ($ENV_FILE) e em JARVIS_SERVER_URL no ESP32; depois: sudo systemctl restart jarvis"
fi

echo
echo "Pronto. Logs: journalctl -u jarvis -f   |   Atualizar: git pull && sudo bash deploy/oracle-setup.sh"
