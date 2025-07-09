#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/network-module.h"
#include "ns3/ping-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tcp-bbr.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Bbr4to1Simulator");

int
main(int argc, char* argv[])
{
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpBbr::GetTypeId()));
    //! BBR must use pacing
    Config::SetDefault("ns3::TcpSocketState::EnablePacing", BooleanValue(true));

    //! 全局 TCP 参数
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 22));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 22));

    //! 配置队列
    Config::SetDefault("ns3::RedQueueDisc::MaxSize", StringValue("10000p"));

    //! 基础参数设置
    Time::SetResolution(Time::NS);
    LogComponentEnable("Bbr4to1Simulator", LOG_LEVEL_INFO);
    LogComponentEnable("TcpSocketBase", ns3::LOG_LEVEL_WARN);
    LogComponentEnable("TcpBbr", LOG_LEVEL_INFO);

    //! 创建节点容器
    NodeContainer senders; // n0‑n3
    senders.Create(4);
    Ptr<Node> receiver = CreateObject<Node>(); // n4
    Ptr<Node> router = CreateObject<Node>();   // n5

    //! 安装协议栈
    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(router);
    stack.Install(receiver);

    // n0 -> n4; n1 -> n4; n2 -> n4; n3 -> n4
    PointToPointHelper p2pHostToRouter;
    p2pHostToRouter.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2pHostToRouter.SetChannelAttribute("Delay", StringValue("30ms"));

    //! 配置队列规则
    PointToPointHelper p2pRouterToRecv;
    p2pRouterToRecv.SetDeviceAttribute("DataRate", StringValue("1Gbps")); // 瓶颈
    p2pRouterToRecv.SetChannelAttribute("Delay", StringValue("10ms"));

    std::vector<NetDeviceContainer> devSenderToRouter(4);
    std::vector<TrafficControlHelper> tchLeft(4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        devSenderToRouter[i] = p2pHostToRouter.Install(senders.Get(i), router);
        tchLeft[i].Install(devSenderToRouter[i]);

        //! 创建随机丢包模型
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(0.00001));
        em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
        devSenderToRouter[i].Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        devSenderToRouter[i].Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    NetDeviceContainer devRouterToRecv = p2pRouterToRecv.Install(router, receiver);
    TrafficControlHelper tchRight;
    tchRight.Install(devRouterToRecv);

    Ipv4AddressHelper addr;
    std::vector<Ipv4InterfaceContainer> ifSenderToRouter(4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        std::ostringstream subnet;
        // 10.1.1.0/24
        // 10.1.2.0/24
        // 10.1.3.0/24
        // 10.1.4.0/24
        subnet << "10.1." << (i + 1) << ".0";
        addr.SetBase(subnet.str().c_str(), "255.255.255.0");
        ifSenderToRouter[i] = addr.Assign(devSenderToRouter[i]);
    }
    addr.SetBase("10.1.100.0", "255.255.255.0"); // Router‑Recv
    Ipv4InterfaceContainer ifRouterToRecv = addr.Assign(devRouterToRecv);

    Ipv4StaticRoutingHelper sRouting;
    Ptr<Ipv4StaticRouting> routerStatic = sRouting.GetStaticRouting(router->GetObject<Ipv4>());
    for (uint32_t i = 0; i < 4; ++i)
    {
        routerStatic->AddNetworkRouteTo(
            Ipv4Address(("10.1." + std::to_string(i + 1) + ".0").c_str()),
            Ipv4Mask("255.255.255.0"),
            i + 1);
    }
    routerStatic->AddNetworkRouteTo(Ipv4Address("10.1.100.0"), Ipv4Mask("255.255.255.0"), 5);

    Ptr<Ipv4StaticRouting> recvStatic = sRouting.GetStaticRouting(receiver->GetObject<Ipv4>());
    recvStatic->SetDefaultRoute(ifRouterToRecv.GetAddress(0), 1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    OnOffHelper mainTcp("ns3::TcpSocketFactory",
                        InetSocketAddress(ifRouterToRecv.GetAddress(1), 9000));
    mainTcp.SetAttribute("DataRate", StringValue("1Gbps"));
    mainTcp.SetAttribute("PacketSize", UintegerValue(1472));
    mainTcp.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=100]"));
    mainTcp.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer mainApp = mainTcp.Install(senders.Get(0));

    for (uint32_t i = 1; i < 4; ++i)
    {
        OnOffHelper burst("ns3::UdpSocketFactory",
                          InetSocketAddress(ifRouterToRecv.GetAddress(1), 9000 + i));
        burst.SetAttribute("DataRate", StringValue("1Gbps"));
        burst.SetAttribute("PacketSize", UintegerValue(1472));
        burst.SetAttribute("OnTime",
                           StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        burst.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
        ApplicationContainer burstApp = burst.Install(senders.Get(i));

        for (double t = 50.0; t < 100.0; t += 10.0)
        {
            burstApp.Start(Seconds(t));
            burstApp.Stop(Seconds(t + 1.0));
        }
    }

    ApplicationContainer sinkApp;
    for (uint16_t port = 9000; port <= 9003; ++port)
    {
        if (port == 9000)
        {
            PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), port));
            sinkApp = sinkHelper.Install(receiver);
        }
        else
        {
            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), port));
            sinkHelper.Install(receiver);
        }
    }
    Ptr<PacketSink> sink = StaticCast<PacketSink>(sinkApp.Get(0));

    PingHelper ping(ifRouterToRecv.GetAddress(0));
    ping.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    ping.SetAttribute("Count", UintegerValue(10000));

    ApplicationContainer pingApps = ping.Install(receiver);
    pingApps.Start(Seconds(1.0));
    pingApps.Stop(Seconds(100.0));

    std::ofstream rttLog("rtt.log");
    Ptr<Ping> pingApp = DynamicCast<Ping>(pingApps.Get(0));
    rttLog << "timestamp(s)\trtt(ms)\n";
    Callback<void, uint16_t, Time> rttCb([&rttLog](uint16_t seq, Time rtt) {
        rttLog << Simulator::Now().GetSeconds() << " " << rtt.GetMilliSeconds() << std::endl;
        RttCache::Instance().PushRtt(rtt); // 缓存 RTT
    });
    pingApp->TraceConnectWithoutContext("Rtt", rttCb);

    //! 安装流量监控
    FlowMonitorHelper flowMonitor;
    Ptr<FlowMonitor> monitor = flowMonitor.InstallAll();

    mainApp.Start(Seconds(1.0));
    mainApp.Stop(Seconds(100.0));

    Simulator::Stop(Seconds(103.0));
    Simulator::Run();
    //! 输出结果
    monitor->CheckForLostPackets();
    Ptr<ns3::Ipv4FlowClassifier> classifier =
        DynamicCast<ns3::Ipv4FlowClassifier>(flowMonitor.GetClassifier());

    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (auto& iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter.first);
        NS_LOG_UNCOND("flowId: " << iter.first << " srcAddr: " << t.sourceAddress
                                 << " dstAddr: " << t.destinationAddress);
        NS_LOG_UNCOND("timeLastRxPacket: " << iter.second.timeLastRxPacket.GetSeconds());
        NS_LOG_UNCOND("timeFirstTxPacket: " << iter.second.timeFirstTxPacket.GetSeconds());
        NS_LOG_UNCOND("发送数据包数量: " << iter.second.txPackets);
        NS_LOG_UNCOND("接收数据包数量: " << iter.second.rxPackets);
        NS_LOG_UNCOND("丢包率: " << (iter.second.lostPackets / (double)iter.second.txPackets) * 100
                                 << "%");
        NS_LOG_UNCOND("吞吐量: " << iter.second.rxBytes * 8.0 /
                                        (iter.second.timeLastRxPacket.GetSeconds() -
                                         iter.second.timeFirstTxPacket.GetSeconds()) /
                                        1e6
                                 << " Mbps");
    }

    NS_LOG_UNCOND("接收总字节数: " << sink->GetTotalRx());
    Simulator::Destroy();

    return 0;
}
