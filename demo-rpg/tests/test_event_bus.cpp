// Демонстрация шаблонной шины событий.

#include "event_bus.h"
#include "events.h"

#include <iostream>
#include <string>

int main() {
    using namespace rpg;

    EventBus<ItemPickedEvent> pickup_bus;
    EventBus<PlayerMovedEvent> move_bus;

    // Подписчик-логгер.
    pickup_bus.subscribe([](const ItemPickedEvent& e) {
        std::cout << "[log] " << e.player_name
                  << " поднял " << e.item_name << "\n";
    });

    // Другой подписчик: ачивка за первый предмет.
    bool first_pickup = true;
    pickup_bus.subscribe([&first_pickup](const ItemPickedEvent& e) {
        if (first_pickup) {
            std::cout << "[ачивка] '" << e.player_name
                      << "' получил первый предмет!\n";
            first_pickup = false;
        }
    });

    move_bus.subscribe([](const PlayerMovedEvent& e) {
        std::cout << "[log] " << e.player_name
                  << ": " << e.from_id << " -> " << e.to_id << "\n";
    });

    std::cout << "pickup-подписчиков: " << pickup_bus.subscriber_count() << "\n";
    std::cout << "move-подписчиков:   " << move_bus.subscriber_count() << "\n\n";

    pickup_bus.publish({"Герой", "ржавый меч"});
    pickup_bus.publish({"Герой", "зелье лечения"});
    move_bus.publish({"Герой", "glade", "village"});

    return 0;
}
