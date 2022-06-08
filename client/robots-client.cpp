/**
 * Implementacja klienta do gry Roboty.
 *
 * Mateusz Sulimowicz, MIMUW 2022.
 */

#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <thread>

#include <cerrno>

#include <boost/program_options.hpp>

#include "tcp-connection.h"
#include "udp-socket.h"
#include "types.h"
#include "server.h"
#include "gui.h"

using std::string;

using namespace boost::program_options;

namespace {
    /**
     * Rozdziela napis postaci: <nazwa hosta/adres IPv4/adres IPv6>:<port>
     * na części <nazwa hosta/adres IPv4/adres IPv6> oraz port.
     */
    void splitPort(const string &s, string &addr, string &port) {
        auto i = s.rfind(':');
        if (i + 1 >= s.length()) {
            // Znak ':' nie istnieje w `state` lub jest ostatnim znakiem.
            throw std::invalid_argument(s);
        }
        addr = s.substr(0, i);
        port = s.substr(i + 1);
    }

    /**
     * Sprawdza, czy wszystkie obowiązkowe opcje programu są podane.
     */
    void checkOptions(const variables_map &vm) {
        for (const string s: {"gui-address",
                              "player-name",
                              "port",
                              "server-address"}) {
            if (!vm.count(s)) {
                throw std::invalid_argument(
                        (boost::format("Missing option: %1%") % s).str());
            }
        }
        if (vm["player-name"].as<string>().length() > UINT8_MAX) {
            throw std::invalid_argument("Player name too long");
        }
    }

    void usage(const options_description &desc) {
        std::cout << "Usage: " << program_invocation_name << "\n";
        std::cout << desc;
    }
}

void run(string server_addr, string server_port, string gui_addr,
         string gui_port, const string &player_name, uint16_t port) {
    boost::asio::io_context io_context;
    std::shared_ptr<TcpConnection> server;
    std::shared_ptr<UdpSocket> gui;
    ClientState state{player_name};

    // Próbuje nawiązać połączenie z serwerem.
    try {
        server = std::make_shared<TcpConnection>(io_context, server_addr,
                                                 server_port);
    } catch (std::exception &e) {
        throw std::runtime_error{(boost::format(
                "Failed to connect to game server at %1%:%2%. "
                "Reason:\n") % server_addr % server_port).str() + e.what()};
    }

    // Próbuje otworzyć gniazdo do komunikacji z GUI.
    try {
        gui = std::make_shared<UdpSocket>(io_context, gui_addr, gui_port, port);
    } catch (std::exception &e) {
        throw std::runtime_error{(boost::format(
                "Failed to open socket to GUI at %1%:%2%. "
                "Reason:\n") % gui_addr % gui_port).str() + e.what()};
    }

    auto gui_handler = GuiHandler{*gui, *server, state};
    auto server_handler = ServerHandler{*server, *gui, state};

    // Na oddzielnym wątku obsługujemy komunikację GUI -> klient -> serwer.
    std::jthread helper{[&]() {
        try {
            gui_handler.run();
        } catch (std::exception &e) {
            std::cerr << e.what() << "\n";
            exit(EXIT_FAILURE);
        }
    }};

    // W głównym wątku obsługujemy komunikację serwer -> klient -> GUI.
    try {
        server_handler.run();
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        exit(EXIT_FAILURE);
    }
}

int main(int ac, char *av[]) {
    options_description desc("Allowed options");
    desc.add_options()
            ("help,h", "Print help information")
            ("gui-address,d", value<string>(),
             "<(hostname):(port) or (IPv4):(port) or (IPv6):(port)>")
            ("player-name,n", value<string>(), "At most 255 bytes string")
            ("port,p", value<uint16_t>())
            ("server-address,s", value<string>(),
             "<(hostname):(port) or (IPv4):(port) or (IPv6):(port)>");

    variables_map vm;
    string server_addr, server_port, gui_addr, gui_port, player_name;
    uint16_t port;
    try {
        store(parse_command_line(ac, av, desc), vm);
        notify(vm);

        checkOptions(vm);
        player_name = vm["player-name"].as<string>();
        port = vm["port"].as<uint16_t>();
        splitPort(vm["server-address"].as<string>(), server_addr, server_port);
        splitPort(vm["gui-address"].as<string>(), gui_addr, gui_port);
    } catch (std::exception &e) {
        usage(desc);
        exit(EXIT_FAILURE);
    }

    if (vm.count("help")) {
        usage(desc);
        exit(EXIT_SUCCESS);
    }

    try {
        run(server_addr, server_port, gui_addr, gui_port, player_name, port);
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        exit(EXIT_FAILURE);
    }
}
