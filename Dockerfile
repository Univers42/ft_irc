# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Dockerfile                                         :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: dlesieur <dlesieur@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/08/24 06:04:30 by dlesieur          #+#    #+#              #
#    Updated: 2026/08/24 06:04:42 by dlesieur         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

FROM debian:bookworm-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make ca-certificates \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .


RUN make re

FROM build AS test
RUN make test

FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends libstdc++6 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd -r -u 10001 ircd
WORKDIR /app
COPY --from=build /src/build/bin/ircserv /app/ircserv
USER ircd
EXPOSE 6667

ENTRYPOINT ["/app/ircserv"]
CMD ["6667", "changeme"]
