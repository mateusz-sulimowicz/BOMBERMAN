#ifndef SIK_2_UDPSOCKET_H
#define SIK_2_UDPSOCKET_H

#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/array.hpp>

const size_t DATAGRAM_MAX_SIZE = 65507;

using std::vector;
using std::map;

using udp_buffer = std::array<uint8_t, DATAGRAM_MAX_SIZE>;

class UdpSocket;

template<typename T>
concept Writable = requires(T t, UdpSocket &s) {
    t.write(s);
};

/**
 * Reprezentuje gniazdo UDP
 * do komunikacji z interfejsem użytkownika.
 */
class UdpSocket {
    using udp = boost::asio::ip::udp;
    using resolver = udp::resolver;
public:
    udp_buffer input_buffer{};
    udp_buffer output_buffer{};

    UdpSocket(boost::asio::io_context &io_context,
              const std::string &address, const std::string &port,
              const uint16_t my_port)
            : socket(io_context, udp::endpoint(udp::v6(), my_port)) {
        resolver resolver(io_context);
        endpoint = *resolver.resolve(address, port,
                                     resolver::resolver_base::numeric_service);
    }

    // --- Zapis danych do bufora wyjściowego ---

    void write(uint8_t val) {
        copyToBuffer(&val, sizeof(val));
    }

    void write(uint16_t val) {
        val = htobe16(val);
        copyToBuffer((uint8_t *) &val, sizeof(val));
    }

    void write(uint32_t val) {
        val = htobe32(val);
        copyToBuffer((uint8_t *) &val, sizeof(val));
    }

    void write(const string &s) {
        write((uint8_t) s.length());
        for (char c : s) {
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
     * Zeruje rozmiar bufora wyjściowego.
     */
    void clearOutput() {
        output_size = 0;
    }

    /**
     * Przesyła przez UDP zawartość bufora `output_buffer`.
     */
    void send() {
        if (output_size == 0) {
            return;
        }
        if (output_size > DATAGRAM_MAX_SIZE) {
            throw std::runtime_error("UDP: Message too big!");
        }

        boost::system::error_code error;
        socket.send_to(boost::asio::buffer(output_buffer, output_size),
                       endpoint, 0, error);
        if (error) {
            throw std::runtime_error{
                    "Failed to send message to GUI. Error: " +
                    std::to_string(error.value())};
        }
    }

    /**
     * Czyta wiadomość i zapisuje ją w buforze `input_buffer`.
     * Zwraca długość odebranej wiadomości w bajtach.
     */
    size_t receive() {
        boost::system::error_code error;
        size_t len = socket.receive(boost::asio::buffer(input_buffer), 0,
                                    error);
        if (error) {
            throw std::runtime_error{
                    "Failed to receive message from GUI. Error: " +
                    std::to_string(error.value())};
        }
        return len;
    }

private:
    udp::socket socket;
    udp::endpoint endpoint;
    size_t output_size = 0;

    /**
     * Kopiuje ciąg bajtów długości `len` wskazywany przez `arr`
     * do bufora `output_buffer`.
     */
    void copyToBuffer(const uint8_t *arr, size_t len) {
        assert(output_size + len <= output_buffer.size());
        memcpy(output_buffer.data() + output_size, arr, len);
        output_size += len;
    }
};

#endif //SIK_2_UDPSOCKET_H
