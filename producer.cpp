#include <string>
#include <fstream>
#include <mqtt/async_client.h>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>

#include "mosquitto.h"
#include <cstdio>
#include <unistd.h>

#define UNUSED(x) (void)(x)

class Configuration {
public:
    Configuration() {
        flags_ = {
            {
                "debug",
                {"print debug messages, e.g. when buffer reaches the limit", "False", ""}
            },
            {
                "fresh",
                {"delete all previous measurements from output folder", "False", ""}
            },
            {
                "separator",
                {"send separator after last message of each payload batch", "True", ""}
            },
            {
                "output",
                {"output file name", "producer_results.txt", ""}
            },
            {
                "topic",
                {"topic to which messages will be published", "test", ""}
            },
            {
                "client_id",
                {"unique client identification", "", ""}
            },
            {
                "library",
                {"which if the supported libraries to use [paho/mosquitto]", "paho", ""}
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
                "period", {"minimum delay between 2 consecutive messages", "80", ""}
            },
            {
                "messages",
                {"number of messages that will send per each payload size (exclusive with --duration flag)", "400", ""}
            },
            {
                "buffer",
                {"max number of messages that can stored in the buffer", "100", ""}
            },
            {
                "repetitions",
                {"number of times to run the all measurements all over again", "1", ""}
            },
            {
                "timeout",
                {"timeout for each KB of payload in ms", "5", ""}
            },
            {
                "min_timeout",
                {"minimum total timeout in ms", "1000", ""}
            },
            {
                "min",
                {"minimum payload size in KB", "72", ""}
            },
            {
                "max",
                {"maximum payload size in KB", "72", ""}
            },
            {
                "percentage",
                {"once buffer is full, wait until buffer is less than percentage [0-100]%", "50", ""}
            },
            {
                "producers",
                {"number of producers involved (used for storage structure)", "1", ""}
            },
            {
                "duration",
                {"number of seconds to send messages (exclusive with --messages flag)", "0", ""}
            },
            {
                "middle",
                {"beginning and end of the measurement will be cut off except middle part of [0-100]%", "50", ""}
            },
            {
                "debug_period",
                {"Time between consecutive progress messages in seconds.", "5", ""}
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

static bool is_reconnecting = false;
static size_t mosquitto_published = 0;

static void connected_handler(const std::string &cause) {
    if (cause != "connect onSuccess called") {
        // except initial connect
        std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Connected : " << cause <<
                std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3)); // TO DO Remove constant
    }
    is_reconnecting = false;
}

static void disconnected_handler(const std::string &cause) {
    std::cerr << std::chrono::steady_clock::now().time_since_epoch().count() << " Disconnected : " << cause <<
            std::endl;
    is_reconnecting = true;
}

std::vector<std::string> parse_qos(const std::string &input) {
    /**
     * @ input string input from the user
     *
     * supported are single valid QoS values or list of valid values separed by comma
     *
     * Returns list of QoS as int values
     */
    std::vector<std::string> numbers;
    std::stringstream ss(input);
    std::string temp;

    while (std::getline(ss, temp, ',')) {
        numbers.push_back(temp);
    }

    return numbers;
}

long get_timeout(const size_t payload) {
    /**
     * @ payload size in B
     * Returns timeout in miliseconds which is 1s or more based on Buffer size and payload size
     */
    const size_t timeout = config.get_value("timeout") * config.get_value("buffer") * static_cast<long>(payload / 1024);
    if (timeout < config.get_value("min_timeout")) {
        return static_cast<long>(config.get_value("min_timeout"));
    }
    return static_cast<long>(timeout);
}

void publish_separator(mqtt::async_client &client, const bool disconnect = false) {
    /**
     * Send empty paylod as a separator for time measurment on the consumer and disconnect from the broker
     *
     * @client - connection object
     */
    const std::string empty_message;
    const auto msg = mqtt::make_message(config.get_string("topic"), empty_message);
    msg->set_qos(1);
    if (!client.publish(msg)->wait_for(get_timeout(0))) {
        std::cerr << "publishing of separator failed" << std::endl;
    }
    if (disconnect) {
        if (client.is_connected()) {
            client.disconnect()->wait_for(get_timeout(0));
        }
    }
}


void publish_separator(mosquitto &mosq, const bool disconnect = false) {
    /**
     * Send empty paylod as a separator for time measurment on the consumer and disconnect from the broker
     *
     * @client - connection object
     */
    const std::string empty_message;
    constexpr int qos = 1;
    const char *topic = config.get_string("topic").c_str();

    if (const int return_code = mosquitto_publish(&mosq, nullptr, topic, 0, &empty_message, qos, false);
        return_code != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(return_code));
    }

    if (disconnect) {
        if (const int return_code = mosquitto_disconnect(&mosq); return_code != MOSQ_ERR_SUCCESS) {
            std::cerr << "Error disconnecting mosquitto " << return_code << std::endl;
        }
    }
}

std::string process_measurement(const std::chrono::steady_clock::time_point start_time,
                                const std::vector<std::shared_ptr<mqtt::message> >::size_type payload_size,
                                const size_t number_of_messages) {
    /**
     * Calculate all attributes from the measuremnt and return them in a string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     *
     * @start_time - when start sending of the first payload
     * @payload_size - how big was each payload
     * @number_of_messages - number of published messages
     */
    const auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time) / 1000.0;
    const auto message_per_seconds = static_cast<int>(number_of_messages) / duration.count();
    const auto throughput = message_per_seconds * static_cast<double>(payload_size);
    const std::string measurement = "[" + std::to_string(number_of_messages) + "," + std::to_string(payload_size) + ","
                                    + std::to_string(static_cast<int>(throughput)) + "," +
                                    std::to_string(static_cast<int>(message_per_seconds)) + "]";
    if (config.is_true("debug")) {
        std::cout << measurement << " - " << duration.count() << "s" << std::endl;
    }


