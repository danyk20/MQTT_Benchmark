#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <mqtt/async_client.h>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>

#include "mosquitto.h"
#include <cstdio>
#include <unistd.h>

std::map<std::string, std::string> s_arguments = {
    {"debug", "False"}, // debug print
    {"fresh", "False"}, // remove all previous measurements
    {"separator", "True"}, // number of different message payloads sizes (except separator)
    {"output", "producer_results.txt"},
    {"topic", "test"}, // subscribed topic
    {"client_id", ""},
    {"library", "paho"}, //
    {"version", "3.1.1"},
    {"qos", "1"},
    {"username", ""},
    {"password", ""},
    {"directory", "data/producer"},
};

std::map<std::string, long> l_arguments = {
    {"period", 80}, // min delay between published messages
    {"messages", 400},
    {"buffer", 100}, // max number of messages in the buffer
    {"repetitions", 1},
    {"timeout", 5}, // wait timeout in ms per message per 1KB payload
    {"min_timeout", 10000}, // minimal timeout in ms
    {"qos", 1},
    {"min", 72}, // minimum payload size in KB
    {"max", 72}, // maximum payload size in KB
    {"percentage", 50}, // % between 1 and 100 buffer window size
    {"producers", 1},
    {"duration", 60}, // in seconds
    {"middle", 50}, // % between 1 and 100 duration of measurement
    {"debug_period", 5},
};

static bool is_reconnecting = false;
static long long mosquitto_published = 0;

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

std::vector<int> parse_qos(const std::string &input) {
    /**
     * @ input string input from the user
     *
     * supported are single valid QoS values or list of valid values separed by comma
     *
     * Returns list of QoS as int values
     */
    std::vector<int> numbers;
    std::stringstream ss(input);
    std::string temp;

    while (std::getline(ss, temp, ',')) {
        numbers.push_back(std::stoi(temp));
    }

    return numbers;
}

long get_timeout(const size_t payload) {
    /**
     * @ payload size in B
     * Returns timeout in miliseconds which is 1s or more based on Buffer size and payload size
     */
    const long timeout = l_arguments["timeout"] * l_arguments["buffer"] * static_cast<long>(payload / 1024);
    if (timeout < l_arguments["min_timeout"]) {
        return l_arguments["min_timeout"];
    }
    return timeout;
}

