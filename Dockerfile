FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential libsqlite3-dev libssl-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile .
COPY src src
RUN make
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends apache2 libsqlite3-0 libssl3 && rm -rf /var/lib/apt/lists/* && a2dismod mpm_event && a2enmod mpm_prefork cgi headers env && rm /etc/apache2/sites-enabled/000-default.conf && mkdir -p /data && chown www-data:www-data /data
COPY --from=build /src/inventory /usr/lib/cgi-bin/inventory
COPY public/ /var/www/html/
COPY apache.conf /etc/apache2/sites-enabled/stockroom.conf
ENV INVENTORY_DB=/data/inventory.sqlite
WORKDIR /data
EXPOSE 80
CMD ["apachectl","-D","FOREGROUND"]
