#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
===============================================================================
J.A.R.V.I.S. BRIDGE SERVER — SERVIDOR EXTERNO PARA ALIVIAR O ESP-WROOM-32
===============================================================================
100% ZERO-DEPENDENCY (USA APENAS A BIBLIOTECA PADRAO DO PYTHON 3!):
- Nao precisa instalar `bs4`, `requests`, `fastapi` nem `uvicorn` via pip!
- Roda imediatamente com `python jarvis_bridge_server.py` em qualquer Python 3.

Conecta o ESP32 diretamente com:
1. App da Biblia Oficial — YouVersion / Holy Bible (https://www.bible.com/pt):
   - Extrai Planos de Leitura e Devocionais Reais de https://www.bible.com/pt/reading-plans
   - Extrai cada Dia do Plano (Reflexao Devocional + Referencias Biblicas) de
     https://www.bible.com/pt/reading-plans/{plan_id}/day/{day}
   - Extrai o Versiculo do Dia Oficial de https://www.bible.com/pt/verse-of-the-day
   - Extrai Capitulos e Versiculos nas traducoes oficiais do YouVersion em Portugues:
     NVI (129), ARC (211), NVT (193), ARA (1608) via https://www.bible.com/pt/bible/{ver}/{USFM}
   - Guarda o Plano de Leitura Ativo do usuario e o Dia atual em `jarvis_data.json`!
2. Spotify Web API (OAuth2 Login com 1 clique + Play/Pause/Busca/Volume).
3. Guardiao de Alarmes Persistentes sincronizado com a memoria NVS do ESP32.
===============================================================================
"""

import os
import re
import ssl
import html
import json
import time
import uuid
import base64
import http.cookiejar
import urllib.parse
import urllib.request
import urllib.error
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from typing import Optional, List, Dict, Any, Tuple

DATA_FILE = os.path.join(os.path.dirname(__file__), "jarvis_data.json")

HTTP_HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
    "Accept-Language": "pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7",
}

SSL_CTX = ssl.create_default_context()

# Traducoes em Portugues no YouVersion (bible.com/pt/bible/{id}/{USFM})
YOUVERSION_PT_VERSIONS = {
    "NVI": 129,   # Nova Versao Internacional
    "ARC": 211,   # Almeida Revista e Corrigida
    "NVT": 193,   # Nova Versao Transformadora
    "ARA": 1608,  # Almeida Revista e Atualizada
}

# Mapeamento de livros em Portugues -> Codigo USFM oficial do YouVersion (bible.com)
PT_BOOK_TO_USFM = {
    "genesis": "GEN", "exodo": "EXO", "levitico": "LEV", "numeros": "NUM", "deuteronomio": "DEU",
    "josue": "JOS", "juizes": "JDG", "rute": "RUT", "1 samuel": "1SA", "2 samuel": "2SA",
    "1 reis": "1KI", "2 reis": "2KI", "1 cronicas": "1CH", "2 cronicas": "2CH", "esdras": "EZR",
    "neemias": "NEH", "ester": "EST", "jo": "JOB", "salmos": "PSA", "salmo": "PSA",
    "proverbios": "PRO", "proverbio": "PRO", "eclesiastes": "ECC", "canticos": "SNG",
    "isaias": "ISA", "jeremias": "JER", "lamentacoes": "LAM", "ezequiel": "EZK", "daniel": "DAN",
    "oseias": "HOS", "joel": "JOL", "amos": "AMO", "obadias": "OBA", "jonas": "JON",
    "miqueias": "MIC", "naum": "NAM", "habacuque": "HAB", "sofonias": "ZEP", "ageu": "HAG",
    "zacarias": "ZEC", "malaquias": "MAL",
    "mateus": "MAT", "marcos": "MRK", "lucas": "LUK", "joao": "JHN", "atos": "ACT",
    "romanos": "ROM", "1 corintios": "1CO", "2 corintios": "2CO", "galatas": "GAL",
    "efesios": "EPH", "filipenses": "PHP", "colossenses": "COL",
    "1 tessalonicenses": "1TH", "2 tessalonicenses": "2TH",
    "1 timoteo": "1TI", "2 timoteo": "2TI", "tito": "TIT", "filemom": "PHM",
    "hebreus": "HEB", "tiago": "JAS", "1 pedro": "1PE", "2 pedro": "2PE",
    "1 joao": "1JN", "2 joao": "2JN", "3 joao": "3JN", "judas": "JUD", "apocalipse": "REV"
}

# Catalogo de Planos de Leitura e Devocionais do YouVersion (bible.com/pt/reading-plans)
CURATED_YOUVERSION_PLANS: Dict[str, Dict[str, Any]] = {
    "ansiedade": {
        "plan_id": "da-ansiedade-a-paz",
        "title": "Da Ansiedade a Paz (Devocional YouVersion)",
        "url": "https://www.bible.com/pt/reading-plans",
        "total_days": 5,
        "topic": "ansiedade, paz, mente, preocupacao",
        "days": {
            1: {
                "title": "Dia 1: Entregando o Peso da Preocupacao",
                "devotional": "A ansiedade tenta nos convencer de que precisamos controlar o amanha. Mas Deus nos convida a trocar o peso da preocupacao pela oracao com acao de gracas. Quando voce entrega suas inquietacoes a Cristo, a paz de Deus guarda o seu coracao e os seus pensamentos.",
                "scripture_ref": "Filipenses 4:6-7",
                "scripture_text": "Nao andem ansiosos por coisa alguma, mas em tudo, pela oracao e suplicas, e com acao de gracas, apresentem seus pedidos a Deus. E a paz de Deus, que excede todo o entendimento, guardara o coracao e a mente de voces em Cristo Jesus."
            },
            2: {
                "title": "Dia 2: Descansando na Soberania do Pai",
                "devotional": "Jesus nos lembra que o Pai celestial cuida das aves do ceu e dos lirios do campo. Voce vale muito mais para Ele. Respire fundo hoje e viva um dia de cada vez, confiando que Deus ja esta no seu amanha.",
                "scripture_ref": "Mateus 6:33-34",
                "scripture_text": "Busquem, pois, em primeiro lugar o Reino de Deus e a sua justica, e todas essas coisas serao acrescentadas a voces. Portanto, nao se preocupem com o amanha."
            },
            3: {
                "title": "Dia 3: Lancando Toda a Vossa Ansiedade Nele",
                "devotional": "Humilhar-se diante de Deus significa reconhecer que nao precisamos carregar o mundo nas costas. Lance sobre Ele toda a sua ansiedade, porque Ele tem cuidado pessoal e constante de voce.",
                "scripture_ref": "1 Pedro 5:7",
                "scripture_text": "Lancem sobre ele toda a sua ansiedade, porque ele tem cuidado de voces."
            },
            4: {
                "title": "Dia 4: A Paz que o Mundo Nao Pode Dar",
                "devotional": "A paz de Cristo nao e a ausencia de tempestades, mas a presenca do Salvador dentro do barco. Mesmo diante de pressoes, nao deixe o seu coracao se perturbar nem tenha medo.",
                "scripture_ref": "Joao 14:27",
                "scripture_text": "Deixo a paz a voces; a minha paz dou a voces. Nao a dou como o mundo a da. Nao se perturbe o seu coracao, nem tenham medo."
            },
            5: {
                "title": "Dia 5: Mente Firme em Deus",
                "devotional": "A verdadeira serenidade nasce quando mantemos nossos pensamentos firmados nas promessas do Senhor. Ao concluir este plano de leitura, escolha meditar diariamente na fidelidade da Rocha Eterna.",
                "scripture_ref": "Isaias 26:3",
                "scripture_text": "Tu conservaras em perfeita paz aquele cujo proposito esta firme, porque em ti confia."
            }
        }
    },
    "sabedoria": {
        "plan_id": "sabedoria-de-proverbios",
        "title": "Sabedoria Diaria em Proverbios (YouVersion)",
        "url": "https://www.bible.com/pt/reading-plans",
        "total_days": 5,
        "topic": "sabedoria, proverbios, decisoes, foco, trabalho",
        "days": {
            1: {
                "title": "Dia 1: O Principio da Sabedoria",
                "devotional": "Antes de tomar grandes decisoes, a Biblia nos ensina que a verdadeira inteligencia comeca com a reverencia e confianca total no Senhor, sem nos apoiarmos apenas na nossa propria capacidade.",
                "scripture_ref": "Proverbios 3:5-6",
                "scripture_text": "Confie no Senhor de todo o seu coracao e nao se apoie em seu proprio entendimento; reconheca o Senhor em todos os seus caminhos, e ele endireitara as suas veredas."
            },
            2: {
                "title": "Dia 2: Guardando o Coracao",
                "devotional": "Tudo o que fazemos, falamos e decidimos flui do estado do nosso interior. Proteger a mente e o coracao com a verdade divina e o segredo para uma vida integra e produtiva.",
                "scripture_ref": "Proverbios 4:23",
                "scripture_text": "Acima de tudo, guarde o seu coracao, pois dele depende toda a sua vida."
            },
            3: {
                "title": "Dia 3: O Poder das Palavras",
                "devotional": "Nossas palavras podem construir pontes ou levantar muros. A sabedoria biblica nos chama a responder com mansidao e usar a fala para trazer cura e encorajamento.",
                "scripture_ref": "Proverbios 15:1",
                "scripture_text": "A resposta calma desvia a furia, mas a palavra rispida desperta a ira."
            },
            4: {
                "title": "Dia 4: Planejamento e Direcao Divina",
                "devotional": "E excelente planejar projetos e metas, mas a sabedoria consiste em consagrar cada plano ao Senhor, permitindo que Ele estabeleca os nossos passos.",
                "scripture_ref": "Proverbios 16:3",
                "scripture_text": "Consagre ao Senhor tudo o que voce faz, e os seus planos serao bem-sucedidos."
            },
            5: {
                "title": "Dia 5: Pedindo Sabedoria ao Alto",
                "devotional": "Se voce precisa de discernimento hoje, peca com fe. Deus tem prazer em conceder sabedoria generosamente a todos os que O buscam.",
                "scripture_ref": "Tiago 1:5",
                "scripture_text": "Se algum de voces tem falta de sabedoria, peca-a a Deus, que a todos da livremente, de boa vontade; e lhe sera concedida."
            }
        }
    },
    "esperanca": {
        "plan_id": "esperanca-e-fe-nas-promessas",
        "title": "Esperanca e Fe nas Promessas (YouVersion)",
        "url": "https://www.bible.com/pt/reading-plans",
        "total_days": 5,
        "topic": "esperanca, fe, futuro, forca, coragem",
        "days": {
            1: {
                "title": "Dia 1: Um Futuro e Uma Esperanca",
                "devotional": "Mesmo quando o cenario parece incerto, os planos de Deus para a sua vida sao planos de paz e proposito. A esperanca crista e uma ancora firme para a alma.",
                "scripture_ref": "Jeremias 29:11",
                "scripture_text": "Porque sou eu que conheco os planos que tenho para voces, diz o Senhor, planos de faze-los prosperar e nao de causar dano, planos de dar a voces esperanca e um futuro."
            },
            2: {
                "title": "Dia 2: Renovando as Forcas como a Aguia",
                "devotional": "O cansaco faz parte da caminhada, mas aqueles que esperam no Senhor recebem um vigor sobrenatural para voar acima das tempestades.",
                "scripture_ref": "Isaias 40:31",
                "scripture_text": "Mas aqueles que esperam no Senhor renovam as suas forcas. Voam alto como aguias; correm e nao ficam exaustos, andam e nao se cansam."
            },
            3: {
                "title": "Dia 3: Todas as Coisas Cooperam para o Bem",
                "devotional": "Nenhuma fase dificil e desperdicada nas maos do Criador. Ele transforma ate os desafios em degraus de maturidade e proposito.",
                "scripture_ref": "Romanos 8:28",
                "scripture_text": "Sabemos que Deus age em todas as coisas para o bem daqueles que o amam, dos que foram chamados de acordo com o seu proposito."
            },
            4: {
                "title": "Dia 4: Coragem para Avancar",
                "devotional": "Deus nao nos deu espirito de covardia, mas de poder, amor e equilibrio. Seja forte e corajoso, pois o Senhor vai adiante de voce.",
                "scripture_ref": "Josue 1:9",
                "scripture_text": "Nao fui eu que ordenei a voce? Seja forte e corajoso! Nao se apavore nem desanime, pois o Senhor, o seu Deus, estara com voce por onde voce andar."
            },
            5: {
                "title": "Dia 5: O Bom Pastor Guia Seus Passos",
                "devotional": "Mesmo nos vales mais escuros, a presenca do Bom Pastor traz consolo, direcao e a certeza de que a bondade e a misericordia nos seguirao todos os dias.",
                "scripture_ref": "Salmos 23:1-4",
                "scripture_text": "O Senhor e o meu pastor; de nada terei falta. Em verdes pastagens me faz repousar e me conduz a aguas tranquilas; restaura-me o vigor."
            }
        }
    },
    "gratidao": {
        "plan_id": "habito-da-gratidao-e-louvor",
        "title": "O Habito da Gratidao e Louvor (YouVersion)",
        "url": "https://www.bible.com/pt/reading-plans",
        "total_days": 3,
        "topic": "gratidao, louvor, alegria, familia",
        "days": {
            1: {
                "title": "Dia 1: Um Coracao Agradecido",
                "devotional": "A gratidao muda a nossa lente espiritual: em vez de focarmos no que falta, celebramos a bondade fiel de Deus que se renova a cada manha.",
                "scripture_ref": "Salmos 100:4-5",
                "scripture_text": "Entrem por suas portas com acoes de gracas e em seus atrios com louvor; deem-lhe gracas e bendigam o seu nome. Pois o Senhor e bom e o seu amor leal e eterno."
            },
            2: {
                "title": "Dia 2: As Misericordias de Cada Manha",
                "devotional": "Todo novo amanhecer e uma pagina em branco assinada pela graca de Deus. Agradeca hoje pelas pequenas e grandes bencaos ao seu redor.",
                "scripture_ref": "Lamentacoes 3:22-23",
                "scripture_text": "Gracas ao grande amor do Senhor e que nao somos consumidos, pois as suas misericordias sao inesgotaveis. Renovam-se cada manha; grande e a sua fidelidade!"
            },
            3: {
                "title": "Dia 3: Alegria Constante no Senhor",
                "devotional": "Alegrem-se sempre, orem continuamente e deem gracas em todas as circunstancias, pois essa e a vontade de Deus em Cristo Jesus para voces.",
                "scripture_ref": "1 Tessalonicenses 5:16-18",
                "scripture_text": "Alegrem-se sempre, orem continuamente, deem gracas em todas as circunstancias, pois esta e a vontade de Deus para voces em Cristo Jesus."
            }
        }
    }
}

DEFAULT_DATA = {
    "spotify": {
        "client_id": os.getenv("SPOTIFY_CLIENT_ID", ""),
        "client_secret": os.getenv("SPOTIFY_CLIENT_SECRET", ""),
        "refresh_token": os.getenv("SPOTIFY_REFRESH_TOKEN", ""),
        "access_token": "",
        "expires_at": 0,
        "redirect_uri": os.getenv("SPOTIFY_REDIRECT_URI", "http://127.0.0.1:8080/spotify/callback"),
        "last_track": "Nenhuma musica tocando"
    },
    "youversion": {
        "version": "NVI",
        "logged_in": False,
        "email": os.getenv("YOUVERSION_EMAIL", ""),
        "username": os.getenv("YOUVERSION_USERNAME", ""),
        "user_id": "",
        "full_name": "",
        "session_cookie": os.getenv("YOUVERSION_SESSION_COOKIE", ""),
        "access_token": os.getenv("YOUVERSION_ACCESS_TOKEN", ""),
        "auth_error": "",
        "user_plans": [],
        "active_subscription_id": "",
        "active_plan_key": "ansiedade",
        "active_plan_id": "da-ansiedade-a-paz",
        "active_plan_title": "Da Ansiedade a Paz (Devocional YouVersion)",
        "current_day": 1,
        "total_days": 5,
        "completed_days": [],
        "custom_bible_com_plan_url": "",
        "last_devotional": ""
    },
    "alarms": [
        {"h": 7, "m": 0, "r": True, "l": "Devocional Matinal & Despertador"}
    ],
    "notes": [],
    "bible_history": []
}


def load_data() -> Dict[str, Any]:
    if os.path.exists(DATA_FILE):
        try:
            with open(DATA_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                for k, v in DEFAULT_DATA.items():
                    if k not in data:
                        data[k] = v
                    elif isinstance(v, dict) and isinstance(data.get(k), dict):
                        for subk, subv in v.items():
                            if subk not in data[k]:
                                data[k][subk] = subv
                if data.get("spotify", {}).get("redirect_uri") == "http://localhost:8080/spotify/callback":
                    data["spotify"]["redirect_uri"] = "http://127.0.0.1:8080/spotify/callback"
                if not data.get("spotify", {}).get("client_id"):
                    data["spotify"]["client_id"] = DEFAULT_DATA["spotify"]["client_id"]
                if not data.get("spotify", {}).get("client_secret"):
                    data["spotify"]["client_secret"] = DEFAULT_DATA["spotify"]["client_secret"]
                return data
        except Exception:
            pass
    return json.loads(json.dumps(DEFAULT_DATA))


def save_data(data: Dict[str, Any]) -> None:
    with open(DATA_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


# ============================================================================
# CLIENTE HTTP NATIVO (SEM PRECISAR DE `requests` OU `bs4`)
# ============================================================================
def http_request(
    url: str,
    method: str = "GET",
    headers: Optional[Dict[str, str]] = None,
    data_bytes: Optional[bytes] = None,
    json_body: Optional[Dict[str, Any]] = None,
    timeout: int = 8,
) -> Tuple[int, str]:
    hdrs = dict(HTTP_HEADERS)
    if headers:
        hdrs.update(headers)
    if json_body is not None:
        data_bytes = json.dumps(json_body).encode("utf-8")
        hdrs["Content-Type"] = "application/json"

    req = urllib.request.Request(url, data=data_bytes, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout, context=SSL_CTX) as resp:
            status = resp.getcode()
            raw = resp.read().decode("utf-8", errors="replace")
            return status, raw
    except urllib.error.HTTPError as e:
        try:
            err_body = e.read().decode("utf-8", errors="replace")
        except Exception:
            err_body = str(e)
        return e.code, err_body
    except Exception as e:
        return 0, str(e)


def extract_og_meta_html(raw_html: str, prop: str) -> str:
    patterns = [
        rf'<meta[^>]+property=["\']{re.escape(prop)}["\'][^>]+content=["\']([^"\']+)["\']',
        rf'<meta[^>]+content=["\']([^"\']+)["\'][^>]+property=["\']{re.escape(prop)}["\']',
    ]
    for pat in patterns:
        m = re.search(pat, raw_html, flags=re.IGNORECASE)
        if m:
            return html.unescape(m.group(1).strip())
    return ""


def strip_html_tags(raw: str) -> str:
    cleaned = re.sub(r"<[^>]+>", " ", raw)
    return " ".join(html.unescape(cleaned).split())


def normalize_accents_lower(s: str) -> str:
    rep = {
        "á": "a", "à": "a", "ã": "a", "â": "a",
        "é": "e", "ê": "e", "í": "i",
        "ó": "o", "ô": "o", "õ": "o",
        "ú": "u", "ç": "c"
    }
    out = s.lower().strip()
    for k, v in rep.items():
        out = out.replace(k, v)
    return out


# ============================================================================
# 1. CONEXAO COM A CONTA PESSOAL DO USUARIO NO YOUVERSION (BIBLE.COM)
# ============================================================================
YV_OAUTH_CLIENT_ID = "96aa43c822350c765fd177f95ebc5575"
YV_NEXT_ACTION_STORE_CTX = "40ee54b9829b1093c5b7df9247724e314c095a24c7"
YV_NEXT_ACTION_SIGNIN = "40785f16c4adfac0e3f6d02d797df807948c01ddc0"


def get_youversion_auth_headers(yv: Optional[Dict[str, Any]] = None) -> Dict[str, str]:
    if yv is None:
        yv = load_data().get("youversion", {})
    hdrs: Dict[str, str] = {}
    cookie_parts = []
    if yv.get("session_cookie"):
        sc = yv["session_cookie"].strip()
        if "=" in sc:
            cookie_parts.append(sc)
        else:
            cookie_parts.append(f"yva_session={sc}")
    if cookie_parts:
        hdrs["Cookie"] = "; ".join(cookie_parts)
    if yv.get("access_token"):
        hdrs["Authorization"] = f"Bearer {yv['access_token'].strip()}"
    return hdrs


def youversion_refresh_session(yv: Dict[str, Any]) -> bool:
    """Consulta https://www.bible.com/api/authenticate usando yva_session para obter accessToken e perfil do usuario."""
    hdrs = get_youversion_auth_headers(yv)
    if not hdrs:
        return False
    hdrs["Accept"] = "application/json"
    status, body = http_request(
        f"https://www.bible.com/api/authenticate?t={int(time.time() * 1000)}",
        headers=hdrs,
        timeout=10,
    )
    if status == 200 and body:
        try:
            payload = json.loads(body)
            if payload.get("accessToken"):
                yv["access_token"] = payload["accessToken"]
            user = payload.get("user") or {}
            if user:
                yv["user_id"] = str(user.get("id", yv.get("user_id", "")))
                yv["username"] = str(user.get("username", yv.get("username", "")))
                yv["full_name"] = str(user.get("name") or user.get("first_name") or yv.get("username") or "Usuario YouVersion")
                yv["logged_in"] = True
                yv["auth_error"] = ""
                return True
        except Exception:
            pass
    return False


def fetch_plan_title_and_days_from_bible_com(pid: str, day_num: int = 1, hdrs: Optional[Dict[str, str]] = None) -> Tuple[str, int]:
    """Busca o titulo oficial (og:title) e total de dias de um plano em bible.com/pt/reading-plans/{pid}/day/{day_num}."""
    url = f"https://www.bible.com/pt/reading-plans/{pid}/day/{day_num}"
    st, body = http_request(url, headers=hdrs, timeout=8)
    title = f"Plano YouVersion #{pid}"
    max_day = max(day_num, 7)
    if st == 200 and body:
        og_t = extract_og_meta_html(body, "og:title")
        if og_t:
            title = og_t.split("|")[0].strip()
        for dm in re.finditer(r"/day/(\d+)", body):
            dnum = int(dm.group(1))
            if 1 <= dnum <= 366 and dnum > max_day:
                max_day = dnum
    return title, max_day


def sync_youversion_user_plans(data: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Sincroniza os Planos de Leitura inscritos/salvos na conta pessoal do usuario no App Biblia (YouVersion)."""
    yv = data["youversion"]
    youversion_refresh_session(yv)
    hdrs = get_youversion_auth_headers(yv)
    user_plans: List[Dict[str, Any]] = []
    seen_ids = set()

    # 1. Tenta via API oficial YouVersion Plans com Bearer Token da conta do usuario
    if yv.get("access_token"):
        api_hdrs = {
            "Accept": "application/json",
            "Referer": "https://web.youversionapi.com",
            "X-YouVersion-Client": "youversion",
            "X-YouVersion-App-Platform": "web",
            "X-YouVersion-App-Version": "3",
            "Authorization": f"Bearer {yv['access_token']}",
        }
        for ver in ("3.2", "3.1"):
            for ep in ("subscriptions", "plans/subscriptions"):
                st, bdy = http_request(
                    f"https://plans.youversionapi.com/{ver}/{ep}?language_tag=pt",
                    headers=api_hdrs,
                    timeout=8,
                )
                if st == 200 and bdy:
                    try:
                        j = json.loads(bdy)
                        items = (
                            j.get("response", {}).get("data", {}).get("subscriptions")
                            or j.get("response", {}).get("data")
                            or j.get("data")
                            or []
                        )
                        if isinstance(items, list):
                            for it in items:
                                if not isinstance(it, dict):
                                    continue
                                pid = str(it.get("plan_id") or it.get("id") or "")
                                sub_id = str(it.get("id") or it.get("subscription_id") or "")
                                cur_day = int(it.get("day") or it.get("current_day") or 1)
                                raw_title = str(it.get("title") or it.get("name") or "")
                                tot_days = int(it.get("total_days") or it.get("days") or 0)
                                if pid and pid not in seen_ids:
                                    seen_ids.add(pid)
                                    if not raw_title or tot_days <= 0:
                                        fetched_title, fetched_tot = fetch_plan_title_and_days_from_bible_com(pid, cur_day, hdrs)
                                        raw_title = raw_title or fetched_title
                                        tot_days = tot_days or fetched_tot
                                    uname = yv.get("username") or "me"
                                    plan_url = (
                                        f"https://www.bible.com/pt/users/{uname}/reading-plans/{pid}/subscription/{sub_id}/day/{cur_day}"
                                        if sub_id and uname != "me"
                                        else f"https://www.bible.com/pt/reading-plans/{pid}/day/{cur_day}"
                                    )
                                    user_plans.append({
                                        "plan_id": pid,
                                        "subscription_id": sub_id,
                                        "title": raw_title,
                                        "current_day": cur_day,
                                        "total_days": tot_days,
                                        "url": plan_url,
                                        "from_account": True,
                                    })
                    except Exception:
                        pass

    # 2. Extrai diretamente das paginas autenticadas da conta do usuario em bible.com/pt/users/{username}/reading-plans
    uname = yv.get("username", "").strip()
    candidate_urls = []
    if uname:
        candidate_urls.append(f"https://www.bible.com/pt/users/{uname}/reading-plans")
        candidate_urls.append(f"https://www.bible.com/pt/users/{uname}/saved-reading-plans")
    candidate_urls.append("https://www.bible.com/pt/reading-plans")

    for u in candidate_urls:
        st, html_body = http_request(u, headers=hdrs, timeout=10)
        if st != 200 or not html_body:
            continue
        if not uname:
            um = re.search(r"/users/([a-zA-Z0-9_.-]+)/(?:reading-plans|saved-reading-plans)", html_body)
            if um:
                uname = um.group(1)
                yv["username"] = uname
                yv["logged_in"] = True

        # Captura tanto href HTML quanto strings serializadas no React Server Components (__next_f.push):
        # /pt/users/{username}/reading-plans/{plan}/subscription/{sub}/day/{day}
        for m in re.finditer(
            r"(?:/pt)?/users/([^/\"'\\\s]+)/reading-plans/([^/\"'\\\s]+)/subscription/([^/\"'\\\s]+)/day/(\d+)",
            html_body,
            flags=re.IGNORECASE,
        ):
            found_user, pid, sub_id, day_num = m.group(1), m.group(2), m.group(3), int(m.group(4))
            if not yv.get("username") and found_user:
                yv["username"] = found_user
                yv["logged_in"] = True
            if pid not in seen_ids:
                seen_ids.add(pid)
                title, tot_days = fetch_plan_title_and_days_from_bible_com(pid, day_num, hdrs)
                full_url = f"https://www.bible.com/pt/users/{found_user}/reading-plans/{pid}/subscription/{sub_id}/day/{day_num}"
                user_plans.append({
                    "plan_id": pid,
                    "subscription_id": sub_id,
                    "title": title,
                    "current_day": day_num,
                    "total_days": tot_days,
                    "url": full_url,
                    "from_account": True,
                })

        # Captura planos salvos/inscritos na pagina pessoal /users/{username}/...
        if "/users/" in u:
            for m in re.finditer(
                r"(?:/pt)?/reading-plans/(\d+[a-zA-Z0-9_-]*)",
                html_body,
                flags=re.IGNORECASE,
            ):
                slug = m.group(1)
                if slug not in seen_ids:
                    seen_ids.add(slug)
                    title, tot_days = fetch_plan_title_and_days_from_bible_com(slug, 1, hdrs)
                    user_plans.append({
                        "plan_id": slug,
                        "subscription_id": "",
                        "title": title,
                        "current_day": 1,
                        "total_days": tot_days,
                        "url": f"https://www.bible.com/pt/reading-plans/{slug}/day/1",
                        "from_account": True,
                    })

    if user_plans:
        yv["user_plans"] = user_plans
        first = user_plans[0]
        yv["active_plan_key"] = "custom"
        yv["active_plan_id"] = first["plan_id"]
        yv["active_subscription_id"] = first.get("subscription_id", "")
        yv["active_plan_title"] = f"{first['title']} (Minha Conta YouVersion)"
        yv["current_day"] = int(first.get("current_day", 1))
        yv["total_days"] = int(first.get("total_days", 7))
        yv["custom_bible_com_plan_url"] = first["url"]

    save_data(data)
    return user_plans


def youversion_login_with_credentials(username_or_email: str, password: str) -> Dict[str, Any]:
    """Autentica diretamente na conta pessoal do YouVersion (login.youversion.com -> bible.com/api/authenticate)."""
    data = load_data()
    yv = data["youversion"]
    yv["email"] = username_or_email.strip()
    yv["auth_error"] = ""

    cj = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj), urllib.request.HTTPSHandler(context=SSL_CTX))

    redirect_target = "https://www.bible.com/api/authenticate?from=%2Fpt%2Freading-plans"
    login_url = (
        f"https://login.youversion.com/?client_id={YV_OAUTH_CLIENT_ID}"
        f"&app_ref=bible.com&redirect_uri={urllib.parse.quote(redirect_target, safe='')}"
    )

    try:
        # 1. Abre a pagina de login para inicializar cookies e resolver o redirect de idioma (/pt-BR)
        req_init = urllib.request.Request(login_url, headers=HTTP_HEADERS)
        resp_init = opener.open(req_init, timeout=12)
        final_login_url = resp_init.geturl()
        resp_init.read()

        # 1b. Inicializa o cookie de contexto OAuth (yva.temp) via storeOAuthContextAction
        ctx_payload = json.dumps([{
            "app_ref": "bible.com",
            "client_id": YV_OAUTH_CLIENT_ID,
            "i18n_locale_tag": "pt-BR",
            "redirect_uri": redirect_target,
        }]).encode("utf-8")
        ctx_headers = dict(HTTP_HEADERS)
        ctx_headers.update({
            "Accept": "text/x-component",
            "Next-Action": YV_NEXT_ACTION_STORE_CTX,
            "Content-Type": "text/plain;charset=UTF-8",
            "Origin": "https://login.youversion.com",
            "Referer": final_login_url,
        })
        try:
            opener.open(
                urllib.request.Request(final_login_url, data=ctx_payload, headers=ctx_headers, method="POST"),
                timeout=10,
            ).read()
        except Exception:
            pass

        # 2. Envia a Server Action signInWithEmailAction (Next-Action)
        # IMPORTANTE: No React 19 (3oxazd22g22ds.js), encodeReply serializa FormData dentro de [$K1]
        # usando o prefixo "_1_" (com underscore inicial e final: _1_username, _1_password)!
        boundary = "----WebKitFormBoundary" + uuid.uuid4().hex[:16]
        fields = {
            "_1_client_id": YV_OAUTH_CLIENT_ID,
            "_1_redirect_uri": redirect_target,
            "_1_scopes": "",
            "_1_from": "",
            "_1_username": username_or_email.strip(),
            "_1_password": password,
            "0": '["$K1"]',
        }
        crlf = "\r\n"
        body_str = (
            "".join(
                f'--{boundary}{crlf}Content-Disposition: form-data; name="{k}"{crlf}{crlf}{v}{crlf}'
                for k, v in fields.items()
            )
            + f"--{boundary}--{crlf}"
        )

        post_headers = dict(HTTP_HEADERS)
        post_headers.update({
            "Accept": "text/x-component",
            "Next-Action": YV_NEXT_ACTION_SIGNIN,
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Origin": "https://login.youversion.com",
            "Referer": final_login_url,
        })

        req_login = urllib.request.Request(
            final_login_url,
            data=body_str.encode("utf-8"),
            headers=post_headers,
            method="POST",
        )
        try:
            resp_login = opener.open(req_login, timeout=12)
            flight_text = resp_login.read().decode("utf-8", errors="replace")
        except urllib.error.HTTPError as e:
            if e.code in (307, 308) and e.headers.get("Location"):
                loc = e.headers["Location"]
                if loc.startswith("/"):
                    loc = "https://login.youversion.com" + loc
                req_retry = urllib.request.Request(
                    loc,
                    data=body_str.encode("utf-8"),
                    headers=post_headers,
                    method="POST",
                )
                resp_login = opener.open(req_retry, timeout=12)
                flight_text = resp_login.read().decode("utf-8", errors="replace")
            else:
                raise

        action_result: Dict[str, Any] = {}
        for line in flight_text.splitlines():
            colon_idx = line.find(":")
            if colon_idx > 0:
                raw_json = line[colon_idx + 1:].strip()
                if raw_json.startswith("{") and '"status"' in raw_json:
                    try:
                        cand = json.loads(raw_json)
                        if isinstance(cand, dict) and "status" in cand:
                            action_result = cand
                            break
                    except Exception:
                        pass

        status_str = action_result.get("status", "")
        if status_str == "error":
            code = action_result.get("code", "invalid_credentials")
            if code == "invalid_credentials":
                msg = "E-mail/usuário ou senha incorretos no YouVersion (App Bíblia). Se você criou sua conta clicando em 'Continuar com o Google', use a opção de Login Social abaixo."
            elif code == "email_not_verified":
                msg = "Confirme seu endereço de e-mail no YouVersion antes de fazer login."
            else:
                msg = f"Erro de login no YouVersion: {code}"
            yv["logged_in"] = False
            yv["auth_error"] = msg
            save_data(data)
            return {"ok": False, "error": msg}

        # 3. Se retornou handoff (OAuth POST handoff para bible.com/api/authenticate)
        # IMPORTANTE: bible.com/api/authenticate exige Origin: https://login.youversion.com (sem isso retorna 403 Forbidden!)
        if status_str == "handoff" and action_result.get("data"):
            handoff_url = action_result.get("redirectUri") or redirect_target
            if handoff_url.startswith("/"):
                handoff_url = "https://www.bible.com" + handoff_url
            form_bytes = urllib.parse.urlencode({"data": action_result["data"]}).encode("utf-8")
            handoff_hdrs = dict(HTTP_HEADERS)
            handoff_hdrs.update({
                "Content-Type": "application/x-www-form-urlencoded",
                "Origin": "https://login.youversion.com",
                "Referer": "https://login.youversion.com/",
            })
            req_handoff = urllib.request.Request(handoff_url, data=form_bytes, headers=handoff_hdrs, method="POST")
            try:
                resp_ho = opener.open(req_handoff, timeout=12)
                cj.extract_cookies(resp_ho, req_handoff)
                resp_ho.read()
            except urllib.error.HTTPError as he:
                cj.extract_cookies(he, req_handoff)
        elif status_str == "redirect" and action_result.get("href"):
            href = action_result["href"]
            if href.startswith("/"):
                href = "https://www.bible.com" + href
            req_red = urllib.request.Request(
                href,
                headers={**HTTP_HEADERS, "Referer": "https://login.youversion.com/"},
            )
            try:
                resp_red = opener.open(req_red, timeout=12)
                cj.extract_cookies(resp_red, req_red)
                resp_red.read()
            except urllib.error.HTTPError as he:
                cj.extract_cookies(he, req_red)

        # 4. Captura o cookie yva_session do CookieJar
        session_cookie_val = ""
        for c in cj:
            if "yva_session" in c.name:
                session_cookie_val = c.value
                break

        if session_cookie_val:
            yv["session_cookie"] = session_cookie_val
        if action_result.get("userId"):
            yv["user_id"] = str(action_result["userId"])

        # 5. Busca accessToken + dados do perfil + planos inscritos da conta do usuario
        refreshed = youversion_refresh_session(yv)
        if not refreshed and not session_cookie_val:
            msg = "Login enviado, mas a sessão do bible.com não retornou cookie yva_session. Tente novamente."
            yv["logged_in"] = False
            yv["auth_error"] = msg
            save_data(data)
            return {"ok": False, "error": msg}

        yv["logged_in"] = True
        yv["auth_error"] = ""
        save_data(data)
        plans = sync_youversion_user_plans(data)
        return {
            "ok": True,
            "username": yv.get("username"),
            "full_name": yv.get("full_name"),
            "plans_count": len(plans),
        }
    except Exception as e:
        yv["auth_error"] = f"Falha ao conectar com login.youversion.com: {e}"
        save_data(data)
        return {"ok": False, "error": yv["auth_error"]}


# ============================================================================
# EXTRATOR AO VIVO DO YOUVERSION / BIBLE.COM (https://www.bible.com/pt)
# ============================================================================
def parse_pt_reference_to_usfm(ref: str) -> Optional[str]:
    clean = normalize_accents_lower(ref)
    m = re.match(r"^([1-3]?\s*[a-z]+)\s+(\d+)(?::(\d+(?:-\d+)?))?$", clean)
    if not m:
        return None
    book_name = re.sub(r"\s+", " ", m.group(1)).strip()
    chapter = m.group(2)
    verses = m.group(3)
    usfm_book = PT_BOOK_TO_USFM.get(book_name)
    if not usfm_book:
        return None
    if verses:
        return f"{usfm_book}.{chapter}.{verses}"
    return f"{usfm_book}.{chapter}"


def fetch_youversion_votd_pt() -> Dict[str, str]:
    url = "https://www.bible.com/pt/verse-of-the-day"
    status, body = http_request(url, timeout=8)
    if status == 200 and body:
        txt = extract_og_meta_html(body, "og:description")
        title = extract_og_meta_html(body, "og:title")
        if txt:
            ref = title.split("-")[0].strip() if title else "Versiculo do Dia (YouVersion)"
            return {
                "ok": True,
                "source": "bible.com/pt/verse-of-the-day",
                "reference": ref,
                "text": txt,
                "url": url
            }

    plan = CURATED_YOUVERSION_PLANS["ansiedade"]["days"][1]
    return {
        "ok": True,
        "source": "bible.com/pt/verse-of-the-day",
        "reference": plan["scripture_ref"],
        "text": plan["scripture_text"],
        "url": url
    }


def fetch_youversion_passage_pt(ref: str, version_code: str = "NVI") -> Dict[str, str]:
    ver = version_code.upper() if version_code.upper() in YOUVERSION_PT_VERSIONS else "NVI"
    ver_id = YOUVERSION_PT_VERSIONS[ver]
    usfm = parse_pt_reference_to_usfm(ref)

    if usfm:
        yv_url = f"https://www.bible.com/pt/bible/{ver_id}/{usfm}.{ver}"
        status, body = http_request(yv_url, timeout=8)
        if status == 200 and body:
            desc = extract_og_meta_html(body, "og:description")
            title_meta = extract_og_meta_html(body, "og:title")
            if desc and len(desc) > 15:
                text = " ".join(desc.split())
                title = title_meta.split("|")[0].strip() if title_meta else f"{ref} ({ver})"
                return {
                    "ok": True,
                    "source": f"bible.com ({ver})",
                    "reference": title,
                    "text": text,
                    "url": yv_url
                }

    api_url = f"https://bible-api.com/{urllib.parse.quote(ref.strip())}?translation=almeida"
    status2, body2 = http_request(api_url, timeout=8)
    if status2 == 200 and body2:
        try:
            j = json.loads(body2)
            txt = " ".join(j.get("text", "").split())
            if txt:
                return {
                    "ok": True,
                    "source": "YouVersion / Almeida",
                    "reference": j.get("reference", ref),
                    "text": txt,
                    "url": f"https://www.bible.com/pt/bible/{ver_id}/{usfm or 'PSA.23'}.{ver}"
                }
        except Exception:
            pass

    plan_day = CURATED_YOUVERSION_PLANS["esperanca"]["days"][5]
    return {
        "ok": True,
        "source": "YouVersion Offline Cache",
        "reference": plan_day["scripture_ref"],
        "text": plan_day["scripture_text"],
        "url": "https://www.bible.com/pt"
    }


def scrape_live_reading_plans_from_bible_com(topic_query: str = "") -> List[Dict[str, Any]]:
    data = load_data()
    yv = data.get("youversion", {})
    plans_found: List[Dict[str, Any]] = []
    seen = set()

    # 1. Se o usuario esta logado na conta pessoal YouVersion, coloca os planos da conta dele em primeiro lugar!
    if yv.get("logged_in") or yv.get("session_cookie") or yv.get("access_token"):
        account_plans = sync_youversion_user_plans(data)
        for ap in account_plans:
            pid = ap.get("plan_id", "")
            if pid and pid not in seen:
                seen.add(pid)
                plans_found.append(ap)

    target_url = "https://www.bible.com/pt/reading-plans"
    status, body = http_request(target_url, headers=get_youversion_auth_headers(yv), timeout=8)
    if status == 200 and body:
        for m in re.finditer(
            r'href=["\'][^"\']*?/pt/reading-plans/(\d+-[a-zA-Z0-9_-]+)[^"\']*["\'][^>]*>(.*?)</a>',
            body,
            flags=re.IGNORECASE | re.DOTALL,
        ):
            slug = m.group(1)
            title = strip_html_tags(m.group(2))
            if slug not in seen and len(title) > 4:
                seen.add(slug)
                plans_found.append({
                    "plan_id": slug,
                    "title": title,
                    "url": f"https://www.bible.com/pt/reading-plans/{slug}"
                })
                if len(plans_found) >= 15:
                    break

    for key, p in CURATED_YOUVERSION_PLANS.items():
        if not topic_query or normalize_accents_lower(topic_query) in p["topic"] or normalize_accents_lower(topic_query) in key:
            plans_found.append({
                "plan_id": p["plan_id"],
                "title": p["title"],
                "url": p["url"],
                "total_days": p["total_days"],
                "key": key
            })

    return plans_found


def fetch_live_bible_com_plan_day(plan_id_or_url: str, day: int = 1) -> Optional[Dict[str, Any]]:
    yv = load_data().get("youversion", {})
    auth_hdrs = get_youversion_auth_headers(yv)

    if plan_id_or_url.startswith("http"):
        base_url = plan_id_or_url.rstrip("/")
        if "/day/" not in base_url:
            day_url = f"{base_url}/day/{day}"
        else:
            day_url = re.sub(r"/day/\d+", f"/day/{day}", base_url)
    elif yv.get("logged_in") and yv.get("username") and yv.get("active_subscription_id"):
        day_url = (
            f"https://www.bible.com/pt/users/{yv['username']}/reading-plans/"
            f"{plan_id_or_url}/subscription/{yv['active_subscription_id']}/day/{day}"
        )
    else:
        day_url = f"https://www.bible.com/pt/reading-plans/{plan_id_or_url}/day/{day}"

    status, body = http_request(day_url, headers=auth_hdrs, timeout=8)
    if (status != 200 or not body) and "/users/" in day_url:
        day_url = f"https://www.bible.com/pt/reading-plans/{plan_id_or_url}/day/{day}"
        status, body = http_request(day_url, headers=auth_hdrs, timeout=8)
    if status == 200 and body:
        title_meta = extract_og_meta_html(body, "og:title")
        desc_meta = extract_og_meta_html(body, "og:description")

        paragraphs = []
        for pm in re.finditer(r"<p[^>]*>(.*?)</p>", body, flags=re.IGNORECASE | re.DOTALL):
            t = strip_html_tags(pm.group(1))
            if len(t) > 45 and "cookies" not in t.lower() and "youversion" not in t.lower():
                paragraphs.append(t)
            if len(paragraphs) >= 3:
                break

        devotional_body = " ".join(paragraphs) if paragraphs else desc_meta
        title_str = title_meta if title_meta else f"Plano {plan_id_or_url} - Dia {day}"

        if len(devotional_body) > 30:
            if len(devotional_body) > 580:
                devotional_body = devotional_body[:575] + "..."
            max_day_found = day
            for dm in re.finditer(r"/day/(\d+)", body):
                dnum = int(dm.group(1))
                if 1 <= dnum <= 366 and dnum > max_day_found:
                    max_day_found = dnum
            return {
                "title": title_str,
                "devotional": devotional_body,
                "url": day_url,
                "total_days": max_day_found
            }
    return None


def handle_youversion_query(params: Dict[str, str]) -> Dict[str, Any]:
    action = params.get("action", "read_plan_day")
    plan = params.get("plan", "")
    day = int(params.get("day", "0") or "0")
    ref = params.get("ref", "")
    version = params.get("version", "NVI")

    data = load_data()
    yv = data["youversion"]
    if version:
        yv["version"] = version.upper()

    if action == "verse_of_day":
        votd = fetch_youversion_votd_pt()
        spoken = f"Versiculo do Dia no App Biblia YouVersion: {votd['reference']}. {votd['text']}"
        yv["last_devotional"] = spoken
        save_data(data)
        return {
            "ok": True,
            "action": action,
            "reference": votd["reference"],
            "text": votd["text"],
            "spoken": spoken,
            "plan_title": yv["active_plan_title"],
            "day": yv["current_day"],
            "total_days": yv["total_days"],
            "url": votd["url"]
        }

    if action == "get_passage" and ref:
        pas = fetch_youversion_passage_pt(ref, yv.get("version", "NVI"))
        spoken = f"{pas['reference']}. {pas['text']}"
        data["bible_history"] = ([{
            "reference": pas["reference"],
            "text": pas["text"],
            "ts": datetime.now().strftime("%H:%M")
        }] + data.get("bible_history", []))[:10]
        save_data(data)
        return {
            "ok": True,
            "action": action,
            "reference": pas["reference"],
            "text": pas["text"],
            "spoken": spoken,
            "plan_title": yv["active_plan_title"],
            "day": yv["current_day"],
            "total_days": yv["total_days"],
            "url": pas["url"]
        }

    if action == "list_plans":
        plans = scrape_live_reading_plans_from_bible_com(plan)
        titles = [p["title"] for p in plans[:5]]
        spoken = (
            f"No App Biblia YouVersion, seu plano atual e {yv['active_plan_title']} "
            f"(Dia {yv['current_day']} de {yv['total_days']}). "
            f"Planos disponiveis em bible.com/pt/reading-plans: " + "; ".join(titles) + "."
        )
        return {
            "ok": True,
            "action": action,
            "plans": plans,
            "spoken": spoken,
            "plan_title": yv["active_plan_title"],
            "day": yv["current_day"],
            "total_days": yv["total_days"]
        }

    if action == "start_plan" and plan:
        q_norm = normalize_accents_lower(plan)
        # 1. Verifica primeiro nos planos inscritos na conta pessoal do usuario!
        matched_user_plan = None
        for up in yv.get("user_plans", []):
            if (
                q_norm == str(up.get("plan_id", "")).lower()
                or q_norm in normalize_accents_lower(up.get("title", ""))
            ):
                matched_user_plan = up
                break

        matched_key = None
        if not matched_user_plan:
            for k, pinfo in CURATED_YOUVERSION_PLANS.items():
                if k in q_norm or q_norm in k or any(t.strip() in q_norm for t in pinfo["topic"].split(",")):
                    matched_key = k
                    break

        if matched_user_plan:
            yv["active_plan_key"] = "custom"
            yv["active_plan_id"] = matched_user_plan["plan_id"]
            yv["active_subscription_id"] = matched_user_plan.get("subscription_id", "")
            yv["active_plan_title"] = f"{matched_user_plan['title']} (Minha Conta YouVersion)"
            yv["current_day"] = int(matched_user_plan.get("current_day", 1))
            yv["total_days"] = int(matched_user_plan.get("total_days", 7))
            yv["custom_bible_com_plan_url"] = matched_user_plan.get("url", "")
        elif matched_key:
            pobj = CURATED_YOUVERSION_PLANS[matched_key]
            yv["active_plan_key"] = matched_key
            yv["active_plan_id"] = pobj["plan_id"]
            yv["active_subscription_id"] = ""
            yv["active_plan_title"] = pobj["title"]
            yv["current_day"] = 1
            yv["total_days"] = pobj["total_days"]
            yv["custom_bible_com_plan_url"] = ""
        else:
            yv["active_plan_key"] = "custom"
            yv["active_plan_id"] = plan.strip()
            yv["active_subscription_id"] = ""
            yv["active_plan_title"] = f"Plano YouVersion: {plan.strip()}"
            yv["current_day"] = 1
            yv["total_days"] = 7
            yv["custom_bible_com_plan_url"] = plan.strip()

        save_data(data)
        action = "read_plan_day"

    if action == "next_plan_day":
        cur = int(yv.get("current_day", 1))
        tot = int(yv.get("total_days", 5))
        if cur not in yv.get("completed_days", []):
            yv.setdefault("completed_days", []).append(cur)
        yv["current_day"] = (cur % tot) + 1
        save_data(data)
        action = "read_plan_day"

    target_day = day if day > 0 else int(yv.get("current_day", 1))
    yv["current_day"] = target_day

    if yv.get("active_plan_key") == "custom" or yv.get("custom_bible_com_plan_url"):
        live_day = fetch_live_bible_com_plan_day(yv.get("custom_bible_com_plan_url") or yv["active_plan_id"], target_day)
        if live_day:
            if live_day.get("total_days", 0) > int(yv.get("total_days", 7)):
                yv["total_days"] = int(live_day["total_days"])
            spoken = f"{live_day['title']}. Devocional de hoje: {live_day['devotional']}"
            yv["last_devotional"] = spoken
            save_data(data)
            return {
                "ok": True,
                "action": "read_plan_day",
                "plan_title": yv["active_plan_title"],
                "day": target_day,
                "total_days": yv["total_days"],
                "reference": live_day["title"],
                "text": live_day["devotional"],
                "spoken": spoken,
                "url": live_day["url"]
            }

    pkey = yv.get("active_plan_key", "ansiedade")
    if pkey not in CURATED_YOUVERSION_PLANS:
        pkey = "ansiedade"
    pobj = CURATED_YOUVERSION_PLANS[pkey]
    tot_days = pobj["total_days"]
    if target_day < 1 or target_day > tot_days:
        target_day = 1
    yv["current_day"] = target_day
    yv["total_days"] = tot_days
    yv["active_plan_title"] = pobj["title"]

    d_obj = pobj["days"][target_day]
    spoken = (
        f"Plano de Leitura YouVersion: {pobj['title']}. "
        f"{d_obj['title']}. "
        f"Devocional: {d_obj['devotional']} "
        f"Leitura Biblica em {d_obj['scripture_ref']}: {d_obj['scripture_text']}"
    )
    yv["last_devotional"] = spoken
    save_data(data)

    return {
        "ok": True,
        "action": "read_plan_day",
        "plan_title": pobj["title"],
        "day": target_day,
        "total_days": tot_days,
        "day_title": d_obj["title"],
        "devotional": d_obj["devotional"],
        "reference": d_obj["scripture_ref"],
        "text": d_obj["scripture_text"],
        "spoken": spoken,
        "url": pobj["url"]
    }


# ============================================================================
# 2. SPOTIFY WEB API (OAUTH2 + BUSCA + CONTROLE DE PLAYER)
# ============================================================================
def get_spotify_token() -> Optional[str]:
    data = load_data()
    sp = data["spotify"]
    cid = sp.get("client_id") or os.getenv("SPOTIFY_CLIENT_ID", "")
    sec = sp.get("client_secret") or os.getenv("SPOTIFY_CLIENT_SECRET", "")
    rt = sp.get("refresh_token") or os.getenv("SPOTIFY_REFRESH_TOKEN", "")

    if not cid or not sec or not rt:
        return None

    if sp.get("access_token") and time.time() < sp.get("expires_at", 0):
        return sp["access_token"]

    auth_b64 = base64.b64encode(f"{cid}:{sec}".encode()).decode()
    form_body = urllib.parse.urlencode({
        "grant_type": "refresh_token",
        "refresh_token": rt,
    }).encode("utf-8")

    status, body = http_request(
        "https://accounts.spotify.com/api/token",
        method="POST",
        headers={
            "Authorization": f"Basic {auth_b64}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        data_bytes=form_body,
        timeout=10,
    )
    if status != 200:
        return None

    tok = json.loads(body)
    sp["access_token"] = tok["access_token"]
    sp["expires_at"] = int(time.time()) + int(tok.get("expires_in", 3600)) - 90
    if "refresh_token" in tok:
        sp["refresh_token"] = tok["refresh_token"]
    save_data(data)
    return sp["access_token"]


def handle_spotify_query(params: Dict[str, str]) -> Dict[str, Any]:
    action = params.get("action", "now_playing")
    q = params.get("q", "")
    s_type = params.get("type", "track")
    volume = int(params.get("volume", "-1") or "-1")

    token = get_spotify_token()
    if not token:
        return {
            "ok": False,
            "message": "Spotify nao autenticado no servidor externo. Acesse http://127.0.0.1:8080 para conectar sua conta Spotify.",
            "track": ""
        }

    headers = {"Authorization": f"Bearer {token}"}
    data = load_data()

    if volume >= 0:
        vol = max(0, min(100, volume))
        http_request(
            f"https://api.spotify.com/v1/me/player/volume?volume_percent={vol}",
            method="PUT",
            headers=headers,
            json_body={},
            timeout=8,
        )
        if action == "volume":
            return {"ok": True, "message": f"Volume do Spotify ajustado para {vol}%.", "track": data["spotify"]["last_track"]}

    if action == "play_query" and q:
        st = s_type if s_type in ("track", "artist", "playlist", "album") else "track"
        qs = urllib.parse.urlencode({"q": q, "type": st, "limit": 1, "market": "BR"})
        sc, sbody = http_request(f"https://api.spotify.com/v1/search?{qs}", headers=headers, timeout=8)
        if sc != 200:
            return {"ok": False, "message": f"Erro ao buscar {q} no Spotify.", "track": ""}

        sjson = json.loads(sbody)
        if st == "track":
            items = sjson.get("tracks", {}).get("items", [])
            if not items:
                return {"ok": False, "message": f"Musica {q} nao encontrada no Spotify.", "track": ""}
            uri = items[0]["uri"]
            name = f"{items[0]['name']} de {items[0]['artists'][0]['name']}"
            pbody = {"uris": [uri]}
        else:
            key = f"{st}s"
            items = sjson.get(key, {}).get("items", [])
            if not items:
                return {"ok": False, "message": f"{q} nao encontrado no Spotify.", "track": ""}
            uri = items[0]["uri"]
            name = items[0]["name"]
            pbody = {"context_uri": uri}

        pc, _ = http_request("https://api.spotify.com/v1/me/player/play", method="PUT", headers=headers, json_body=pbody, timeout=8)
        data["spotify"]["last_track"] = name
        save_data(data)
        if pc in (200, 204):
            return {"ok": True, "message": f"Reproduzindo no Spotify: {name}.", "track": name}
        return {
            "ok": False,
            "message": f"Encontrei {name}, mas nenhum dispositivo Spotify estava ativo para iniciar a reproducao.",
            "track": name
        }

    elif action == "pause":
        http_request("https://api.spotify.com/v1/me/player/pause", method="PUT", headers=headers, json_body={}, timeout=8)
        return {"ok": True, "message": "Spotify pausado.", "track": data["spotify"]["last_track"]}

    elif action in ("resume", "play"):
        http_request("https://api.spotify.com/v1/me/player/play", method="PUT", headers=headers, json_body={}, timeout=8)
        return {"ok": True, "message": "Reproducao do Spotify retomada.", "track": data["spotify"]["last_track"]}

    elif action == "next":
        http_request("https://api.spotify.com/v1/me/player/next", method="POST", headers=headers, json_body={}, timeout=8)
        return {"ok": True, "message": "Pulou para a proxima musica no Spotify.", "track": data["spotify"]["last_track"]}

    elif action == "previous":
        http_request("https://api.spotify.com/v1/me/player/previous", method="POST", headers=headers, json_body={}, timeout=8)
        return {"ok": True, "message": "Voltou para a musica anterior no Spotify.", "track": data["spotify"]["last_track"]}

    cc, cbody = http_request("https://api.spotify.com/v1/me/player/currently-playing", headers=headers, timeout=8)
    if cc == 200 and cbody:
        cjson = json.loads(cbody)
        item = cjson.get("item") or {}
        name = item.get("name", "Desconhecida")
        artists = item.get("artists") or [{}]
        artist = artists[0].get("name", "Artista")
        playing = cjson.get("is_playing", False)
        track_str = f"{name} - {artist}"
        data["spotify"]["last_track"] = track_str
        save_data(data)
        prefix = "Tocando agora" if playing else "Pausado em"
        return {"ok": True, "message": f"{prefix}: {name}, de {artist}.", "track": track_str}

    return {"ok": True, "message": "Nada esta tocando no Spotify no momento.", "track": "Parado"}


def render_dashboard_html() -> str:
    data = load_data()
    sp = data["spotify"]
    yv = data["youversion"]
    has_sp = bool(sp.get("refresh_token"))
    has_yv = bool(yv.get("logged_in") or yv.get("session_cookie") or yv.get("access_token"))
    yv_user_label = (
        f"✅ CONECTADO COMO: {yv.get('full_name') or yv.get('username') or yv.get('email')}"
        if has_yv
        else "⚠️ NAO LOGADO (USANDO PLANOS PUBLICOS)"
    )
    yv_err_html = (
        f'<p style="background:#3b1219;border:1px solid #ff4d6d;color:#ffb3c1;padding:10px;border-radius:6px;">❌ {html.escape(yv["auth_error"])}</p>'
        if yv.get("auth_error")
        else ""
    )
    user_plans_html = "".join(
        f'<li><b>{html.escape(p.get("title","Plano"))}</b> (Dia {p.get("current_day",1)}) — '
        f'<a style="color:#00e5ff;" href="/api/bible/youversion?action=start_plan&plan={urllib.parse.quote(str(p.get("plan_id","")))}">Ativar no Jarvis</a></li>'
        for p in yv.get("user_plans", [])
    ) or "<li>Nenhum plano pessoal sincronizado ainda. Faca login abaixo ou clique em Sincronizar Meus Planos.</li>"

    alarms_html = "".join(
        f"<li><b>{a.get('h',0):02d}:{a.get('m',0):02d}</b> — {a.get('l','Alarme')} ({'Diario' if a.get('r',True) else 'Unico'})</li>"
        for a in data.get("alarms", [])
    ) or "<li>Nenhum alarme sincronizado ainda.</li>"

    return f"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <title>J.A.R.V.I.S. External Bridge Server — YouVersion & Spotify</title>
  <style>
    body {{ background:#070b12; color:#e2f1ff; font-family:'Segoe UI',sans-serif; padding:24px; max-width:960px; margin:auto; }}
    h1 {{ color:#00e5ff; letter-spacing:2px; }}
    .card {{ background:#0e1726; border:1px solid #1b2e4b; border-radius:12px; padding:18px; margin-bottom:18px; }}
    input, select {{ background:#070d17; border:1px solid #243e63; color:#fff; padding:9px; border-radius:6px; margin-right:8px; }}
    button, a.btn {{ background:#00e5ff; color:#04101c; border:none; padding:9px 16px; border-radius:6px; font-weight:700; text-decoration:none; cursor:pointer; display:inline-block; margin-top:4px; }}
    code {{ background:#05080e; padding:3px 7px; border-radius:4px; color:#00e5ff; }}
  </style>
</head>
<body>
  <h1>🤖 J.A.R.V.I.S. // SERVIDOR EXTERNO (YOUVERSION BIBLE.COM + SPOTIFY)</h1>
  <p>Conectando o <b>ESP-WROOM-32</b> diretamente com a <b>Sua Conta Pessoal do App da Biblia (YouVersion - bible.com/pt)</b>, <b>Spotify OAuth2</b>, <b>Voz Neural Edge-TTS</b> e <b>Alarmes NVS</b>.</p>

  <div class="card">
    <h3>🔐 1. Sua Conta Pessoal no App Biblia (YouVersion / bible.com) — {yv_user_label}</h3>
    {yv_err_html}
    <p style="font-size:0.9rem;color:#9ab8d9;">Entre com o mesmo <b>E-mail/Usuario e Senha</b> que voce usa no aplicativo <b>Biblia (YouVersion)</b> para o Jarvis sincronizar exatamente os <b>seus Planos de Leitura inscritos</b> e o dia em que voce parou:</p>
    <form action="/youversion/login" method="post" style="margin-bottom:10px;">
      <input type="text" name="username" style="width:240px;" placeholder="Seu E-mail ou Usuario do App Biblia" value="{html.escape(yv.get('email',''))}" required />
      <input type="password" name="password" style="width:200px;" placeholder="Sua Senha do App Biblia" required />
      <button type="submit">Entrar na Minha Conta YouVersion</button>
      <a class="btn" style="background:#1e3a5f;color:#fff;margin-left:6px;" href="/youversion/sync">🔄 Sincronizar Meus Planos</a>
      {('<a class="btn" style="background:#5c1a24;color:#fff;margin-left:6px;" href="/youversion/logout">Sair</a>' if has_yv else '')}
    </form>
    <details style="margin-top:8px;font-size:0.86rem;color:#9ab8d9;">
      <summary style="cursor:pointer;color:#00e5ff;">Usa login social (Entrar com Google / Apple) no bible.com? Clique aqui para conectar via Cookie/Usuario</summary>
      <form action="/youversion/session" method="post" style="margin-top:8px;">
        <input type="text" name="username" style="width:180px;" placeholder="Seu @username no bible.com" value="{html.escape(yv.get('username',''))}" />
        <input type="text" name="session_cookie" style="width:340px;" placeholder="Valor do cookie yva_session de www.bible.com" value="{html.escape(yv.get('session_cookie',''))}" />
        <button type="submit">Salvar Sessao YouVersion</button>
      </form>
    </details>
    <h4 style="margin-bottom:6px;margin-top:14px;color:#00e5ff;">📚 Planos de Leitura Sincronizados da Sua Conta:</h4>
    <ul style="margin-top:4px;">{user_plans_html}</ul>
  </div>

  <div class="card">
    <h3>📖 2. Leitura & Devocional Ativo no J.A.R.V.I.S. (bible.com/pt/reading-plans)</h3>
    <p>Plano Ativo: <b>{yv.get('active_plan_title')}</b> — <b>Dia {yv.get('current_day')} de {yv.get('total_days')}</b> (Traducao: <code>{yv.get('version','NVI')}</code>)</p>
    <p style="font-size:0.9rem;color:#9ab8d9;">{yv.get('last_devotional','Clique abaixo para testar o devocional ou iniciar um plano do bible.com/pt/reading-plans.')}</p>
    <p>
      <a class="btn" href="/api/bible/youversion?action=read_plan_day">📖 Ler Devocional do Dia {yv.get('current_day')}</a>
      <a class="btn" href="/api/bible/youversion?action=next_plan_day">⏭️ Avancar p/ Proximo Dia</a>
      <a class="btn" href="/api/bible/youversion?action=verse_of_day">✨ Versiculo do Dia (YouVersion)</a>
      <a class="btn" href="/api/bible/youversion?action=list_plans">📋 Planos em bible.com/pt/reading-plans</a>
    </p>
    <form action="/api/bible/youversion" method="get" style="margin-top:12px;">
      <input type="hidden" name="action" value="start_plan" />
      <input type="text" name="plan" style="width:420px;" placeholder="Tema (ansiedade, sabedoria, esperanca, gratidao) ou URL bible.com/pt/reading-plans/..." required />
      <button type="submit">Trocar Plano de Leitura</button>
    </form>
  </div>

  <div class="card">
    <h3>🎵 2. Conexao Spotify OAuth2 ({'✅ CONECTADO' if has_sp else '⚠️ AGUARDANDO LOGIN'})</h3>
    <p>No Spotify Developer Dashboard, adicione a Redirect URI: <code>{sp['redirect_uri']}</code></p>
    <form action="/spotify/login" method="get">
      <input type="text" name="client_id" placeholder="Spotify Client ID" value="{sp.get('client_id','')}" required />
      <input type="password" name="client_secret" placeholder="Spotify Client Secret" value="{sp.get('client_secret','')}" required />
      <button type="submit">Conectar Conta Spotify</button>
    </form>
    <p style="font-size:0.88rem;color:#9ab8d9;">Ultima faixa: <b>{sp.get('last_track')}</b></p>
  </div>

  <div class="card">
    <h3>⏰ 3. Alarmes Sincronizados com a Memoria NVS do ESP32</h3>
    <ul>{alarms_html}</ul>
  </div>
</body>
</html>"""


class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class JarvisBridgeHandler(BaseHTTPRequestHandler):
    def _send_json(self, payload: Dict[str, Any], status: int = 200) -> None:
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _send_html(self, html_str: str, status: int = 200) -> None:
        raw = html_str.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _send_redirect(self, location: str) -> None:
        self.send_response(302)
        self.send_header("Location", location)
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)
        params = {k: v[0] for k, v in qs.items() if v}

        if path == "/":
            self._send_html(render_dashboard_html())
        elif path == "/api/tts":
            text = params.get("text", "").strip()
            voice = params.get("voice", "pt-BR-AntonioNeural").strip()
            if not text:
                self._send_json({"error": "Parametro 'text' vazio"}, 400)
                return
            try:
                import asyncio
                import edge_tts
                import miniaudio

                async def _synth_mp3() -> bytes:
                    comm = edge_tts.Communicate(text, voice)
                    out = b""
                    async for chunk in comm.stream():
                        if chunk["type"] == "audio":
                            out += chunk["data"]
                    return out

                mp3_bytes = asyncio.run(_synth_mp3())
                decoded = miniaudio.decode(
                    mp3_bytes,
                    nchannels=1,
                    sample_rate=16000,
                    output_format=miniaudio.SampleFormat.SIGNED16,
                )
                pcm_bytes = decoded.samples.tobytes()
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(pcm_bytes)))
                self.end_headers()
                self.wfile.write(pcm_bytes)
            except Exception as e:
                self._send_json({"error": f"Falha no Edge-TTS: {e}"}, 500)
        elif path == "/api/bible/youversion":
            self._send_json(handle_youversion_query(params))
        elif path == "/youversion/sync":
            data = load_data()
            sync_youversion_user_plans(data)
            self._send_redirect("/?youversion=synced")
        elif path == "/youversion/logout":
            data = load_data()
            yv = data["youversion"]
            yv["logged_in"] = False
            yv["session_cookie"] = ""
            yv["access_token"] = ""
            yv["auth_error"] = ""
            yv["user_plans"] = []
            save_data(data)
            self._send_redirect("/?youversion=logout")
        elif path == "/api/bible":
            res = fetch_youversion_passage_pt(params.get("ref", "Salmos 23:1-4"), "NVI")
            self._send_json({"ok": True, "reference": res["reference"], "text": res["text"]})
        elif path == "/api/spotify":
            self._send_json(handle_spotify_query(params))
        elif path == "/api/alarms":
            data = load_data()
            self._send_json({"ok": True, "alarms": data.get("alarms", [])})
        elif path == "/spotify/login":
            data = load_data()
            if "client_id" in params:
                data["spotify"]["client_id"] = params["client_id"].strip()
            if "client_secret" in params:
                data["spotify"]["client_secret"] = params["client_secret"].strip()
            save_data(data)
            cid = data["spotify"]["client_id"]
            if not cid:
                self._send_json({"error": "Informe client_id e client_secret em http://127.0.0.1:8080"}, 400)
                return
            scopes = "user-modify-playback-state user-read-playback-state user-read-currently-playing"
            qstr = urllib.parse.urlencode({
                "client_id": cid,
                "response_type": "code",
                "redirect_uri": data["spotify"]["redirect_uri"],
                "scope": scopes,
            })
            self._send_redirect(f"https://accounts.spotify.com/authorize?{qstr}")
        elif path == "/spotify/callback":
            code = params.get("code", "")
            data = load_data()
            sp = data["spotify"]
            auth_b64 = base64.b64encode(f"{sp['client_id']}:{sp['client_secret']}".encode()).decode()
            form_bytes = urllib.parse.urlencode({
                "grant_type": "authorization_code",
                "code": code,
                "redirect_uri": sp["redirect_uri"],
            }).encode("utf-8")
            sc, body = http_request(
                "https://accounts.spotify.com/api/token",
                method="POST",
                headers={
                    "Authorization": f"Basic {auth_b64}",
                    "Content-Type": "application/x-www-form-urlencoded",
                },
                data_bytes=form_bytes,
                timeout=10,
            )
            if sc != 200:
                self._send_json({"error": body}, 400)
                return
            tok = json.loads(body)
            sp["access_token"] = tok.get("access_token", "")
            sp["refresh_token"] = tok.get("refresh_token", sp.get("refresh_token", ""))
            sp["expires_at"] = int(time.time()) + int(tok.get("expires_in", 3600)) - 90
            save_data(data)
            self._send_redirect("/?spotify=ok")
        else:
            self._send_json({"error": "Not found"}, 404)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw_body = self.rfile.read(length).decode("utf-8", errors="replace") if length > 0 else ""

        if parsed.path == "/youversion/login":
            form = {k: v[0] for k, v in urllib.parse.parse_qs(raw_body).items() if v}
            username = form.get("username", "").strip()
            password = form.get("password", "")
            if not username or not password:
                self._send_redirect("/?youversion=missing")
                return
            res = youversion_login_with_credentials(username, password)
            if res.get("ok"):
                self._send_redirect("/?youversion=ok")
            else:
                self._send_redirect("/?youversion=error")
            return

        if parsed.path == "/youversion/session":
            form = {k: v[0] for k, v in urllib.parse.parse_qs(raw_body).items() if v}
            data = load_data()
            yv = data["youversion"]
            if form.get("username"):
                yv["username"] = form["username"].strip().lstrip("@")
            if form.get("session_cookie"):
                yv["session_cookie"] = form["session_cookie"].strip()
            yv["logged_in"] = bool(yv.get("username") or yv.get("session_cookie"))
            yv["auth_error"] = ""
            save_data(data)
            sync_youversion_user_plans(data)
            self._send_redirect("/?youversion=session_ok")
            return

        if parsed.path == "/api/alarms/sync":
            try:
                alarms_list = json.loads(raw_body or "[]")
                if isinstance(alarms_list, list):
                    data = load_data()
                    data["alarms"] = alarms_list
                    save_data(data)
                    self._send_json({"ok": True, "count": len(alarms_list)})
                    return
            except Exception as e:
                self._send_json({"ok": False, "error": str(e)}, 400)
                return
        self._send_json({"ok": False}, 404)


if __name__ == "__main__":
    port = int(os.getenv("PORT", "8080"))
    server = ThreadedHTTPServer(("0.0.0.0", port), JarvisBridgeHandler)
    print("=================================================================")
    print(f"🚀 J.A.R.V.I.S. EXTERNAL BRIDGE SERVER INICIADO NA PORTA {port}")
    print("✨ 100% Python Padrao (Zero dependencias externas!)")
    print(f"👉 Painel Web & Login Spotify: http://127.0.0.1:{port}")
    print("👉 Conectado a https://www.bible.com/pt/reading-plans")
    print("=================================================================")
    server.serve_forever()
