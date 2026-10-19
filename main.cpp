/*
 * Macro Assist — Geode mod for Geometry Dash 2.2081 (Windows)
 *
 * Ти натискаєш кнопку -> мод шукає найближчу подію макросу того ж типу на поточному
 * кадрі або до `max-delay` кадрів уперед і відкладає твій інпут точно на цей кадр.
 * Раніше за себе інпут не рухається (тільки затримка).
 *
 * Формати макросу:
 *   .gdr — GDReplayFormat (через бібліотеку gdr)
 *   .txt — рядки:  <frame> <down|up> [button=1] [p1|p2]   ('#' = коментар)
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <gdr/gdr.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

using namespace geode::prelude;

struct AssistReplay : gdr::Replay<AssistReplay, gdr::Input<>> {
	AssistReplay() : Replay("MacroAssist", 1) {}
};

struct MacroEvent {
	int frame;
	bool down;
	int button;
	bool p1;
	bool used = false;
};

struct PendingInput {
	int frame;
	bool down;
	int button;
	bool p1;
};

namespace state {
	inline std::vector<MacroEvent> events;
	inline std::vector<PendingInput> pending;
	inline bool injecting = false;
}

static void loadGdr(std::filesystem::path const& path) {
	auto bytes = file::readBinary(path);
	if (!bytes) {
		log::error("Macro Assist: can't read {}: {}", path.string(), bytes.unwrapErr());
		return;
	}
	auto res = AssistReplay::importData(bytes.unwrap());
	if (!res) {
		log::error("Macro Assist: can't parse .gdr: {}", res.unwrapErr());
		return;
	}
	auto replay = std::move(res).unwrap();
	for (auto const& in : replay.inputs) {
		MacroEvent e;
		e.frame = static_cast<int>(in.frame);
		e.down = in.down;
		e.button = static_cast<int>(in.button);
		e.p1 = !in.player2;
		state::events.push_back(e);
	}
}

static void loadTxt(std::filesystem::path const& path) {
	std::ifstream in(path);
	if (!in) {
		log::error("Macro Assist: can't open {}", path.string());
		return;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		int frame = 0;
		std::string action, extra;
		if (!(ss >> frame >> action)) continue;
		MacroEvent e;
		e.frame = frame;
		e.down = (action == "down");
		e.button = 1;
		e.p1 = true;
		while (ss >> extra) {
			if (extra == "p2") e.p1 = false;
			else if (extra == "p1") e.p1 = true;
			else e.button = std::atoi(extra.c_str());
		}
		state::events.push_back(e);
	}
}

static void loadMacro() {
	state::events.clear();
	state::pending.clear();

	auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-path");
	if (path.empty()) {
		log::warn("Macro Assist: no macro file selected");
		return;
	}

	auto ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

	if (ext == ".gdr") loadGdr(path);
	else loadTxt(path);

	int offset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("frame-offset"));
	for (auto& e : state::events) e.frame += offset;

	std::stable_sort(state::events.begin(), state::events.end(),
		[](MacroEvent const& a, MacroEvent const& b) { return a.frame < b.frame; });

	log::info("Macro Assist: loaded {} events from {}", state::events.size(), path.string());
}

class $modify(AssistPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		loadMacro();
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		for (auto& e : state::events) e.used = false;
		state::pending.clear();
	}
};

class $modify(AssistBaseLayer, GJBaseGameLayer) {
	// Перехоплює інпут гравця.
	void handleButton(bool down, int button, bool isPlayer1) {
		auto mod = Mod::get();

		// Вимкнено / нема макросу / це наша власна ін'єкція -> повністю нічого не чіпаємо.
		if (state::injecting
			|| state::events.empty()
			|| !mod->getSettingValue<bool>("enabled")
			|| !PlayLayer::get()) {
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}

		int cur = m_gameState.m_currentProgress;
		int maxDelay = static_cast<int>(mod->getSettingValue<int64_t>("max-delay"));

		// Якщо для цієї кнопки вже є відкладений інпут — новий мусить іти ПІСЛЯ нього,
		// інакше порядок press/release ламається і кнопка "залипає".
		PendingInput* tail = nullptr;
		for (auto& p : state::pending) {
			if (p.button == button && p.p1 == isPlayer1) tail = &p;
		}
		int minFrame = tail ? std::max(cur, tail->frame) : cur;

		MacroEvent* match = nullptr;
		auto it = std::lower_bound(state::events.begin(), state::events.end(), minFrame,
			[](MacroEvent const& e, int f) { return e.frame < f; });
		for (; it != state::events.end(); ++it) {
			if (it->frame > cur + maxDelay) break;
			if (it->used || it->down != down || it->button != button || it->p1 != isPlayer1) continue;
			match = &*it;
			break;
		}

		if (!match) {
			if (tail) {
				// зберігаємо порядок: ставимо одразу за попереднім відкладеним
				if (mod->getSettingValue<bool>("block-unmatched")) return;
				state::pending.push_back({tail->frame, down, button, isPlayer1});
				return;
			}
			if (mod->getSettingValue<bool>("block-unmatched")) return;
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}

		match->used = true;
		if (!tail && match->frame <= cur) {
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}
		state::pending.push_back({match->frame, down, button, isPlayer1});
	}

	// Виконує відкладені інпути на початку потрібного фізичного кроку.
	void processCommands(float dt) {
		if (!state::pending.empty() && PlayLayer::get()) {
			bool enabled = Mod::get()->getSettingValue<bool>("enabled");
			int cur = m_gameState.m_currentProgress;
			state::injecting = true;
			for (auto it = state::pending.begin(); it != state::pending.end();) {
				// якщо мод вимкнули посеред рівня — одразу віддаємо все, що лишилось
				if (!enabled || it->frame <= cur) {
					GJBaseGameLayer::handleButton(it->down, it->button, it->p1);
					it = state::pending.erase(it);
				} else {
					++it;
				}
			}
			state::injecting = false;
		}
		GJBaseGameLayer::processCommands(dt);
	}
};
