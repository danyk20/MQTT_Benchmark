#include <fstream>
#include <mqtt/client.h>
#include <ostream>
#include <filesystem>

class Configuration {
public:
    Configuration() {
        flags_ = {
            {
                "debug",
                {"print debug messages, e.g. separator arrived", "False", ""}
            },
            {
                "fresh",
                {"delete all previous measurements from data folder", "False", ""}
            },
            {
                "output",
                {"output file name", "consumer_results.txt", ""}
            },
            {
                "topic",
                {"topic from which messages will be subscribed", "test", ""}
            },
            {
                "client_id",
                {"unique client identification", "", ""}
            },
            {
                "library",
                {"which if the supported library to use [paho]", "paho", ""}
            },
            {
                "version",
                {"protocol version (currently supported only for paho)", "3.1.1", ""}
            },
            {
                "qos",
                {"Quality of Service, one or more values separated by comma", "1", ""}
            },
            {
                "username",
                {"authentication on broker", "", ""}
            },
            {
                "password",
                {"authentication on broker", "", ""}
            },
            {
                "directory",
                {"path to the directory where all measurements will be stored", "data/producer", ""}
            },
            {
                "consumers",
                {"number of consumers involved (used for storage structure)", "1", ""}
            },
            {
                "separators",
                {"number of separators to consume", "1", ""}
            },
            {
                "duration",
                {"number of seconds to send messages (exclusive with --messages flag)", "0", ""}
            },
            {
                "ratio",
                {
                    "ratio of overall duration that will be measured (starting phase is complement, ignored) [0-100]%",
                    "80", ""
                }
            }
        };
    }

    void set_flag(const std::string &flag, const std::string &value) {
        if (flags_.count(flag)) {
            flags_[flag].user_input = value;
        }
    }

    void set_preset(const std::string &flag, const std::string &value) {
        if (flags_.count(flag)) {
            flags_[flag].preset_value = value;
        }
    }

    std::string get_string(const std::string &flag) const {
        if (flags_.count(flag)) {
            return flags_.at(flag).user_input.empty()
                       ? flags_.at(flag).preset_value
                       : flags_.at(flag).user_input;
        }
        throw std::invalid_argument("Invalid flag name: " + flag);
    }

    std::string get_preset(const std::string &flag) const {
        if (flags_.count(flag)) {
            return flags_.at(flag).preset_value;
        }
        throw std::invalid_argument("Invalid flag name: " + flag);
    }

    size_t get_value(const std::string &flag) const {
        return std::stoul(get_string(flag));
    }

    std::string get_description(const std::string &flag) const {
        if (flags_.count(flag)) {
            return flags_.at(flag).description;
        }
        throw std::invalid_argument("Invalid flag name: " + flag);
    }

    bool is_true(const std::string &flag) const {
        if (flags_.count(flag)) {
            return get_string(flag) == "True";
        }
        throw std::invalid_argument("Invalid flag name: " + flag);
    }

    bool is_empty(const std::string &flag) const {
        if (flags_.count(flag)) {
            return get_string(flag).empty();
        }
        throw std::invalid_argument("Invalid flag name: " + flag);
    }

    std::vector<std::string> all_flags() const {
        std::vector<std::string> keys;
        keys.reserve(flags_.size());
        for (const auto &[key, _]: flags_) {
            keys.push_back(key);
        }
        return keys;
    }

    bool is_supported(const std::string &flag) const {
        return flags_.count(flag) > 0;
    }

private:
    struct Flag {
        std::string description;
        std::string preset_value;
        std::string user_input;
    };

    std::unordered_map<std::string, Flag> flags_;
};

Configuration config;

std::chrono::time_point<std::chrono::steady_clock> get_phase_deadline(
    const int phase,
    const std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now()) {
    /**
    * @ phase - number (0 - starting, 1- measutring)
    * @ start_time - to which time should be added the phase duration
    *
    * Calculates timepoint in future when should given phase end with assuption that it starts from start_time.
    *
    * Returns time_point when given phase should finish.
    */
    long phase_duration = 0;
    if (phase == 1) {
        phase_duration = config.get_value("duration") * config.get_value("ratio") / 100;
    } else {
        phase_duration = config.get_value("duration") * (100 - config.get_value("ratio")) / 100;
    }
    std::chrono::time_point deadline = start_time + std::chrono::seconds(phase_duration);
    return deadline;
}

