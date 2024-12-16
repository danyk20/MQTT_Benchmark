#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <mqtt/async_client.h>
#include <vector>
#include <chrono>
#include <thread>

std::map<std::string, std::string> s_arguments = {
    {"debug", "False"}, // debug print
    {"separator", "True"}, // number of different message payloads sizes (except separator)
    {"output", "producer_results.txt"},
    {"topic", "test"}, // subscribed topic
    {"client_id", ""},
    {"protocol", "MQTT"} //
};

std::map<std::string, long> l_arguments = {
    {"period", 80}, // min delay between published messages
    {"messages", 400},
    {"buffer", 100}, // max number of messages in the buffer
    {"repetitions", 1},
    {"timeout", 3}, // wait timeout in ms per message per 1KB payload
    {"qos", 1},
    {"min", 72}, // minimum payload size in KB
    {"max", 72}, // maximum payload size in KB
    {"percentage", 50},
    {"consumers", 1}
};

long get_timeout(const size_t payload) {
    /**
     * @ payload size in B
     * Returns timeout which is 1s or more based on Buffer size and payload size
     */
    long timeout = l_arguments["timeout"] * l_arguments["buffer"] * static_cast<long>(payload / 1024);
    if (timeout < 5000) {
        return 5000;
    }
    return timeout;
}

void publish_separator(mqtt::async_client *client, const bool disconnect = false) {
    /**
     * Send empty paylod as a separator for time measurment on the consumer and disconnect from the broker
     *
     * @client - connection object
     */
    const std::string empty_message;
    const auto msg = mqtt::make_message(s_arguments["topic"], empty_message);
    msg->set_qos(1);
    if (!client->publish(msg)->wait_for(get_timeout(0))) {
        std::cerr << "publishing of separator failed" << std::endl;
    }
    if (disconnect) {
        client->disconnect()->wait_for(get_timeout(0));
    }
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
    int number_of_messages = static_cast<int>(l_arguments["messages"]);
    auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time) / 1000.0;
    const auto message_per_seconds = number_of_messages / duration.count();
    const auto throughput = message_per_seconds * static_cast<double>(payload_size);
    const std::string measurement = "[" + std::to_string(number_of_messages) + "," + std::to_string(payload_size) + ","
                                    + std::to_string(static_cast<int>(throughput)) + "," +
                                    std::to_string(static_cast<int>(message_per_seconds)) + "]";
    if (s_arguments["debug"] == "True") {
        std::cout << measurement << " - " << duration.count() << "ms" << std::endl;
    }


    if (s_arguments["debug"] == "True") {
        std::cout << "Sent " << number_of_messages << " messages of size " << payload_size
                << " bytes to topic '" << s_arguments["topic"] << "' in " << duration.count() << "ns" << std::endl;
    }

    return "[" + std::to_string(number_of_messages) + "," + std::to_string(payload_size) + "," +
           std::to_string(throughput) + "," + std::to_string(message_per_seconds) + "]";
}

void wait_for_buffer_dump(const std::vector<std::shared_ptr<mqtt::token> > &tokens, size_t &last_published,
                          int percentage, size_t payload_size) {
    unsigned long long available_buffer = 100ull / percentage;
    last_published += std::max(l_arguments["buffer"] / available_buffer, 1ull);
    if (last_published >= tokens.size()) {
        last_published = tokens.size() - 1;
    }
    if (!tokens[last_published]->wait_for(get_timeout(payload_size))) {
        std::cout << get_timeout(payload_size) << "ms timeout waiting for message " << last_published << std::endl;
    }
}

