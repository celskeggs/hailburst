import hashlib

from siren.core import MacroError
from siren.language import is_valid_variable_name
from siren.token import argument, new_token


def register(parser):
    mutable = [parser, 0]
    parser.add_macro("anonymous_symbol", lambda args, name_token: anonymous_symbol(args, name_token, mutable))


def anonymous_symbol(args, name_token, counter):
    if len(args) != 1:
        raise MacroError("anonymous_symbol takes exactly one argument")
    variable_name = argument(args[0])
    if not is_valid_variable_name(variable_name):
        raise MacroError("invalid variable name %r" % variable_name)

    uniq = counter[0].source_hash + str(counter[1]).encode()
    counter[1] += 1
    replacement = new_token("_anon_" + hashlib.sha256(uniq).hexdigest()[:8], args[0])
    return lambda body: ([(replacement if token.match(variable_name) else token) for token in body], True)
