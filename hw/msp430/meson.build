msp430_ss = ss.source_set()
msp430_ss.add(when: 'CONFIG_MSP430_MCU', if_true: files(
    'bcm.c',
    'fllp.c',
    'mpy.c',
    'msp430.c',
))
msp430_ss.add(when: 'CONFIG_SOFTBAUGH', if_true: files('softbaugh.c'))
msp430_ss.add(when: 'CONFIG_VIRT', if_true: files('virt.c'))

hw_arch += {'msp430': msp430_ss}