void publish_separator(mqtt::async_client &client, const bool disconnect = false) {
    /**
     * Send empty paylod as a separator for time measurment on the consumer and disconnect from the broker
     *
     * @client - connection object
     */
    const std::string empty_message;
    const auto msg = mqtt::make_message(s_arguments["topic"], empty_message);
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
    const char *topic = s_arguments["topic"].c_str();

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

std::string process_measurement(std::chrono::steady_clock::time_point start_time,
                                std::vector<std::shared_ptr<mqtt::message> >::size_type payload_size,
                                size_t number_of_messages) {
    /**
     * Calculate all attributes from the measuremnt and return them in a string of following format:
     * [number_of_messages,single-message_size,B/s,number_of_message/s]
     *
     * @start_time - when start sending of the first payload
     * @payload_size - how big was each payload
     * @number_of_messages - number of published messages
     */
    auto end_time = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time) / 1000.0;
    const auto message_per_seconds = static_cast<int>(number_of_messages) / duration.count();
    const auto throughput = message_per_seconds * static_cast<double>(payload_size);
    const std::string measurement = "[" + std::to_string(number_of_messages) + "," + std::to_string(payload_size) + ","
                                    + std::to_string(static_cast<int>(throughput)) + "," +
                                    std::to_string(static_cast<int>(message_per_seconds)) + "]";
    if (s_arguments["debug"] == "True") {
        std::cout << measurement << " - " << duration.count() << "s" << std::endl;
    }


    if (s_arguments["debug"] == "True") {
        std::cout << "Sent " << number_of_messages << " messages of size " << payload_size << " using QoS " <<
                s_arguments["qos"] << " bytes to topic '" << s_arguments["topic"] << "' in " << duration.count() << " s"
                << std::endl;
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

bool wait_for_buffer_dump(const long long sent, const long long percentage, const size_t payload_size) {
    /**
    * @ sent - number of messaged that have been sent regadless of their acknowledgment
    * @ percentage - size of the full baffer that is reaquired to become free
    * @ payload_size - message size
    * Returns true if there is free space in the buffer therwise false
    *
    * Waits until there is required percentage of buffer free or there is timeout - whichever comes first
    */
    const long long max_occupied_buffer = ((100 - percentage) * l_arguments["buffer"]) / 100;

    const std::chrono::time_point<std::chrono::steady_clock> deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(get_timeout(payload_size));

    while (std::chrono::steady_clock::now() < deadline) {
        if (sent - mosquitto_published < max_occupied_buffer) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const long long publishing_message = (mosquitto_published + 1);
    if (s_arguments["debug"] == "True") {
        std::cout << get_timeout(payload_size) << "ms timeout waiting for message " << publishing_message << std::endl;
    }
    return false;
}

bool wait_for_buffer_dump(const std::vector<std::shared_ptr<mqtt::token> > &tokens, size_t &last_published,
                          long long percentage, size_t payload_size) {
    /**
    * @ tokens - list of all tokens for messages that have been already sent asynchronously
    * @ last_published - index of the last confirmed token (message has been sent) from the tokens list
    * @ percentage - size of the full baffer that is reaquired to be free
    * @ payload_size - message size
    * Returns true if there is free space in the buffer therwise false
    *
    * Waits until there is required percentage of buffer free or there is timeout - whichever comes first
    */
    const long long available_buffer = (l_arguments["buffer"] * percentage) / 100;
    size_t middle_index = std::max(available_buffer, 1ll) + last_published;
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

std::chrono::time_point<std::chrono::steady_clock> get_phase_deadline(int phase) {
    /**
    * @ phase - number (0 - starting, 1- measutring, 2- cleanup)
    *
    * Calculates timepoint in future when should given phase end with assuption that it starts now. (cleanup and
    * starting phase have eaqual duration)
    *
    * Returns time_point when given phase should finish.
    */
    long phase_duration = 0;
    if (phase == 1) {
        phase_duration = l_arguments["duration"] * l_arguments["middle"] / 100;
    } else {
        phase_duration = l_arguments["duration"] * (100 - l_arguments["middle"]) / 200;
    }
    const std::chrono::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(phase_duration);
    return deadline;
}

void send(size_t payload_size, mosquitto &mosq, const char *msg, const char *topic, const int qos,
          long long &sent, const bool debug = false) {
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
    if (sent - mosquitto_published >= l_arguments["buffer"]) {
        // message buffer is full
        while (true) {
            if (!wait_for_buffer_dump(sent, l_arguments["percentage"], payload_size)) {
                std::cerr << "Buffer couldn't be freed!";
            }
            break;
        }
    }
    if (const int return_code = mosquitto_publish(&mosq, nullptr, topic, payload_size, msg, qos, false);
        return_code != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(return_code));
    } else {
        sent++;
    }
    const auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(l_arguments["period"]);
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
    if (tokens.size() - last_published >= l_arguments["buffer"]) {
        // message buffer is full
        while (true) {
            if (!wait_for_buffer_dump(tokens, last_published, l_arguments["percentage"], payload_size)) {
                if (client.is_connected()) {
                    throw std::runtime_error("Buffer reached limit!");
                }
                std::cerr << "Buffer couldn't be freed because the client is disconnected!";
                // TO DO remove too often print
            }
            break;
        }
    }
    const auto next_run_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(l_arguments["period"]);
    while (is_reconnecting) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    tokens.push_back(client.publish(msg));

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
        next_print = current_time + std::chrono::seconds(l_arguments["debug_period"]);
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
    const bool debug = s_arguments["debug"] == "True";
    std::chrono::time_point<std::chrono::steady_clock> next_print = std::chrono::steady_clock::now();

    if (l_arguments["duration"] == 0) {
        // Publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        for (auto i = 0; i < l_arguments["messages"]; ++i) {
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
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));

    std::vector<std::shared_ptr<mqtt::token> > tokens;
    constexpr long expected_throughput = 1000000000l; // max 1 GB
    const long expected_messages = l_arguments["duration"] * (
                                       expected_throughput / static_cast<long>(message.size()));
    tokens.reserve(std::max(expected_messages, l_arguments["messages"])); // to track async messages

    try {
        auto client = mqtt::async_client(brokerAddress + ":" + std::to_string(brokerPort), s_arguments["client_id"],
                                         static_cast<int>(l_arguments["buffer"]));

        client.set_connection_lost_handler(disconnected_handler);
        client.set_connected_handler(connected_handler);

        auto connOpts = mqtt::connect_options_builder()
                .clean_session()
                .automatic_reconnect(true)
                .mqtt_version(get_mqtt_version(s_arguments["version"]))
                .finalize();
        if (!s_arguments["username"].empty()) {
            connOpts.set_user_name(s_arguments["username"]);
        }
        if (!s_arguments["password"].empty()) {
            connOpts.set_password(s_arguments["password"]);
        }

        if (!client.connect(connOpts)->wait_for(get_timeout(0))) {
            std::cerr << "connect failed - timeout" << std::endl;
            return "Client could not connect - Connect timeout!";
        }

        // Pre-create the message to minimize allocation overhead
        auto mqtt_message = mqtt::make_message(s_arguments["topic"], message, qos, false);
        std::string ignore = message;
        ignore.replace(0, 1, "!");
        mqtt::message_ptr mqtt_ignore = mqtt::make_message(s_arguments["topic"], ignore, qos, false);

        // first non measured message
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
    printf("%lld connected: %s\n", std::chrono::steady_clock::now().time_since_epoch().count(),
           mosquitto_connack_string(reason_code));
    std::this_thread::sleep_for(std::chrono::seconds(5)); // TO DO Remove constant
    is_reconnecting = false;
}

void on_publish(struct mosquitto *mosq, void *obj, const int id) {
    mosquitto_published++;
}

void on_disconnect(struct mosquitto *mosq, void *p, int i) {
    is_reconnecting = true;
    printf("%lld Disconnected: %s\n", std::chrono::steady_clock::now().time_since_epoch().count(),
           mosquitto_connack_string(i));
}

mosquitto *get_mosquitto() {
    /**
     * Initilize Mosquitto client
     */
    mosquitto_lib_init();
    const char *client_id = s_arguments["client_id"].empty() ? nullptr : s_arguments["client_id"].c_str();
    mosquitto *mosq = mosquitto_new(client_id, true, nullptr);
    if (mosq == nullptr) {
        fprintf(stderr, "Error: Out of memory.\n");
        throw std::runtime_error("Fail to create client");
    }

    const char *username = s_arguments["username"].empty() ? nullptr : s_arguments["username"].c_str();
    const char *password = s_arguments["password"].empty() ? nullptr : s_arguments["password"].c_str();

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

long long perform_publishing_cycle(mosquitto &mosq, const std::string &message, const int qos) {
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
    const char *topic = s_arguments["topic"].c_str();
    const bool debug = s_arguments["debug"] == "True";
    long long measured_messages = 0;
    std::chrono::time_point<std::chrono::steady_clock> next_print = std::chrono::steady_clock::now();

    long long sent = 0;
    if (l_arguments["duration"] == 0) {
        sent++; // initial separator
        // Publish pre-created messages NUMBER_OF_MESSAGES times asynchronously
        for (long long i = 0; i < l_arguments["messages"]; ++i) {
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
        long long phase_0_messages = 0;
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

    const long long sent_messages = perform_publishing_cycle(*mosq, message, qos);
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
    std::string path = s_arguments["directory"] + "/" + std::to_string(l_arguments["qos"]) + "/" + std::to_string(
                           l_arguments["producers"]) + "/" + s_arguments["output"];
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
        std::cout << "  --" << argument.first << ((argument.first == "debug") ? "" : " <value>") << std::endl;
    }
    for (const auto &argument: l_arguments) {
        std::cout << "  --" << argument.first << " <value>" << std::endl;
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
        } else if ((arg == "-u" || arg == "--username") && i + 1 < argc) {
            s_arguments["username"] = argv[++i];
        } else if ((arg == "-p" || arg == "--password") && i + 1 < argc) {
            s_arguments["password"] = argv[++i];
        } else if ((arg == "-c" || arg == "--client_id") && i + 1 < argc) {
            s_arguments["client_id"] = argv[++i];
        } else if (arg == "--debug" || arg == "-d") {
            s_arguments["debug"] = "True";
        } else if (arg == "--fresh" || arg == "-f") {
            s_arguments["fresh"] = "True";
        } else if ((arg == "-s" || arg == "--separator") && i + 1 < argc) {
            s_arguments["separator"] = argv[++i];
        } else if (arg == "--library" && i + 1 < argc) {
            s_arguments["library"] = argv[++i];
        } else if (arg == "--directory" && i + 1 < argc) {
            s_arguments["directory"] = argv[++i];
        } else if (arg == "--version" && i + 1 < argc) {
            s_arguments["version"] = argv[++i];
        } else if ((arg == "-q" || arg == "--qos") && i + 1 < argc) {
            s_arguments["qos"] = argv[++i];
        } else if ((arg == "--debug_period") && i + 1 < argc) {
            l_arguments["debug_period"] = std::stol(argv[++i]);
        } else if ((arg == "--period") && i + 1 < argc) {
            l_arguments["period"] = std::stol(argv[++i]);
        } else if ((arg == "-b" || arg == "--buffer") && i + 1 < argc) {
            l_arguments["buffer"] = std::stol(argv[++i]);
        } else if ((arg == "-r" || arg == "--repetitions") && i + 1 < argc) {
            l_arguments["repetitions"] = std::stol(argv[++i]);
        } else if ((arg == "--timeout") && i + 1 < argc) {
            l_arguments["timeout"] = std::stol(argv[++i]);
        } else if ((arg == "--min") && i + 1 < argc) {
            l_arguments["min"] = std::stol(argv[++i]);
        } else if ((arg == "--max") && i + 1 < argc) {
            l_arguments["max"] = std::stol(argv[++i]);
        } else if ((arg == "--percentage") && i + 1 < argc) {
            l_arguments["percentage"] = std::stol(argv[++i]);
        } else if ((arg == "--producers") && i + 1 < argc) {
            l_arguments["producers"] = std::stol(argv[++i]);
        } else if ((arg == "--middle") && i + 1 < argc) {
            l_arguments["middle"] = std::stol(argv[++i]);
        } else if ((arg == "-m" || arg == "--messages") && i + 1 < argc) {
            l_arguments["messages"] = std::stol(argv[++i]);
            l_arguments["duration"] = 0;
        } else if ((arg == "--duration") && i + 1 < argc) {
            l_arguments["duration"] = std::stol(argv[++i]);
            l_arguments["messages"] = 0;
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
            if (s_arguments["debug"] == "True") {
                std::cout << "Old measurements cleared successfully." << std::endl;
            }
        } else {
            std::cerr << "Directory does not exist: " << dirPath << std::endl;
        }
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char *argv[]) {
    if (!set_parameters(argc, argv)) {
        return 1;
    }
    if (s_arguments["fresh"] == "True") {
        clear_old_data(s_arguments["directory"]);
    }
    const std::vector<std::string> messages = generate_messages(l_arguments["min"], l_arguments["max"]);
    std::vector<std::string> measurements;
    measurements.reserve(messages.size());
    for (const auto &qos: parse_qos(s_arguments["qos"])) {
        l_arguments["qos"] = qos;
        for (int i = 0; i < l_arguments["repetitions"]; ++i) {
            for (const auto &message: messages) {
                measurements.emplace_back(publish(s_arguments["library"], message,
                                                  static_cast<int>(l_arguments["qos"])));
            }
            store_string(format_output(measurements));
        }
    }
    return 0;
}
