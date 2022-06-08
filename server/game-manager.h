#ifndef ROBOTS_SERVER_GAME_MANAGER_H
#define ROBOTS_SERVER_GAME_MANAGER_H

#include <set>
#include <random>
#include <utility>
#include <thread>
#include <chrono>
#include <optional>

#include "types.h"
#include "server.h"

using std::set;

/**
 * To jest zarządca gry.
 *
 * Rozpoczyna rozgrywkę, interpretuje ruchy graczy,
 * decyduje, czy ruchy są poprawne, liczy eksplozje bomb itp.
 */
class GameManager {
    struct GameState {
        map<BombId, Bomb> bombs;
        set<Position> blocks;
        map<PlayerId, Position> player_pos;
        map<PlayerId, Score> scores;
        BombId next_bomb_id = {0};
    };

public:
    GameManager(ServerParams params, std::shared_ptr<Server> server)
            : params(std::move(params)),
              server(std::move(server)),
              random(params.seed) {}

    [[noreturn]] void run() {
        for (;;) {
            GameState state;

            auto players = server->waitForPlayersToStartGame();
            auto initial_events = initializeGame(players, state);

            server->closeTurn(0, std::move(initial_events));

            for (uint16_t turn = 1; turn <= params.game_length; ++turn) {
                vector<event_t> events;

                std::this_thread::sleep_for(std::chrono::milliseconds(params.turn_duration));
                auto client_messages = server->collectLastMessagesFromClients();

                updateBombs(state, events);
                interpretAllClientMessages(client_messages, state, events);
                placeMissingRobots(players, state, events);

                server->closeTurn(turn, std::move(events));
            }
            server->endGame(state.scores);
        }
    }

private:
    ServerParams params;
    std::shared_ptr<Server> server;
    std::minstd_rand random;

    vector<event_t> initializeGame(const map<PlayerId, Player> &players, GameState &state) {
        vector<event_t> initial_events{};

        resetScores(players, state);
        placeMissingRobots(players, state, initial_events);
        placeInitialBlocks(state, initial_events);

        return initial_events;
    }

    void resetScores(const map<PlayerId, Player> &players, GameState &state) {
        for (const auto &[player_id, player]: players) {
            state.scores.clear();
            state.scores[player_id] = {0};
        }
    }

    /**
     * Umieszcza w pseudolosowych miejscach roboty,
     * których nie ma na planszy.
     */
    void placeMissingRobots(const map<PlayerId, Player> &players,
                            GameState &state,
                            vector<event_t> &events) {
        for (const auto &[player_id, player]: players) {
            if (!state.player_pos.contains(player_id)) {
                state.player_pos[player_id] = Position{
                        .x = (uint16_t) (random() % params.size_x),
                        .y = (uint16_t) (random() % params.size_y)
                };
                events.emplace_back(PlayerMoved{player_id, state.player_pos[player_id]});
            }
        }
    }

    /**
     * Umieszcza w pseudolosowych miejscach
     * dokładnie `params.initial_blocks`
     * bloków na planszy.
     */
    void placeInitialBlocks(GameState &state, vector<event_t> &events) {
        for (uint16_t i = 0; i < params.initial_blocks; ++i) {
            auto new_block_pos = Position{
                    .x = (uint16_t) (random() % params.size_x),
                    .y = (uint16_t) (random() % params.size_y)
            };
            state.blocks.insert(new_block_pos);
            events.emplace_back(BlockPlaced{new_block_pos});
        }
    }

