#pragma once

#include "network_interface.hh"

#include <optional>
#include <queue>

// A wrapper for NetworkInterface that makes the host-side
// interface asynchronous: instead of returning received datagrams
// immediately (from the `recv_frame` method), it stores them for
// later retrieval. Otherwise, behaves identically to the underlying
// implementation of NetworkInterface.
class AsyncNetworkInterface : public NetworkInterface
{
  std::queue<InternetDatagram> datagrams_in_ {};

public:
  using NetworkInterface::NetworkInterface;

  // Construct from a NetworkInterface
  explicit AsyncNetworkInterface( NetworkInterface&& interface ) : NetworkInterface( interface ) {}

  // \brief Receives and Ethernet frame and responds appropriately.

  // - If type is IPv4, pushes to the `datagrams_out` queue for later retrieval by the owner.
  // - If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // - If type is ARP reply, learn a mapping from the "target" fields.
  //
  // \param[in] frame the incoming Ethernet frame
  void recv_frame( const EthernetFrame& frame )
  {
    auto optional_dgram = NetworkInterface::recv_frame( frame );
    if ( optional_dgram.has_value() ) {
      datagrams_in_.push( std::move( optional_dgram.value() ) );
    }
  };

  // Access queue of Internet datagrams that have been received
  std::optional<InternetDatagram> maybe_receive()
  {
    if ( datagrams_in_.empty() ) {
      return {};
    }

    InternetDatagram datagram = std::move( datagrams_in_.front() );
    datagrams_in_.pop();
    return datagram;
  }
};

// A router that has multiple network interfaces and
// performs longest-prefix-match routing between them.
class Router
{
  // The router's collection of network interfaces
  std::vector<AsyncNetworkInterface> interfaces_ {};
  //List of 33 entries of maps from prefix_max -> <interface_num, next_hop_addr> tuples, Indexed by length of prefix match required. 
  std::unordered_map<uint32_t, std::pair<size_t, std::optional<uint32_t>>> routing_table_[33];

  //Helpers:

  uint32_t get_prefmask(uint8_t prefix_length, const uint32_t route); //This makes a mask from a route, consisting of the first prefix_length bits, shifted to the rightmost bits.

  void process_interface(size_t interface_num); //Handles all packages waiting at an interface.

  void process_dgram(InternetDatagram& dgram); //This processes the dgram, matching it to an interface to send to, and sending it, or dropping it if required. handling next hop data, etc. 

  std::optional<std::pair<size_t, uint32_t>> find_match(uint32_t dst_ip); //Uses longest prefix matching to find next hop IP and interface num to route packet, if a match is found in the table.

public:
  // Add an interface to the router
  // interface: an already-constructed network interface
  // returns the index of the interface after it has been added to the router
  size_t add_interface( AsyncNetworkInterface&& interface )
  {
    interfaces_.push_back( std::move( interface ) );
    return interfaces_.size() - 1;
  }

  // Access an interface by index
  AsyncNetworkInterface& interface( size_t N ) { return interfaces_.at( N ); }

  // Add a route (a forwarding rule)
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // Route packets between the interfaces. For each interface, use the
  // maybe_receive() method to consume every incoming datagram and
  // send it on one of interfaces to the correct next hop. The router
  // chooses the outbound interface and next-hop as specified by the
  // route with the longest prefix_length that matches the datagram's
  // destination address.
  void route();
};
