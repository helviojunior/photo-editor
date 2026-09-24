FROM python:3.11-slim

WORKDIR /app

# Log em container so serve se sair na hora: sem PYTHONUNBUFFERED o stdout do
# Python fica em buffer de bloco sob uwsgi e o `docker logs` atrasa (ou perde,
# se o processo morre) o que foi escrito.
ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1

# Instalar dependências do sistema
RUN apt-get update && apt-get install -y --no-install-recommends \
    libmagic1 \
    curl \
    jq \
    build-essential \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir wheel uWSGI && \
    pip install --no-cache-dir -r requirements.txt

# Modelo de segmentacao das camadas: SAM 2.1 tiny (Meta, Apache-2.0) em ONNX,
# ~155 MB. Vai na imagem para funcionar offline; revisao e hashes fixos, entao
# o build sempre roda o mesmo modelo. Fora de /app: o compose de dev monta o
# codigo por cima de /app.
ARG SEGMENT_MODEL_REPO=onnx-community/sam2.1-hiera-tiny-ONNX
ARG SEGMENT_MODEL_REVISION=814a066640debee5a91e70aa401fb8e17e030503
RUN mkdir -p /opt/models/sam2.1-hiera-tiny && cd /opt/models/sam2.1-hiera-tiny && \
    for f in vision_encoder.onnx vision_encoder.onnx_data \
             prompt_encoder_mask_decoder.onnx prompt_encoder_mask_decoder.onnx_data; do \
        curl -fsSL --retry 3 -o "$f" \
            "https://huggingface.co/${SEGMENT_MODEL_REPO}/resolve/${SEGMENT_MODEL_REVISION}/onnx/$f"; \
    done && \
    printf '%s\n' \
        "4f30aacd3aaefbca81a0b7fe4c1fc96345570ea0a6f80ced599493d1b3be2e8c  vision_encoder.onnx" \
        "e83df9866a5afe68ea7f0f721f18f65137fc3acbf0da1c74e946d363e09c69cc  vision_encoder.onnx_data" \
        "874414704c5d686db7d206a35f6e15d26563d50c8c4468fccc6739bd7e491dcf  prompt_encoder_mask_decoder.onnx" \
        "e9874d900dd4134ed60eab1e97910327c2419e0b2954485d8fd6e7f1a1470f47  prompt_encoder_mask_decoder.onnx_data" \
        | sha256sum -c -

COPY . .

# Tornar entrypoint executável
RUN chmod +x entrypoint.sh

EXPOSE 8001

ENTRYPOINT ["./entrypoint.sh"]

# --enable-threads: sem ele o uwsgi nao deixa rodar a thread da exportacao.
# Um processo so, com threads: o progresso da exportacao vive na memoria do
# processo (services/export.py) e todas as requisicoes precisam enxerga-lo.
CMD ["uwsgi", "--enable-threads", "--threads", "4", "--buffer-size=32768", "--http", "0.0.0.0:8001", "--http-buffer-size=32768", "--module", "core.wsgi", "--env", "DJANGO_SETTINGS_MODULE=core.settings", "--reload-mercy=1", "--worker-reload-mercy=1", "--req-logger", "file:/dev/null", "--touch-reload", "./core/settings.py"]
