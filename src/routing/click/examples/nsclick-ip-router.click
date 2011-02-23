// Generated by make-ip-conf.pl
// eth0 172.16.1.2 00:00:00:00:00:02
// eth1 172.16.2.1 00:00:00:00:00:03

elementclass IPRouter {
  $myaddr1, $myaddr_ethernet1, $myaddr2, $myaddr_ethernet2 |

// Shared IP input path and routing table
ip :: Strip(14)
    -> CheckIPHeader2(INTERFACES 172.16.1.2/255.255.255.0 172.16.2.1/255.255.255.0)
    -> rt :: StaticIPLookup(
	172.16.1.2/32 0,
	172.16.1.255/32 0,
	172.16.1.0/32 0,
	172.16.2.1/32 0,
	172.16.2.255/32 0,
	172.16.2.0/32 0,
	172.16.1.0/255.255.255.0 1,
	172.16.2.0/255.255.255.0 2,
	255.255.255.255/32 0.0.0.0 0,
	0.0.0.0/32 0,
	0.0.0.0/0.0.0.0 18.26.4.1 1);

// ARP responses are copied to each ARPQuerier and the host.
arpt :: Tee(3);

// Input and output paths for eth0
c0 :: Classifier(12/0806 20/0001, 12/0806 20/0002, 12/0800, -);
FromSimDevice(eth0, 4096) -> c0;
out0 :: Queue(200) -> todevice0 :: ToSimDevice(eth0);
c0[0] -> ar0 :: ARPResponder(eth0) -> out0;
arpq0 :: ARPQuerier(eth0) -> out0;
c0[1] -> arpt;
arpt[0] -> [1]arpq0;
c0[2] -> Paint(1) -> ip;
c0[3] -> Discard;

// Input and output paths for eth1
c1 :: Classifier(12/0806 20/0001, 12/0806 20/0002, 12/0800, -);
FromSimDevice(eth1, 4096) -> c1;
out1 :: Queue(200) -> todevice1 :: ToSimDevice(eth1);
c1[0] -> ar1 :: ARPResponder(eth0) -> out1;
arpq1 :: ARPQuerier(eth1) -> out1;
c1[1] -> arpt;
arpt[1] -> [1]arpq1;
c1[2] -> Paint(2) -> ip;
c1[3] -> Discard;

// Local delivery
//toh :: ToSimDevice(tap0,IP);
arpt[2] -> Discard;
rt[0] -> Discard;

// Forwarding path for eth0
rt[1] -> DropBroadcasts
    -> cp0 :: PaintTee(1)
    -> gio0 :: IPGWOptions($myaddr1)
    -> FixIPSrc($myaddr1)
    -> dt0 :: DecIPTTL
    -> fr0 :: IPFragmenter(1500)
    -> [0]arpq0;
dt0[1] -> ICMPError($myaddr1, timeexceeded) -> rt;
fr0[1] -> ICMPError($myaddr1, unreachable, needfrag) -> rt;
gio0[1] -> ICMPError($myaddr1, parameterproblem) -> rt;
cp0[1] -> ICMPError($myaddr1, redirect, host) -> rt;

// Forwarding path for eth1
rt[2] -> DropBroadcasts
    -> cp1 :: PaintTee(2)
    -> gio1 :: IPGWOptions($myaddr2)
    -> FixIPSrc($myaddr2)
    -> dt1 :: DecIPTTL
    -> fr1 :: IPFragmenter(1500)
    -> [0]arpq1;
dt1[1] -> ICMPError($myaddr2, timeexceeded) -> rt;
fr1[1] -> ICMPError($myaddr2, unreachable, needfrag) -> rt;
gio1[1] -> ICMPError($myaddr2, parameterproblem) -> rt;
cp1[1] -> ICMPError($myaddr2, redirect, host) -> rt;

}

u :: IPRouter(eth0,eth0,eth1,eth1);
