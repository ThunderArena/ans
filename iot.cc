#include <iomanip> // Required for std::setprecision

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;
using namespace ns3::energy;

NS_LOG_COMPONENT_DEFINE("MmWaveIoT_Simulation");

static uint64_t g_packetsReceived = 0;

static void
RxCallback(const Ptr<const Packet> packet, const Address& addr)
{
    g_packetsReceived++;
}

void EnergyTrace(std::string context, double oldValue, double newValue)
{
    std::ofstream out("energy-log.txt", std::ios::app);
    out << Simulator::Now().GetSeconds() << "," << context << "," << newValue << std::endl;
    out.close();
}

int
main(int argc, char* argv[])
{
    uint32_t nIot = 20;
    double simulationTime = 10.0;
    double initialEnergyJ = 1.0;
    double distance = 30.0;
    uint32_t packetRate = 1;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nIot", "Number of IoT devices", nIot);
    cmd.AddValue("simulationTime", "Simulation time in seconds", simulationTime);
    cmd.Parse(argc, argv);

    NodeContainer wapNode;
    wapNode.Create(1);
    NodeContainer iotNodes;
    iotNodes.Create(nIot);
    NodeContainer allNodes = NodeContainer(wapNode, iotNodes);

    Ptr<YansWifiChannel> channel = CreateObject<YansWifiChannel>();
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());
    channel->SetPropagationLossModel(CreateObject<LogDistancePropagationLossModel>());

    YansWifiPhyHelper phy;
    phy.SetChannel(channel);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a); // Valid standard
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps")); // Valid mode

    WifiMacHelper mac;
    Ssid ssid = Ssid("mmWave-IoT-Network");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer iotDevices = wifi.Install(phy, mac, iotNodes);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer wapDevice = wifi.Install(phy, mac, wapNode);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(wapNode);

    mobility.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                  "X", StringValue("0.0"),
                                  "Y", StringValue("0.0"),
                                  "Rho", StringValue("ns3::UniformRandomVariable[Min=1.0|Max=" +
                                                     std::to_string(distance) + "]"));
    mobility.Install(iotNodes);

    InternetStackHelper stack;
    stack.Install(allNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(iotDevices);
    Ipv4InterfaceContainer wapInterface = address.Assign(wapDevice);

    uint16_t port = 9;
    PacketSinkHelper packetSinkHelper("ns3::UdpSocketFactory",
                                      InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = packetSinkHelper.Install(wapNode.Get(0));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simulationTime + 1.0));

    Config::ConnectWithoutContext(
        "/NodeList/0/ApplicationList/*/$ns3::PacketSink/Rx",
        MakeCallback(&RxCallback));

    OnOffHelper onOffHelper("ns3::UdpSocketFactory",
                            InetSocketAddress(wapInterface.GetAddress(0), port));
    onOffHelper.SetConstantRate(DataRate(std::to_string(512 * packetRate) + "B/s"), 512);

    ApplicationContainer clientApps;
    for (uint32_t i = 0; i < nIot; ++i)
    {
        ApplicationContainer tempApp = onOffHelper.Install(iotNodes.Get(i));
        tempApp.Start(Seconds(1.0));
        tempApp.Stop(Seconds(simulationTime));
        clientApps.Add(tempApp);
    }

    BasicEnergySourceHelper basicSourceHelper;
    basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(initialEnergyJ));
    EnergySourceContainer iotEnergySources = basicSourceHelper.Install(iotNodes);

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.2));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(0.1));
    radioEnergyHelper.Set("IdleCurrentA", DoubleValue(0.05));
    radioEnergyHelper.Install(iotDevices, iotEnergySources);
   

     for (uint32_t i = 0; i < iotEnergySources.GetN(); ++i)
{
    Ptr<BasicEnergySource> source = DynamicCast<BasicEnergySource>(iotEnergySources.Get(i));
    source->TraceConnectWithoutContext("RemainingEnergy", MakeCallback(&EnergyTrace));
}


    
    // NetAnim setup
    AnimationInterface anim("iot-animation.xml");

    anim.UpdateNodeDescription(wapNode.Get(0), "WAP");
    anim.UpdateNodeColor(wapNode.Get(0), 255, 0, 0); // Red

     for (uint32_t i = 0; i < iotNodes.GetN(); ++i)
     {
    anim.UpdateNodeDescription(iotNodes.Get(i), "IoT");
    anim.UpdateNodeColor(iotNodes.Get(i), 0, 255, 0); // Green
    // Energy visualization not supported directly
     }

     anim.EnablePacketMetadata(true);
     anim.EnableWifiMacCounters(Seconds(0.0), Seconds(simulationTime), Seconds(1.0));
     anim.EnableIpv4RouteTracking("iot-routing.xml", Seconds(0.0), Seconds(simulationTime));


    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    uint64_t totalTxPackets = nIot * packetRate * (simulationTime - 1.0);
    double pdr = (totalTxPackets > 0) ? ((double)g_packetsReceived / totalTxPackets) * 100.0 : 0;

    double totalEnergyConsumed = 0;
    for (uint32_t i = 0; i < iotNodes.GetN(); ++i)
    {
        Ptr<BasicEnergySource> source =
            DynamicCast<BasicEnergySource>(iotEnergySources.Get(i));
        totalEnergyConsumed += (initialEnergyJ - source->GetRemainingEnergy());
    }
    double avgEnergyConsumed = (nIot > 0) ? (totalEnergyConsumed / nIot) : 0;

    std::cout << "\n--- Simulation Results ---" << std::endl;
    std::cout << "Total Packets Sent:     " << totalTxPackets << std::endl;
    std::cout << "Total Packets Received: " << g_packetsReceived << std::endl;
    std::cout << "Packet Delivery Ratio (PDR): " << std::fixed << std::setprecision(2) << pdr
              << " %" << std::endl;
    std::cout << "Average Energy Consumption per Device: " << avgEnergyConsumed * 1000.0 << " mJ"
              << std::endl;
    std::cout << "--------------------------" << std::endl;

    Simulator::Destroy();
    return 0;
}




///Packet Delivery Ratio (PDR): 88.89% That’s a solid baseline for a simple mmWave-style IoT setup. It suggests good connectivity despite random node placement and basic PHY settings.

///Average Energy Consumption: 189.53 mJ per device With 1 J initial energy, this means each device used ~19% of its energy budget over 9 seconds of active transmission. That’s efficient for low-rate UDP traffic.
