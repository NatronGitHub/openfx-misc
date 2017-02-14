PRESETS = \
tex00.jpg \
tex01.jpg \
tex02.jpg \
tex03.jpg \
tex04.jpg \
tex05.jpg \
tex06.jpg \
tex07.jpg \
tex08.jpg \
tex09.jpg \
tex10.png \
tex11.png \
tex12.png \
tex14.png \
tex15.png \
tex16.png \
tex17.jpg \
tex18.jpg \
tex19.png \
tex20.jpg \
tex21.png \
vid00.ogv \
vid01.webm \
vid02.ogv \
vid03.webm \
cube00_0.jpg \
cube01_0.png \
cube02_0.jpg \
cube03_0.png \
cube04_0.png \
cube05_0.png

all: $(PRESETS)

$(PRESETS):
	curl -s -S -L -O https://www.shadertoy.com/presets/$@

clean:
	rm -f $(PRESETS)
