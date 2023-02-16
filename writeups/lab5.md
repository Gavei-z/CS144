# CS44 Lab5

> **In common usage, TCP segments are almost always placed directly inside an Internet datagram, without a UDP header between the IP and TCP headers**
>
> **Linux provides an interface, called a TUN device, that lets application supply an  entire Internet datagram**


It seems that we should store the IP datagram and the time since it has been sent. When we receive a new IP datagram, we should check whether it is in the list. I set up the following data structure:

```c++
    struct BlockedEthernetFrame {
        // Need two data members:
        // A data member whose type is EthernetFrame
        // This frame's blocked time
    };
```
```c++
std::unordered_map<uint32_t, BlockedEthernetFrame> blocked{};
```

when we send a datagram, we should insert it into `block` vector.

Also:

> **If the inbound frame is ARP, parse the payload as an** `ARPMessage` **and, if successful, remember the mapping between the sender’s IP address and Ethernet address for 30 seconds**

It seems that we need hash map, because we need to maintain the mapping relationship from ip address to the Ethernet address and with expiration time 30s. Similar to above, I set up the following data structure:

```c++
    struct EthernetEntry {
        // Need two data members:
        // A data member whose type is EthernetAddress (such as EthernetAddress _mac{})
        // This request's survival time
    };
    std::unordered_map<uint32_t, EthernetEntry> _arp_cache{};
```



## Information constructors

We need to create `EthernetFrame` with different type (`TYPE_IPv4` and `TYPE_ARP`, which are already defined) and with different  payload. For `TYPE_IPv4`, we do not consider payload, for `TYPE_ARP`, we need to consider its payload, we need to create the `ARPMessage` when we should reply.

Firstly, I define a function named `new_ethernet_frame`, which unifies the process of creating different kinds of `EthernetFrame`.

```c++
EthernetFrame NetworkInterface::new_ethernet_frame(uint16_t type, EthernetAddress src, EthernetAddress dst,
                                                   BufferList payload) {
    EthernetFrame frame{};
    frame.header().type = type;
    frame.header().src = src;
    frame.header().dst = dst;
    frame.payload() = payload;

    return frame;
}
```

Also, we still need a function called `create_arp_message` to create a new `ARPMessage`.

```c++
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
```



## main process: `send_datagram()` and `recv_frame()`

As to `send_datagram()`,  we need send `dgram` to `Address`.  We should check the `_arp_cache` to ensure the `EthernetAddress` exists or not. If not, we make a new `ARPMessage` to broadcast for it's `EthernetAddress`. Meantime, we store this current request and set it's `_time`.

The main idea can been got from the comments in my code.

```c++
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

        // if the `next_hop_ip` already exists, we query and check it
        if (blocked.find(next_hop_ip) != blocked.end() && blocked[next_hop_ip]._time <= 5000)
            return;

        // otherwise we make a new `ARP` for it
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
```

As to `recv_frame`,  this function only takes in the frame whose destination Ethernet address equals to the current peer's **or** those frames with `ETHERNET_BROADCAST`. It's easy to handle the former situation. When it comes to broadcast messages,  we take out the `arp_message` from `frame.payload()`  , we just return if `arp_message.target_ip_address ` doesn't equal to the current peer's `_ip_address`, and we construct a reply `ARPMessage` into Ethernet Frame as the result `arp_ethernet_frame` and push it into `frames_out()`.

```c++
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // deal with the incoming ethernet frame

    optional<InternetDatagram> new_datagram{};

    // check the destination of EthernetFrame is mine (_ethernet_address or broadcast)
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
```

