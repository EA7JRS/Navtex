#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Manuel - EA7JRS
#
# Navtex (fork EA7JRS) · Instala el receptor y la interfaz web como servicios.
#
#   sudo sh tools/instalar-servicios.sh                 instala y arranca
#   sudo sh tools/instalar-servicios.sh --usuario pepe  usuario que los ejecuta
#   sudo sh tools/instalar-servicios.sh --quitar        los para y los retira
#
# Antes hay que haber compilado e instalado los programas (./configure, make,
# sudo make install). Qué hace:
#   1. deja /opt/Navtex y su base de datos a nombre del usuario de servicio;
#   2. crea /etc/default/navtex (opciones del receptor, puerto web) si no existe;
#   3. instala navtex-receiver.service y navtex-web.service, sin root;
#   4. si hay otros servicios que usan el mismo receptor SDRplay (SDRconnect,
#      SDR++ en modo servidor), los declara incompatibles: arrancar uno para
#      el otro, y nunca se pisan. En ese caso el receptor NAVTEX queda
#      instalado pero no arranca solo.
set -eu

USUARIO="${SUDO_USER:-}"
QUITAR=0
while [ $# -gt 0 ]; do
  case "$1" in
    --usuario) shift; USUARIO="${1:?falta el nombre del usuario}" ;;
    --quitar) QUITAR=1 ;;
    -h|--help|--ayuda) sed -n '5,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Opción desconocida: $1 (pruebe --ayuda)" >&2; exit 2 ;;
  esac
  shift
done

[ "$(id -u)" = 0 ] || { echo "Ejecútelo con sudo: sudo sh tools/instalar-servicios.sh" >&2; exit 1; }
DIR="$(cd "$(dirname "$0")/.." && pwd)"
UNIDADES=/etc/systemd/system
paso() { printf '\n== %s\n' "$*"; }

if [ "$QUITAR" = 1 ]; then
  systemctl disable --now navtex-receiver.service navtex-web.service 2>/dev/null || true
  rm -f "$UNIDADES/navtex-receiver.service" "$UNIDADES/navtex-web.service"
  rm -rf "$UNIDADES/navtex-receiver.service.d"
  systemctl daemon-reload
  echo "Servicios retirados. Se conservan /opt/Navtex, /etc/default/navtex y los programas."
  exit 0
fi

[ -n "$USUARIO" ] && [ "$USUARIO" != root ] || { echo "Indique el usuario que ejecutará los servicios: --usuario <nombre> (no root)" >&2; exit 1; }
id "$USUARIO" >/dev/null 2>&1 || { echo "No existe el usuario $USUARIO" >&2; exit 1; }
for p in navtexreceiver navtexwebserver; do
  [ -x "/usr/local/bin/$p" ] || { echo "Falta /usr/local/bin/$p: compile e instale antes (./configure && make && sudo make install)" >&2; exit 1; }
done
[ -f /opt/Navtex/Navtex.db ] || { echo "Falta /opt/Navtex/Navtex.db: ejecute antes sudo make install" >&2; exit 1; }

paso "Carpeta de datos para $USUARIO"
chown "$USUARIO" /opt/Navtex /opt/Navtex/Navtex.db
chmod 775 /opt/Navtex
chmod 664 /opt/Navtex/Navtex.db
# Los RTL-SDR se abren con el grupo plugdev
if getent group plugdev >/dev/null 2>&1; then usermod -aG plugdev "$USUARIO"; fi

paso "Ajustes"
if [ -f /etc/default/navtex ]; then
  echo "Se conserva /etc/default/navtex"
else
  install -m 644 "$DIR/systemd/navtex.default" /etc/default/navtex
  echo "Creado /etc/default/navtex"
fi

paso "Servicios"
for u in navtex-receiver navtex-web; do
  sed "s/@USUARIO@/$USUARIO/" "$DIR/systemd/$u.service" > "$UNIDADES/$u.service"
  chmod 644 "$UNIDADES/$u.service"
done

# Otros servicios que abren el mismo receptor SDRplay
OTROS=""
for u in sdrconnect.service sdrpp-server.service; do
  if systemctl cat "$u" >/dev/null 2>&1; then OTROS="$OTROS $u"; fi
done
OTROS="${OTROS# }"
USA_RTL=0
if grep -q '^RECEIVER_OPTS=.*-s *rtlsdr' /etc/default/navtex; then USA_RTL=1; fi
rm -rf "$UNIDADES/navtex-receiver.service.d"
if [ -n "$OTROS" ] && [ "$USA_RTL" = 0 ]; then
  mkdir -p "$UNIDADES/navtex-receiver.service.d"
  printf '# Comparten el receptor SDRplay: arrancar uno para el otro\n[Unit]\nConflicts=%s\n' "$OTROS" > "$UNIDADES/navtex-receiver.service.d/10-receptor-compartido.conf"
fi
systemctl daemon-reload

# restart, y no solo arrancar: si ya estaba en marcha seguiría con el programa anterior
systemctl enable navtex-web.service
systemctl restart navtex-web.service
if [ -n "$OTROS" ] && [ "$USA_RTL" = 0 ]; then
  systemctl disable navtex-receiver.service >/dev/null 2>&1 || true
  echo "El receptor SDRplay lo comparten: $OTROS"
  # Si estaba recibiendo, que siga, pero con el programa nuevo
  if systemctl is-active --quiet navtex-receiver.service; then systemctl restart navtex-receiver.service; fi
  echo "navtex-receiver queda instalado pero NO arranca solo, para no quitarles el receptor."
  echo "  Recibir NAVTEX:      sudo systemctl start navtex-receiver   (para los otros)"
  echo "  Volver a lo de antes: sudo systemctl start ${OTROS%% *}"
else
  systemctl enable navtex-receiver.service
  systemctl restart navtex-receiver.service
fi

paso "Estado"
sleep 3
PUERTO="$(sed -n 's/^WEB_PORT=\([0-9][0-9]*\).*/\1/p' /etc/default/navtex | head -1)"
echo "navtex-web:      $(systemctl is-active navtex-web.service 2>/dev/null || true)  (http://<este equipo>:${PUERTO:-8080}/)"
echo "navtex-receiver: $(systemctl is-active navtex-receiver.service 2>/dev/null || true)"
if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q '^Status: active'; then
  if ! ufw status | grep -q "^${PUERTO:-8080}/tcp"; then
    echo "El cortafuegos está activo y no abre el puerto ${PUERTO:-8080}. Para la red local, por ejemplo:"
    echo "  sudo ufw allow from 192.168.0.0/16 to any port ${PUERTO:-8080} proto tcp"
  fi
fi
echo "Registro:  journalctl -u navtex-receiver -f"
