from siren.core import MacroError
from siren.token import argument, new_token


def register(parser):
    parser.add_macro("symbol_str", symbol_str)


def symbol_str(args, name_token):
    if len(args) != 1:
        raise MacroError("symbol_str takes exactly one argument")
    symbol = argument(args[0])
    return [new_token('"%s"' % symbol.replace('\\', '\\\\').replace('"', '\\"'), name_token)], False
