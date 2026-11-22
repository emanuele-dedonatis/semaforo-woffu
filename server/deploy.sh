#!/bin/sh
# Despliega el backend en la Raspberry: reconstruye y relanza el contenedor.
# Uso: server/deploy.sh   (requiere server/.env, ver .env.example)
set -e
cd "$(dirname "$0")"
[ -f .env ] || { echo "Falta server/.env (copia .env.example)"; exit 1; }
mkdir -p data
docker compose up -d --build
docker image prune -f > /dev/null
echo "$(date '+%F %T') desplegado $(git rev-parse --short HEAD)"
