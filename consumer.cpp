#include <fstream>
#include <mqtt/async_client.h>
#include <ostream>
#include <filesystem>
#include <unistd.h>

#include "mosquitto.h"

class Measurement {
public:
    bool active;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    int received_messages;
    std::chrono::time_point<std::chrono::steady_clock> starting_phase;
    std::chrono::time_point<std::chrono::steady_clock> measuring_phase;
    std::vector<std::string> results;
    size_t payload_size = 0;
    std::chrono::time_point<std::chrono::steady_clock> next_report;
    std::vector<long long> latency = {};

    Measurement() {
        this->active = true;
        this->received_messages = 0;
    }

    void start() {
        this->start_time = std::chrono::steady_clock::now();
        this->next_report = std::chrono::steady_clock::now();
    }
};

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
                {"delete all previous measurements from output folder", "False", ""}
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
                {"which if the supported library to use [paho/mosquitto]", "paho", ""}
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
                {"path to the directory where all measurements will be stored", "data/consumer", ""}
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
            },
            {
                "report",
                {"how often should consumer report number of received messages when debug is True in seconds", "30", ""}
            },
            {
                "session",
                {"whether to keep previous seasion with broker or not after disconnect", "True", ""}
            },
            {
                "latency",
                {"whether to measure delivery latency", "False", ""}
            },
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
    size_t phase_duration = 0;
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

std::string latency_output(const std::vector<long long> &latencies) {
    /**
     * Format elements in the vector into following string (min,max,avg)
     *
     * @latencies - vector of latencies in ms to be formatted
     */
    if (latencies.empty()) {
        return "No latency measurements!";
    }
    long long min_latency = latencies.at(0);
    long long max_latency = latencies.at(0);
    long long total_latency = 0;
    for (const long long latency: latencies) {
        total_latency += latency;
        if (latency < min_latency) {
            min_latency = latency;
        }
        if (latency > max_latency) {
            max_latency = latency;
        }
    }
    return "Min: " + std::to_string(min_latency) + "ms, Max: " + std::to_string(max_latency) + "ms, Avg: " +
           std::to_string(total_latency / latencies.size()) + "ms";
}

void add_measurement(Measurement &measurement, const size_t current_size) {
    /*
     * Add measurement as string into given vector of measurements. Format of a single measurement is the following:
     * [number_of_messages,size_of_the_message,B/s,number_of_messages/s]
     *
     * @measurement - object containing all measurement data
     * @current_size - how big is each of the messages
     */
    const auto start_time = measurement.start_time;
    const auto received_messages = measurement.received_messages;
    const std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time) / 1000.0;
    const auto message_per_seconds = received_messages / duration.count();
    const auto throughput = message_per_seconds * static_cast<double>(current_size);
    const std::string record = "[" + std::to_string(received_messages) + "," + std::to_string(current_size) + ","
                               + std::to_string(static_cast<int>(throughput)) + "," +
                               std::to_string(static_cast<int>(message_per_seconds)) + "]";
    if (config.is_true("debug")) {
        std::cout << record << " - " << duration.count() << "s" << std::endl;
    }
    if (config.is_true("latency")) {
        std::cout << latency_output(measurement.latency) << std::endl;
    }
    measurement.results.push_back(record);
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

void init_client(mqtt::async_client &client) {
    /**
    * This function sets up an MQTT client connection with options from the global config object.
    * It handles:
    * - Connection parameters (clean session, keep alive, MQTT version)
    * - Authentication (username/password if provided)
    * - Automatic reconnection
    * - Topic subscription if this is a new session
    * - Error handling with retries for connection failures
    *
    * The function will continuously retry connection with 1-second delays until successful.
    *
    * @param client Reference to the MQTT async client to initialize
    */
    auto connOpts = mqtt::connect_options_builder()
            .clean_session(config.is_true("session"))
            .automatic_reconnect()
            .keep_alive_interval(std::chrono::seconds(30))
            .mqtt_version(get_mqtt_version(config.get_string("version")))
            .finalize();
    if (!config.is_empty("username")) {
        connOpts.set_user_name(config.get_string("username"));
    }
    if (!config.is_empty("password")) {
        connOpts.set_password(config.get_string("password"));
    }
    while (true) {
        try {
            if (!client.connect(connOpts)->get_connect_response().is_session_present()) {
                client.subscribe(config.get_string("topic"), std::stoi(config.get_preset("qos")));
            }
            break;
        } catch (mqtt::exception &e) {
            if (e.get_return_code() == -1) {
                std::cerr << "MQTT connection failed - check if the broker is running :" << e.what() << std::endl;
            } else if (e.get_return_code() == 2) {
                std::cerr << "MQTT connection refused - check if the client_id and credentials are valid :" << e.what()
                        <<
                        std::endl;
            } else {
                std::cerr << e.what() << " code: " << std::to_string(e.get_return_code()) << std::endl;
            }
            sleep(config.get_value("report"));
        }
    }
}

