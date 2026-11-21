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
import threading
from collections import deque
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
        "refresh_token": "",
        "token_expires_at": 0,
        "auth_method": "",
        "auth_error": "",
        "sync_log": [],
        "last_sync": "",
        "last_sync_ts": 0,
        "plan_meta_cache": {},
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
    "bible_history": [],
    "settings": {},
    "tts_store": {},
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


_DATA_LOCK = threading.RLock()


def save_data(data: Dict[str, Any]) -> None:
    """Gravacao atomica (arquivo temporario + os.replace): requisicoes simultaneas nunca corrompem o JSON."""
    with _DATA_LOCK:
        tmp = f"{DATA_FILE}.{threading.get_ident()}.tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        os.replace(tmp, DATA_FILE)


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
# 1. CONEXAO COM A CONTA PESSOAL DO USUARIO NO YOUVERSION (APP BIBLIA)
# ============================================================================
# Estrategia de autenticacao (na ordem):
#   A) OAuth2 "password grant" em https://auth.youversionapi.com/token — o mesmo fluxo
#      do app Android. NAO depende dos IDs de Server Action do Next.js
#      (Next-Action: 40785f..., 40ee54...), que mudam a cada deploy do login.youversion.com.
#   B) Fluxo web login.youversion.com -> bible.com/api/authenticate (fallback), agora
#      guardando TODOS os cookies do bible.com (o yva_session pode vir dividido em
#      yva_session.0 / yva_session.1 quando passa de 4 KB).
#   C) Token Bearer ou cookie colado manualmente no painel (login social Google/Apple).
#
# Sincronizacao dos planos (na ordem):
#   1) moments.youversionapi.com/3.1/items.json?kind=plan_subscription (+ plan_segment_completion
#      para descobrir o dia exato em que voce parou e plan_completion para os concluidos).
#   2) Fallback: paginas autenticadas bible.com/pt/users/{username}/reading-plans, lendo
#      tanto o HTML quanto o payload RSC (self.__next_f.push) do Next.js.
#
# Toda a sequencia fica registrada em youversion.sync_log e pode ser vista em
# http://127.0.0.1:8080/youversion/debug (sem expor senha/tokens).
# ============================================================================
from html.parser import HTMLParser
from concurrent.futures import ThreadPoolExecutor

YV_OAUTH_CLIENT_ID = "96aa43c822350c765fd177f95ebc5575"   # client web (bible.com)
YV_NEXT_ACTION_STORE_CTX = "40ee54b9829b1093c5b7df9247724e314c095a24c7"
YV_NEXT_ACTION_SIGNIN = "40785f16c4adfac0e3f6d02d797df807948c01ddc0"

YV_AUTH_TOKEN_URL = "https://auth.youversionapi.com/token"
# Credenciais publicas do app Android do YouVersion (as mesmas usadas pela biblioteca
# open-source youversion-bible-client, MIT). API nao oficial: pode mudar sem aviso.
YV_APP_CLIENT_ID = base64.b64decode("ODViNjFkOTdhNzliOTZiZTQ2NWViYWVlZTgzYjEzMTM=").decode()
YV_APP_CLIENT_SECRET = base64.b64decode("NzVjZjBlMTQxY2JmNDFlZjQxMGFkY2U1YjY1MzdhNDk=").decode()
YV_API_HEADERS = {
    "Accept": "application/json",
    "Referer": "http://android.youversionapi.com/",
    "X-YouVersion-App-Platform": "android",
    "X-YouVersion-App-Version": "17114",
    "X-YouVersion-Client": "youversion",
}
YV_MOMENTS_URL = "https://moments.youversionapi.com/3.1/items.json"
YV_USER_VIEW_URL = "https://users.youversionapi.com/3.1/view.json"
YV_PLAN_VIEW_URL = "https://reading-plans.youversionapi.com/3.1/view.json"
YV_PLAN_REFS_URL = "https://reading-plans.youversionapi.com/3.1/references.json"
YV_CHAPTER_URL = "https://bible.youversionapi.com/3.1/chapter.json"

# Nomes dos livros em portugues (com acentos, para o TTS pronunciar bem)
USFM_TO_PT_BOOK = {
    "GEN": "Gênesis", "EXO": "Êxodo", "LEV": "Levítico", "NUM": "Números", "DEU": "Deuteronômio",
    "JOS": "Josué", "JDG": "Juízes", "RUT": "Rute", "1SA": "1 Samuel", "2SA": "2 Samuel",
    "1KI": "1 Reis", "2KI": "2 Reis", "1CH": "1 Crônicas", "2CH": "2 Crônicas", "EZR": "Esdras",
    "NEH": "Neemias", "EST": "Ester", "JOB": "Jó", "PSA": "Salmos", "PRO": "Provérbios",
    "ECC": "Eclesiastes", "SNG": "Cânticos", "ISA": "Isaías", "JER": "Jeremias", "LAM": "Lamentações",
    "EZK": "Ezequiel", "DAN": "Daniel", "HOS": "Oséias", "JOL": "Joel", "AMO": "Amós",
    "OBA": "Obadias", "JON": "Jonas", "MIC": "Miquéias", "NAM": "Naum", "HAB": "Habacuque",
    "ZEP": "Sofonias", "HAG": "Ageu", "ZEC": "Zacarias", "MAL": "Malaquias",
    "MAT": "Mateus", "MRK": "Marcos", "LUK": "Lucas", "JHN": "João", "ACT": "Atos",
    "ROM": "Romanos", "1CO": "1 Coríntios", "2CO": "2 Coríntios", "GAL": "Gálatas",
    "EPH": "Efésios", "PHP": "Filipenses", "COL": "Colossenses", "1TH": "1 Tessalonicenses",
    "2TH": "2 Tessalonicenses", "1TI": "1 Timóteo", "2TI": "2 Timóteo", "TIT": "Tito",
    "PHM": "Filemom", "HEB": "Hebreus", "JAS": "Tiago", "1PE": "1 Pedro", "2PE": "2 Pedro",
    "1JN": "1 João", "2JN": "2 João", "3JN": "3 João", "JUD": "Judas", "REV": "Apocalipse",
}
USFM_RE = re.compile(r"\b([1-4]?[A-Z]{2,3})\.(\d{1,3})(?:\.(\d{1,3}))?(?:-(?:[1-4]?[A-Z]{2,3}\.\d{1,3}\.)?(\d{1,3}))?\b")


# ----------------------------------------------------------------------------
# Utilitarios genericos
# ----------------------------------------------------------------------------
def _yv_log(yv: Dict[str, Any], msg: str) -> None:
    log = yv.setdefault("sync_log", [])
    log.append(f"{datetime.now().strftime('%H:%M:%S')} {msg}")
    del log[:-60]
    print(f"[YouVersion] {msg}")


def yv_jwt_claims(token: str) -> Dict[str, Any]:
    """Decodifica (sem validar assinatura) o payload de um JWT para ler user_id / exp."""
    try:
        parts = (token or "").split(".")
        if len(parts) < 2:
            return {}
        seg = parts[1] + "=" * (-len(parts[1]) % 4)
        out = json.loads(base64.urlsafe_b64decode(seg.encode()).decode("utf-8", "replace"))
        return out if isinstance(out, dict) else {}
    except Exception:
        return {}


def _looks_like_jwt(v: Any) -> bool:
    return isinstance(v, str) and v.count(".") == 2 and len(v) > 40 and " " not in v


def _yv_unwrap(j: Any) -> Any:
    if isinstance(j, dict) and isinstance(j.get("response"), dict):
        r = j["response"]
        return r.get("data", r)
    return j


def _deep_get(obj: Any, keys: Tuple[str, ...], depth: int = 0) -> Any:
    """Procura (em largura) a primeira chave de `keys` com valor nao vazio dentro de dicts/listas."""
    if depth > 7:
        return None
    if isinstance(obj, dict):
        for k in keys:
            v = obj.get(k)
            if v not in (None, "", [], {}):
                return v
        for v in obj.values():
            if isinstance(v, (dict, list)):
                r = _deep_get(v, keys, depth + 1)
                if r is not None:
                    return r
    elif isinstance(obj, list):
        for v in obj[:80]:
            r = _deep_get(v, keys, depth + 1)
            if r is not None:
                return r
    return None


def html_to_speech_text(raw: str) -> str:
    """Converte HTML de devocional em texto corrido, preservando a separacao dos paragrafos."""
    if not raw:
        return ""
    t = re.sub(r"(?is)<(script|style)[^>]*>.*?</\1>", " ", raw)
    t = re.sub(r"(?i)<br\s*/?>|</p>|</div>|</li>|</h\d>", "\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t)
    lines = [" ".join(ln.split()) for ln in t.splitlines()]
    lines = [ln for ln in lines if ln]
    out = []
    for ln in lines:
        if out and not re.search(r"[.!?:;…]$", out[-1]):
            out[-1] += "."
        out.append(ln)
    return " ".join(out).strip()


def _yv_text(v: Any, depth: int = 0) -> str:
    """Extrai texto legivel de campos localizados do YouVersion ({"default": ..}, {"pt": ..}, l_str/l_args, html)."""
    if v is None or depth > 6:
        return ""
    if isinstance(v, bool):
        return ""
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, str):
        return html_to_speech_text(v) if "<" in v else " ".join(v.split())
    if isinstance(v, list):
        return " ".join(t for t in (_yv_text(x, depth + 1) for x in v) if t)
    if isinstance(v, dict):
        if "l_str" in v:
            s = str(v.get("l_str") or "")
            args = v.get("l_args") or {}
            if isinstance(args, dict):
                s = re.sub(r"\{\{\s*(\w+)\s*\}\}", lambda m: _yv_text(args.get(m.group(1)), depth + 1), s)
            if s.strip():
                return " ".join(s.split())
        for k in ("pt-BR", "pt_BR", "pt", "por", "default", "text", "html", "str", "value", "en"):
            if k in v:
                t = _yv_text(v[k], depth + 1)
                if t:
                    return t
    return ""


def _plan_num(pid: Any) -> str:
    m = re.match(r"\s*(\d+)", str(pid or ""))
    return m.group(1) if m else ""


def yv_api_request(
    url: str,
    yv: Optional[Dict[str, Any]] = None,
    params: Optional[Dict[str, Any]] = None,
    method: str = "GET",
    form: Optional[Dict[str, Any]] = None,
    auth: bool = True,
    timeout: int = 12,
) -> Tuple[int, Any, str]:
    """Chamada as APIs *.youversionapi.com. Retorna (status, json_ou_None, corpo_bruto)."""
    if params:
        clean = {k: v for k, v in params.items() if v not in (None, "")}
        url += ("&" if "?" in url else "?") + urllib.parse.urlencode(clean)
    hdrs = dict(YV_API_HEADERS)
    hdrs["Accept-Language"] = "pt-BR,pt;q=0.9"
    if auth and yv and yv.get("access_token"):
        hdrs["Authorization"] = f"Bearer {yv['access_token'].strip()}"
    data_bytes = None
    if form is not None:
        data_bytes = urllib.parse.urlencode(form).encode("utf-8")
        hdrs["Content-Type"] = "application/x-www-form-urlencoded"
    st, body = http_request(url, method=method, headers=hdrs, data_bytes=data_bytes, timeout=timeout)
    parsed = None
    if body and body.lstrip()[:1] in ("{", "["):
        try:
            parsed = json.loads(body)
        except Exception:
            parsed = None
    return st, parsed, body or ""


# ----------------------------------------------------------------------------
# Sessao / token
# ----------------------------------------------------------------------------
def get_youversion_auth_headers(yv: Optional[Dict[str, Any]] = None) -> Dict[str, str]:
    """Headers para paginas www.bible.com (cookie de sessao completo)."""
    if yv is None:
        yv = load_data().get("youversion", {})
    hdrs: Dict[str, str] = {}
    sc = (yv.get("session_cookie") or "").strip()
    if sc:
        hdrs["Cookie"] = sc if "=" in sc else f"yva_session={sc}"
    return hdrs


def _yv_apply_token(yv: Dict[str, Any], tok: Dict[str, Any], method: str) -> None:
    yv["access_token"] = str(tok.get("access_token") or tok.get("accessToken") or "")
    if tok.get("refresh_token"):
        yv["refresh_token"] = tok["refresh_token"]
    exp = int(time.time()) + int(tok.get("expires_in") or 3600)
    claims = yv_jwt_claims(yv["access_token"])
    if claims.get("exp"):
        try:
            exp = int(claims["exp"])
        except Exception:
            pass
    yv["token_expires_at"] = exp - 60
    uid = claims.get("user_id") or claims.get("uid") or claims.get("sub")
    if uid and str(uid).isdigit():
        yv["user_id"] = str(uid)
    yv["auth_method"] = method
    yv["logged_in"] = True
    yv["auth_error"] = ""


def yv_oauth_token_request(form: Dict[str, str]) -> Tuple[bool, Dict[str, Any], str]:
    payload = {"client_id": YV_APP_CLIENT_ID, "client_secret": YV_APP_CLIENT_SECRET}
    payload.update(form)
    st, j, raw = yv_api_request(YV_AUTH_TOKEN_URL, method="POST", form=payload, auth=False, timeout=15)
    if st == 200 and isinstance(j, dict) and j.get("access_token"):
        return True, j, ""
    err = ""
    if isinstance(j, dict):
        err = str(j.get("error_description") or j.get("error") or j.get("message") or "")
    return False, {}, f"HTTP {st}: {err or raw[:160]}"


def yv_fetch_user_profile(yv: Dict[str, Any]) -> None:
    uid = yv.get("user_id")
    if not uid:
        return
    st, j, raw = yv_api_request(YV_USER_VIEW_URL, yv, {"id": uid})
    d = _yv_unwrap(j)
    if st == 200 and isinstance(d, dict):
        uname = d.get("username") or ""
        name = d.get("name") or " ".join(x for x in (d.get("first_name"), d.get("last_name")) if x)
        if uname:
            yv["username"] = str(uname)
        if name:
            yv["full_name"] = str(name)
        _yv_log(yv, f"perfil: @{yv.get('username')} ({yv.get('full_name')})")
    else:
        _yv_log(yv, f"perfil users/view.json: HTTP {st} {raw[:100]}")


def youversion_refresh_session(yv: Dict[str, Any]) -> bool:
    """Usa os cookies do www.bible.com para obter accessToken + perfil em /api/authenticate."""
    sc = (yv.get("session_cookie") or "").strip()
    if not sc:
        return False
    # O cookie "yva" do bible.com e o proprio JWT Bearer
    m = re.search(r"(?:^|;\s*)yva=([^;]+)", sc)
    if m and _looks_like_jwt(urllib.parse.unquote(m.group(1))) and not yv.get("access_token"):
        _yv_apply_token(yv, {"access_token": urllib.parse.unquote(m.group(1))}, "cookie_yva")
    hdrs = get_youversion_auth_headers(yv)
    hdrs["Accept"] = "application/json"
    st, body = http_request(f"https://www.bible.com/api/authenticate?t={int(time.time() * 1000)}", headers=hdrs, timeout=12)
    payload: Any = None
    try:
        payload = json.loads(body) if body and body.lstrip()[:1] == "{" else None
    except Exception:
        payload = None
    if st != 200 or not isinstance(payload, dict):
        _yv_log(yv, f"bible.com/api/authenticate: HTTP {st} (nao-JSON: {str(body)[:80]!r})")
        return bool(yv.get("access_token"))
    tok = _deep_get(payload, ("accessToken", "access_token", "token"))
    if isinstance(tok, str) and len(tok) > 20:
        _yv_apply_token(yv, {"access_token": tok, "refresh_token": _deep_get(payload, ("refreshToken", "refresh_token"))}, "bible.com_session")
    user = payload.get("user") if isinstance(payload.get("user"), dict) else (_deep_get(payload, ("user",)) or {})
    if isinstance(user, dict) and user:
        if user.get("id"):
            yv["user_id"] = str(user["id"])
        if user.get("username"):
            yv["username"] = str(user["username"])
        yv["full_name"] = str(user.get("name") or user.get("first_name") or yv.get("username") or "")
        yv["logged_in"] = True
    _yv_log(yv, f"bible.com/api/authenticate OK (token={'sim' if yv.get('access_token') else 'nao'}, user_id={yv.get('user_id') or '?'})")
    return bool(yv.get("access_token"))


