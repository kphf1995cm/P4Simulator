/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) YEAR COPYRIGHTHOLDER
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
* Author:
*/

#include "ns3/p4-net-device.h"
#include "ns3/helper.h"
#include "ns3/global.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/ethernet-header.h"
#include "ns3/arp-l3-protocol.h"
#include <bm/bm_sim/switch.h>
#include <bm/bm_sim/core/primitives.h>
#include <bm/bm_runtime/bm_runtime.h>
#include <bm/bm_sim/simple_pre.h>
#include <bm/SimpleSwitch.h>
#include <bm/bm_sim/options_parse.h>
#include <memory.h>
#include <memory>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <array>
#include <chrono>
#include <thread>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fstream>
#include <map>

using namespace ns3;

#define MAXSIZE 512

NS_OBJECT_ENSURE_REGISTERED(P4NetDevice);

NS_LOG_COMPONENT_DEFINE("P4NetDevice");

TypeId P4NetDevice::GetTypeId(void)
{
	static TypeId tid =
		TypeId("ns3::P4NetDevice").SetParent<NetDevice>().SetGroupName("P4").AddConstructor<
		P4NetDevice>().AddAttribute("Mtu",
			"The MAC-level Maximum Transmission Unit",
			UintegerValue(1500),
			MakeUintegerAccessor(&P4NetDevice::SetMtu,
				&P4NetDevice::GetMtu),
			MakeUintegerChecker<uint16_t>());
	return tid;
}

// Call external commands. Maybe used in controller model.
std::string exec(const char* cmd)
{
	std::array<char, 128> buffer;
	std::string result;
	std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
	if (!pipe)
		throw std::runtime_error("popen() failed!");
	while (!feof(pipe.get())) {
		if (fgets(buffer.data(), 128, pipe.get()) != NULL)
			result += buffer.data();
	}
	return result;
}

P4NetDevice::P4NetDevice() :
	m_node(0), m_ifIndex(0) {
	NS_LOG_FUNCTION_NOARGS();
	m_channel = CreateObject<BridgeChannel>();// Use BridgeChannel for prototype. Will develop P4 Channel in the future.
	P4SwitchInterface* p4Switch = P4GlobalVar::g_p4Controller.AddP4Switch();

	p4Model = new P4Model(this);

	p4Switch->SetP4Model(p4Model);
	p4Switch->SetJsonPath(P4GlobalVar::g_p4JsonPath);
	p4Switch->SetP4InfoPath(P4GlobalVar::g_p4MatchTypePath);
	p4Switch->SetFlowTablePath(P4GlobalVar::g_flowTablePath);
	p4Switch->SetViewFlowTablePath(P4GlobalVar::g_viewFlowTablePath);
	p4Switch->SetNetworkFunc(P4GlobalVar::g_networkFunc);

	char * a3;
	a3 = (char*)P4GlobalVar::g_p4JsonPath.data();
	char * args[2] = { NULL,a3 };
	p4Model->init(2, args);

	//TO DO: call P4Model start_and_return_ ,start mutiple thread

	//Init P4Model Flow Table
	if (P4GlobalVar::g_populateFlowTableWay == LOCAL_CALL)
		p4Switch->Init();

	p4Switch = NULL;
	NS_LOG_LOGIC("A P4 Netdevice was initialized.");
}

void P4NetDevice::ReceiveFromDevice(Ptr<ns3::NetDevice> device,
	Ptr<const ns3::Packet> packetIn, uint16_t protocol,
	Address const &source, Address const &destination,
	PacketType packetType)
{
	NS_LOG_FUNCTION(this);

	Mac48Address src48 = Mac48Address::ConvertFrom(source);
	Mac48Address dst48 = Mac48Address::ConvertFrom(destination);

	int inPort = GetPortNumber(device);

	EthernetHeader eeh;
	eeh.SetDestination(dst48);
	eeh.SetSource(src48);
	eeh.SetLengthType(protocol);

	Ptr<ns3::Packet> ns3Packet((ns3::Packet*)PeekPointer(packetIn));

	ns3Packet->AddHeader(eeh);

	//Call P4Model receive packet, no return value
	p4Model->ReceivePacket(ns3Packet, inPort, protocol, destination);
}

void P4NetDevice::SendNs3Packet(Ptr<ns3::Packet> packetOut, int outPort, uint16_t protocol, Address const &destination)
{
	if (packetOut)
	{
		EthernetHeader eeh;
		packetOut->RemoveHeader(eeh);
		if (outPort != 511)
		{
			NS_LOG_LOGIC("EgressPortNum: " << outPort);
			Ptr<NetDevice> outNetDevice = GetBridgePort(outPort);
			outNetDevice->Send(packetOut->Copy(), destination, protocol);
		}
		else
			std::cout << "Drop Packet!!!(511)\n";
	}
	else
		std::cout << "Null Packet!\n";
}

