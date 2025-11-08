#!/bin/bash

# --- CONFIGURAÇÃO GLOBAL DO EXPERIMENTO ---
# Mude esta variável para testar diferentes "modelos" de NIDS
# (para o Estudo 3: Custo-Benefício)
NIDS_CAPACITY="30Mbps"
# NIDS_CAPACITY="100Mbps" # (Para um teste futuro)
# ------------------------------------------

NIDS_CPU_PPS=2000    # Capacidade de inspeção do NIDS (pps) - fail-open (baixo para evidenciar FNs)

NS3_DIR="../ns-3.46.1"
NS3_RUN="$NS3_DIR/ns3 run"
SIM_SCRIPT="simulacao"
OUTPUT_FILE="analysis/resultados.csv"

if [ ! -d "$NS3_DIR" ]; then
    echo "Erro: Diretório do ns-3 não encontrado em $NS3_DIR"
    exit 1
fi

echo "Iniciando experimento completo para o artigo..."
echo "TESTANDO NIDS COM CAPACIDADE DE: $NIDS_CAPACITY"

# Limpa resultados antigos e escreve o novo cabeçalho
echo "estudo,cenario,nids_cpu_pps,taxa_pps_total,taxa_bps_total,taxa_descarte_geral,taxa_falsos_negativos,throughput_legitimo_mbps,latencia_media_s" > $OUTPUT_FILE

NUM_CLIENTES=5
SEMENTES=$(seq 1 5) # 5 rodadas por ponto de dados para média
TAMANHO_FILA_BASELINE="1000000p" # Fila "infinita"
CAPACIDADE_BASELINE="10Gbps"     # DataRate "infinita"

# --- ESTUDO 1: A Natureza da Carga (pps vs bps) ---
echo "--- Iniciando Estudo 1: pps vs bps ---"
TAMANHO_FILA_ESTUDO_1="100p" # Fila fixa de 100 pacotes
PACKET_SIZES=(128 1400)      # (Alto-PPS vs Alto-BPS)
CPU_SWEEP=(1000 2000 5000 10000) # Capacidades de CPU do NIDS para varrer

for pps_por_cliente in $(seq 1000 1000 10000); do
    for size in "${PACKET_SIZES[@]}"; do
        for cpu in "${CPU_SWEEP[@]}"; do
            NIDS_CPU_PPS=$cpu
        
        if [ $size -eq 128 ]; then cenario="alto_pps_128b"; else cenario="alto_bps_1400b"; fi
        
        taxa_total_pps=$(($pps_por_cliente * $NUM_CLIENTES))
        taxa_total_bps=$(( $taxa_total_pps * $size * 8 )) 
        
        echo "Rodando Estudo 1 ($cenario) | cpu=$NIDS_CPU_PPS pps | pps_total=$taxa_total_pps | fila=$TAMANHO_FILA_ESTUDO_1"

        for i in $SEMENTES; do
            # Cenário de Gargalo (Usa NIDS_CAPACITY)
            RESULTADO=$($NS3_RUN "$SIM_SCRIPT --taxaPpsFundo=$pps_por_cliente --semente=$i --tamanhoFilaNids=$TAMANHO_FILA_ESTUDO_1 --packetSizeFundo=$size --nidsDataRate=$NIDS_CAPACITY --nidsCpuPps=$NIDS_CPU_PPS" | grep "RESULT")
            echo "estudo_1,gargalo_$cenario,$NIDS_CPU_PPS,$taxa_total_pps,$taxa_total_bps,${RESULTADO#*,}" >> $OUTPUT_FILE
            
            # Cenário Baseline (Usa capacidade e fila "infinitas")
            RESULTADO_BASE=$($NS3_RUN "$SIM_SCRIPT --taxaPpsFundo=$pps_por_cliente --semente=$i --tamanhoFilaNids=$TAMANHO_FILA_BASELINE --packetSizeFundo=$size --nidsDataRate=$CAPACIDADE_BASELINE --nidsCpuPps=100000000" | grep "RESULT")
            echo "estudo_1,baseline_$cenario,100000000,$taxa_total_pps,$taxa_total_bps,${RESULTADO_BASE#*,}" >> $OUTPUT_FILE
        done
        done
    done
done


# --- ESTUDO 2: A Resiliência do NIDS (Capacidade da Fila) ---
echo "--- Iniciando Estudo 2: Resiliência da Fila ---"
PACKET_SIZE_FIXO=128 # Pior caso (Alto-PPS)
TAMANHOS_FILA=(50 100 250) # (NIDS Barato, Padrão, Caro)
CPU_SWEEP2=(1000 2000 5000 10000)

for pps_por_cliente in $(seq 1000 1000 10000); do
    for fila in "${TAMANHOS_FILA[@]}"; do
        for cpu in "${CPU_SWEEP2[@]}"; do
            NIDS_CPU_PPS=$cpu
        
        taxa_total_pps=$(($pps_por_cliente * $NUM_CLIENTES))
        taxa_total_bps=$(( $taxa_total_pps * $PACKET_SIZE_FIXO * 8 ))
        cenario="fila_$fila"
        
        echo "Rodando Estudo 2 ($cenario) | cpu=$NIDS_CPU_PPS pps | pps_total=$taxa_total_pps"

        for i in $SEMENTES; do
            # Cenário de Gargalo (Usa NIDS_CAPACITY)
            RESULTADO=$($NS3_RUN "$SIM_SCRIPT --taxaPpsFundo=$pps_por_cliente --semente=$i --tamanhoFilaNids=${fila}p --packetSizeFundo=$PACKET_SIZE_FIXO --nidsDataRate=$NIDS_CAPACITY --nidsCpuPps=$NIDS_CPU_PPS" | grep "RESULT")
            echo "estudo_2,$cenario,$NIDS_CPU_PPS,$taxa_total_pps,$taxa_total_bps,${RESULTADO#*,}" >> $OUTPUT_FILE
        done
        done
    done
done

# Adiciona a Baseline para o Estudo 2 
echo "Rodando Estudo 2 (Baseline)..."
for pps_por_cliente in $(seq 1000 1000 10000); do
    taxa_total_pps=$(($pps_por_cliente * $NUM_CLIENTES))
    taxa_total_bps=$(( $taxa_total_pps * $PACKET_SIZE_FIXO * 8 ))
    for i in $SEMENTES; do
        # Cenário Baseline (Usa capacidade e fila "infinitas")
    RESULTADO_BASE=$($NS3_RUN "$SIM_SCRIPT --taxaPpsFundo=$pps_por_cliente --semente=$i --tamanhoFilaNids=$TAMANHO_FILA_BASELINE --packetSizeFundo=$PACKET_SIZE_FIXO --nidsDataRate=$CAPACIDADE_BASELINE --nidsCpuPps=100000000" | grep "RESULT")
    echo "estudo_2,baseline,100000000,$taxa_total_pps,$taxa_total_bps,${RESULTADO_BASE#*,}" >> $OUTPUT_FILE
    done
done


echo "----------------------------------------------------"
echo "Experimento COMPLETO concluído (NIDS @ $NIDS_CAPACITY)."
echo "Resultados para o artigo salvos em $OUTPUT_FILE"