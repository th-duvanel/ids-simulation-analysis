# Análise e Simulação da Eficácia de Sistemas de Detecção de Intrusão (IDS)

Este repositório contém um projeto desenvolvido para analisar e simular a eficácia de diferentes Sistemas de Detecção de Intrusão (IDS). O objetivo é avaliar o desempenho dos IDS em detectar atividades maliciosas em redes de computadores utilizando-se do simulador NS3.

"Qual é a correlação entre o aumento da taxa de pacotes por segundo (pps) de tráfego de fundo e a taxa de descarte de pacotes em um sensor NIDS, e como essa saturação induzida afeta a probabilidade de detecção de um ataque de baixa taxa (low-rate) concorrente?"

## Como rodar?

Recomendo você seguir todos os passos e colocar a pasta (GitHub) do ns-3 no diretório anterior do diretório principal.
Com o ns-3.46.1 instalado, você primeiro tem que criar um link simbólico do script de simulação na pasta `scratch/` do ns-3:

```bash
cd ../ns-3.46.1/scratch
ln -s ../../ids-simulation-analysis
```

Depois, você tem que compilar o novo módulo do ns-3:

```bash
cd ../ns-3.46.1
./ns3 build
```

Após isso, pode executar o script `run.sh` para rodar a simulação e gerar os resultados:

```bash
cd ../ids-simulation-analysis
./run.sh
```