std::string publishMQTT(const std::string &message, int qos) {
    /**
     * Send messages asynchronously and measure that time. After sending all messages send one empty payload and close
     * the connection.
     *
     * @message - payload to publish
     *
     * Returns measurement as string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     */
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));

    mqtt::async_client client(brokerAddress + ":" + std::to_string(brokerPort), s_arguments["user_id"],
                              static_cast<int>(l_arguments["buffer"]));

    auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .finalize();
    try {
        if (!client.connect(connOpts)->wait_for(get_timeout(0))) {
            std::cerr << "connect failed - timeout" << std::endl;
        }

        // first non measured message
        publish_separator(&client);

        // Pre-create the message to minimize allocation overhead
        auto mqtt_message = mqtt::make_message(s_arguments["topic"], message);
        mqtt_message->set_qos(qos);

        // Publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        std::vector<std::shared_ptr<mqtt::token> > tokens;
        tokens.reserve(l_arguments["messages"]);
        size_t last_published = 0;
        auto start_time = std::chrono::steady_clock::now();
        auto next_run_time = std::chrono::steady_clock::now();

        for (auto i = 0; i < l_arguments["messages"]; ++i) {
            next_run_time += std::chrono::milliseconds(l_arguments["period"]);
            tokens.push_back(client.publish(mqtt_message));
            //std::this_thread::sleep_for(std::chrono::milliseconds());
            if (tokens.size() - last_published >= l_arguments["buffer"]) {
                // message buffer is full
                if (s_arguments["debug"] == "True") {
                    std::cout << last_published << " - published" << std::endl;
                }
                wait_for_buffer_dump(tokens, last_published, static_cast<int>(l_arguments["percentage"]),
                                     message.size());
            }
            std::this_thread::sleep_until(next_run_time);
        }
        // Wait for all publish tokens to complete
        if (!tokens.back()->wait_for(get_timeout(message.size()))) {
            std::cout << get_timeout(message.size()) << " timeout waiting for message " << last_published << std::endl;
        }

        auto payload_size = message.size();
        std::string measurement = process_measurement(start_time, payload_size);

        publish_separator(&client, true);
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


std::vector<std::string> generate_messages(long min_size_in_kb, long max_size_in_kb) {
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
    std::string path = "data/" + std::to_string(l_arguments["qos"]) + "/" + std::to_string(l_arguments["consumers"]) +
                       s_arguments["output"];
    create_directories(std::filesystem::path(path).parent_path());
    std::ofstream outfile(path, std::ios_base::app);
    if (outfile.is_open()) {
        outfile << data << std::endl;
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

void print_flags() {
    /**
     * Print possible arguments and their usecases
     */
    std::cout << "Supported arguments flags:" << std::endl;
    for (const auto &argument: s_arguments) {
        if (argument.first == "debug") {
            std::cout << "  --" << argument.first << std::endl;
        } else {
            std::cout << "  --" << argument.first << " <value>" << std::endl;
        }
    }
    for (const auto &argument: l_arguments) {
        if (argument.first == "debug") {
            std::cout << "  --" << argument.first << std::endl;
        } else {
            std::cout << "  --" << argument.first << " <value>" << std::endl;
        }
    }
}

bool set_parameters(int argc, char *argv[]) {
    /**
     * Set all parameter from command line arguments and return True unlless bad argument or 'help' was provided
     */
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            s_arguments["output_file"] = argv[++i];
        } else if ((arg == "-t" || arg == "--topic") && i + 1 < argc) {
            s_arguments["topic"] = argv[++i];
        } else if ((arg == "-c" || arg == "--client_id") && i + 1 < argc) {
            s_arguments["client_id"] = argv[++i];
        } else if (arg == "--debug" || arg == "-d") {
            s_arguments["debug"] = "True";
        } else if ((arg == "-s" || arg == "--separator") && i + 1 < argc) {
            s_arguments["separator"] = argv[++i];
        } else if ((arg == "-p" || arg == "--protocol") && i + 1 < argc) {
            s_arguments["protocol"] = argv[++i];
        } else if ((arg == "--period") && i + 1 < argc) {
            l_arguments["period"] = std::stol(argv[++i]);
        } else if ((arg == "-m" || arg == "--messages") && i + 1 < argc) {
            l_arguments["messages"] = std::stol(argv[++i]);
        } else if ((arg == "-b" || arg == "--buffer") && i + 1 < argc) {
            l_arguments["buffer"] = std::stol(argv[++i]);
        } else if ((arg == "-r" || arg == "--repetitions") && i + 1 < argc) {
            l_arguments["repetitions"] = std::stol(argv[++i]);
        } else if ((arg == "-q" || arg == "--qos") && i + 1 < argc) {
            l_arguments["qos"] = std::stol(argv[++i]);
        } else if ((arg == "--timeout") && i + 1 < argc) {
            l_arguments["timeout"] = std::stol(argv[++i]);
        } else if ((arg == "--min") && i + 1 < argc) {
            l_arguments["min"] = std::stol(argv[++i]);
        } else if ((arg == "--max") && i + 1 < argc) {
            l_arguments["max"] = std::stol(argv[++i]);
        } else if ((arg == "--percentage") && i + 1 < argc) {
            l_arguments["percentage"] = std::stol(argv[++i]);
        } else if ((arg == "--consumers") && i + 1 < argc) {
            l_arguments["consumers"] = std::stol(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_flags();
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_flags();
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (!set_parameters(argc, argv)) {
        return 1;
    }
    const std::vector<std::string> messages = generate_messages(l_arguments["min"], l_arguments["max"]);
    std::vector<std::string> measurements;
    measurements.reserve(messages.size());
    for (int i = 0; i < l_arguments["repetitions"]; ++i) {
        for (const auto &message: messages) {
            measurements.emplace_back(publish(s_arguments["protocol"], message, static_cast<int>(l_arguments["qos"])));
        }
        store_string(format_output(measurements));
    }
    return 0;
}
