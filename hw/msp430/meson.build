msp430_ss = ss.source_set()
msp430_ss.add(when: 'CONFIG_MSP430_MCU', if_true: files('bcmp.c', 'fllp.c', 'mpy.c', 'msp430.c'))
msp430_ss.add(when: 'CONFIG_LAUNCHPAD', if_true: files('launchpad.c'))

hw_arch += {'msp430': msp430_ss}
