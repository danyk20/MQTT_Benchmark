FROM emqx/emqx-enterprise:5.8.4
COPY ../emqx/emqx.conf /opt/emqx/etc/emqx.conf
RUN rabbitmq-plugins enable rabbitmq_mqtt
EXPOSE 1883 8083 8084 8883 18083