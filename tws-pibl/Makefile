CC      = gcc
CFLAGS  = -Wall
LDFLAGS = -lpthread

TWS_SRC = tws/tws.c tws/handler.c tws/http_parser.c tws/logger.c tws/mime.c tws/net_utils.c
TWS_OBJ = $(TWS_SRC:.c=.o)
TWS_BIN = tws/tws

PIBL_SRC = pibl/pibl.c pibl/proxy.c pibl/cache.c pibl/balancer.c pibl/config.c pibl/logger.c pibl/net_utils.c
PIBL_OBJ = $(PIBL_SRC:.c=.o)
PIBL_BIN = pibl/pibl

all: $(TWS_BIN) $(PIBL_BIN)

tws: $(TWS_BIN)
pibl: $(PIBL_BIN)

$(TWS_BIN): $(TWS_OBJ)
	$(CC) $(TWS_OBJ) -o $@ $(LDFLAGS)

$(PIBL_BIN): $(PIBL_OBJ)
	$(CC) $(PIBL_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TWS_OBJ) $(PIBL_OBJ) $(TWS_BIN) $(PIBL_BIN)

.PHONY: all tws pibl clean
