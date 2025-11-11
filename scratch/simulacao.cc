#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ppp-header.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("NidsSaturationSimV9");

static uint64_t g_totalQueuePackets = 0;
static uint64_t g_totalDrops = 0;
static Ptr<PacketSink> g_legitimateSink;
static Ptr<PacketSink> g_attackSink;

static double g_nidsCpuPps = 50000.0;
static double g_availableTokens = 0.0;
static uint64_t g_totalInspected = 0;
static uint64_t g_totalCpuBypass = 0;
static uint64_t g_totalAttacksCpuBypass = 0;
static uint64_t g_totalAttacksInspected = 0;
static uint64_t g_totalAttacksDetected = 0;
static uint64_t g_totalAttacksSent = 0;

static void RefillCpuTokens()
{
    g_availableTokens = g_nidsCpuPps;
    Simulator::Schedule(Seconds(1.0), &RefillCpuTokens);
}

static void OnPacketEnqueued(Ptr<const Packet> p)
{
    g_totalQueuePackets++;
}

static void OnPacketDropped(Ptr<const Packet> p)
{
    g_totalDrops++;
}

void OnPacketDroppedDisc(Ptr<const QueueDiscItem> item) {
    OnPacketDropped(item->GetPacket());
}

static void OnNidsPacketReceived(Ptr<const Packet> p)
{
    bool isAttack = false;
    Ptr<Packet> copy = p->Copy();
    PppHeader ppp;
    if (copy->RemoveHeader(ppp))
    {
        const uint16_t PPP_PROTO_IPV4 = 0x0021;
        if (ppp.GetProtocol() == PPP_PROTO_IPV4)
        {
            Ipv4Header ip4;
            if (copy->RemoveHeader(ip4))
            {
                const uint8_t PROTO_UDP = 17;
                if (ip4.GetProtocol() == PROTO_UDP)
                {
                    UdpHeader udp;
                    if (copy->RemoveHeader(udp))
                    {
                        isAttack = (udp.GetDestinationPort() == 80);
                    }
                }
            }
        }
    }

    if (g_availableTokens >= 1.0)
    {
        g_availableTokens -= 1.0;
        g_totalInspected++;
        if (isAttack)
        {
            g_totalAttacksInspected++;
            g_totalAttacksDetected++;
        }
    }
    else
    {
        g_totalCpuBypass++;
        if (isAttack)
        {
            g_totalAttacksCpuBypass++;
        }
    }
}

static void OnNidsDequeue(Ptr<const Packet> p)
{
    OnNidsPacketReceived(p);
}

