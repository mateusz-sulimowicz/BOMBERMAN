#ifndef ROBOTS_SERVER_SERVER_H
#define ROBOTS_SERVER_SERVER_H

#include <optional>
#include <utility>
#include <semaphore>
#include <queue>

#include "blocking-queue.h"
#include "messages.h"

using std::queue;

struct ServerParams {
    uint16_t bomb_timer;
    uint8_t players_count;
    uint64_t turn_duration;
    uint16_t explosion_radius;
    uint16_t initial_blocks;
    uint16_t game_length;
    string server_name;
    uint16_t port;
    uint32_t seed;
    uint16_t size_x;
    uint16_t size_y;
};

/**
 * To jest punkt komunikacji między wątkami.
 *
 * Za pomocą tej struktury, wątki obsługujące
 * przychodzące wiadomości od klientów
 * udostępniają ostatnią przeczytaną
 * wiadomość zarządcy gry, a zarządca może
 * rozgłosić komunikat do wszystkich połączonych klientów.
 */
class Server {
public:
    explicit Server(ServerParams params) : params(std::move(params)) {
        initializeMessageHistory();
    }

    /**
     * Nadaje połączonemu klientowi identyfikator.
     */
    client_id_t acceptClient() {
        std::unique_lock lock(mutex);

        return next_client_id++;
    }

    /**
     * Tworzy kolejkę do przesyłania danemu
     * klientowi wiadomości.
     */
    std::shared_ptr<server_mess_queue_t> createMessageQueue(client_id_t client_id) {
        std::unique_lock lock(mutex);

        assert(!client_message_queues.contains(client_id));
        client_message_queues[client_id] = std::make_shared<server_mess_queue_t>(message_history);
        return client_message_queues[client_id];
    }

    /**
     * Usuwa związane z klientem o danym identyfikatorze
     * struktury danych.
     */
    void eraseClient(client_id_t client_id) {
        std::unique_lock lock(mutex);

        players.erase(player_ids[client_id]);
        player_ids.erase(client_id);

        if (client_message_queues.contains(client_id)) {
            client_message_queues[client_id]->close();
            client_message_queues.erase(client_id);
        }

        last_messages_from_clients.erase(client_id);
    }

    /**
     * Aktualizuje ostatnio odebraną wiadomość od danego klienta.
     */
    void setLastMessage(client_id_t id, const client_mess_t &message) {
        std::unique_lock lock(mutex);

        last_messages_from_clients[id] = message;
    }

    /**
     * Tworzy mapę najnowszych wiadomości od klientów,
     * przysłanych w czasie trwania aktualnej tury.
     */
    map<PlayerId, client_mess_t> collectLastMessagesFromClients() {
        std::unique_lock lock(mutex);

        map<PlayerId, client_mess_t> messages;
        for (const auto &[client_id, last_message] : last_messages_from_clients) {
            messages[player_ids[client_id]] = last_message;
        }
        last_messages_from_clients.clear();

        return messages;
    }

    void tryAcceptPlayer(client_id_t client_id, const string &name, const string &address) {
        std::unique_lock lock(mutex);

        if (is_lobby) {
            if (!player_ids.contains(client_id) && player_ids.size() < params.players_count) {
                // Zaakceptuj nowego gracza.
                auto player_id = PlayerId{(uint8_t) (player_ids.size())};
                auto player = Player{name, address};

                player_ids[client_id] = player_id;
                players[player_id] = player;

                // Powiadamiom wszystkich klientów, że nowy gracz dołączył do Lobby.
                broadcast(server_mess_t{AcceptedPlayer{player_id, player}});
                players_joined.notify_all();
            }
        } // Wpp ignoruj wiadomość.
    }

    /**
     * Wołający wątek czeka aż zbierze się params.players_count
     * graczy. Gdy zostanie obudzony, rozpoczyna grę.
     */
    map<PlayerId, Player> waitForPlayersToStartGame() {
        std::unique_lock lock(mutex);

        // Czekaj aż zbierze się dostatecznie wielu graczy.
        players_joined.wait(lock, [&] { return players.size() == params.players_count; });

        startGame();
        return players;
    }

    /**
     * Rozgłasza do podłączonych klientów komunikat TURN.
     */
    void closeTurn(uint16_t turn_id, vector<event_t> events) {
        std::unique_lock lock(mutex);

        broadcast(server_mess_t{Turn{turn_id, std::move(events)}});
    }

    void endGame(const map<PlayerId, Score> &scores) {
        std::unique_lock lock(mutex);

        // Powiadamiom wszystkich klientów, że gra zakończyła się.
        broadcast(server_mess_t{GameEnded{scores}});
        startLobby();
    }

private:
    std::mutex mutex;
    std::condition_variable players_joined;

    map<PlayerId, Player> players{};
    map<client_id_t, PlayerId> player_ids{};
    map<client_id_t, std::shared_ptr<server_mess_queue_t>> client_message_queues{};
    map<client_id_t, client_mess_t> last_messages_from_clients{};
    client_id_t next_client_id = 0;

    bool is_lobby = true;
    const ServerParams params;

    queue<std::shared_ptr<server_mess_t>> message_history{};

    /**
     * Wyczyszczona historia wiadomości zawiera
     * tylko komunikat HELLO.
     */
    void initializeMessageHistory() {
        message_history = queue<std::shared_ptr<server_mess_t>>{};
        message_history.push(std::make_shared<server_mess_t>(
                Hello{
                        .server_name = params.server_name,
                        .players_count = params.players_count,
                        .size_x = params.size_x,
                        .size_y = params.size_y,
                        .game_length = params.game_length,
                        .explosion_radius = params.explosion_radius,
                        .bomb_timer = params.bomb_timer
                })
        );
    }

    void startLobby() {
        is_lobby = true;
        players.clear();
        player_ids.clear();
        last_messages_from_clients.clear();
        initializeMessageHistory();
    }

    void startGame() {
        is_lobby = false;
        last_messages_from_clients.clear();
        initializeMessageHistory();
        // Powiadamiom wszystkich klientów, że gra się rozpoczęła.
        broadcast(server_mess_t{GameStarted{players}});
    }

    /**
     * Rozsyła wiadomość do wszystkich podłączonych klientów
     * poprzez umieszczenie wskaźnika na nią
     * w kolejce każdego z nich.
     */
    void broadcast(const server_mess_t &message) {
        auto message_ptr = std::make_shared<server_mess_t>(message);
        message_history.push(message_ptr);
        for (auto &[client_id, message_queue_ptr]: client_message_queues) {
            if (message_queue_ptr->isOpen()) {
                message_queue_ptr->push(message_ptr);
            }
        }
    }

};

#endif //ROBOTS_SERVER_SERVER_H
