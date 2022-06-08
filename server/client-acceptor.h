#ifndef ROBOTS_SERVER_CLIENT_ACCEPTOR_H
#define ROBOTS_SERVER_CLIENT_ACCEPTOR_H

#include <ctime>
#include <iostream>
#include <string>
#include <map>

#include <boost/asio.hpp>
#include <boost/format.hpp>

#include "tcp-connection.h"
#include "types.h"
#include "server.h"
#include "client-handler.h"

using std::map;

using boost::asio::ip::tcp;

/**
 * Akceptuje nowe połączenia klientów i
 * inicjuje ich obsługę.
 */
class ClientAcceptor {
public:
    ClientAcceptor(uint16_t port,
                   std::shared_ptr<Server> server,
                   std::shared_ptr<boost::asio::io_context> context,
                   std::shared_ptr<boost::asio::thread_pool> thread_pool)
            : acceptor(*context, tcp::endpoint(tcp::v6(), port)),
              context(std::move(context)),
              thread_pool(std::move(thread_pool)),
              server(std::move(server)) {}

    [[noreturn]] void run() {
        for (;;) {
            auto socket = tcp::socket(*context);
            acceptor.accept(socket);

            socket.set_option(tcp::no_delay{true});
            auto tcp = std::make_shared<TcpConnection>(std::move(socket));
            auto client_id = server->acceptClient();
            auto message_queue_ptr = server->createMessageQueue(client_id);

            // Wątek do wysyłki wiadomości do klienta.
            boost::asio::post(*thread_pool, [tcp, message_queue_ptr] {
                try {
                    MessageSender{tcp, message_queue_ptr}.run();
                } catch (std::exception &e) {
                    std::cerr << e.what() << "\n";
                }
            });

            // Wątek do odbioru wiadomości od klienta.
            boost::asio::post(*thread_pool, [tcp, server = this->server, client_id] {
                try {
                    MessageReceiver{tcp, server, client_id}.run();
                } catch (std::exception &e) {
                    std::cerr << e.what() << "\n";
                }
            });
        }
    }

private:
    tcp::acceptor acceptor;
    std::shared_ptr<boost::asio::io_context> context;
    std::shared_ptr<boost::asio::thread_pool> thread_pool;
    std::shared_ptr<Server> server;
};


#endif //ROBOTS_SERVER_CLIENT_ACCEPTOR_H