void store_string(const std::string &data) {
    /**
     * Append string into the file
     *
     * @data - string to store
     */
    const std::string path = config.get_string("directory") + "/" + config.get_string("qos") + "/" + config.
                             get_string("consumers") + "/" + config.get_string("output");
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

void add_measurement(const std::chrono::steady_clock::time_point start_time, const int received_messages,
                     const size_t current_size, std::vector<std::string> *measurements) {
    /*
     * Add measurement as string into given vector of measurements. Format of single measurement is following:
     * [number_of_messages,size_of_the_message,B/s,number_of_messages/s]
     *
     * @start_time - when was the first message received
     * @received_messages - how many messages have been received
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
    if (config.is_true("debug")) {
        std::cout << measurement << " - " << duration.count() << "s" << std::endl;
    }
    measurements->push_back(measurement);
}

int get_mqtt_version(const std::string &user_input) {
    int version = 0;
    if (user_input == "3.1") {
        version = MQTTVERSION_3_1;
    } else if (user_input == "3.1.1") {
        version = MQTTVERSION_3_1_1;
    } else if (user_input == "5.0") {
        version = MQTTVERSION_5;
    } else {
        std::cerr << "Unsupported MQTT version: " << user_input << std::endl;
        std::cerr << "Using default one: " << std::endl;
    }
    return version;
}

std::unique_ptr<mqtt::client> prepare_consumer() {
    /**
     * Create connection to the broker and subsribe to the topic based on global constants and enviromental variables
     */
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));
    std::string broker = brokerAddress + ":" + std::to_string(brokerPort);

    auto client = std::make_unique<mqtt::client>(broker, config.get_string("client_id"),
                                                 mqtt::create_options(get_mqtt_version(config.get_string("version"))));
    auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .mqtt_version(get_mqtt_version(config.get_string("version")))
            .finalize();
    if (!config.is_empty("username")) {
        connOpts.set_user_name(config.get_string("username"));
    }
    if (!config.is_empty("password")) {
        connOpts.set_password(config.get_string("password"));
    }
    try {
        client->connect(connOpts);
        client->subscribe(config.get_string("topic"), std::stoi(config.get_preset("qos")));
        client->start_consuming();
    } catch (mqtt::exception &e) {
        if (e.get_return_code() == -1) {
            std::cerr << "MQTT connection failed - check if the broker is running :" << e.what() << std::endl;
        } else {
            std::cerr << e.what() << " code: " << std::to_string(e.get_return_code()) << std::endl;
        }
    }

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
    if (config.get_string("debug") == "messages" || config.get_string("debug") == "MESSAGES") {
        std::cout << messageString << std::endl;
    }
    if (messageString.empty() || messageString.at(0) == '!') {
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
    for (const auto &argument: config.all_flags()) {
        printf("--%-15s %-100s : <%s>\n", argument.c_str(), config.get_description(argument).c_str(),
               config.get_preset(argument).c_str());
    }
}


bool time_measurement(int &received_messages, std::vector<std::string> &measurements,
                      const mqtt::const_message_ptr &messagePointer,
                      std::chrono::time_point<std::chrono::steady_clock> &starting_phase,
                      std::chrono::time_point<std::chrono::steady_clock> &measuring_phase) {
    /**
    * Process measurement restricted by time.
    *
    * @received_messages - how many messages have been received
    * @measurements - list of measurements to save as the result
    * @message_pointer - pointer to the current received message
    * @starting_phase - deadline of starting phase
    * @measuring_phase - deadline of measurement phase
    *
    * returns True only if the measurement hasn't finished yet otherwise False
    */
    std::chrono::time_point<std::chrono::steady_clock> current_time = std::chrono::steady_clock::now();
    if (starting_phase == std::chrono::time_point<std::chrono::steady_clock>{}) {
        // set deadlines for each phase
        starting_phase = get_phase_deadline(0);
        measuring_phase = get_phase_deadline(1, starting_phase);
        if (config.is_true("debug")) {
            std::cout << "Starting phase started!" << std::endl;
        }
    }
    if (current_time >= measuring_phase) {
        // cleanup phase
        auto current_size = messagePointer->get_payload_str().size();
        add_measurement(starting_phase, received_messages, current_size, &measurements);
        if (config.is_true("debug")) {
            std::cout << "Cleanup phase started!" << std::endl;
        }
        return false;
    }
    if (current_time >= starting_phase) {
        // measurement phase
        received_messages++;
        if (config.is_true("debug") && received_messages == 1) {
            std::cout << "Measurement phase started!" << std::endl;
        }
    }
    return true;
}

