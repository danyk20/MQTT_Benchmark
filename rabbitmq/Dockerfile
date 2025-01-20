FROM rabbitmq:4.0-management
COPY rabbitmq.conf /etc/rabbitmq/rabbitmq.conf
RUN rabbitmq-plugins enable rabbitmq_mqtt
EXPOSE 1888 5672 15672