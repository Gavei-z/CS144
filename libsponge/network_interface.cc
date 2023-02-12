#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

EthernetFrame NetworkInterface::new_ethernet_frame(uint16_t type, EthernetAddress src, EthernetAddress dst,
                                                   BufferList payload) {
    EthernetFrame frame{};
    frame.header().type = type;
    frame.header().src = src;
    frame.header().dst = dst;
    frame.payload() = payload;

    return frame;
}

void NetworkInterface::set_ethernet_frame_dst(EthernetFrame &frame, EthernetAddress dst) { frame.header().dst = dst; }

ARPMessage NetworkInterface::create_arp_message(uint32_t sender_ip_address, EthernetAddress sender_ethernet_address,
                                                uint32_t target_ip_address, EthernetAddress target_ethernet_address,
                                                uint16_t opcode) {
    ARPMessage message{};
    message.sender_ip_address = sender_ip_address;
    message.sender_ethernet_address = sender_ethernet_address;
    message.target_ip_address = target_ip_address;
    message.target_ethernet_address = target_ethernet_address;
    message.opcode = opcode;
    return message;
}

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // create an ipv4 type EthernetFrame
    EthernetFrame ipv4_ethernet_frame =
            new_ethernet_frame(EthernetHeader::TYPE_IPv4, _ethernet_address, {}, dgram.serialize());

    // When the hardware address is in cache and it does not expire.
    if (_arp_cache.find(next_hop_ip) != _arp_cache.end() && _arp_cache[next_hop_ip]._time <= 30000) {
        set_ethernet_frame_dst(ipv4_ethernet_frame, _arp_cache[next_hop_ip]._mac);
        frames_out().push(ipv4_ethernet_frame);
    } else {
        // We do not allow ARP flood, add a rate limit
        if (blocked.find(next_hop_ip) != blocked.end() && blocked[next_hop_ip]._time <= 5000)
            return;

        ARPMessage arp_message = create_arp_message(
                _ip_address.ipv4_numeric(), _ethernet_address, next_hop_ip, {}, ARPMessage::OPCODE_REQUEST);
        EthernetFrame arp_ethernet_frame = new_ethernet_frame(EthernetHeader::TYPE_ARP,
                                                              _ethernet_address,
                                                              ETHERNET_BROADCAST,
                                                              BufferList{arp_message.serialize()});

        BlockedEthernetFrame new_blocked_datagram{};
        new_blocked_datagram._time = 0;
        new_blocked_datagram._frame = ipv4_ethernet_frame;

        blocked.insert({next_hop_ip, new_blocked_datagram});
        frames_out().push(arp_ethernet_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // deal with the incoming ethernet frame

    optional<InternetDatagram> new_datagram{};

    // We need to check whether the EthernetFrame is correct
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) {
        return new_datagram;
    }
    // If the EthernetFrame type is IPv4, we just parse its payload
    // to get the ip segment
    if (frame.header().type == frame.header().TYPE_IPv4) {
        InternetDatagram ipv4_datagram{};
        ipv4_datagram.parse(frame.payload());
        new_datagram.emplace(ipv4_datagram);
    } else {
        ARPMessage arp_message{};
        arp_message.parse(frame.payload());

        // If it asks for mac
        if (arp_message.target_ip_address != _ip_address.ipv4_numeric()) {
            return new_datagram;
        }

        // As long as we receive a ARPMessage, we should update the cache and
        // pushes the ipv4 segment.
        _arp_cache[arp_message.sender_ip_address]._mac = arp_message.sender_ethernet_address;
        _arp_cache[arp_message.sender_ip_address]._time = 0;
        auto iter = blocked.find(arp_message.sender_ip_address);
        if (iter != blocked.end()) {
            iter->second._frame.header().dst = arp_message.sender_ethernet_address;
            frames_out().push(iter->second._frame);
            blocked.erase(iter);
        }

        // When sending
        if (arp_message.opcode == arp_message.OPCODE_REQUEST) {
            ARPMessage message = create_arp_message(_ip_address.ipv4_numeric(),
                                                    _ethernet_address,
                                                    arp_message.sender_ip_address,
                                                    arp_message.sender_ethernet_address,
                                                    ARPMessage::OPCODE_REPLY);

            EthernetFrame arp_ethernet_frame = new_ethernet_frame(EthernetHeader::TYPE_ARP,
                                                                  _ethernet_address,
                                                                  arp_message.sender_ethernet_address,
                                                                  BufferList{message.serialize()});
            frames_out().push(arp_ethernet_frame);
        }
    }
    return new_datagram;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // We should iterate the `blocked`
    for (auto &&entry : blocked) {
        entry.second._time += ms_since_last_tick;
    }
    // We should iterate the `_arp_cache`
    for (auto &&cache : _arp_cache) {
        cache.second._time += ms_since_last_tick;
    }
}
