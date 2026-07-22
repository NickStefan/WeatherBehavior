#pragma once

#include <array>
#include <cstdint>

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

	inline std::uint32_t WeatherClassOf(const RE::TESWeather* a_weather)
	{
		if (!a_weather) {
			return 0;
		}
		return static_cast<std::uint32_t>(a_weather->data.flags.underlying()) & kAnyWeather;
	}

	inline std::uint32_t SeasonBitOf(std::uint32_t a_month)
	{
		switch (a_month) {
		case RE::Calendar::Month::kEveningStar:
		case RE::Calendar::Month::kMorningStar:
		case RE::Calendar::Month::kSunsDawn:
			return kWinter;
		case RE::Calendar::Month::kFirstSeed:
		case RE::Calendar::Month::kRainsHand:
		case RE::Calendar::Month::kSecondSeed:
			return kSpring;
		case RE::Calendar::Month::kMidyear:
		case RE::Calendar::Month::kSunsHeight:
		case RE::Calendar::Month::kLastSeed:
			return kSummer;
		default:
			return kAutumn;
		}
	}
}
