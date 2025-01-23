#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpUdpMobilityScenario");

int main(int argc, char* argv[])
{
    uint32_t nClients = 4; // Número total de clientes (deve ser par para dividir 50/50)
    double simulationTime = 11.0;
    CommandLine cmd;
    cmd.AddValue("nClients", "Número de clientes na rede sem fio", nClients);
    cmd.Parse(argc, argv);

    // Verifica se o número de clientes é par
    if (nClients % 2 != 0)
    {
        NS_LOG_ERROR("O número de clientes deve ser par para dividir 50% TCP e 50% UDP.");
        return 1;
    }

    // Configuração do log
    LogComponentEnable("TcpUdpMobilityScenario", LOG_LEVEL_INFO);

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

    NetDeviceContainer p2pDevices = pointToPoint.Install(p2pNodes);

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

   // Configura a mobilidade para o AP (fixo)
    MobilityHelper mobility;

    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(apNode);

    // Configura a mobilidade para os clientes (mobilidade aleatória)
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds",
                              RectangleValue(Rectangle(-50, 50, -50, 50)));
    mobility.Install(wifiClients);

    // Servidor fixo
    mobility.Install(serverNode);

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

    // Configurar aplicações
    uint16_t tcpPort = 9;  // Porta TCP
    uint16_t udpPort = 10; // Porta UDP

    // Servidor TCP
    PacketSinkHelper tcpServer("ns3::TcpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
    ApplicationContainer serverApps = tcpServer.Install(serverNode.Get(0));

    // Servidor UDP
    PacketSinkHelper udpServer("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), udpPort));
    serverApps.Add(udpServer.Install(serverNode.Get(0)));

    NS_LOG_INFO("Aplicações do servidor TCP e UDP iniciadas.");
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simulationTime));

    // Clientes TCP
    uint32_t halfClients = nClients / 2;

    BulkSendHelper tcpClient("ns3::TcpSocketFactory",
                             InetSocketAddress(p2pInterfaces.GetAddress(0), tcpPort));
    tcpClient.SetAttribute("MaxBytes", UintegerValue(0));    // Sem limite de bytes
    tcpClient.SetAttribute("SendSize", UintegerValue(1024)); // Tamanho do pacote

    ApplicationContainer tcpClientApps;

    // Instalar o aplicativo TCP nos primeiros `halfClients` nós
    for (uint32_t i = 0; i < halfClients; ++i)
    {
        tcpClientApps.Add(tcpClient.Install(wifiClients.Get(i)));
    }

    NS_LOG_INFO("Clientes TCP iniciados.");
    tcpClientApps.Start(Seconds(1.0));
    tcpClientApps.Stop(Seconds(simulationTime));

    // Clientes UDP

    UdpClientHelper udpClient(p2pInterfaces.GetAddress(0), udpPort);
    udpClient.SetAttribute("MaxPackets", UintegerValue(1));
    udpClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    udpClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer udpClientApps;

    // Instalar o aplicativo UDP nos clientes restantes
    for (uint32_t i = halfClients; i < nClients; ++i)
    {
        udpClientApps.Add(udpClient.Install(wifiClients.Get(i)));
    }

    NS_LOG_INFO("Clientes UDP iniciados.");
    udpClientApps.Start(Seconds(2.0));
    udpClientApps.Stop(Seconds(simulationTime));

    // Habilita o rastreamento de pacotes (opcional)
    phy.EnablePcap("udp-tcp-mobility", apDevice.Get(0));

    // Configurar o FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    // Iniciar a simulação
    Simulator::Stop(Seconds(11.0));
    NS_LOG_INFO("Iniciando a simulação...");
    Simulator::Run();
    NS_LOG_INFO("Simulação finalizada.");

    // Relatório do FlowMonitor
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    if (stats.empty())
    {
        NS_LOG_ERROR("Nenhum fluxo coletado.");
    }
    else
    {
        NS_LOG_INFO("Fluxos coletados: " << stats.size());
    }
    std::cout << std::fixed << std::setprecision(6);

    std::cout << "\t\t\t|================= UDP/TCP com Mobilidade =================|\n";
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

    // Finaliza a simulação
    Simulator::Destroy();

    return 0;
}