def yv_ensure_token(yv: Dict[str, Any]) -> bool:
    """Garante um access_token valido (renova via refresh_token ou cookie do bible.com)."""
    tok = (yv.get("access_token") or "").strip()
    if tok:
        exp = int(yv.get("token_expires_at") or 0)
        if not exp:
            exp = int(yv_jwt_claims(tok).get("exp") or 0)
        if not yv.get("user_id"):
            c = yv_jwt_claims(tok)
            uid = c.get("user_id") or c.get("uid") or c.get("sub")
            if uid and str(uid).isdigit():
                yv["user_id"] = str(uid)
        if not exp or exp > time.time() + 30:
            return True
        _yv_log(yv, "access_token expirado, renovando...")
    if yv.get("refresh_token"):
        ok, tokd, err = yv_oauth_token_request({"grant_type": "refresh_token", "refresh_token": yv["refresh_token"]})
        if ok:
            _yv_apply_token(yv, tokd, yv.get("auth_method") or "oauth_password")
            _yv_log(yv, "token renovado via refresh_token")
            return True
        _yv_log(yv, f"refresh_token falhou: {err}")
    if yv.get("session_cookie"):
        yv["access_token"] = ""
        if youversion_refresh_session(yv):
            return True
    return False


# ----------------------------------------------------------------------------
# Login
# ----------------------------------------------------------------------------
class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):  # noqa: D401
        return None


def _yv_web_login(username_or_email: str, password: str, yv: Dict[str, Any]) -> Tuple[bool, str]:
    """Fluxo web: login.youversion.com (React Server Actions) -> handoff -> bible.com/api/authenticate."""
    cj = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj), urllib.request.HTTPSHandler(context=SSL_CTX))
    opener_noredir = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj), urllib.request.HTTPSHandler(context=SSL_CTX), _NoRedirect())

    redirect_target = "https://www.bible.com/api/authenticate?from=%2Fpt%2Freading-plans"
    login_url = (
        f"https://login.youversion.com/?client_id={YV_OAUTH_CLIENT_ID}"
        f"&app_ref=bible.com&redirect_uri={urllib.parse.quote(redirect_target, safe='')}"
    )
    resp_init = opener.open(urllib.request.Request(login_url, headers=HTTP_HEADERS), timeout=15)
    final_login_url = resp_init.geturl()   # resolve o 307 de idioma (/pt-BR?...)
    init_html = resp_init.read().decode("utf-8", errors="replace")

    ctx_payload = json.dumps([{
        "app_ref": "bible.com", "client_id": YV_OAUTH_CLIENT_ID,
        "i18n_locale_tag": "pt-BR", "redirect_uri": redirect_target,
    }]).encode("utf-8")
    ctx_headers = dict(HTTP_HEADERS)
    ctx_headers.update({
        "Accept": "text/x-component", "Next-Action": YV_NEXT_ACTION_STORE_CTX,
        "Content-Type": "text/plain;charset=UTF-8", "Origin": "https://login.youversion.com",
        "Referer": final_login_url,
    })
    try:
        opener.open(urllib.request.Request(final_login_url, data=ctx_payload, headers=ctx_headers, method="POST"), timeout=12).read()
    except Exception as e:
        _yv_log(yv, f"web: storeOAuthContextAction falhou ({e})")

    boundary = "----WebKitFormBoundary" + uuid.uuid4().hex[:16]
    fields = {
        "_1_client_id": YV_OAUTH_CLIENT_ID, "_1_redirect_uri": redirect_target,
        "_1_scopes": "", "_1_from": "",
        "_1_username": username_or_email.strip(), "_1_password": password,
        "0": '["$K1"]',
    }
    crlf = "\r\n"
    body_str = "".join(
        f'--{boundary}{crlf}Content-Disposition: form-data; name="{k}"{crlf}{crlf}{v}{crlf}' for k, v in fields.items()
    ) + f"--{boundary}--{crlf}"
    post_headers = dict(HTTP_HEADERS)
    post_headers.update({
        "Accept": "text/x-component", "Next-Action": YV_NEXT_ACTION_SIGNIN,
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Origin": "https://login.youversion.com", "Referer": final_login_url,
    })
    try:
        resp_login = opener.open(urllib.request.Request(final_login_url, data=body_str.encode(), headers=post_headers, method="POST"), timeout=15)
        flight_text = resp_login.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        if e.code in (307, 308) and e.headers.get("Location"):
            loc = urllib.parse.urljoin(final_login_url, e.headers["Location"])
            resp_login = opener.open(urllib.request.Request(loc, data=body_str.encode(), headers=post_headers, method="POST"), timeout=15)
            flight_text = resp_login.read().decode("utf-8", errors="replace")
        elif e.code == 404:
            return False, "login.youversion.com nao reconheceu o Next-Action (o site foi atualizado; IDs de Server Action mudaram)"
        else:
            raise

    action_result: Dict[str, Any] = {}
    for line in flight_text.splitlines():
        idx = line.find(":")
        if idx > 0:
            raw_json = line[idx + 1:].strip()
            if raw_json.startswith("{") and '"status"' in raw_json:
                try:
                    cand = json.loads(raw_json)
                    if isinstance(cand, dict) and "status" in cand:
                        action_result = cand
                        break
                except Exception:
                    pass
    status_str = str(action_result.get("status", ""))
    _yv_log(yv, f"web: signInWithEmailAction status={status_str or '?'}")
    if not action_result:
        hint = "Server Action desatualizada" if "Next-Action" not in init_html else "resposta inesperada"
        return False, f"login.youversion.com nao retornou status ({hint}): {flight_text[:120]!r}"
    if status_str == "error":
        code = action_result.get("code", "invalid_credentials")
        if code == "invalid_credentials":
            return False, "E-mail/usuario ou senha incorretos no YouVersion."
        if code == "email_not_verified":
            return False, "Confirme seu e-mail no YouVersion antes de fazer login."
        return False, f"Erro de login no YouVersion: {code}"

    def _open_capture(req: urllib.request.Request) -> None:
        try:
            r = opener_noredir.open(req, timeout=15)
            r.read()
        except urllib.error.HTTPError as he:   # 302/303/307 caem aqui (sem seguir o redirect)
            cj.extract_cookies(he, req)
            loc = he.headers.get("Location")
            if he.code in (301, 302, 303, 307, 308) and loc:
                nxt = urllib.parse.urljoin(req.full_url, loc)
                if "bible.com" in nxt:
                    try:
                        opener.open(urllib.request.Request(nxt, headers=HTTP_HEADERS), timeout=15).read()
                    except Exception:
                        pass
            elif he.code >= 400:
                _yv_log(yv, f"web: handoff HTTP {he.code}")

    if status_str == "handoff" and action_result.get("data"):
        handoff_url = urllib.parse.urljoin("https://www.bible.com", action_result.get("redirectUri") or redirect_target)
        form_bytes = urllib.parse.urlencode({"data": action_result["data"]}).encode("utf-8")
        hh = dict(HTTP_HEADERS)
        hh.update({"Content-Type": "application/x-www-form-urlencoded", "Origin": "https://login.youversion.com", "Referer": "https://login.youversion.com/"})
        _open_capture(urllib.request.Request(handoff_url, data=form_bytes, headers=hh, method="POST"))
    elif status_str == "redirect" and action_result.get("href"):
        href = urllib.parse.urljoin("https://www.bible.com", action_result["href"])
        _open_capture(urllib.request.Request(href, headers={**HTTP_HEADERS, "Referer": "https://login.youversion.com/"}))

    # Guarda TODOS os cookies do bible.com (yva_session, yva_session.0/.1, yva, etc.)
    bible_cookies = [(c.name, c.value) for c in cj if "bible.com" in (c.domain or "")]
    names = [n for n, _ in bible_cookies]
    _yv_log(yv, f"web: cookies bible.com recebidos: {names or 'nenhum'}")
    if action_result.get("userId"):
        yv["user_id"] = str(action_result["userId"])
    if not any(n.startswith("yva") for n in names):
        return False, "Login aceito, mas bible.com nao devolveu cookie de sessao (yva_session)."
    yv["session_cookie"] = "; ".join(f"{n}={v}" for n, v in bible_cookies)
    yv["access_token"] = ""
    if youversion_refresh_session(yv):
        yv["auth_method"] = "web_login"
        return True, ""
    return False, "Sessao do bible.com criada, mas /api/authenticate nao devolveu accessToken."


def youversion_login_with_credentials(username_or_email: str, password: str) -> Dict[str, Any]:
    """Autentica na conta pessoal do YouVersion. Tenta OAuth2 (API do app) e depois o fluxo web."""
    data = load_data()
    yv = data["youversion"]
    yv["email"] = username_or_email.strip()
    yv["auth_error"] = ""
    yv["sync_log"] = []
    errors = []

    ok, tok, err = yv_oauth_token_request({"grant_type": "password", "username": username_or_email.strip(), "password": password})
    if ok:
        _yv_apply_token(yv, tok, "oauth_password")
        _yv_log(yv, f"OAuth2 OK (user_id={yv.get('user_id') or '?'})")
        yv_fetch_user_profile(yv)
    else:
        errors.append(f"OAuth2 auth.youversionapi.com -> {err}")
        _yv_log(yv, errors[-1])
        try:
            wok, werr = _yv_web_login(username_or_email, password, yv)
            if not wok:
                errors.append(f"Login web -> {werr}")
                _yv_log(yv, errors[-1])
        except Exception as e:
            wok = False
            errors.append(f"Login web -> {e}")
            _yv_log(yv, errors[-1])
        if not wok:
            yv["logged_in"] = False
            yv["auth_error"] = " | ".join(errors)
            save_data(data)
            return {"ok": False, "error": yv["auth_error"]}

    save_data(data)
    plans = sync_youversion_user_plans(data)
    return {"ok": True, "username": yv.get("username"), "full_name": yv.get("full_name"), "plans_count": len(plans)}


# ----------------------------------------------------------------------------
# Sincronizacao dos planos da conta
# ----------------------------------------------------------------------------
def yv_fetch_moments(yv: Dict[str, Any], kind: str, max_pages: int = 8) -> Tuple[List[Dict[str, Any]], str]:
    out: List[Dict[str, Any]] = []
    err = ""
    for page in range(1, max_pages + 1):
        st, j, raw = yv_api_request(YV_MOMENTS_URL, yv, {"kind": kind, "user_id": yv.get("user_id"), "page": page})
        if st != 200 or not isinstance(j, dict):
            err = f"HTTP {st}: {raw[:140]}"
            break
        d = _yv_unwrap(j)
        items = d.get("moments") if isinstance(d, dict) else (d if isinstance(d, list) else None)
        if not items:
            break
        out.extend(x for x in items if isinstance(x, dict))
        if isinstance(d, dict) and "next_page" in d and not d.get("next_page"):
            break
    return out, err


def _moment_info(m: Dict[str, Any]) -> Dict[str, Any]:
    obj = m.get("object") if isinstance(m.get("object"), dict) else m
    pid = _deep_get(obj, ("plan_id", "reading_plan_id")) or _deep_get(m, ("plan_id", "reading_plan_id"))
    sub = _deep_get(obj, ("subscription_id",)) or ""
    day = _deep_get(obj, ("day", "day_number"))
    created = _deep_get(obj, ("created_dt", "created_at", "updated_dt")) or _deep_get(m, ("created_dt", "created_at")) or ""
    total = _deep_get(obj, ("total_days", "days_count", "length"))
    title = _yv_text(_deep_get(obj, ("plan_title", "plan_name"))) or _yv_text(_deep_get(obj, ("title", "name")))
    try:
        day = int(day) if day is not None else 0
    except Exception:
        day = 0
    try:
        total = int(total) if total is not None else 0
    except Exception:
        total = 0
    return {"plan_id": _plan_num(pid), "subscription_id": str(sub), "day": day, "created": str(created), "total": total, "title": title}


def yv_plan_meta(yv: Dict[str, Any], pid: str) -> Dict[str, Any]:
    """Titulo e total de dias do plano (cache em youversion.plan_meta_cache)."""
    cache = yv.setdefault("plan_meta_cache", {})
    num = _plan_num(pid) or str(pid)
    hit = cache.get(num)
    if isinstance(hit, dict) and hit.get("title") and hit.get("total_days"):
        return hit
    meta = {"title": "", "total_days": 0, "slug": ""}
    if _plan_num(pid):
        st, j, raw = yv_api_request(YV_PLAN_VIEW_URL, yv, {"id": num, "language_tag": "pt"})
        d = _yv_unwrap(j)
        if st == 200 and isinstance(d, dict):
            meta["title"] = _yv_text(d.get("name") or d.get("title"))
            try:
                meta["total_days"] = int(d.get("total_days") or d.get("days") or 0)
            except Exception:
                meta["total_days"] = 0
            meta["slug"] = str(d.get("slug") or "")
        else:
            _yv_log(yv, f"plano {num} view.json: HTTP {st}")
    if not meta["title"] or not meta["total_days"]:
        t, tot = fetch_plan_title_and_days_from_bible_com(pid, 1, get_youversion_auth_headers(yv))
        meta["title"] = meta["title"] or t
        meta["total_days"] = meta["total_days"] or tot
    cache[num] = meta
    return meta


def decode_next_flight(html_body: str) -> str:
    """Junta o payload React Server Components (self.__next_f.push([1,"..."])) do Next.js."""
    chunks = []
    for m in re.finditer(r'self\.__next_f\.push\(\[\d+,\s*"((?:\\.|[^"\\])*)"\]\)', html_body or ""):
        try:
            chunks.append(json.loads('"' + m.group(1) + '"'))
        except Exception:
            pass
    return "".join(chunks)


def fetch_plan_title_and_days_from_bible_com(pid: str, day_num: int = 1, hdrs: Optional[Dict[str, str]] = None) -> Tuple[str, int]:
    url = f"https://www.bible.com/pt/reading-plans/{pid}/day/{max(1, day_num)}"
    st, body = http_request(url, headers=hdrs, timeout=10)
    title = f"Plano YouVersion #{pid}"
    max_day = 0
    if st == 200 and body:
        og_t = extract_og_meta_html(body, "og:title")
        if og_t:
            title = og_t.split("|")[0].strip()
        blob = body + decode_next_flight(body)
        for pat in (r'"total_days"\s*:\s*(\d+)', r'"totalDays"\s*:\s*(\d+)'):
            m = re.search(pat, blob)
            if m:
                max_day = int(m.group(1))
                break
        if not max_day:
            for dm in re.finditer(r"/day/(\d+)", blob):
                max_day = max(max_day, int(dm.group(1)))
    return title, max(max_day, day_num, 1)


