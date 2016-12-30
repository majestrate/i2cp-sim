#include <cassert>
#include "Base.h"
#include "Log.h"
#include "Destination.h"
#include "ClientContext.h"
#include "I2PTunnel.h"

namespace i2p
{
namespace client
{

	/** set standard socket options */
	static void I2PTunnelSetSocketOptions(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		if (socket && socket->is_open())
		{			 
			boost::asio::socket_base::receive_buffer_size option(I2P_TUNNEL_CONNECTION_BUFFER_SIZE);
			socket->set_option(option);
		}
	}
	
	I2PTunnelConnection::I2PTunnelConnection (I2PService * owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket,
		std::shared_ptr<const i2p::data::LeaseSet> leaseSet, int port): 
		I2PServiceHandler(owner), m_Socket (socket), m_RemoteEndpoint (socket->remote_endpoint ()),
		m_IsQuiet (true)
	{
		m_Stream = GetOwner()->GetLocalDestination ()->CreateStream (leaseSet, port);
	}	

	I2PTunnelConnection::I2PTunnelConnection (I2PService * owner,
	    std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<i2p::stream::Stream> stream):
		I2PServiceHandler(owner), m_Socket (socket), m_Stream (stream),
		m_RemoteEndpoint (socket->remote_endpoint ()), m_IsQuiet (true)
	{
	}

	I2PTunnelConnection::I2PTunnelConnection (I2PService * owner, std::shared_ptr<i2p::stream::Stream> stream,
	    std::shared_ptr<boost::asio::ip::tcp::socket> socket, const boost::asio::ip::tcp::endpoint& target, bool quiet):
		I2PServiceHandler(owner), m_Socket (socket), m_Stream (stream),
		m_RemoteEndpoint (target), m_IsQuiet (quiet)
	{
	}

	I2PTunnelConnection::~I2PTunnelConnection ()
	{
	}	
  
	void I2PTunnelConnection::I2PConnect (const uint8_t * msg, size_t len)
	{
		if (m_Stream)
		{
			if (msg)
				m_Stream->Send (msg, len); // connect and send
			else	
				m_Stream->Send (m_Buffer, 0); // connect
		}
		StreamReceive ();
		Receive ();
	}

	static boost::asio::ip::address GetLoopbackAddressFor(const i2p::data::IdentHash & addr)
	{
		boost::asio::ip::address_v4::bytes_type bytes;
		const uint8_t * ident = addr;
		bytes[0] = 127;
		memcpy (bytes.data ()+1, ident, 3);
		boost::asio::ip::address ourIP = boost::asio::ip::address_v4 (bytes);
		return ourIP;
	}
	
	static void MapToLoopback(const std::shared_ptr<boost::asio::ip::tcp::socket> & sock, const i2p::data::IdentHash & addr)
	{

		// bind to 127.x.x.x address 
		// where x.x.x are first three bytes from ident
		auto ourIP = GetLoopbackAddressFor(addr);
		sock->bind (boost::asio::ip::tcp::endpoint (ourIP, 0));

	}

	void I2PTunnelConnection::Connect (bool mapToLoopback)
	{
		I2PTunnelSetSocketOptions(m_Socket);
		if (m_Socket) {

#ifdef __linux__
			if (m_RemoteEndpoint.address ().is_v4 () &&
				m_RemoteEndpoint.address ().to_v4 ().to_bytes ()[0] == 127 && mapToLoopback)
			{
				m_Socket->open (boost::asio::ip::tcp::v4 ());
				auto ident = m_Stream->GetRemoteIdentity()->GetIdentHash();
				MapToLoopback(m_Socket, ident);
			}
#endif
 			m_Socket->async_connect (m_RemoteEndpoint, std::bind (&I2PTunnelConnection::HandleConnect,
				shared_from_this (), std::placeholders::_1));
		}
	}	
		
	void I2PTunnelConnection::Terminate ()
	{
		if (Kill()) return;
		if (m_Stream)
		{
			m_Stream->Close ();
			m_Stream.reset ();
		}	
		m_Socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send); // avoid RST
		m_Socket->close ();

