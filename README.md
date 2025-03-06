# MQTT Benchmark

Publish and consume messages of different payloads and evaluate how long does it take.

## Prerequisites

- Install cmake
- ```shell
   sudo dnf install -y cmake
   ```

- Install Compiler
- ```shell
   sudo dnf groupinstall -y "Development Tools"
   sudo dnf install -y gcc gcc-c++
   ```

- Install OpenSSL
- ```shell
   sudo dnf install -y openssl-devel
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

- Install & run Docker
- ```shell
   sudo dnf install -y yum-utils  
   sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
   sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
   sudo systemctl start docker
   sudo systemctl enable docker
   ```

- Setup MQTT brokers with Docker Compose or manually

a) -  ```shell
      sudo docker compose up --build --detach
      ```

b) -  ```shell
      sudo docker build -t rabbitmq-benchmark:0.0.1 ./rabbitmq
      sudo docker run --rm -it -d --name rabbitmq -p 1888:1888 -p 5672:5672 -p 15672:15672 rabbitmq-benchmark:0.0.1
      sudo docker build -t emqx:0.0.1 ./emqx
      sudo docker run --rm -it -d --name emqx -p 1883:1883 -p 8083:8083 -p 8084:8084 -p 8883:8883 -p 18083:18083 emqx:0.0.1
      ```

## Compilation

0. Clone & open project
   ```shell
   git clone https://github.com/danyk20/MQTT_Benchmark.git
   cd MQTT_Benchmark
   ```

1. Export environment variables
    ```shell
    export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH BROKER_IP=<broker_ip> MQTT_PORT=<broker_port>
    ```

2. Compile sources

    ```shell
    g++ -o produce producer.cpp -L /usr/local/lib64 -lpaho-mqttpp3 -lpaho-mqtt3c
    g++ -o consume consumer.cpp -L /usr/local/lib64 -lpaho-mqttpp3 -lpaho-mqtt3c
    ```

3. Run consumer
   `./consume --separators <number_of_measurements> --output <file_name> --consumers <number_of_consumers> --qos <QoS>`

    ```shell
    ./consume --separators 11 --output consumer_93.txt --consumers 1 --qos 0
    ```
4. Run producer
   `./produce --min <min_size_in_kb> --max <max_size_in_kb> --messages <number_of_messages> --period <delay_in_ms> --qos <QoS>`

   ```shell
   ./produce --period 0 --min 1 --max 8192 --messages 1000 --qos 1
    ```
    - Note: This example will send 14 different payload size: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
      8192

### Producer

Publish messages of sizes from given range. Range specify the minimum and maximum size of the payload that will be
published. Maximum payload size is smaller or equal to the larges payload that will be sent. Producer sends this
messages in `NUMBER_OF_REPETITIONS` repetitions and in each repetition each message size will be sent
`NUMBER_OF_MESSAGES` times.

e.g. message sizes based on given range:

- `Min` - 1; `Max` 10: `[1,2,4,8]`
- `Min` - 1; `Max` 16: `[1,2,4,8,16]`

Test support up to three levels of QoS (exact number depends on the broker support):

- At most  (QoS `0`)

- At least once (QoS `1`)

- Exactly once (QoS `2`)

These levels correspond to increasing levels of reliability for message delivery. QoS `0` may lose messages, QoS `1`
guarantees the message delivery but potentially exists duplicate messages, and QoS `2` ensures that messages are
delivered exactly once without duplication. As the QoS level increases, the reliability of message delivery also
increases, but so does the complexity of the transmission process.

#### Pseudocode

```text
messages = generate_messages(min_size, max_size)
for message in messages:
  client = connect(ip, port, protocol_version)
  payload = pack(topic, message, qos)
  measurement = perform_publishing_cycle(limit)
  store(measurement)
