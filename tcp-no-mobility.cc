#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/log.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/netanim-module.h"

#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpNoMobilityScenario");

int
main(int argc, char* argv[])
{
    uint32_t nClients = 30; // Número inicial de clientes
    double simulationTime = 20.0;
    CommandLine cmd;
    cmd.AddValue("nClients", "Número de clientes na rede sem fio", nClients);
    cmd.Parse(argc, argv);

    // Configuração do log
    LogComponentEnable("TcpNoMobilityScenario", LOG_LEVEL_INFO);

    // Configurar os nós
    NodeContainer serverNode;
    serverNode.Create(1); // Nó servidor

    NodeContainer apNode;
    apNode.Create(1); // Access Point (AP)

    NodeContainer wifiClients;
    wifiClients.Create(nClients); // Clientes sem fio

    // Configurar o link cabeado (servidor <-> AP)
    NodeContainer p2pNodes = NodeContainer(serverNode.Get(0), apNode.Get(0));
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer p2pDevices;
    p2pDevices = pointToPoint.Install(p2pNodes);

    // Configurar a rede Wi-Fi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("Equipe_2");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer clientDevices = wifi.Install(phy, mac, wifiClients);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    // Configurar mobilidade
    MobilityHelper mobility;
    MobilityHelper ApMobility;
    MobilityHelper MobilityServer;

    // Clientes sem mobilidade
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(40.0),
                                  "MinY",
                                  DoubleValue(40.0),
                                  "DeltaX",
                                  DoubleValue(5.0),
                                  "DeltaY",
                                  DoubleValue(5.0),
                                  "GridWidth",
                                  UintegerValue(3),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(wifiClients);

    // AP fixo
    Ptr<ListPositionAllocator> positionAp = CreateObject<ListPositionAllocator>();
    positionAp->Add(Vector(40, 40, 0));
    ApMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    ApMobility.SetPositionAllocator(positionAp);
    ApMobility.Install(apNode);

    // Servidor fixo
    Ptr<ListPositionAllocator> positionServer = CreateObject<ListPositionAllocator>();
    positionServer->Add(Vector(0, 0, 0));
    MobilityServer.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    MobilityServer.SetPositionAllocator(positionServer);
    MobilityServer.Install(serverNode);

    // Instalar a pilha de Internet
    InternetStackHelper stack;
    stack.Install(serverNode);
    stack.Install(apNode);
    stack.Install(wifiClients);

    // Atribuir endereços IP
    Ipv4AddressHelper address;

    // Rede cabeada (10.1.1.0/24)
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices);

    // Rede sem fio (192.168.0.0/24)
    address.SetBase("192.168.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(clientDevices);
    address.Assign(apDevice);

    // Configurar a aplicação TCP
    uint16_t port = 9;

   for (u_int32_t i = 0; i < nClients; i++)
    {
        u_int16_t m_port = port + i;

    // Cria o servidor TCP
        PacketSinkHelper tcpServer("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), m_port));
        ApplicationContainer serverApp = tcpServer.Install(serverNode.Get(0));
        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(simulationTime));

    // Aplicação nos clientes
        OnOffHelper onoffHelper("ns3::TcpSocketFactory", InetSocketAddress(p2pInterfaces.GetAddress(0), m_port));
        onoffHelper.SetAttribute("DataRate", StringValue("1Mbps"));  // Taxa de dados de 1 Mbps
        onoffHelper.SetAttribute("PacketSize", UintegerValue(1024));  // Tamanho do pacote de 1024 bytes
        onoffHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));  // Tempo de atividade 1 segundo
        onoffHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]")); // Tempo de inatividade 0 segundos
    
        ApplicationContainer clientApps = onoffHelper.Install(wifiClients.Get(i));
        clientApps.Start(Seconds(2.0));
        clientApps.Stop(Seconds(simulationTime));
    }

    // Habilitar o roteamento
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Configurar o FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();

    // Habilitar rastreamento
    pointToPoint.EnablePcapAll("tcp-no-mobility");
    phy.EnablePcap("tcp-no-mobility", apDevice.Get(0));

    // Rodar a simulação
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Coletar métricas do FlowMonitor
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();
    flowMonitor->SerializeToXmlFile("TCP-No-Mobility.xml", true, true);
   
    if (stats.empty())
    {
        NS_LOG_ERROR("Nenhum fluxo coletado.");
    }
    else
    {
        NS_LOG_INFO("Fluxos coletados: " << stats.size());
    }

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "\t\t\t|================= TCP sem Mobilidade =================|\n";
    std::cout
        << "Fluxo ID\tOrigem\t\tDestino\t\tTaxa (Mbps)\tAtraso médio (ms)\tPerda de Pacotes (%)\n";

    for (const auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        double averageDelayMs = (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) * 1000;
        double packetLossPercentage =
            (double(flow.second.lostPackets) / (flow.second.lostPackets + flow.second.rxPackets)) *
            100;

        std::cout << flow.first << "\t\t"         // Fluxo ID
                  << t.sourceAddress << "\t"      // Endereço de origem
                  << t.destinationAddress << "\t" // Endereço de destino
                  << std::setw(5) << (flow.second.rxBytes * 8.0 / simulationTime) / 1e6
                  << "\t"                                          // Taxa em Mbps, alinhada
                  << std::setw(5) << averageDelayMs << "\t"        // Atraso médio em ms, alinhado
                  << std::setw(5) << packetLossPercentage << "\n"; // Perda de pacotes, alinhada
    }

    AnimationInterface anim("AnimTcpNoMobility.xml");

    anim.SetConstantPosition(serverNode.Get(0), 0, 0);
    anim.SetConstantPosition(apNode.Get(0), 40, 40);

    for (uint32_t i = 0; i < nClients; i++)
    {
        anim.SetConstantPosition(wifiClients.Get(i), 40 + (i % 3) * 5, 40 + (i / 3) * 5);
    }

    // Definir cores para diferenciar os tipos de nó
    anim.UpdateNodeColor(serverNode.Get(0), 255, 0, 0); // Vermelho para o servidor
    anim.UpdateNodeColor(apNode.Get(0), 0, 255, 0);     // Verde para o AP
    for (uint32_t i = 0; i < nClients; i++)
    {
        anim.UpdateNodeColor(wifiClients.Get(i), 0, 0, 255); // Azul para clientes
    }

    // Finalizar a simulação
    Simulator::Destroy();

    return 0;
}