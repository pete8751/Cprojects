#include "router.hh"
#include "arp_message.hh"
#include "network_interface_test_harness.hh"

#include <iostream>
#include <list>
#include <random>
#include <unordered_map>
#include <utility>

using namespace std;

EthernetAddress random_host_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) |= 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 0 ) &= 0xfe;

  return addr;
}

EthernetAddress random_router_ethernet_address()
{
  EthernetAddress addr;
  for ( auto& byte : addr ) {
    byte = random_device()(); // use a random local Ethernet address
  }
  addr.at( 0 ) = 0x02; // "10" in last two binary digits marks a private Ethernet address
  addr.at( 1 ) = 0;
  addr.at( 2 ) = 0;

  return addr;
}

uint32_t ip( const string& str )
{
  return Address { str }.ipv4_numeric();
}

class Host
{
  string _name;
  Address _my_address;
  AsyncNetworkInterface _interface;
  Address _next_hop;

  std::list<InternetDatagram> _expecting_to_receive {};

  bool expecting( const InternetDatagram& expected ) const
  {
    return ranges::any_of( _expecting_to_receive, [&expected]( const auto& x ) { return equal( x, expected ); } );
  }

  void remove_expectation( const InternetDatagram& expected )
  {
    for ( auto it = _expecting_to_receive.begin(); it != _expecting_to_receive.end(); ++it ) {
      if ( equal( *it, expected ) ) {
        _expecting_to_receive.erase( it );
        return;
      }
    }
  }

public:
  Host( string name, const Address& my_address, const Address& next_hop ) // NOLINT(*-easily-swappable-*)
    : _name( std::move( name ) )
    , _my_address( my_address )
    , _interface( random_host_ethernet_address(), _my_address )
    , _next_hop( next_hop )
  {}

  InternetDatagram send_to( const Address& destination, const uint8_t ttl = 64 )
  {
    InternetDatagram dgram;
    dgram.header.src = _my_address.ipv4_numeric();
    dgram.header.dst = destination.ipv4_numeric();
    dgram.payload.emplace_back( string { "random payload: {" + to_string( random_device()() ) + "}" } );
    dgram.header.len = static_cast<uint64_t>( dgram.header.hlen ) * 4 + dgram.payload.back().size();
    dgram.header.ttl = ttl;
    dgram.header.compute_checksum();

    _interface.send_datagram( dgram, _next_hop );

    cerr << "Host " << _name << " trying to send datagram (with next hop = " << _next_hop.ip()
         << "): " << dgram.header.to_string()
         << +" payload=\"" + Printer::prettify( concat( dgram.payload ) ) + "\"\n";

    return dgram;
  }

  const Address& address() { return _my_address; }

  AsyncNetworkInterface& interface() { return _interface; }

  void expect( const InternetDatagram& expected ) { _expecting_to_receive.push_back( expected ); }

  const string& name() { return _name; }

  void check()
  {
    while ( auto dgram_received = _interface.maybe_receive() ) {
      if ( not expecting( *dgram_received ) ) {
        throw runtime_error( "Host " + _name
                             + " received unexpected Internet datagram: " + dgram_received->header.to_string() );
      }
      remove_expectation( *dgram_received );
    }

    if ( not _expecting_to_receive.empty() ) {
      throw runtime_error( "Host " + _name + " did NOT receive an expected Internet datagram: "
                           + _expecting_to_receive.front().header.to_string() );
    }
  }
};

class Network
{
private:
  Router _router {};

  size_t default_id, eth0_id, eth1_id, eth2_id, uun3_id, hs4_id, mit5_id;

  size_t eth3_id, eth4_id; 

  std::unordered_map<string, Host> _hosts {};

  static void exchange_frames( const string& x_name,
                               AsyncNetworkInterface& x,
                               const string& y_name,
                               AsyncNetworkInterface& y )
  {
    deliver( x_name, x, y_name, y );
    deliver( y_name, y, x_name, x );
  }

  static void exchange_frames( const string& x_name,
                               AsyncNetworkInterface& x,
                               const string& y_name,
                               AsyncNetworkInterface& y,
                               const string& z_name,
                               AsyncNetworkInterface& z )
  {
    deliver( x_name, x, y_name, y, z_name, z );
    deliver( y_name, y, x_name, x, z_name, z );
    deliver( z_name, z, x_name, x, y_name, y );
  }

  static void deliver( const string& src_name,
                       AsyncNetworkInterface& src,
                       const string& dst_name,
                       AsyncNetworkInterface& dst )
  {
    while ( optional<EthernetFrame> frame = src.maybe_send() ) {
      cerr << "Transferring frame from " << src_name << " to " << dst_name << ": " << summary( *frame ) << "\n";
      dst.recv_frame( *frame );
    }
  }

