FROM rabbitmq:4.0-management
COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf
# turn on MQTT protocol on RabbitMQ
RUN rabbitmq-plugins enable rabbitmq_mqtt
# 1888-MQTT, 5671-AMQP, 15672-management GUI
EXPOSE 1888 5672 15672