bool count_measurement(int &received_messages, std::vector<std::string> &measurements,
                       const mqtt::const_message_ptr &messagePointer,
                       std::chrono::steady_clock::time_point &start_time, size_t &payload_size) {
    /**
    * Process measurement restricted by time.
    *
    * @received_messages - how many messages have been received
    * @measurements - list of measurements to save as the result
    * @message_pointer - pointer to the current received message
    * @start_time - first message timestap that trigger the mesurement
    * @payload_size - size of the first measured payload
    *
    * returns True only if the measurement hasn't finished yet otherwise False
    */
    size_t current_size;
    bool separation = process_payload(received_messages, current_size, messagePointer);
    if (received_messages == 1 && (!separation)) {
        start_time = std::chrono::steady_clock::now(); // start timer - first measured payload arrived
        payload_size = current_size;
    } else if (separation && received_messages > 0) {
        add_measurement(start_time, received_messages, payload_size, &measurements);
        // stop timer - separator
        received_messages = 0; // reset message counter
    }

    return measurements.size() < config.get_value("separators");
}

void consume(std::unique_ptr<mqtt::client>::pointer client) {
    /**
     * Consume all messages from subscribed topic untill defined number of separators arrive or time based deadline is
     * reached. Several consecutive separators are counted as single separator.
     */
    auto start_time = std::chrono::steady_clock::now();
    int received_messages = 0;
    std::vector<std::string> measurements;
    mqtt::const_message_ptr messagePointer;
    std::chrono::time_point<std::chrono::steady_clock> starting_phase;
    std::chrono::time_point<std::chrono::steady_clock> measuring_phase;
    bool measuring = true;
    size_t payload_size;

    while (measuring) {
        if (client->is_connected()) {
            if (client->try_consume_message(&messagePointer)) {
                if (messagePointer) {
                    // message arrived
                    if (config.get_value("duration")) {
                        measuring = time_measurement(received_messages, measurements, messagePointer, starting_phase,
                                                     measuring_phase);
                    } else {
                        measuring = count_measurement(received_messages, measurements, messagePointer, start_time,
                                                      payload_size);
                    }
                } else {
                    client->disconnect();
                    std::cerr << "Connection corrupted!" << std::endl;
                }
            }
        } else {
            std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Reconnecting client! - " <<
                    received_messages << " messages received" << std::endl;
            client = prepare_consumer().release();
            if (client->is_connected()) {
                std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Client Reconnected!" <<
                        std::endl;
            }
        }
    }
    store_string(format_output(measurements)); // save all measured data into file
}

std::vector<std::string> parse_qos(const std::string &input) {
    std::vector<std::string> numbers;
    std::stringstream ss(input);
    std::string temp;

    while (std::getline(ss, temp, ',')) {
        numbers.push_back(temp);
    }

    return numbers;
}

void clear_old_data(const std::string &path) {
    /**
     * Remove all previous measurments
     *
     * @path - string path to the direcctory
     */
    const std::filesystem::path dirPath = path;
    try {
        if (exists(dirPath)) {
            for (const auto &entry: std::filesystem::directory_iterator(dirPath)) {
                remove_all(entry.path());
            }
            if (config.is_true("debug")) {
                std::cout << "Old measurements cleared successfully." << std::endl;
            }
        } else {
            std::cerr << "Directory does not exist: " << dirPath << std::endl;
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

bool parse_arguments(int argc, char *argv[], Configuration &config) {
    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];
        flag = flag.substr(2); // remove leading "--"

        if (argv[i] == "--help" || argv[i] == "-h") {
            print_flags();
            return false;
        } else if (config.is_supported(flag)) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.set_flag(flag, argv[++i]);
            } else {
                std::cerr << "Warning: Missing value for flag " << flag << ". Using default.\n";
            }
        } else {
            std::cerr << "Warning: Unknown flag '" << flag << "' ignored.\n";
            print_flags();
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (!parse_arguments(argc, argv, config)) {
        return 1;
    }
    if (config.is_true("fresh")) {
        clear_old_data(config.get_string("directory"));
    }
    const auto client = prepare_consumer().release();
    for (const auto &qos: parse_qos(config.get_preset("qos"))) {
        config.set_preset("qos", qos);
        consume(client);
    }
    return 0;
}
