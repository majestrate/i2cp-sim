#ifndef SIMSERV_SERVER_HPP__
#define SIMSERV_SERVER_HPP__

#include <i2pd/I2CP.h>
#include "net_context.hpp"

namespace i2p
{
namespace simulation
{


	/**
		 loopback destination for network simulations
		 abstract class
	 */
	class BaseDestination : public i2p::client::I2CPDestination
	{
	public:
		Destination(std::shared_ptr<const i2p::data::IdentityEx> ourIdent, NetContext_ptr netctx);
		~Destination();

		std::shared_ptr<const i2p::data::LeaseSet> FindLeaseSet(const i2p::data::IdentHash & ident);

		std::shared_ptr<const i2p::data::IdentityEx> GetIdentity() const { return m_OurIdenity; }

		// overrides i2cp destination
		bool SendMsgTo(const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident, uint32_t nonce);

	protected:

		/** @brief return true if this message should be dropped silently by network */
		virtual bool ShouldDrop() = 0;

		void StartTunnelPool ();
		void RequestLeaseSet (const i2p::data::IdentHash& dest, RequestComplete requestComplete);


	private:
		std::shared_ptr<const i2p::data::IdentityEx> m_OurIdentity;
		NetContext_ptr m_Network;

	};
}
}

#endif
