# Builds the project. Grading overrides CC and CFLAGS (for example, to add
# -fsanitize=thread), so keep using them in every rule.
CC     = gcc
CFLAGS = -std=c17 -Wall -Wextra -pthread -g

# Every object file except main.o. Add one for each .c file you write
# (not tester.c or test_bldg.c, which have their own main()).
OBJS   = tools.o

# Every header file. Add any that you write.
HEADERS = defs.h tools.h common.h

hunt: main.o $(OBJS)
	$(CC) $(CFLAGS) -o hunt main.o $(OBJS)

# Loads one building and prints it with your building_print().
test_bldg: test_bldg.o $(OBJS)
	$(CC) $(CFLAGS) -o test_bldg test_bldg.o $(OBJS)

# Checks the recordings your last run left in this folder.
tester: tester.o tools.o
	$(CC) $(CFLAGS) -o tester tester.o tools.o

# Each .o is compiled from the .c of the same name ($< is that .c file).
# Changing any header in HEADERS rebuilds every object file.
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

# Compares test_bldg's output for each building with data/tests/.
test-bldg: test_bldg
	@for f in data/tests/*.expected; do \
	    b=$$(basename $$f .expected); \
	    if ./test_bldg data/$$b.bldg 2> /dev/null | diff -q $$f - > /dev/null; then echo "PASS $$b"; \
	    else echo "FAIL $$b (to see why: ./test_bldg data/$$b.bldg | diff $$f -)"; fi; \
	done

clean:
	rm -f *.o hunt test_bldg tester rec-*.bin

# These targets are commands to run, not files to build.
.PHONY: clean test-bldg
