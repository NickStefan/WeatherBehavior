#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "Util.h"

namespace WeatherBehavior
{
	std::uint32_t NextRuleUID();

	struct Rule
	{
		std::string              name{ "New Rule" };
		std::string              preset{ "Default" };
		bool                     enabled{ true };
		Target                   target{ Target::kAllNPCs };
		std::uint32_t            chance{ 100 };
		std::uint32_t            weatherMask{ 0 };
		std::uint32_t            seasonMask{ 0 };
		std::vector<std::string> items;
		std::uint32_t            uid{ NextRuleUID() };  // runtime only, never saved

		[[nodiscard]] bool          EnvMatches(std::uint32_t a_weather, std::uint32_t a_season) const;
		[[nodiscard]] std::uint32_t Seed() const;
	};

	class Config
	{
	public:
		static Config& GetSingleton();

		std::atomic<bool>          enabled{ true };
		std::atomic<bool>          onlyOutdoors{ true };
		std::atomic<std::uint32_t> pollSeconds{ 5 };

		std::mutex        rulesMutex;
		std::vector<Rule> rules;

		void Load();
		void Save();

		static std::string SanitizeFileName(std::string_view a_name);
		static std::string PresetsLocation();

	private:
		Config() = default;
	};
}
