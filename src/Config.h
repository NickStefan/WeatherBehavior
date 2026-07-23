#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "Util.h"

namespace WeatherBehavior
{
	struct Rule
	{
		std::uint32_t            id{ 0 };
		std::string              name{ "New Rule" };
		std::string              preset{ "Default" };
		bool                     enabled{ true };
		Target                   target{ Target::kAllNPCs };
		std::uint32_t            chance{ 100 };
		std::uint32_t            weatherMask{ 0 };
		std::uint32_t            seasonMask{ 0 };
		std::vector<std::string> items;

		[[nodiscard]] bool EnvMatches(std::uint32_t a_weather, std::uint32_t a_season) const;
	};

	std::uint32_t MakeRuleID();

	class Config
	{
	public:
		static Config& GetSingleton();

		bool                       enabled{ true };
		bool                       onlyOutdoors{ true };
		std::atomic<std::uint32_t> pollSeconds{ 5 };
		std::vector<Rule>          rules;

		void Load();
		void Save();

		static std::string SanitizeFileName(std::string_view a_name);
		static std::string PresetsLocation();

		[[nodiscard]] std::uint32_t Revision() const { return _revision.load(); }
		void                        Bump() { _revision.fetch_add(1); }

	private:
		Config() = default;

		std::atomic<std::uint32_t> _revision{ 0 };
	};
}
