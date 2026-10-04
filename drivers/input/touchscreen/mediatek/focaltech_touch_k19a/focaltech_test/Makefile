obj-$(CONFIG_TOUCHSCREEN_FTS_XIAOMI) += focaltech_test.o
obj-$(CONFIG_TOUCHSCREEN_FTS_XIAOMI) += focaltech_test_ini.o
obj-$(CONFIG_TOUCHSCREEN_FTS_XIAOMI) += supported_ic/

# selftest reads whole frames into locals (as in selene 4.14)
CFLAGS_focaltech_test.o += -Wframe-larger-than=8192
