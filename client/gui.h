#ifndef SIK_2_GUI_H
#define SIK_2_GUI_H

#include "types.h"
#include "udp-socket.h"

#define netstruct struct __attribute((packed))

/**
 * Reaguje na komunikaty od GUI i
 * w odpowiedzi przesyła odpowiednie wiadomości do serwera.
 */
class GuiHandler {
    const uint8_t DIRECTION_MAX = 3;

    enum InputMessage {
        GUI_PLACE_BOMB, GUI_PLACE_BLOCK, GUI_MOVE
    };

    enum ClientMessage {
        CLIENT_JOIN, CLIENT_PLACE_BOMB, CLIENT_PLACE_BLOCK, CLIENT_MOVE
    };

    netstruct GUIMessage {
        uint8_t type;
    };

    netstruct PlaceBomb {
        uint8_t type;
    };

    netstruct PlaceBlock {
        uint8_t type;
    };

    netstruct Move {
        uint8_t type;
        uint8_t direction;
    };

public:
    GuiHandler(UdpSocket &gui, TcpConnection &server, ClientState &state)
            : gui(gui), server(server), state(state) {}

    [[noreturn]] void run() {
        for (;;) {
            handleMessage();
        }
    }

private:
    UdpSocket &gui;
    TcpConnection &server;
    ClientState &state;

    void handleJoin() {
        server.write((uint8_t) CLIENT_JOIN);
        server.write(state.player_name);
    }

    void handlePlaceBomb() {
        if (state.is_lobby) {
            handleJoin();
            return;
        }
        server.write((uint8_t) CLIENT_PLACE_BOMB);
    }

    void handlePlaceBlock() {
        if (state.is_lobby) {
            handleJoin();
            return;
        }
        server.write((uint8_t) CLIENT_PLACE_BLOCK);
    }

    void handleMove(uint8_t direction) {
        if (direction > DIRECTION_MAX) {
            return;
        }

        if (state.is_lobby) {
            handleJoin();
            return;
        }

        server.write((uint8_t) CLIENT_MOVE);
        server.write(direction);
    }

    void handleMessage() {
        size_t len = gui.receive();
        std::scoped_lock lock(state.mutex);
        server.clearOutput();
        auto m = (GUIMessage *) gui.input_buffer.data();
        if (len == sizeof(PlaceBomb) && m->type == GUI_PLACE_BOMB) {
            handlePlaceBomb();
        } else if (len == sizeof(PlaceBlock) && m->type == GUI_PLACE_BLOCK) {
            handlePlaceBlock();
        } else if (len == sizeof(Move) && m->type == GUI_MOVE) {
            handleMove(((Move *) m)->direction);
        } else {
            // Ignoruj niepoprawną wiadomość.
            return;
        }
        server.send();
    }

};

#endif //SIK_2_GUI_H