		Done(shared_from_this ());
	}			

	void I2PTunnelConnection::Receive ()
	{
		m_Socket->async_read_some (boost::asio::buffer(m_Buffer, I2P_TUNNEL_CONNECTION_BUFFER_SIZE),                
			std::bind(&I2PTunnelConnection::HandleReceived, shared_from_this (), 
			std::placeholders::_1, std::placeholders::_2));
	}	
	
	void I2PTunnelConnection::HandleReceived (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				LogPrint (eLogError, "I2PTunnel: read error: ", ecode.message ());
				Terminate ();
			}
		}
		else
		{	
			if (m_Stream)
			{	
				auto s = shared_from_this ();
				m_Stream->AsyncSend (m_Buffer, bytes_transferred,
					[s](const boost::system::error_code& ecode)
				    {
						if (!ecode)
							s->Receive ();
						else
							s->Terminate ();
					});
			}	
		}
	}	

	void I2PTunnelConnection::HandleWrite (const boost::system::error_code& ecode)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PTunnel: write error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
				Terminate ();
		}
		else
			StreamReceive ();
	}

	void I2PTunnelConnection::StreamReceive ()
	{
		if (m_Stream)
		{
			if (m_Stream->GetStatus () == i2p::stream::eStreamStatusNew ||
			    m_Stream->GetStatus () == i2p::stream::eStreamStatusOpen) // regular
			{	
				m_Stream->AsyncReceive (boost::asio::buffer (m_StreamBuffer, I2P_TUNNEL_CONNECTION_BUFFER_SIZE),
					std::bind (&I2PTunnelConnection::HandleStreamReceive, shared_from_this (),
						std::placeholders::_1, std::placeholders::_2),
					I2P_TUNNEL_CONNECTION_MAX_IDLE);
			}	
			else // closed by peer
			{
				// get remaning data
				auto len = m_Stream->ReadSome (m_StreamBuffer, I2P_TUNNEL_CONNECTION_BUFFER_SIZE);
				if (len > 0) // still some data
					Write (m_StreamBuffer, len);
				else // no more data
					Terminate (); 
			}		
		}	
	}	

	void I2PTunnelConnection::HandleStreamReceive (const boost::system::error_code& ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PTunnel: stream read error: ", ecode.message ());
			if (ecode != boost::asio::error::operation_aborted)
			{
				if (bytes_transferred > 0)
					Write (m_StreamBuffer, bytes_transferred); // postpone termination
				else if (ecode == boost::asio::error::timed_out && m_Stream && m_Stream->IsOpen ())
					StreamReceive ();
				else
					Terminate ();
			}
			else
				Terminate ();
		}
		else
			Write (m_StreamBuffer, bytes_transferred);
	}

	void I2PTunnelConnection::Write (const uint8_t * buf, size_t len)
	{
		boost::asio::async_write (*m_Socket, boost::asio::buffer (buf, len), boost::asio::transfer_all (),
        	std::bind (&I2PTunnelConnection::HandleWrite, shared_from_this (), std::placeholders::_1));
	}

	void I2PTunnelConnection::HandleConnect (const boost::system::error_code& ecode)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PTunnel: connect error: ", ecode.message ());
			Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "I2PTunnel: connected");
			if (m_IsQuiet)
				StreamReceive ();
			else
			{
				// send destination first like received from I2P
				std::string dest = m_Stream->GetRemoteIdentity ()->ToBase64 ();
				dest += "\n";
				if(sizeof(m_StreamBuffer) >= dest.size()) {
					memcpy (m_StreamBuffer, dest.c_str (), dest.size ());
				}
				HandleStreamReceive (boost::system::error_code (), dest.size ());
			}	
			Receive ();	
		}
	}

	I2PTunnelConnectionHTTP::I2PTunnelConnectionHTTP (I2PService * owner, std::shared_ptr<i2p::stream::Stream> stream,
		std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
		const boost::asio::ip::tcp::endpoint& target, const std::string& host):
		I2PTunnelConnection (owner, stream, socket, target), m_Host (host), m_HeaderSent (false), m_From (stream->GetRemoteIdentity ())
	{
	}

	void I2PTunnelConnectionHTTP::Write (const uint8_t * buf, size_t len)
	{
		if (m_HeaderSent)
			I2PTunnelConnection::Write (buf, len);
		else
		{	
			m_InHeader.clear ();
			m_InHeader.write ((const char *)buf, len);
			std::string line;
			bool endOfHeader = false;
			while (!endOfHeader)
			{
				std::getline(m_InHeader, line);
				if (!m_InHeader.fail ())
				{
					if (line == "\r") endOfHeader = true;
					else
					{	
						if (m_Host.length () > 0 && line.find ("Host:") != std::string::npos)
							m_OutHeader << "Host: " << m_Host << "\r\n"; // override host
						else
							m_OutHeader << line << "\n";
					}	
				}
				else
					break;
			}
			// add X-I2P fields
			if (m_From)
			{
				m_OutHeader << X_I2P_DEST_B32 << ": " << context.GetAddressBook ().ToAddress(m_From->GetIdentHash ()) << "\r\n";
				m_OutHeader << X_I2P_DEST_HASH << ": " << m_From->GetIdentHash ().ToBase64 () << "\r\n";
				m_OutHeader << X_I2P_DEST_B64 << ": " << m_From->ToBase64 () << "\r\n";
			}

			if (endOfHeader)
			{
				m_OutHeader << "\r\n"; // end of header
				m_OutHeader << m_InHeader.str ().substr (m_InHeader.tellg ()); // data right after header
				m_HeaderSent = true;
				I2PTunnelConnection::Write ((uint8_t *)m_OutHeader.str ().c_str (), m_OutHeader.str ().length ());
			}
		}	
	}

	I2PTunnelConnectionIRC::I2PTunnelConnectionIRC (I2PService * owner, std::shared_ptr<i2p::stream::Stream> stream,
        std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
       	const boost::asio::ip::tcp::endpoint& target, const std::string& webircpass):
        I2PTunnelConnection (owner, stream, socket, target), m_From (stream->GetRemoteIdentity ()), 
        m_NeedsWebIrc (webircpass.length() ? true : false), m_WebircPass (webircpass)
    {
    }

    void I2PTunnelConnectionIRC::Write (const uint8_t * buf, size_t len)
    {
		m_OutPacket.str ("");
    	if (m_NeedsWebIrc) 
		{
        	m_NeedsWebIrc = false;
            m_OutPacket << "WEBIRC " << m_WebircPass << " cgiirc " << context.GetAddressBook ().ToAddress (m_From->GetIdentHash ()) << " 127.0.0.1\n";
        }

        m_InPacket.clear ();
        m_InPacket.write ((const char *)buf, len);
        
        while (!m_InPacket.eof () && !m_InPacket.fail ())
        {
			std::string line;
            std::getline (m_InPacket, line);
            if (line.length () == 0 && m_InPacket.eof ()) 
            	m_InPacket.str ("");
            auto pos = line.find ("USER");
            if (!pos) // start of line
            {
                pos = line.find (" ");
                pos++;
                pos = line.find (" ", pos);
                pos++;
                auto nextpos = line.find (" ", pos);
                m_OutPacket << line.substr (0, pos);
                m_OutPacket << context.GetAddressBook ().ToAddress (m_From->GetIdentHash ());
                m_OutPacket << line.substr (nextpos) << '\n';
            } 
			else
                m_OutPacket << line << '\n';
        }
        I2PTunnelConnection::Write ((uint8_t *)m_OutPacket.str ().c_str (), m_OutPacket.str ().length ());
    }


	/* This handler tries to stablish a connection with the desired server and dies if it fails to do so */
	class I2PClientTunnelHandler: public I2PServiceHandler, public std::enable_shared_from_this<I2PClientTunnelHandler>
	{
		public:
			I2PClientTunnelHandler (I2PClientTunnel * parent, i2p::data::IdentHash destination,
				int destinationPort, std::shared_ptr<boost::asio::ip::tcp::socket> socket):
				I2PServiceHandler(parent), m_DestinationIdentHash(destination), 
				m_DestinationPort (destinationPort), m_Socket(socket) {};
			void Handle();
			void Terminate();
		private:
			void HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);
			i2p::data::IdentHash m_DestinationIdentHash;
			int m_DestinationPort;
			std::shared_ptr<boost::asio::ip::tcp::socket> m_Socket;
	};

	void I2PClientTunnelHandler::Handle()
	{
		GetOwner()->GetLocalDestination ()->CreateStream ( 
			std::bind (&I2PClientTunnelHandler::HandleStreamRequestComplete, shared_from_this(), std::placeholders::_1), 
			m_DestinationIdentHash, m_DestinationPort);
	}

	void I2PClientTunnelHandler::HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
		{
			if (Kill()) return;
			LogPrint (eLogDebug, "I2PTunnel: new connection");
			auto connection = std::make_shared<I2PTunnelConnection>(GetOwner(), m_Socket, stream);
			GetOwner()->AddHandler (connection);
			connection->I2PConnect ();
			Done(shared_from_this());
		}
		else
		{
			LogPrint (eLogError, "I2PTunnel: Client Tunnel Issue when creating the stream, check the previous warnings for more info.");
			Terminate();
		}
	}

	void I2PClientTunnelHandler::Terminate()
	{
		if (Kill()) return;
		if (m_Socket)
		{
			m_Socket->close();
			m_Socket = nullptr;
		}
		Done(shared_from_this());
	}

	I2PClientTunnel::I2PClientTunnel (const std::string& name, const std::string& destination, 
	    const std::string& address, int port, std::shared_ptr<ClientDestination> localDestination, int destinationPort): 
		TCPIPAcceptor (address, port, localDestination), m_Name (name), m_Destination (destination), 
		m_DestinationIdentHash (nullptr), m_DestinationPort (destinationPort) 
	{
	}	

	void I2PClientTunnel::Start ()
	{
		TCPIPAcceptor::Start ();
		GetIdentHash();
	}

	void I2PClientTunnel::Stop ()
	{
		TCPIPAcceptor::Stop();
		auto *originalIdentHash = m_DestinationIdentHash;
		m_DestinationIdentHash = nullptr;
		delete originalIdentHash;
	}

	/* HACK: maybe we should create a caching IdentHash provider in AddressBook */
	const i2p::data::IdentHash * I2PClientTunnel::GetIdentHash ()
	{
		if (!m_DestinationIdentHash)
		{
			i2p::data::IdentHash identHash;
			if (i2p::client::context.GetAddressBook ().GetIdentHash (m_Destination, identHash))
				m_DestinationIdentHash = new i2p::data::IdentHash (identHash);
			else
				LogPrint (eLogWarning, "I2PTunnel: Remote destination ", m_Destination, " not found");
		}
		return m_DestinationIdentHash;
	}

	std::shared_ptr<I2PServiceHandler> I2PClientTunnel::CreateHandler(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		const i2p::data::IdentHash *identHash = GetIdentHash();
		if (identHash)
			return  std::make_shared<I2PClientTunnelHandler>(this, *identHash, m_DestinationPort, socket);
		else
			return nullptr;
	}

	I2PServerTunnel::I2PServerTunnel (const std::string& name, const std::string& address, 
	    int port, std::shared_ptr<ClientDestination> localDestination, int inport, bool gzip): 
		I2PService (localDestination), m_MapToLoopback(true), m_Name (name), m_Address (address), m_Port (port), m_IsAccessList (false)
	{
		m_PortDestination = localDestination->CreateStreamingDestination (inport > 0 ? inport : port, gzip);
	}

	void I2PServerTunnel::Start ()
	{
		m_Endpoint.port (m_Port);	
		boost::system::error_code ec;
		auto addr = boost::asio::ip::address::from_string (m_Address, ec);
		if (!ec)	
		{
			m_Endpoint.address (addr);
			Accept ();
		}
		else
		{
			auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(GetService ());
			resolver->async_resolve (boost::asio::ip::tcp::resolver::query (m_Address, ""), 
				std::bind (&I2PServerTunnel::HandleResolve, this, 
					std::placeholders::_1, std::placeholders::_2, resolver));
		}	
	}

	void I2PServerTunnel::Stop ()
	{
		ClearHandlers ();
	}	

	void I2PServerTunnel::HandleResolve (const boost::system::error_code& ecode, boost::asio::ip::tcp::resolver::iterator it, 
		std::shared_ptr<boost::asio::ip::tcp::resolver> resolver)
	{	
		if (!ecode)
		{	
			auto addr = (*it).endpoint ().address ();
			LogPrint (eLogInfo, "I2PTunnel: server tunnel ", (*it).host_name (), " has been resolved to ", addr);
			m_Endpoint.address (addr);
			Accept ();	
		}	
		else
			LogPrint (eLogError, "I2PTunnel: Unable to resolve server tunnel address: ", ecode.message ());
	}

	void I2PServerTunnel::SetAccessList (const std::set<i2p::data::IdentHash>& accessList)
	{
		m_AccessList = accessList;
		m_IsAccessList = true;		
	}

	void I2PServerTunnel::Accept ()
	{
		if (m_PortDestination)
			m_PortDestination->SetAcceptor (std::bind (&I2PServerTunnel::HandleAccept, this, std::placeholders::_1));

		auto localDestination = GetLocalDestination ();	
		if (localDestination)
		{
			if (!localDestination->IsAcceptingStreams ()) // set it as default if not set yet
				localDestination->AcceptStreams (std::bind (&I2PServerTunnel::HandleAccept, this, std::placeholders::_1));
		}
		else
			LogPrint (eLogError, "I2PTunnel: Local destination not set for server tunnel");
	}

	void I2PServerTunnel::HandleAccept (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
		{	
			if (m_IsAccessList)
			{
				if (!m_AccessList.count (stream->GetRemoteIdentity ()->GetIdentHash ()))
				{
					LogPrint (eLogWarning, "I2PTunnel: Address ", stream->GetRemoteIdentity ()->GetIdentHash ().ToBase32 (), " is not in white list. Incoming connection dropped");
					stream->Close ();
					return;
				}
			}
			CreateI2PConnection (stream);
		}	
	}

	void I2PServerTunnel::CreateI2PConnection (std::shared_ptr<i2p::stream::Stream> stream)
	{
		auto conn = std::make_shared<I2PTunnelConnection> (this, stream, std::make_shared<boost::asio::ip::tcp::socket> (GetService ()), GetEndpoint ());
		AddHandler (conn);
		conn->Connect (m_MapToLoopback);
	}

	I2PServerTunnelHTTP::I2PServerTunnelHTTP (const std::string& name, const std::string& address, 
	    int port, std::shared_ptr<ClientDestination> localDestination, 
	    const std::string& host, int inport, bool gzip):
		I2PServerTunnel (name, address, port, localDestination, inport, gzip), 
		m_Host (host)
	{
	}

	void I2PServerTunnelHTTP::CreateI2PConnection (std::shared_ptr<i2p::stream::Stream> stream)
	{
		auto conn = std::make_shared<I2PTunnelConnectionHTTP> (this, stream, 
			std::make_shared<boost::asio::ip::tcp::socket> (GetService ()), GetEndpoint (), m_Host);
		AddHandler (conn);
		conn->Connect ();
	}

    I2PServerTunnelIRC::I2PServerTunnelIRC (const std::string& name, const std::string& address, 
        int port, std::shared_ptr<ClientDestination> localDestination, 
        const std::string& webircpass, int inport, bool gzip):
        I2PServerTunnel (name, address, port, localDestination, inport, gzip),
        m_WebircPass (webircpass)
    {
    }

    void I2PServerTunnelIRC::CreateI2PConnection (std::shared_ptr<i2p::stream::Stream> stream)
    {
	    auto conn = std::make_shared<I2PTunnelConnectionIRC> (this, stream, std::make_shared<boost::asio::ip::tcp::socket> (GetService ()), GetEndpoint (), this->m_WebircPass);
	    AddHandler (conn);
        conn->Connect ();
    }

  void I2PUDPServerTunnel::HandleRecvFromI2P(const i2p::data::IdentityEx& from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
  {
    std::lock_guard<std::mutex> lock(m_SessionsMutex);
    auto session = ObtainUDPSession(from, toPort, fromPort);
    session->IPSocket.send_to(boost::asio::buffer(buf, len), m_RemoteEndpoint);
    session->LastActivity = i2p::util::GetMillisecondsSinceEpoch();
  }

  void I2PUDPServerTunnel::ExpireStale(const uint64_t delta) {
    std::lock_guard<std::mutex> lock(m_SessionsMutex);
    uint64_t now = i2p::util::GetMillisecondsSinceEpoch();
		auto itr = m_Sessions.begin();
		while(itr != m_Sessions.end()) {
			if(now - (*itr)->LastActivity >= delta )
				itr = m_Sessions.erase(itr);
			else
				++itr;
		}
  }

	void I2PUDPClientTunnel::ExpireStale(const uint64_t delta) {
		std::lock_guard<std::mutex> lock(m_SessionsMutex);
    uint64_t now = i2p::util::GetMillisecondsSinceEpoch();
		std::vector<uint16_t> removePorts;
		for (const auto & s : m_Sessions) {
			if (now - s.second.second >= delta)
				removePorts.push_back(s.first);
		}
		for(auto port : removePorts) {
			m_Sessions.erase(port);
		}
  }
	
	UDPSessionPtr I2PUDPServerTunnel::ObtainUDPSession(const i2p::data::IdentityEx& from, uint16_t localPort, uint16_t remotePort)
  {
    auto ih = from.GetIdentHash();
    for (auto & s : m_Sessions )
    {
      if ( s->Identity == ih)
      {
        /** found existing session */
        LogPrint(eLogDebug, "UDPServer: found session ", s->IPSocket.local_endpoint(), " ", ih.ToBase32());
        return s;
      }
    }
		boost::asio::ip::address addr;
		/** create new udp session */
		if(m_LocalAddress.is_loopback() && m_MapToLoopback) {
			auto ident = from.GetIdentHash();
			addr = GetLoopbackAddressFor(ident);
		} else {
			addr = m_LocalAddress;
		}
		boost::asio::ip::udp::endpoint ep(addr, 0);
    m_Sessions.push_back(std::make_shared<UDPSession>(ep, m_LocalDest, m_RemoteEndpoint, &ih, localPort, remotePort));
		auto & back = m_Sessions.back();
		return back;
  }

  UDPSession::UDPSession(boost::asio::ip::udp::endpoint localEndpoint,
    const std::shared_ptr<i2p::client::ClientDestination> & localDestination,
    boost::asio::ip::udp::endpoint endpoint, const i2p::data::IdentHash * to,
    uint16_t ourPort, uint16_t theirPort) :
    m_Destination(localDestination->GetDatagramDestination()),
		IPSocket(localDestination->GetService(), localEndpoint),
		SendEndpoint(endpoint),
		LastActivity(i2p::util::GetMillisecondsSinceEpoch()),
		LocalPort(ourPort),
		RemotePort(theirPort)
	{
		memcpy(Identity, to->data(), 32);
		Receive();
	}


	void UDPSession::Receive() {
		LogPrint(eLogDebug, "UDPSession: Receive");
		IPSocket.async_receive_from(boost::asio::buffer(m_Buffer, I2P_UDP_MAX_MTU),
			FromEndpoint, std::bind(&UDPSession::HandleReceived, this, std::placeholders::_1, std::placeholders::_2));
	}
	
	void UDPSession::HandleReceived(const boost::system::error_code & ecode, std::size_t len)
	{
		if(!ecode)
		{
			LogPrint(eLogDebug, "UDPSession: forward ", len, "B from ", FromEndpoint);
			LastActivity = i2p::util::GetMillisecondsSinceEpoch();
			m_Destination->SendDatagramTo(m_Buffer, len, Identity, LocalPort, RemotePort);
			Receive();
		} else {
			LogPrint(eLogError, "UDPSession: ", ecode.message());
		}
	}

	
	
	I2PUDPServerTunnel::I2PUDPServerTunnel(const std::string & name, std::shared_ptr<i2p::client::ClientDestination> localDestination,
		boost::asio::ip::address localAddress, boost::asio::ip::udp::endpoint forwardTo, uint16_t port) :
		m_MapToLoopback(true),
		m_Name(name),
		m_LocalAddress(localAddress),
		m_RemoteEndpoint(forwardTo)
	{
		m_LocalDest = localDestination;
		m_LocalDest->Start();
		auto dgram = m_LocalDest->CreateDatagramDestination();
		dgram->SetReceiver(std::bind(&I2PUDPServerTunnel::HandleRecvFromI2P, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
	}

	I2PUDPServerTunnel::~I2PUDPServerTunnel()
	{
		auto dgram = m_LocalDest->GetDatagramDestination();
		if (dgram) dgram->ResetReceiver();
		
		LogPrint(eLogInfo, "UDPServer: done");
	}

	void I2PUDPServerTunnel::Start() {
		m_LocalDest->Start();
	}

	std::vector<std::shared_ptr<DatagramSessionInfo> > I2PUDPServerTunnel::GetSessions()
	{
		std::vector<std::shared_ptr<DatagramSessionInfo> > sessions;
		std::lock_guard<std::mutex> lock(m_SessionsMutex);

		for ( UDPSessionPtr s : m_Sessions )
		{
			if (!s->m_Destination) continue;
			auto info = s->m_Destination->GetInfoForRemote(s->Identity);
			if(!info) continue;

			auto sinfo = std::make_shared<DatagramSessionInfo>();
			sinfo->Name = m_Name;
			sinfo->LocalIdent = std::make_shared<i2p::data::IdentHash>(m_LocalDest->GetIdentHash().data());
			sinfo->RemoteIdent = std::make_shared<i2p::data::IdentHash>(s->Identity.data());
			sinfo->CurrentIBGW = info->IBGW;
			sinfo->CurrentOBEP = info->OBEP;
			sessions.push_back(sinfo);
		}
		return sessions;
	}
	
	I2PUDPClientTunnel::I2PUDPClientTunnel(const std::string & name, const std::string &remoteDest,
		boost::asio::ip::udp::endpoint localEndpoint,
		std::shared_ptr<i2p::client::ClientDestination> localDestination,
		uint16_t remotePort) :
		m_Name(name),
		m_RemoteDest(remoteDest),
		m_LocalDest(localDestination),
		m_LocalEndpoint(localEndpoint),
		m_RemoteIdent(nullptr),
		m_ResolveThread(nullptr),
		m_LocalSocket(localDestination->GetService(), localEndpoint),
		RemotePort(remotePort),
		m_cancel_resolve(false)
	{
		auto dgram = m_LocalDest->CreateDatagramDestination();
		dgram->SetReceiver(std::bind(&I2PUDPClientTunnel::HandleRecvFromI2P, this,
																 std::placeholders::_1, std::placeholders::_2,
																 std::placeholders::_3, std::placeholders::_4,
																 std::placeholders::_5));
	}

	
	
	void I2PUDPClientTunnel::Start() {
		m_LocalDest->Start();
		if (m_ResolveThread == nullptr)
			m_ResolveThread = new std::thread(std::bind(&I2PUDPClientTunnel::TryResolving, this));
		RecvFromLocal();
	}

	void I2PUDPClientTunnel::RecvFromLocal()
	{
		m_LocalSocket.async_receive_from(boost::asio::buffer(m_RecvBuff, I2P_UDP_MAX_MTU),
			m_RecvEndpoint, std::bind(&I2PUDPClientTunnel::HandleRecvFromLocal, this, std::placeholders::_1, std::placeholders::_2));
	}

	void I2PUDPClientTunnel::HandleRecvFromLocal(const boost::system::error_code & ec, std::size_t transferred)
	{
		if(ec) {
			LogPrint(eLogError, "UDP Client: ", ec.message());
			return;
		}
		if(!m_RemoteIdent) {
			LogPrint(eLogWarning, "UDP Client: remote endpoint not resolved yet");
			RecvFromLocal();
			return; // drop, remote not resolved
		}
		auto remotePort = m_RecvEndpoint.port();
		auto itr = m_Sessions.find(remotePort);
		if (itr == m_Sessions.end()) {
			// track new udp convo
			m_Sessions[remotePort] = {boost::asio::ip::udp::endpoint(m_RecvEndpoint), 0};
		}
		// send off to remote i2p destination
		LogPrint(eLogDebug, "UDP Client: send ", transferred, " to ", m_RemoteIdent->ToBase32(), ":", RemotePort);
		m_LocalDest->GetDatagramDestination()->SendDatagramTo(m_RecvBuff, transferred, *m_RemoteIdent, remotePort, RemotePort);
		// mark convo as active
		m_Sessions[remotePort].second = i2p::util::GetMillisecondsSinceEpoch();
		RecvFromLocal();
	}
	
	std::vector<std::shared_ptr<DatagramSessionInfo> > I2PUDPClientTunnel::GetSessions()
	{
		// TODO: implement
		std::vector<std::shared_ptr<DatagramSessionInfo> > infos;
		return infos;
	}
	
	void I2PUDPClientTunnel::TryResolving() {
		LogPrint(eLogInfo, "UDP Tunnel: Trying to resolve ", m_RemoteDest);
		i2p::data::IdentHash * h = new i2p::data::IdentHash;

		while(!context.GetAddressBook().GetIdentHash(m_RemoteDest, *h) && !m_cancel_resolve)
		{
			LogPrint(eLogWarning, "UDP Tunnel: failed to lookup ", m_RemoteDest);
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		if(m_cancel_resolve)
		{
			LogPrint(eLogError, "UDP Tunnel: lookup of ", m_RemoteDest, " was cancelled");
			return;
		}
		m_RemoteIdent = h;
		LogPrint(eLogInfo, "UDP Tunnel: resolved ", m_RemoteDest, " to ", m_RemoteIdent->ToBase32());
	}

	void I2PUDPClientTunnel::HandleRecvFromI2P(const i2p::data::IdentityEx& from, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		if(m_RemoteIdent && from.GetIdentHash() == *m_RemoteIdent)
		{
			auto itr = m_Sessions.find(toPort);
			// found convo ?
			if(itr != m_Sessions.end())
			{
				// found convo
				if (len > 0) {
					LogPrint(eLogDebug, "UDP Client: got ", len, "B from ", from.GetIdentHash().ToBase32());
					m_LocalSocket.send_to(boost::asio::buffer(buf, len), itr->second.first);
					// mark convo as active
					itr->second.second = i2p::util::GetMillisecondsSinceEpoch();
				}
			}
			else
				LogPrint(eLogWarning, "UDP Client: not tracking udp session using port ", (int) toPort); 
		}
		else
			LogPrint(eLogWarning, "UDP Client: unwarrented traffic from ", from.GetIdentHash().ToBase32());
		
	}

	I2PUDPClientTunnel::~I2PUDPClientTunnel() {
		auto dgram = m_LocalDest->GetDatagramDestination();
		if (dgram) dgram->ResetReceiver();

		m_Sessions.clear();
		
		if(m_LocalSocket.is_open())
			m_LocalSocket.close();

		m_cancel_resolve = true;

		if(m_ResolveThread)
		{
			m_ResolveThread->join();
			delete m_ResolveThread;
			m_ResolveThread = nullptr;
		}
		if (m_RemoteIdent) delete m_RemoteIdent;
	}
}
}