#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace WeatherBehavior
{
	class Manager
	{
	public:
		static Manager& GetSingleton();

		void Start();
		void Stop();
		void RequestApply();
		void ResetTracking();

		static void RegisterSerialization();
		void        SaveState(SKSE::SerializationInterface* a_intfc);
		void        LoadState(SKSE::SerializationInterface* a_intfc);
		void        RevertState();

	private:
		Manager() = default;

		void PollLoop();
		void Tick();
		void Apply();
		void RevertAll();

		std::thread             _worker;
		std::mutex              _sleepMutex;
		std::condition_variable _sleepCv;
		std::atomic<bool>       _running{ false };
		std::atomic<bool>       _wake{ false };

		std::uint32_t _lastWeather{ 0 };
		std::uint32_t _lastSeason{ 0 };
		RE::FormID    _lastRegion{ 0 };
		std::uint32_t _lastRevision{ std::numeric_limits<std::uint32_t>::max() };

		std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, bool>> _forced;
	};
}
