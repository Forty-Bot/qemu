#!/usr/bin/env python3

from argparse import ArgumentParser
from pathlib import Path
import os
import sys

from jinja2 import Environment, FileSystemLoader, StrictUndefined
import jinja2.ext

def BIT(n):
    return 1 << n

class test_id:
    def __init__(self):
        self.test_count = 1

    def __call__(self):
        id = self.test_count
        self.test_count += 1
        return id

def ext0(value):
    return value & 0xff

def ext1(value):
    return value | -256

def selectkeys(value, args):
    return { k: v for k, v in value.items() if k in args }

class TracingFileSystemLoader(FileSystemLoader):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.templates = set()

    def get_source(self, environment, template):
        self.templates.add(template)
        return super().get_source(environment, template)

if __name__ == '__main__':
    parser = ArgumentParser(description="Preprocess assembly files with jinja")
    parser.add_argument("-M", action='store_true',
                        help="generate make dependencies")
    parser.add_argument("-MD", action='store_true',
                        help="generate make dependencies and preprocess")
    parser.add_argument("-MF", metavar='filename', type=Path,
                        help="write dependency output to the given file")
    parser.add_argument("-MT", metavar='target', type=Path,
                        help="specify a dependency target")
    parser.add_argument("-o", default='-', metavar="outfile", dest='outfile',
                        type=Path, help="preprocessed output")
    parser.add_argument("infile", default='-', type=Path,
                        help="input to preprocess")

    args = parser.parse_args()
    if args.MD:
        args.M = True

    if args.MF is None:
        if args.MD:
            if args.outfile == Path('-'):
                args.MF = Path(args.infile.name)
            else:
                args.MF = args.outfile
            args.MF = args.MF.with_suffix('.d')
        else:
            args.MF = args.outfile

    if args.MT is None:
        args.MT = Path(args.infile.name).with_suffix('.S')

    env = Environment(
            auto_reload=False,
            extensions=(jinja2.ext.do,),
            keep_trailing_newline=True,
            loader=TracingFileSystemLoader(args.infile.parent),
            trim_blocks=True,
            undefined=StrictUndefined,
    )
    env.filters['ext0'] = ext0
    env.filters['ext1'] = ext1
    env.filters['hex'] = hex
    env.filters['selectkeys'] = selectkeys
    env.globals['V'] = BIT(8)
    env.globals['N'] = BIT(2)
    env.globals['Z'] = BIT(1)
    env.globals['C'] = BIT(0)
    env.globals['test_id'] = test_id()

    if args.infile == Path('-'):
        template = env.from_string(sys.stdin.read())
    else:
        template = env.get_template(args.infile.name)

    stream = template.stream(base1=0x1234, base2=0x5678)
    if args.M and args.outfile == args.MF:
        stream.dump(os.devnull)
    elif args.outfile == Path('-'):
        stream.dump(sys.stdout)
    else:
        stream.dump(str(args.outfile))

    if args.M:
        with sys.stdout if args.MF == Path('-') else args.MF.open('w') as f:
            f.write(f"{args.MT}: {' '.join(env.loader.templates)}\n")
