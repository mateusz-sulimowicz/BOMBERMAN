#ifndef SIK_2_TYPES_H
#define SIK_2_TYPES_H

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <set>

#include <endian.h>

#include "tcp-connection.h"
#include "udp-socket.h"

/**
 * Definicje struktur występujących w komunikatach
 * przesyłanych między serwerem oraz interfejsem użytkownika.
 */

struct PlayerId {
    uint8_t value;

    static PlayerId read(TcpConnection &c) {
        return {c.readU8()};
    }

    void write(UdpSocket &s) const {
        s.write(value);
    }

    auto operator<=>(const PlayerId &other) const {
        return value <=> other.value;
    }

};

struct Score {
    uint32_t value;

    static Score read(TcpConnection &c) {
        return {c.readU32()};
    }

    void write(UdpSocket &s) const {
        s.write(value);
    }
};

struct BombId {
    uint32_t value;

    static BombId read(TcpConnection &c) {
        return {c.readU32()};
    }

    void write(UdpSocket &s) const {
        s.write(value);
    }

    auto operator<=>(const BombId &other) const {
        return value <=> other.value;
    }
};

struct Player {
    string name;
    string address;

    static Player read(TcpConnection &c) {
        return {c.readString(), c.readString()};
    }

    void write(UdpSocket &s) const {
        s.write(name);
        s.write(address);
    }
};

struct Position {
    uint16_t x;
    uint16_t y;

    static Position read(TcpConnection &c) {
        return {c.readU16(), c.readU16()};
    }

    void write(UdpSocket &s) const {
        s.write(x);
        s.write(y);
    }

    auto operator<=>(const Position &other) const {
        return std::tie(x, y) <=> std::tie(other.x, other.y);
    }
};

struct Bomb {
    Position position;
    uint16_t timer;

    void write(UdpSocket &s) const {
        position.write(s);
        s.write(timer);
    }
};

enum State {
    LOBBY, GAME
};

/**
 * To jest struktura reprezentująca aktualny stan rozgrywki.
 * Przechowuje wszystkie informacje,
 * które są wysyłane do interfejsu użytkownika.
 */
struct ClientState {
    explicit ClientState(std::string player_name_opt)
            : player_name(std::move(player_name_opt)) {}

    /* Agregacja informacji z listy wydarzeń
     * przesyłanej przez serwer w wiadomości TURN. */
    std::set<PlayerId> robots_destroyed_in_turn;
    std::set<Position> blocks_destroyed_in_turn;

    /* Synchronizacja, żeby klient nie wysłał komunikatu JOIN
     * po odbiorze od serwera wiadomości GAME_STARTED. */
    std::mutex mutex;

    bool is_lobby = true;
    string player_name{};

    string server_name;
    uint8_t players_count{};
    uint16_t size_x{};
    uint16_t size_y{};
    uint16_t game_length{};
    uint16_t explosion_radius{};
    uint16_t bomb_timer{};

    // w grze
    uint16_t turn{};
    map<PlayerId, Player> players;
    map<PlayerId, Position> player_positions;
    std::set<Position> blocks;
    map<BombId, Bomb> bombs;
    std::set<Position> explosions;
    map<PlayerId, Score> scores;

    void write(UdpSocket &s) {
        if (is_lobby) {
            writeLobby(s);
        } else {
            writeGame(s);
        }
    }

private:
    void writeLobby(UdpSocket &s) const {
        s.write((uint8_t) LOBBY);
        s.write(server_name);
        s.write(players_count);
        s.write(size_x);
        s.write(size_y);
        s.write(game_length);
        s.write(explosion_radius);
        s.write(bomb_timer);
        s.writeMap<PlayerId, Player>(players);
    }

    void writeGame(UdpSocket &s) const {
        s.write((uint8_t) GAME);
        s.write(server_name);
        s.write(size_x);
        s.write(size_y);
        s.write(game_length);
        s.write(turn);
        s.writeMap<PlayerId, Player>(players);
        s.writeMap<PlayerId, Position>(player_positions);
        s.writeList<Position>(
                vector<Position>(blocks.begin(), blocks.end()));
        s.writeList<Bomb>(toList(bombs));
        s.writeList<Position>(
                vector<Position>(explosions.begin(), explosions.end()));
        s.writeMap<PlayerId, Score>(scores);
    }

private:
    static vector<Bomb> toList(const map<BombId, Bomb> &bombs) {
        vector<Bomb> res;
        res.reserve(bombs.size());
        for (const auto &[k, v]: bombs) {
            res.push_back(v);
        }
        return res;
    }
};


#endif //SIK_2_TYPES_H
