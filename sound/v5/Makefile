ifeq ($(CONFIG_SND_FTSSP010_AC97),y)
snd-ftssp010-objs := FTSSP010_ALSA.o FTSSP010_lib.o
endif
ifeq ($(CONFIG_SND_FTSSP010_I2S),y)
snd-ftssp010-objs := FTSSP010_ALSA.o FTSSP010_lib.o
endif
ifeq ($(CONFIG_SND_FTSSP010_HDA),y)
snd-ftssp010-objs := FTSSP010_HDA.o FTSSP010_HDA_lib.o
endif
obj-$(CONFIG_SND_FTSSP010) += snd-ftssp010.o