    if (config.is_true("debug")) {
        std::cout << "Sent " << number_of_messages << " messages of size " << payload_size << " using QoS " <<
                config.get_preset("qos") << " bytes to topic '" << config.get_string("topic") << "' in " << duration.
                count() << " s" << std::endl;
    }

    return "[" + std::to_string(number_of_messages) + "," + std::to_string(payload_size) + "," +
           std::to_string(throughput) + "," + std::to_string(message_per_seconds) + "]";
}

size_t delivered_messages(const std::vector<std::shared_ptr<mqtt::token> > &tokens) {
    /**
     * Count number of succefully send messages.
     *
     * @ tokens - list of all tokens for messages that have been already sent asynchronously
     */
    size_t index = tokens.size();
    for (; index > 0 && tokens[index - 1]->get_return_code() != 0; index--) {
    }
    return index;
}

bool wait_for_buffer_dump(const size_t sent, const size_t percentage, const size_t payload_size) {
    /**
    * @ sent - number of messaged that have been sent regadless of their acknowledgment
    * @ percentage - size of the full baffer that is reaquired to become free
    * @ payload_size - message size
    * Returns true if there is free space in the buffer therwise false
    *
    * Waits until there is required percentage of buffer free or there is timeout - whichever comes first
    */
    const size_t max_occupied_buffer = ((100 - percentage) * config.get_value("buffer")) / 100;

    const std::chrono::time_point<std::chrono::steady_clock> deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(get_timeout(payload_size));

    while (std::chrono::steady_clock::now() < deadline) {
        if (sent - mosquitto_published < max_occupied_buffer) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const size_t publishing_message = (mosquitto_published + 1);
    if (config.is_true("debug")) {
        std::cout << get_timeout(payload_size) << "ms timeout waiting for message " << publishing_message << std::endl;
    }
    return false;
}

bool wait_for_buffer_dump(const std::vector<std::shared_ptr<mqtt::token> > &tokens, size_t &last_published,
                          const size_t percentage, const size_t payload_size) {
    /**
    * @ tokens - list of all tokens for messages that have been already sent asynchronously
    * @ last_published - index of the last confirmed token (message has been sent) from the tokens list
    * @ percentage - size of the full baffer that is reaquired to be free
    * @ payload_size - message size
    * Returns true if there is free space in the buffer therwise false
    *
    * Waits until there is required percentage of buffer free or there is timeout - whichever comes first
    */
    const size_t available_buffer = (config.get_value("buffer") * percentage) / 100;
    size_t middle_index = std::max(available_buffer, 1ul) + last_published;
    if (middle_index >= tokens.size()) {
        middle_index = tokens.size() - 1;
    }

    try {
        if (!tokens[middle_index]->wait_for(get_timeout(payload_size))) {
            std::cout << get_timeout(payload_size) << "ms timeout waiting for message " << middle_index << std::endl;
            return false;
        }
    } catch (const std::exception &e) {
        std::cerr << "Messages from last batch might be lost " << e.what() << std::endl;
        for (auto i = last_published; i < tokens.size() - 1; ++i) {
            if (!tokens[i]->is_complete()) {
                last_published = i - 1;
                break;
            }
        }
        return tokens[last_published]->is_complete();
    }

    last_published = middle_index;
    return true;
}

int get_mqtt_version(const std::string &user_input) {
    /**
    * @ user_input - string input from the user
    *
    * supported are all valid version with dot notation
    *
    * Returns valid MQTT version as int
    */
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

std::chrono::time_point<std::chrono::steady_clock> get_phase_deadline(const int phase) {
    /**
    * @ phase - number (0 - starting, 1- measutring, 2- cleanup)
    *
    * Calculates timepoint in future when should given phase end with assuption that it starts now. (cleanup and
    * starting phase have eaqual duration)
    *
    * Returns time_point when given phase should finish.
    */
    size_t phase_duration = 0;
    if (phase == 1) {
        phase_duration = config.get_value("duration") * config.get_value("middle") / 100;
    } else {
        phase_duration = config.get_value("duration") * (100 - config.get_value("middle")) / 200;
    }
    const std::chrono::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(phase_duration);
    return deadline;
}

char* add_timestamp(const size_t payload_size, const char *msg) {
    /**
     * Creates a new character array containing the provided message,
     * optionally prepending a timestamp if a specific configuration is enabled.
     *
     * @param payload_size The intended maximum size of the payload (excluding the null terminator).
     * @param msg A null-terminated C-string to be used as the base message.
     * @return A pointer to a newly allocated character array containing the (potentially timestamped) payload.
     */
    const auto payload = new char[payload_size + 1];
    std::strcpy(payload, msg);
    if (config.is_true("latency")) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        const std::string time_str = std::to_string(now.count());
        strncpy(payload, time_str.c_str(), strlen(time_str.c_str()));
    }
    return payload;
}

void send(size_t payload_size, mosquitto &mosq, const char *msg, const char *topic, const int qos,
          size_t &sent, const bool debug = false) {
    /**
    * @ payload_size - size of the message
    * @ client - configured connection to broker
    * @ msg - prepared message
    * @ tokens - list of all tokens for messages that have been already sent asynchronously
    * @ last_published - index of the last confirmed token (message has been sent) from the tokens list
    * @ debug - print debug message when buffer window moves
    *
    * Sends single message
    */
    while (is_reconnecting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (sent - mosquitto_published >= config.get_value("buffer")) {
        // message buffer is full
        while (true) {
            if (!wait_for_buffer_dump(sent, config.get_value("percentage"), payload_size)) {
                std::cerr << "Buffer couldn't be freed!";
            }
            break;
        }
    }
    const char *payload = add_timestamp(payload_size, msg);
    if (const int return_code = mosquitto_publish(&mosq, nullptr, topic, static_cast<int>(payload_size), payload, qos,
                                                  false);
        return_code != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(return_code));
    } else {
        sent++;
    }
    delete[] payload;
    const auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.get_value("period"));
    if (debug) {
        std::cout << sent << " - published and " << sent - mosquitto_published << " in buffer" << std::endl;
    }
    std::this_thread::sleep_until(next_run_time);
}

