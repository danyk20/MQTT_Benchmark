#include <fstream>
#include <mqtt/client.h>
#include <ostream>

constexpr auto CLIENT_ID = "";
std::map<std::string, std::string> arguments = {
    {"debug", "False"}, // debug print
    {"separators", "1"}, // number of different message payloads sizes (except separator)
    {"output_file", "consumer_results.txt"},
    {"topic", "test"},
    {"client_id", ""}
};

void store_string(const std::string &data) {
    /**
     * Append string into the file
     *
     * @data - string to store
     */
    std::ofstream outfile(arguments["output_file"], std::ios_base::app);

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

    for (const auto &str: strings) {
        if (&str != &strings.back()) {
            result += str + ",";
        } else {
            result += str;
        }
    }

    result += "]";
    return result;
}

void add_measurement(std::chrono::steady_clock::time_point start_time, int received_messages, size_t current_size,
                     std::vector<std::string> *measurements) {
    /*
     * Add measurement as string into given vector of measurements. Format of single measurement is following :
     * [number_of_messages,size_of_the_message,B/s,number_of_messages/s]
     *
     * @start_time - when was the first message received
     * @received_messages - how many message have been received
     * @current_size - how big is each of the messages
     * @measurements - vector of previous measurements
     */
    const std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time) / 1000.0;
    const auto message_per_seconds = received_messages / duration.count();
    const auto throughput = message_per_seconds * static_cast<double>(current_size);
    const std::string measurement = "[" + std::to_string(received_messages) + "," + std::to_string(current_size) + ","
                                    + std::to_string(static_cast<int>(throughput)) + "," +
                                    std::to_string(static_cast<int>(message_per_seconds)) + "]";
    if (arguments["debug"] == "True") {
        std::cout << measurement << " - " << duration.count() << "ms" << std::endl;
    }
    measurements->push_back(measurement);
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
    client->subscribe(arguments["topic"]);
    client->start_consuming();
    return client;
}

bool process_payload(int &received_messages, size_t &current_size,
                     const mqtt::const_message_ptr &message_pointer) {
    /**
     * Read message size and update message counter. Empty message is considered as separator.
     *
     * @received_messages - how many messages have been received
     * @current_size - how big is each of the messages
     * @separation - flag whether is it separation message (with no payload)
     * @message_pointer - pointer to the received message
     *
     * returns True only if the message is empty
     */
    const std::string messageString = message_pointer->get_payload_str();
    if (messageString.empty()) {
        return true;
    }
    current_size = messageString.size();
    received_messages++;
    return false;
}

void print_flags() {
    /**
     * Print possible arguments and their usecases
     */
    std::cout << "Supported arguments flags:" << std::endl;
    for (const auto &argument: arguments) {
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
        if ((arg == "-s" || arg == "--separators") && i + 1 < argc) {
            arguments["separators"] = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            arguments["output_file"] = argv[++i];
        } else if ((arg == "-t" || arg == "--topic") && i + 1 < argc) {
            arguments["topic"] = argv[++i];
        } else if ((arg == "-c" || arg == "--client_id") && i + 1 < argc) {
            arguments["client_id"] = argv[++i];
        } else if (arg == "--debug" || arg == "-d") {
            arguments["debug"] = "True";
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

void consume(const std::unique_ptr<mqtt::client>::pointer client) {
    /**
     * Consume all messages from subscribed topic untill defined number of separators arrive. Several consecutive
     * separators are counted as single separator.
     */
    auto start_time = std::chrono::steady_clock::now();
    int received_messages = 0;
    size_t current_size = 0;
    std::vector<std::string> measurements;
    mqtt::const_message_ptr messagePointer;

    while (measurements.size() < stoi(arguments["separators"])) {
        if (client->try_consume_message(&messagePointer)) {
            // message arrived
            bool separation = process_payload(received_messages, current_size, messagePointer);
            if (received_messages == 1 && !separation) {
                start_time = std::chrono::steady_clock::now(); // start timer - first measured payload arrived
            } else if (separation && received_messages > 0) {
                add_measurement(start_time, received_messages, current_size, &measurements); // stop timer - separator
                received_messages = 0; // reset message counter
            }
        }
    }
    store_string(format_output(measurements)); // save all measured data into file
}

int main(int argc, char *argv[]) {
    if (!set_parameters(argc, argv)) {
        return 1;
    }
    const auto client = prepare_consumer().release();
    consume(client);
    return 0;
}
