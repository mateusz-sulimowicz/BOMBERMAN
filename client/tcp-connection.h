#ifndef SIK_2_TCPCONNECTION_H
#define SIK_2_TCPCONNECTION_H

#include <string>
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

namespace asio = boost::asio;

/**
 * Reprezentuje połączenie TCP z serwerem.
 */
class TcpConnection {
    using tcp = asio::ip::tcp;
    using resolver = tcp::resolver;
    using socket_t = tcp::socket;
public:
    TcpConnection(asio::io_context &io_context,
                  const std::string &address, const std::string &port)
            : socket(io_context) {

        resolver resolver(io_context);
        auto endpoints = resolver.resolve(address, port,
                                          resolver::resolver_base::numeric_service);
        asio::connect(socket, endpoints);
        socket.set_option(tcp::no_delay{true});
    }

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

    void write(uint8_t val) {
        if (output_size == output_buffer.size()) {
            send();
            output_size = 0;
        }

        memcpy(output_buffer.data() + output_size, (uint8_t *) &val, sizeof(val));
        ++output_size;
    }

    void write(string &s) {
        write((uint8_t) s.length());
        for (char c : s) {
            write((uint8_t) c);
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

#endif //SIK_2_TCPCONNECTION_H
