#ifndef ROBOTS_SERVER_TCPCONNECTION_H
#define ROBOTS_SERVER_TCPCONNECTION_H

#include <string>
#include <sstream>
#include <vector>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/format.hpp>

using std::vector;
using std::map;
using std::string;

const size_t BUFFER_SIZE = 10000;

class TcpConnection;

template<typename T>
concept Readable = requires(T, TcpConnection &c) {
    { T::read(c) } -> std::convertible_to<T>;
};

template<typename T>
concept Writable = requires(T t, TcpConnection &s) {
    t.write(s);
};

namespace asio = boost::asio;

/**
 * Reprezentuje połączenie TCP z klientem.
 */
class TcpConnection {
    using tcp = asio::ip::tcp;
    using resolver = tcp::resolver;
    using socket_t = tcp::socket;
public:
    explicit TcpConnection(socket_t socket)
            : socket(std::move(socket)) {}

    // --- Czytanie przysyłanych danych ---

    uint8_t readU8() {
        if (input_beg == input_end) {
            receive();
        }
        return input_buffer[input_beg++];
    }

    uint16_t readU16() {
        uint16_t res;
        ((uint8_t *) &res)[0] = readU8();
        ((uint8_t *) &res)[1] = readU8();
        return be16toh(res);
    }

    uint32_t readU32() {
        uint32_t res;
        ((uint8_t *) &res)[0] = readU8();
        ((uint8_t *) &res)[1] = readU8();
        ((uint8_t *) &res)[2] = readU8();
        ((uint8_t *) &res)[3] = readU8();
        return be32toh(res);
    }

    string readString() {
        uint8_t len = readU8();
        string res;
        for (uint8_t i = 0; i < len; ++i) {
            res += (char) readU8();
        }
        return res;
    }

    template<Readable T>
    vector<T> readList() {
        uint32_t len = readU32();
        vector<T> res;
        for (uint32_t i = 0; i < len; ++i) {
            res.push_back(T::read(*this));
        }
        return res;
    }

    template<Readable K, Readable V>
    map<K, V> readMap() {
        uint32_t len = readU32();
        map<K, V> res;
        for (uint32_t i = 0; i < len; ++i) {
            K key = K::read(*this);
            V value = V::read(*this);
            res[key] = value;
        }
        return res;
    }

    // --- Zapis danych do bufora `output_buffer` ---

    /**
     * Zapisuje do bufora wyjściowego jeden bajt danych.
     * Jeśli w buforze brakuje miejsca, to jego zawartość zostaje
     * wysłana i dopiero wtedy są zapisywane nowe dane.
     */
    void write(uint8_t val) {
        if (output_size == output_buffer.size()) {
            send();
            output_size = 0;
        }

        memcpy(output_buffer.data() + output_size, (uint8_t *) &val, sizeof(val));
        ++output_size;
    }

    void write(uint16_t val) {
        val = htobe16(val);
        for (size_t i = 0; i < sizeof(val); ++i) {
            write(((uint8_t *) &val)[i]);
        }
    }

    void write(uint32_t val) {
        val = htobe32(val);
        for (size_t i = 0; i < sizeof(val); ++i) {
            write(((uint8_t *) &val)[i]);
        }
    }

    void write(uint64_t val) {
        val = htobe64(val);
        for (size_t i = 0; i < sizeof(val); ++i) {
            write(((uint8_t *) &val)[i]);
        }
    }

    void write(const string &s) {
        write((uint8_t) s.length());
        for (char c: s) {
            write((uint8_t) c);
        }
    }

    template<Writable T>
    void writeList(const std::vector<T> &v) {
        write((uint32_t) v.size());
        for (const T &t: v) {
            t.write(*this);
        }
    }

    template<Writable K, Writable V>
    void writeMap(const std::map<K, V> m) {
        write((uint32_t) m.size());
        for (const auto &[k, v]: m) {
            k.write(*this);
            v.write(*this);
        }
    }

    /**
     * Wysyła `output_size` bajtów
     * danych zapisanych w buforze `output_buffer`.
     */
    void send() {
        if (output_size == 0) {
            return;
        }

        boost::system::error_code error;
        asio::write(socket, asio::buffer(output_buffer, output_size),
                    asio::transfer_all(), error);

        if (error == boost::asio::error::eof) {
            throw std::runtime_error("Server connection closed");
        } else if (error) {
            throw std::runtime_error{
                    "Failed to send message to server. Error: " +
                    std::to_string(error.value())};
        }
        output_size = 0;
    }

    void clearOutput() {
        output_size = 0;
    }

    void close() {
        try {
            socket.shutdown(socket.shutdown_both);
        } catch (std::exception &e) {
            // Ignoruj.
        }

    }

    string getRemoteAddress() {
        auto remote_ep = socket.remote_endpoint();
        std::stringstream ss;
        ss << remote_ep;
        return ss.str();
    }

private:
    socket_t socket;
    std::array<uint8_t, BUFFER_SIZE> input_buffer{};
    std::array<uint8_t, BUFFER_SIZE> output_buffer{};
    size_t input_beg = 0;
    size_t input_end = 0;
    size_t output_size = 0;

    /**
     * Odbiera porcję danych i przechowuje ją w buforze `input_buffer`
     * na pozycjach [0, długość odebranej porcji danych).
     */
    void receive() {
        assert(input_beg == input_end);
        boost::system::error_code error;
        size_t len = socket.read_some(asio::buffer(input_buffer), error);

        if (error == boost::asio::error::eof) {
            throw std::runtime_error("Server connection closed");
        } else if (error) {
            throw std::runtime_error{
                    "Failed to receive message from server. Error: " +
                    std::to_string(error.value())};
        }

        input_beg = 0;
        input_end = len;
    }
};

#endif //ROBOTS_SERVER_TCPCONNECTION_H
