/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 Strasbourg University
 *
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
 * Author: David Gross <gdavid.devel@gmail.com>
 */

// Network topology
//
//             STA2
//              |
//              |
//   R1         R2
//   |          |
//   |          |
//   ------------
//           |
//           |
//          STA 1
//
// - Initial configuration :
//         - STA1 default route : R1
//         - R1 static route to STA2 : R2
//         - STA2 default route : R2
// - STA1 send Echo Request to STA2 using its default route to R1
// - R1 receive Echo Request from STA1, and forward it to R2
// - R1 send an ICMPv6 Redirection to STA1 with Target STA2 and Destination R2
// - Next Echo Request from STA1 to STA2 are directly sent to R2

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv6-static-routing-helper.h"

#include "ns3/ipv6-routing-table-entry.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Icmpv6RedirectExample");

/**
 * \class StackHelper
 * \brief Helper to set or get some IPv6 information about nodes.
 */
class StackHelper
{
public:
  /**
   * \brief Print the routing table.
   * \param n the node
   */
  inline void PrintRoutingTable (Ptr<Node>& n)
  {
    Ptr<Ipv6StaticRouting> routing = 0;
    Ipv6StaticRoutingHelper routingHelper;
    Ptr<Ipv6> ipv6 = n->GetObject<Ipv6> ();
    uint32_t nbRoutes = 0;
    Ipv6RoutingTableEntry route;

    routing = routingHelper.GetStaticRouting (ipv6);

    std::cout << "Routing table of " << n << " : " << std::endl;
    std::cout << "Destination\t\t\t\t" << "Gateway\t\t\t\t\t" << "Interface\t" << "Prefix to use" << std::endl;

    nbRoutes = routing->GetNRoutes ();
    for(uint32_t i = 0 ; i < nbRoutes ; i++)
      {
        route = routing->GetRoute (i);
        std::cout << route.GetDest () << "\t"
          << route.GetGateway () << "\t"
          << route.GetInterface () << "\t" 
          << route.GetPrefixToUse () << "\t"
          << std::endl;
      }
  }

  /**
   * \brief Add an host route.
   * \param n node 
   * \param dst destination address
   * \param nextHop next hop for destination
   * \param interface output interface
   */
  inline void AddHostRouteTo (Ptr<Node>& n, Ipv6Address dst, Ipv6Address nextHop, uint32_t interface)
  {
    Ptr<Ipv6StaticRouting> routing = 0;
    Ipv6StaticRoutingHelper routingHelper;
    Ptr<Ipv6> ipv6 = n->GetObject<Ipv6> ();

    routing = routingHelper.GetStaticRouting (ipv6);
    routing->AddHostRouteTo (dst, nextHop, interface);
  }
};

int main (int argc, char **argv)
{
#if 0 
  LogComponentEnable ("Icmpv6RedirectExample", LOG_LEVEL_INFO);
  LogComponentEnable ("Icmpv6L4Protocol", LOG_LEVEL_INFO);
  LogComponentEnable("Ipv6L3Protocol", LOG_LEVEL_ALL);
  LogComponentEnable("Ipv6StaticRouting", LOG_LEVEL_ALL);
  LogComponentEnable("Ipv6Interface", LOG_LEVEL_ALL);
  LogComponentEnable("Icmpv6L4Protocol", LOG_LEVEL_ALL);
  LogComponentEnable("NdiscCache", LOG_LEVEL_ALL);
#endif

  CommandLine cmd;
  cmd.Parse (argc, argv);

  NS_LOG_INFO ("Create nodes.");
  Ptr<Node> sta1 = CreateObject<Node> ();
  Ptr<Node> r1 = CreateObject<Node> ();
  Ptr<Node> r2 = CreateObject<Node> ();
  Ptr<Node> sta2 = CreateObject<Node> ();
  NodeContainer net1(sta1, r1, r2);
  NodeContainer net2(r2, sta2);
  NodeContainer all(sta1, r1, r2, sta2);

  StackHelper stackHelper;

  InternetStackHelper internetv6;
  internetv6.Install (all);

  NS_LOG_INFO ("Create channels.");
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue(5000000));
  csma.SetChannelAttribute ("Delay", TimeValue(MilliSeconds (2)));
  NetDeviceContainer ndc1 = csma.Install (net1); 
  NetDeviceContainer ndc2 = csma.Install (net2);

  NS_LOG_INFO ("Assign IPv6 Addresses.");
  Ipv6AddressHelper ipv6;

  ipv6.NewNetwork (Ipv6Address ("2001:1::"), 64);
  Ipv6InterfaceContainer iic1 = ipv6.Assign (ndc1);
  iic1.SetRouter (2, true);
  iic1.SetRouter (1, true);

  ipv6.NewNetwork (Ipv6Address ("2001:2::"), 64);
  Ipv6InterfaceContainer iic2 = ipv6.Assign (ndc2);
  iic2.SetRouter (0, true);

  stackHelper.AddHostRouteTo (r1, iic2.GetAddress (1, 1), iic1.GetAddress (2, 1), iic1.GetInterfaceIndex (1));

  Simulator::Schedule(Seconds(0.0), &StackHelper::PrintRoutingTable, &stackHelper, r1);
  Simulator::Schedule(Seconds(3.0), &StackHelper::PrintRoutingTable, &stackHelper, sta1);

  NS_LOG_INFO ("Create Applications.");
  uint32_t packetSize = 1024;
  uint32_t maxPacketCount = 5;
  Time interPacketInterval = Seconds (1.);
  Ping6Helper ping6;

  ping6.SetLocal (iic1.GetAddress(0, 1));
  ping6.SetRemote (iic2.GetAddress(1, 1));
  ping6.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
  ping6.SetAttribute ("Interval", TimeValue(interPacketInterval));
  ping6.SetAttribute ("PacketSize", UintegerValue (packetSize));
  ApplicationContainer apps = ping6.Install (sta1);
  apps.Start (Seconds (2.0));
  apps.Stop (Seconds (10.0));

  AsciiTraceHelper ascii;
  csma.EnableAsciiAll (ascii.CreateFileStream ("icmpv6-redirect.tr"));
  csma.EnablePcapAll ("icmpv6-redirect", true);

  /* Now, do the actual simulation. */
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}

