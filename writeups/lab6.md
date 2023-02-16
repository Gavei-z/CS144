# CS144: lab6

This lab is easy to implement.

## add_route()

We need a data structure firstly, so the `RouterInformation` is defined, it has `route_prefix`, `prefix_length`, `next_hop`  and `interface_num`

```c++
class Router {
    ...
    //! The router information
    struct RouterInformation {
        uint32_t route_prefix;
        uint8_t prefix_length;
        std::optional<Address> next_hop;
        size_t interface_num;
    };

    //! The router table
    std::vector<RouterInformation> _router_table{};
}
```

As to the addition of route information, we just need to copy the information into the data structure above.

The code looks like this:

```c++
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    
    // add the parameters into the data structure
    RouterInformation record{};
    record.route_prefix = route_prefix;
    record.prefix_length = prefix_length;
    record.next_hop = next_hop;
    record.interface_num = interface_num;
    _router_table.push_back(record);
}
```



## route_one_datagram()

This method needs to route one datagram to the next hop, out the appropriate interface. It need to implement the "longest-prefix match" logic of an IP router to find the best route to follow.

The core idea of this module is to iterate the `_router_table` and find the longest-prefix-match.

During the iteration, we need to maintain the `max_len`.

The trick of getting the prefix is using mask. For example, `_prefix_length = 8`, we should produce `0xff000000`. We could right shift `INT_MIN` 7 bits to get that, i.e., `uint32 mask = 0x80000000 >> _prefix_length - 1`.

The code looks like this:

```c++
void Router::route_one_datagram(InternetDatagram &dgram) {
    int max_len = -1;
    optional<Address> next_hop{};
    int interface_num = -1;

    // The Router searches the routing table to find the routes that match the datagram’s
	// destination address. By \match," we mean the most-significant prefix length bits of
	// the destination address are identical to the most-significant prefix length bits of the route prefix.
    for (auto &&record: _router_table) {
        uint32_t mask = record.prefix_length == 0 ? 0 :numeric_limits<int>:: min() >> (record.prefix_length - 1);
        if ((dgram.header().dst & mask) == record.route_prefix && max_len < record.prefix_length) {
            max_len = record.prefix_length;
            next_hop = record.next_hop;
            interface_num = record.interface_num;
        }
    }

    if (max_len == -1 || dgram.header().ttl == 0 || --dgram.header().ttl == 0) return;

    if (next_hop.has_value())
        _interfaces[interface_num].send_datagram(dgram, next_hop.value());
    else
        _interfaces[interface_num].send_datagram(dgram, Address::from_ipv4_numeric(dgram.header().dst));
}
```

we should deal with the corner case in the code. `dgram.header().ttl == 0 || --dgram.header().ttl == 0` means there is no time for the current datagram to live.

`ttl` => time to live field.

if we get the `next_hop` address, we just send the datagram to it.