#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NidsSaturacaoSimV9");

static uint64_t g_totalPacotesFila = 0;
static uint64_t g_totalDescartes = 0;
static uint64_t g_descartesAtaque = 0;
static Ptr<PacketSink> g_sinkLegitimo;

static void PacoteNaFila(Ptr<const Packet> p)
{
    g_totalPacotesFila++;
}

static void PacoteDescartado(Ptr<const Packet> p)
{
    g_totalDescartes++;
    if (g_totalDescartes == 1)
    {
        NS_LOG_UNCOND("--- PRIMEIRO PACOTE DESCARTADO --- O GARGALO ESTÁ FUNCIONANDO!");
    }

    Ptr<Packet> packet = p->Copy();
    Ipv4Header ipv4;
    UdpHeader udp;

    if (packet->RemoveHeader(ipv4))
    {
        if (packet->RemoveHeader(udp))
        {
            if (udp.GetDestinationPort() == 80)
            {
                g_descartesAtaque++;
            }
        }
    }
}

void PacoteDescartadoDisc(Ptr<const QueueDiscItem> item) {
    PacoteDescartado(item->GetPacket());
}

int main(int argc, char* argv[])
{
    double taxaPpsFundoPorCliente = 5000;
    uint32_t packetSizeFundo = 128;
    std::string nidsDataRate = "30Mbps";
    std::string tamanhoFilaNids = "100p";
    uint32_t semente = 1;

    CommandLine cmd;
    cmd.AddValue("taxaPpsFundo", "Taxa (pps) de cada cliente de fundo", taxaPpsFundoPorCliente);
    cmd.AddValue("packetSizeFundo", "Tamanho (bytes) dos pacotes de fundo", packetSizeFundo);
    cmd.AddValue("nidsDataRate", "Capacidade (DataRate) do link do NIDS", nidsDataRate);
    cmd.AddValue("tamanhoFilaNids", "Tamanho da fila (ex: '100p') do NIDS", tamanhoFilaNids);
    cmd.AddValue("semente", "Semente do gerador aleatório", semente);
    cmd.Parse(argc, argv);

    RngSeedManager::SetSeed(semente);
    double simTime = 10.0;

    uint32_t nClientesFundo = 5;
    NodeContainer nosClientesFundo;
    nosClientesFundo.Create(nClientesFundo);

    NodeContainer noAtacante;
    noAtacante.Create(1);

    NodeContainer noClienteLegitimo;
    noClienteLegitimo.Create(1);

    NodeContainer todosClientes;
    todosClientes.Add(nosClientesFundo);
    todosClientes.Add(noAtacante);
    todosClientes.Add(noClienteLegitimo);

    NodeContainer noRoteadorClientes;
    noRoteadorClientes.Create(1);
    Ptr<Node> ptrRoteadorClientes = noRoteadorClientes.Get(0);

    NodeContainer noRoteadorNids;
    noRoteadorNids.Create(1);
    Ptr<Node> ptrRoteadorNids = noRoteadorNids.Get(0);

    NodeContainer noServidor;
    noServidor.Create(1);
    Ptr<Node> ptrServidor = noServidor.Get(0);

    NodeContainer todosNos;
    todosNos.Add(todosClientes);
    todosNos.Add(noRoteadorClientes);
    todosNos.Add(noRoteadorNids);
    todosNos.Add(noServidor);

    PointToPointHelper p2pLinksRapidos;
    p2pLinksRapidos.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2pLinksRapidos.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper p2pLinkNids;
    p2pLinkNids.SetDeviceAttribute("DataRate", StringValue(nidsDataRate));
    p2pLinkNids.SetChannelAttribute("Delay", StringValue("1ms"));
    p2pLinkNids.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(tamanhoFilaNids));

    NetDeviceContainer devsClientesRoteador;
    for (uint32_t i = 0; i < todosClientes.GetN(); ++i)
    {
        NetDeviceContainer link = p2pLinksRapidos.Install(todosClientes.Get(i), ptrRoteadorClientes);
        devsClientesRoteador.Add(link);
    }

    NetDeviceContainer linkNids = p2pLinkNids.Install(ptrRoteadorClientes, ptrRoteadorNids);
    NetDeviceContainer linkServidor = p2pLinksRapidos.Install(ptrRoteadorNids, ptrServidor);

    InternetStackHelper stack;
    stack.Install(todosNos);

    Ipv4AddressHelper addrHelper;
    Ipv4Address ipServidor;
    Ipv4Address ipLegitimo;

    for (uint32_t i = 0; i < todosClientes.GetN(); ++i)
    {
        NetDeviceContainer linkDevices;
        linkDevices.Add(devsClientesRoteador.Get(i * 2));
        linkDevices.Add(devsClientesRoteador.Get(i * 2 + 1));

        std::string subnet = "10.1." + std::to_string(i + 1) + ".0";
        addrHelper.SetBase(Ipv4Address(subnet.c_str()), "255.255.255.252");
        Ipv4InterfaceContainer ifacesLink = addrHelper.Assign(linkDevices);

        if (i == (nClientesFundo + 1))
        {
            ipLegitimo = ifacesLink.GetAddress(0);
        }
    }

    addrHelper.SetBase("10.2.1.0", "255.255.255.252");
    addrHelper.Assign(linkNids);

    addrHelper.SetBase("10.3.1.0", "255.255.255.252");
    Ipv4InterfaceContainer ifaceServidor = addrHelper.Assign(linkServidor);
    ipServidor = ifaceServidor.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t portFundo = 9;
    PacketSinkHelper sinkFundo("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), portFundo));
    ApplicationContainer appsServidor = sinkFundo.Install(noServidor.Get(0));

    uint16_t portAtaque = 80;
    PacketSinkHelper sinkAtaque("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), portAtaque));
    appsServidor.Add(sinkAtaque.Install(noServidor.Get(0)));

    uint16_t portLegitimo = 8080;
    PacketSinkHelper sinkLegitimo("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), portLegitimo));
    ApplicationContainer appSinkLegitimo = sinkLegitimo.Install(noServidor.Get(0));
    g_sinkLegitimo = DynamicCast<PacketSink>(appSinkLegitimo.Get(0));
    appsServidor.Add(appSinkLegitimo);

    appsServidor.Start(Seconds(0.0));
    appsServidor.Stop(Seconds(simTime));

    ApplicationContainer appsClientesFundo;
    std::string dataRateFundo = std::to_string(taxaPpsFundoPorCliente * packetSizeFundo * 8) + "bps";
    OnOffHelper onoffFundo("ns3::UdpSocketFactory", InetSocketAddress(ipServidor, portFundo));
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

    uint32_t packetSizeAtaque = 1024;
    uint32_t totalPacotesAtaque = 100;
    uint32_t totalBytesAtaque = packetSizeAtaque * totalPacotesAtaque;
    OnOffHelper onoffAtaque("ns3::UdpSocketFactory", InetSocketAddress(ipServidor, portAtaque));
    onoffAtaque.SetAttribute("PacketSize", UintegerValue(packetSizeAtaque));
    onoffAtaque.SetAttribute("DataRate", StringValue("500kbps"));
    onoffAtaque.SetAttribute("MaxBytes", UintegerValue(totalBytesAtaque));
    onoffAtaque.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoffAtaque.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer appsAtacante = onoffAtaque.Install(noAtacante.Get(0));
    appsAtacante.Start(Seconds(2.0));
    appsAtacante.Stop(Seconds(simTime - 2.0));

    BulkSendHelper bulkSend("ns3::TcpSocketFactory", InetSocketAddress(ipServidor, portLegitimo));
    bulkSend.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer appClienteLegitimo = bulkSend.Install(noClienteLegitimo.Get(0));
    appClienteLegitimo.Start(Seconds(1.0));
    appClienteLegitimo.Stop(Seconds(simTime));

    Ptr<NetDevice> devNidsSaida = linkNids.Get(0);
    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(devNidsSaida);
    Ptr<Queue<Packet>> filaNids = p2pDev->GetQueue();
    filaNids->TraceConnectWithoutContext("Enqueue", MakeCallback(&PacoteNaFila));

    Ptr<TrafficControlLayer> tc = p2pDev->GetNode()->GetObject<TrafficControlLayer>();
    if (tc != nullptr)
    {
        Ptr<QueueDisc> queueDiscNids = tc->GetRootQueueDiscOnDevice(p2pDev);
        if (queueDiscNids != nullptr)
        {
            queueDiscNids->TraceConnectWithoutContext("Drop", MakeCallback(&PacoteDescartadoDisc));
        }
        else
        {
            NS_LOG_UNCOND("Nenhum QueueDisc encontrado no dispositivo NIDS");
        }
    }
    else
    {
        NS_LOG_UNCOND("Nenhum TrafficControlLayer encontrado no node do dispositivo NIDS");
    }

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    NS_LOG_UNCOND("--- Contadores Finais ---");
    NS_LOG_UNCOND("Total Pacotes Enfileirados: " << g_totalPacotesFila);
    NS_LOG_UNCOND("Total Pacotes Descartados: " << g_totalDescartes);
    NS_LOG_UNCOND("Total Ataques Descartados (FN): " << g_descartesAtaque);

    double taxaDescarteGeral = 0.0;
    if (g_totalPacotesFila > 0)
    {
        taxaDescarteGeral = (double)g_totalDescartes / g_totalPacotesFila;
    }

    double taxaFalsosNegativos = (double)g_descartesAtaque / totalPacotesAtaque;

    double throughputLegitimoMbps = 0.0;
    if (g_sinkLegitimo)
    {
        uint64_t totalBytesRecebidos = g_sinkLegitimo->GetTotalRx();
        NS_LOG_UNCOND("Total Bytes TCP Recebidos: " << totalBytesRecebidos);
        double duracaoApp = simTime - 1.0;
        throughputLegitimoMbps = (totalBytesRecebidos * 8.0) / (duracaoApp * 1000000.0);
    }

    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    double mediaLatencia = 0.0;

    for (auto const& kv : stats)
    {
        uint32_t flowId = kv.first;
        FlowMonitor::FlowStats flowStats = kv.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);

        if (t.sourceAddress == ipLegitimo &&
            t.destinationAddress == ipServidor &&
            t.destinationPort == portLegitimo)
        {
            if (flowStats.rxPackets > 0)
            {
                mediaLatencia = flowStats.delaySum.GetSeconds() / flowStats.rxPackets;
                NS_LOG_UNCOND("Latência Média TCP: " << mediaLatencia << "s");
            }
            break;
        }
    }

    std::cout << "RESULT,"
              << taxaDescarteGeral << ","
              << taxaFalsosNegativos << ","
              << throughputLegitimoMbps << ","
              << mediaLatencia << std::endl;

    Simulator::Destroy();
    return 0;
}
