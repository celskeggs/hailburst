import hashlib
import inspect
import os
import string
import sys


class MacroError(RuntimeError):
    pass


class Token:
    def __init__(self, token, filename, line, column):
        assert type(token) == str and type(filename) == str and type(line) == int and type(column) == int
        assert line >= 1 and column >= 1
        self.token = token
        self.filename = filename
        self.line = line
        self.column = column  # starting at 1 when the first character of the token is at the start of the line

    def is_whitespace(self):
        return self.token.isspace()

    def match(self, *options):
        return self.token in options

    def ending_position(self):
        # returns (line, column) of the very first character of the next token
        if "\n" in self.token:
            return self.line + self.token.count("\n"), len(self.token) - self.token.rindex('\n')
        else:
            return self.line, self.column + len(self.token)

    def transition(self, last_token):
        if last_token is not None and self.filename == last_token.filename:
            last_line, last_column = last_token.ending_position()
            if self.line == last_line and self.column >= last_column:
                return " " * (self.column - last_column)
            elif last_line < self.line <= last_line + 10:
                return "\n" * (self.line - last_line) + " " * (self.column - 1)
        assert '"\n\\' not in self.filename, "odd filename not handled"
        newline = ('\n' if last_token is not None and not last_token.token.endswith('\n') else '')
        return "%s# %d \"%s\"\n%s" % (newline, self.line, self.filename, " " * (self.column - 1))

    def __repr__(self):
        return "Token(%r, %r, %r, %r)" % (self.token, self.filename, self.line, self.column)


def new_token(text, reference):
    if type(reference) == list:
        refitem = reference[0]
        for token in reference:
            if token.token.strip():
                refitem = token
                break
        reference = refitem
    return Token(text, reference.filename, reference.line, reference.column)


def python_token(text):
    # references the source code of this file directly
    fi = inspect.getframeinfo(inspect.stack()[1][0])
    # TODO: figure out how to fill in column
    return Token(text, fi.filename, fi.lineno, 1)


def argument(tokens):
    return "".join(token.token for token in tokens).strip()


ARG_CHAR = "unsigned char"
ARG_SHORT = "unsigned short"
ARG_INT = "unsigned int"
ARG_LONG = "unsigned long"
ARG_LONG_LONG = "unsigned long long"
ARG_PTRDIFF_T = "ptrdiff_t"
ARG_INTMAX_T = "intmax_t"
ARG_SIZE_T = "size_t"
ARG_VOIDPTR = "const void *"
ARG_DOUBLE = "double"
ARG_STRING = "const char *"


def parse_printf_format(format):
    # based on embedded-artistry printf format
    chars = list(format)

    def accept(x):
        if not chars:
            raise MacroError("format string ended early during specifier (string=%r)" % format)
        if chars[0] in x:
            chars.pop(0)
            return True
        else:
            return False

    args = []
    while chars:
        if chars.pop(0) != '%':
            continue
        # see if this is just a '%%' escape
        if accept('%'):
            continue
        # skip flags
        while accept("0-+ #"):
            pass
        # check for dynamic size
        if accept("*"):
            args.append(ARG_INT)
        else:
            while accept(string.digits):
                pass
        # check for dynamic precision
        if accept("."):
            if accept("*"):
                args.append(ARG_INT)
            else:
                while accept(string.digits):
                    pass
        # parse length field
        if accept('l'):
            length = ARG_LONG_LONG if accept('l') else ARG_LONG
        elif accept('h'):
            length = ARG_CHAR if accept('h') else ARG_SHORT
        elif accept('t'):
            length = ARG_PTRDIFF_T
        elif accept('j'):
            length = ARG_INTMAX_T
        elif accept('z'):
            length = ARG_SIZE_T
        else:
            length = ARG_INT
        # parse specifier
        if accept("diuxXob"):
            # the details don't really matter... we just write these all out as integers
            args.append(length)
        elif accept("fFeEgG"):
            args.append(ARG_DOUBLE)
        elif accept('c'):
            args.append(ARG_CHAR)
        elif accept('s'):
            args.append(ARG_STRING)
        elif accept('p'):
            args.append(ARG_VOIDPTR)
        else:
            raise MacroError("unexpected specifier %r in string %r" % (chars[0], format))

    return args


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


def validate_stable_id(stable_id):
    stable_id = stable_id.strip()
    if not stable_id:
        return None
    if not stable_id.isupper() or not stable_id.replace("_", "").isalnum():
        raise MacroError("invalid stable id: %r" % stable_id)
    return stable_id


