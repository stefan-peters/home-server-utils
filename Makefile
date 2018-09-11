power-meter: sml/lib/libsml.a
	gcc -std=c11 power-meter.c libs/libsml/sml/lib/libsml.a -Ilibs/libsml/sml/include -Ilibs/libsml/examples -luuid -lm -lmosquitto -o power-meter

sml/lib/libsml.a:
	make -C libs/libsml