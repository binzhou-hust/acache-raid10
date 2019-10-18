export MAKEROOT := $(shell pwd)
TARGET_DIR := common traces raids algorithms

include ./env.mk
define build_obj
for SubDir in $(TARGET_DIR); do \
	if ! [ -d $$SubDir ]; then \
		echo "The $$SubDir is not exist !"; \
		exit 11; \
	fi; \
	echo "Building $$SubDir ..."; \
	make -C $$SubDir ; \
	if [ $$? -ne 0 ]; then \
		echo "Build $$SubDir is failed !"; \
	fi; \
done
endef

all :
	@$(call build_obj)
	gcc -fPIC -o acache ./obj/*.o -lpthread -laio 
clean :
	-rm $(MAKEROOT)/obj/*.o ./acache
