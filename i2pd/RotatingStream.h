#ifdef ROTATING_STREAM_H_
#define ROTATING_STREAM_H_

#include "Streaming.h"


namespace i2p
{
namespace stream
{



  /** @brief a streaming destination that periodically rotates i2p destination, holds onto streams smartly. */
  class RotatingStreamingDestination 
  {
  public:

    typedef std::shared_ptr<Stream> Stream_t;
    typedef std::shared_ptr<const i2p::data::LeaseSet> LeaseSet_t;
    typedef std::chrono::duration Duration_t;
    typedef std::shared_ptr<RotatingStreamingDestination> RotatingDest_t;

    RotatingStreamingDestination(Duration_t rotateInterval);

    /** @brief create an outgoing stream using the latest ephemeral destination */
    Stream_t CreateNewOutgoingStream(LeaseSet_t remote, int port = 0);

  private:

    typedef std::shared_ptr<StreamingDestination> StreamDest_t;
    typedef std::shared_ptr<i2p::client::ClientDestination> ClientDest_t;

    /** @brief ephemeral destination with streaming destination */
    struct EphemeralDestination
    {
      StreamDest_t stream = nullptr;
      ClientDest_t client = nullptr;
      RotatingDest_t parent = nullptr;
      Time_t expires = 0;

      EphemeralDest_t(const RotatingDest_t & parent);
      ~EphemeralDest_t();

      /** @brief start building tunnels */
      void Start();
      /** @brief stop using this ephemeral destination, does graceful stream expiration */
      void Expire();
      /** @brief kill this destination right away, all streams will die suddenly if they are open */
      void Kill();
    };
    typedef std::shared_ptr<EphemeralDestination> EphemeralDest_t;

    /** @brief called when an ephemeral destination's tunnels are built and ready to use */
    void DestinationReady(EphemeralDest_t dest);
    /** @brief called when an ephemeral destination expires */
    void DestinationExpired(EphemeralDest_t dest);

  };

}
}

#endif
