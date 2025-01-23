#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/log.h"
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UdpMobilityScenario");

int main(int argc, char* argv[])
{
    // Configura o número de clientes
    uint32_t nClients = 32;
    double simulationTime = 11.0; // Tempo de simulação em segundos

    // Configuração do log
    LogComponentEnable("UdpMobilityScenario", LOG_LEVEL_INFO);

    // Configura os nós
    NodeContainer serverNode;
    serverNode.Create(1); // nó servidor

    NodeContainer apNode;
    apNode.Create(1); // Access Point

    NodeContainer wifiClients;
    wifiClients.Create(nClients); // Clientes sem fio

    // configurar o link cabeado (servidor <-> AP)
    NodeContainer p2pNodes = NodeContainer(serverNode.Get(0), apNode.Get(0));
    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer p2pDevices;
    p2pDevices = pointToPoint.Install(p2pNodes);

    // Configura o padrão de Wi-Fi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    // Configura o canal Wi-Fi
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("Equipe_2");

    // Configura o MAC para o AP
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode);

    // Configura o MAC para os clientes
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer clientDevices = wifi.Install(phy, mac, wifiClients);

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

    // Instala a pilha de Internet
    InternetStackHelper stack;
    stack.Install(serverNode);
    stack.Install(apNode);
    stack.Install(wifiClients);

    // Declara a variável address para as interfaces
    Ipv4AddressHelper address;

    // Rede cabeada (10.1.1.0/24)
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices);

    // Rede sem fio (192.168.0.0/24)
    address.SetBase("192.168.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(clientDevices);

    // Configura o servidor UDP no AP
    uint16_t port = 9; // Porta UDP padrão
    UdpServerHelper udpServer(port);
    ApplicationContainer serverApp = udpServer.Install(serverNode.Get(0));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simulationTime));

    // Configura os clientes UDP que enviam pacotes ao AP
    UdpClientHelper udpClient(p2pInterfaces.GetAddress(0), port);
    udpClient.SetAttribute("MaxPackets", UintegerValue(1));
    udpClient.SetAttribute("Interval", TimeValue(Seconds(1.0))); // Intervalo entre pacotes
    udpClient.SetAttribute("PacketSize", UintegerValue(1024));    // Tamanho dos pacotes

    ApplicationContainer clientApps = udpClient.Install(wifiClients);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(simulationTime));

    // Habilitar o roteamento
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Habilita o rastreamento de pacotes (opcional)
    pointToPoint.EnablePcapAll("udp-mobility");
    phy.EnablePcap("udp-mobility", apDevice.Get(0));

    // Configura o FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowMonitor = flowmonHelper.InstallAll();

    // Executa a simulação
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // Coletar métricas do FlowMonitor
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();
    if (stats.empty())
    {
        NS_LOG_ERROR("Nenhum fluxo coletado.");
    }
    else
    {
        NS_LOG_INFO("Fluxos coletados: " << stats.size());
    }
    std::cout << std::fixed << std::setprecision(6);

    std::cout << "\t\t\t|================= UDP com Mobilidade =================|\n";
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