  static void deliver( const string& src_name,
                       AsyncNetworkInterface& src,
                       const string& dst1_name,
                       AsyncNetworkInterface& dst1,
                       const string& dst2_name,
                       AsyncNetworkInterface& dst2 )
  {
    while ( optional<EthernetFrame> frame = src.maybe_send() ) {
      cerr << "Transferring frame from " << src_name << " to " << dst1_name << " and " << dst2_name << ": "
           << summary( *frame ) << "\n";
      dst1.recv_frame( *frame );
      dst2.recv_frame( *frame );
    }
  }

public:
  Network()
    : default_id( _router.add_interface( { random_router_ethernet_address(), Address { "171.67.76.46" } } ) )
    , eth0_id( _router.add_interface( { random_router_ethernet_address(), Address { "10.0.0.1" } } ) )
    , eth1_id( _router.add_interface( { random_router_ethernet_address(), Address { "172.16.0.1" } } ) )
    , eth2_id( _router.add_interface( { random_router_ethernet_address(), Address { "192.168.0.1" } } ) )
    , uun3_id( _router.add_interface( { random_router_ethernet_address(), Address { "198.178.229.1" } } ) )
    , hs4_id ( _router.add_interface( { random_router_ethernet_address(), Address { "143.195.0.2" } } ) )
    , mit5_id( _router.add_interface( { random_router_ethernet_address(), Address { "128.30.76.255" } } ) )

    , eth3_id( _router.add_interface( { random_router_ethernet_address(), Address { "100.70.0.1" } } ) )
    , eth4_id( _router.add_interface( { random_router_ethernet_address(), Address { "100.70.1.1" } } ) )

  {
    _hosts.insert( { "applesauce", { "applesauce", Address { "10.0.0.2" }, Address { "10.0.0.1" } } } );
    _hosts.insert( { "default_router", { "default_router", Address { "171.67.76.1" }, Address { "0" } } } );
    _hosts.insert( { "cherrypie", { "cherrypie", Address { "192.168.0.2" }, Address { "192.168.0.1" } } } );
    _hosts.insert( { "hs_router", { "hs_router", Address { "143.195.0.1" }, Address { "0" } } } );
    _hosts.insert( { "dm42", { "dm42", Address { "198.178.229.42" }, Address { "198.178.229.1" } } } );
    _hosts.insert( { "dm43", { "dm43", Address { "198.178.229.43" }, Address { "198.178.229.1" } } } );

    _hosts.insert( { "blueberrymuffin", { "blueberrymuffin", Address { "100.70.0.2" }, Address { "100.70.0.1" } } } );
    _hosts.insert( { "doughnut", { "doughnut", Address { "100.70.1.2" }, Address { "100.70.1.1" } } } );

    // not connected. hence the sadness
    _hosts.insert( { "sadlittlehost", { "sadlittlehost", Address { "200.0.0.1" }, Address { "200.0.0.2" } } } );

    // _router.add_route( ip( "0.0.0.0" ), 0, host( "default_router" ).address(), default_id );
    _router.add_route( ip( "10.0.0.0" ), 8, {}, eth0_id );
    _router.add_route( ip( "172.16.0.0" ), 16, {}, eth1_id );
    _router.add_route( ip( "192.168.0.0" ), 24, {}, eth2_id );
    _router.add_route( ip( "198.178.229.0" ), 24, {}, uun3_id );
    _router.add_route( ip( "143.195.0.0" ), 17, host( "hs_router" ).address(), hs4_id );
    _router.add_route( ip( "143.195.128.0" ), 18, host( "hs_router" ).address(), hs4_id );
    _router.add_route( ip( "143.195.192.0" ), 19, host( "hs_router" ).address(), hs4_id );
    _router.add_route( ip( "128.30.76.255" ), 16, Address { "128.30.0.1" }, mit5_id );

    _router.add_route( ip( "100.70.0.0" ), 16, {}, eth3_id );
    _router.add_route( ip( "100.70.1.0" ), 24, {}, eth4_id );
  }

  void simulate_physical_connections()
  {
    exchange_frames( "router.default", _router.interface( default_id ), "default_router", host( "default_router" ).interface() );
    exchange_frames( "router.eth0", _router.interface( eth0_id ), "applesauce", host( "applesauce" ).interface() );
    exchange_frames( "router.eth2", _router.interface( eth2_id ), "cherrypie", host( "cherrypie" ).interface() );
    exchange_frames( "router.hs4", _router.interface( hs4_id ), "hs_router", host( "hs_router" ).interface() );
    exchange_frames( "router.uun3",
                     _router.interface( uun3_id ),
                     "dm42",
                     host( "dm42" ).interface(),
                     "dm43",
                     host( "dm43" ).interface() );

    exchange_frames( "router.eth3", _router.interface( eth3_id ), "blueberrymuffin", host( "blueberrymuffin" ).interface() );
    exchange_frames( "router.eth4", _router.interface( eth4_id ), "doughnut", host( "doughnut" ).interface() );

  }

  void simulate()
  {
    for ( unsigned int i = 0; i < 256; i++ ) {
      _router.route();
      simulate_physical_connections();
    }

    for ( auto& host : _hosts ) {
      host.second.check();
    }
  }

  Host& host( const string& name )
  {
    auto it = _hosts.find( name );
    if ( it == _hosts.end() ) {
      throw runtime_error( "unknown host: " + name );
    }
    if ( it->second.name() != name ) {
      throw runtime_error( "invalid host: " + name );
    }
    return it->second;
  }
};