int main(int argc, char* argv[])
{
    double backgroundPpsPerClient = 5000;
    uint32_t backgroundPacketSize = 128;
    std::string nidsDataRate = "30Mbps";
    std::string nidsQueueSize = "100p";
    uint32_t seed = 1;
    double nidsCpuPps = 50000.0;

    CommandLine cmd;
    cmd.AddValue("backgroundPps", "Taxa (pps) de cada cliente de fundo", backgroundPpsPerClient);
    cmd.AddValue("backgroundPktSize", "Tamanho (bytes) dos pacotes de fundo", backgroundPacketSize);
    cmd.AddValue("nidsDataRate", "Capacidade (DataRate) do link do NIDS", nidsDataRate);
    cmd.AddValue("nidsQueueSize", "Tamanho da fila (ex: '100p') do NIDS", nidsQueueSize);
    cmd.AddValue("seed", "Semente do gerador aleatório", seed);
    cmd.AddValue("nidsCpuPps", "Capacidade de inspeção do NIDS (pps)", nidsCpuPps);
    cmd.Parse(argc, argv);

    g_nidsCpuPps = nidsCpuPps;
    g_availableTokens = g_nidsCpuPps;
    Simulator::Schedule(Seconds(1.0), &RefillCpuTokens);

    RngSeedManager::SetSeed(seed);
    double simTime = 10.0;

    uint32_t numBackgroundClients = 5;
    NodeContainer backgroundClientNodes;
    backgroundClientNodes.Create(numBackgroundClients);

    NodeContainer attackerNode;
    attackerNode.Create(1);

    NodeContainer legitimateClientNode;
    legitimateClientNode.Create(1);

    NodeContainer allClientNodes;
    allClientNodes.Add(backgroundClientNodes);
    allClientNodes.Add(attackerNode);
    allClientNodes.Add(legitimateClientNode);

    NodeContainer clientRouterNode;
    clientRouterNode.Create(1);
    Ptr<Node> clientRouterPtr = clientRouterNode.Get(0);

    NodeContainer nidsRouterNode;
    nidsRouterNode.Create(1);
    Ptr<Node> nidsRouterPtr = nidsRouterNode.Get(0);

    NodeContainer serverNode;
    serverNode.Create(1);
    Ptr<Node> serverPtr = serverNode.Get(0);

    NodeContainer allNodes;
    allNodes.Add(allClientNodes);
    allNodes.Add(clientRouterNode);
    allNodes.Add(nidsRouterNode);
    allNodes.Add(serverNode);

    PointToPointHelper fastP2pLinks;
    fastP2pLinks.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    fastP2pLinks.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper nidsP2pLink;
    nidsP2pLink.SetDeviceAttribute("DataRate", StringValue(nidsDataRate));
    nidsP2pLink.SetChannelAttribute("Delay", StringValue("1ms"));
    nidsP2pLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(nidsQueueSize));

    NetDeviceContainer clientRouterDevices;
    for (uint32_t i = 0; i < allClientNodes.GetN(); ++i)
    {
        NetDeviceContainer link = fastP2pLinks.Install(allClientNodes.Get(i), clientRouterPtr);
        clientRouterDevices.Add(link);
    }

    NetDeviceContainer nidsLinkDevices = nidsP2pLink.Install(clientRouterPtr, nidsRouterPtr);
    NetDeviceContainer serverLinkDevices = fastP2pLinks.Install(nidsRouterPtr, serverPtr);

    InternetStackHelper stack;
    stack.Install(allNodes);

    Ipv4AddressHelper addrHelper;
    Ipv4Address serverIp;
    Ipv4Address legitimateIp;

    for (uint32_t i = 0; i < allClientNodes.GetN(); ++i)
    {
        NetDeviceContainer linkDevices;
        linkDevices.Add(clientRouterDevices.Get(i * 2));
        linkDevices.Add(clientRouterDevices.Get(i * 2 + 1));

        std::string subnet = "10.1." + std::to_string(i + 1) + ".0";
        addrHelper.SetBase(Ipv4Address(subnet.c_str()), "255.255.255.252");
        Ipv4InterfaceContainer linkInterfaces = addrHelper.Assign(linkDevices);

        if (i == (numBackgroundClients + 1))
        {
            legitimateIp = linkInterfaces.GetAddress(0);
        }
    }

    addrHelper.SetBase("10.2.1.0", "255.255.255.252");
    addrHelper.Assign(nidsLinkDevices);

    addrHelper.SetBase("10.3.1.0", "255.255.255.252");
    Ipv4InterfaceContainer serverInterface = addrHelper.Assign(serverLinkDevices);
    serverIp = serverInterface.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t backgroundPort = 9;
    PacketSinkHelper backgroundSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), backgroundPort));
    ApplicationContainer serverApps = backgroundSinkHelper.Install(serverNode.Get(0));

    uint16_t attackPort = 80;
    PacketSinkHelper attackSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), attackPort));
    ApplicationContainer attackSinkApp = attackSinkHelper.Install(serverNode.Get(0));
    g_attackSink = DynamicCast<PacketSink>(attackSinkApp.Get(0));
    serverApps.Add(attackSinkApp);

    uint16_t legitimatePort = 8080;
    PacketSinkHelper legitimateSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), legitimatePort));
    ApplicationContainer legitimateSinkApp = legitimateSinkHelper.Install(serverNode.Get(0));
    g_legitimateSink = DynamicCast<PacketSink>(legitimateSinkApp.Get(0));
    serverApps.Add(legitimateSinkApp);

    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simTime));

    ApplicationContainer backgroundClientApps;
    std::string backgroundDataRate = std::to_string(backgroundPpsPerClient * backgroundPacketSize * 8) + "bps";
    OnOffHelper backgroundOnOffHelper("ns3::UdpSocketFactory", InetSocketAddress(serverIp, backgroundPort));
    backgroundOnOffHelper.SetAttribute("PacketSize", UintegerValue(backgroundPacketSize));
    backgroundOnOffHelper.SetAttribute("DataRate", StringValue(backgroundDataRate));
    backgroundOnOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    backgroundOnOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    for (uint32_t i = 0; i < numBackgroundClients; ++i)
    {
        backgroundClientApps.Add(backgroundOnOffHelper.Install(backgroundClientNodes.Get(i)));
    }
    backgroundClientApps.Start(Seconds(1.0));
    backgroundClientApps.Stop(Seconds(simTime - 1.0));

    uint32_t attackPacketSize = 1024;
    uint32_t totalAttackPackets = 100;
    uint32_t totalAttackBytes = attackPacketSize * totalAttackPackets;
    OnOffHelper attackOnOffHelper("ns3::UdpSocketFactory", InetSocketAddress(serverIp, attackPort));
    attackOnOffHelper.SetAttribute("PacketSize", UintegerValue(attackPacketSize));
    attackOnOffHelper.SetAttribute("DataRate", StringValue("500kbps"));
    attackOnOffHelper.SetAttribute("MaxBytes", UintegerValue(totalAttackBytes));
    attackOnOffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    attackOnOffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer attackerApps = attackOnOffHelper.Install(attackerNode.Get(0));
    attackerApps.Start(Seconds(2.0));
    attackerApps.Stop(Seconds(simTime - 2.0));
    g_totalAttacksSent = totalAttackPackets;

    for (uint32_t s = 1; s <= (uint32_t)simTime; ++s)
    {
        Simulator::Schedule(Seconds(s), [](){
            NS_LOG_UNCOND("[CPU] t=" << Simulator::Now().GetSeconds()
                                     << "s tokens_restantes=" << g_availableTokens
                                     << " inspecionados=" << g_totalInspected
                                     << " bypass=" << g_totalCpuBypass
                                     << " ataques_bypass=" << g_totalAttacksCpuBypass);
        });
    }

    BulkSendHelper bulkSend("ns3::TcpSocketFactory", InetSocketAddress(serverIp, legitimatePort));
    bulkSend.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer legitimateClientApp = bulkSend.Install(legitimateClientNode.Get(0));
    legitimateClientApp.Start(Seconds(1.0));
    legitimateClientApp.Stop(Seconds(simTime));

    Ptr<NetDevice> nidsOutDevice = nidsLinkDevices.Get(0);
    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(nidsOutDevice);
    Ptr<Queue<Packet>> nidsQueue = p2pDev->GetQueue();
    nidsQueue->TraceConnectWithoutContext("Enqueue", MakeCallback(&OnPacketEnqueued));
    nidsQueue->TraceConnectWithoutContext("Drop", MakeCallback(&OnPacketDropped));

    nidsQueue->TraceConnectWithoutContext("Dequeue", MakeCallback(&OnNidsDequeue));

    Ptr<TrafficControlLayer> tc = p2pDev->GetNode()->GetObject<TrafficControlLayer>();
    if (tc != nullptr)
    {
        Ptr<QueueDisc> nidsQueueDisc = tc->GetRootQueueDiscOnDevice(p2pDev);
        if (nidsQueueDisc != nullptr)
        {
            nidsQueueDisc->TraceConnectWithoutContext("Drop", MakeCallback(&OnPacketDroppedDisc));
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

    NS_LOG_UNCOND("Total Pacotes Enfileirados: " << g_totalQueuePackets);
    NS_LOG_UNCOND("Total Pacotes Descartados: " << g_totalDrops);
    NS_LOG_UNCOND("Total Inspecionados (CPU): " << g_totalInspected);
    NS_LOG_UNCOND("Total Bypass CPU (Fail-Open): " << g_totalCpuBypass);
    NS_LOG_UNCOND("Ataques Inspecionados: " << g_totalAttacksInspected);
    NS_LOG_UNCOND("Ataques Detectados (drop lógico): " << g_totalAttacksDetected);
    NS_LOG_UNCOND("Ataques Bypass CPU (FNs): " << g_totalAttacksCpuBypass);

    double overallDropRate = 0.0;
    if (g_totalQueuePackets > 0)
    {
        overallDropRate = (double)g_totalDrops / g_totalQueuePackets;
    }

    double attacksReceived = 0.0;
    if (g_attackSink)
    {
        attacksReceived = g_attackSink->GetTotalRx() / (double)attackPacketSize;
    }

    double falseNegativeRate = 0.0;
    if (totalAttackPackets > 0)
    {
        falseNegativeRate = (double)g_totalAttacksCpuBypass / (double)totalAttackPackets;
    }
    NS_LOG_UNCOND("Ataques Enviados: " << totalAttackPackets
                                       << " | Recebidos (servidor): " << attacksReceived
                                       << " | Detectados(lógico): " << g_totalAttacksDetected
                                       << " | Bypass(FN): " << g_totalAttacksCpuBypass);
    NS_LOG_UNCOND("Taxa Falsos Negativos (bypass/enviados): " << falseNegativeRate);

    double legitimateThroughputMbps = 0.0;
    if (g_legitimateSink)
    {
        uint64_t totalBytesReceived = g_legitimateSink->GetTotalRx();
        NS_LOG_UNCOND("Total Bytes TCP Recebidos: " << totalBytesReceived);
        double appDuration = simTime - 1.0;
        legitimateThroughputMbps = (totalBytesReceived * 8.0) / (appDuration * 1000000.0);
    }

    monitor->CheckForLostPackets();
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();
    double avgLatency = 0.0;

    for (auto const& kv : stats)
    {
        uint32_t flowId = kv.first;
        FlowMonitor::FlowStats flowStats = kv.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);

        if (t.sourceAddress == legitimateIp &&
            t.destinationAddress == serverIp &&
            t.destinationPort == legitimatePort)
        {
            if (flowStats.rxPackets > 0)
            {
                avgLatency = (flowStats.delaySum.GetSeconds() / flowStats.rxPackets) * 1000;
                NS_LOG_UNCOND("Latência Média TCP: " << avgLatency << "s");
            }
            break;
        }
    }

    // NÃO APAGAR, É A SAÍDA DO RESULTADO PARA O SCRIPT DE ANÁLISE
    std::cout << "RESULT,"
              << overallDropRate << ","
              << falseNegativeRate << ","
              << legitimateThroughputMbps << ","
              << avgLatency << std::endl;

    Simulator::Destroy();
    return 0;
}