CC := gcc -ggdb3
#directors containing header files
INCLUDE_DIR := \
-I $(MAKEROOT)/common \
-I $(MAKEROOT)/traces \
-I $(MAKEROOT)/algorithms \
-I $(MAKEROOT)/raids \
-lpthread -laio

CFLAGS := $(INCLUDE_DIR)
%.o : %.c
	${CC} ${CFLAGS} -c $< -o ${MAKEROOT}/obj/$@