void P4NetDevice::AddBridgePort(Ptr<NetDevice> bridgePort)
{
	if (!Mac48Address::IsMatchingType(bridgePort->GetAddress())) {
		NS_FATAL_ERROR("Device does not support eui 48 addresses: cannot be added to bridge.");
	}
	if (!bridgePort->SupportsSendFrom()) {
		NS_FATAL_ERROR("Device does not support SendFrom: cannot be added to bridge.");
	}
	if (m_address == Mac48Address()) {
		m_address = Mac48Address::ConvertFrom(bridgePort->GetAddress());
	}

	m_node->RegisterProtocolHandler(
		MakeCallback(&P4NetDevice::ReceiveFromDevice, this), 0, bridgePort,
		true);
	m_ports.push_back(bridgePort);
	m_channel->AddChannel(bridgePort->GetChannel());
}

int P4NetDevice::GetPortNumber(Ptr<NetDevice> port) {
	int portsNum = GetNBridgePorts();
	for (int i = 0; i < portsNum; i++) {
		if (GetBridgePort(i) == port)
			return i;
	}
	return -1;
}

uint32_t
P4NetDevice::GetNBridgePorts(void) const {
	// NS_LOG_FUNCTION_NOARGS ();
	return m_ports.size();
}

Ptr<NetDevice>
P4NetDevice::GetBridgePort(uint32_t n) const {
	// NS_LOG_FUNCTION_NOARGS ();
	if (n >= m_ports.size()) return NULL;
	return m_ports[n];
}

P4NetDevice::~P4NetDevice() {
	if (p4Model != NULL)
		delete p4Model;
	NS_LOG_FUNCTION_NOARGS();
}

void
P4NetDevice::DoDispose() {
	NS_LOG_FUNCTION_NOARGS();
	for (std::vector< Ptr<NetDevice> >::iterator iter = m_ports.begin(); iter != m_ports.end(); iter++) {
		*iter = 0;
	}
	m_ports.clear();
	m_channel = 0;
	m_node = 0;
	NetDevice::DoDispose();
}

void
P4NetDevice::SetIfIndex(const uint32_t index) {
	NS_LOG_FUNCTION_NOARGS();
	m_ifIndex = index;
}

uint32_t
P4NetDevice::GetIfIndex(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return m_ifIndex;
}

Ptr<Channel>
P4NetDevice::GetChannel(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return m_channel;
}

void
P4NetDevice::SetAddress(Address address) {
	NS_LOG_FUNCTION_NOARGS();
	m_address = Mac48Address::ConvertFrom(address);
}

Address
P4NetDevice::GetAddress(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return m_address;
}

bool
P4NetDevice::SetMtu(const uint16_t mtu) {
	NS_LOG_FUNCTION_NOARGS();
	m_mtu = mtu;
	return true;
}

uint16_t
P4NetDevice::GetMtu(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return m_mtu;
}


bool
P4NetDevice::IsLinkUp(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}


void
P4NetDevice::AddLinkChangeCallback(Callback<void> callback)
{}


bool
P4NetDevice::IsBroadcast(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}


Address
P4NetDevice::GetBroadcast(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return Mac48Address("ff:ff:ff:ff:ff:ff");
}

bool
P4NetDevice::IsMulticast(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}

Address
P4NetDevice::GetMulticast(Ipv4Address multicastGroup) const {
	NS_LOG_FUNCTION(this << multicastGroup);
	Mac48Address multicast = Mac48Address::GetMulticast(multicastGroup);
	return multicast;
}


bool
P4NetDevice::IsPointToPoint(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return false;
}

bool
P4NetDevice::IsBridge(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}


bool
P4NetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) {
	return false;
}

bool
P4NetDevice::SendFrom(Ptr<Packet> packet, const Address& src, const Address& dest, uint16_t protocolNumber) {
	return false;
}

bool
P4NetDevice::SendPacket(Ptr<Packet> packet, const Address& dest, Ptr<NetDevice>outDevice) {
	NS_LOG_FUNCTION_NOARGS();
	// outDevice->Send();
	return false;
}

bool P4NetDevice::SendPacket(Ptr<Packet> packet, Ptr<NetDevice>outDevice) {
	NS_LOG_FUNCTION_NOARGS();
	// outDevice->Send();
	return false;
}


Ptr<Node>
P4NetDevice::GetNode(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return m_node;
}


void
P4NetDevice::SetNode(Ptr<Node> node) {
	NS_LOG_FUNCTION_NOARGS();
	m_node = node;
}


bool
P4NetDevice::NeedsArp(void) const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}


void
P4NetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb) {
	NS_LOG_FUNCTION_NOARGS();
	m_rxCallback = cb;
}

void
P4NetDevice::SetPromiscReceiveCallback(NetDevice::PromiscReceiveCallback cb) {
	NS_LOG_FUNCTION_NOARGS();
	m_promiscRxCallback = cb;
}

bool
P4NetDevice::SupportsSendFrom() const {
	NS_LOG_FUNCTION_NOARGS();
	return true;
}

Address P4NetDevice::GetMulticast(Ipv6Address addr) const {
	NS_LOG_FUNCTION(this << addr);
	return Mac48Address::GetMulticast(addr);
}





