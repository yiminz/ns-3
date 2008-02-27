/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006 INRIA
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
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "ns3/packet.h"
#include "ns3/llc-snap-header.h"
#include "ns3/node.h"
#include "ns3/composite-trace-resolver.h"

#include "wifi-net-device.h"
#include "wifi-phy.h"
#include "wifi-channel.h"
#include "mac-stations.h"
#include "mac-low.h"
#include "mac-parameters.h"
#include "mac-rx-middle.h"
#include "mac-tx-middle.h"
#include "mac-high-adhoc.h"
#include "mac-high-nqsta.h"
#include "mac-high-nqap.h"
#include "dca-txop.h"
#include "dcf-manager.h"
#include "wifi-default-parameters.h"
#include "arf-mac-stations.h"
#include "aarf-mac-stations.h"
#include "ideal-mac-stations.h"
#include "cr-mac-stations.h"
#include "onoe-mac-stations.h"
#include "amrr-mac-stations.h"
#include "rraa-mac-stations.h"

namespace ns3 {

/***************************************************************
 *         hold an enumeration of the trace types
 ***************************************************************/

WifiNetDeviceTraceType::WifiNetDeviceTraceType ()
  : m_type (RX)
{}

WifiNetDeviceTraceType::WifiNetDeviceTraceType (enum Type type)
  : m_type (type)
{}
enum WifiNetDeviceTraceType::Type 
WifiNetDeviceTraceType::Get (void) const
{
  return m_type;
}
uint16_t 
WifiNetDeviceTraceType::GetUid (void)
{
  static uint16_t uid = AllocateUid<WifiNetDeviceTraceType> ("ns3::WifiNetDeviceTraceType");
  return uid;
}
void 
WifiNetDeviceTraceType::Print (std::ostream &os) const
{
  os << "event=";
  switch (m_type) {
  case RX: 
    os << "rx"; 
    break;
  case TX: 
    os << "tx";
    break;
  }
}
std::string 
WifiNetDeviceTraceType::GetTypeName (void) const
{
   return "ns3::WifiNetDeviceTraceType";
}

/***************************************************************
 *         a static helper
 ***************************************************************/

static WifiMode 
GetWifiModeForPhyMode (Ptr<WifiPhy> phy, enum WifiDefaultParameters::PhyModeParameter mode)
{
  uint32_t phyRate = (uint32_t)mode;
  for (uint32_t i= 0; i < phy->GetNModes (); i++)
    {
      WifiMode mode = phy->GetMode (i);
      if (mode.GetDataRate () == phyRate)
        {
          return mode;
        }
    }
  NS_ASSERT (false);
  return WifiMode ();
}

/***************************************************************
 *         Listener for Nav events. Forwards to DcfManager
 ***************************************************************/

class WifiNetDevice::NavListener : public ns3::MacLowNavListener {
public:
  NavListener (ns3::DcfManager *dcf)
    : m_dcf (dcf) {}
  virtual ~NavListener () {}
  virtual void NavStart (Time duration) {
    m_dcf->NotifyNavStartNow (duration);
  }
  virtual void NavReset (Time duration) {
    m_dcf->NotifyNavResetNow (duration);
  }
private:
  ns3::DcfManager *m_dcf;
};

/***************************************************************
 *         Listener for PHY events. Forwards to DcfManager
 ***************************************************************/

class WifiNetDevice::PhyListener : public ns3::WifiPhyListener {
public:
  PhyListener (ns3::DcfManager *dcf)
    : m_dcf (dcf) {}
  virtual ~PhyListener () {}
  virtual void NotifyRxStart (Time duration) {
    m_dcf->NotifyRxStartNow (duration);
  }
  virtual void NotifyRxEndOk (void) {
    m_dcf->NotifyRxEndOkNow ();
  }
  virtual void NotifyRxEndError (void) {
    m_dcf->NotifyRxEndErrorNow ();
  }
  virtual void NotifyTxStart (Time duration) {
    m_dcf->NotifyTxStartNow (duration);
  }
  virtual void NotifyCcaBusyStart (Time duration) {
    m_dcf->NotifyCcaBusyStartNow (duration);
  }
private:
  ns3::DcfManager *m_dcf;
};

/***************************************************************
 *              The NetDevice itself
 ***************************************************************/



WifiNetDevice::WifiNetDevice (Ptr<Node> node, Mac48Address self)
  : m_node (node),
    m_address (self),
    m_name (""),
    m_linkUp (false)
{
  Construct ();
}

WifiNetDevice::~WifiNetDevice ()
{}

void
WifiNetDevice::Construct (void)
{
  m_mtu = 2300;
  // the physical layer.
  m_phy = Create<WifiPhy> (this);

  // the rate control algorithm
  switch (WifiDefaultParameters::GetRateControlAlgorithm ()) {
  case WifiDefaultParameters::ARF:
    m_stations = new ArfMacStations (m_phy->GetMode (0), 
                                     WifiDefaultParameters::GetArfRateControlTimerThreshold (),
                                     WifiDefaultParameters::GetArfRateControlSuccessThreshold ());
    break;
  case WifiDefaultParameters::AARF:
    m_stations = new AarfMacStations (m_phy->GetMode (0),
                                      WifiDefaultParameters::GetAarfRateControlMinTimerThreshold (),
                                      WifiDefaultParameters::GetAarfRateControlMinSuccessThreshold (),
                                      WifiDefaultParameters::GetAarfRateControlSuccessK (),
                                      WifiDefaultParameters::GetAarfRateControlMaxSuccessThreshold (),
                                      WifiDefaultParameters::GetAarfRateControlTimerK ());
    break;
  case WifiDefaultParameters::CONSTANT_RATE: {
    WifiMode dataRate = GetWifiModeForPhyMode (m_phy, WifiDefaultParameters::GetConstantDataRate ());
    WifiMode ctlRate = GetWifiModeForPhyMode (m_phy, WifiDefaultParameters::GetConstantCtlRate ());
    m_stations = new CrMacStations (dataRate, ctlRate);
  } break;
  case WifiDefaultParameters::IDEAL: {
    double ber = WifiDefaultParameters::GetIdealRateControlBer ();
    IdealMacStations *ideal = new IdealMacStations (m_phy->GetMode (0));
    uint32_t nModes = m_phy->GetNModes ();
    for (uint32_t i = 0; i < nModes; i++) 
      {
        WifiMode mode = m_phy->GetMode (i);
        ideal->AddModeSnrThreshold (mode, m_phy->CalculateSnr (mode, ber));
      }
    m_stations = ideal;
  } break;
  case WifiDefaultParameters::ONOE: {
    m_stations = new OnoeMacStations (m_phy->GetMode (0));
  } break;
  case WifiDefaultParameters::AMRR: {
    m_stations = new AmrrMacStations (m_phy->GetMode (0));
  } break;
  case WifiDefaultParameters::RRAA: {
    m_stations = new RraaMacStations (m_phy->GetMode (0));
  } break;
  default:
    // NOTREACHED
    NS_ASSERT (false);
    break;
  }

  // MacParameters
  MacParameters *parameters = new MacParameters ();
  WifiMacHeader hdr;
  hdr.SetType (WIFI_MAC_CTL_CTS);
  Time ctsDelay = m_phy->CalculateTxDuration (hdr.GetSize () + 4, m_phy->GetMode (0), WIFI_PREAMBLE_LONG);
  hdr.SetType (WIFI_MAC_CTL_ACK);
  Time ackDelay = m_phy->CalculateTxDuration (hdr.GetSize () + 4, m_phy->GetMode (0), WIFI_PREAMBLE_LONG);
  parameters->Initialize (ctsDelay, ackDelay);
  m_parameters = parameters;
  m_stations->SetParameters (m_parameters);

  // the MacLow
  Ptr<MacLow> low = CreateObject<MacLow> ();
  low->SetDevice (this);
  low->SetPhy (m_phy);
  low->SetStations (m_stations);
  low->SetParameters (m_parameters);
  m_phy->SetReceiveOkCallback (MakeCallback (&MacLow::ReceiveOk, low));
  m_phy->SetReceiveErrorCallback (MakeCallback (&MacLow::ReceiveError, low));
  m_low = low;

  // the 'middle' rx
  MacRxMiddle *rxMiddle = new MacRxMiddle ();
  low->SetRxCallback (MakeCallback (&MacRxMiddle::Receive, rxMiddle));
  m_rxMiddle = rxMiddle;

  // the 'middle' tx
  MacTxMiddle *txMiddle = new MacTxMiddle ();
  m_txMiddle = txMiddle;

  m_manager = new DcfManager ();
  Time ackTxDuration = m_phy->CalculateTxDuration (8 * (2+2+6+4), m_phy->GetMode (0), WIFI_PREAMBLE_LONG);
  m_manager->SetAckTxDuration (ackTxDuration);
  m_manager->SetSlotTime (m_parameters->GetSlotTime ());
  m_manager->SetSifs (m_parameters->GetSifs ());
  m_phyListener = new WifiNetDevice::PhyListener (m_manager);
  m_phy->RegisterListener (m_phyListener);
  m_navListener = new WifiNetDevice::NavListener (m_manager);
  m_low->RegisterNavListener (m_navListener);
}

Ptr<DcaTxop>
WifiNetDevice::CreateDca (uint32_t minCw, uint32_t maxCw, uint32_t aifsn) const
{
  Ptr<DcaTxop> dca = CreateObject<DcaTxop> (minCw, maxCw, aifsn, m_manager);
  dca->SetParameters (m_parameters);
  dca->SetTxMiddle (m_txMiddle);
  dca->SetLow (m_low);
  dca->SetStations (m_stations);
  dca->SetMaxQueueSize (400);
  dca->SetMaxQueueDelay (Seconds (10));
  return dca;
}

Ptr<TraceResolver> 
WifiNetDevice::GetTraceResolver (void) const
{
  Ptr<CompositeTraceResolver> resolver = 
    Create<CompositeTraceResolver> ();
  resolver->AddSource ("rx", 
                       TraceDoc ("Receive a packet",
                                 "Packet", "the packet received",
                                 "Mac48Address", "the sender of the packet"),
                       m_rxLogger,
                       WifiNetDeviceTraceType (WifiNetDeviceTraceType::RX));
  resolver->AddSource ("tx", 
                       TraceDoc ("Send a packet",
                                 "Packet", "the packet to send",
                                 "Mac48Address", "the destination of the packet"),
                       m_txLogger,
                       WifiNetDeviceTraceType (WifiNetDeviceTraceType::TX));
  resolver->AddComposite ("phy", m_phy);
  resolver->AddComposite ("maclow", m_low);
  resolver->SetParentResolver (NetDevice::GetTraceResolver ());
  return resolver;
}

void 
WifiNetDevice::Attach (Ptr<WifiChannel> channel)
{
  m_channel = channel;
  m_phy->SetChannel (channel);
  NotifyAttached ();
}
void 
WifiNetDevice::DoForwardUp (Ptr<Packet> packet, const Mac48Address &from)
{
  m_rxLogger (packet, from);

  LlcSnapHeader llc;
  packet->RemoveHeader (llc);
  m_rxCallback (this, packet, llc.GetType (), from);
}
Mac48Address 
WifiNetDevice::GetSelfAddress (void) const
{
  NS_ASSERT (Mac48Address::IsMatchingType (GetAddress ()));
  Mac48Address self = Mac48Address::ConvertFrom (GetAddress ());
  return self;
}
void 
WifiNetDevice::DoDispose (void)
{
  // cleanup local
  m_node = 0;
  m_channel = 0;
  delete m_stations;
  delete m_rxMiddle;
  delete m_txMiddle;
  delete m_parameters;
  delete m_manager;
  delete m_phyListener;
  delete m_navListener;
  m_phy = 0;
  m_stations = 0;
  m_low = 0;
  m_rxMiddle = 0;
  m_txMiddle = 0;
  m_parameters = 0;
  // chain up.
  NetDevice::DoDispose ();
}

void
WifiNetDevice::NotifyLinkUp (void)
{
  m_linkUp = true;
  if (!m_linkChangeCallback.IsNull ())
    {
      m_linkChangeCallback ();
    }
}
void
WifiNetDevice::NotifyLinkDown (void)
{
  m_linkUp = false;
  m_linkChangeCallback ();
}

void 
WifiNetDevice::SetName(const std::string name)
{
  m_name = name;
}
std::string 
WifiNetDevice::GetName(void) const
{
  return m_name;
}
void 
WifiNetDevice::SetIfIndex(const uint32_t index)
{
  m_ifIndex = index;
}
uint32_t 
WifiNetDevice::GetIfIndex(void) const
{
  return m_ifIndex;
}
Ptr<Channel> 
WifiNetDevice::GetChannel (void) const
{
  return m_channel;
}
Address 
WifiNetDevice::GetAddress (void) const
{
  return m_address;
}
bool 
WifiNetDevice::SetMtu (const uint16_t mtu)
{
  m_mtu = mtu;
  return true;
}
uint16_t 
WifiNetDevice::GetMtu (void) const
{
  return m_mtu;
}
bool 
WifiNetDevice::IsLinkUp (void) const
{
  return m_linkUp;
}
void 
WifiNetDevice::SetLinkChangeCallback (Callback<void> callback)
{
  m_linkChangeCallback = callback;
}
bool 
WifiNetDevice::IsBroadcast (void) const
{
  return true;
}
Address
WifiNetDevice::GetBroadcast (void) const
{
  return Mac48Address ("ff:ff:ff:ff:ff:ff");
}
bool 
WifiNetDevice::IsMulticast (void) const
{
  return false;
}
Address 
WifiNetDevice::GetMulticast (void) const
{
  return Mac48Address ("01:00:5e:00:00:00");
}
Address 
WifiNetDevice::MakeMulticastAddress (Ipv4Address multicastGroup) const
{
  return Mac48Address ("01:00:5e:00:00:00");
}
bool 
WifiNetDevice::IsPointToPoint (void) const
{
  return false;
}
bool 
WifiNetDevice::Send(Ptr<Packet> packet, const Address& to, uint16_t protocolNumber)
{
  NS_ASSERT (Mac48Address::IsMatchingType (to));

  Mac48Address realTo = Mac48Address::ConvertFrom (to);

  LlcSnapHeader llc;
  llc.SetType (protocolNumber);
  packet->AddHeader (llc);

  m_txLogger (packet, realTo);

  return DoSendTo (packet, realTo);
}
Ptr<Node> 
WifiNetDevice::GetNode (void) const
{
  return m_node;
}
bool 
WifiNetDevice::NeedsArp (void) const
{
  return true;
}
void 
WifiNetDevice::SetReceiveCallback (NetDevice::ReceiveCallback cb)
{
  m_rxCallback = cb;
}


/*****************************************************
 *            Adhoc code
 *****************************************************/

AdhocWifiNetDevice::AdhocWifiNetDevice (Ptr<Node> node, Mac48Address self)
  : WifiNetDevice (node, self)
{
  DoConstruct ();
}
void
AdhocWifiNetDevice::DoConstruct (void)
{
  m_ssid = WifiDefaultParameters::GetSsid ();
  m_dca = CreateDca (15, 1023, 2);

  MacHighAdhoc *high = new MacHighAdhoc ();
  high->SetDevice (this);
  high->SetDcaTxop (m_dca);
  high->SetForwardCallback (MakeCallback (&AdhocWifiNetDevice::DoForwardUp, 
                                          static_cast<WifiNetDevice *> (this)));
  high->SetPhy (m_phy);
  high->SetStations (m_stations);
  m_rxMiddle->SetForwardCallback (MakeCallback (&MacHighAdhoc::Receive, high));
  m_high = high;
}

AdhocWifiNetDevice::~AdhocWifiNetDevice ()
{}
Mac48Address 
AdhocWifiNetDevice::GetBssid (void) const
{
  return m_high->GetBssid ();
}
Ssid 
AdhocWifiNetDevice::GetSsid (void) const
{
  return m_ssid;
}
void 
AdhocWifiNetDevice::SetSsid (Ssid ssid)
{
  // XXX restart adhoc network join.
  m_ssid = ssid;
}
bool
AdhocWifiNetDevice::DoSendTo (Ptr<const Packet> packet, Mac48Address const &to)
{
  m_high->Enqueue (packet, to);
  return true;
}
void
AdhocWifiNetDevice::NotifyAttached (void)
{
  NotifyLinkUp ();
}
void 
AdhocWifiNetDevice::DoDispose (void)
{
  // chain up.
  WifiNetDevice::DoDispose ();
  // local cleanup
  delete m_high;
  m_dca = 0;
  m_high = 0;
}
Ptr<TraceResolver> 
AdhocWifiNetDevice::GetTraceResolver (void) const
{
  Ptr<CompositeTraceResolver> resolver = 
    Create<CompositeTraceResolver> ();
  resolver->AddComposite ("dca", m_dca);
  resolver->SetParentResolver (WifiNetDevice::GetTraceResolver ());
  return resolver;
}


/*****************************************************
 *            STA code
 *****************************************************/

NqstaWifiNetDevice::NqstaWifiNetDevice (Ptr<Node> node, Mac48Address self)
  : WifiNetDevice (node, self)
{
  DoConstruct ();
}
void
NqstaWifiNetDevice::DoConstruct (void)
{
  m_ssid = WifiDefaultParameters::GetSsid ();
  m_dca = CreateDca (15, 1023, 2);
  
  MacHighNqsta *high = new MacHighNqsta ();
  high->SetDevice (this);
  high->SetDcaTxop (m_dca);
  high->SetForwardCallback (MakeCallback (&NqstaWifiNetDevice::DoForwardUp, 
                                          this));
  high->SetAssociatedCallback (MakeCallback (&NqstaWifiNetDevice::Associated, 
                                             this));
  high->SetDisAssociatedCallback (MakeCallback (&NqstaWifiNetDevice::DisAssociated, 
                                                this));
  high->SetStations (m_stations);
  high->SetPhy (m_phy);
  m_rxMiddle->SetForwardCallback (MakeCallback (&MacHighNqsta::Receive, high));
  m_high = high;
}

NqstaWifiNetDevice::~NqstaWifiNetDevice ()
{}
Mac48Address 
NqstaWifiNetDevice::GetBssid (void) const
{
  return m_high->GetBssid ();
}
Ssid 
NqstaWifiNetDevice::GetSsid (void) const
{
  return m_ssid;
}
void 
NqstaWifiNetDevice::StartActiveAssociation (Ssid ssid)
{
  m_ssid = ssid;
  m_high->StartActiveAssociation ();
}
bool
NqstaWifiNetDevice::DoSendTo (Ptr<const Packet> packet, Mac48Address const &to)
{
  m_high->Queue (packet, to);
  return true;
}
void
NqstaWifiNetDevice::NotifyAttached (void)
{
  // do nothing because link status is kept track of in
  // ::Associated and ::Disassociated
}
void 
NqstaWifiNetDevice::Associated (void)
{
  WifiNetDevice::NotifyLinkUp ();
}

void 
NqstaWifiNetDevice::DisAssociated (void)
{
  WifiNetDevice::NotifyLinkDown ();
}
void 
NqstaWifiNetDevice::DoDispose (void)
{
  // chain up.
  WifiNetDevice::DoDispose ();
  // local cleanup
  delete m_high;
  m_dca = 0;
  m_high = 0;
}

Ptr<TraceResolver> 
NqstaWifiNetDevice::GetTraceResolver (void) const
{
  Ptr<CompositeTraceResolver> resolver = 
    Create<CompositeTraceResolver> ();
  resolver->AddComposite ("dca", m_dca);
  resolver->SetParentResolver (WifiNetDevice::GetTraceResolver ());
  return resolver;
}

/*****************************************************
 *            AP code
 *****************************************************/


NqapWifiNetDevice::NqapWifiNetDevice (Ptr<Node> node, Mac48Address self)
  : WifiNetDevice (node, self)
{
  DoConstruct ();

}
void
NqapWifiNetDevice::DoConstruct (void)
{
  m_ssid = WifiDefaultParameters::GetSsid ();

  // The Beacon DCA is higher priority than
  // the normal DCA so, we create it first.
  m_beaconDca = CreateDca (0, 0, 1);
  m_dca = CreateDca (15, 1023, 2);

  // By default, we configure the Basic Rate Set to be the set
  // of rates we support which are mandatory.
  // This could be trivially made user-configurable
  for (uint32_t i = 0; i < m_phy->GetNModes (); i++)
    {
      WifiMode mode = m_phy->GetMode (i);
      if (mode.IsMandatory ())
        {
          m_stations->AddBasicMode (mode);
        }
    }  

  MacHighNqap *high = new MacHighNqap ();
  high->SetDevice (this);
  high->SetDcaTxop (m_dca);
  high->SetBeaconDcaTxop (m_beaconDca);
  high->SetStations (m_stations);
  high->SetPhy (m_phy);
  high->SetForwardCallback (MakeCallback (&NqapWifiNetDevice::DoForwardUp, 
                                          this));
  m_rxMiddle->SetForwardCallback (MakeCallback (&MacHighNqap::Receive, high));
  m_high = high;
}
NqapWifiNetDevice::~NqapWifiNetDevice ()
{}
Mac48Address 
NqapWifiNetDevice::GetBssid (void) const
{
  return GetSelfAddress ();
}
Ssid 
NqapWifiNetDevice::GetSsid (void) const
{
  return m_ssid;
}
void 
NqapWifiNetDevice::SetSsid (Ssid ssid)
{
  m_ssid = ssid;
}
void
NqapWifiNetDevice::StartBeaconing (void)
{
  m_high->StartBeaconing ();
}
bool
NqapWifiNetDevice::DoSendTo (Ptr<const Packet> packet, Mac48Address const & to)
{
  m_high->Queue (packet, to);
  return true;
}
void
NqapWifiNetDevice::NotifyAttached (void)
{
  NotifyLinkUp ();
}
void 
NqapWifiNetDevice::DoDispose (void)
{
  // chain up.
  WifiNetDevice::DoDispose ();
  // local cleanup
  delete m_high;
  m_dca = 0;
  m_high = 0;
  m_beaconDca = 0;
}

Ptr<TraceResolver> 
NqapWifiNetDevice::GetTraceResolver (void) const
{
  Ptr<CompositeTraceResolver> resolver = 
    Create<CompositeTraceResolver> ();
  resolver->AddComposite ("dca", m_dca);
  resolver->AddComposite ("beaconDca", m_beaconDca);
  resolver->SetParentResolver (WifiNetDevice::GetTraceResolver ());
  return resolver;
}

} // namespace ns3

