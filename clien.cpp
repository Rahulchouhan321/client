#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

using boost::asio::ip::tcp;

// Struct to represent a packet
struct Packet {
    std::string symbol;
    char buy_sell_indicator;
    int32_t quantity;
    int32_t price;
    int32_t sequence_number;
};

// ABXClient class for handling server communication
class ABXClient {
public:
    ABXClient(const std::string& host, uint16_t port) 
        : io_context(), socket(io_context), endpoint(boost::asio::ip::address::from_string(host), port) {}

    void connect() {
        socket.connect(endpoint);
    }

    void send_request(const std::vector<uint8_t>& request) {
        boost::asio::write(socket, boost::asio::buffer(request));
    }

    std::vector<uint8_t> receive_response(size_t size) {
        std::vector<uint8_t> response(size);
        boost::asio::read(socket, boost::asio::buffer(response));
        return response;
    }

    void close() {
        socket.close();
    }

private:
    boost::asio::io_context io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;
};

// Create a request to stream all packets
std::vector<uint8_t> create_stream_request() {
    return {1};  // Call type 1: Stream All Packets
}

// Create a request to resend a specific packet
std::vector<uint8_t> create_resend_request(uint8_t sequence_number) {
    return {2, sequence_number};  // Call type 2: Resend Packet
}

// Parse a single packet from binary data
Packet parse_packet(const std::vector<uint8_t>& data) {
    Packet packet;
    packet.symbol = std::string(data.begin(), data.begin() + 4);
    packet.buy_sell_indicator = data[4];
    packet.quantity = (data[5] << 24) | (data[6] << 16) | (data[7] << 8) | data[8];
    packet.price = (data[9] << 24) | (data[10] << 16) | (data[11] << 8) | data[12];
    packet.sequence_number = (data[13] << 24) | (data[14] << 16) | (data[15] << 8) | data[16];
    return packet;
}

// Generate JSON output from packets
nlohmann::json generate_json_output(const std::vector<Packet>& packets) {
    nlohmann::json json_output = nlohmann::json::array();
    for (const auto& packet : packets) {
        json_output.push_back({
            {"symbol", packet.symbol},
            {"buy_sell_indicator", packet.buy_sell_indicator},
            {"quantity", packet.quantity},
            {"price", packet.price},
            {"sequence_number", packet.sequence_number}
        });
    }
    return json_output;
}

int main() {
    try {
        // Create the client and connect to the server
        ABXClient client("127.0.0.1", 3000);
        client.connect();

        // Step 1: Request to stream all packets
        std::vector<uint8_t> stream_request = create_stream_request();
        client.send_request(stream_request);

        // Step 2: Receive and parse packets
        std::vector<Packet> packets;
        std::vector<uint8_t> response = client.receive_response(1024); // Adjust size as needed
        size_t index = 0;

        while (index + 17 <= response.size()) { // Ensure we read complete packets
            std::vector<uint8_t> packet_data(response.begin() + index, response.begin() + index + 17);
            packets.push_back(parse_packet(packet_data));
            index += 17;
        }

        // Step 3: Handle missing sequences
        std::vector<int32_t> received_sequences;
        for (const auto& packet : packets) {
            received_sequences.push_back(packet.sequence_number);
        }
        std::sort(received_sequences.begin(), received_sequences.end());

        int32_t expected_sequence = 1;
        while (expected_sequence <= received_sequences.back()) {
            if (std::find(received_sequences.begin(), received_sequences.end(), expected_sequence) == received_sequences.end()) {
                // Missing sequence found; request packet
                std::vector<uint8_t> resend_request = create_resend_request(static_cast<uint8_t>(expected_sequence));
                client.send_request(resend_request);

                // Receive the missing packet and parse it
                std::vector<uint8_t> missing_response = client.receive_response(17);
                packets.push_back(parse_packet(missing_response));
            }
            expected_sequence++;
        }

        // Step 4: Generate JSON output
        nlohmann::json json_output = generate_json_output(packets);

        // Step 5: Write JSON to file
        std::ofstream json_file("output.json");
        json_file << json_output.dump(4);  // Pretty print with indentation
        json_file.close();

        // Close the connection
        client.close();

        std::cout << "JSON output saved to output.json\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
