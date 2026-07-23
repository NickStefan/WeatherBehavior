#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
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

		static void RegisterSerialization();
		void        SaveState(SKSE::SerializationInterface* a_intfc);
		void        LoadState(SKSE::SerializationInterface* a_intfc);
		void        RevertState();

	private:
		Manager() = default;
		~Manager();

		void PollLoop();
		void Tick();
		void Apply();
		void RevertAll();

		std::thread             _worker;
		std::mutex              _sleepMutex;
		std::condition_variable _sleepCv;
		std::atomic<bool>       _running{ false };
		std::atomic<bool>       _wake{ false };

		std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, bool>> _forced;
	};
}
