# Imagem do servidor J.A.R.V.I.S. (cerebro). O ESP32 so captura/toca audio.
FROM python:3.12-slim
ENV PYTHONUNBUFFERED=1 PORT=8080 DATA_DIR=/data
WORKDIR /app
COPY external_server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY external_server/ .
EXPOSE 8080
CMD ["python", "jarvis_bridge_server.py"]
