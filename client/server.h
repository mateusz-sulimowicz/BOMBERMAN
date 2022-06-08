#ifndef SIK_2_SERVER_H
#define SIK_2_SERVER_H

#include <variant>
#include <type_traits>

#include "types.h"
#include "events.h"
#include "udp-socket.h"

/**
 * Obsługuje komunikację z serwerem gry, czyli:
 * - Odbiera wiadomości od serwera,
 * - Aktualizuje stan klienta,
 * - przesyła odpowiednie komunikaty do
 *   interfejsu użytkownika.
 */
class ServerHandler {

    /**
     * Pomocniczy typ dla std::visit.
     * Funktor z przeładowanym operatorem ()
     * dla każdego z typów, które chcemy obsługiwać jako argument.
     */
    template<class... Ts>
    struct Overloaded : Ts ... {
        using Ts::operator()...;
    };

    // --- Struktury reprezentujące wiadomości od serwera ---

    enum ServerMessage {
        HELLO, ACCEPTED_PLAYER, GAME_STARTED, TURN, GAME_ENDED
    };

    struct Hello {
        string server_name;
        uint8_t players_count;
        uint16_t size_x;
        uint16_t size_y;
        uint16_t game_length;
        uint16_t explosion_radius;
        uint16_t bomb_timer;

        static Hello read(TcpConnection &c) {
            return {
                    c.readString(), // server_name
                    c.readU8(), // players_count
                    c.readU16(), // size_x
                    c.readU16(), // size_y
                    c.readU16(), // game_length
                    c.readU16(), // explosion_radius
                    c.readU16(), // bomb_timer
            };
        }
    };

    struct AcceptedPlayer {
        PlayerId id;
        Player player;

        static AcceptedPlayer read(TcpConnection &c) {
            return {PlayerId::read(c), Player::read(c)};
        }
    };

    struct GameStarted {
        map<PlayerId, Player> players;

        static GameStarted read(TcpConnection &c) {
            return {c.readMap<PlayerId, Player>()};
        }
    };

    struct Turn {
        uint16_t turn;
        vector<event_t> events;

        static Turn read(TcpConnection &c) {
            return {c.readU16(), readEventList(c)};
        }
    };

    struct GameEnded {
        map<PlayerId, Score> scores;

        static GameEnded read(TcpConnection &c) {
            return {c.readMap<PlayerId, Score>()};
        }
    };

public:
    ServerHandler(TcpConnection &server, UdpSocket &gui, ClientState &s)
            : server(server), gui(gui), state(s) {}

    [[noreturn]] void run() {
        for (;;) {
            gui.clearOutput();
            handleMessage();
            gui.send();
        }
    }

private:
    TcpConnection &server;
    UdpSocket &gui;
    ClientState &state;

    // --- Obsługa komunikatów od serwera. ---

    void handle(const Hello &m) {
        state.server_name = m.server_name;
        state.players_count = m.players_count;
        state.size_x = m.size_x;
        state.size_y = m.size_y;
        state.game_length = m.game_length;
        state.explosion_radius = m.explosion_radius;
        state.bomb_timer = m.bomb_timer;
        state.write(gui);
    }

    void handle(const AcceptedPlayer &m) {
        state.players[m.id] = m.player;
        state.write(gui);
    }

    void handle(const GameStarted &m) {
        state.scores.clear();
        state.blocks.clear();
        state.bombs.clear();
        state.explosions.clear();

        state.is_lobby = false; // Rozpoczęcie rozgrywki.
        state.players = m.players;

        for (const auto &[id, player]: m.players) {
            state.scores[id] = {0};
        }
    }

    void handle(const Turn &m) {
        state.turn = m.turn;
        state.explosions.clear();

        // Z listy wydarzeń zbieramy informacje
        // o zniszczonych blokach i robotach.
        state.blocks_destroyed_in_turn.clear();
        state.robots_destroyed_in_turn.clear();

        for (auto &[id, bomb]: state.bombs) {
            --bomb.timer;
        }

        for (auto &e: m.events) {
            std::visit(Overloaded{
                    [&](const BombPlaced &event) { event.apply(state); },
                    [&](const BombExploded &event) { event.apply(state); },
                    [&](const PlayerMoved &event) { event.apply(state); },
                    [&](const BlockPlaced &event) { event.apply(state); }
            }, e);
        }

        // Aplikujemy zebrane informacje
        // o zniszczonych blokach i robotach.
        // do stanu klienta.
        for (const auto &p: state.robots_destroyed_in_turn) {
            state.scores[p] = {state.scores[p].value + 1};
        }
        for (const auto &p: state.blocks_destroyed_in_turn) {
            state.blocks.erase(p);
        }

        state.write(gui);
    }

    void handle(const GameEnded &m) {
        state.is_lobby = true; // Powrót do lobby.
        state.scores = m.scores;
        state.players.clear();
        state.blocks.clear();
        state.bombs.clear();
        state.explosions.clear();
        state.write(gui);
    }

    void handleMessage() {
        uint8_t event_type = server.readU8();
        std::scoped_lock lock(state.mutex);
        switch (event_type) {
            case HELLO:
                handle(Hello::read(server));
                break;
            case ACCEPTED_PLAYER:
                handle(AcceptedPlayer::read(server));
                break;
            case GAME_STARTED:
                handle(GameStarted::read(server));
                break;
            case TURN:
                handle(Turn::read(server));
                break;
            case GAME_ENDED:
                handle(GameEnded::read(server));
                break;
            default:
                // Klient powinien rozłączyć się
                // po napotkaniu niepoprawnego komunikatu.
                throw std::invalid_argument((boost::format(
                        "Server message - Unrecognised message type: %1%.\n")
                                             % event_type).str());
        }
    }

};

#endif //SIK_2_SERVER_H
