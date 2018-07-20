/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 * Author: PengKuang <kphf1995cm@outlook.com>
 */

/*
 *          Network topology discription
 *          _________________________
 *          |                        |
 *          |        switch          |      
 *          |                        |
 *          |________________________|
 *            |      |      |      |  
 *           h0(dst) h1     h2     h3(src)
 *      ip:10.1.1.1              10.1.1.4
 *     mac:00:00:00:00:00:01     00:00:00:00:00:07
 *application:UdpEchoServer      UdpEchoClient
 * simulate start:h3 send ArpHeader packet whose dstIp is 10.1.1.1 to switch.
 *
*/

#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/p4-helper.h"
#include "ns3/v4ping-helper.h"
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include "ns3/global.h"
#include <bm/SimpleSwitch.h>
#include <bm/bm_runtime/bm_runtime.h>
#include <bm/bm_sim/target_parser.h>
#include "ns3/switch-api.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("P4Example");

int main (int argc, char *argv[]) 
{

  LogComponentEnable ("P4Example", LOG_LEVEL_LOGIC);
  LogComponentEnable ("P4NetDevice", LOG_LEVEL_LOGIC);
  
  // init 
  P4GlobalVar::g_homePath="/home/kphf1995cm/";
  P4GlobalVar::g_ns3RootName="ns-allinone-3.26/";
  P4GlobalVar::g_ns3SrcName="ns-3.26/";
  P4GlobalVar::g_nfDir=P4GlobalVar::g_homePath+P4GlobalVar::g_ns3RootName+P4GlobalVar::g_ns3SrcName+"src/P4Simulator/test/";
  P4GlobalVar::g_nsType=NS3;
  P4GlobalVar::g_runtimeCliTime=10;
  SwitchApi::InitApiMap();
  

  CommandLine cmd;
  cmd.AddValue("time", "Waiting time for Runtime CLI Operations", P4GlobalVar::g_runtimeCliTime);
  cmd.Parse (argc, argv);
  //NS_LOG_INFO ("Create nodes.");
  NodeContainer terminals;
  terminals.Create (4);
  NodeContainer csmaSwitch;
  csmaSwitch.Create (1);

  //NS_LOG_INFO ("Build Topology");
    
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (5000000));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
    
  NetDeviceContainer terminalDevices;
  NetDeviceContainer switchDevices;
    
  for (int i = 0; i < 4; i++)
    {
      NetDeviceContainer link = csma.Install (NodeContainer (terminals.Get (i), csmaSwitch));
      terminalDevices.Add (link.Get (0));
      switchDevices.Add (link.Get (1));
    }
  Ptr<Node> switchNode = csmaSwitch.Get (0);
  if (P4GlobalVar::g_nsType==P4Simulator) //P4Simulator mode
    {
      P4GlobalVar::g_populateFlowTableWay=LOCAL_CALL;//LOCAL_CALL RUNTIME_CLI
      P4GlobalVar::g_networkFunc=SILKROAD;
      P4GlobalVar::SetP4MatchTypeJsonPath();
      P4GlobalVar::g_flowTablePath=P4GlobalVar::g_nfDir+"silkroad/command.txt";
	  P4GlobalVar::g_viewFlowTablePath=P4GlobalVar::g_nfDir+"silkroad/view.txt";
      P4Helper bridge;
      bridge.Install (switchNode, switchDevices);

	  // view switch flowtable info by controller before simulate
	  P4GlobalVar::g_p4Controller.ViewP4SwitchFlowTableInfo(0);
    }
  else //ns3 mode
    {
      BridgeHelper bridge;
      bridge.Install (switchNode, switchDevices);
    }
  // Add internet stack to the terminals
  InternetStackHelper internet;
  internet.Install (terminals);

  // Assign IP addresses.
  //NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer addresses = ipv4.Assign (terminalDevices);

  //NS_LOG_INFO ("Create Applications.");
  //NS_LOG_INFO ("Create Source");
  Config::SetDefault ("ns3::Ipv4RawSocketImpl::Protocol", StringValue ("2"));
    
  UdpEchoServerHelper echoServer (9);
  ApplicationContainer serverApps = echoServer.Install (terminals.Get (0));
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));
  Ipv4Address dstIp;
  if(P4GlobalVar::g_networkFunc==SILKROAD)
    dstIp=Ipv4Address("10.1.1.10");
  else
    dstIp=addresses.GetAddress (0);
  UdpEchoClientHelper echoClient (dstIp, 9);
  echoClient.SetAttribute ("MaxPackets", UintegerValue (100));
  echoClient.SetAttribute ("Interval", TimeValue (Seconds (0.1)));
  echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
  ApplicationContainer clientApps = echoClient.Install (terminals.Get (3));
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (10.0));
  
  /*
  // host 3 send packet to host 0
	ApplicationContainer clientApp;
	ApplicationContainer serverApp;
	unsigned int serverI = 0;
	unsigned int clientI = 3;
	Ipv4Address serverAddr;
	if (P4GlobalVar::g_networkFunc == SILKROAD)
		serverAddr = Ipv4Address("10.1.1.10");
	else
		serverAddr = addresses.GetAddress(serverI);

	InetSocketAddress dst = InetSocketAddress(serverAddr);

	//client
	OnOffHelper onOff = OnOffHelper("ns3::TcpSocketFactory", dst);
	onOff.SetAttribute("PacketSize", UintegerValue(1024));
	onOff.SetAttribute("DataRate", StringValue("1Mbps"));
	onOff.SetAttribute("MaxBytes", UintegerValue(0));

	clientApp = onOff.Install(terminals.Get(clientI));
	clientApp.Start(Seconds(1.0));
	clientApp.Stop(Seconds(20.0));

	//server
	PacketSinkHelper sink = PacketSinkHelper("ns3::TcpSocketFactory", dst);
	serverApp = sink.Install(terminals.Get(serverI));
	serverApp.Start(Seconds(0.0));
	serverApp.Stop(Seconds(21.0));
  */
  //NS_LOG_INFO ("Configure Tracing.");
  //csma.EnablePcapAll ("p4-example", false);
  Packet::EnablePrinting ();
  
  //NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();

  //view switch flowtable info after simulate
  //P4GlobalVar::g_p4Controller.ViewP4SwitchFlowTableInfo(0);

  //********View switch receive packet Num***************************
    std::cout<<"CsmaSwitch :"<<csmaSwitch.Get(0)->m_packetNum<<std::endl;
    unsigned int tN=terminals.GetN();
    for(unsigned int i=0;i<tN;i++)
        std::cout<<"Terminal "<<i<<" : "<<terminals.Get(i)->m_packetNum<<std::endl;
  //*****************************************************************

  //*******View OnOffApplication Send Packet Num*************************

    //std::cout<<"OnOffApplication Send Packet Num:"<<terminals.Get(clientI)->GetApplication(0)->m_packetNum<<std::endl;

  //*********************************************************************

  Simulator::Destroy ();
  if(P4GlobalVar::g_populateFlowTableWay==RUNTIME_CLI)
    while (true) std::this_thread::sleep_for(std::chrono::seconds(100));
  //NS_LOG_INFO ("Done.");
  std::cout<<"Run successfully!"<<std::endl;
}


