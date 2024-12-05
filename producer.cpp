#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <mqtt/async_client.h>
#include <vector>
#include <chrono>

constexpr long long NUMBER_OF_MESSAGES = 1000;
constexpr long long BUFFER = 100;
constexpr long long TIMEOUT_MULTIPLIER = 3;
constexpr long long NUMBER_OF_REPETITIONS = 1;
constexpr auto RESULTS_FILE = "producer_results.txt";
constexpr auto TOPIC = "test";
constexpr char USER_ID[] = "bril";

void publish_separator(mqtt::async_client *client) {
    /**
     * Send empty paylod as a separator for time measurment on the consumer and disconnect from the broker
     *
     * @client - connection object
     */
    const std::string empty_message;
    const auto msg = mqtt::make_message(TOPIC, empty_message);
    if (!client->publish(msg)->wait_for(1000)) {
        std::cerr << "publishing of separator failed" << std::endl;
    }
    client->disconnect()->wait_for(1000);
}

std::string process_measurement(std::chrono::steady_clock::time_point start_time,
                                std::vector<std::shared_ptr<mqtt::message> >::size_type payload_size) {
    /**
     * Calculate all attributes from the measuremnt and return them in a string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     *
     * @start_time - when start sending of the first payload
     * @payload_size - how big was each payload
     */
    auto end_time = std::chrono::steady_clock::now();
    auto duration = end_time - start_time;
    auto throughput = 1000000000 * NUMBER_OF_MESSAGES * payload_size / duration.count();
    auto message_per_seconds = 1000000000 * NUMBER_OF_MESSAGES / duration.count();

    std::cout << "Sent " << NUMBER_OF_MESSAGES << " messages of size " << payload_size
            << " bytes to topic '" << TOPIC << "' in " << duration.count() << "ns" << std::endl;

    return "[" + std::to_string(NUMBER_OF_MESSAGES) + "," + std::to_string(payload_size) + "," +
           std::to_string(throughput) + "," + std::to_string(message_per_seconds) + "]";
}

std::string publishMQTT(const std::string &message, int qos) {
    /**
     * Send messages asynchronously and measure that time. After sending all messages send one empty payloud and close
     * the connection.
     *
     * @message - payload to publish
     *
     * Returns measurement as string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     */
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));

    mqtt::async_client client(brokerAddress + ":" + std::to_string(brokerPort), USER_ID, BUFFER);


    auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .finalize();
    try {
        if (!client.connect(connOpts)->wait_for(10000)) {
            std::cerr << "connect failed - timeout" << std::endl;
        }

        // Pre-create the message to minimize allocation overhead
        auto mqtt_message = mqtt::make_message(TOPIC, message);
        mqtt_message->set_qos(qos);

        // Publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        std::vector<std::shared_ptr<mqtt::token> > tokens;
        tokens.reserve(NUMBER_OF_MESSAGES);
        auto start_time = std::chrono::steady_clock::now();
        size_t last_published = 0;
        for (auto i = 0; i < NUMBER_OF_MESSAGES; ++i) {
            tokens.push_back(client.publish(mqtt_message));
            if (tokens.size() - last_published >= BUFFER) {
                // message buffer is full
                last_published += std::max(BUFFER / 2ull, 1ull);
                if (last_published >= tokens.size()) {
                    last_published = tokens.size() - 1;
                }
                if (!tokens[last_published]->wait_for(1000 * TIMEOUT_MULTIPLIER * BUFFER)) {
                    std::cout << "Timeout waiting for message " << last_published << std::endl;
                }
            }
        }

        // Wait for all publish tokens to complete
        if (!tokens.back()->wait_for(1000 * TIMEOUT_MULTIPLIER * BUFFER)) {
            std::cout << "Timeout waiting for message " << last_published << std::endl;
        }

        auto payload_size = message.size();
        std::string measurement = process_measurement(start_time, payload_size);

        publish_separator(&client);
        return measurement;
    } catch (const mqtt::exception &e) {
        std::cerr << "Failed to publish MQTT messages: " << e.what() << std::endl;
    }
    return "[0,0,0,0]";
}

std::string publish(const std::string &protocol, const std::string &message, int qos) {
    /**
    * Send messages based on given protocol.
    *
    * @protocol - currently only "mqtt" supported
    * @message - payload to publish
    *
    * Returns measurement as string of following format:
    * [number_of_messages,single-message_size,B/s,number_of_message/s]
    */
    if (protocol == "MQTT" || protocol == "mqtt") {
        return publishMQTT(message, qos);
    }
    std::cerr << "Unsupported protocol: " << protocol << std::endl;
    return "";
}


std::vector<std::string> generate_messages(int min_size_in_kb, int max_size_in_kb) {
    /**
     * Generate message of specific length from min_size_in_kb to max_size_in_kb. Each new message is twice as big as
     * previos.
     *
     * @min_size_in_kb - start of the range (included in the range)
     * @max_size_in_kb - end of the range (included or bigger element in the range)
     *
     * Return vector of strings with given size (geometric series) - [1,2,4,8,16...]
     */
    std::vector<std::string> result;

    for (size_t memorySize = min_size_in_kb * 1024; memorySize <= max_size_in_kb * 1024; memorySize *= 2) {
        result.emplace_back(memorySize, '0');
    }

    return result;
}

void store_string(const std::string &data) {
    /**
     * Append string into the file
     *
     * @data - string to store
     */
std:
    std::ofstream outfile(RESULTS_FILE, std::ios_base::app);
    if (outfile.is_open()) {
        outfile << data;
        outfile.close();
    } else {
        std::cerr << "Error opening file." << std::endl;
    }
}

std::string format_output(const std::vector<std::string> &strings) {
    /**
     * Format elements in the vector into following string [<element>,<element>,...,<element>]
     *
     * @strings - vector of string elements to be formatted
     */
    std::string result = "[";

    for (size_t i = 0; i < strings.size(); ++i) {
        result += strings[i];
        if (i != strings.size() - 1) {
            result += ",";
        }
    }

    result += "]";
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <protocol> <min_size_kb> <max_size_kb> <QoS>" << std::endl;
        return 1;
    }

    const std::string protocol = argv[1];
    const std::vector<std::string> messages = generate_messages(std::stoi(argv[2]), std::stoi(argv[3]));
    const int qos = std::stoi(argv[4]);

    std::vector<std::string> measurements;
    measurements.reserve(messages.size());
    for (int i = 0; i < NUMBER_OF_REPETITIONS; ++i) {
        for (const auto &message: messages) {
            measurements.emplace_back(publish(protocol, message, qos));
        }
        store_string(format_output(measurements));
    }
    return 0;
}
