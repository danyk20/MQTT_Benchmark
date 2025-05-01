# MQTT Benchmark

There is producer and consumer architecture where messages from producer go through the broker to the consumer.

![Publisher - Broker - Consumer](./diagrams/MQTT_Benchmark.svg)

The purpose of this benchmark is to measure throughput (Bytes/s) from producer to consumer. Measurement can be
parametrized by several key variables:

- Message size
- Quality of Service (QoS)
- MQTT implementation library
- Number of producers/consumers

## Prerequisites

Before getting started with the benchmark tools, install the required software dependencies:

```shell
sudo dnf install -y yum-utils
sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
sudo dnf install -y cmake gcc g++ openssl-devel docker-ce
sudo dnf groupinstall -y "Development Tools"
sudo systemctl start docker
```

### MQTT Libraries

In this benchmark, two distinct MQTT libraries were tested. These
are [eclipse-paho](https://github.com/eclipse-paho/paho.mqtt.cpp)
and [eclipse-mosquitto](https://github.com/eclipse-mosquitto/mosquitto).

The steps to install `eclipse-paho` are the following:

```shell
git clone https://github.com/eclipse/paho.mqtt.cpp
cd paho.mqtt.cpp
export PAHO_REPO_PATH=`pwd`
git checkout v1.5.2
git submodule init
git submodule update
cmake -Bbuild -H. -DPAHO_WITH_MQTT_C=ON -DPAHO_BUILD_EXAMPLES=ON
cmake --build build/ -j $nprocs
```

The steps to install `eclipse-mosquitto` are the following:

```shell
git clone https://github.com/eclipse-mosquitto/mosquitto.git
cd mosquitto
export MOSQUITTO_REPO_PATH=`pwd`
git checkout v2.0.21
git submodule init
git submodule update
cmake -Bbuild -H. -DDOCUMENTATION=OFF
cmake --build build/ -j $nprocs
```

---
:warning: Make sure to have the following environment variables set on your system:

- `MOSQUITTO_LIB_PATH`: path to `eclipse-mosquitto` library installation directory.
- `MOSQUITTO_INC_PATH`: path to `eclipse-mosquitto` include directory.
- `PAHO_LIB_PATH`: path to `eclipse-paho` library installation directory.
- `PAHO_INC_PATH`: path to `eclipse-paho` include directory.

If you followed the previous installations steps, these variables should be set as:

```shell
export MOSQUITTO_LIB_PATH="$MOSQUITTO_REPO_PATH/build/lib/"
export MOSQUITTO_INC_PATH="$MOSQUITTO_REPO_PATH/include/"
export PAHO_LIB_PATH="${PAHO_REPO_PATH}/build/src/;${PAHO_REPO_PATH}/build/externals/paho-mqtt-c/src/"
export PAHO_INC_PATH="${PAHO_REPO_PATH}/include/;${PAHO_REPO_PATH}/externals/paho-mqtt-c/src/"
```

---

## Setting up MQTT Broker

All software tools should now be installed on the system. The Next step is to start the MQTT brokers. These brokers act
as the middle men in the communication between producers and consumers. Therefore, these need to be operational in order
to perform the benchmark measurements.

These benchmark tools can work with any MQTT broker. There were used two different MQTT brokers that are separately
configured and spawned with Dockerfiles:

- [RabbitMQ Broker](./rabbitmq/)
    - [Configuration](./rabbitmq/rabbitmq.conf)

      TODO: ADD DETAILS ABOUT CONFIGURATION
    - [Dockerfile](./rabbitmq/Dockerfile)

      TODO: ADD DETAILS ABOUT DOCKERFILE

  To get this broker up and running, you must first build the docker image:

  ```shell
  sudo docker build -t rabbitmq-broker ./rabbitmq
  ```

  Now you can run the image like so:

  ```shell
  sudo docker run --rm -it -d --name rabbitmq -p 1888:1888 -p 5672:5672 -p 15672:15672 rabbitmq-broker
  ```

- [EMQX Broker](./emqx/)
    - [Configuration](./emqx/emqx.conf)

      TODO: ADD DETAILS ABOUT CONFIGURATION
    - [Dockerfile](./emqx/Dockerfile)

      TODO: ADD DETAILS ABOUT DOCKERFILE

  Similarly to the other broker, you need to build the image:

  ```shell
  sudo docker build -t emqx-broker ./emqx
  ```

  and run the broker as:

  ```shell
  sudo docker run --rm -it -d --name emqx -p 1883:1883 -p 8083:8083 -p 8084:8084 -p 8883:8883 -p 18083:18083 emqx
  ```

---
:warning: In case you plan to run both brokers at the same time, make sure to have them use different ports to listen for incoming TCP connections.

---

---
:+1: You can start and stop both brokers using Docker Compose if you have it installed.

Start both brokers:
```shell
   sudo docker compose up --build --detach
```

Stop both brokers:
```shell
   sudo docker compose down
```
---

## Compiling the Producer and Consumer

The steps to compile the producer and consumer applications are the following:

```shell
git clone https://github.com/danyk20/MQTT_Benchmark.git
cd MQTT_Benchmark
cmake -Bbuild -H. -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build build
```

You can now test the compilation was successful with:

```shell
./build/produce --help
```

and

```shell
./build/consume --help
```

## Executing the Producer and Consumer

Before running either of these applications, be sure to set the following environment variables:

```shell
export BROKER_IP=<broker_ip>
export MQTT_PORT=<broker_port> # port on which broker is listening
```

In order not to lose any messages, one should first start the consumer. A simple example of this is:

```shell
./build/consume --separators <number_of_measurements> --output <file_name> --consumers <number_of_consumers> --qos <QoS>
```

Once the consumer is running, you can start the producers with:

```shell
./build/produce --min <min_size_in_kb> --max <max_size_in_kb> --messages <number_of_messages> --period <delay_in_ms> --qos <QoS>
```

### Producer Flags

```console
Supported arguments flags:
--debug_period    Time between consecutive progress messages in seconds.                                     : <5>
--middle          beginning and end of the measurement will be cut off except middle part of [0-100]%        : <50>
--duration        number of seconds to send messages (exclusive with --messages flag)                        : <0>
--percentage      once buffer is full, wait until buffer is less than percentage [0-100]%                    : <50>
--timeout         timeout for each KB of payload in ms                                                       : <5>
--repetitions     number of times to run the all measurements all over again                                 : <1>
--messages        number of messages that will send per each payload size (exclusive with --duration flag)   : <400>
--min             minimum payload size in KB                                                                 : <72>
--period          minimum delay between 2 consecutive messages                                               : <80>
--directory       path to the directory where all measurements will be stored                                : <data/producer>
--password        authentication on broker                                                                   : <>
--buffer          max number of messages that can stored in the buffer                                       : <100>
--username        authentication on broker                                                                   : <>
--qos             Quality of Service, one or more values separated by comma                                  : <1>
--producers       number of producers involved (used for storage structure)                                  : <1>
--max             maximum payload size in KB                                                                 : <72>
--library         which if the supported libraries to use [paho/mosquitto]                                   : <paho>
--client_id       unique client identification                                                               : <>
--topic           topic to which messages will be published                                                  : <test>
--output          output file name                                                                           : <producer_results.txt>
--version         protocol version (currently supported only for paho)                                       : <3.1.1>
--separator       send separator after last message of each payload batch                                    : <True>
--min_timeout     minimum total timeout in ms                                                                : <1000>
--fresh           delete all previous measurements from output folder                                        : <False>
--debug           print debug messages, e.g. when buffer reaches the limit                                   : <False>
```

### Producer Examples

- Produce messages with payload sizes doubling from `--min` up to `--max` in kilobytes (e.g., 1, 2, 4, ..., 4096).
  Repeat each payload `--messages` times each. Send seperator message after each `--messages` messages have been sent.
  Use `--library` library to connect to broker. Repeat the whole measurement loop `--repetitions` times.

  ```shell
  ./build/produce --min 1 --max 8000 --messages 1000 --seperator True --library mosquitto --repetitions 5
  ```

- Produce messages for `--duration` seconds in periods of `--period` miliseconds. Connect to broker with credentials of
  `--username` and `--password`. By default, message payload size is 72 kilobytes. Only middle `--middle` percent of
  messages will be included in final measurement. The messages in the edges of the time window will be separators.
  Perform benchmark using each listed `--qos` quality of service.

  ```shell
  ./build/produce --username user --password pass --qos 0,1 --period 1000 --duration 30 --middle 80
  ```

- Produce `--messages` number of messages. Set capacity of local message buffer to `--buffer` messages. If this buffer
  is ever full, wait for `--timeout` miliseconds $\times$ aggregate buffer payload (in kilobytes) or until buffer
  `--percentage` percentage of buffer is available. Whichever happens first.

  ```shell
  ./build/produce --buffer 1000 --timeout 10 --percentage 40 --messages 10000
  ```

### Consumer Flags

```console
Supported arguments flags:
--report          how often should consumer report number of received messages when debug is True in seconds           : <30>
--duration        number of seconds to send messages (exclusive with --messages flag)                                  : <0>
--separators      number of separators to consume                                                                      : <1>
--consumers       number of consumers involved (used for storage structure)                                            : <1>
--directory       path to the directory where all measurements will be stored                                          : <data/producer>
--qos             Quality of Service, one or more values separated by comma                                            : <1>
--version         protocol version (currently supported only for paho)                                                 : <3.1.1>
--library         which if the supported library to use [paho]                                                         : <paho>
--ratio           ratio of overall duration that will be measured (starting phase is complement, ignored) [0-100]%     : <80>
--password        authentication on broker                                                                             : <>
--client_id       unique client identification                                                                         : <>
--username        authentication on broker                                                                             : <>
--topic           topic from which messages will be subscribed                                                         : <test>
--output          output file name                                                                                     : <consumer_results.txt>
--fresh           delete all previous measurements from output folder                                                  : <False>
--debug           print debug messages, e.g. separator arrived                                                         : <False>
```

### Consumer Examples

- Consume messages until `--separators` separators are consumed.

  ```shell
  ./build/consume --separators 10
  ```

- Consume messages for `--duration` seconds. Only include messages in measurement that arrived in last `--ratio` percent
  of full duration period.

  ```shell
  ./build/consume --duration 30 --ratio 70
  ```

### Synchronization of Producer and Consumer Flags

The producer-consumer architecture allows for a complete decoupling of the components that produce messages and those
that consume them. However, in order to perform meaningful benchmarks, there must exist some level of coupling between
the flags of the consumer and the producer.

- **Quality of Service** (`--qos`): This flag **must** be the same for both consumer and producer applications. There
  exist 3 categories of quality of service:

    - QoS 0: With this quality of service, the broker does not send acknowledgment for any message sent by the producer.
    - QoS 1: With this quality of service, producer receives acknowledgement from broker for every message produced.
    - QoS 2: With this quality of service, producer receives acknowledgement from consumer (via broker) for every
      message produced.

  At the time of testing, RabbitMQ MQTT broker does not support QoS 2.
- **Separators** (`--separators`): Separators are special message batches used to signal the consumer. The consumer will
  continue consuming messages until a predefined number of separators are consumed. Consider the following message
  stream where '0's are representing separators.

  ```text
  0000000111110000001111101111100111110000
  ```

  In this stream of messages, there are a total of four separators. The consumer will count consecutive stream of
  separators as a single occurrence and will ignore any separators until it receives a first valid message.

  Having this concept in mind is important for performing benchmarks as it allows the producer and consumer to "agree"
  on a message stream structure allowing for the results to be saved accordingly.

   ---
  :warning: If the consumer expects more separators, then the producer sends then it will run indefinitely.

  ---

## Troubleshooting

### CMAKE error: implicit instantiation of undefined template

**Error**:

```text
...  error: implicit instantiation of undefined template 'std::char_traits<unsigned char>' 
821 |   static_assert(is_same<_CharT, typename traits_type::char_type>::value,
```

or

```text
Clang incorrect version (17), please use only Clang 18!
```

**Explanation**: LLVM version is not compatible, download and use LLVM@18

- **Fix 1**:
  ```shell
  brew install llvm@18
  export CXX=/opt/homebrew/opt/llvm@18/bin/clang++
  export CC=/opt/homebrew/opt/llvm@18/bin/clang
  ```

### TCP/TLS connect failure

**Error**:

```text
Failed to publish MQTT messages: MQTT error [-1]: TCP/TLS connect failure
```

**Explanation**: The producer or consumer can't connect to the broker. This could happen for several reasons.

- **Fix 1**: [Follow these steps](#setting-up-mqtt-broker) to make sure the broker is running.
- **Fix 2**: Check if the `$BROKER_IP` is reachable and listening on `MQTT_PORT`.

### No more messages can be buffered

**Error**:

```text
<X>ms timeout waiting for message <Y>
...
Failed to publish MQTT messages: MQTT error [-12]: No more messages can be buffered
```

**Explanation**: The local buffer in the producer application is full and can't store more messages.

- **Fix 1**: Increase buffer capacity via the `--buffer` flag.
- **Fix 2**: Increase timeout via the `--timeout` flag.
    - **Might help**: Set minimum timeout via the `--min_timeout` flag in order to force timeout to be less dependent on
      smaller payload sizes.

### You have reached your unauthenticated pull rate limit

**Error**:

```text
 => ERROR [internal] load metadata for docker.io/library/rabbitmq:4.0-management              2.0s
------
 > [internal] load metadata for docker.io/library/rabbitmq:4.0-management:
------
Dockerfile:1
--------------------
   1 | >>> FROM rabbitmq:4.0-management
   2 |     COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf
   3 |     RUN rabbitmq-plugins enable rabbitmq_mqtt
--------------------
ERROR: failed to solve: rabbitmq:4.0-management: failed to resolve source metadata for docker.io/library/rabbitmq:4.0-management: failed to copy: httpReadSeeker: failed open: unexpected status code https://registry-1.docker.io/v2/library/rabbitmq/manifests/sha256:c405df63e4fb4fb5e2dfa2177dc367914f5f288b3b6b9ed959cfcc69908742c5: 429 Too Many Requests - Server message: toomanyrequests: You have reached your unauthenticated pull rate limit. https://www.docker.com/increase-rate-limit
```

**Explanation**: For some reason, you have reached a pull rate limit.

- **Fix 1**: Try again later.
- **Fix 2**: Try again much later.

## Visualisation

You can visualize your results using [MQTT Benchmark Plot](https://github.com/danyk20/MQTT_Benchmark_Plot) repo.
