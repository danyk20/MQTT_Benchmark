RabbitMQ Quorum queues

The MQTT plugin creates a classic queue, quorum queue, or MQTT QoS 0 queue per MQTT subscriber. By default, the plugin
creates a classic queue. The plugin can be configured to create quorum queues (instead of classic queues) for
subscribers whose MQTT session lasts longer than their MQTT network connection.

To enable quorum queues, consumer must subscribe to the given topic with clean_seasion set to
False (MQTT session outlasts the MQTT network connection), QoS set to 1 and `client_id` flag must be provided.

While quorum queues are designed for data safety and predictable efficient recovery from replica failures, they also
have downsides. A quorum queue by definition requires at least three replicas in the cluster. Therefore quorum queues
take longer to declare and delete, and are not a good fit for environments with high client connection churn or
environments with many (hundreds of thousands) subscribers.

# RabbitMQ Quorum Queues and MQTT QoS 0 Queues

This document provides an overview of RabbitMQ quorum queues in the context of MQTT subscriptions, and also briefly
touches upon MQTT QoS 0 queues.

## RabbitMQ Quorum Queues for MQTT Subscribers

The RabbitMQ MQTT plugin allows for the creation of different queue types for each MQTT subscriber. By default, it
creates a classic queue. However, it can be configured to use quorum queues for subscribers that maintain an MQTT
session longer than their network connection.

### Key Characteristics of Quorum Queues with MQTT

- *Enabling Quorum Queues*: To utilize quorum queues for an MQTT subscriber, the following conditions must be met:
    - The MQTT consumer must subscribe to the topic with `clean_session` set to `False`. This ensures the MQTT session
      persists beyond the network connection.
    - The MQTT consumer must subscribe with a Quality of Service (QoS) level of `1`.
- *Purpose* : Quorum queues are specifically designed to enhance data safety and provide predictable, efficient recovery
  from replica failures within a RabbitMQ cluster.
- *Requirements*: A fundamental requirement for quorum queues is that they require at least three replicas in the
  cluster to function correctly.
- *Downsides and Considerations*: While offering significant advantages in terms of data safety, quorum queues also have
  notable drawbacks:
    - Lower throughput: Each message need to be stored in persistent memory (disk) and synchronized between nodes.
    - Many (hundreds of) subscribers: The overhead of managing a large number of quorum queues can be significant.

## MQTT QoS 0 Queues

In addition to classic and quorum queues, the MQTT plugin can also create MQTT QoS 0 queues. These queues are
specifically for MQTT subscribers using Quality of Service level 0. Messages published with QoS 0 offer no guarantees of
delivery and are not persisted by the broker after transmission. As such, queues associated with QoS 0 subscriptions are
typically transient and are not designed for message persistence or reliability.

---

## Queue Naming Conventions and Configuration

### Queue Name Convention

RabbitMQ follows a specific naming convention for MQTT subscription queues:

`mqtt-subscription-<client_id>qos<qos_level>`

For example, a client with the ID `myClient123` subscribing with QoS 1 would have a queue named
`mqtt-subscription-myClient123qos1`.

### Changing Maximum Queue Length

To modify the maximum length of a queue, you need to update the `max-length` parameter in the `definition.json` file,
specifically under `policies.definition`.

---

## Monitoring Queues with RabbitMQ Management UI

The **RabbitMQ Management UI** provides a graphical interface for monitoring and managing your RabbitMQ cluster,
including all queues.

* **Accessing the UI**: Typically, the Management UI is accessible via your web browser at
  `http://<your-rabbitmq-server-ip>:15672`. (The port `15672` is the default for the Management UI).
* **Checking Queues**: Once logged in, navigate to the **"Queues and Streams"** tab. Here, you will see a list of all
  queues, including those created for MQTT subscriptions (e.g., `mqtt-subscription-myClient123qos1`). You can inspect
  various metrics such as message rates, consumers, and queue type.