def debugf_core(args, name_token):
    if len(args) < 3:
        raise MacroError("debugf requires at least two arguments")
    loglevel_tokens, stable_id_tokens, format_raw_tokens, args = args[0], args[1], args[2], args[3:]
    if argument(loglevel_tokens) not in ("CRITICAL", "WARNING", "INFO", "DEBUG", "TRACE"):
        raise MacroError("debugf requires a valid log level, not %r" % argument(loglevel_tokens))
    stable_id = decode_string(argument(stable_id_tokens))
    if not stable_id:
        stable_id = None
    elif not stable_id.isalnum():
        raise MacroError("debugf stable id is invalid: %r" % stable_id)
    format = decode_string(argument(format_raw_tokens))
    arg_types = parse_printf_format(format)
    if len(arg_types) != len(args):
        raise MacroError("debugf format string indicates %d arguments, but %d passed" % (len(arg_types), len(args)))
    tokens = [
        python_token('({'),
        python_token('static __attribute__((section ("debugf_messages"))) const char _msg_format[] = ('),
    ]
    tokens += format_raw_tokens
    tokens += [
        python_token(');'),
        python_token('static __attribute__((section ("debugf_messages"))) const char _msg_filename[] = "%s";'
                     % name_token.filename.replace("\\", "\\\\").replace('"', '\\"')),
    ]
    if stable_id is not None:
        tokens += [
            python_token('static __attribute__((section ("debugf_messages"))) const char _msg_stable[] = '),
        ]
        tokens += stable_id_tokens
        tokens += [
            python_token(';'),
        ]
    tokens += [
        python_token('static __attribute__((section ("debugf_messages"))) const struct debugf_metadata '),
        python_token('_msg_metadata = {'),
        python_token('.loglevel = ('),
    ]
    tokens += loglevel_tokens
    tokens += [
        python_token('),')
    ]
    if stable_id is not None:
        tokens += [
            python_token('.stable_id = _msg_stable,')
        ]
    else:
        tokens += [
            python_token('.stable_id = (void *) 0,'),
        ]
    tokens += [
        python_token('.format = _msg_format,'),
        python_token('.filename = _msg_filename,'),
        python_token('.line_number = %u,' % name_token.line),
        python_token('};'),
        python_token("struct {"),
        python_token("const struct debugf_metadata *metadata;"),
        python_token("uint64_t timestamp;"),
    ]
    for i, arg_type in enumerate(arg_types):
        if arg_type != ARG_STRING:
            tokens += [
                python_token("%s arg%d;" % (arg_type, i)),
            ]
    tokens += [
        python_token("} __attribute__((packed)) _msg_state = {"),
        python_token(".metadata = &_msg_metadata,"),
        python_token(".timestamp = clock_timestamp_fast(),"),
    ]
    for i, (arg_type, arg_expr) in enumerate(zip(arg_types, args)):
        if arg_type != ARG_STRING:
            tokens += [
                python_token(".arg%d = (" % i),
            ]
            tokens += arg_expr
            tokens += [
                python_token("),"),
            ]
    tokens += [
        python_token("};"),
    ]
    for i, (arg_type, arg_expr) in enumerate(zip(arg_types, args)):
        if arg_type == ARG_STRING:
            tokens += [
                python_token("%s _msg_str%d = (" % (arg_type, i)),
            ]
            tokens += arg_expr
            tokens += [
                python_token(");"),
            ]
    tokens += [
        python_token("const void *_msg_seqs[] = {"),
        python_token("&_msg_state,"),
    ]
    last_string_arg = None
    for i, arg_type in enumerate(arg_types):
        if arg_type == ARG_STRING:
            tokens += [
                python_token("_msg_str%d," % i),
            ]
            last_string_arg = i
    tokens += [
        python_token("};"),
        python_token("size_t _msg_sizes[] = { sizeof(_msg_state),"),
    ]
    num_seqs = 1
    for i, arg_type in enumerate(arg_types):
        if arg_type == ARG_STRING:
            # only need to include the final null terminator if we have more strings to encode
            tokens += [
                python_token("strlen(_msg_str%d) + %d," % (i, 0 if i == last_string_arg else 1)),
            ]
            num_seqs += 1
    tokens += [
        python_token("};"),
        python_token("debugf_internal(_msg_seqs, _msg_sizes, %d);" % num_seqs),
        python_token('})'),
    ]

    return tokens, False


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


def symbol_join(args, name_token):
    if len(args) < 2:
        raise MacroError("symbol_join requires at least two arguments")
    return [new_token("_".join(argument(arg) for arg in args), name_token)], False


def symbol_str(args, name_token):
    if len(args) != 1:
        raise MacroError("symbol_str takes exactly one argument")
    symbol = argument(args[0])
    return [new_token('"%s"' % symbol.replace('\\', '\\\\').replace('"', '\\"'), name_token)], False


