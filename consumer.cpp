#include <mqtt/client.h>
#include <ostream>

[[noreturn]] int main()
{
    const std::string brokerAddress = std::getenv("BROKER_IP");
    const int brokerPort = std::stoi(std::getenv("MQTT_PORT"));
    std::string broker = brokerAddress + ":" + std::to_string(brokerPort);
    std::string id = "";
    mqtt::client client(broker, id, mqtt::create_options(MQTTVERSION_5));
    client.connect();
    client.subscribe("test");
    client.start_consuming();
    while (true)
    {
        mqtt::const_message_ptr messagePointer;

        if (client.try_consume_message(&messagePointer))
        {
            std::string messageString = messagePointer -> get_payload_str();
            std::cout << messageString << std::endl;
        }
    }
}