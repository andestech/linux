ifeq ($(CONFIG_SND_FTSSP010_AC97),y)
snd-ftssp010-objs := ftssp010_alsa.o ftssp010_lib.o
endif
ifeq ($(CONFIG_SND_FTSSP010_I2S),y)
snd-ftssp010-objs := ftssp010_alsa.o ftssp010_lib.o
endif

obj-$(CONFIG_SND_FTSSP010) += snd-ftssp010.o ftssp010_dma.o
