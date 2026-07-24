#include <spdlog/sinks/basic_file_sink.h>

#include "Config.h"
#include "Manager.h"
#include "Menu.h"

using namespace WeatherBehavior;

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "WeatherBehavior.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
		logger->set_level(spdlog::level::info);
		logger->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(logger));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Config::GetSingleton().Load();
			Menu::Register();
			Manager::GetSingleton().Start();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Manager::GetSingleton().RequestApply();
			break;
		default:
			break;
		}
	}
}

SKSEPluginInfo(
	.Version = { 1, 0, 0, 0 },
	.Name = "WeatherBehavior",
	.Author = "bottle")

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();
	SKSE::Init(a_skse);

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler)) {
		return false;
	}

	Manager::RegisterSerialization();

	SKSE::log::info("WeatherBehavior loaded");
	return true;
}
