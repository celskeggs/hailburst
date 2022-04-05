from siren.core import MacroError
from siren.language import is_valid_variable_name
from siren.token import argument, new_token, python_token


def register(parser):
    parser.add_macro("macro_define", lambda args, name_token: macro_define(args, name_token, parser, False))
    parser.add_macro("macro_block_define", lambda args, name_token: macro_define(args, name_token, parser, True))


def macro_define(args, name_token, parser, is_block):
    if is_block:
        if len(args) < 2:
            raise MacroError("macro_block_define must always have a macro name to define and a body variable")
    else:
        if len(args) < 1:
            raise MacroError("macro_define must always have a macro name to define")
    param_names = [argument(arg) for arg in args]
    vararg = None
    if not is_block and len(param_names[-1]) > 3 and param_names[-1][-3:] == "...":
        vararg = param_names.pop()[:-3]
        if not is_valid_variable_name(vararg):
            raise MacroError("invalid identifier %r" % vararg)
    for name in param_names:
        if not is_valid_variable_name(name):
            raise MacroError("invalid identifier %r" % name)
    macro_name = param_names.pop(0)
    body_name = param_names.pop() if is_block else None

    def accept_body(body):
        def substitute(lookup, call_site):
            substitution = []
            blame_caller_flag = -1
            for token in body:
                # if we're supposed to make sure the caller is blamed for any errors, rewrite the tokens
                if blame_caller_flag == -1 and token.token == "blame_caller":
                    blame_caller_flag = 0
                    continue
                elif blame_caller_flag == 0:
                    if token.token == "{":
                        blame_caller_flag = 1
                        # make sure not to let this closing { pass through
                        continue
                    elif token.is_whitespace():
                        # skip intervening whitespace too
                        continue
                    else:
                        raise MacroError("unexpected symbol %r when expecting { after blame_caller" % token)
                elif token.token == "}" and blame_caller_flag == 1:
                    blame_caller_flag = -1
                    # make sure not to let this opening } pass through
                    continue
                elif blame_caller_flag >= 1:
                    if token.token == "{":
                        blame_caller_flag += 1
                    elif token.token == "}":
                        blame_caller_flag -= 1
                    # rewrite token to refer to the call site itself
                    token = new_token(token.token, call_site)
                # eliminate any trailing comma that's going to cause a problem with the vararg substitution.
                if token.token == vararg and len(lookup[vararg]) == 0:
                    count = 1
                    while count < len(substitution) and substitution[len(substitution) - count].is_whitespace():
                        count += 1
                    if count <= len(substitution) and substitution[len(substitution) - count].token == ",":
                        substitution = substitution[:len(substitution) - count]
                substitution += lookup.get(token.token, [token])
            return substitution, True

        def defined_macro_callback(call_args, call_token):
            if len(call_args) < len(param_names) or (vararg is None and len(call_args) > len(param_names)):
                raise MacroError("user-defined macro %r requires %d arguments but found %d"
                                 % (macro_name, len(param_names), len(call_args)))

            lookup = {param: tokens for param, tokens in zip(param_names, call_args)}

            if vararg is not None:
                vararg_flat = []
                for i, callarg in enumerate(call_args[len(param_names):]):
                    if i > 0:
                        vararg_flat.append(python_token(","))
                    vararg_flat += callarg
                lookup[vararg] = vararg_flat

            def macro_accept_body(body):
                lookup[body_name] = body
                return substitute(lookup, call_token)

            return macro_accept_body if is_block else substitute(lookup, call_token)

        if not parser.try_add_macro(macro_name, defined_macro_callback):
            raise MacroError("macro already defined: %r" % macro_name)

        return [], False

    return accept_body
