# MQTT Benchmark

Publish and consume messages of different payloads and evaluate how long does it take.

## Prerequisites

- Install cmake
- ```shell
   sudo dnf install -y cmake
   ```

- Install [eclipsie-paho](https://github.com/eclipse-paho/paho.mqtt.cpp)
- ```shell
   git clone https://github.com/eclipse/paho.mqtt.cpp
   cd paho.mqtt.cpp
   git checkout v1.4.0
   git submodule init
   git submodule update
   cmake -Bbuild -H. -DPAHO_WITH_MQTT_C=ON -DPAHO_BUILD_EXAMPLES=ON
   sudo cmake --build build/ --target install
   cd ..
   ```

- Install Docker
- ```shell
   sudo dnf install -y yum-utils  
   sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
   sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
   ```

- Setup MQTT broker
-  ```shell
   docker build -t rabbitmq-benchmark:0.0.1 .
   docker run --rm -it -d --name rabbitmq -p 1883:1888 -p 5672:5672 -p 15672:15672 rabbitmq-benchmark:0.0.1
   ```

## Compilation

1. Export environment variables
    ```shell
    BROKER_IP=<broker_ip> MQTT_PORT=<broker_port> DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH
    ```

2. Compile sources

    ```shell
    g++ -o produce producer.cpp -L /usr/local/lib64 -lpaho-mqttpp3 -lpaho-mqtt3c
    g++ -o consume consumer.cpp -L /usr/local/lib64 -lpaho-mqttpp3 -lpaho-mqtt3c
    ```

3. Run consumer `./consume`

    ```shell
    ./consumer --debug --separators 10
    ```
4. Run producer `./produce <protocol> <min_size_in_kb> <max_size_in_kb> <QoS>`

   ```shell
   ./produce --min 1 --max 1024 --messages 1000 --period 0
    ```
    - Note: This example will send 14 different payload size: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
      8192

### Producer

Publish messages of sizes from given range. Range specify the minimum and maximum size of the payload that will be
published. Maximum payload size is smaller or equal to the larges payload that will be sent. Producer sends this
messages in `NUMBER_OF_REPETITIONS` repetitions and in each repetition each message size will be sent
`NUMBER_OF_MESSAGES` times.

e.g. message sizes based on given range:

- `Min` - 1; `Max` 10: `[1,2,4,8,16]`
- `Min` - 1; `Max` 16: `[1,2,4,8,16]`

Test support up to three levels of QoS (exact number depends on the broker support):

- At most  (QoS `0`)

- At least once (QoS `1`)

- Exactly once (QoS `2`)

These levels correspond to increasing levels of reliability for message delivery. QoS `0` may lose messages, QoS `1`
guarantees the message delivery but potentially exists duplicate messages, and QoS `2` ensures that messages are
delivered exactly once without duplication. As the QoS level increases, the reliability of message delivery also
increases, but so does the complexity of the transmission process.

#### Flags
};

- `debug` - print debug print when buffer reach the limit: *False*
- `protocol` - which protocol to use *MQTT*
- `separator` - send separator after last message of each payload batch *True*
- `output` - output file: *consumer_results.txt*
- `topic` - topic to subscribe: *test*
- `client` - client id: *\<blank>*
- `min` - minimum payload size *72*KB
- `max` - minimum payload size *72*KB
- `qos` - QoS *1*
- `timeout` - timeout for wait() *5000*ms
- `repetitions` - number of times to run the script *1*
- `buffer` - max number of messages that can stored in the buffer *100*
- `messages` - number of messages that will send per each payload size *400*
- `period` - minimum delay between 2 messages *80*ms
- `percentage` - once is buffer full wait until buffer is less than percentage % full *50*

### Consumer

Run indefinitely but store new measurement data each time when the number of measurements (the only argument) is
reached. Number of measurements counts how many separators (message with empty payload) arrives. Several separator in a
row are counted just once.

e.g. received payload's sizes:

- 1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0, = 5 separators
- 1,1,1,1,1,0,0,0,0,,0,,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0, = 4 separators

#### Flags

- `debug` - print debug print after each separator: *False*
- `separators` - number of separators to consume: *1*
- `output` - output file: *consumer_results.txt*
- `topic` - topic to subscribe: *test*
- `client` - client id: *\<blank>*

## Visualisation

You can visualise your results using [MQTT Benchmark Plot](https://github.com/danyk20/MQTT_Benchmark_Plot) repo.

## Default Version

MQTT: 3.1.1
QoS: 0
