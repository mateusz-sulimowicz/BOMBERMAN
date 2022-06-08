/**
 * Implementacja serwera do gry Roboty.
 *
 * Mateusz Sulimowicz, MIMUW 2022.
 */

#include <iostream>
#include <string>
#include <vector>
#include <tuple>

#include <cerrno>

#include <boost/program_options.hpp>

#include "client-acceptor.h"
#include "game-manager.h"

using std::string;
using std::vector;

using namespace boost::program_options;

namespace {

    const size_t MAX_THREADS = 64;

    const std::set<string> SERVER_PARAMS = {"server-name", "players-count",
                                            "bomb-timer",
                                            "explosion-radius", "initial-blocks",
                                            "game-length", "port", "size-x",
                                            "size-y",
                                            "seed", "turn-duration"};

    /**
     * Sprawdza, czy wszystkie obowiązkowe
     * opcje programu zostały podane.
     */
    void checkOptions(const variables_map &vm) {
        for (const string &s: SERVER_PARAMS) {
            if (!vm.count(s)) {
                throw std::invalid_argument(
                        (boost::format("Missing option: %1%") % s).str());
            }
        }
        if (vm["server-name"].as<string>().length() > UINT8_MAX) {
            throw std::invalid_argument("Player name too long");
        }
    }

    /**
     * Boost Program Options dla parametrów bez znaku
     * akceptuje liczby ujemne, bo po prostu je rzutuje.
     * Z tego powodu, powstały poniższe funkcje, akceptujące
     * wartość 2 razy większego typu ze znakiem,
     * do ręcznego sprawdzania, czy argument jest w odpowiednim zakresie.
     */

    uint8_t parsePositive(int16_t val) {
        if (0 < val && val <= UINT8_MAX) {
            return (uint8_t) val;
        }
        throw std::invalid_argument{"Program option out of range.\n"};
    }

    uint16_t parse(int32_t val) {
        if (0 <= val && val <= UINT16_MAX) {
            return (uint16_t) val;
        }
        throw std::invalid_argument{"Program option out of range.\n"};
    }

    uint16_t parsePositive(int32_t val) {
        if (0 < val && val <= UINT16_MAX) {
            return (uint16_t) val;
        }
        throw std::invalid_argument{"Program option out of range.\n"};
    }

    uint32_t parsePositive(int64_t val) {
        if (0 < val && val <= UINT32_MAX) {
            return (uint32_t) val;
        }
        throw std::invalid_argument{"Program option out of range.\n"};
    }

    uint64_t parse(const string &val) {
        char *endptr = nullptr;
        errno = 0;
        uint64_t res = std::strtoull(val.c_str(), &endptr, 10);
        if (val[0] != '-' && errno == 0 && endptr == val.c_str() + val.length()) {
            return res;
        }
        throw std::invalid_argument{"Program option invalid.\n"};
    }

    uint64_t parsePositive(const string &val) {
        uint64_t res = parse(val);
        if (res > 0) {
            return res;
        }
        throw std::invalid_argument{"Program option out of range.\n"};
    }

    ServerParams parseParams(const variables_map &vm) {
        ServerParams p;

        p.bomb_timer = parsePositive(vm["bomb-timer"].as<int32_t>());
        p.players_count = parsePositive(vm["players-count"].as<int16_t>());
        p.turn_duration = parsePositive(vm["turn-duration"].as<string>());
        p.explosion_radius = parse(vm["explosion-radius"].as<int32_t>());
        p.initial_blocks = parse(vm["initial-blocks"].as<int32_t>());
        p.game_length = parsePositive(vm["game-length"].as<int32_t>());
        p.server_name = vm["server-name"].as<string>();
        p.port = parsePositive(vm["port"].as<int32_t>());
        p.seed = parsePositive(vm["seed"].as<int64_t>());
        p.size_x = parsePositive(vm["size-x"].as<int32_t>());
        p.size_y = parsePositive(vm["size-y"].as<int32_t>());

        return p;
    }

    void printHelp(const options_description &desc) {
        std::cout << "Usage: " << program_invocation_name << "\n";
        std::cout << desc;
    }
}

int main(int ac, char *av[]) {
    options_description desc("Allowed options");
    desc.add_options()
            ("help,h", "Print help information")
            ("bomb-timer,b", value<int32_t>(),
             "After this amount of turns placed bomb should explode. In (0, UINT16_MAX].")
            ("players-count,c", value<int16_t>(),
             "Number of allowed in-game players. In (0, UINT8_MAX].")
            ("turn-duration,d", value<string>(),
             "Lenght of turn in miliseconds. In (0, UINT64_MAX].")
            ("explosion-radius,e", value<int32_t>(),
             "Length of the explosion cross radius in [0, UINT16_MAX]")
            ("initial-blocks,k", value<int32_t>(),
             "Amount of initially placed blocks by server. In [0, UINT16_MAX].")
            ("game-length,l", value<int32_t>(),
             "Amount of turns in game in (0, UINT16_MAX].")
            ("server-name,n", value<string>(),
             "At most 255 byte string.")
            ("port,p", value<int32_t>(),
             "On this port server accepts new connections. In (0, UINT16_MAX].")
            ("seed,s", value<int64_t>()->default_value(time(nullptr)),
             "Seed for random number generation. In (0, UINT32_MAX]. Optional parameter.")
            ("size-x,x", value<int32_t>(),
             "Size of board in X direction. In (0, UINT16_MAX].")
            ("size-y,y", value<int32_t>(),
             "Size of board in Y direction. In (0, UINT16_MAX].");

    variables_map vm;
    ServerParams params;
    try {
        store(parse_command_line(ac, av, desc), vm);
        notify(vm);

        checkOptions(vm);
        params = parseParams(vm);
    } catch (std::exception &e) {
        printHelp(desc);
        exit(EXIT_FAILURE);
    }

    if (vm.count("help")) {
        printHelp(desc);
        exit(EXIT_SUCCESS);
    }

    auto thread_pool = std::make_shared<boost::asio::thread_pool>(MAX_THREADS);
    auto context = std::make_shared<boost::asio::io_context>();
    auto server = std::make_shared<Server>(params);

    boost::asio::post(*thread_pool, [=] {
        try {
            ClientAcceptor{params.port, server, context, thread_pool}.run();
        } catch (std::exception &e) {
            std::cerr << "Client acceptor failed. Reason:\n";
            std::cerr << e.what() << "\n";
            std::exit(EXIT_FAILURE);
        }
    });

    try {
        GameManager{params, server}.run();
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        std::exit(EXIT_FAILURE);
    }
}
