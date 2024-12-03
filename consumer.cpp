#include <fstream>
#include <mqtt/client.h>
#include <ostream>

constexpr auto RESULTS_FILE = "consumer_results.txt";
constexpr int NUMBER_OF_MEASUREMENTS = 14; // number of different message payloads sizes (except separator)
constexpr auto TOPIC = "test";
constexpr auto CLIENT_ID = "";

void store_string(const std::string &data) {
    /**
     * Save string into the file
     *
     * @data string to store
     */
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

void add_measurement(std::chrono::steady_clock::time_point starttime, long long received_messages, size_t current_size,
                     std::vector<std::string> measurements) {
    /*
     * Add measurement as string into given vector of measurements. Format of single measurement is following :
     * [number_of_messages,size_of_the_message,B/s,number_of_messages/s]
     */
    std::chrono::steady_clock::time_point endtime = std::chrono::steady_clock::now();
    auto duration = endtime - starttime;
    auto throughput = 1000000000 * received_messages * current_size / duration.count();
    auto message_per_seconds = 1000000000 * received_messages / duration.count();
    std::string measurement = "[" + std::to_string(received_messages) + "," + std::to_string(current_size) +
                              "," + std::to_string(throughput) + "," + std::to_string(message_per_seconds)
                              + "]";
    measurements.push_back(measurement);
}

std::unique_ptr<mqtt::client> prepare_consumer() {
    /**
     * Create connection to the broker and subsribe to the topic based on global constants and enviromental variables
     */
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));
    std::string broker = brokerAddress + ":" + std::to_string(brokerPort);
    auto client = std::make_unique<mqtt::client>(broker, CLIENT_ID, mqtt::create_options(MQTTVERSION_5));
    client->connect();
    client->subscribe(TOPIC);
    client->start_consuming();
    return client;
}

void process_payload(long long &received_messages, size_t &current_size, bool &separation,
                     const mqtt::const_message_ptr &message_pointer) {
    /**
     * Read message size and update message counter. Empty message is considered as separator.
     */
    const std::string messageString = message_pointer->get_payload_str();
    if (messageString.empty()) {
        separation = true;
    } else {
        current_size = messageString.size();
        separation = false;
        received_messages++;
    }
}

int main() {
    const auto client = prepare_consumer().release();
    auto start_time = std::chrono::steady_clock::now();
    long long received_messages = 0;
    size_t current_size = 0;
    std::vector<std::string> measurements;
    bool separation = false;
    mqtt::const_message_ptr messagePointer;

    while (true) {
        if (client->try_consume_message(&messagePointer)) {
            // message arrived
            process_payload(received_messages, current_size, separation, messagePointer);
            if (received_messages > 0 && !separation) {
                // start timer - first measured payload arrived
                start_time = std::chrono::steady_clock::now();
            } else if (separation && received_messages > 0) {
                // stop timer - separator payload arrived
                add_measurement(start_time, received_messages, current_size, measurements);
                received_messages = 0; // reset message counter
            }
            if (measurements.size() == NUMBER_OF_MEASUREMENTS) {
                store_string(format_output(measurements)); // save all measured data into file
                measurements.clear(); // remove measured data for last batch (payloads with the same size)
                return 0;
            }
        }
    }
}
