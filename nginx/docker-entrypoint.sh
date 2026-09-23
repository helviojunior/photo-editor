#!/bin/sh
set -e

BRAND_NAME="${BRAND_NAME:-PhotoEditor}"
TLS_CN="${TLS_CN:-photoeditor.local}"
# FORCE_TLS=true (padrao): HTTP redireciona para HTTPS + HSTS.
# FORCE_TLS=false: a app tambem responde via HTTP (sem redirect, sem HSTS).
FORCE_TLS="${FORCE_TLS:-true}"

# Real client IP behind trusted proxies. USE_REAL_IP toggles the whole feature;
# the values come with defaults so it works out of the box when enabled.
USE_REAL_IP="${USE_REAL_IP:-true}"
REAL_IP_FROM="${REAL_IP_FROM:-192.168.0.0/16,10.0.0.0/8,172.16.0.0/12}"
REAL_IP_HEADER="${REAL_IP_HEADER:-SC-Connecting-IP}"
REAL_IP_RECURSIVE="${REAL_IP_RECURSIVE:-on}"

TLS_DIR="/etc/nginx/tls"
CERT_FILE="${TLS_DIR}/tls.cer"
KEY_FILE="${TLS_DIR}/tls.key"
INCLUDES_DIR="/etc/nginx/includes"

mkdir -p "$INCLUDES_DIR"

# Generate self-signed certificate if none exists
if [ ! -f "$CERT_FILE" ] || [ ! -f "$KEY_FILE" ]; then
    echo "[nginx-entrypoint] TLS certificate not found, generating self-signed..."
    mkdir -p "$TLS_DIR"
    openssl req -x509 -nodes -days 3650 \
        -newkey rsa:2048 \
        -keyout "$KEY_FILE" \
        -out "$CERT_FILE" \
        -subj "/C=BR/ST=SP/L=SaoPaulo/O=${BRAND_NAME}/CN=${TLS_CN}"
    echo "[nginx-entrypoint] Self-signed certificate created."
else
    echo "[nginx-entrypoint] TLS certificate found, using existing."
fi

# Gera o real_ip.conf conforme USE_REAL_IP: uma linha set_real_ip_from por faixa
# do REAL_IP_FROM (virgula/espaco), o header e o modo recursivo. Desligado =
# arquivo vazio (nginx nao reescreve o IP de origem).
case "$(echo "$USE_REAL_IP" | tr '[:upper:]' '[:lower:]')" in
    true|1|yes|on)
        {
            echo "$REAL_IP_FROM" | tr ',' ' ' | tr -s ' ' '\n' | while read -r cidr; do
                [ -n "$cidr" ] && echo "set_real_ip_from ${cidr};"
            done
            echo "real_ip_header ${REAL_IP_HEADER};"
            echo "real_ip_recursive ${REAL_IP_RECURSIVE};"
        } > "${INCLUDES_DIR}/real_ip.conf"
        echo "[nginx-entrypoint] real_ip: on — header=${REAL_IP_HEADER}, from=[${REAL_IP_FROM}]"
        ;;
    *)
        : > "${INCLUDES_DIR}/real_ip.conf"
        echo "[nginx-entrypoint] real_ip: off (USE_REAL_IP=${USE_REAL_IP})"
        ;;
esac

# Normaliza FORCE_TLS (true/1/yes/on).
case "$(echo "$FORCE_TLS" | tr '[:upper:]' '[:lower:]')" in
    true|1|yes|on) FORCE_TLS_ON=1 ;;
    *)             FORCE_TLS_ON=0 ;;
esac

if [ "$FORCE_TLS_ON" = "1" ]; then
    echo "[nginx-entrypoint] FORCE_TLS=on — HTTP redireciona para HTTPS."
    # So redireciona quando o proto efetivo NAO e https. Assim, se um proxy
    # upstream ja terminou o TLS (X-Forwarded-Proto: https) e a conexao chega
    # aqui pela porta 80, a app e servida normalmente — sem loop de redirect.
    cat > "${INCLUDES_DIR}/http_mode.conf" <<EOF
if (\$forwarded_proto != "https") {
    return 301 https://\$host\$request_uri;
}
include /etc/nginx/includes/app_locations.conf;
EOF
    cat > "${INCLUDES_DIR}/hsts.conf" <<'EOF'
add_header Strict-Transport-Security "max-age=31536000; includeSubDomains" always;
EOF
else
    echo "[nginx-entrypoint] FORCE_TLS=off — HTTP tambem serve a aplicacao."
    cat > "${INCLUDES_DIR}/http_mode.conf" <<'EOF'
include /etc/nginx/includes/app_locations.conf;
EOF
    : > "${INCLUDES_DIR}/hsts.conf"
fi

exec nginx -g "daemon off;"
