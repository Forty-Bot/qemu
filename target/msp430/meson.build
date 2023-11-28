gen = [
  decodetree.process('insn.decode', extra_args: [ '--insnwidth', '16' ])
]

msp430_ss = ss.source_set()
msp430_ss.add(gen)
msp430_ss.add(files(
  'cpu.c',
  'disas.c',
  'gdbstub.c',
  'helper.c',
  'translate.c',
))

msp430_system_ss = ss.source_set()
msp430_system_ss.add(files(
  'cio.c',
))

target_arch += {'msp430': msp430_ss}
target_system_arch += {'msp430': msp430_system_ss}
