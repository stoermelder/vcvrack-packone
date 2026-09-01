
CHECK_SOURCES += $(wildcard src/*.?pp src/**/*.?pp src/**/**/*.?pp)

cppcheck:
	cppcheck $(CHECK_SOURCES) $(filter -I% -D%,$(FLAGS)) \
		--suppress=*:*dep/* --suppress=*:*src/test* \
		--std=c++11 --max-configs=1 --enable=warning -j 8 -q --xml