def _yv_scrape_web_plans(yv: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Fallback: le bible.com/pt/users/{username}/reading-plans (HTML + RSC)."""
    hdrs = get_youversion_auth_headers(yv)
    if not hdrs:
        return []
    uname = (yv.get("username") or "").strip().lstrip("@")
    urls = []
    if uname:
        urls += [f"https://www.bible.com/pt/users/{uname}/reading-plans", f"https://www.bible.com/pt/users/{uname}/saved-reading-plans"]
    else:
        urls.append("https://www.bible.com/pt/reading-plans")
    found: Dict[str, Dict[str, Any]] = {}
    for u in urls:
        st, body = http_request(u, headers=hdrs, timeout=12)
        _yv_log(yv, f"web: GET {u.replace('https://www.bible.com', '')} -> HTTP {st}, {len(body or '')} bytes")
        if st != 200 or not body:
            continue
        blob = html.unescape(body) + "\n" + decode_next_flight(body)
        blob = blob.replace("\\/", "/")
        if not uname:
            um = re.search(r"/users/([A-Za-z0-9_.-]+)/(?:reading-plans|saved-reading-plans)", blob)
            if um:
                uname = um.group(1)
                yv["username"] = uname
        saved = "saved-reading-plans" in u
        for m in re.finditer(r"/users/([^/\"'\\\s]+)/reading-plans/(\d+)[^/\"'\\\s]*/subscription/([^/\"'\\\s?]+)/day/(\d+)", blob):
            pid, sub, day = m.group(2), m.group(3), int(m.group(4))
            if pid not in found:
                found[pid] = {"plan_id": pid, "subscription_id": sub, "day_seen": day, "saved": saved}
        # JSON embutido no RSC: {"plan_id":123,...,"id":"sub"} ou "planId"
        for m in re.finditer(r'"(?:plan_id|planId)"\s*:\s*"?(\d+)', blob):
            pid = m.group(1)
            if pid not in found:
                found[pid] = {"plan_id": pid, "subscription_id": "", "day_seen": 0, "saved": saved}
        if uname and "/users/" in u:
            for m in re.finditer(r"/reading-plans/(\d+)(?:-[A-Za-z0-9_-]+)?", blob):
                pid = m.group(1)
                if pid not in found:
                    found[pid] = {"plan_id": pid, "subscription_id": "", "day_seen": 0, "saved": saved}
    out = []
    for p in found.values():
        out.append({
            "plan_id": p["plan_id"], "subscription_id": p["subscription_id"], "title": "",
            "max_done": max(0, p["day_seen"] - 1), "created": "", "completed": False,
            "saved_only": p["saved"] and not p["subscription_id"], "source": "web",
        })
    return out


def _yv_set_active(yv: Dict[str, Any], p: Dict[str, Any], day: Optional[int] = None) -> None:
    yv["active_plan_key"] = "account"
    yv["active_plan_id"] = p["plan_id"]
    yv["active_subscription_id"] = p.get("subscription_id", "")
    yv["active_plan_title"] = p.get("title") or f"Plano #{p['plan_id']}"
    yv["total_days"] = int(p.get("total_days") or 0) or 1
    yv["current_day"] = max(1, min(int(day or p.get("current_day") or 1), yv["total_days"]))
    yv["custom_bible_com_plan_url"] = ""


def sync_youversion_user_plans(data: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Sincroniza os Planos de Leitura em andamento da conta pessoal do YouVersion."""
    yv = data["youversion"]
    if not any(yv.get(k) for k in ("access_token", "refresh_token", "session_cookie")):
        _yv_log(yv, "sync ignorado: nenhuma credencial salva")
        save_data(data)
        return yv.get("user_plans", [])

    plans: Dict[str, Dict[str, Any]] = {}
    if yv_ensure_token(yv):
        if not yv.get("username") or not yv.get("full_name"):
            yv_fetch_user_profile(yv)
        subs, e1 = yv_fetch_moments(yv, "plan_subscription")
        _yv_log(yv, f"moments plan_subscription: {len(subs)} itens {e1}")
        for m in subs:
            i = _moment_info(m)
            pid = i["plan_id"]
            if not pid:
                continue
            prev = plans.get(pid)
            if prev is None or (i["created"] and i["created"] > prev["created"]):
                plans[pid] = {
                    "plan_id": pid, "subscription_id": i["subscription_id"], "title": i["title"],
                    "total_days": i["total"], "max_done": 0, "created": i["created"],
                    "completed": False, "saved_only": False, "source": "api",
                }
        if plans:
            segs, e2 = yv_fetch_moments(yv, "plan_segment_completion", max_pages=15)
            _yv_log(yv, f"moments plan_segment_completion: {len(segs)} itens {e2}")
            for m in segs:
                i = _moment_info(m)
                p = plans.get(i["plan_id"])
                if p and i["day"] and (not p["created"] or not i["created"] or i["created"] >= p["created"]):
                    p["max_done"] = max(p["max_done"], i["day"])
            comps, e3 = yv_fetch_moments(yv, "plan_completion")
            _yv_log(yv, f"moments plan_completion: {len(comps)} itens {e3}")
            for m in comps:
                i = _moment_info(m)
                p = plans.get(i["plan_id"])
                if p and (not p["created"] or not i["created"] or i["created"] >= p["created"]):
                    p["completed"] = True
    else:
        _yv_log(yv, "sem access_token valido (faca login novamente)")

    if not plans:
        for p in _yv_scrape_web_plans(yv):
            plans.setdefault(p["plan_id"], p)
        _yv_log(yv, f"fallback web: {len(plans)} planos encontrados")

    plist: List[Dict[str, Any]] = []
    for p in plans.values():
        meta = yv_plan_meta(yv, p["plan_id"])
        p["title"] = meta.get("title") or p.get("title") or f"Plano #{p['plan_id']}"
        p["total_days"] = int(meta.get("total_days") or p.get("total_days") or 0) or max(1, p["max_done"] + 1)
        p["current_day"] = max(1, min(p["max_done"] + 1, p["total_days"]))
        p["slug"] = meta.get("slug", "")
        p["url"] = f"https://www.bible.com/pt/reading-plans/{p['plan_id']}/day/{p['current_day']}"
        if p.get("subscription_id") and yv.get("username"):
            p["url"] = (f"https://www.bible.com/pt/users/{yv['username']}/reading-plans/{p['plan_id']}"
                        f"/subscription/{p['subscription_id']}/day/{p['current_day']}")
        p["from_account"] = True
        plist.append(p)
    # Em andamento primeiro (mais recentes antes), depois salvos e concluidos
    plist.sort(key=lambda x: x.get("created", ""), reverse=True)
    plist.sort(key=lambda x: (x.get("completed", False), x.get("saved_only", False)))

    yv["user_plans"] = plist
    yv["last_sync"] = datetime.now().strftime("%d/%m/%Y %H:%M")
    yv["last_sync_ts"] = time.time()
    _yv_log(yv, f"sync concluido: {len(plist)} planos")

    if plist:
        current = next((p for p in plist if p["plan_id"] == _plan_num(yv.get("active_plan_id"))), None)
        if current and yv.get("active_plan_key") in ("account", "custom"):
            # Mantem o plano que voce escolheu; adota o progresso do app se ele estiver na frente
            _yv_set_active(yv, current, max(int(yv.get("current_day") or 1), current["current_day"]))
        elif yv.get("active_plan_key") == "custom" and yv.get("custom_bible_com_plan_url"):
            pass  # voce colou uma URL de plano manualmente: nao sobrescreve
        else:
            _yv_set_active(yv, next((p for p in plist if not p.get("completed")), plist[0]))
    save_data(data)
    return plist


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

    # 1. Planos ja sincronizados da conta pessoal YouVersion vem primeiro
    for ap in yv.get("user_plans", []) or []:
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


def _shape(obj: Any, depth: int = 0) -> Any:
    """Estrutura (chaves/tipos) de um JSON, sem valores sensiveis — para diagnostico."""
    if depth > 4:
        return "..."
    if isinstance(obj, dict):
        return {k: ("<redacted>" if any(x in k.lower() for x in ("token", "email", "password", "secret")) else _shape(v, depth + 1))
                for k, v in list(obj.items())[:25]}
    if isinstance(obj, list):
        return [_shape(obj[0], depth + 1), f"... {len(obj)} itens"] if obj else []
    if isinstance(obj, str):
        return obj[:60]
    return obj


def youversion_debug_report() -> Dict[str, Any]:
    """GET /youversion/debug — executa cada etapa e mostra o que o YouVersion respondeu (sem tokens)."""
    data = load_data()
    yv = data["youversion"]
    rep: Dict[str, Any] = {}
    tok = yv.get("access_token", "")
    claims = yv_jwt_claims(tok)
    rep["credenciais"] = {
        "auth_method": yv.get("auth_method"), "tem_access_token": bool(tok), "tem_refresh_token": bool(yv.get("refresh_token")),
        "cookies_bible_com": [c.split("=")[0].strip() for c in (yv.get("session_cookie") or "").split(";") if "=" in c],
        "token_exp": datetime.fromtimestamp(int(claims["exp"])).strftime("%d/%m %H:%M") if claims.get("exp") else None,
        "jwt_claims": sorted(claims.keys()), "user_id": yv.get("user_id"), "username": yv.get("username"),
    }
    rep["ensure_token"] = yv_ensure_token(yv)
    if rep["ensure_token"]:
        for kind in ("plan_subscription", "plan_segment_completion", "plan_completion"):
            st, j, raw = yv_api_request(YV_MOMENTS_URL, yv, {"kind": kind, "user_id": yv.get("user_id"), "page": 1})
            d = _yv_unwrap(j)
            items = d.get("moments") if isinstance(d, dict) else None
            rep[f"moments_{kind}"] = {"http": st, "itens_pag1": len(items or []),
                                      "exemplo": _shape(items[0]) if items else _shape(j) if j else raw[:200],
                                      "extraido": _moment_info(items[0]) if items else None}
    ups = yv.get("user_plans") or []
    pid = _plan_num(yv.get("active_plan_id")) or (ups[0]["plan_id"] if ups else "")
    if pid:
        st, j, raw = yv_api_request(YV_PLAN_VIEW_URL, yv, {"id": pid, "language_tag": "pt"})
        rep["plan_view"] = {"plan_id": pid, "http": st, "shape": _shape(_yv_unwrap(j)) if j else raw[:200]}
        dday = int(yv.get("current_day") or 1)
        st, j, raw = yv_api_request(YV_PLAN_REFS_URL, yv, {"id": pid, "day": dday, "language_tag": "pt"})
        info = yv_fetch_plan_day_api(yv, pid, dday)
        rep["plan_day_api"] = {"day": dday, "http": st, "shape": _shape(_yv_unwrap(j)) if j else raw[:200],
                               "passagens": [usfm_label(*x) for x in (info or {}).get("passages", [])],
                               "devocional_chars": len((info or {}).get("devotional", ""))}
    v = yv_fetch_chapter_verses(129, "JHN", 3)
    rep["bible_chapter_api"] = {"JHN.3_versiculos": len(v), "JHN.3.16": v.get(16, "")[:120]}
    rep["sync_log"] = yv.get("sync_log", [])[-30:]
    rep["user_plans"] = [{k: p.get(k) for k in ("plan_id", "title", "current_day", "total_days", "completed", "source")} for p in ups]
    save_data(data)
    return rep


# ----------------------------------------------------------------------------
# Leitura do dia do plano: devocional + passagens biblicas completas
# ----------------------------------------------------------------------------
class _YVChapterParser(HTMLParser):
    """Extrai {versiculo: texto} do HTML de capitulo do YouVersion, ignorando notas e rotulos."""
    SKIP = ("note", "label", "heading", "fr", "ft", "fq", "xt", "x")

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.stack: List[Tuple[List[str], Optional[int]]] = []
        self.out: Dict[int, List[str]] = {}

    def handle_starttag(self, tag, attrs):
        if tag not in ("span", "div"):
            return
        a = dict(attrs)
        classes = (a.get("class") or "").split()
        verse = None
        if "verse" in classes and a.get("data-usfm"):
            m = re.search(r"\.(\d+)(?:\+|$)", a["data-usfm"].split("+")[0])
            if m:
                verse = int(m.group(1))
        self.stack.append((classes, verse))

    def handle_endtag(self, tag):
        if tag in ("span", "div") and self.stack:
            self.stack.pop()

    def handle_data(self, data):
        verse = None
        in_content = False
        for classes, v in self.stack:
            if any(c in self.SKIP for c in classes):
                return
            if "content" in classes:
                in_content = True
            if v is not None:
                verse = v
        if verse is not None and in_content and data.strip():
            self.out.setdefault(verse, []).append(data)


_CHAPTER_CACHE: Dict[str, Dict[int, str]] = {}


def yv_fetch_chapter_verses(version_id: int, book: str, chapter: int) -> Dict[int, str]:
    key = f"{version_id}:{book}.{chapter}"
    if key in _CHAPTER_CACHE:
        return _CHAPTER_CACHE[key]
    st, j, _ = yv_api_request(YV_CHAPTER_URL, None, {"id": version_id, "reference": f"{book}.{chapter}"}, auth=False)
    content = _deep_get(_yv_unwrap(j), ("content",)) if st == 200 else None
    verses: Dict[int, str] = {}
    if isinstance(content, str) and content:
        p = _YVChapterParser()
        try:
            p.feed(content)
        except Exception:
            pass
        verses = {k: " ".join("".join(v).split()) for k, v in p.out.items() if "".join(v).strip()}
    if verses:
        if len(_CHAPTER_CACHE) > 64:
            _CHAPTER_CACHE.clear()
        _CHAPTER_CACHE[key] = verses
    return verses


def parse_usfm_list(blob: str) -> List[Tuple[str, int, int, int]]:
    """'JHN.3.16+JHN.3.17', 'PSA.23', 'ROM.8.28-30' -> [(livro, cap, v_ini, v_fim)] (v=0 => capitulo inteiro)."""
    out: List[Tuple[str, int, int, int]] = []
    for m in USFM_RE.finditer(blob or ""):
        book = m.group(1)
        if book not in USFM_TO_PT_BOOK:
            continue
        ch = int(m.group(2))
        v1 = int(m.group(3)) if m.group(3) else 0
        v2 = int(m.group(4)) if m.group(4) else v1
        if out and out[-1][0] == book and out[-1][1] == ch and v1 and out[-1][2] and v1 <= out[-1][3] + 1:
            b, c, s, e = out[-1]
            out[-1] = (b, c, s, max(e, v2))      # junta JHN.3.16 + JHN.3.17 em 16-17
        elif (book, ch, v1, v2) not in out:
            out.append((book, ch, v1, v2))
    return out


def usfm_label(book: str, ch: int, v1: int, v2: int) -> str:
    lbl = f"{USFM_TO_PT_BOOK.get(book, book)} {ch}"
    if v1:
        lbl += f":{v1}" + (f"-{v2}" if v2 and v2 != v1 else "")
    return lbl


def yv_passages_text(passages: List[Tuple[str, int, int, int]], version_code: str = "NVI") -> List[Dict[str, str]]:
    ver = version_code.upper() if version_code.upper() in YOUVERSION_PT_VERSIONS else "NVI"
    ver_id = YOUVERSION_PT_VERSIONS[ver]
    out = []
    for book, ch, v1, v2 in passages[:12]:
        label = usfm_label(book, ch, v1, v2)
        verses = yv_fetch_chapter_verses(ver_id, book, ch)
        if verses:
            keys = sorted(k for k in verses if (not v1) or (v1 <= k <= (v2 or v1)))
            text = " ".join(verses[k] for k in keys)
        else:
            usfm = f"{book}.{ch}" + (f".{v1}" + (f"-{v2}" if v2 and v2 != v1 else "") if v1 else "")
            st, body = http_request(f"https://www.bible.com/pt/bible/{ver_id}/{usfm}.{ver}", timeout=10)
            text = " ".join(extract_og_meta_html(body, "og:description").split()) if st == 200 else ""
        if text:
            out.append({"label": f"{label} ({ver})", "text": text})
    return out


def yv_fetch_plan_day_api(yv: Dict[str, Any], pid: str, day: int) -> Optional[Dict[str, Any]]:
    num = _plan_num(pid)
    if not num:
        return None
    st, j, raw = yv_api_request(YV_PLAN_REFS_URL, yv, {"id": num, "day": day, "language_tag": "pt", "version_id": YOUVERSION_PT_VERSIONS.get(yv.get("version", "NVI"), 129)})
    d = _yv_unwrap(j)
    if st != 200 or not isinstance(d, (dict, list)):
        _yv_log(yv, f"plano {num} dia {day} references.json: HTTP {st} {raw[:100]}")
        return None
    refs = _deep_get(d, ("references",)) if isinstance(d, dict) else d
    passages = parse_usfm_list(json.dumps(refs if refs is not None else d, ensure_ascii=False))
    devotional = ""
    if isinstance(d, dict):
        devotional = _yv_text(d.get("additional_content") or d.get("content") or d.get("devotional"))
    if not passages and not devotional:
        return None
    return {"devotional": devotional, "passages": passages, "source": "api"}


def fetch_live_bible_com_plan_day(plan_id_or_url: str, day: int = 1, yv: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
    """Fallback web: le a pagina do dia (da sua inscricao, se houver) em bible.com, HTML + RSC, sem truncar."""
    if yv is None:
        yv = load_data().get("youversion", {})
    auth_hdrs = get_youversion_auth_headers(yv)
    urls = []
    if str(plan_id_or_url).startswith("http"):
        base_url = plan_id_or_url.rstrip("/")
        urls.append(re.sub(r"/day/\d+", f"/day/{day}", base_url) if "/day/" in base_url else f"{base_url}/day/{day}")
    else:
        if yv.get("username") and yv.get("active_subscription_id"):
            urls.append(f"https://www.bible.com/pt/users/{yv['username']}/reading-plans/{plan_id_or_url}/subscription/{yv['active_subscription_id']}/day/{day}")
        urls.append(f"https://www.bible.com/pt/reading-plans/{plan_id_or_url}/day/{day}")
    for day_url in urls:
        status, body = http_request(day_url, headers=auth_hdrs, timeout=12)
        if status != 200 or not body:
            continue
        flight = decode_next_flight(body)
        title_meta = extract_og_meta_html(body, "og:title")
        # 1. Blocos longos de texto no payload RSC (conteudo do devocional)
        cands: List[str] = []
        for m in re.finditer(r'"(?:content|html|text|body)"\s*:\s*"((?:\\.|[^"\\]){160,})"', flight):
            try:
                t = html_to_speech_text(json.loads('"' + m.group(1) + '"'))
            except Exception:
                continue
            if len(t) > 120 and t not in cands and "{" not in t[:40] and "cookie" not in t.lower():
                cands.append(t)
        # 2. Paragrafos do HTML renderizado
        if not cands:
            for pm in re.finditer(r"<p[^>]*>(.*?)</p>", body, flags=re.IGNORECASE | re.DOTALL):
                t = strip_html_tags(pm.group(1))
                if len(t) > 45 and "cookie" not in t.lower() and "youversion" not in t.lower():
                    cands.append(t)
        devotional = " ".join(cands) or extract_og_meta_html(body, "og:description")
        passages = parse_usfm_list(" ".join(re.findall(r'"usfm"\s*:\s*(\[[^\]]*\]|"[^"]+")', flight + body)))
        if len(devotional) > 30 or passages:
            return {"devotional": devotional, "passages": passages, "title": title_meta, "url": day_url, "source": "web"}
    return None


def yv_read_plan_day(yv: Dict[str, Any], plan_ref: str, day: int, plan_title: str, total_days: int) -> Optional[Dict[str, Any]]:
    """Monta o texto completo do dia: devocional + todas as passagens do dia (sem truncar)."""
    info = None
    if not str(plan_ref).startswith("http") and _plan_num(plan_ref):
        yv_ensure_token(yv)
        info = yv_fetch_plan_day_api(yv, plan_ref, day)
    if info is None:
        info = fetch_live_bible_com_plan_day(plan_ref, day, yv)
    if info is None:
        return None
    readings = yv_passages_text(info.get("passages", []), yv.get("version", "NVI"))
    parts = [f"{plan_title}. Dia {day} de {total_days}." if total_days else f"{plan_title}. Dia {day}."]
    if info.get("devotional"):
        parts.append(info["devotional"])
    for r in readings:
        parts.append(f"Leitura de {r['label']}. {r['text']}")
    if len(parts) == 1:
        return None
    refs_human = ", ".join(r["label"] for r in readings) or ", ".join(usfm_label(*p) for p in info.get("passages", []))
    num = _plan_num(plan_ref)
    return {
        "spoken": " ".join(p.strip() for p in parts if p.strip()),
        "devotional": info.get("devotional", ""),
        "readings": readings,
        "reference": refs_human or f"Dia {day}",
        "url": info.get("url") or (f"https://www.bible.com/pt/reading-plans/{num}/day/{day}" if num else str(plan_ref)),
        "source": info.get("source", ""),
    }


def _short_for_device(text: str, limit: int = 600) -> str:
    """Versao curta (para o display/historico do ESP32). O audio completo vem do /api/tts?key=last."""
    if len(text) <= limit:
        return text
    cut = text[: limit - 150]
    m = list(re.finditer(r"[.!?](?=\s)", cut))
    if m and m[-1].end() > 120:
        cut = cut[: m[-1].end()]
    return cut.strip() + " ..."


def _match_user_plan(yv: Dict[str, Any], query: str) -> Optional[Dict[str, Any]]:
    ups = yv.get("user_plans", []) or []
    q = normalize_accents_lower(query or "")
    if not q or not ups:
        return None
    num = _plan_num(query)
    if num:
        for up in ups:
            if str(up.get("plan_id")) == num:
                return up
        if len(num) <= 2 and 1 <= int(num) <= len(ups):          # "plano 2" -> segundo da lista
            return ups[int(num) - 1]
    ordinals = {"primeiro": 1, "segundo": 2, "terceiro": 3, "quarto": 4, "quinto": 5}
    for w, i in ordinals.items():
        if w in q and i <= len(ups):
            return ups[i - 1]
    for up in ups:
        t = normalize_accents_lower(up.get("title", ""))
        if q in t or (len(q) > 3 and t and t in q):
            return up
    words = [w for w in re.findall(r"[a-z0-9]{4,}", q) if w not in ("plano", "leitura", "biblia", "meu", "minha")]
    best, best_hits = None, 0
    for up in ups:
        t = normalize_accents_lower(up.get("title", ""))
        hits = sum(1 for w in words if w in t)
        if hits > best_hits:
            best, best_hits = up, hits
    return best


def _yv_result(yv: Dict[str, Any], action: str, spoken: str, **extra: Any) -> Dict[str, Any]:
    res = {
        "ok": True, "action": action, "spoken": spoken,
        "plan_title": yv.get("active_plan_title"), "day": yv.get("current_day"),
        "total_days": yv.get("total_days"),
    }
    res.update(extra)
    return res


def handle_youversion_query(params: Dict[str, str]) -> Dict[str, Any]:
    action = params.get("action", "read_plan_day") or "read_plan_day"
    plan = params.get("plan", "").strip()
    try:
        day = int(params.get("day", "0") or "0")
    except ValueError:
        day = 0
    ref = params.get("ref", "")
    version = params.get("version", "")

    data = load_data()
    yv = data["youversion"]
    if version and version.upper() in YOUVERSION_PT_VERSIONS:
        yv["version"] = version.upper()
    has_account = any(yv.get(k) for k in ("access_token", "refresh_token", "session_cookie"))

    # Re-sincroniza a conta se fizer mais de 6h (pega o progresso feito no celular)
    if has_account and (action == "sync" or time.time() - float(yv.get("last_sync_ts") or 0) > 6 * 3600):
        yv["last_sync_ts"] = time.time()
        sync_youversion_user_plans(data)
        yv = data["youversion"]

    if action == "sync":
        ups = yv.get("user_plans", [])
        spoken = (f"Sincronizei {len(ups)} planos da sua conta YouVersion. Plano ativo: {yv['active_plan_title']}, dia {yv['current_day']}."
                  if ups else "Nao encontrei planos na sua conta do YouVersion. Verifique o login no painel do servidor.")
        save_data(data)
        return _yv_result(yv, action, spoken, plans=ups)

    if action == "verse_of_day":
        votd = fetch_youversion_votd_pt()
        spoken = f"Versiculo do Dia no App Biblia YouVersion: {votd['reference']}. {votd['text']}"
        save_data(data)
        return _yv_result(yv, action, spoken, reference=votd["reference"], text=votd["text"], url=votd["url"])

    if action == "get_passage" and ref:
        usfm = parse_pt_reference_to_usfm(ref)
        readings = yv_passages_text(parse_usfm_list(usfm), yv.get("version", "NVI")) if usfm else []
        if readings:
            reference, text, url = readings[0]["label"], readings[0]["text"], f"https://www.bible.com/pt/bible/{YOUVERSION_PT_VERSIONS.get(yv.get('version', 'NVI'), 129)}/{usfm}"
        else:
            pas = fetch_youversion_passage_pt(ref, yv.get("version", "NVI"))
            reference, text, url = pas["reference"], pas["text"], pas["url"]
        data["bible_history"] = ([{"reference": reference, "text": text[:400], "ts": datetime.now().strftime("%H:%M")}] + data.get("bible_history", []))[:10]
        save_data(data)
        return _yv_result(yv, action, f"{reference}. {text}", reference=reference, text=text, url=url)

    if action == "list_plans":
        ups = yv.get("user_plans", [])
        if ups:
            items = [f"{i + 1}, {p['title']}, dia {p.get('current_day', 1)} de {p.get('total_days', '?')}"
                     for i, p in enumerate(ups[:8]) if not p.get("completed")]
            spoken = (f"Voce tem {len(items)} planos em andamento no YouVersion: " + "; ".join(items) +
                      f". O plano ativo no Jarvis e {yv['active_plan_title']}. Diga, por exemplo, ative o plano 2.")
            save_data(data)
            return _yv_result(yv, action, spoken, plans=ups)
        plans = scrape_live_reading_plans_from_bible_com(plan)
        titles = [p["title"] for p in plans[:5]]
        spoken = (f"Seu plano atual e {yv['active_plan_title']} (Dia {yv['current_day']} de {yv['total_days']}). "
                  f"Planos disponiveis: " + "; ".join(titles) + ".")
        return _yv_result(yv, action, spoken, plans=plans)

    if action == "start_plan" and plan:
        up = _match_user_plan(yv, plan)
        q_norm = normalize_accents_lower(plan)
        matched_key = None
        if not up:
            for k, pinfo in CURATED_YOUVERSION_PLANS.items():
                if k in q_norm or q_norm in k or any(t.strip() and t.strip() in q_norm for t in pinfo["topic"].split(",")):
                    matched_key = k
                    break
        if up:
            _yv_set_active(yv, up)
        elif matched_key:
            pobj = CURATED_YOUVERSION_PLANS[matched_key]
            yv.update({"active_plan_key": matched_key, "active_plan_id": pobj["plan_id"], "active_subscription_id": "",
                       "active_plan_title": pobj["title"], "current_day": 1, "total_days": pobj["total_days"],
                       "custom_bible_com_plan_url": ""})
        elif "bible.com" in plan or _plan_num(plan):
            m = re.search(r"/reading-plans/(\d+)", plan)
            pid = m.group(1) if m else _plan_num(plan)
            meta = yv_plan_meta(yv, pid)
            yv.update({"active_plan_key": "custom", "active_plan_id": pid, "active_subscription_id": "",
                       "active_plan_title": meta.get("title") or f"Plano #{pid}",
                       "current_day": 1, "total_days": int(meta.get("total_days") or 1),
                       "custom_bible_com_plan_url": plan if plan.startswith("http") else ""})
        else:
            save_data(data)
            return _yv_result(yv, action, f"Nao encontrei o plano {plan} na sua conta do YouVersion nem nos temas disponiveis.", ok_match=False)
        save_data(data)
        action, day = "read_plan_day", 0

    prefix = ""
    if action == "next_plan_day":
        cur = int(yv.get("current_day", 1))
        tot = int(yv.get("total_days", 1) or 1)
        if cur not in yv.setdefault("completed_days", []):
            yv["completed_days"].append(cur)
        if yv.get("active_plan_key") in CURATED_YOUVERSION_PLANS:
            yv["current_day"] = (cur % tot) + 1
        elif cur >= tot:
            save_data(data)
            return _yv_result(yv, action, f"Parabens! Voce chegou ao ultimo dia do plano {yv['active_plan_title']}. Escolha outro plano no app ou diga liste meus planos.")
        else:
            yv["current_day"] = cur + 1
        prefix = f"Avancando para o dia {yv['current_day']}. "
        day = 0

    target_day = day if day > 0 else int(yv.get("current_day", 1))

    # Plano da sua conta (ou ID/URL colado) -> leitura ao vivo do YouVersion
    if yv.get("active_plan_key") in ("account", "custom"):
        tot = int(yv.get("total_days") or 0)
        if tot and target_day > tot:
            target_day = tot
        plan_ref = yv.get("custom_bible_com_plan_url") or yv.get("active_plan_id", "")
        live = yv_read_plan_day(yv, plan_ref, target_day, yv.get("active_plan_title", "Plano"), tot)
        yv["current_day"] = target_day
        if live:
            yv["last_devotional"] = live["spoken"]
            save_data(data)
            return _yv_result(yv, "read_plan_day", prefix + live["spoken"], reference=live["reference"],
                              text=live["devotional"], url=live["url"], source=live["source"])
        save_data(data)
        return _yv_result(yv, "read_plan_day", prefix + f"Nao consegui baixar o dia {target_day} do plano {yv.get('active_plan_title')} agora. "
                          "Confira o login do YouVersion no painel do servidor.", ok_match=False)

    pkey = yv.get("active_plan_key", "ansiedade")
    if pkey not in CURATED_YOUVERSION_PLANS:
        pkey = "ansiedade"
    pobj = CURATED_YOUVERSION_PLANS[pkey]
    tot_days = pobj["total_days"]
    if target_day < 1 or target_day > tot_days:
        target_day = 1
    yv.update({"active_plan_key": pkey, "current_day": target_day, "total_days": tot_days, "active_plan_title": pobj["title"]})
    d_obj = pobj["days"][target_day]
    spoken = (f"{prefix}Plano de Leitura: {pobj['title']}. {d_obj['title']}. Devocional: {d_obj['devotional']} "
              f"Leitura Biblica em {d_obj['scripture_ref']}: {d_obj['scripture_text']}")
    yv["last_devotional"] = spoken
    save_data(data)
    return _yv_result(yv, "read_plan_day", spoken, day_title=d_obj["title"], devotional=d_obj["devotional"],
                      reference=d_obj["scripture_ref"], text=d_obj["scripture_text"], url=pobj["url"])


def compact_for_esp32(res: Dict[str, Any]) -> Dict[str, Any]:
    """Resposta enxuta para o ESP-WROOM-32 (cabe num DynamicJsonDocument de 3 KB)."""
    full = str(res.get("spoken") or "")
    return {
        "ok": res.get("ok", True),
        "action": res.get("action"),
        "day": int(res.get("day") or 0),
        "total_days": int(res.get("total_days") or 0),
        "plan_title": str(res.get("plan_title") or "")[:120],
        "reference": str(res.get("reference") or "")[:160],
        "spoken": _short_for_device(full),
        "tts_key": "last" if full else "",
        "tts_chars": len(full),
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
        imgs = (item.get("album") or {}).get("images") or [{}]
        return {"ok": True, "message": f"{prefix}: {name}, de {artist}.", "track": track_str,
                "name": name, "artist": artist, "album": (item.get("album") or {}).get("name", ""),
                "image": imgs[0].get("url", ""), "is_playing": playing,
                "progress_ms": cjson.get("progress_ms") or 0, "duration_ms": item.get("duration_ms") or 0,
                "device": ((cjson.get("device") or {}).get("name") or "")}

    return {"ok": True, "message": "Nada esta tocando no Spotify no momento.", "track": "Parado"}


def render_dashboard_html() -> str:
    data = load_data()
    sp = data["spotify"]
    yv = data["youversion"]
    has_sp = bool(sp.get("refresh_token"))
    has_yv = bool(yv.get("access_token") or yv.get("refresh_token") or yv.get("session_cookie"))
    who = html.escape(str(yv.get('full_name') or yv.get('username') or yv.get('email') or ''))
    if has_yv and yv.get("access_token"):
        yv_user_label = f"✅ CONECTADO COMO: {who} <small>({html.escape(str(yv.get('auth_method') or ''))})</small>"
    elif has_yv:
        yv_user_label = "⚠️ SESSAO SALVA, MAS SEM TOKEN VALIDO (veja o diagnostico)"
    else:
        yv_user_label = "⚠️ NAO LOGADO (USANDO PLANOS PUBLICOS)"
    sync_log_html = html.escape("\n".join(yv.get("sync_log", [])[-15:])) or "(vazio)"
    yv_err_html = (
        f'<p style="background:#3b1219;border:1px solid #ff4d6d;color:#ffb3c1;padding:10px;border-radius:6px;">❌ {html.escape(yv["auth_error"])}</p>'
        if yv.get("auth_error")
        else ""
    )
    user_plans_html = "".join(
        f'<li>{"▶️ " if str(p.get("plan_id")) == str(yv.get("active_plan_id")) else ""}<b>{html.escape(str(p.get("title","Plano")))}</b> '
        f'(Dia {p.get("current_day",1)} de {p.get("total_days","?")}{" — concluido" if p.get("completed") else ""}{" — salvo" if p.get("saved_only") else ""}) — '
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
      <summary style="cursor:pointer;color:#00e5ff;">Usa login social (Google / Apple)? Conecte colando o token ou os cookies do bible.com</summary>
      <p>No navegador logado em www.bible.com: F12 &rarr; Application/Armazenamento &rarr; Cookies &rarr; https://www.bible.com. Copie o valor do cookie <code>yva</code> (e o token Bearer) <b>ou</b> a linha inteira de cookies (aba Network &rarr; qualquer requisicao &rarr; cabecalho <code>Cookie</code>).</p>
      <form action="/youversion/session" method="post" style="margin-top:8px;">
        <input type="text" name="username" style="width:160px;" placeholder="@username (opcional)" value="{html.escape(yv.get('username',''))}" />
        <input type="password" name="access_token" style="width:240px;" placeholder="Token Bearer / valor do cookie yva" />
        <input type="password" name="session_cookie" style="width:240px;" placeholder="Cookies do bible.com (nome=valor; ...)" />
        <button type="submit">Salvar Sessao YouVersion</button>
      </form>
    </details>
    <details style="margin-top:8px;font-size:0.86rem;color:#9ab8d9;">
      <summary style="cursor:pointer;color:#00e5ff;">Log da ultima sincronizacao ({html.escape(str(yv.get('last_sync') or 'nunca'))}) — <a style="color:#00e5ff;" href="/youversion/debug">diagnostico completo (JSON)</a></summary>
      <pre style="white-space:pre-wrap;background:#05080e;padding:8px;border-radius:6px;">{sync_log_html}</pre>
    </details>
    <h4 style="margin-bottom:6px;margin-top:14px;color:#00e5ff;">📚 Planos de Leitura Sincronizados da Sua Conta:</h4>
    <ul style="margin-top:4px;">{user_plans_html}</ul>
  </div>

  <div class="card">
    <h3>📖 2. Leitura & Devocional Ativo no J.A.R.V.I.S. (bible.com/pt/reading-plans)</h3>
    <p>Plano Ativo: <b>{html.escape(str(yv.get('active_plan_title')))}</b> — <b>Dia {yv.get('current_day')} de {yv.get('total_days')}</b> (Traducao: <code>{yv.get('version','NVI')}</code>)</p>
    <p style="font-size:0.9rem;color:#9ab8d9;">{html.escape(str(yv.get('last_devotional') or 'Clique abaixo para testar o devocional ou iniciar um plano do bible.com/pt/reading-plans.'))}</p>
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


# ============================================================================
# PAINEL DE CONTROLE v2 — ESTADO DO ESP32, EVENTOS, CONFIGURACOES, CLIMA, BRIEFING
# ============================================================================
PANEL_HTML_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web", "panel.html")
DEVICE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "jarvis_device.json")
DEVICE_ONLINE_SEC = 75          # sem heartbeat ha mais que isso = offline
DEVICE_LOCK = threading.RLock()
EVENTS_LOCK = threading.Lock()
EVENTS: "deque[Dict[str, Any]]" = deque(maxlen=400)
_EVENT_SEQ = [0]
SERVER_STARTED = time.time()

PT_VOICES = [
    {"id": "pt-BR-AntonioNeural", "label": "Antonio (masculina, padrao)"},
    {"id": "pt-BR-FranciscaNeural", "label": "Francisca (feminina)"},
    {"id": "pt-BR-ThalitaMultilingualNeural", "label": "Thalita (feminina, multilingue)"},
    {"id": "pt-PT-DuarteNeural", "label": "Duarte (Portugal, masculina)"},
    {"id": "pt-PT-RaquelNeural", "label": "Raquel (Portugal, feminina)"},
]

DEFAULT_SETTINGS = {
    "user_name": "",
    "voice": "pt-BR-AntonioNeural",
    "rate": "+0%",
    "esp_ip": "",                       # vazio = usa o IP informado pelo heartbeat
    "city": "Sao Paulo",
    "latitude": "-23.5505",
    "longitude": "-46.6333",
    "briefing_weather": True,
    "briefing_alarms": True,
    "briefing_plan": True,
    "briefing_plan_full": True,         # le o devocional completo no briefing
    "briefing_verse": True,
    "briefing_reminders": True,         # lembretes de hoje
    "briefing_dates": True,             # aniversarios / datas especiais proximas
    "briefing_quotes": False,           # dolar, euro, bitcoin
    "briefing_news": False,             # 3 manchetes
    "news_topic": "",                   # vazio = principais noticias
}

WMO_PT = {
    0: "céu limpo", 1: "predominantemente limpo", 2: "parcialmente nublado", 3: "nublado",
    45: "neblina", 48: "neblina com geada", 51: "garoa fraca", 53: "garoa", 55: "garoa forte",
    61: "chuva fraca", 63: "chuva", 65: "chuva forte", 66: "chuva congelante", 67: "chuva congelante forte",
    71: "neve fraca", 73: "neve", 75: "neve forte", 80: "pancadas de chuva fracas", 81: "pancadas de chuva",
    82: "pancadas de chuva fortes", 95: "trovoadas", 96: "trovoadas com granizo", 99: "trovoadas fortes com granizo",
}
WEEKDAYS_PT = ["segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado", "domingo"]
MONTHS_PT = ["janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto",
             "setembro", "outubro", "novembro", "dezembro"]


def log_event(kind: str, msg: str, level: str = "info") -> None:
    with EVENTS_LOCK:
        _EVENT_SEQ[0] += 1
        EVENTS.append({"id": _EVENT_SEQ[0], "ts": time.time(), "time": datetime.now().strftime("%H:%M:%S"),
                       "kind": kind, "level": level, "msg": str(msg)[:400]})
    print(f"[{kind}] {msg}")


def get_events(after: int = 0) -> List[Dict[str, Any]]:
    with EVENTS_LOCK:
        return [e for e in EVENTS if e["id"] > after]


def get_settings(data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    data = data if data is not None else load_data()
    st = dict(DEFAULT_SETTINGS)
    st.update({k: v for k, v in (data.get("settings") or {}).items() if k in DEFAULT_SETTINGS})
    return st


def update_settings(patch: Dict[str, Any]) -> Dict[str, Any]:
    data = load_data()
    st = get_settings(data)
    for k, v in patch.items():
        if k not in DEFAULT_SETTINGS:
            continue
        if isinstance(DEFAULT_SETTINGS[k], bool):
            v = v if isinstance(v, bool) else str(v).lower() in ("1", "true", "on", "sim", "yes")
        else:
            v = str(v).strip()[:80]
        st[k] = v
    if not re.fullmatch(r"[+-]\d{1,2}%", st["rate"]):
        st["rate"] = "+0%"
    data["settings"] = st
    save_data(data)
    log_event("config", "Configuracoes atualizadas")
    return st


# ---------------------------------------------------------------- estado do ESP32
def _load_device() -> Dict[str, Any]:
    try:
        with open(DEVICE_FILE, "r", encoding="utf-8") as f:
            d = json.load(f)
            if isinstance(d, dict):
                return d
    except Exception:
        pass
    return {"info": {}, "queue": [], "conversations": [], "last_seen": 0}


DEVICE: Dict[str, Any] = _load_device()


def _save_device() -> None:
    tmp = DEVICE_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(DEVICE, f, ensure_ascii=False, indent=1)
    os.replace(tmp, DEVICE_FILE)


def device_online() -> bool:
    return time.time() - float(DEVICE.get("last_seen") or 0) < DEVICE_ONLINE_SEC


def handle_heartbeat(payload: Dict[str, Any], client_ip: str) -> Dict[str, Any]:
    """POST /api/device/heartbeat do ESP32. Responde com os comandos pendentes do painel."""
    with DEVICE_LOCK:
        was_online = device_online()
        info = DEVICE.setdefault("info", {})
        for k in ("ip", "heap", "min_heap", "rssi", "uptime", "volume", "plan", "notes", "timers",
                  "alarms", "fw", "busy", "spotify"):
            if k in payload:
                info[k] = payload[k]
        info["ip"] = payload.get("ip") or client_ip
        DEVICE["last_seen"] = time.time()
        lu, lr = str(payload.get("last_user") or ""), str(payload.get("last_reply") or "")
        convs = DEVICE.setdefault("conversations", [])
        if lr and (not convs or convs[-1].get("reply") != lr or convs[-1].get("user") != lu):
            convs.append({"time": datetime.now().strftime("%d/%m %H:%M"), "user": lu, "reply": lr})
            del convs[:-60]
            log_event("voz", f"Você: {lu} | Jarvis: {lr[:160]}")
        cmds = DEVICE.get("queue", [])[:6]
        DEVICE["queue"] = DEVICE.get("queue", [])[6:]
        _save_device()
    if not was_online:
        log_event("esp32", f"ESP32 online em {info['ip']} (RAM {info.get('heap', '?')} KB, WiFi {info.get('rssi', '?')} dBm)")
    if isinstance(payload.get("alarms"), list):
        data = load_data()
        if data.get("alarms") != payload["alarms"]:
            data["alarms"] = payload["alarms"]
            save_data(data)
    return {"ok": True, "commands": cmds, "server_time": int(time.time())}


def tts_store_put(key: str, text: str) -> None:
    data = load_data()
    store = data.setdefault("tts_store", {})
    store[key] = text
    if key == "last":
        data["last_spoken_tts"] = text
    save_data(data)


def tts_store_get(key: str) -> str:
    data = load_data()
    if key == "last":
        return str(data.get("last_spoken_tts") or "")
    return str((data.get("tts_store") or {}).get(key) or "")


ESP_CMD_TYPES = ("speak", "say_key", "ask", "mic", "volume", "alarm_add", "alarm_remove", "alarm_clear",
                 "note_add", "notes_clear", "timer", "test", "briefing", "bible", "spotify", "restart")


def esp_send(cmd: Dict[str, Any]) -> Dict[str, Any]:
    """Envia um comando ao ESP32: direto em http://<ip>/api/queue ou, se falhar, na fila do heartbeat."""
    ctype = str(cmd.get("type", ""))
    if ctype not in ESP_CMD_TYPES:
        return {"ok": False, "error": f"tipo de comando invalido: {ctype}"}
    clean: Dict[str, Any] = {"type": ctype}
    for k in ("text", "key", "h", "m", "v", "label", "r", "mode", "action", "sec"):
        if k in cmd and cmd[k] not in (None, ""):
            clean[k] = cmd[k]
    # Fala longa: o texto completo fica no servidor; o ESP recebe so a chave + um resumo
    if ctype == "speak" and len(str(clean.get("text", ""))) > 300:
        full = str(clean["text"])
        tts_store_put("panel", full)
        clean = {"type": "say_key", "key": "panel", "text": _short_for_device(full, 280)}
    st = get_settings()
    ip = st.get("esp_ip") or DEVICE.get("info", {}).get("ip")
    if ip:
        qs = urllib.parse.urlencode({k: (1 if v is True else 0 if v is False else v) for k, v in clean.items()})
        code, body = http_request(f"http://{ip}/api/queue?{qs}", timeout=4)
        if code == 200:
            log_event("painel", f"Comando '{ctype}' enviado ao ESP32 ({ip})")
            return {"ok": True, "delivered": "direct"}
    with DEVICE_LOCK:
        q = DEVICE.setdefault("queue", [])
        q.append(clean)
        del q[:-12]
        _save_device()
    log_event("painel", f"ESP32 não respondeu; comando '{ctype}' na fila (entregue no próximo heartbeat)", "warn")
    return {"ok": True, "delivered": "queued"}


# ---------------------------------------------------------------- clima
_WEATHER_CACHE: Dict[str, Any] = {"ts": 0, "key": "", "data": None}


def get_weather(force: bool = False) -> Dict[str, Any]:
    st = get_settings()
    key = f"{st['latitude']},{st['longitude']}"
    if not force and _WEATHER_CACHE["data"] and _WEATHER_CACHE["key"] == key and time.time() - _WEATHER_CACHE["ts"] < 600:
        return _WEATHER_CACHE["data"]
    url = ("https://api.open-meteo.com/v1/forecast?" + urllib.parse.urlencode({
        "latitude": st["latitude"], "longitude": st["longitude"], "timezone": "auto", "forecast_days": 3,
        "current": "temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m",
        "daily": "temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code",
    }))
    code, body = http_request(url, timeout=8)
    try:
        j = json.loads(body) if code == 200 else None
    except Exception:
        j = None
    if not j:
        return {"ok": False, "error": f"Open-Meteo HTTP {code}", "city": st["city"]}
    cur, daily = j.get("current", {}), j.get("daily", {})
    days = []
    for i, dt in enumerate(daily.get("time", [])[:3]):
        days.append({
            "date": dt, "max": daily.get("temperature_2m_max", [None] * 3)[i],
            "min": daily.get("temperature_2m_min", [None] * 3)[i],
            "rain": daily.get("precipitation_probability_max", [None] * 3)[i],
            "desc": WMO_PT.get(int(daily.get("weather_code", [0] * 3)[i] or 0), "tempo variável"),
        })
    out = {
        "ok": True, "city": st["city"], "temp": cur.get("temperature_2m"), "feels": cur.get("apparent_temperature"),
        "humidity": cur.get("relative_humidity_2m"), "wind": cur.get("wind_speed_10m"),
        "precip": cur.get("precipitation"), "desc": WMO_PT.get(int(cur.get("weather_code") or 0), "tempo variável"),
        "days": days,
    }
    _WEATHER_CACHE.update({"ts": time.time(), "key": key, "data": out})
    return out


def _fmt_num(v: Any) -> str:
    try:
        return str(int(round(float(v))))
    except Exception:
        return "?"


# ---------------------------------------------------------------- briefing matinal
def build_briefing() -> Dict[str, Any]:
    st = get_settings()
    now = datetime.now()
    nome = st.get("user_name") or "senhor"
    saud = "Bom dia" if now.hour < 12 else ("Boa tarde" if now.hour < 18 else "Boa noite")
    parts = [f"{saud}, {nome}. Hoje é {WEEKDAYS_PT[now.weekday()]}, {now.day} de {MONTHS_PT[now.month - 1]}, "
             f"e agora são {now.hour} e {now.minute:02d}."]
    sections: Dict[str, str] = {}
    if st["briefing_weather"]:
        w = get_weather()
        if w.get("ok"):
            t = (f"Em {w['city']}, {w['desc']}, {_fmt_num(w['temp'])} graus agora")
            if w.get("days"):
                d0 = w["days"][0]
                t += f", máxima de {_fmt_num(d0['max'])} e mínima de {_fmt_num(d0['min'])}"
                if d0.get("rain") is not None:
                    t += f", com {_fmt_num(d0['rain'])} por cento de chance de chuva"
            sections["clima"] = t + "."
    if st["briefing_alarms"]:
        alarms = load_data().get("alarms", []) or []
        nowm = now.hour * 60 + now.minute
        nxt = sorted((a for a in alarms if isinstance(a, dict) and int(a.get("h", 0)) * 60 + int(a.get("m", 0)) > nowm),
                     key=lambda a: int(a.get("h", 0)) * 60 + int(a.get("m", 0)))
        if nxt:
            sections["alarmes"] = "Próximos alarmes de hoje: " + "; ".join(
                f"{int(a.get('h', 0)):02d}:{int(a.get('m', 0)):02d}, {a.get('l', 'alarme')}" for a in nxt[:4]) + "."
    if st["briefing_reminders"]:
        todays = [r for r in reminders_sorted(load_data()) if r["date"] == now.date().isoformat() and r["time"] > now.strftime("%H:%M")]
        if todays:
            sections["lembretes"] = "Lembretes de hoje: " + "; ".join(f"{spoken_time(r['time'])}, {r['text']}" for r in todays[:5]) + "."
    if st["briefing_dates"]:
        soon = [e for e in special_dates_view() if e["days_left"] <= 7]
        if soon:
            sections["datas"] = special_date_countdown(soon[0]["name"])["spoken"] if len(soon) == 1 else \
                "; ".join(("Hoje é " if e["days_left"] == 0 else f"Em {e['days_left']} dias, ") + e["name"] for e in soon[:3]) + "."
    if st["briefing_quotes"]:
        sections["cotacoes"] = quotes_spoken()
    if st["briefing_news"]:
        sections["noticias"] = news_spoken(st.get("news_topic", ""), 3)
    if st["briefing_plan"]:
        if st["briefing_plan_full"]:
            res = handle_youversion_query({"action": "read_plan_day"})
            if res.get("spoken"):
                sections["biblia"] = "Agora, seu devocional de hoje. " + res["spoken"]
        else:
            yv = load_data()["youversion"]
            sections["biblia"] = (f"Seu plano de leitura {yv.get('active_plan_title')} está no dia "
                                  f"{yv.get('current_day')} de {yv.get('total_days')}.")
    if st["briefing_verse"] and "biblia" not in sections:
        v = fetch_youversion_votd_pt()
        sections["versiculo"] = f"Versículo do dia, {v['reference']}: {v['text']}"
    parts += [sections[k] for k in ("clima", "alarmes", "lembretes", "datas", "cotacoes", "noticias", "biblia", "versiculo") if k in sections]
    parts.append("Tenha um ótimo dia." if now.hour < 18 else "Tenha uma boa noite.")
    spoken = " ".join(parts)
    log_event("briefing", f"Briefing gerado ({len(spoken)} caracteres)")
    return {"ok": True, "action": "briefing", "spoken": spoken, "sections": sections,
            "reference": "Briefing", "day": 0, "total_days": 0, "plan_title": ""}


def preview_plan_day(plan_id: str, day: int) -> Dict[str, Any]:
    """Le qualquer dia de qualquer plano sem alterar o progresso (para o painel)."""
    data = load_data()
    yv = data["youversion"]
    up = next((p for p in yv.get("user_plans", []) if str(p.get("plan_id")) == str(plan_id)), None)
    title = (up or {}).get("title") or yv.get("active_plan_title") or "Plano"
    total = int((up or {}).get("total_days") or yv.get("total_days") or 0)
    ref = plan_id or yv.get("active_plan_id", "")
    res = yv_read_plan_day(yv, ref, max(1, day), title, total)
    save_data(data)
    if res:
        tts_store_put("preview", res["spoken"])
    if not res:
        return {"ok": False, "error": "Nao consegui baixar esse dia."}
    return {"ok": True, "title": title, "day": day, "total_days": total, **res}


def build_state() -> Dict[str, Any]:
    data = load_data()
    yv, sp = data["youversion"], data["spotify"]
    with DEVICE_LOCK:
        dev = dict(DEVICE.get("info", {}))
        convs = list(DEVICE.get("conversations", [])[-15:])
        queue_len = len(DEVICE.get("queue", []))
        last_seen = float(DEVICE.get("last_seen") or 0)
    return {
        "server": {"uptime": int(time.time() - SERVER_STARTED), "time": datetime.now().strftime("%d/%m/%Y %H:%M:%S")},
        "device": {"online": device_online(), "last_seen_ago": int(time.time() - last_seen) if last_seen else None,
                   "queue": queue_len, **dev},
        "conversations": convs,
        "youversion": {
            "logged_in": bool(yv.get("access_token") or yv.get("refresh_token") or yv.get("session_cookie")),
            "has_token": bool(yv.get("access_token")), "user": yv.get("full_name") or yv.get("username") or "",
            "email": yv.get("email", ""), "auth_method": yv.get("auth_method", ""), "auth_error": yv.get("auth_error", ""),
            "active_plan_id": yv.get("active_plan_id"), "active_plan_title": yv.get("active_plan_title"),
            "current_day": yv.get("current_day"), "total_days": yv.get("total_days"), "version": yv.get("version", "NVI"),
            "last_sync": yv.get("last_sync", ""), "sync_log": yv.get("sync_log", [])[-20:],
            "plans": [{k: p.get(k) for k in ("plan_id", "title", "current_day", "total_days", "completed", "saved_only")}
                      for p in yv.get("user_plans", []) or []],
            "last_devotional": str(yv.get("last_devotional") or "")[:1500],
        },
        "spotify": {"connected": bool(sp.get("refresh_token")), "last_track": sp.get("last_track"),
                    "client_id": sp.get("client_id", ""), "redirect_uri": sp.get("redirect_uri")},
        "alarms": data.get("alarms", []),
        "bible_history": data.get("bible_history", [])[:10],
        "settings": get_settings(data),
        "voices": PT_VOICES,
    }


def tts_preview_mp3(text: str, voice: str, rate: str) -> bytes:
    import asyncio
    import edge_tts

    async def _go() -> bytes:
        buf = bytearray()
        async for ch in edge_tts.Communicate(text[:30000], voice, rate=rate).stream():
            if ch["type"] == "audio":
                buf.extend(ch["data"])
        return bytes(buf)
    return asyncio.run(_go())


# ============================================================================
# ASSISTENTE PESSOAL: LEMBRETES AGENDADOS, LISTAS, ROTINAS, DATAS ESPECIAIS,
# NOTICIAS, COTACOES E WIKIPEDIA
# ============================================================================
# Tudo fica no PC (jarvis_data.json). O ESP32 chama GET /api/assistant?client=esp32&action=...
# pela ferramenta `assistant_tools` do Gemini; o painel usa /api/assistant/data e /api/assistant/op.
# Um agendador em thread dispara lembretes e rotinas no horario, enviando a fala ao ESP32
# (direto ou pela fila do heartbeat, se ele estiver ocupado/offline naquele momento).
# ============================================================================
from contextlib import contextmanager
from datetime import date as _date, timedelta
import xml.etree.ElementTree as ET

WEEKDAY_WORDS = {"segunda": 0, "terca": 1, "quarta": 2, "quinta": 3, "sexta": 4, "sabado": 5, "domingo": 6}
REPEAT_LABEL = {"none": "uma vez", "daily": "todo dia", "weekdays": "de segunda a sexta", "weekly": "toda semana"}


@contextmanager
def data_txn():
    """Le-modifica-grava o jarvis_data.json sem perder escritas de outras threads."""
    with _DATA_LOCK:
        d = load_data()
        yield d
        save_data(d)


def _new_id() -> str:
    return uuid.uuid4().hex[:8]


def resolve_date(text: str, base: Optional[_date] = None) -> Optional[_date]:
    """'hoje', 'amanha', 'depois de amanha', 'sexta', '2026-12-25', '25/12', '25/12/2026', 'dia 15'."""
    base = base or _date.today()
    s = normalize_accents_lower(text or "")
    if not s or s == "hoje":
        return base
    if "depois de amanha" in s:
        return base + timedelta(days=2)
    if "amanha" in s:
        return base + timedelta(days=1)
    m = re.search(r"(\d{4})-(\d{1,2})-(\d{1,2})", s)
    if m:
        try:
            return _date(int(m.group(1)), int(m.group(2)), int(m.group(3)))
        except ValueError:
            return None
    m = re.search(r"\b(\d{1,2})/(\d{1,2})(?:/(\d{2,4}))?\b", s)
    if m:
        y = int(m.group(3)) if m.group(3) else base.year
        y = y + 2000 if y < 100 else y
        try:
            d = _date(y, int(m.group(2)), int(m.group(1)))
        except ValueError:
            return None
        if not m.group(3) and d < base:
            d = d.replace(year=d.year + 1)
        return d
    for w, wd in WEEKDAY_WORDS.items():
        if w in s:
            return base + timedelta(days=((wd - base.weekday()) % 7) or 7)
    m = re.search(r"\bdia (\d{1,2})\b", s)
    if m:
        day = int(m.group(1))
        y, mo = base.year, base.month
        if day < base.day:
            mo += 1
            if mo > 12:
                y, mo = y + 1, 1
        try:
            return _date(y, mo, day)
        except ValueError:
            return None
    return None


def resolve_time(text: str) -> Optional[str]:
    """'09:30', '9h30', '9h', '21', '7 e meia' -> 'HH:MM'."""
    s = normalize_accents_lower(text or "")
    m = re.search(r"(\d{1,2})\s*(?:[:h]\s*(\d{2})?|\s+e\s+meia)?", s)
    if not m:
        return None
    h = int(m.group(1))
    mi = int(m.group(2)) if m.group(2) else (30 if "meia" in s else 0)
    if "tarde" in s or "noite" in s:
        if h < 12:
            h += 12
    if not (0 <= h <= 23 and 0 <= mi <= 59):
        return None
    return f"{h:02d}:{mi:02d}"


def spoken_date(d: _date) -> str:
    today = _date.today()
    if d == today:
        return "hoje"
    if d == today + timedelta(days=1):
        return "amanhã"
    return f"{WEEKDAYS_PT[d.weekday()]}, {d.day} de {MONTHS_PT[d.month - 1]}"


def spoken_time(hhmm: str) -> str:
    h, m = hhmm.split(":")
    return f"{int(h)} horas" + (f" e {int(m)}" if int(m) else "")


# ---------------------------------------------------------------- lembretes
def _advance_reminder(r: Dict[str, Any], now: datetime) -> None:
    d = _date.fromisoformat(r["date"])
    rep = r.get("repeat", "none")
    if rep == "none":
        r["active"] = False
        return
    while True:
        d += timedelta(days=7 if rep == "weekly" else 1)
        if rep == "weekdays" and d.weekday() >= 5:
            continue
        if datetime.fromisoformat(f"{d.isoformat()}T{r['time']}") > now:
            break
    r["date"] = d.isoformat()


def reminder_add(text: str, when_date: str = "", when_time: str = "", repeat: str = "none", minutes: Any = None) -> Dict[str, Any]:
    text = " ".join((text or "").split())
    if not text:
        return {"ok": False, "spoken": "Sobre o que devo te lembrar?"}
    now = datetime.now()
    if minutes not in (None, ""):
        try:
            dt = now + timedelta(minutes=float(minutes))
        except ValueError:
            dt = None
        if dt is None:
            return {"ok": False, "spoken": "Não entendi em quantos minutos."}
        d, t = dt.date(), dt.strftime("%H:%M")
    else:
        d = resolve_date(when_date)
        t = resolve_time(when_time) if when_time else None
        if d is None:
            return {"ok": False, "spoken": f"Não entendi a data {when_date}."}
        if not t:
            t = "09:00"
        if datetime.fromisoformat(f"{d.isoformat()}T{t}") <= now:
            if d == now.date():
                d += timedelta(days=1)      # "as 7 horas" já passou hoje -> amanha
            else:
                return {"ok": False, "spoken": f"A data {spoken_date(d)} já passou."}
    repeat = repeat if repeat in REPEAT_LABEL else "none"
    r = {"id": _new_id(), "text": text, "date": d.isoformat(), "time": t, "repeat": repeat, "active": True, "created": now.isoformat(timespec="seconds")}
    with data_txn() as data:
        data.setdefault("reminders", []).append(r)
    rep = "" if repeat == "none" else f", {REPEAT_LABEL[repeat]}"
    log_event("lembrete", f"Novo lembrete: {text} ({d.isoformat()} {t}{rep})")
    return {"ok": True, "reminder": r, "spoken": f"Certo. Vou te lembrar de {text} {spoken_date(d)} às {spoken_time(t)}{rep}."}


def reminders_sorted(data: Dict[str, Any]) -> List[Dict[str, Any]]:
    rs = [r for r in data.get("reminders", []) if r.get("active")]
    return sorted(rs, key=lambda r: (r["date"], r["time"]))


def reminders_spoken(day: Optional[_date] = None) -> str:
    rs = reminders_sorted(load_data())
    if day:
        rs = [r for r in rs if r["date"] == day.isoformat()]
    if not rs:
        return "Você não tem lembretes" + (" para esse dia." if day else " agendados.")
    parts = [f"{r['text']}, {spoken_date(_date.fromisoformat(r['date']))} às {spoken_time(r['time'])}" for r in rs[:8]]
    return f"Você tem {len(rs)} lembrete{'s' if len(rs) > 1 else ''}: " + "; ".join(parts) + "."


def reminder_remove(query: str = "", rid: str = "") -> Dict[str, Any]:
    q = normalize_accents_lower(query)
    with data_txn() as data:
        rs = data.get("reminders", [])
        hit = next((r for r in rs if r.get("active") and (r["id"] == rid or (q and q in normalize_accents_lower(r["text"])))), None)
        if hit:
            rs.remove(hit)
    if not hit:
        return {"ok": False, "spoken": f"Não encontrei o lembrete {query}."}
    log_event("lembrete", f"Lembrete removido: {hit['text']}")
    return {"ok": True, "spoken": f"Lembrete {hit['text']} removido."}


# ---------------------------------------------------------------- listas
def _join_pt(xs: List[str]) -> str:
    xs = [x for x in xs if x]
    return xs[0] if len(xs) == 1 else ", ".join(xs[:-1]) + " e " + xs[-1] if xs else ""


def _split_items(items: str) -> List[str]:
    parts = re.split(r",|;|\s+e\s+|\n", items or "")
    return [" ".join(p.split()) for p in parts if p.strip()]


def _list_key(name: str) -> str:
    n = normalize_accents_lower(name or "compras")
    n = re.sub(r"^(a |de |da |do )?lista( de| da| do)?\s*", "", n).strip()
    return n or "compras"


def list_add(name: str, items: str) -> Dict[str, Any]:
    key, new = _list_key(name), _split_items(items)
    if not new:
        return {"ok": False, "spoken": "O que devo adicionar na lista?"}
    with data_txn() as data:
        lst = data.setdefault("lists", {}).setdefault(key, [])
        existing = {normalize_accents_lower(i["text"]) for i in lst if not i.get("done")}
        added = [it for it in new if normalize_accents_lower(it) not in existing]
        lst.extend({"text": it, "done": False} for it in added)
    log_event("lista", f"Lista {key}: + {', '.join(added) or '(nada novo)'}")
    if not added:
        return {"ok": True, "spoken": f"Isso já está na lista de {key}."}
    return {"ok": True, "spoken": f"Adicionei {_join_pt(added)} na lista de {key}."}


def list_read(name: str) -> Dict[str, Any]:
    key = _list_key(name)
    lst = [i["text"] for i in load_data().get("lists", {}).get(key, []) if not i.get("done")]
    if not lst:
        return {"ok": True, "spoken": f"A lista de {key} está vazia."}
    return {"ok": True, "items": lst, "spoken": f"Na lista de {key} tem {len(lst)} ite{'ns' if len(lst) > 1 else 'm'}: {_join_pt(lst)}."}


def list_remove(name: str, items: str) -> Dict[str, Any]:
    key, targets = _list_key(name), [normalize_accents_lower(x) for x in _split_items(items)]
    removed = []
    with data_txn() as data:
        lst = data.setdefault("lists", {}).setdefault(key, [])
        for it in list(lst):
            n = normalize_accents_lower(it["text"])
            if any(t == n or t in n for t in targets):
                lst.remove(it)
                removed.append(it["text"])
    if not removed:
        return {"ok": False, "spoken": f"Não encontrei {items} na lista de {key}."}
    return {"ok": True, "spoken": f"Tirei {_join_pt(removed)} da lista de {key}."}


def list_clear(name: str) -> Dict[str, Any]:
    key = _list_key(name)
    with data_txn() as data:
        data.setdefault("lists", {})[key] = []
    return {"ok": True, "spoken": f"Lista de {key} limpa."}


# ---------------------------------------------------------------- rotinas
STEP_ALIASES = {
    "falar": "speak", "dizer": "speak", "fala": "speak", "comando": "ask", "pedir": "ask", "volume": "volume",
    "musica": "music", "spotify": "spotify", "briefing": "briefing", "biblia": "bible", "esperar": "wait",
    "aguardar": "wait", "som": "chime", "lista": "list",
}
DEFAULT_ROUTINES = [
    {"name": "Bom dia", "steps_text": "som\nbriefing", "time": "", "days": [0, 1, 2, 3, 4, 5, 6]},
    {"name": "Boa noite", "steps_text": "falar: Boa noite, senhor. Vou pausar a música e baixar o volume.\nspotify: pause\nvolume: 30\ncomando: desliga a luz da sala",
     "time": "", "days": [0, 1, 2, 3, 4, 5, 6]},
    {"name": "Foco", "steps_text": "falar: Modo foco ativado. Vinte e cinco minutos de concentração.\nmusica: lofi para estudar\ncomando: coloca um timer de 25 minutos chamado foco",
     "time": "", "days": [0, 1, 2, 3, 4]},
]


def parse_routine_steps(text: str) -> List[Dict[str, str]]:
    steps = []
    for line in (text or "").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^([A-Za-zÀ-ÿ]+)\s*:?\s*(.*)$", line)
        if not m:
            continue
        kind = STEP_ALIASES.get(normalize_accents_lower(m.group(1)))
        if kind:
            steps.append({"type": kind, "arg": m.group(2).strip()})
    return steps


def _ensure_routines(data: Dict[str, Any]) -> List[Dict[str, Any]]:
    if "routines" not in data:
        data["routines"] = [{"id": _new_id(), **r, "last_run": ""} for r in DEFAULT_ROUTINES]
    return data["routines"]


def run_routine_steps(name: str, steps: List[Dict[str, str]]) -> None:
    """Executa a rotina em segundo plano (ordem preservada, com pausas entre os passos)."""
    def _go() -> None:
        log_event("rotina", f"Executando rotina {name} ({len(steps)} passos)")
        for st in steps:
            t, a = st["type"], st.get("arg", "")
            try:
                if t == "speak" and a:
                    esp_send({"type": "speak", "text": a})
                elif t == "ask" and a:
                    esp_send({"type": "ask", "text": a})
                elif t == "volume":
                    esp_send({"type": "volume", "v": max(0, min(100, int(re.sub(r"\D", "", a) or 50)))})
                elif t == "music" and a:
                    stype = "playlist" if "playlist" in a.lower() else "track"
                    handle_spotify_query({"action": "play_query", "q": a.replace("playlist", "").strip(), "type": stype})
                elif t == "spotify":
                    handle_spotify_query({"action": (a or "pause").split()[0]})
                elif t == "briefing":
                    esp_send({"type": "briefing"})
                elif t == "bible":
                    esp_send({"type": "bible", "action": a or "read_plan_day"})
                elif t == "chime":
                    esp_send({"type": "test", "mode": a or "chime"})
                elif t == "list":
                    esp_send({"type": "speak", "text": list_read(a or "compras")["spoken"]})
                elif t == "wait":
                    time.sleep(max(0.0, min(600.0, float(re.sub(r"[^\d.]", "", a) or 1))))
                    continue
            except Exception as e:
                log_event("rotina", f"Passo '{t}' falhou: {e}", "error")
            time.sleep(0.6)
    threading.Thread(target=_go, daemon=True).start()


def routine_run(query: str = "", rid: str = "") -> Dict[str, Any]:
    q = normalize_accents_lower(query)
    with data_txn() as data:
        rts = _ensure_routines(data)
        hit = next((r for r in rts if r["id"] == rid), None)
        if not hit and q:
            hit = next((r for r in rts if normalize_accents_lower(r["name"]) in q or q in normalize_accents_lower(r["name"])), None)
        if hit:
            hit["last_run"] = datetime.now().isoformat(timespec="minutes")
    if not hit:
        names = ", ".join(r["name"] for r in load_data().get("routines", []))
        return {"ok": False, "spoken": f"Não encontrei a rotina {query}. Rotinas disponíveis: {names}."}
    run_routine_steps(hit["name"], parse_routine_steps(hit["steps_text"]))
    return {"ok": True, "spoken": f"Rotina {hit['name']} iniciada."}


# ---------------------------------------------------------------- datas especiais
def _next_occurrence(spec: str, today: _date) -> Tuple[Optional[_date], Optional[int]]:
    """'MM-DD' ou 'YYYY-MM-DD' (anual). Retorna (proxima data, anos completados nela)."""
    m = re.fullmatch(r"(?:(\d{4})-)?(\d{1,2})-(\d{1,2})", spec or "")
    if not m:
        return None, None
    year0 = int(m.group(1)) if m.group(1) else None
    mo, dd = int(m.group(2)), int(m.group(3))
    for y in (today.year, today.year + 1):
        try:
            d = _date(y, mo, dd)
        except ValueError:
            d = _date(y, mo, 28)
        if d >= today:
            return d, (y - year0 if year0 else None)
    return None, None


def special_dates_view() -> List[Dict[str, Any]]:
    today = _date.today()
    out = []
    for e in load_data().get("special_dates", []):
        if e.get("yearly", True):
            d, years = _next_occurrence(e["date"], today)
        else:
            try:
                d, years = _date.fromisoformat(e["date"]), None
            except ValueError:
                d, years = None, None
        if d is None or d < today:
            continue
        out.append({**e, "next": d.isoformat(), "days_left": (d - today).days, "years": years})
    return sorted(out, key=lambda x: x["days_left"])


def special_date_add(name: str, when: str, yearly: bool = True) -> Dict[str, Any]:
    d = resolve_date(when)
    if not name or not d:
        return {"ok": False, "spoken": "Preciso do nome e da data do evento."}
    m = re.search(r"(\d{4})", when or "")
    spec = (f"{m.group(1)}-{d.month:02d}-{d.day:02d}" if (yearly and m) else f"{d.month:02d}-{d.day:02d}") if yearly else d.isoformat()
    with data_txn() as data:
        data.setdefault("special_dates", []).append({"id": _new_id(), "name": name.strip(), "date": spec, "yearly": bool(yearly)})
    return {"ok": True, "spoken": f"Guardei {name}: {d.day} de {MONTHS_PT[d.month - 1]}{', todo ano' if yearly else ''}."}


def special_date_countdown(query: str = "") -> Dict[str, Any]:
    q = normalize_accents_lower(query)
    items = special_dates_view()
    if q:
        items = [e for e in items if q in normalize_accents_lower(e["name"]) or normalize_accents_lower(e["name"]) in q]
    if not items:
        return {"ok": False, "spoken": "Não encontrei essa data. Cadastre no painel ou diga, por exemplo: guarda o aniversário da Maria em 10 de março."}
    phrases = []
    for e in items[:4]:
        extra = f", completando {e['years']} anos" if e.get("years") else ""
        if e["days_left"] == 0:
            phrases.append(f"hoje é {e['name']}{extra}")
        elif e["days_left"] == 1:
            phrases.append(f"amanhã é {e['name']}{extra}")
        else:
            phrases.append(f"faltam {e['days_left']} dias para {e['name']}{extra}")
    s = "; ".join(phrases)
    return {"ok": True, "spoken": s[:1].upper() + s[1:] + "."}


# ---------------------------------------------------------------- noticias / cotacoes / wikipedia
_NEWS_CACHE: Dict[str, Any] = {}


def get_news(topic: str = "", limit: int = 6) -> Dict[str, Any]:
    key = normalize_accents_lower(topic)
    hit = _NEWS_CACHE.get(key)
    if hit and time.time() - hit["ts"] < 600:
        return hit["data"]
    base = "https://news.google.com/rss"
    url = (f"{base}/search?q={urllib.parse.quote(topic)}&hl=pt-BR&gl=BR&ceid=BR:pt-419" if topic
           else f"{base}?hl=pt-BR&gl=BR&ceid=BR:pt-419")
    code, body = http_request(url, timeout=10)
    items = []
    if code == 200 and body:
        try:
            root = ET.fromstring(body.encode("utf-8"))
            for it in root.iter("item"):
                title = (it.findtext("title") or "").strip()
                src = (it.findtext("source") or "").strip()
                if src and title.endswith(" - " + src):
                    title = title[: -len(src) - 3]
                items.append({"title": html.unescape(title), "source": src, "link": it.findtext("link") or "",
                              "date": it.findtext("pubDate") or ""})
                if len(items) >= limit:
                    break
        except ET.ParseError:
            pass
    data = {"ok": bool(items), "topic": topic, "items": items, "error": "" if items else f"Google News HTTP {code}"}
    if items:
        _NEWS_CACHE[key] = {"ts": time.time(), "data": data}
    return data


def news_spoken(topic: str = "", n: int = 5) -> str:
    nw = get_news(topic)
    if not nw["ok"]:
        return "Não consegui buscar as notícias agora."
    head = f"Notícias sobre {topic}" if topic else "Principais notícias"
    return head + ": " + " ".join(f"{i + 1}. {x['title']}{', do ' + x['source'] if x['source'] else ''}." for i, x in enumerate(nw["items"][:n]))


_QUOTES_CACHE: Dict[str, Any] = {"ts": 0, "data": None}


def get_quotes() -> Dict[str, Any]:
    if _QUOTES_CACHE["data"] and time.time() - _QUOTES_CACHE["ts"] < 300:
        return _QUOTES_CACHE["data"]
    code, body = http_request("https://economia.awesomeapi.com.br/json/last/USD-BRL,EUR-BRL,BTC-BRL", timeout=8)
    try:
        j = json.loads(body) if code == 200 else {}
    except Exception:
        j = {}
    out = []
    for k, label in (("USDBRL", "Dólar"), ("EURBRL", "Euro"), ("BTCBRL", "Bitcoin")):
        q = j.get(k) if isinstance(j, dict) else None
        if isinstance(q, dict) and q.get("bid"):
            out.append({"code": k[:3], "name": label, "bid": float(q["bid"]), "pct": float(q.get("pctChange") or 0)})
    data = {"ok": bool(out), "quotes": out, "error": "" if out else f"AwesomeAPI HTTP {code}"}
    if out:
        _QUOTES_CACHE.update({"ts": time.time(), "data": data})
    return data


def brl_spoken(v: float) -> str:
    if v >= 10000:
        return f"{round(v / 1000):,} mil reais".replace(",", ".")
    reais, cent = int(v), int(round((v - int(v)) * 100))
    if cent == 100:
        reais, cent = reais + 1, 0
    return f"{reais} rea{'is' if reais != 1 else 'l'}" + (f" e {cent} centavos" if cent else "")


def quotes_spoken() -> str:
    q = get_quotes()
    if not q["ok"]:
        return "Não consegui consultar as cotações agora."
    parts = []
    for x in q["quotes"]:
        trend = "alta" if x["pct"] > 0 else ("queda" if x["pct"] < 0 else "estável")
        pct = f"{abs(x['pct']):.1f}".replace(".", ",")
        parts.append(f"{x['name']} a {brl_spoken(x['bid'])}" + (f", {trend} de {pct} por cento" if trend != "estável" else ""))
    return "Cotações agora: " + "; ".join(parts) + "."


def wiki_summary(query: str) -> Dict[str, Any]:
    q = (query or "").strip()
    if not q:
        return {"ok": False, "spoken": "Sobre o que voce quer saber?"}
    title = q
    code, body = http_request("https://pt.wikipedia.org/w/api.php?" + urllib.parse.urlencode(
        {"action": "opensearch", "search": q, "limit": 1, "namespace": 0, "format": "json"}), timeout=8)
    try:
        j = json.loads(body) if code == 200 else None
        if j and len(j) > 1 and j[1]:
            title = j[1][0]
    except Exception:
        pass
    code, body = http_request("https://pt.wikipedia.org/api/rest_v1/page/summary/" + urllib.parse.quote(title.replace(" ", "_")), timeout=8)
    try:
        s = json.loads(body) if code == 200 else {}
    except Exception:
        s = {}
    extract = " ".join(str(s.get("extract") or "").split())
    if not extract:
        return {"ok": False, "spoken": f"Não encontrei {q} na Wikipédia."}
    sentences = re.split(r"(?<=[.!?])\s+", extract)
    short = " ".join(sentences[:3])
    return {"ok": True, "title": s.get("title", title), "extract": extract,
            "url": ((s.get("content_urls") or {}).get("desktop") or {}).get("page", ""),
            "spoken": f"Segundo a Wikipédia, {short}"}


# ---------------------------------------------------------------- dispatcher (voz / ESP32)
def handle_assistant(params: Dict[str, str]) -> Dict[str, Any]:
    a = (params.get("action") or "").strip()
    q = params.get("query", "")
    lst = params.get("list_name", "") or "compras"
    items = params.get("items", "") or q
    if a == "news":
        res = {"ok": True, "spoken": news_spoken(q)}
    elif a == "quotes":
        res = {"ok": True, "spoken": quotes_spoken()}
    elif a == "wiki":
        res = wiki_summary(q)
    elif a == "reminder_add":
        res = reminder_add(q, params.get("date", ""), params.get("time", ""), params.get("repeat", "none"), params.get("minutes"))
    elif a == "reminder_list":
        d = resolve_date(params.get("date", "")) if params.get("date") else None
        res = {"ok": True, "spoken": reminders_spoken(d)}
    elif a == "reminder_remove":
        res = reminder_remove(q)
    elif a == "list_add":
        res = list_add(lst, items)
    elif a == "list_read":
        res = list_read(lst)
    elif a == "list_remove":
        res = list_remove(lst, items)
    elif a == "list_clear":
        res = list_clear(lst)
    elif a == "routine_run":
        res = routine_run(q)
    elif a == "routine_list":
        names = [r["name"] for r in _ensure_routines(load_data())]
        res = {"ok": True, "spoken": "Suas rotinas: " + ", ".join(names) + "."}
    elif a == "date_add":
        res = special_date_add(q, params.get("date", ""), params.get("repeat", "yearly") != "none")
    elif a == "date_countdown":
        res = special_date_countdown(q)
    else:
        res = {"ok": False, "spoken": f"Ação {a} desconhecida no assistente."}
    res.setdefault("action", a)
    log_event("assistente", f"{a}: {res.get('spoken', '')[:150]}")
    return res


# ---------------------------------------------------------------- operacoes do painel
def assistant_data() -> Dict[str, Any]:
    with data_txn() as data:
        rts = _ensure_routines(data)
    return {
        "reminders": reminders_sorted(data),
        "lists": {k: v for k, v in (data.get("lists") or {}).items()},
        "routines": rts,
        "dates": special_dates_view(),
        "all_dates": data.get("special_dates", []),
    }


def assistant_op(b: Dict[str, Any]) -> Dict[str, Any]:
    op = b.get("op", "")
    if op == "reminder_add":
        return reminder_add(b.get("text", ""), b.get("date", ""), b.get("time", ""), b.get("repeat", "none"))
    if op == "reminder_remove":
        return reminder_remove(rid=str(b.get("id", "")))
    if op == "list_add":
        return list_add(b.get("list", "compras"), b.get("items", ""))
    if op in ("list_toggle", "list_remove_index"):
        key = _list_key(b.get("list", ""))
        with data_txn() as data:
            lst = data.setdefault("lists", {}).setdefault(key, [])
            i = int(b.get("index", -1))
            if 0 <= i < len(lst):
                if op == "list_toggle":
                    lst[i]["done"] = not lst[i].get("done")
                else:
                    lst.pop(i)
        return {"ok": True}
    if op == "list_clear_done":
        key = _list_key(b.get("list", ""))
        with data_txn() as data:
            data.setdefault("lists", {})[key] = [i for i in data["lists"].get(key, []) if not i.get("done")]
        return {"ok": True}
    if op == "list_delete":
        with data_txn() as data:
            data.setdefault("lists", {}).pop(_list_key(b.get("list", "")), None)
        return {"ok": True}
    if op == "routine_save":
        name = str(b.get("name", "")).strip()[:40]
        if not name:
            return {"ok": False, "error": "Nome da rotina vazio"}
        days = [int(d) for d in b.get("days", []) if str(d).isdigit() and 0 <= int(d) <= 6]
        tm = resolve_time(b.get("time", "")) if b.get("time") else ""
        with data_txn() as data:
            rts = _ensure_routines(data)
            r = next((x for x in rts if x["id"] == b.get("id")), None)
            if r is None:
                r = {"id": _new_id(), "last_run": ""}
                rts.append(r)
            r.update({"name": name, "steps_text": str(b.get("steps_text", "")), "time": tm or "", "days": days})
        return {"ok": True, "steps": parse_routine_steps(str(b.get("steps_text", "")))}
    if op == "routine_delete":
        with data_txn() as data:
            data["routines"] = [r for r in _ensure_routines(data) if r["id"] != b.get("id")]
        return {"ok": True}
    if op == "routine_run":
        return routine_run(rid=str(b.get("id", "")))
    if op == "date_add":
        return special_date_add(b.get("name", ""), b.get("date", ""), bool(b.get("yearly", True)))
    if op == "date_remove":
        with data_txn() as data:
            data["special_dates"] = [e for e in data.get("special_dates", []) if e["id"] != b.get("id")]
        return {"ok": True}
    return {"ok": False, "error": f"operacao desconhecida: {op}"}


# ---------------------------------------------------------------- agendador
def scheduler_tick(now: Optional[datetime] = None) -> List[str]:
    """Dispara lembretes e rotinas agendadas. Retorna o que foi disparado (para testes/log)."""
    now = now or datetime.now()
    fired: List[str] = []
    to_speak: List[str] = []
    to_run: List[Tuple[str, List[Dict[str, str]]]] = []
    with data_txn() as data:
        for r in data.get("reminders", []):
            if not r.get("active"):
                continue
            due = datetime.fromisoformat(f"{r['date']}T{r['time']}")
            if now < due:
                continue
            late = (now - due).total_seconds()
            if late < 6 * 3600:
                to_speak.append(r["text"] if late < 300 else f"{r['text']} (lembrete das {r['time']}, atrasado)")
                fired.append("lembrete:" + r["text"])
            _advance_reminder(r, now)
        data["reminders"] = [r for r in data.get("reminders", []) if r.get("active") or
                             datetime.fromisoformat(f"{r['date']}T{r['time']}") > now - timedelta(days=7)]
        hhmm, today = now.strftime("%H:%M"), now.date().isoformat()
        for rt in data.get("routines", []):
            if rt.get("time") == hhmm and now.weekday() in rt.get("days", []) and not str(rt.get("last_run", "")).startswith(today + "T" + hhmm):
                rt["last_run"] = now.isoformat(timespec="minutes")
                to_run.append((rt["name"], parse_routine_steps(rt.get("steps_text", ""))))
                fired.append("rotina:" + rt["name"])
    for text in to_speak:
        log_event("lembrete", f"Disparando: {text}")
        esp_send({"type": "test", "mode": "chime"})
        esp_send({"type": "speak", "text": f"Lembrete, senhor: {text}."})
    for name, steps in to_run:
        run_routine_steps(name, steps)
    return fired


def start_scheduler() -> None:
    def _loop() -> None:
        while True:
            try:
                scheduler_tick()
            except Exception as e:
                log_event("agendador", f"erro: {e}", "error")
            time.sleep(20 - (time.time() % 20))
    threading.Thread(target=_loop, daemon=True, name="jarvis-scheduler").start()


# ============================================================================
# VOZ NEURAL EDGE-TTS EM STREAMING POR BLOCOS (TEXTOS LONGOS SEM TRUNCAR)
# ============================================================================
def split_text_for_tts(text: str, first_max: int = 240, max_len: int = 900) -> List[str]:
    """Divide em blocos por frase. O 1o bloco e curto para o audio comecar rapido."""
    text = " ".join((text or "").split())
    if not text:
        return []
    sentences = re.split(r"(?<=[.!?;:…])\s+", text)
    chunks: List[str] = []
    cur = ""
    for s in sentences:
        limit = first_max if not chunks else max_len
        while len(s) > max_len:                       # frase gigante: quebra em virgula/espaco
            cut = s.rfind(", ", 0, max_len)
            if cut > 80:
                cut += 1
            else:
                sp = s.rfind(" ", 0, max_len)
                cut = sp if sp > 0 else max_len
            if cur:
                chunks.append(cur)
                cur = ""
            chunks.append(s[:cut].strip())
            s = s[cut:].strip()
        if cur and len(cur) + 1 + len(s) > limit:
            chunks.append(cur)
            cur = s
        else:
            cur = f"{cur} {s}".strip()
    if cur:
        chunks.append(cur)
    return [c for c in chunks if c]


def edge_tts_pcm16k(text: str, voice: str = "pt-BR-AntonioNeural", rate: str = "+0%") -> bytes:
    """Sintetiza com Edge-TTS (MP3) e decodifica para PCM 16-bit 16 kHz mono via miniaudio."""
    import asyncio
    import edge_tts
    import miniaudio

    async def _synth() -> bytes:
        comm = edge_tts.Communicate(text, voice, rate=rate)
        buf = bytearray()
        async for chunk in comm.stream():
            if chunk["type"] == "audio":
                buf.extend(chunk["data"])
        return bytes(buf)

    mp3 = asyncio.run(_synth())
    if not mp3:
        return b""
    dec = miniaudio.decode(mp3, nchannels=1, sample_rate=16000, output_format=miniaudio.SampleFormat.SIGNED16)
    return dec.samples.tobytes()


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

    def _stream_tts(self, text: str, voice: str = "") -> None:
        """Envia PCM 16 kHz mono em blocos: o ESP32 comeca a falar no 1o bloco enquanto o resto e sintetizado."""
        st = get_settings()
        voice = voice or st["voice"]
        rate = st["rate"]
        chunks = split_text_for_tts(text)
        if not chunks:
            self._send_json({"error": "Texto vazio para TTS"}, 400)
            return
        try:
            first = edge_tts_pcm16k(chunks[0], voice, rate)
        except Exception as e:
            self._send_json({"error": f"Falha no Edge-TTS: {e}"}, 500)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("X-Audio-Format", "pcm_s16le;rate=16000;channels=1")
        if len(chunks) == 1:
            self.send_header("Content-Length", str(len(first)))
        else:
            self.send_header("Connection", "close")   # sem Content-Length: fim do audio = fim da conexao
            self.close_connection = True
        self.end_headers()
        try:
            self.wfile.write(first)
            if len(chunks) > 1:
                with ThreadPoolExecutor(max_workers=1) as pool:
                    fut = pool.submit(edge_tts_pcm16k, chunks[1], voice, rate)
                    for i in range(1, len(chunks)):
                        pcm = fut.result()
                        if i + 1 < len(chunks):
                            fut = pool.submit(edge_tts_pcm16k, chunks[i + 1], voice, rate)   # pre-sintetiza o proximo
                        self.wfile.write(pcm)
                        self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            print("[TTS] ESP32 interrompeu a reproducao (botao).")
        except Exception as e:
            print(f"[TTS] erro no bloco: {e}")

    def _send_bytes(self, raw: bytes, ctype: str, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, fmt: str, *args: Any) -> None:   # silencia o log de polling do painel
        if "/api/state" in self.path or "/api/events" in self.path or "/api/device/heartbeat" in self.path:
            return
        super().log_message(fmt, *args)

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
            try:
                with open(PANEL_HTML_FILE, "rb") as f:
                    self._send_bytes(f.read(), "text/html; charset=utf-8")
            except OSError:
                self._send_html(render_dashboard_html())
        elif path == "/classic":
            self._send_html(render_dashboard_html())
        elif path == "/api/state":
            self._send_json(build_state())
        elif path == "/api/assistant":
            res = handle_assistant(params)
            tts_store_put("last", res.get("spoken", ""))
            self._send_json(compact_for_esp32(res) if params.get("client") == "esp32" else res)
        elif path == "/api/assistant/data":
            self._send_json(assistant_data())
        elif path == "/api/news":
            self._send_json(get_news(params.get("topic", "")))
        elif path == "/api/quotes":
            self._send_json(get_quotes())
        elif path == "/api/wiki":
            self._send_json(wiki_summary(params.get("q", "")))
        elif path == "/api/events":
            self._send_json({"events": get_events(int(params.get("after", "0") or 0))})
        elif path == "/api/weather":
            self._send_json(get_weather(params.get("force") == "1"))
        elif path == "/api/settings":
            self._send_json(get_settings())
        elif path == "/api/voices":
            self._send_json({"voices": PT_VOICES})
        elif path == "/api/briefing":
            res = build_briefing()
            tts_store_put("last", res["spoken"])
            self._send_json(compact_for_esp32(res) if params.get("client") == "esp32" else res)
        elif path == "/api/bible/preview":
            try:
                pday = int(params.get("day", "1") or 1)
            except ValueError:
                pday = 1
            self._send_json(preview_plan_day(params.get("plan", ""), pday))
        elif path == "/api/tts/preview":
            text = params.get("text", "").strip() or "Ola, eu sou o Jarvis."
            if params.get("key"):
                text = tts_store_get(params["key"]) or text
            st = get_settings()
            try:
                mp3 = tts_preview_mp3(text, params.get("voice") or st["voice"], params.get("rate") or st["rate"])
                self._send_bytes(mp3, "audio/mpeg")
            except Exception as e:
                self._send_json({"error": f"Edge-TTS indisponivel: {e}"}, 500)
        elif path == "/api/tts":
            text = params.get("text", "").strip()
            if params.get("key"):
                text = tts_store_get(params["key"]).strip()
            log_event("tts", f"Voz para o ESP32: {len(text)} caracteres")
            self._stream_tts(text, params.get("voice", "").strip())
        elif path == "/api/bible/youversion":
            res = handle_youversion_query(params)
            tts_store_put("last", res.get("spoken", ""))   # texto COMPLETO para /api/tts?key=last
            log_event("biblia", f"{res.get('action')}: {res.get('plan_title') or ''} dia {res.get('day')} ({params.get('client', 'painel')})")
            self._send_json(compact_for_esp32(res) if params.get("client") == "esp32" else res)
        elif path == "/youversion/sync":
            data = load_data()
            data["youversion"]["sync_log"] = []
            sync_youversion_user_plans(data)
            self._send_redirect("/?youversion=synced")
        elif path == "/youversion/debug":
            self._send_json(youversion_debug_report())
        elif path == "/youversion/logout":
            data = load_data()
            yv = data["youversion"]
            yv["logged_in"] = False
            for k in ("session_cookie", "access_token", "refresh_token", "auth_error", "auth_method", "user_id"):
                yv[k] = ""
            yv["token_expires_at"] = 0
            yv["user_plans"] = []
            yv["sync_log"] = []
            save_data(data)
            self._send_redirect("/?youversion=logout")
        elif path == "/api/bible":
            res = fetch_youversion_passage_pt(params.get("ref", "Salmos 23:1-4"), "NVI")
            self._send_json({"ok": True, "reference": res["reference"], "text": res["text"]})
        elif path == "/api/spotify":
            res = handle_spotify_query(params)
            if params.get("action") not in (None, "", "now_playing"):
                log_event("spotify", res.get("message", ""))
            self._send_json(res)
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

        if parsed.path in ("/api/device/heartbeat", "/api/esp/send", "/api/settings", "/api/assistant/op"):
            try:
                body = json.loads(raw_body or "{}")
                if not isinstance(body, dict):
                    raise ValueError("JSON deve ser um objeto")
            except Exception as e:
                self._send_json({"ok": False, "error": f"JSON invalido: {e}"}, 400)
                return
            if parsed.path == "/api/device/heartbeat":
                self._send_json(handle_heartbeat(body, self.client_address[0]))
            elif parsed.path == "/api/esp/send":
                self._send_json(esp_send(body))
            elif parsed.path == "/api/assistant/op":
                self._send_json(assistant_op(body))
            else:
                self._send_json({"ok": True, "settings": update_settings(body)})
            return

        if parsed.path == "/api/tts":
            qs = {k: v[0] for k, v in urllib.parse.parse_qs(parsed.query).items() if v}
            self._stream_tts(raw_body.strip(), qs.get("voice", ""))
            return

        if parsed.path == "/youversion/login":
            form = {k: v[0] for k, v in urllib.parse.parse_qs(raw_body).items() if v}
            username = form.get("username", "").strip()
            password = form.get("password", "")
            if not username or not password:
                self._send_redirect("/?youversion=missing")
                return
            res = youversion_login_with_credentials(username, password)
            log_event("biblia", f"Login YouVersion: {'OK, ' + str(res.get('plans_count')) + ' planos' if res.get('ok') else res.get('error')}",
                      "info" if res.get("ok") else "error")
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
                yv["access_token"] = ""
            tok = form.get("access_token", "").strip()
            if tok.lower().startswith("bearer "):
                tok = tok[7:].strip()
            if tok:
                _yv_apply_token(yv, {"access_token": tok}, "manual_token")
            yv["logged_in"] = bool(yv.get("session_cookie") or yv.get("access_token"))
            yv["auth_error"] = ""
            yv["sync_log"] = []
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
    print(f"👉 Painel classico: http://127.0.0.1:{port}/classic")
    log_event("servidor", f"Bridge Server iniciado na porta {port}")
    start_scheduler()
    print("=================================================================")
    server.serve_forever()
