#include "Manager.h"

#include <chrono>
#include <limits>
#include <random>
#include <unordered_set>
#include <vector>

#include "Config.h"
#include "Util.h"

namespace WeatherBehavior
{
	Manager& Manager::GetSingleton()
	{
		static Manager instance;
		return instance;
	}

	void Manager::Start()
	{
		if (_running.exchange(true)) {
			return;
		}
		_worker = std::thread(&Manager::PollLoop, this);
	}

	void Manager::Stop()
	{
		if (!_running.exchange(false)) {
			return;
		}
		_sleepCv.notify_all();
		if (_worker.joinable()) {
			_worker.join();
		}
	}

	void Manager::RequestApply()
	{
		_wake.store(true);
		_sleepCv.notify_all();
	}

	void Manager::ResetTracking()
	{
		_lastRevision = std::numeric_limits<std::uint32_t>::max();
		_lastWeather = 0;
		_lastSeason = 0;
	}

	void Manager::PollLoop()
	{
		while (_running.load()) {
			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([this]() { Tick(); });
			}

			const auto seconds =
				std::max<std::uint32_t>(1, Config::GetSingleton().pollSeconds.load(std::memory_order_relaxed));
			std::unique_lock lock(_sleepMutex);
			_sleepCv.wait_for(lock, std::chrono::seconds(seconds), [this]() {
				return !_running.load() || _wake.load();
			});
			_wake.store(false);
		}
	}

	void Manager::Tick()
	{
		auto& config = Config::GetSingleton();

		if (!config.enabled) {
			if (!_forced.empty()) {
				RevertAll();
			}
			_lastRevision = std::numeric_limits<std::uint32_t>::max();
			return;
		}

		const auto sky = RE::Sky::GetSingleton();
		const auto calendar = RE::Calendar::GetSingleton();
		if (!sky || !calendar) {
			return;
		}

		const std::uint32_t weather = WeatherClassOf(sky->currentWeather);
		const std::uint32_t season = SeasonBitOf(calendar->GetMonth());
		const std::uint32_t revision = config.Revision();

		if (weather == _lastWeather && season == _lastSeason && revision == _lastRevision) {
			return;
		}

		_lastWeather = weather;
		_lastSeason = season;
		_lastRevision = revision;

		Apply();
	}

	static bool EnsureHasItem(RE::Actor* a_actor, RE::TESObjectARMO* a_item)
	{
		const auto counts = a_actor->GetInventoryCounts();
		for (const auto& [obj, count] : counts) {
			if (obj == a_item && count > 0) {
				return false;
			}
		}
		a_actor->AddObjectToContainer(a_item, nullptr, 1, nullptr);
		return true;
	}

	static void SelectItems(const Rule& a_rule, RE::FormID a_actorID, std::unordered_set<RE::FormID>& a_out)
	{
		if (a_rule.items.empty()) {
			return;
		}

		std::mt19937 rng(a_actorID * 2654435761u ^ (a_rule.id * 40503u));

		if (a_rule.chance < 100 && (rng() % 100u) >= a_rule.chance) {
			return;
		}

		std::vector<RE::FormID> pool;
		pool.reserve(a_rule.items.size());
		for (const auto& ref : a_rule.items) {
			if (const auto armor = ref.Resolve()) {
				pool.push_back(armor->GetFormID());
			}
		}
		if (pool.empty()) {
			return;
		}

		const std::size_t count = 1 + (rng() % pool.size());
		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t j = i + (rng() % (pool.size() - i));
			std::swap(pool[i], pool[j]);
			a_out.insert(pool[i]);
		}
	}

	void Manager::Apply()
	{
		auto& config = Config::GetSingleton();
		const auto processLists = RE::ProcessLists::GetSingleton();
		const auto equipManager = RE::ActorEquipManager::GetSingleton();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!processLists || !equipManager) {
			return;
		}

		const std::uint32_t weather = _lastWeather;
		const std::uint32_t season = _lastSeason;

		std::vector<const Rule*> active;
		for (const auto& rule : config.rules) {
			if (rule.EnvMatches(weather, season)) {
				active.push_back(&rule);
			}
		}

		processLists->ForEachHighActor([&](RE::Actor* a_actor) {
			if (!a_actor || a_actor == player || a_actor->IsDead() || a_actor->IsDisabled() ||
				!a_actor->GetActorBase()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const bool isFollower = a_actor->IsPlayerTeammate();
			const bool indoors = config.onlyOutdoors && a_actor->GetParentCell() &&
			                     a_actor->GetParentCell()->IsInteriorCell();
			const RE::FormID actorID = a_actor->GetFormID();

			std::unordered_set<RE::FormID> desired;
			if (!indoors) {
				for (const auto rule : active) {
					if (rule->target == Target::kFollowersOnly && !isFollower) {
						continue;
					}
					SelectItems(*rule, actorID, desired);
				}
			}

			auto& worn = _forced[actorID];

			for (auto it = worn.begin(); it != worn.end();) {
				if (!desired.contains(it->first)) {
					if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(it->first)) {
						equipManager->UnequipObject(a_actor, armor, nullptr, 1, nullptr, true, true, false, false);
						if (it->second) {
							a_actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						}
					}
					it = worn.erase(it);
				} else {
					++it;
				}
			}

			for (const auto id : desired) {
				if (worn.contains(id)) {
					continue;
				}
				if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
					const bool added = EnsureHasItem(a_actor, armor);
					equipManager->EquipObject(a_actor, armor, nullptr, 1, nullptr, true, true, false, false);
					worn[id] = added;
				}
			}

			if (worn.empty()) {
				_forced.erase(actorID);
			}

			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	void Manager::RevertAll()
	{
		const auto equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			_forced.clear();
			return;
		}

		for (const auto& [actorID, worn] : _forced) {
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
			if (!actor) {
				continue;
			}
			for (const auto& [itemID, addedCopy] : worn) {
				if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(itemID)) {
					equipManager->UnequipObject(actor, armor, nullptr, 1, nullptr, true, true, false, false);
					if (addedCopy) {
						actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
				}
			}
		}
		_forced.clear();
	}

	namespace
	{
		constexpr std::uint32_t kSerUID = 'WBHV';
		constexpr std::uint32_t kRecForced = 'FRCD';
		constexpr std::uint32_t kRecVersion = 1;

		void SaveCallback(SKSE::SerializationInterface* a_intfc) { Manager::GetSingleton().SaveState(a_intfc); }
		void LoadCallback(SKSE::SerializationInterface* a_intfc) { Manager::GetSingleton().LoadState(a_intfc); }
		void RevertCallback(SKSE::SerializationInterface*) { Manager::GetSingleton().RevertState(); }
	}

	void Manager::RegisterSerialization()
	{
		const auto ser = SKSE::GetSerializationInterface();
		if (!ser) {
			return;
		}
		ser->SetUniqueID(kSerUID);
		ser->SetSaveCallback(SaveCallback);
		ser->SetLoadCallback(LoadCallback);
		ser->SetRevertCallback(RevertCallback);
	}

	void Manager::SaveState(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc->OpenRecord(kRecForced, kRecVersion)) {
			return;
		}
		const std::uint32_t actorCount = static_cast<std::uint32_t>(_forced.size());
		a_intfc->WriteRecordData(actorCount);
		for (const auto& [actorID, worn] : _forced) {
			a_intfc->WriteRecordData(actorID);
			const std::uint32_t itemCount = static_cast<std::uint32_t>(worn.size());
			a_intfc->WriteRecordData(itemCount);
			for (const auto& [itemID, added] : worn) {
				a_intfc->WriteRecordData(itemID);
				const std::uint8_t flag = added ? 1 : 0;
				a_intfc->WriteRecordData(flag);
			}
		}
	}

	void Manager::LoadState(SKSE::SerializationInterface* a_intfc)
	{
		_forced.clear();

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecForced) {
				continue;
			}
			std::uint32_t actorCount = 0;
			a_intfc->ReadRecordData(actorCount);
			for (std::uint32_t i = 0; i < actorCount; ++i) {
				RE::FormID actorID = 0;
				a_intfc->ReadRecordData(actorID);
				std::uint32_t itemCount = 0;
				a_intfc->ReadRecordData(itemCount);

				RE::FormID newActorID = 0;
				const bool actorOk = a_intfc->ResolveFormID(actorID, newActorID);

				std::unordered_map<RE::FormID, bool> worn;
				for (std::uint32_t j = 0; j < itemCount; ++j) {
					RE::FormID   itemID = 0;
					std::uint8_t flag = 0;
					a_intfc->ReadRecordData(itemID);
					a_intfc->ReadRecordData(flag);

					RE::FormID newItemID = 0;
					if (actorOk && a_intfc->ResolveFormID(itemID, newItemID)) {
						worn[newItemID] = flag != 0;
					}
				}

				if (actorOk && !worn.empty()) {
					_forced[newActorID] = std::move(worn);
				}
			}
		}
	}

	void Manager::RevertState()
	{
		_forced.clear();
		ResetTracking();
	}
}
