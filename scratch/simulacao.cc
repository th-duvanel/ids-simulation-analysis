#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
// #include "ns3/point-to-point-layout-module.h" // --- MUDANÇA: Não precisamos mais disto
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"

//
// Simulação de Saturação de NIDS (v3 - Corrigido para ns-3.46.1)
//
// Topologia:
// [5 Clientes Fundo] ----.
//                       |
// [1 Cliente Ataque] ---+---> [Roteador/NIDS] ---> [Servidor Vítima]
//
// NIDS: Modelado como uma fila "DropTailQueue" com limite de *pacotes*.
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NidsSaturacaoSim");

// --- Contadores Globais para Métricas ---
static uint64_t g_totalPacotesFila = 0;
static uint64_t g_totalDescartes = 0;
static uint64_t g_descartesAtaque = 0; // Pacotes de ataque (Porta 80) descartados

// --- Callbacks (Funções de Coleta de Dados) ---

static void PacoteNaFila(Ptr<const Packet> p)
{
    g_totalPacotesFila++;
}

static void PacoteDescartado(Ptr<const Packet> p)
{
    g_totalDescartes++;
    Ptr<Packet> packet = p->Copy();
    Ipv4Header ipv4;
    UdpHeader udp;
    if (packet->PeekHeader(ipv4) && packet->PeekHeader(udp))
    {
        if (udp.GetDestinationPort() == 80)
        {
            g_descartesAtaque++;
        }
    }
}

// --- Função Principal ---