std::unique_ptr<mqtt::async_client> prepare_consumer() {
    /**
     * Create connection to the broker and subsribe to the topic based on global constants and enviromental variables
     */
    const std::string brokerAddress = std::getenv("BROKER_IP")
                                          ? std::getenv("BROKER_IP")
                                          : throw std::runtime_error(
                                              "Broker IP not provided as environment variable BROKER_IP!");
    const int brokerPort = std::getenv("MQTT_PORT")
                               ? std::stoi(std::getenv("MQTT_PORT"))
                               : throw std::runtime_error(
                                   "Broker port not provided as environment variable MQTT_PORT!");
    std::string broker = brokerAddress + ":" + std::to_string(brokerPort);

    auto client = std::make_unique<mqtt::async_client>(broker, config.get_string("client_id"));

    client->start_consuming();
    init_client(*client);

    return client;
}

void calculate_latency(const std::string &message, Measurement &measurement) {
    /**
     * Calculates and records the latency of a message
     *
     * @message - The received message as a constant reference to a `std::string`.
     * The first 16 characters of this string are expected to contain a `long long` timestamp
     * @measurement - A reference to a `Measurement` object where the calculated latency will be stored. The `latency`
     * member of this object
     */
    if (config.is_true("latency")) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        const long long sender_timestamp = std::stoll(message.substr(0, 13)); // ms timestamp has 13 digits
        measurement.latency.push_back(now.count() - sender_timestamp);
    }
}

void report(Measurement &measurement) {
    /**
     * Prints debug report in given frequncy
     *
     * @measurement - object contianing: number of received meessages, deadline of starting_phase and measuring_phase
     *
     */
    std::cout << "Consumed: " << std::to_string(measurement.received_messages) << " messages" << std::endl;
    measurement.next_report = std::chrono::steady_clock::now() + std::chrono::seconds(config.get_value("report"));
}

