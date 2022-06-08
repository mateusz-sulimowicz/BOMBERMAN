#ifndef ROBOTS_SERVER_TYPES_H
#define ROBOTS_SERVER_TYPES_H

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>

#include <endian.h>

#include <boost/asio.hpp>

#include "tcp-connection.h"

const int DIRECTIONS = 4;

/* To są możliwe kierunki ruchu gracza. */
enum Direction : uint8_t {
    UP, RIGHT, DOWN, LEFT
};

Direction readDirection(TcpConnection &c) {
    uint8_t d = c.readU8();
    if (d >= DIRECTIONS) {
        throw std::invalid_argument("Invalid move direction!");
    }
    return (Direction) d;
}

std::pair<int, int> getDelta(Direction d) {
    switch (d) {
        case UP:
            return {0, 1};
        case DOWN:
            return {0, -1};
        case LEFT:
            return {-1, 0};
        case RIGHT:
            return {1, 0};
        default:
            return {0, 0};
    }
}

struct Position {
    uint16_t x;
    uint16_t y;

    static Position read(TcpConnection &c) {
        return {c.readU16(), c.readU16()};
    }

    void write(TcpConnection &s) const {
        s.write(x);
        s.write(y);
    }

    auto operator<=>(const Position &other) const {
        return std::tie(x, y) <=> std::tie(other.x, other.y);
    }
};

/**
 * Definicje struktur występujących w komunikatach
 * przesyłanych między serwerem i klientem.
 */

using client_id_t = size_t;

struct PlayerId {
    uint8_t value;

    static PlayerId read(TcpConnection &c) {
        return {c.readU8()};
    }

    void write(TcpConnection &s) const {
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

    void write(TcpConnection &s) const {
        s.write(value);
    }
};

struct BombId {
    uint32_t value;

    static BombId read(TcpConnection &c) {
        return {c.readU32()};
    }

    void write(TcpConnection &s) const {
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

    void write(TcpConnection &s) const {
        s.write(name);
        s.write(address);
    }
};

struct Bomb {
    Position position;
    uint16_t timer;

    void write(TcpConnection &s) const {
        position.write(s);
        s.write(timer);
    }
};

#endif //ROBOTS_SERVER_TYPES_H
