from siren.core import MacroError


def is_valid_variable_name(name):
    if not name: return False
    name = name.replace("_", "")
    return name.isalnum() and name[0:1].isalpha()


ESCAPE_LOOKUP = {'\\': '\\', '"': '"', 'n': '\n'}


def decode_string(argument):
    textual = ""
    in_string = False
    in_escape = False
    for c in argument:
        if not in_string:
            if c == '"':
                in_string = True
            elif c in " \t\n":
                # skip spaces
                pass
            else:
                raise MacroError("unexpected symbol %r in string argument %r" % (c, argument))
        elif in_escape:
            if c not in ESCAPE_LOOKUP:
                raise MacroError("unknown escape sequence '\\%c'" % c)
            textual += ESCAPE_LOOKUP[c]
            in_escape = False
        elif c == '"':
            in_string = False
        elif c == '\\':
            in_escape = True
        else:
            textual += c
    if in_string:
        raise MacroError("unterminated string in argument %r" % argument)
    return textual
