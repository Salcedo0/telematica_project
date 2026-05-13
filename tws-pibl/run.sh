#!/usr/bin/env bash
#
# run.sh — relanza la arquitectura PIBL + TWS desde cero.
#
# Mata los procesos previos, limpia el caché del PIBL y vuelve
# a levantar los 3 backends TWS (8081/8082/8083) y el PIBL (8080).

set -euo pipefail

cd "$(dirname "$0")"

TWS_BIN="./tws/tws"
PIBL_BIN="./pibl/pibl"
PIBL_CFG="./pibl/config.txt"
DOCROOT="./web_content"
LOG_DIR="/tmp"

# ─── colores para la salida ────────────────────────────────────────
C_DIM="\033[2m"; C_OK="\033[1;32m"; C_WARN="\033[1;33m"; C_ERR="\033[1;31m"; C_OFF="\033[0m"

step()  { printf "${C_DIM}[%s]${C_OFF} %s\n" "$1" "$2"; }
ok()    { printf "  ${C_OK}✓${C_OFF} %s\n"   "$1"; }
warn()  { printf "  ${C_WARN}!${C_OFF} %s\n" "$1"; }
fail()  { printf "  ${C_ERR}✗${C_OFF} %s\n"  "$1"; exit 1; }

# ─── pre-chequeos ──────────────────────────────────────────────────
[ -x "$TWS_BIN"  ] || fail "no encuentro $TWS_BIN — corre 'make' primero"
[ -x "$PIBL_BIN" ] || fail "no encuentro $PIBL_BIN — corre 'make' primero"
[ -f "$PIBL_CFG" ] || fail "no encuentro $PIBL_CFG"
[ -d "$DOCROOT"  ] || fail "no encuentro $DOCROOT"

# ─── 1) detener procesos previos ───────────────────────────────────
step "1/4" "deteniendo procesos previos…"
pkill -f "$TWS_BIN"  2>/dev/null && ok "tws detenidos"  || warn "no había tws corriendo"
pkill -f "$PIBL_BIN" 2>/dev/null && ok "pibl detenido"  || warn "no había pibl corriendo"
sleep 1   # margen para que el SO libere los puertos

# ─── 2) limpiar caché ──────────────────────────────────────────────
step "2/4" "limpiando caché del PIBL…"
rm -rf ./cache/*  2>/dev/null || true
ok "cache/ vacío"

# ─── 3) lanzar 3 backends TWS ──────────────────────────────────────
step "3/4" "iniciando 3 backends TWS…"
"$TWS_BIN" 8081 "$LOG_DIR/tws1.log" "$DOCROOT" > /dev/null 2>&1 &
"$TWS_BIN" 8082 "$LOG_DIR/tws2.log" "$DOCROOT" > /dev/null 2>&1 &
"$TWS_BIN" 8083 "$LOG_DIR/tws3.log" "$DOCROOT" > /dev/null 2>&1 &
sleep 1
ok "tws → 8081, 8082, 8083  (docroot=$DOCROOT)"

# ─── 4) lanzar PIBL ────────────────────────────────────────────────
step "4/4" "iniciando PIBL…"
"$PIBL_BIN" "$PIBL_CFG" "$LOG_DIR/pibl.log" > /dev/null 2>&1 &
sleep 1
ok "pibl → 8080  (config=$PIBL_CFG)"

# ─── verificación ──────────────────────────────────────────────────
echo
step "check" "puertos abiertos:"
ss -tlnp 2>/dev/null | awk '/:(8080|8081|8082|8083) / { printf "      %s\n", $0 }' \
  || netstat -tlnp 2>/dev/null | awk '/:(8080|8081|8082|8083) / { printf "      %s\n", $0 }'

echo
step "check" "respuesta HTTP de las 4 páginas (vía PIBL):"
all_ok=1
for c in 1 2 3 4; do
  code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:8080/case$c/index.html" || echo "ERR")
  if [ "$code" = "200" ]; then
    ok "case$c → HTTP 200"
  else
    warn "case$c → HTTP $code"
    all_ok=0
  fi
done

echo
if [ "$all_ok" = "1" ]; then
  printf "${C_OK}listo.${C_OFF} todas las páginas responden 200 OK.\n"
  echo
  echo "abre en el navegador:"
  echo "  http://localhost:8080/case1/index.html"
  echo "  (o reemplaza localhost por la IP pública si estás en EC2)"
  echo
  echo "logs en vivo:"
  echo "  tail -f $LOG_DIR/pibl.log"
  echo "  tail -f $LOG_DIR/tws1.log $LOG_DIR/tws2.log $LOG_DIR/tws3.log"
else
  printf "${C_WARN}atención:${C_OFF} alguna página no respondió 200. Revisa los logs en $LOG_DIR/\n"
  exit 1
fi
