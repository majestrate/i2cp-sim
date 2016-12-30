#ifndef BLOCKLIST_H_
#define BLOCKLIST_H_

#include <string>
#include <memory>
#include <set>

namespace i2p
{
namespace data
{
	/** @brief netdb router info blocklist checker */
	class NetdbBlocklist
	{
	public:
		NetdbBlocklist();
		~NetdbBlocklist();

		/** @brief check router info against blocklist */
		bool IsBlocked(std::shared_ptr<RouterInfo> info) const;

		/** @brief subscribe to remote blocklist by url (i2p only) */
		void SubscribeToBlocklist(const std::string & url);
		
	private:
		std::set<std::string> m_BlockedIPs;

		uint32_t m_LastUpdate;
		
		// manually update from remote url inside i2p
		void CheckUpdate(const std::string & url);
		
	};
}
}

#endif
