msp430_ss = ss.source_set()
msp430_ss.add(when: 'CONFIG_MSP430_MCU', if_true: files('bcmp.c', 'fllp.c', 'mpy.c'))

hw_arch += {'msp430': msp430_ss}
