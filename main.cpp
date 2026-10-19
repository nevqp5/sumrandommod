#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <vector>

using namespace geode::prelude;

// Структура для події макросу (приклад підвантаження з .gdr)
struct MacroEvent {
    int frame;
    int button; // PlayerButton
    bool down;  // true = натискання, false = відпускання
};

class $modify(AssistPlayLayer, PlayLayer) {
    int m_currentFrame = 0;
    std::vector<MacroEvent> m_macroEvents;

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        m_currentFrame = 0;
        m_macroEvents.clear();

        // ТУТ ПРИКЛАД: Ви можете додати парсер вашого .gdr файлу, який заповнить m_macroEvents.
        // Наприклад: m_macroEvents.push_back({1000, 0, true});

        log::info("MacroLegitAssist успішно ініціалізовано на рівні: {}", level->m_levelName);
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        // Збільшуємо лічильник кадрів, якщо гра активна і не на паузі
        if (!m_isPaused && m_isStart) {
            m_currentFrame++;
        }
    }

    void handleButton(PlayerButton button, bool holding, bool isPlayer1) {
        // Зчитуємо налаштування з mod.json в реальному часі
        bool modEnabled = Mod::get()->getSettingValue<bool>("enabled");
        int tolerance = static_cast<int>(Mod::get()->getSettingValue<int64_t>("tolerance-frames"));

        // Якщо мод вимкнено або макрос порожній — граємо звичайно
        if (!modEnabled || m_macroEvents.empty()) {
            PlayLayer::handleButton(button, holding, isPlayer1);
            return;
        }

        bool shouldIgnore = false;
        bool shouldDelay = false;

        // Шукаємо відповідну подію в макросі
        for (const auto& event : m_macroEvents) {
            if (event.button == static_cast<int>(button) && event.down == holding) {
                int frameDiff = event.frame - m_currentFrame;

                // Якщо гравець натиснув у межах дозволеного вікна допуску
                if (std::abs(frameDiff) <= tolerance) {
                    if (frameDiff > 0) {
                        // Натиснув трохи раніше — затримуємо виконання до кадру макросу
                        shouldDelay = true;
                    }
                    break;
                } 
                else if (frameDiff > tolerance) {
                    // Натиснув НАБАГАТО раніше (поза межами допуску) — ігноруємо для легіт-гри
                    shouldIgnore = true;
                }
            }
        }

        if (shouldIgnore) {
            // Повністю ігноруємо передчасний клік гравця
            log::debug("Ввід проігноровано (надто рано): кадр {}", m_currentFrame);
            return;
        }

        // Передаємо ввід далі у гру
        PlayLayer::handleButton(button, holding, isPlayer1);
    }
};