def is_valid_variable_name(name):
    if not name: return False
    name = name.replace("_", "")
    return name.isalnum() and name[0:1].isalpha()


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


def default_parser(rawlines):
    parser = Parser(rawlines)
    parser.add_macro("debugf_core", debugf_core)
    parser.add_macro("static_repeat", static_repeat)
    parser.add_macro("symbol_join", symbol_join)
    parser.add_macro("symbol_str", symbol_str)
    parser.add_macro("macro_define", lambda args, name_token: macro_define(args, name_token, parser, False))
    parser.add_macro("macro_block_define", lambda args, name_token: macro_define(args, name_token, parser, True))
    mutable = [parser, 0]
    parser.add_macro("anonymous_symbol", lambda args, name_token: anonymous_symbol(args, name_token, mutable))
    return parser


def tokenize(line, filename, line_number):
    start_column = None
    in_escape = False
    cur_string = None
    cur_token = None
    cur_spaces = None
    for column, c in enumerate(line, 1):
        if cur_string is not None:
            cur_string += c
            if in_escape:
                in_escape = False
            elif c == '\\':
                in_escape = True
            elif c == '"':
                yield Token(cur_string, filename, line_number, start_column)
                cur_string = None
        elif c in " \t\n":
            if cur_token is not None:
                yield Token(cur_token, filename, line_number, start_column)
                cur_token = None
            if cur_spaces is None:
                cur_spaces = c
                start_column = column
            else:
                cur_spaces += c
        elif c in "<[{(,.;&*)}]>":
            if cur_token is not None:
                yield Token(cur_token, filename, line_number, start_column)
                cur_token = None
            if cur_spaces is not None:
                yield Token(cur_spaces, filename, line_number, start_column)
                cur_spaces = None
            yield Token(c, filename, line_number, column)
        elif c == '"':
            if cur_token is not None:
                yield Token(cur_token, filename, line_number, start_column)
                cur_token = None
            if cur_spaces is not None:
                yield Token(cur_spaces, filename, line_number, start_column)
                cur_spaces = None
            cur_string = c
            start_column = column
        else:
            if cur_spaces is not None:
                yield Token(cur_spaces, filename, line_number, start_column)
                cur_spaces = None
            if cur_token is None:
                cur_token = c
                start_column = column
            else:
                cur_token += c
    if cur_string is not None:
        raise RuntimeError("string did not finish by end of line")
    if cur_token is not None:
        yield Token(cur_token, filename, line_number, start_column)
    if cur_spaces is not None:
        yield Token(cur_spaces, filename, line_number, start_column)


class MacroExpr:
    def __init__(self, macro_func, name_token):
        self.macro_func = macro_func
        self.name_token = name_token
        self.args = []

    def allow_macro(self, macro):
        return True

    def on_tokens(self, tokens):
        if not self.args:
            self.args.append([])
        self.args[-1] += tokens

    def on_open_brace(self, token):
        return False

    def on_comma(self, token):
        assert token.match(",")
        self.args.append([])

    def execute(self, token):
        if not token.match(")"):
            raise MacroError("Macro %r expected ')' but got %r" % (self.macro_func, token))
        result = self.macro_func(self.args, self.name_token)
        if callable(result):
            # indicates brace continuation
            return MacroBodyExpr(result), False
        else:
            output, reinterpret = result
            assert type(output) == list and type(reinterpret) == bool, "invalid output from macro function"
            return output, reinterpret

    def __str__(self):
        return "%r: %r" % (self.name_token, self.args)


class MacroBodyExpr:
    def __init__(self, macro_func):
        self.macro_func = macro_func
        self.has_open = False
        self.body = []

    def allow_macro(self, macro):
        return not self.has_open

    def on_tokens(self, tokens):
        if not self.has_open and not all(token.is_whitespace() for token in tokens):
            raise MacroError("Macro expected '{' but got %r" % tokens)
        self.body += tokens

    def on_open_brace(self, token):
        if self.has_open:
            return False
        assert token.match("{")
        self.has_open = True
        return True

    def on_comma(self, token):
        assert token.match(",")
        self.body.append(token)

    def execute(self, token):
        if not token.match("}"):
            raise MacroError("Expected '}' but got %r" % token)
        output, reinterpret = self.macro_func(self.body)
        assert type(output) == list and type(reinterpret) == bool
        return output, reinterpret


class ParenExpr:
    def __init__(self, token):
        assert token.match("(")
        self.tokens = [token]

    def allow_macro(self, macro):
        return True

    def execute(self, token):
        if not token.match(")"):
            raise MacroError("Expected ')' but got %r" % token)
        self.tokens.append(token)
        return self.tokens, False

    def on_open_brace(self, token):
        return False

    def on_comma(self, token):
        assert token.match(",")
        self.tokens.append(token)

    def on_tokens(self, tokens):
        self.tokens += tokens


