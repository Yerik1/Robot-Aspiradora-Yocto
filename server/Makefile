CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=gnu11 -O2 -D_GNU_SOURCE -Isrc -Imongoose
LDFLAGS ?= -lssl -lcrypto -lm -lpthread

SRCS = src/main.c \
       src/rpc_handlers.c \
       src/auth.c \
       src/crypto_util.c \
       src/sim_robot.c \
       mongoose/mongoose.c

OBJS = $(SRCS:.c=.o)
TARGET = robot-server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) users.db

.PHONY: all clean
