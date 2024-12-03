#include <fstream>
#include <mqtt/client.h>
#include <ostream>

constexpr auto RESULTS_FILE = "consumer_results.txt";
constexpr auto FILE_OPENNING = "[";
constexpr auto FILE_CLOSSING = "]";

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

[[noreturn]] int main() {
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));
    std::string broker = brokerAddress + ":" + std::to_string(brokerPort);
    std::string id = "";
    mqtt::client client(broker, id, mqtt::create_options(MQTTVERSION_5));
    client.connect();
    client.subscribe("test");
    client.start_consuming();
    auto starttime = std::chrono::steady_clock::now();
    auto endtime = std::chrono::steady_clock::now();
    long long received_messages = 0;
    size_t current_size = 0;
    std::vector<std::string> measurements;
    bool separation = false;
    while (true) {
        mqtt::const_message_ptr messagePointer;

        if (client.try_consume_message(&messagePointer)) {
            std::string messageString = messagePointer->get_payload_str();
            if (messageString.empty()) {
                separation = true;
            } else {
                current_size = messageString.size();
                separation = false;
                received_messages++;
            }
            //std::cout << messageString.size() << std::endl;
        }
        if (received_messages == 1 && !separation) {
            starttime = std::chrono::steady_clock::now();
        } else if (separation && received_messages > 0) {
            endtime = std::chrono::steady_clock::now();
            auto duration = endtime - starttime;
            auto throughput = 1000000000 * received_messages * current_size / duration.count();
            auto message_per_seconds = 1000000000 * received_messages / duration.count();
            std::string measurement = "[" + std::to_string(received_messages) + "," + std::to_string(current_size) +
                                      "," + std::to_string(throughput) + "," + std::to_string(message_per_seconds)
                                      + "]";
            measurements.push_back(measurement);
            received_messages = 0;
        }
        if (measurements.size() == 15) {
            store_string(format_output(measurements));
            measurements.clear();
        }
    }
}
