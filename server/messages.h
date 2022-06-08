#ifndef ROBOTS_SERVER_MESSAGES_H
#define ROBOTS_SERVER_MESSAGES_H

#include <optional>
#include <thread>
#include <variant>

#include "types.h"
#include "events.h"
#include "blocking-queue.h"

const int CLIENT_MESSAGE_TYPE_MAX = 3;

/**
 * Pomocniczy typ dla std::visit.
 * Funktor z przeładowanym operatorem ()
 * dla każdego z typów, które chcemy obsługiwać jako argument.
 */
template<class... Ts>
struct Overloaded : Ts ... {
    using Ts::operator()...;
};

/* To są obsługiwane rodzaje wiadomości od klientów. */
enum ClientMessageType : uint8_t {
    CLIENT_JOIN, CLIENT_PLACE_BOMB, CLIENT_PLACE_BLOCK, CLIENT_MOVE
};

ClientMessageType readClientMessageType(TcpConnection &c) {
    uint8_t t = c.readU8();
    if (!(t <= CLIENT_MESSAGE_TYPE_MAX)) {
        throw std::invalid_argument("Invalid move direction!");
    }
    return (ClientMessageType) t;
}

/* Klient chce dołączyć do gry. */
struct Join {
    ClientMessageType type;
    string name;
};

/* Gracz chce podłożyć na swojej pozycji bombę. */
struct PlaceBomb {
    ClientMessageType type;
};

/* Gracz chce położyć na swojej pozycji blok. */
struct PlaceBlock {
    ClientMessageType type;
};

/* Gracz chce wykonać ruch. */
struct Move {
    ClientMessageType type;
    Direction direction;
};

using client_mess_t = std::variant<Join, PlaceBomb, PlaceBlock, Move>;

/* To są rodzaje wiadomości wysyłanych przez serwer. */
enum ServerMessage : uint8_t {
    HELLO, ACCEPTED_PLAYER, GAME_STARTED, TURN, GAME_ENDED
};

/**
 * Parametry serwera wysyłane klientowi
 * od razu po akceptacji połączenia.
 */
struct Hello {
    string server_name;
    uint8_t players_count;
    uint16_t size_x;
    uint16_t size_y;
    uint16_t game_length;
    uint16_t explosion_radius;
    uint16_t bomb_timer;

    void write(TcpConnection &c) const {
        c.write((uint8_t) HELLO);
        c.write(server_name);
        c.write(players_count);
        c.write(size_x);
        c.write(size_y);
        c.write(game_length);
        c.write(explosion_radius);
        c.write(bomb_timer);
    }
};

/* Serwer zaakceptował chęć klienta do gry. */
struct AcceptedPlayer {
    PlayerId id;
    Player player;

    void write(TcpConnection &c) const {
        c.write(ACCEPTED_PLAYER);
        id.write(c);
        player.write(c);
    }
};

/* Rozgrywka się rozpoczęła. */
struct GameStarted {
    map<PlayerId, Player> players;

    void write(TcpConnection &c) const {
        c.write(GAME_STARTED);
        c.writeMap<PlayerId, Player>(players);
    }
};

/* Rozgrywka została zakończona. */
struct GameEnded {
    // Wyniki poszczególnych graczy.
    map<PlayerId, Score> scores;

    void write(TcpConnection &c) const {
        c.write(GAME_ENDED);
        c.writeMap<PlayerId, Score>(scores);
    }
};

/* Jedna tura rozgrywki. */
struct Turn {
    uint16_t turn;
    vector<event_t> events;

    void write(TcpConnection &c) const {
        c.write(TURN);
        c.write(turn);
        writeEventList(c);
    }

private:
    void writeEventList(TcpConnection &c) const {
        c.write((uint32_t) events.size());
        for (auto &e: events) {
            std::visit(Overloaded{
                    [&](const BombPlaced &event) { event.write(c); },
                    [&](const BombExploded &event) { event.write(c); },
                    [&](const PlayerMoved &event) { event.write(c); },
                    [&](const BlockPlaced &event) { event.write(c); }
            }, e);
        }
    }
};

using server_mess_t = std::variant<Hello, AcceptedPlayer, GameStarted, Turn, GameEnded>;
using server_mess_queue_t = BlockingQueue<std::shared_ptr<server_mess_t>>;

void writeServerMessage(TcpConnection &c, const std::shared_ptr<server_mess_t>& message_ptr) {
    server_mess_t mess = *message_ptr;
    std::visit(Overloaded{
            [&](const Hello &m) { m.write(c); },
            [&](const AcceptedPlayer &m) { m.write(c); },
            [&](const GameStarted &m) { m.write(c); },
            [&](const Turn &m) { m.write(c); },
            [&](const GameEnded &m) { m.write(c); }
    }, mess);
}

#endif //ROBOTS_SERVER_MESSAGES_H