class BraceExpr:
    def __init__(self, token):
        assert token.match("{")
        self.tokens = [token]

    def allow_macro(self, macro):
        return True

    def execute(self, token):
        if not token.match("}"):
            raise MacroError("Expected '}' but got %r" % token)
        self.tokens.append(token)
        return self.tokens, False

    def on_open_brace(self, token):
        return False

    def on_comma(self, token):
        assert token.match(",")
        self.tokens.append(token)

    def on_tokens(self, tokens):
        self.tokens += tokens

    def __str__(self):
        return "BraceExpression(%r)" % self.tokens


class Parser:
    def __init__(self, rawlines):
        self.macros = {}
        self.pending_macro = None
        self.stack = []
        self.rawlines = rawlines
        self.source_file = None
        self.source_line = 0
        self.source_hash = None
        self.last_token = None

    def on_token(self, token):
        if self.pending_macro is not None:
            if token.match("("):
                self.stack.append(MacroExpr(self.macros[self.pending_macro.token], self.pending_macro))
                self.pending_macro = None
                return
            else:
                yield self.pending_macro
                self.pending_macro = None
        if token.token in self.macros and all(entry.allow_macro(token.token) for entry in self.stack):
            self.pending_macro = token
            return
        if not self.stack:
            # not processing any macros... just write each token out directly
            yield token
            return
        if token.match("}", ")"):
            generated, reinterpret = self.stack.pop().execute(token)
            if hasattr(generated, "execute"):
                self.stack.append(generated)
            elif reinterpret:
                for generated_token in generated:
                    for output_token in self.on_token(generated_token):
                        yield output_token
            elif self.stack:
                self.stack[-1].on_tokens(generated)
            else:
                for token in generated:
                    yield token
        elif token.match("("):
            self.stack.append(ParenExpr(token))
        elif token.match("{"):
            if not self.stack[-1].on_open_brace(token):
                self.stack.append(BraceExpr(token))
        elif token.match(","):
            self.stack[-1].on_comma(token)
        else:
            self.stack[-1].on_tokens([token])

    def on_tokens(self, tokens):
        last = None
        for token in tokens:
            assert last is None or token.transition(last) == ""
            last = token
            for output_token in self.on_token(token):
                yield output_token

    def try_add_macro(self, name, func):
        if name in self.macros:
            return False
        self.add_macro(name, func)
        return True

    def add_macro(self, name, func):
        assert name not in self.macros
        self.macros[name] = func

    def translate(self, iname, oname):
        with open(iname, "r") as input:
            ok = False
            try:
                with open(oname, "w") as output:
                    self.source_file = iname
                    self.source_line = 0
                    self.source_hash = hashlib.sha256(iname.encode()).digest()
                    for line in input:
                        try:
                            output.write(self.translate_line(line))
                        except MacroError as e:
                            print(e, file=sys.stderr)
                            print("---> %s" % line.rstrip('\n'), file=sys.stderr)
                            print("At %s:%d" % (self.source_file, self.source_line), file=sys.stderr)
                            sys.exit(1)
                    if self.stack:
                        print("Cannot finish preprocessing: %d unterminated macros" % len(self.stack), file=sys.stderr)
                        for macro in self.stack:
                            print(" ", macro, file=sys.stderr)
                        sys.exit(1)
                ok = True
            finally:
                if not ok and os.path.exists(oname):
                    os.unlink(oname)

    def translate_line(self, line):
        if line.startswith('#'):
            if self.rawlines:
                self.source_line += 1
                return ""
            parts = line.split(" ")
            if len(parts) >= 3 and parts[0] == '#' and parts[1].isdigit() and parts[2].startswith('"'):
                self.source_file = decode_string(parts[2])
                self.source_line = int(parts[1]) - 1
            return ""
        self.source_line += 1
        if not line.strip():
            # line will be reintroduced later if required
            return ""
        fragments = []
        tokens = tokenize(line, self.source_file, self.source_line)
        # process and generate output
        for token in self.on_tokens(tokens):
            fragments.append(token.transition(self.last_token))
            fragments.append(token.token)
            self.last_token = token
        return "".join(fragments)


if __name__ == '__main__':
    is_rawlines = sys.argv[1:2] == ["--rawlines"]
    if len(sys.argv) != (3 + is_rawlines):
        print("Usage: %s [--rawlines] <input> <output>" % sys.argv[0], file=sys.stderr)
        print("Add --rawlines to reference input file as source rather than original source.", file=sys.stderr)
        sys.exit(1)
    default_parser(is_rawlines).translate(sys.argv[1 + is_rawlines], sys.argv[2 + is_rawlines])
