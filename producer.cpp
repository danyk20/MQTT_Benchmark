#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <mqtt/async_client.h>

constexpr int NUMBER_OF_MESSAGES = 100;
constexpr std::string RESULTS_FILE = "results.txt";
constexpr std::string FILE_OPENNING = "[";
constexpr std::string FILE_CLOSSING = "]";


std::string publishMQTT(const std::string &message, const std::string &topic = "test");

std::string publish(const std::string &protocol, const std::string &message) {
    if (protocol == "MQTT" || protocol == "mqtt") {
        return publishMQTT(message);
    } else {
        std::cerr << "Unsupported protocol: " << protocol << std::endl;
    }
    return "";
}

std::string publishMQTT(const std::string &message, const std::string &topic) {
    // [nmessages,single-messagesize, bit/s, nmessage/s]
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));

    mqtt::async_client client(brokerAddress + ":" + std::to_string(brokerPort), "test");

    auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .finalize();
    std::string measurement;
    try {
        // Connect to broker
        client.connect(connOpts)->wait();

        auto starttime = std::chrono::steady_clock::now();

        // Publish all messages
        for (int i = 0; i < NUMBER_OF_MESSAGES; ++i) {
            auto msg = mqtt::make_message(topic, message);
            client.publish(msg)->wait();
        }

        auto endtime = std::chrono::steady_clock::now();
        auto duration = endtime - starttime;
        auto message_per_seconds = NUMBER_OF_MESSAGES / (duration.count() / 1000000000.0);

        std::cout << "Sent " << NUMBER_OF_MESSAGES << " times `" << message.size() << "` bytes to topic `" << topic <<
                "` in " << duration << std::endl;

        measurement = "[" + std::to_string(NUMBER_OF_MESSAGES) + "," + std::to_string(message.size()) + "," +
                      std::to_string(duration.count()) + "," + std::to_string(message_per_seconds) + "]";

        // Disconnect client
        client.disconnect()->wait();
    } catch (const mqtt::exception &e) {
        std::cerr << "Failed to publish MQTT message: " << e.what() << std::endl;
    }

    return measurement;
}


std::vector<std::string> generate_messages(int min_size_in_kb, int max_size_in_kb) {
    std::vector<std::string> result;

    for (size_t memorySize = min_size_in_kb * 1024; memorySize <= max_size_in_kb * 1024; memorySize *= 2) {
        char *buffer = new char[memorySize];

        std::fill_n(buffer, memorySize, '0');
        std::string message(buffer, memorySize);
        delete[] buffer;

        result.emplace_back(message);
    }

    return result;
}

void store_string(const std::string &data) {
    std::ofstream outfile(RESULTS_FILE, std::ios_base::app);

    if (outfile.is_open()) {
        outfile << data;
        outfile.close();
    } else {
        std::cerr << "Error opening file." << std::endl;
    }
}

std::string format_output(const std::vector<std::string> &strings) {
    // [<measurement>,<measurement>,...,<measurement>]
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
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <protocol> <min_size> <max_size>" << std::endl;
        return 1;
    }

    const std::string protocol = argv[1];
    const std::vector<std::string> messages = generate_messages(atoi(argv[2]), atoi(argv[3]));


    std::vector<std::string> measurements;
    measurements.reserve(messages.size());
    for (const std::string &message: messages) {
        measurements.emplace_back(publish(protocol, message));
    }
    store_string(format_output(measurements));

    return 0;
}
