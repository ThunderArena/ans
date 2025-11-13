/* security.cc
 *
 * THz WAP security demo using IP-layer packet inspection and ACL.
 * - Uses THz module helpers to create devices (AP + clients)
 * - Server uses a UDP socket and inspects sender IP against an ACL
 * - Authorized vs Unauthorized clients
 *
 * 
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"

#include "ns3/antenna-module.h"
#include "ns3/thz-channel.h"
#include "ns3/thz-dir-antenna.h"
#include "ns3/thz-directional-antenna-helper.h"
#include "ns3/thz-helper.h"
#include "ns3/thz-mac-macro-ap-helper.h"
#include "ns3/thz-mac-macro-client-helper.h"
#include "ns3/thz-phy-macro-helper.h"

#include <vector>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ThzSecurityWap");

// ---------- Global ACL ----------
std::vector<Ipv4Address> g_acl;
uint32_t g_numAuthorized = 0;
uint32_t g_numUnauthorized = 0;

// ---------- Packet inspection callback ----------
void MyPacketRxCallback(Ptr<const Packet> packet, const Address &from)
{
    InetSocketAddress inetFrom = InetSocketAddress::ConvertFrom(from);
    Ipv4Address src = inetFrom.GetIpv4();

    if (std::find(g_acl.begin(), g_acl.end(), src) != g_acl.end())
    {
        g_numAuthorized++;
        NS_LOG_UNCOND("[AUTHORIZED] Packet from " << src << " size=" << packet->GetSize() << " bytes");
    }
    else
    {
        g_numUnauthorized++;
        NS_LOG_UNCOND("[UNAUTHORIZED] Packet from " << src << " size=" << packet->GetSize() << " bytes");
    }
}

int main(int argc, char *argv[])
{
    Time::SetResolution(Time::US);
    LogComponentEnable("ThzSecurityWap", LOG_LEVEL_INFO);

    double simTimeSec = 8.0;
    CommandLine cmd;
    cmd.AddValue("simTime", "Simulation time in seconds", simTimeSec);
    cmd.Parse(argc, argv);

    // Create nodes: server (AP) + authorized client
    NodeContainer nodes;
    nodes.Create(2); // Node0=server, Node1=authorized client

    NodeContainer serverNode; serverNode.Add(nodes.Get(0));
    NodeContainer clientNode; clientNode.Add(nodes.Get(1));

    // Rogue client node
    Ptr<Node> rogueNode = CreateObject<Node>();
    InternetStackHelper internet;
    internet.Install(rogueNode);

    // Mobility: fixed positions
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> posAlloc = CreateObject<ListPositionAllocator>();
    posAlloc->Add(Vector(0.0, 0.0, 0.0)); // server at origin
    mobility.SetPositionAllocator(posAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(serverNode);

    mobility.SetPositionAllocator("ns3::UniformDiscPositionAllocator",
                                  "X", DoubleValue(0.0),
                                  "Y", DoubleValue(0.0),
                                  "rho", DoubleValue(10.0));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(clientNode);

    // Place rogue near server
    Ptr<ListPositionAllocator> roguePos = CreateObject<ListPositionAllocator>();
    roguePos->Add(Vector(5.0, 5.0, 0.0));
    mobility.SetPositionAllocator(roguePos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(rogueNode);

    // THz helpers
    Ptr<THzChannel> thzChan = CreateObjectWithAttributes<THzChannel>("NoiseFloor", DoubleValue(-174.0 + 7.0));

    THzPhyMacroHelper thzPhy = THzPhyMacroHelper::Default();
    thzPhy.Set("TxPower", DoubleValue(20.0));
    thzPhy.Set("BasicRate", DoubleValue(157.44e9));
    thzPhy.Set("DataRate", DoubleValue(157.44e9));

    THzMacMacroApHelper thzMacAp = THzMacMacroApHelper::Default();
    THzMacMacroClientHelper thzMacClient = THzMacMacroClientHelper::Default();

    THzDirectionalAntennaHelper thzDirAntenna = THzDirectionalAntennaHelper::Default();
    double sectors = 30;
    double beamwidth = 360.0 / sectors;
    thzDirAntenna.Set("MaxGain", DoubleValue(20 * std::log10(sectors) - 4.971498726941338));
    thzDirAntenna.Set("BeamWidth", DoubleValue(beamwidth));

    // Install THz devices
    THzHelper thz;
    NetDeviceContainer serverDevices = thz.Install(serverNode, thzChan, thzPhy, thzMacAp, thzDirAntenna);
    NetDeviceContainer clientDevices = thz.Install(clientNode, thzChan, thzPhy, thzMacClient, thzDirAntenna);
    NetDeviceContainer rogueDevices = thz.Install(NodeContainer(rogueNode), thzChan, thzPhy, thzMacClient, thzDirAntenna);

    NetDeviceContainer allDevices; 
    allDevices.Add(serverDevices); 
    allDevices.Add(clientDevices);
    allDevices.Add(rogueDevices);

    // Internet stack and IP addresses
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifaces = ipv4.Assign(allDevices);

    Ipv4Address serverIp = ifaces.GetAddress(0);
    Ipv4Address clientIp = ifaces.GetAddress(1);
    Ipv4Address rogueIp = ifaces.GetAddress(2);
    NS_LOG_UNCOND("Server IP: " << serverIp << "  Client IP: " << clientIp << "  Rogue IP: " << rogueIp);

    // ACL: allow only authorized client
    g_acl.push_back(clientIp);

    // ---------- UDP server ----------
    uint16_t port = 9000;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(serverNode);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(3.0));

    // Attach packet inspection to server device
    for (uint32_t i = 0; i < serverDevices.GetN(); ++i)
    {
        Ptr<NetDevice> dev = serverDevices.Get(i);
        dev->TraceConnectWithoutContext("PhyRxEnd", MakeCallback(&MyPacketRxCallback));
    }

    // ---------- Authorized client ----------
    UdpEchoClientHelper echoClient(serverIp, port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(5));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(clientNode);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(3.0));

    // ---------- Rogue client (unauthorized) ----------
    UdpEchoClientHelper rogueClient(serverIp, port);
    rogueClient.SetAttribute("MaxPackets", UintegerValue(3));
    rogueClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    rogueClient.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer rogueApps = rogueClient.Install(rogueNode);
    rogueApps.Start(Seconds(2.5));
    rogueApps.Stop(Seconds(3.0));

    Simulator::Stop(Seconds(3.0));
    Simulator::Run();

    std::cout << "Authorized packets: " << g_numAuthorized << std::endl;
    std::cout << "Unauthorized packets: " << g_numUnauthorized << std::endl;

    Simulator::Destroy();
    return 0;
}

