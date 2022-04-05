import sys

from siren.macros import register_all
from siren.parser import Parser


def default_parser(use_raw_lines):
    parser = Parser(use_raw_lines)
    register_all(parser)
    return parser


def main():
    use_raw_lines = sys.argv[1:2] == ["--rawlines"]
    if len(sys.argv) != (3 + use_raw_lines):
        print("Usage: %s [--rawlines] <input> <output>" % sys.argv[0], file=sys.stderr)
        print("Add --rawlines to reference input file as source rather than original source.", file=sys.stderr)
        sys.exit(1)
    default_parser(use_raw_lines).translate(sys.argv[1 + use_raw_lines], sys.argv[2 + use_raw_lines])


if __name__ == '__main__':
    main()
