#ifndef ROBOTS_SERVER_CLIENT_HANDLER_H
#define ROBOTS_SERVER_CLIENT_HANDLER_H

#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include "types.h"
#include "events.h"
#include "blocking-queue.h"
#include "messages.h"
#include "server.h"

using std::queue;

/**
 * To jest nadawca wiadomości do klienta.
 *
 * W nieskończonej pętli wysyła klientowi wszystko,
 * co zostanie mu przekazane poprzez kolejkę blokującą.
 */
class MessageSender {
public:
    MessageSender(std::shared_ptr<TcpConnection> connection,
                  std::shared_ptr<server_mess_queue_t> init_messages)
            : tcp(std::move(connection)),
              messages(std::move(init_messages)) {}

    void run() {
        try {
            for (;;) {
                auto m = messages->pop();
                writeServerMessage(*tcp, m);
                tcp->send();
            }
        } catch (std::exception &e) {
            tcp->close();
            messages->close();
        }
    }

private:
    std::shared_ptr<TcpConnection> tcp;
    std::shared_ptr<BlockingQueue<std::shared_ptr<server_mess_t>>> messages;
};

/**
 * To jest odbiorca wiadomości od klienta.
 *
 * W nieskończonej pętli odbiera wiadomości i
 * odpowiednio je obsługuje.
 */
class MessageReceiver {
public:
    MessageReceiver(std::shared_ptr<TcpConnection> connection,
                    std::shared_ptr<Server> server_state,
                    client_id_t client_id)
            : connection(std::move(connection)),
              server_state(std::move(server_state)),
              id(client_id) {}

    void run() {
        try {
            for (;;) {
                handleClientMessage();
            }
        } catch (std::exception &e) {
            connection->close();
            server_state->eraseClient(id);
        }
    }

private:
    std::shared_ptr<TcpConnection> connection;
    std::shared_ptr<Server> server_state;
    const client_id_t id;

    void handle(const Join &message) {
        server_state->tryAcceptPlayer(id, message.name, connection->getRemoteAddress());
    }

    void handle(const PlaceBomb &message) {
        server_state->setLastMessage(id, client_mess_t{message});
    }

    void handle(const PlaceBlock &message) {
        server_state->setLastMessage(id, client_mess_t{message});
    }

    void handle(const Move &message) {
        server_state->setLastMessage(id, client_mess_t{message});
    }

    /*
     * Odbiera i obsługuje wiadomość od klienta.
     *
     * Operacja blokująca.
     */
    void handleClientMessage() {
        auto t = readClientMessageType(*connection);
        switch (t) {
            case CLIENT_JOIN:
                handle(Join{t, connection->readString()});
                break;
            case CLIENT_PLACE_BOMB:
                handle(PlaceBomb{t});
                break;
            case CLIENT_PLACE_BLOCK:
                handle(PlaceBlock{t});
                break;
            case CLIENT_MOVE:
                handle(Move{t, readDirection(*connection)});
                break;
            default:
                throw std::invalid_argument("Client message type not recognised!");
        }
    }
};

#endif //ROBOTS_SERVER_CLIENT_HANDLER_H
