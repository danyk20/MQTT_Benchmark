FROM rabbitmq:4.0-management
COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf
RUN rabbitmq-plugins enable rabbitmq_mqtt # turn on MQTT protocol on RabbitMQ
EXPOSE 1888 5672 15672 # 1888 - MQTT 5671 - AMQP 15672 - management GUI