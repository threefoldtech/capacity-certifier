EXEC = grid-cpubench
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

# Using -march=westmere disable AVX
# Execution time is slower using AVX than SSE instruction set

CFLAGS += -g -std=gnu99 -O0 -W -Wall -Wextra -Wno-implicit-fallthrough -march=westmere -fopenmp
LDFLAGS += -fopenmp

all: $(EXEC)

release: CFLAGS += -DRELEASE -O2
release: clean $(EXEC)

static: CFLAGS += -DRELEASE -O2
static: LDFLAGS += -static
static: clean $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) *.o

mrproper: clean
	$(RM) $(EXEC)
