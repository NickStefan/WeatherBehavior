#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "Util.h"

namespace WeatherBehavior
{
	struct FormRef
	{
		std::string plugin;
		RE::FormID  localID{ 0 };

		[[nodiscard]] bool     Valid() const { return !plugin.empty(); }
		[[nodiscard]] RE::TESObjectARMO* Resolve() const;
		[[nodiscard]] RE::FormID          ResolveID() const;
		[[nodiscard]] std::string Serialize() const;

		static std::optional<FormRef> Parse(std::string_view a_text);
		static FormRef                From(const RE::TESForm* a_form);

		bool operator==(const FormRef&) const = default;
	};

	struct Rule
	{
		std::uint32_t        id{ 0 };
		std::string          name{ "New Rule" };
		std::string          preset{ "Default" };
		bool                 enabled{ true };
		Target               target{ Target::kAllNPCs };
		std::uint32_t        chance{ 100 };
		std::uint32_t        weatherMask{ 0 };
		std::uint32_t        seasonMask{ 0 };
		std::vector<FormRef> excludedRaces;
		std::vector<FormRef> items;

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