    /**
     * Interpretuje wiadomości otrzymane od klientów w trakcie tury.
     * Wydarzenia niepoprawne, np. chęć wyjścia poza planszę, są ignorowane.
     *
     * Zwraca listę wydarzeń utworzonych na podstawie odebranych wiadomości.
     */
    void interpretAllClientMessages(map<PlayerId, client_mess_t> &messages, GameState &state,
                      vector<event_t> &events) {
        for (auto &[player_id, message]: messages) {
            if (state.player_pos.contains(player_id)) {
                // Robot gracza nie został
                // w aktualnej turze zniszczony.
                std::visit(Overloaded{
                        [&](const PlayerId &, const Join &) {
                            /* Ignoruj. */
                        },
                        [&](const PlayerId &p_id, const PlaceBomb &m) {
                            interpret(p_id, m, state, events);
                        },
                        [&](const PlayerId &p_id, const PlaceBlock &m) {
                            interpret(p_id, m, state, events);
                        },
                        [&](const PlayerId &p_id, const Move &m) {
                            interpret(p_id, m, state, events);
                        },
                }, std::variant<PlayerId>(player_id), message);
            }
        }
    }

    void interpret(PlayerId p_id, const PlaceBomb &, GameState &state,
                   vector<event_t> &events) {
        placeBomb(state.player_pos[p_id], state, events);
    }

    void interpret(PlayerId p_id, const PlaceBlock &, GameState &state,
                          vector<event_t> &events) {
        Position pos = state.player_pos[p_id];
        if (state.blocks.contains(pos)) {
            // Ignoruj próbę postawienia bloku tam, gdzie już stoi blok.
            return;
        }

        placeBlock(state.player_pos[p_id], state, events);
    }

    void interpret(PlayerId p_id, const Move &m, GameState &state,
                   vector<event_t> &events) {
        Position pos = state.player_pos[p_id];
        auto[delta_x, delta_y] = getDelta(m.direction); // Przesunięcie gracza.
        int new_x = pos.x + delta_x;
        int new_y = pos.y + delta_y;

        if (0 <= new_x && new_x < params.size_x
            && 0 <= new_y && new_y < params.size_y) {
            // Ponieważ `new_x`, `new_y` <= UINT16_MAX,
            // to można bezpiecznie rzutować.
            Position new_pos = {.x = (uint16_t) new_x, .y = (uint16_t) new_y};
            movePlayer(p_id, new_pos, state, events);
        } // Wpp ignoruj ruch.
    }

    void placeBomb(Position pos, GameState &state, vector<event_t> &events) {
        state.bombs[state.next_bomb_id] = Bomb{
                .position = pos,
                .timer = params.bomb_timer
        };;
        events.emplace_back(BombPlaced{state.next_bomb_id, pos});
        state.next_bomb_id = BombId{state.next_bomb_id.value + 1};
    }

    static void placeBlock(Position pos, GameState &state, vector<event_t> &events) {
        if (!state.blocks.contains(pos)) {
            state.blocks.insert(pos);
            events.emplace_back(BlockPlaced{pos});
        }
    }

    void movePlayer(PlayerId p_id, Position pos, GameState &state,
                    vector<event_t> &events) const {
        if (pos.x >= params.size_x || pos.y >= params.size_y) {
            // Gracz próbuje wyjśc poza planszę.
            return;
        }
        if (state.blocks.contains(pos)) {
            // Gracz próbuje wejść na pole, na którym stoi blok.
            return;
        }

        // Ruch jest poprawny.
        state.player_pos[p_id] = pos;
        events.emplace_back(PlayerMoved{p_id, pos});
    }

