#include <i2pd/I2CP.h>
#include <i2pd/Log.h>
#include <i2pd/RouterContext.h>
#include <random>
#include <mutex>
#include <condition_variable>

std::condition_variable done;
std::mutex done_mtx;

struct DropSettings
{
	std::function<bool(void)> Drop;
	uint64_t Latency = 500;
	uint64_t Jitter = 40;
};

class DroppyI2CPServer : public i2p::client::I2CPServer
{
public:
	DroppyI2CPServer(const std::string & addr, int port, DropSettings drop) :
		i2p::client::I2CPServer(addr, port),
		m_Settings(drop)
	{
	}

	bool ShouldDrop(i2p::client::DropEvent & ev)
	{

		ev.Delay = m_Settings.Latency;
		if (m_Settings.Jitter > 0)
		{
			ev.Delay += rand() % m_Settings.Jitter;
		}
		if(m_Settings.Drop)
		{
			return m_Settings.Drop();
		}
		return false;
	}

private:
	
	DropSettings m_Settings;

};

DroppyI2CPServer * server = nullptr;

void handle_sig(int signal)
{
	done.notify_all();
}

int main(int argc, char * argv[])
{

	auto & log = i2p::log::Logger();
	log.Start();
	log.SetLogLevel("debug");

	if(argc == 5)
	{

		std::string addr = argv[1];
		int port = std::atoi(argv[2]);
		double drop = std::atof(argv[3]);

		std::random_device rd;
		std::mt19937 gen(rd());

		std::bernoulli_distribution dist(drop);

		DropSettings settings;
		settings.Drop = [&] () -> bool {
			return dist(gen);
		};

		settings.Latency = std::atoi(argv[4]);

		i2p::context.Init();

		DroppyI2CPServer server(addr, port, settings);

		signal(SIGINT, handle_sig);
		signal(SIGTERM, handle_sig);


		server.Start();
		LogPrint(eLogInfo, "i2cp server started");
		{
			std::unique_lock<std::mutex> lock(done_mtx);
			done.wait(lock);
		}
		server.Stop();
	}
	else
		LogPrint(eLogError, "usage ", argv[0], " addr port drop_prob");
	log.Stop();
	return 0;
}
