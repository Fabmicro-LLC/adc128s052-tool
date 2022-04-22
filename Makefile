
CFLAGS += -O2 -Wall -std=c99 -g -D_GNU_SOURCE -I./


adc128s052-tool: adc128s052-tool.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f adc128s052-tool 

