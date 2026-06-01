# ==========================================================
# MycoNet - Backend Flask
# Ponte entre FIWARE e Dashboard Web
# PINK CODE - Global Solution 2026
#
# Este backend tem duas funções principais:
# 1. Buscar dados atuais no Orion Context Broker
# 2. Buscar histórico de sensores no STH-Comet
#
# O dashboard nunca acessa o FIWARE diretamente,
# toda a comunicação passa por esta API.
# ==========================================================

# Autoras:
# Lara Mofid Essa Alssabak - RM567947
# Maria Luisa Boucinhas Franco - RM567355
# Maria Luiza Kochnoff da Matta - RM568459
# Roberta Moreira dos Santos - RM567825

from flask import Flask, jsonify
from flask_cors import CORS
import requests

# Criação da aplicação Flask
app = Flask(__name__)

# Permite requisições do dashboard hospedado em outro endereço
CORS(app)

# ==========================================================
# CONFIGURAÇÃO DOS SERVIÇOS FIWARE
# ==========================================================

# IP da máquina virtual onde os containers estão executando.
# Caso o Flask rode na mesma máquina do FIWARE, pode ser usado localhost.
IP = "35.188.129.19"

# Endereço do Orion Context Broker
ORION = f"http://{IP}:1026"

# Endereço do STH-Comet responsável pelo histórico
STH = f"http://{IP}:8666"

# Cabeçalhos obrigatórios para identificar o serviço FIWARE
HEADERS = {
    "fiware-service": "smart",
    "fiware-servicepath": "/"
}

# ==========================================================
# MAPEAMENTO DE NOMES
# ==========================================================
# Converte os IDs cadastrados no FIWARE para nomes mais
# amigáveis que serão exibidos no dashboard.

NAMES = {
    "HabitatA001": "HABITAT_A",
    "HabitatB001": "HABITAT_B",
    "Lab001": "LAB",
    "Garagem001": "GARAGEM",
    "Energia001": "ENERGIA",
}

# ==========================================================
# ENDPOINT: /api/modules
# ==========================================================
# Retorna o estado atual de todos os módulos da colônia.
#
# O dashboard utiliza estes dados para:
# - Atualizar os sensores
# - Verificar status dos módulos
# - Detectar módulos offline
# - Atualizar a rede MycoNet
# ==========================================================

@app.route("/api/modules")
def modules():

    # options=dateModified faz o Orion retornar
    # a data da última atualização de cada atributo
    r = requests.get(
        f"{ORION}/v2/entities?type=Module&options=dateModified",
        headers=HEADERS,
        timeout=5
    )

    out = []

    # Percorre todos os módulos encontrados no Orion
    for e in r.json():

        # Remove o prefixo do URN para obter apenas o ID
        ent = e["id"].split(":")[-1]

        # Função auxiliar para extrair o valor de um atributo
        # sem precisar repetir código várias vezes
        def gv(a):
            x = e.get(a)
            return x.get("value") if isinstance(x, dict) else None

        # Procura a data mais recente entre todos os atributos
        # do módulo para identificar quando foi a última atualização
        times = []

        for a, v in e.items():
            if isinstance(v, dict):

                dm = v.get("metadata", {}).get(
                    "dateModified", {}
                )

                if dm.get("value"):
                    times.append(dm["value"])

        # Monta o objeto que será enviado ao dashboard
        out.append({
            "nm":      NAMES.get(ent, ent),
            "temp":    gv("temperature"),
            "hum":     gv("humidity"),
            "luz":     gv("luminosity"),
            "gas":     gv("gasLevel"),
            "status":  gv("environmentStatus"),
            "alert":   gv("alertStatus"),
            "trigger": gv("triggerViolated"),

            # Utilizado pelo dashboard para detectar
            # módulos sem atualização recente
            "ts": max(times) if times else None,
        })

    return jsonify(out)

# ==========================================================
# ENDPOINT: /api/history/<entidade>/<attr>
# ==========================================================
# Retorna os últimos registros históricos de um sensor.
#
# Exemplo:
# /api/history/HabitatA001/temperature
#
# Utilizado para alimentar os gráficos de tendência
# exibidos no dashboard.
# ==========================================================

@app.route("/api/history/<entidade>/<attr>")
def history(entidade, attr):

    # Consulta ao STH-Comet solicitando
    # os 60 registros mais recentes
    url = (
        f"{STH}/STH/v1/contextEntities/type/Module/"
        f"id/urn:ngsi-ld:Module:{entidade}/attributes/{attr}?lastN=60"
    )

    r = requests.get(
        url,
        headers=HEADERS,
        timeout=5
    )

    try:
        # Estrutura padrão retornada pelo STH-Comet
        pts = (
            r.json()["contextResponses"][0]
            ["contextElement"]["attributes"][0]
            ["values"]
        )

    except (KeyError, IndexError):

        # Caso não exista histórico disponível
        pts = []

    return jsonify(pts)

# ==========================================================
# INICIALIZAÇÃO DA APLICAÇÃO
# ==========================================================
# Disponibiliza a API para o dashboard.
# Escuta em todas as interfaces da máquina na porta 5000.
# ==========================================================

if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000
    )
