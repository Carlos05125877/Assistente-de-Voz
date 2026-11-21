@echo off
REM ===========================================================================
REM J.A.R.V.I.S. - servidor no seu PC Windows (gratis). O ESP32 usa
REM http://IP-DO-PC:8080 na rede local. Para iniciar junto com o Windows:
REM   Win+R -> shell:startup -> cole um atalho para este arquivo.
REM ===========================================================================
cd /d "%~dp0\..\external_server"
if not exist ".venv\Scripts\python.exe" (
  echo Criando ambiente Python...
  py -3 -m venv .venv || python -m venv .venv
  .venv\Scripts\python -m pip install -r requirements.txt
)
REM Na rede local a senha e opcional; defina se for expor o painel (Tailscale etc.)
REM set PANEL_PASSWORD=troque-esta-senha
REM set DEVICE_TOKEN=o-mesmo-do-secrets_local.h
:loop
.venv\Scripts\python jarvis_bridge_server.py
echo Servidor parou. Reiniciando em 5 s...
timeout /t 5 >nul
goto loop