void send(size_t payload_size, mqtt::async_client &client, const mqtt::message_ptr &msg,
          std::vector<std::shared_ptr<mqtt::token> > &tokens, size_t &last_published, const bool debug = false) {
    /**
    * @ payload_size - size of the message
    * @ client - configured connection to broker
    * @ msg - prepared message
    * @ tokens - list of all tokens for messages that have been already sent asynchronously
    * @ last_published - index of the last confirmed token (message has been sent) from the tokens list
    * @ debug - print debug message when buffer window moves
    *
    * Sends single message
    */
    if (tokens.size() - last_published >= config.get_value("buffer")) {
        // message buffer is full
        while (true) {
            if (!wait_for_buffer_dump(tokens, last_published, config.get_value("percentage"), payload_size)) {
                if (client.is_connected()) {
                    throw std::runtime_error("Buffer reached limit!");
                }
                std::cerr << "Buffer couldn't be freed because the client is disconnected!";
                // TO DO remove too frequent print
            }
            break;
        }
    }
    const auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.get_value("period"));
    while (is_reconnecting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const char *payload = add_timestamp(payload_size, msg->get_payload_str().c_str());
    if (config.is_true("latency")) {
        msg->set_payload(payload);
    }

    tokens.push_back(client.publish(msg));
    delete[] payload;

    if (debug) {
        std::cout << last_published << " - published and " << tokens.size() - last_published << " in buffer" <<
                std::endl;
    }
    std::this_thread::sleep_until(next_run_time);
}

