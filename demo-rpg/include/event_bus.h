#ifndef RPG_EVENT_BUS_H
#define RPG_EVENT_BUS_H

#include <functional>
#include <utility>
#include <vector>

namespace rpg {

// Шаблонная шина событий. Для каждого типа события — свой экземпляр шины,
// со своим списком подписчиков. Использование:
//
//   struct ItemPickedEvent { std::string name; };
//   EventBus<ItemPickedEvent> bus;
//   bus.subscribe([](const auto& e) {
//       std::cout << "поднят: " << e.name;
//   });
//   bus.publish({"меч"});
template <typename Event>
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    // Подписать обработчик. Он будет вызван на каждом publish.
    void subscribe(Handler handler) {
        handlers_.push_back(std::move(handler));
    }

    // Опубликовать событие — вызывает всех подписчиков по очереди.
    void publish(const Event& event) const {
        for (const auto& h : handlers_) {
            h(event);
        }
    }

    // Количество подписчиков — для тестов и диагностики.
    std::size_t subscriber_count() const {
        return handlers_.size();
    }

private:
    std::vector<Handler> handlers_;
};

}  // namespace rpg

#endif