bool process_payload(const std::string &payload, Measurement &measurement) {
    /**
     * Read message size and update message counter. Empty message is considered as separator.
     *
     * @measurement - object contianing: number of received meessages, start time, payload size, time of the next report
     * @payload - received message as a String
     *
     * returns True only if the message is empty
     */
    calculate_latency(payload, measurement);
    if (config.get_string("debug") == "messages" || config.get_string("debug") == "MESSAGES") {
        std::cout << payload << std::endl;
    }
    if (config.is_true("debug") && measurement.next_report <= std::chrono::steady_clock::now()) {
        report(measurement);
    }
    if (payload.empty() || payload.at(0) == '!') {
        return true;
    }
    measurement.payload_size = payload.size();
    measurement.received_messages++;
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

bool time_measurement(const std::string &message, Measurement &measurement) {
    /**
    * Process measurement restricted by time.
    *
    * @measurement - object contianing: number of received meessages, deadline of starting_phase and measuring_phase
    * @message - the current received message
    *
    * returns True only if the measurement hasn't finished yet otherwise False
    */
    const std::chrono::time_point<std::chrono::steady_clock> current_time = std::chrono::steady_clock::now();
    if (measurement.starting_phase == std::chrono::time_point<std::chrono::steady_clock>{}) {
        // set deadlines for each phase
        measurement.starting_phase = get_phase_deadline(0);
        measurement.measuring_phase = get_phase_deadline(1, measurement.starting_phase);
        if (config.is_true("debug")) {
            std::cout << "Starting phase started!" << std::endl;
        }
    }
    if (current_time >= measurement.measuring_phase) {
        // cleanup phase
        const size_t current_size = message.size();
        add_measurement(measurement, current_size);
        if (config.is_true("debug")) {
            std::cout << "Cleanup phase started!" << std::endl;
        }
        return false;
    }
    if (current_time >= measurement.starting_phase) {
        // measurement phase
        calculate_latency(message, measurement);
        measurement.received_messages++;
        if (measurement.received_messages == 1) {
            measurement.start();
            if (config.is_true("debug")) {
                std::cout << "Measurement phase started!" << std::endl;
            }
        }
        if (measurement.next_report <= current_time && config.is_true("debug")) {
            report(measurement);
        }
    }
    return true;
}

bool count_measurement(const std::string &message, Measurement &measurement) {
    /**
    * Process measurement restricted by time.
    *
    * @measurement - object contianing: number of received meessages, start time, payload size, time of the next report
    * @message - current received message as a String
    *
    * returns True only if the measurement hasn't finished yet otherwise False
    */
    bool separation = process_payload(message, measurement);
    if (measurement.received_messages == 1 && (!separation)) {
        measurement.start_time = std::chrono::steady_clock::now(); // start timer - first measured payload arrived
    } else if (separation && measurement.received_messages > 0) {
        add_measurement(measurement, measurement.payload_size);
        // stop timer - separator
        measurement.received_messages = 0; // reset message counter
    }

    return measurement.results.size() < config.get_value("separators");
}

std::vector<std::string> paho_measure() {
    std::unique_ptr<mqtt::async_client>::pointer client = prepare_consumer().release();
    mqtt::const_message_ptr messagePointer;
    auto measurement = Measurement();

    while (measurement.active) {
        auto event = client->consume_event();
        const auto &shared_ptr = event.get_message_if();
        if (shared_ptr) {
            auto &message_pointer = *shared_ptr;
            std::string message = message_pointer->get_payload_str();
            if (message_pointer) {
                if (config.get_value("duration")) {
                    measurement.active = time_measurement(message, measurement);
                } else {
                    measurement.active = count_measurement(message, measurement);
                }
            } else {
                std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Empty pointer arrived!" <<
                        std::endl;
            }
        } else if (event.is_connected()) {
            std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Client Reconnected!" <<
                    std::endl;
            if (config.is_true("session")) {
                // Client needs to subscribe to the same topic again after reconnecting if the session was cleaned
                client->subscribe(config.get_string("topic"), std::stoi(config.get_preset("qos")));
            }
        } else if (event.is_connection_lost()) {
            std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() <<
                    " Connection lost - reconnecting! - " << measurement.received_messages << " messages received" <<
                    std::endl;
        }
    }
    return measurement.results;
}

void on_connect(struct mosquitto *mosq, void *obj, int rc) {
    if (rc == 0) {
        std::cout << "Connected to broker.\n";
        mosquitto_subscribe(mosq, nullptr, config.get_string("topic").c_str(), std::stoi(config.get_preset("qos")));
    } else {
        std::cerr << "Failed to connect, return code " << rc << "\n";
    }
}

void on_message(struct mosquitto *mosq, void *obj, const mosquitto_message *msg) {
    const std::string message(static_cast<char *>(msg->payload), msg->payloadlen);
    if (config.get_string("debug") == "messages" || config.get_string("debug") == "MESSAGES") {
        std::cout << "Received message on topic " << msg->topic << ": " << message << "\n";
    }
    auto *measurement = static_cast<Measurement *>(obj);

    if (config.get_value("duration")) {
        measurement->active = time_measurement(message, *measurement);
    } else {
        measurement->active = count_measurement(message, *measurement);
    }
}

void authenticate(mosquitto *mosq) {
    /**
     * Set credentials for MQTT connection
     *
     * @ *mosq - Mosquitto configuration object reference
     */
    const std::string username_str = config.get_string("username");
    const std::string password_str = config.get_string("password");
    const char *username = username_str.empty() ? nullptr : username_str.c_str();
    const char *password = password_str.empty() ? nullptr : password_str.c_str();
    const int authentification = mosquitto_username_pw_set(mosq, username, password);
    if (authentification == MOSQ_ERR_SUCCESS && config.is_true("debug")) {
        std::cout << "User '" << username << "':'" << password << "' authenticated!\n";
    } else if (authentification == MOSQ_ERR_INVAL) {
        std::cout << "User '" << username << "' credentials invalid!\n";
    }
}

std::vector<std::string> mosquitto_measure() {
    Measurement measurement;
    mosquitto_lib_init();

    mosquitto *mosq = mosquitto_new(config.is_empty("client_id") ? nullptr : config.get_string("client_id").c_str(),
                                    config.is_true("session"), &measurement);
    while (!mosq) {
        std::cerr << "Failed to create Mosquitto instance.\n";
        sleep(config.get_value("report"));
    }

    authenticate(mosq);

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    while (mosquitto_connect(mosq, std::getenv("BROKER_IP"), std::stoi(std::getenv("MQTT_PORT")), 60) !=
           MOSQ_ERR_SUCCESS) {
        std::cerr << "Unable to connect to broker.\n";
        sleep(config.get_value("report"));
    }

    mosquitto_loop_start(mosq);

    while (measurement.active){};
    mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return measurement.results;
}

void consume() {
    /**
     * Consume all messages from subscribed topic untill defined number of separators arrive or time based deadline is
     * reached. Several consecutive separators are counted as single separator.
     */
    const std::string library = config.get_string("library");
    std::vector<std::string> measurements;
    if (library == "PAHO" || library == "paho") {
        measurements = paho_measure();
    } else if (library == "MOSQUITTO" || library == "mosquitto") {
        measurements = mosquitto_measure();
    } else {
        std::cerr << "Unknown library type: " << library << "\n";
    }
    store_string(format_output(measurements)); // save all measured data into the file
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

bool parse_arguments(const int argc, char *argv[], Configuration &config) {
    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];
        flag = flag.substr(2); // remove leading "--"

        if (flag == "help" || flag == "h") {
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

int main(const int argc, char *argv[]) {
    if (!parse_arguments(argc, argv, config)) {
        return 1;
    }
    if (config.is_true("fresh")) {
        clear_old_data(config.get_string("directory"));
    }
    for (const auto &qos: parse_qos(config.get_string("qos"))) {
        config.set_preset("qos", qos);
        consume();
    }
    return 0;
}