bool print_debug(std::chrono::time_point<std::chrono::steady_clock> &next_print) {
    /**
    * @ next_print - earliest timestamp when next this function returns true
    *
    * Compute whether it is time to print another debug message or not yet
    *
    * Returns true only if from last time when it returned true passed at least defined number of seconds otherwise false
    */

    if (const std::chrono::time_point current_time = std::chrono::steady_clock::now(); current_time >= next_print) {
        next_print = current_time + std::chrono::seconds(config.get_value("debug_period"));
        return true;
    }
    return false;
}

void perform_publishing_cycle(size_t payload_size, mqtt::async_client &client, const mqtt::message_ptr &mqtt_message,
                              const mqtt::message_ptr &mqtt_ignore,
                              std::vector<std::shared_ptr<mqtt::token> > &tokens) {
    /**
    * @ payload_size - size of the message
    * @ client - configured connection to broker
    * @ mqtt_message - prepared measured message
    * @ mqtt_ignore - prepared non measured message
    * @ tokens - list of all tokens for messages that have been already sent asynchronously
    *
    * Sends multiple messages - restricted via time or number of messages
    */
    size_t last_published = 0;
    const bool debug = config.is_true("debug");
    std::chrono::time_point<std::chrono::steady_clock> next_print = std::chrono::steady_clock::now();

    if (config.get_value("duration") == 0) {
        // Publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        for (size_t i = 0; i < config.get_value("messages"); ++i) {
            send(payload_size, client, mqtt_message, tokens, last_published, debug && print_debug(next_print));
        }
    } else {
        // Starting phase: 0
        if (debug) {
            std::cout << " Starting phase " << std::endl;
        }
        auto end_time = get_phase_deadline(0);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_size, client, mqtt_ignore, tokens, last_published, debug && print_debug(next_print));
        }
        // Measurement phase: 1
        if (debug) {
            std::cout << " Measurement phase " << std::endl;
        }


        end_time = get_phase_deadline(1);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_size, client, mqtt_message, tokens, last_published, debug && print_debug(next_print));
        }
        // Cleanup phase: 2
        if (debug) {
            std::cout << " Cleanup phase " << std::endl;
        }
        end_time = get_phase_deadline(2);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_size, client, mqtt_ignore, tokens, last_published, debug && print_debug(next_print));
        }
    }
    // Wait for all published tokens to complete
    if (!tokens.empty() && !tokens.back()->wait_for(get_timeout(payload_size))) {
        std::cout << get_timeout(payload_size) << "ms timeout waiting for message " << last_published << std::endl;
    }
}

