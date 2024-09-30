CC=riscv64-linux-gcc

ECHO = echo_test
ECHO_OBJS = echo_test.o rpmsg_demo_ctrl.o

MAT_MUL = mat_mul_demo
MAT_MUL_OBJS = mat_mul_demo.o rpmsg_demo_ctrl.o

PROXY = proxy_app
PROXY_OBJS = proxy_app.o rpmsg_demo_ctrl.o

CFLAGS += -Wall -Werror

all: $(ECHO) $(MAT_MUL) $(PROXY)

$(ECHO): $(ECHO_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $(ECHO_OBJS) $(LDLIBS)

$(MAT_MUL): $(MAT_MUL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $(MAT_MUL_OBJS) $(LDLIBS) -lpthread

$(PROXY): $(PROXY_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $(PROXY_OBJS) $(LDLIBS)

clean:
	rm -rf $(ECHO) $(MAT_MUL) $(PROXY) *.o

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<
