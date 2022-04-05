from siren.core import MacroError
from siren.token import argument, new_token


def register(parser):
    parser.add_macro("symbol_join", symbol_join)


def symbol_join(args, name_token):
    if len(args) < 2:
        raise MacroError("symbol_join requires at least two arguments")
    return [new_token("_".join(argument(arg) for arg in args), name_token)], False