    void updateBombs(GameState &state, vector<event_t> &events) {
        set<PlayerId> robots_destroyed_total;
        set<Position> blocks_destroyed_total;
        set<BombId> bombs_exploded;

        for (auto &[bomb_id, bomb]: state.bombs) {
            if (bomb.timer > 1) {
                --bomb.timer;
            } else {
                // Bomba ma teraz wybuchnąć.
                auto[robots_destroyed, blocks_destroyed] = calcExplosionResult(bomb_id, state);

                robots_destroyed_total.insert(robots_destroyed.begin(), robots_destroyed.end());
                blocks_destroyed_total.insert(blocks_destroyed.begin(), blocks_destroyed.end());
                bombs_exploded.insert(bomb_id);

                events.emplace_back(BombExploded{
                        .id = bomb_id,
                        .robots_destroyed = toList<PlayerId>(robots_destroyed),
                        .blocks_destroyed = toList<Position>(blocks_destroyed)
                });
            }
        }

        // Wyczyść pozycje graczy, których roboty
        // zostały zniszczone w wyniku wybuchów.
        for (const auto &id: robots_destroyed_total) {
            state.scores[id] = {state.scores[id].value + 1};
            state.player_pos.erase(id);
        }

        // Usuń bloki zniszczone w wyniku wybuchów.
        for (const auto &pos: blocks_destroyed_total) {
            state.blocks.erase(pos);
        }

        for (const auto &id : bombs_exploded) {
            state.bombs.erase(id);
        }
    }

    std::pair<set<PlayerId>, set<Position>>
    calcExplosionResult(BombId id, const GameState &state) {
        auto bomb = state.bombs.at(id);

        set<Position> affected_positions = calcExplosion(bomb.position, state);
        set<PlayerId> robots_destroyed = calcDestroyedRobots(affected_positions, state);
        set<Position> blocks_destroyed = calcDestroyedBlocks(affected_positions, state);

        return std::pair<set<PlayerId>, set<Position>>{robots_destroyed, blocks_destroyed};
    }

    /**
     * Zwraca zbiór identyfikatorów graczy,
     * których roboty zostały zniszczone w wyniku wybuchu.
     */
    static set<PlayerId> calcDestroyedRobots(const set<Position> &positions_affected_by_explosion,
                                             const GameState &state) {
        set<PlayerId> robots_destroyed;

        for (const auto &[player_id, position]: state.player_pos) {
            if (positions_affected_by_explosion.contains(position)) {
                robots_destroyed.insert(player_id);
            }
        }

        return robots_destroyed;
    }

    /**
    * Zwraca zbiór pozycji bloków,
     * które zostały zniszczone w wyniku eksplozji.
    */
    static set<Position> calcDestroyedBlocks(const set<Position> &positions_affected_by_explosion,
                                             const GameState &state) {
        set<Position> blocks_destroyed;

        for (const Position &pos : positions_affected_by_explosion) {
            if (state.blocks.contains(pos)) {
                blocks_destroyed.insert(pos);
            }
        }

        return blocks_destroyed;
    }

    /**
     * Wybuch bomby ma kształt krzyża
     * o ramieniu długości `ClientState.explosion_radius`.
     * Eksplozja zatrzymuje się na blokach, więc rzeczywiste
     * ramię krzyża może być krótsze.
     *
     * Zwraca zbiór pozycji, na które eksplozja miała wpływ.
     */
    [[nodiscard]] set<Position>
    calcExplosion(Position bomb_pos, const GameState &state) const {
        set<Position> affected_pos;

        std::array<int, DIRECTIONS> dx = {1, -1, 0, 0};
        std::array<int, DIRECTIONS> dy = {0, 0, 1, -1};
        for (size_t i = 0; i < DIRECTIONS; ++i) {
            for (uint16_t r = 0; r <= params.explosion_radius; ++r) {
                int x = (int) bomb_pos.x + dx[i] * (int) r;
                int y = (int) bomb_pos.y + dy[i] * (int) r;

                if (0 <= x && x < (int) params.size_x
                    && 0 <= y && y < (int) params.size_y) {
                    // 0 <= x, y < UINT16_MAX, więc można bezpiecznie rzutować.
                    auto pos = Position{(uint16_t) x, (uint16_t) y};

                    affected_pos.insert(pos);
                    if (state.blocks.contains(pos)) {
                        break;
                    }
                }
            }
        }
        return affected_pos;
    }

    template<typename T>
    static vector<T> toList(const set<T> &s) {
        return vector<T>{s.begin(), s.end()};
    }

};


#endif //ROBOTS_SERVER_GAME_MANAGER_H
