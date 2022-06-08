#ifndef SIK_2_EVENTS_H
#define SIK_2_EVENTS_H

#include <iostream>
#include <variant>
#include "types.h"

/**
 * Struktury reprezentujące zdarzenia,
 * które mogą odbyć się w trakcie tury rozgrywki.
 *
 * Każdy rodzaj zdarzenia można zaaplikować
 * do aktualnego stanu klienta, odpowiedno go modyfikując.
 */

enum EventType {
    BOMB_PLACED, BOMB_EXPLODED,
    PLAYER_MOVED, BLOCK_PLACED
};

struct BombPlaced {
    BombId id;
    Position position;

    static BombPlaced read(TcpConnection &c) {
        return {BombId::read(c), Position::read(c)};
    }

    void apply(ClientState &c) const {
        c.bombs[id] = {position, c.bomb_timer};
    }
};

struct BombExploded {
    BombId id;
    vector<PlayerId> robots_destroyed;
    vector<Position> blocks_destroyed;

    static BombExploded read(TcpConnection &c) {
        return {
                BombId::read(c),
                c.readList<PlayerId>(),
                c.readList<Position>()
        };
    }

    void apply(ClientState &c) const {
        calcExplosion(c);

        for (const auto &p: blocks_destroyed) {
            c.blocks_destroyed_in_turn.insert(p);
        }
        for (const auto &p: robots_destroyed) {
            c.robots_destroyed_in_turn.insert(p);
            c.player_positions.erase(p);
        }
        c.bombs.erase(id);
    }

private:
    static const size_t DIRECTIONS = 4;

    /**
     * Wybuch bomby ma kształt krzyża
     * o ramieniu długości `ClientState.explosion_radius`.
     * Eksplozja zatrzymuje się na blokach, więc rzeczywiste
     * ramię krzyża może być krótsze.
     */
    void calcExplosion(ClientState &c) const {
        auto bomb_pos = c.bombs[id].position;
        std::array<int, DIRECTIONS> dx = {1, -1, 0, 0};
        std::array<int, DIRECTIONS> dy = {0, 0, 1, -1};
        for (size_t i = 0; i < DIRECTIONS; ++i) {
            for (uint16_t r = 0; r <= c.explosion_radius; ++r) {
                int x = (int) bomb_pos.x + dx[i] * (int) r;
                int y = (int) bomb_pos.y + dy[i] * (int) r;
                if (0 <= x && x < (int) c.size_x
                    && 0 <= y && y < (int) c.size_y) {
                    // 0 <= x, y < UINT16_MAX, więc można bezpiecznie rzutować.
                    auto pos = Position{(uint16_t) x, (uint16_t) y};
                    c.explosions.insert(pos);
                    if (c.blocks.contains(pos)) {
                        break;
                    }
                }
            }
        }
    }

};

struct PlayerMoved {
    PlayerId id;
    Position position;

    static PlayerMoved read(TcpConnection &c) {
        return {PlayerId::read(c), Position::read(c)};
    }

    void apply(ClientState &c) const {
        c.player_positions[id] = position;
    }
};

struct BlockPlaced {
    Position position;

    static BlockPlaced read(TcpConnection &c) {
        return BlockPlaced{Position::read(c)};
    }

    void apply(ClientState &c) const {
        c.blocks.insert(position);
    }
};

using event_t = std::variant<BombPlaced, BombExploded, PlayerMoved, BlockPlaced>;

event_t readEvent(TcpConnection &c) {
    uint8_t event_type = c.readU8();
    switch (event_type) {
        case BOMB_PLACED:
            return BombPlaced::read(c);
        case BOMB_EXPLODED:
            return BombExploded::read(c);
        case PLAYER_MOVED:
            return PlayerMoved::read(c);
        case BLOCK_PLACED:
            return BlockPlaced::read(c);
        default:
            // Klient zrywa połączenie przy napotkaniu niepoprawnej wiadomości.
            throw std::invalid_argument((boost::format(
                    "Server message - Unrecognised event type: %1%")
                                         % event_type).str());
    }
}

vector<event_t> readEventList(TcpConnection &c) {
    uint32_t len = c.readU32();
    vector<event_t> res;
    for (uint32_t i = 0; i < len; ++i) {
        res.push_back(readEvent(c));
    }
    return res;
}

#endif //SIK_2_EVENTS_H