std::string publish_paho(const std::string &message, int qos) {
    /**
     * Send messages asynchronously and measure that time. After sending all messages send one empty payload and close
     * the connection.
     *
     * @message - payload to publish
     *
     * Returns measurement as string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     */
    const std::string brokerAddress = std::getenv("BROKER_IP")
                                          ? std::getenv("BROKER_IP")
                                          : throw std::runtime_error(
                                              "Broker IP not provided as environment variable BROKER_IP!");
    const int brokerPort = std::getenv("MQTT_PORT")
                               ? std::stoi(std::getenv("MQTT_PORT"))
                               : throw std::runtime_error(
                                   "Broker port not provided as environment variable MQTT_PORT!");

    std::vector<std::shared_ptr<mqtt::token> > tokens;
    constexpr long expected_throughput = 1000000000l; // max 1 GB
    const size_t expected_messages = config.get_value("duration") * (
                                         expected_throughput / static_cast<long>(message.size()));
    tokens.reserve(std::max(expected_messages, config.get_value("messages"))); // to track async messages

    try {
        auto client = mqtt::async_client(brokerAddress + ":" + std::to_string(brokerPort),
                                         config.get_string("client_id"),
                                         static_cast<int>(config.get_value("buffer")));

        client.set_connection_lost_handler(disconnected_handler);
        client.set_connected_handler(connected_handler);

        auto connOpts = mqtt::connect_options_builder()
                .clean_session(config.is_true("session"))
                .automatic_reconnect(true)
                .mqtt_version(get_mqtt_version(config.get_string("version")))
                .finalize();
        if (!config.is_empty("username")) {
            connOpts.set_user_name(config.get_string("username"));
        }
        if (!config.is_empty("password")) {
            connOpts.set_password(config.get_string("password"));
        }

        if (!client.connect(connOpts)->wait_for(get_timeout(0))) {
            std::cerr << "connect failed - timeout" << std::endl;
            return "Client could not connect - Connect timeout!";
        }

        // Pre-create the message to minimize allocation overhead
        auto mqtt_message = mqtt::make_message(config.get_string("topic"), message, qos, false);
        std::string ignore = message;
        ignore.replace(0, 1, "!");
        mqtt::message_ptr mqtt_ignore = mqtt::make_message(config.get_string("topic"), ignore, qos, false);

        // first non-measured message
        publish_separator(client);

        auto payload_size = message.size();
        auto start_time = std::chrono::steady_clock::now();

        perform_publishing_cycle(message.size(), client, mqtt_message, mqtt_ignore, tokens);
        std::string measurement = process_measurement(start_time, payload_size, tokens.size());

        publish_separator(client, true);
        return measurement;
    } catch (const std::exception &e) {
        size_t successful_messages = delivered_messages(tokens);
        std::cerr << "Failed to publish " << successful_messages << "th MQTT messages: " << e.what() << std::endl;
        std::stringstream ss;
        ss << "[" << successful_messages << ",0,0,0] - and Failed because " << e.what();
        return ss.str(); // NaN - measurement failed
    }
    catch (...) {
        size_t successful_messages = delivered_messages(tokens);
        std::cerr << "Unknown exception occurred" << std::endl;
        std::stringstream ss;
        ss << "[" << successful_messages << ",0,0,0] - and Failed" << std::endl;
        return ss.str(); // NaN - measurement failed
    }
}

void on_connect(struct mosquitto *mosq, void *obj, const int reason_code) {
    printf("%ld connected: %s\n", std::chrono::steady_clock::now().time_since_epoch().count(),
           mosquitto_connack_string(reason_code));
    std::this_thread::sleep_for(std::chrono::seconds(5)); // TO DO Remove constant
    is_reconnecting = false;
}

void on_publish(struct mosquitto *mosq, void *obj, const int id) {
    mosquitto_published++;
}

void on_disconnect(struct mosquitto *mosq, void *p, int i) {
    is_reconnecting = true;
    printf("%ld Disconnected: %s\n", std::chrono::steady_clock::now().time_since_epoch().count(),
           mosquitto_connack_string(i));
}

mosquitto *get_mosquitto() {
    /**
     * Initilize Mosquitto client
     */
    mosquitto_lib_init();
    const char *client_id = config.is_empty("client_id") ? nullptr : config.get_string("client_id").c_str();
    mosquitto *mosq = mosquitto_new(client_id, config.is_true("session"), nullptr);
    if (mosq == nullptr) {
        fprintf(stderr, "Error: Out of memory.\n");
        throw std::runtime_error("Fail to create client");
    }

    const char *username = config.is_empty("username") ? nullptr : config.get_string("username").c_str();
    const char *password = config.is_empty("password") ? nullptr : config.get_string("password").c_str();

    mosquitto_username_pw_set(mosq, username, password);

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_publish_callback_set(mosq, on_publish);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);

    int rc = mosquitto_connect(mosq, std::getenv("BROKER_IP"), std::stoi(std::getenv("MQTT_PORT")), 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq);
        fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
        throw std::runtime_error("Fail connect");
    }

    rc = mosquitto_loop_start(mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq);
        fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
        throw std::runtime_error("Fail network loop");
    }

    return mosq;
}

