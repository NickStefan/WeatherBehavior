#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace WeatherBehavior
{
	enum WeatherClass : std::uint32_t
	{
		kPleasant = 1u << 0,
		kCloudy = 1u << 1,
		kRainy = 1u << 2,
		kSnowy = 1u << 3,
		kAnyWeather = kPleasant | kCloudy | kRainy | kSnowy
	};

	enum Season : std::uint32_t
	{
		kWinter = 1u << 0,
		kSpring = 1u << 1,
		kSummer = 1u << 2,
		kAutumn = 1u << 3,
		kAnySeason = kWinter | kSpring | kSummer | kAutumn
	};

	enum class Target : std::uint32_t
	{
		kAllNPCs = 0,
		kFollowersOnly = 1
	};

	inline constexpr std::array kWeatherNames{ "Pleasant", "Cloudy", "Rainy", "Snowy" };
	inline constexpr std::array kSeasonNames{ "Winter", "Spring", "Summer", "Autumn" };

	using MonthSeasonTable = std::array<std::uint32_t, 12>;

	// Indices match RE::Calendar::Month (Morning Star = 0 … Evening Star = 11).
	inline constexpr MonthSeasonTable kVanillaMonthSeasons{
		kWinter,  // Morning Star
		kWinter,  // Sun's Dawn
		kSpring,  // First Seed
		kSpring,  // Rain's Hand
		kSpring,  // Second Seed
		kSummer,  // Midyear
		kSummer,  // Sun's Height
		kSummer,  // Last Seed
		kAutumn,  // Hearthfire
		kAutumn,  // Frostfall
		kAutumn,  // Sun's Dusk
		kWinter,  // Evening Star
	};

	// Monthly calendar (season changes each month; e.g. Four Seasons - Faster Seasons).
	inline constexpr MonthSeasonTable kMonthlyMonthSeasons{
		kAutumn,  // Morning Star
		kWinter,  // Sun's Dawn
		kSpring,  // First Seed
		kSummer,  // Rain's Hand
		kAutumn,  // Second Seed
		kWinter,  // Midyear
		kSpring,  // Sun's Height
		kSummer,  // Last Seed
		kAutumn,  // Hearthfire
		kWinter,  // Frostfall
		kSpring,  // Sun's Dusk
		kSummer,  // Evening Star
	};

	inline constexpr std::string_view kSeasonCalendarVanilla{ "vanilla" };
	inline constexpr std::string_view kSeasonCalendarMonthly{ "monthly" };

	[[nodiscard]] inline const MonthSeasonTable& MonthSeasonsForCalendar(std::string_view a_calendar)
	{
		if (a_calendar == kSeasonCalendarMonthly) {
			return kMonthlyMonthSeasons;
		}
		return kVanillaMonthSeasons;
	}

	inline std::uint32_t WeatherClassOf(const RE::TESWeather* a_weather)
	{
		if (!a_weather) {
			return 0;
		}
		return static_cast<std::uint32_t>(a_weather->data.flags.underlying()) & kAnyWeather;
	}

	inline std::uint32_t SeasonBitOf(std::uint32_t a_month, const MonthSeasonTable& a_table)
	{
		return a_table[a_month % a_table.size()];
	}
}
