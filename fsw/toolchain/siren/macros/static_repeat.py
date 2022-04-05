from siren.core import MacroError
from siren.token import argument, new_token


def register(parser):
    parser.add_macro("static_repeat", static_repeat)


def static_repeat(args, name_token):
    if len(args) != 2:
        raise MacroError("static_repeat requires exactly two arguments")
    repeat_count_tokens = args[0]
    repeat_count, variable_name = argument(repeat_count_tokens), argument(args[1])
    if not repeat_count.isdigit():
        raise MacroError("invalid repeat count %r" % repeat_count)
    if not (variable_name.replace("_", "").isalnum() and variable_name[0].isalpha()):
        raise MacroError("invalid variable name %r" % variable_name)

    repeat_count = int(repeat_count)

    def handle_body(body):
        tokens = []
        for r in range(repeat_count):
            count_token = new_token(str(r), repeat_count_tokens)
            tokens += [(count_token if token.match(variable_name) else token) for token in body]
        return tokens, True

    return handle_body
