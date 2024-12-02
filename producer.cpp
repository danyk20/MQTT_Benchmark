#include <iostream>
#include <string>
#include <cstdlib>
#include <mqtt/async_client.h>

void publishMQTT(const std::string &message, const std::string &topic = "test");

void publish(const std::string &protocol, const std::string &message) {
    if (protocol == "MQTT" || protocol == "mqtt") {
        publishMQTT(message);
    } else {
        std::cerr << "Unsupported protocol: " << protocol << std::endl;
    }
}

void publishMQTT(const std::string &message, const std::string &topic) {
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));

    mqtt::async_client client(brokerAddress + ":" + std::to_string(brokerPort), "test");

    auto connOpts = mqtt::connect_options_builder()
            .clean_session()
            .finalize();

    try {
        // Connect to broker
        std::cout << "Connecting to MQTT broker..." << std::endl;
        client.connect(connOpts)->wait();
        std::cout << "Connected to MQTT Broker!" << std::endl;

        // Publish message
        auto msg = mqtt::make_message(topic, message);
        client.publish(msg)->wait();
        std::cout << "Sent `" << message.size() << "` bytes to topic `" << topic << "`" << std::endl;

        // Disconnect client
        client.disconnect()->wait();
    } catch (const mqtt::exception &e) {
        std::cerr << "Failed to publish MQTT message: " << e.what() << std::endl;
    }
}


std::string generate_message(int size_in_kb) {

    const size_t memorySize = size_in_kb * 1024;
    char *buffer = new char[memorySize];

    std::fill(buffer, buffer + memorySize, '0');
    std::string message(buffer, memorySize);
    delete[] buffer;

    return message;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <protocol> <size_in_kb>" << std::endl;
        return 1;
    }

    const std::string protocol = argv[1];
    const std::string message = generate_message(atoi(argv[2]));

    publish(protocol, message);

    return 0;
}