```

- time restricted measurement

```text
perform_publishing_cycle(limit):
  while(starting_phase_duration(limit)):
    send(ignored_data)

  while(measurement_phase_duration(limit)):
    send(measured_data)

  while(cleanup_phase_duration(limit)):
    send(ignored_data)

```

- message restricted measurement

```text
perform_publishing_cycle(limit):
  send(ignored_data)
  for i in range(limit):
    send(measured_data)
  send(ignored_data)
```

- send(data)

```text
if (buffer_full):
  wait(required_space)
next_message_timestamp = now() + period
buffer(publish(data))
sleep_until(next_message_timestamp)
```

#### Flags

};

- `debug` - print debug print when buffer reach the limit: *False*
- `protocol` - which protocol to use *MQTT*
- `separator` - send separator after last message of each payload batch *True*
- `output` - output file: *consumer_results.txt*
- `topic` - topic to subscribe: *test*
- `client_id` - client id: *\<blank>*
- `username` - authentication on broker *\<blank>*
- `password` - authentication on broker *\<blank>*
- `min` - minimum payload size *72*KB
- `max` - maximum payload size *72*KB
- `qos` - QoS list of more values separated by comma *1*
- `timeout` - timeout for each KB of payload *5*s
- `min_timeout` - minimum total timeout *10000*ms
- `repetitions` - number of times to run the script *1*
- `buffer` - max number of messages that can stored in the buffer *100*
- `messages` - number of messages that will send per each payload size *400*
- `period` - minimum delay between 2 messages *80*ms
- `percentage` - once is buffer full wait until buffer is less than percentage % full *50*
- `producers` - number of producers involved (used for storage structure): *1*
- `version` - protocol version: *3.1.1*
- `duration` - messages will be constantly sending for: *60*s
- `middle` - begging and end of the measurement will be cut off except middle part of: *50*%
    - look into `time restricted measurement` pseudocode above

#### Troubleshooting

- Broker is not running:
    - ```text
        Failed to publish MQTT messages: MQTT error [-1]: TCP/TLS connect failure
      ```
    - fix : ``` sudo docker compose up --build --detach ```

- Client buffer is full:
    - ```text
        <X>ms timeout waiting for message <Y>
        ...
        Failed to publish MQTT messages: MQTT error [-12]: No more messages can be buffered
    ```
    - fix : ``` ./produce --timeout <increased_value> ... ``` or ``` ./produce --min_timeout <increased_value> ... ```

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
- `client_id` - client id: *\<blank>*
- `consumers` - number of consumers involved (used for storage structure): *\1*
- `version` - protocol version: *\3.1.1*
- `qos` - QoS list of more values separated by comma *1*
- `username` - authentication on broker *\<blank>*
- `password` - authentication on broker *\<blank>*
- `duration` - total measurement duration (disabled by default) *0*s
- `ratio` - ratio of overall duration that will be measured (starting phase is complement before this phase nad it will
  not be measured) *80*%

## Visualisation

You can visualise your results using [MQTT Benchmark Plot](https://github.com/danyk20/MQTT_Benchmark_Plot) repo.

#### Pseudocode

```text

for current_qos in given_qoses:
  client = connect(ip, port, protocol_version)
  client.subscribe(topic, qos)
  while (measuring):
    measuring = measurement(message)
  store_results()
    
```

- separator restricted measurement

```text
separation = process_payload(received_messages, message);
if (first_message_arrived() && !separation) {
        start_time = now(); 
} else if (separation && received_messages > 0) {
        add_measurement(start_time, received_messages);
        received_messages = 0; 
}

return measurements.size() < expected_separators
```

- time restricted measurement

```text
    current_time = now();
    if (phases.empty()) {
        starting_phase = get_phase_deadline(0);
        measuring_phase = get_phase_deadline(1, starting_phase);
    }
    if (current_time > measuring_phase) {
        add_measurement(start_time, received_messages, message.size());
        return false;
    }
    if (current_time > starting_phase) {
        received_messages++;
    }
    return true;
    
```