size_t perform_publishing_cycle(mosquitto &mosq, const std::string &message, const int qos) {
    /**
    * @ mosq - configured connection to broker
    * @ message - message as a string
    * @ qos - Quality of service
    * Returns number of messages excluding separators
    *
    * Sends multiple messages - restricted via time or number of messages
    */
    const size_t payload_len = message.size();
    std::string ignore = message;
    ignore.replace(0, 1, "!");
    const char *message_ignore_ptr = ignore.c_str();
    const char *message_ptr = message.c_str();
    const char *topic = config.get_string("topic").c_str();
    const bool debug = config.is_true("debug");
    size_t measured_messages = 0;
    std::chrono::time_point<std::chrono::steady_clock> next_print = std::chrono::steady_clock::now();

    size_t sent = 0;
    if (config.get_value("duration") == 0) {
        sent++; // initial separator
        // publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        for (size_t i = 0; i < config.get_value("messages"); ++i) {
            send(payload_len, mosq, message_ptr, topic, qos, sent, debug && print_debug(next_print));
        }
        measured_messages = sent - 1;
        std::chrono::time_point<std::chrono::steady_clock> deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(get_timeout(payload_len));
        while (mosquitto_published < sent) {
            if (std::chrono::steady_clock::now() > deadline) {
                if (debug) {
                    std::cerr << "Timeout waiting for message id: " << mosquitto_published << std::endl;
                }
                break;
            }
        }
    } else {
        size_t phase_0_messages = 0;
        // Starting phase: 0
        if (debug) {
            std::cout << " Starting phase " << std::endl;
        }
        auto end_time = get_phase_deadline(0);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_len, mosq, message_ignore_ptr, topic, qos, sent, debug && print_debug(next_print));
        }
        phase_0_messages = sent;
        // Measurement phase: 1
        if (debug) {
            std::cout << " Measurement phase " << std::endl;
        }

        end_time = get_phase_deadline(1);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_len, mosq, message_ptr, topic, qos, sent, debug && print_debug(next_print));
        }
        measured_messages = sent - phase_0_messages;
        // Cleanup phase: 2
        if (debug) {
            std::cout << " Cleanup phase " << std::endl;
        }
        end_time = get_phase_deadline(2);
        while (std::chrono::steady_clock::now() < end_time) {
            send(payload_len, mosq, message_ignore_ptr, topic, qos, sent, debug && print_debug(next_print));
        }
    }
    return measured_messages;
}


std::string publish_mosquitto(const std::string &message, const int qos) {
    /**
    * Send messages asynchronously and measure that time. After sending all messages send one empty payload and close
    * the connection.
    *
    * @message - payload to publish
    * @qos - Quality of Service
    *
    * Returns measurement as string of following format:
    * [number_of_messages,single-message_size,B/s,number_of_message/s]
    */
    mosquitto *mosq = get_mosquitto();
    const auto payload_len = message.size();
    std::string ignore = message;
    ignore.replace(0, 1, "!");

    // first non measured message
    publish_separator(*mosq);

    const auto start_time = std::chrono::steady_clock::now();

    const size_t sent_messages = perform_publishing_cycle(*mosq, message, qos);
    std::string measurement = process_measurement(start_time, payload_len, sent_messages);

    publish_separator(*mosq, true);
    mosquitto_lib_cleanup();
    return measurement;
}

std::string publish(const std::string &library, const std::string &message, int qos) {
    /**
    * Send messages based on given library.
    *
    * @library - currently only "mqtt" supported
    * @message - payload to publish
    *
    * Returns measurement as string of following format:
    * [number_of_messages,single-message_size,B/s,number_of_message/s]
    */
    if (library == "PAHO" || library == "paho") {
        return publish_paho(message, qos);
    } else if (library == "MOSQUITTO" || library == "mosquitto") {
        return publish_mosquitto(message, qos);
    }
    std::cerr << "Unsupported library: " << library << std::endl;
    return "";
}


std::vector<std::string> generate_messages(size_t min_size_in_kb, size_t max_size_in_kb) {
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
    std::string path = config.get_string("directory") + "/" + config.get_preset("qos") + "/" + std::to_string(
                           config.get_value("producers")) + "/" + config.get_string("output");
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
    for (const auto &argument: config.all_flags()) {
        printf("--%-15s %-90s : <%s>\n", argument.c_str(), config.get_description(argument).c_str(),
               config.get_preset(argument).c_str());
    }
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

int main(int argc, char *argv[]) {
    if (!parse_arguments(argc, argv, config)) {
        return 1;
    }
    if (config.is_true("fresh")) {
        clear_old_data(config.get_string("directory"));
    }
    const std::vector<std::string> messages = generate_messages(config.get_value("min"), config.get_value("max"));
    std::vector<std::string> measurements;
    measurements.reserve(messages.size());
    for (const auto &qos: parse_qos(config.get_string("qos"))) {
        config.set_preset("qos", qos);
        for (size_t i = 0; i < config.get_value("repetitions"); ++i) {
            for (const auto &message: messages) {
                measurements.emplace_back(publish(config.get_string("library"), message,
                                                  std::stoi(config.get_preset("qos"))));
            }
            store_string(format_output(measurements));
        }
    }
    return 0;
}