int main(int argc, char* argv[])
{
    // --- 1. Configuração dos Parâmetros ---
    double taxaPpsFundoPorCliente = 5000;
    uint32_t tamanhoFilaNids = 100;
    uint32_t semente = 1;
    uint32_t packetSizeFundo = 128;
    std::string nidsDataRate = "30Mbps";

    CommandLine cmd;
    cmd.AddValue("taxaPpsFundo", "Taxa de pacotes por segundo (pps) para cada cliente de fundo", taxaPpsFundoPorCliente);
    cmd.AddValue("tamanhoFila", "Tamanho da fila do NIDS em pacotes", tamanhoFilaNids);
    cmd.AddValue("semente", "Semente do gerador aleatório", semente);
    cmd.AddValue("nidsDataRate", "Capacidade (DataRate) do link do NIDS", nidsDataRate);
    cmd.AddValue("packetSizeFundo", "Tamanho do pacote de fundo em bytes", packetSizeFundo);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(semente);
    double simTime = 10.0;

    // --- 2. Definição da Topologia ---
    uint32_t nClientesFundo = 5;
    NodeContainer nosClientesFundo;
    nosClientesFundo.Create(nClientesFundo);

    NodeContainer noAtacante;
    noAtacante.Create(1);

    NodeContainer noRoteadorNids;
    noRoteadorNids.Create(1);
    Ptr<Node> ptrRoteador = noRoteadorNids.Get(0); // Pega o ponteiro do roteador

    NodeContainer noServidor;
    noServidor.Create(1);
    Ptr<Node> ptrServidor = noServidor.Get(0); // Pega o ponteiro do servidor

    NodeContainer todosClientes;
    todosClientes.Add(nosClientesFundo);
    todosClientes.Add(noAtacante);

    // --- 3. Configuração dos Links (Ponto a Ponto) ---
    // --- MUDANÇA: Lógica de links reescrita (sem StarHelper) ---

    // Helper para o link do hub (roteador) para o servidor (link de saida)
    PointToPointHelper p2pRoteadorServidor;
    p2pRoteadorServidor.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2pRoteadorServidor.SetChannelAttribute("Delay", StringValue("5ms"));

    // Helper para os links dos clientes (links de entrada do NIDS)
    PointToPointHelper p2pClienteRoteador;
    p2pClienteRoteador.SetDeviceAttribute("DataRate", StringValue(nidsDataRate));
    p2pClienteRoteador.SetChannelAttribute("Delay", StringValue("5ms"));
    
    // A MÁGICA: Definimos a fila do NIDS
    p2pClienteRoteador.SetQueue("ns3::DropTailQueue",
                                "MaxSize", StringValue(std::to_string(tamanhoFilaNids) + "p"));

    // Instala o link Roteador -> Servidor
    NetDeviceContainer devLinkServidor = p2pRoteadorServidor.Install(ptrRoteador, ptrServidor);

    // Instala os links Clientes -> Roteador (usando um loop)
    NetDeviceContainer devsRoteadorParaClientes;
    NetDeviceContainer devsClientes;

    for (uint32_t i = 0; i < todosClientes.GetN(); ++i)
    {
        NetDeviceContainer link = p2pClienteRoteador.Install(todosClientes.Get(i), ptrRoteador);
        
        devsClientes.Add(link.Get(0)); // Device no lado do cliente
        devsRoteadorParaClientes.Add(link.Get(1)); // Device no lado do roteador
    }

    // --- 4. Configuração da Pilha de Rede (Internet) ---
    InternetStackHelper stack;
    stack.Install(todosClientes);
    stack.Install(noRoteadorNids);
    stack.Install(noServidor);

    // --- MUDANÇA: Lógica de IPs mantida da v2 (é a forma correta) ---
    Ipv4AddressHelper addrHelper;

    // Link Roteador -> Servidor
    addrHelper.SetBase("10.1.1.0", "255.255.255.252");
    Ipv4InterfaceContainer ifaceLinkServidor = addrHelper.Assign(devLinkServidor);

    // Links Clientes -> Roteador (cada um é uma sub-rede /30)
    Ipv4InterfaceContainer ifacesClientes;
    for (uint32_t i = 0; i < devsClientes.GetN(); ++i)
    {
        std::string subnet = "10.2." + std::to_string(i + 1) + ".0";
        addrHelper.SetBase(Ipv4Address(subnet.c_str()), "255.255.255.252"); 
        
        NetDeviceContainer linkDevices;
        linkDevices.Add(devsClientes.Get(i)); // spoke (cliente)
        linkDevices.Add(devsRoteadorParaClientes.Get(i)); // hub (roteador)
        
        Ipv4InterfaceContainer ifacesLink = addrHelper.Assign(linkDevices);
        ifacesClientes.Add(ifacesLink.Get(0)); // Salva a interface do cliente
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // --- 5. Configuração das Aplicações (Tráfego) ---
    Ipv4Address ipServidor = ifaceLinkServidor.GetAddress(1); // IP do Servidor

    // --- Servidor (Sinks) ---
    uint16_t portFundo = 9;
    PacketSinkHelper sinkFundo("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), portFundo));
    ApplicationContainer appsServidor = sinkFundo.Install(noServidor.Get(0));

    uint16_t portAtaque = 80;
    PacketSinkHelper sinkAtaque("ns3::UdpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), portAtaque));
    appsServidor.Add(sinkAtaque.Install(noServidor.Get(0)));
    appsServidor.Start(Seconds(0.0));
    appsServidor.Stop(Seconds(simTime));

    // --- Clientes de Fundo (VI) ---
    ApplicationContainer appsClientesFundo;
    
    std::string dataRateFundo = std::to_string(taxaPpsFundoPorCliente * packetSizeFundo * 8) + "bps";

    OnOffHelper onoffFundo("ns3::UdpSocketFactory",
                           InetSocketAddress(ipServidor, portFundo));
    onoffFundo.SetAttribute("PacketSize", UintegerValue(packetSizeFundo));
    onoffFundo.SetAttribute("DataRate", StringValue(dataRateFundo));
    onoffFundo.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoffFundo.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    for (uint32_t i = 0; i < nClientesFundo; ++i)
    {
        appsClientesFundo.Add(onoffFundo.Install(nosClientesFundo.Get(i)));
    }
    appsClientesFundo.Start(Seconds(1.0));
    appsClientesFundo.Stop(Seconds(simTime - 1.0));

    // --- Cliente Atacante (Tráfego Alvo) ---
    // --- Cliente Atacante (Tráfego Alvo) ---
    uint32_t packetSizeAtaque = 1024; // Pacote "SQLi"
    uint32_t totalPacotesAtaque = 100; // Denominador Fixo para VD2
    
    // Calcula o total de bytes a serem enviados
    uint32_t totalBytesAtaque = packetSizeAtaque * totalPacotesAtaque;

    OnOffHelper onoffAtaque("ns3::UdpSocketFactory", // Garanta que aqui é UdpSocketFactory
                            InetSocketAddress(ipServidor, portAtaque));
    onoffAtaque.SetAttribute("PacketSize", UintegerValue(packetSizeAtaque));
    onoffAtaque.SetAttribute("DataRate", StringValue("500kbps")); // Taxa baixa
    onoffAtaque.SetAttribute("MaxBytes", UintegerValue(totalBytesAtaque)); // <-- CORREÇÃO
    onoffAtaque.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoffAtaque.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

    ApplicationContainer appsAtacante = onoffAtaque.Install(noAtacante.Get(0));
    appsAtacante.Start(Seconds(2.0));
    appsAtacante.Stop(Seconds(simTime - 2.0));


    // --- 6. Coleta de Dados (TraceConnect) ---
    // A lógica está correta, pois 'devsRoteadorParaClientes' foi preenchido no loop
    for (uint32_t i = 0; i < devsRoteadorParaClientes.GetN(); ++i)
    {
        Ptr<NetDevice> devNids = devsRoteadorParaClientes.Get(i);
        Ptr<PointToPointNetDevice> p2pDevNids = DynamicCast<PointToPointNetDevice>(devNids);
        Ptr<Queue<Packet>> filaNids = p2pDevNids->GetQueue();

        filaNids->TraceConnectWithoutContext("Enqueue", MakeCallback(&PacoteNaFila));
        filaNids->TraceConnectWithoutContext("Drop", MakeCallback(&PacoteDescartado));
    }


    // --- 7. Execução e Resultados ---
    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // --- Cálculo das Métricas Finais ---
    double taxaDescarteGeral = 0.0;
    if (g_totalPacotesFila > 0) // Evita divisão por zero
    {
        taxaDescarteGeral = (double)g_totalDescartes / g_totalPacotesFila;
    }
    
    double taxaDeteccaoAtaque = (double)(totalPacotesAtaque - g_descartesAtaque) / totalPacotesAtaque;

    std::cout << "RESULT,"
              << taxaPpsFundoPorCliente * nClientesFundo << "," // VI (Total PPS)
              << taxaDescarteGeral << ","                     // VD1
              << taxaDeteccaoAtaque << std::endl;             // VD2

    Simulator::Destroy();
    return 0;
}