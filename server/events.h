#ifndef ROBOTS_SERVER_EVENTS_H
#define ROBOTS_SERVER_EVENTS_H

#include <iostream>
#include <variant>
#include "types.h"

enum EventType {
    BOMB_PLACED, BOMB_EXPLODED,
    PLAYER_MOVED, BLOCK_PLACED
};

struct BombPlaced {
    BombId id;
    Position position;

    void write(TcpConnection &c) const {
        c.write((uint8_t) BOMB_PLACED);
        id.write(c);
        position.write(c);
    }
};

struct BombExploded {
    BombId id;
    vector<PlayerId> robots_destroyed;
    vector<Position> blocks_destroyed;

    void write(TcpConnection &c) const {
        c.write((uint8_t) BOMB_EXPLODED);
        id.write(c);
        c.writeList<PlayerId>(robots_destroyed);
        c.writeList<Position>(blocks_destroyed);
    }
};

struct PlayerMoved {
    PlayerId id;
    Position position;

    void write(TcpConnection &c) const {
        c.write((uint8_t) PLAYER_MOVED);
        id.write(c);
        position.write(c);
    }
};

struct BlockPlaced {
    Position position;

    void write(TcpConnection &c) const {
        c.write((uint8_t) BLOCK_PLACED);
        position.write(c);
    }
};

using event_t = std::variant<BombPlaced, BombExploded, PlayerMoved, BlockPlaced>;

#endif //ROBOTS_SERVER_EVENTS_H
