import os
import string
import sys


class MacroError(RuntimeError):
    pass


ARG_CHAR = "unsigned char"
ARG_SHORT = "unsigned short"
ARG_INT = "unsigned int"
ARG_LONG = "unsigned long"
ARG_LONG_LONG = "unsigned long long"
ARG_PTRDIFF_T = "ptrdiff_t"
ARG_INTMAX_T = "intmax_t"
ARG_SIZE_T = "size_t"
ARG_VOIDPTR = "void*"
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


def debugf_core(args, filename, line_num):
    if len(args) < 3:
        raise MacroError("debugf requires at least two arguments")
    loglevel, stable_id_raw, format_raw, args = args[0], args[1], args[2], args[3:]
    if loglevel.strip() not in ("CRITICAL", "WARNING", "INFO", "DEBUG", "TRACE"):
        raise MacroError("debugf requires a valid log level, not %r" % loglevel)
    stable_id = decode_string(stable_id_raw)
    if not stable_id:
        stable_id = None
    elif not stable_id.isalnum():
        raise MacroError("debugf stable id is invalid: %r" % stable_id)
    format = decode_string(format_raw)
    arg_types = parse_printf_format(format)
    if len(arg_types) != len(args):
        raise MacroError("debugf format string indicates %d arguments, but %d passed" % (len(arg_types), len(args)))
    fragments = [
        '({',
        'static __attribute__((section (".debugf_messages"))) const char _msg_format[] = (%s);' % format_raw,
        'static __attribute__((section (".debugf_messages"))) const char _msg_filename[] = "%s";'
            % filename.replace("\\", "\\\\").replace('"', '\\"'),
    ]
    if stable_id is not None:
        fragments += [
            'static __attribute__((section (".debugf_messages"))) const char _msg_stable[] = "%s";' % stable_id,
        ]
    fragments += [
        'static __attribute__((section (".debugf_messages"))) const struct debugf_metadata _msg_metadata = {',
        '.loglevel = (%s),' % loglevel,
    ]
    if stable_id is not None:
        fragments += [
            '.stable_id = _msg_stable,'
        ]
    else:
        fragments += [
            '.stable_id = (void *) 0,',
        ]
    fragments += [
        '.format = _msg_format,',
        '.filename = _msg_filename,',
        '.line_number = %u,' % line_num,
        '};',
        "struct {",
        "const struct debugf_metadata *metadata;",
        "uint64_t timestamp;",
    ]
    for i, arg_type in enumerate(arg_types):
        if arg_type != ARG_STRING:
            fragments.append("%s arg%d;" % (arg_type, i))
    fragments += [
        "} __attribute__((packed)) _msg_state = {",
        ".metadata = &_msg_metadata,",
        ".timestamp = timer_now_ns(),"
    ]
    for i, (arg_type, arg_expr) in enumerate(zip(arg_types, args)):
        if arg_type != ARG_STRING:
            fragments += [".arg%d = (%s)," % (i, arg_expr)]
    fragments += [
        "};",
    ]
    for i, (arg_type, arg_expr) in enumerate(zip(arg_types, args)):
        if arg_type == ARG_STRING:
            fragments += ["%s _msg_str%d = (%s);" % (arg_type, i, arg_expr)]
    fragments += [
        "const void *_msg_seqs[] = {",
        "&_msg_state,",
    ]
    last_string_arg = None
    for i, arg_type in enumerate(arg_types):
        if arg_type == ARG_STRING:
            fragments += ["_msg_str%d," % i]
            last_string_arg = i
    fragments += [
        "};",
        "size_t _msg_sizes[] = { sizeof(_msg_state),",
    ]
    num_seqs = 1
    for i, arg_type in enumerate(arg_types):
        if arg_type == ARG_STRING:
            # only need to include the final null terminator if we have more strings to encode
            fragments += ["strlen(_msg_str%d) + %d," % (i, 0 if i == last_string_arg else 1)]
            num_seqs += 1
    fragments += [
        "};",
        "debugf_internal(_msg_seqs, _msg_sizes, %d);" % num_seqs,
        '})',
    ]

    return " ".join(fragments)


def default_parser():
    parser = Parser()
    parser.add_macro("debugf_core", debugf_core)
    return parser


def tokenize(line):
    cur_string = None
    in_escape = False
    cur_token = None
    cur_spaces = None
    for c in line:
        if cur_string is not None:
            cur_string += c
            if in_escape:
                in_escape = False
            elif c == '\\':
                in_escape = True
            elif c == '"':
                yield cur_string
                cur_string = None
        elif c in " \t\n":
            if cur_token is not None:
                yield cur_token
                cur_token = None
            if cur_spaces is None:
                cur_spaces = c
            else:
                cur_spaces += c
        elif c in "(,.)":
            if cur_token is not None:
                yield cur_token
                cur_token = None
            if cur_spaces is not None:
                yield cur_spaces
                cur_spaces = None
            yield c
        elif c == '"':
            if cur_token is not None:
                yield cur_token
                cur_token = None
            if cur_spaces is not None:
                yield cur_spaces
                cur_spaces = None
            cur_string = c
        else:
            if cur_spaces is not None:
                yield cur_spaces
                cur_spaces = None
            if cur_token is None:
                cur_token = c
            else:
                cur_token += c
    if cur_string is not None:
        raise RuntimeError("string did not finish by end of line")
    if cur_token is not None:
        yield cur_token
    if cur_spaces is not None:
        yield cur_spaces


class MacroExpr:
    def __init__(self, macro_func, filename, line_num):
        self.macro_func = macro_func
        self.filename = filename
        self.line_num = line_num
        self.args = []

    def on_tokens(self, tokens):
        if not self.args:
            self.args.append([])
        for token in tokens:
            self.args[-1].append(token)

    def on_comma(self):
        self.args.append([])

    def execute(self):
        return tokenize(self.macro_func(["".join(tokens) for tokens in self.args], self.filename, self.line_num))


class ParenExpr:
    def __init__(self):
        self.tokens = ["("]

    def execute(self):
        self.tokens.append(")")
        return self.tokens

    def on_comma(self):
        self.tokens.append(",")

    def on_tokens(self, tokens):
        for token in tokens:
            self.tokens.append(token)


class Parser:
    def __init__(self):
        self.macros = {}
        self.pending_macro = None
        self.stack = []
        self.source_file = None
        self.source_line = 0

    def on_tokens(self, tokens):
        for token in tokens:
            if self.pending_macro is not None:
                if token == "(":
                    self.stack.append(MacroExpr(self.macros[self.pending_macro], self.source_file, self.source_line))
                    self.pending_macro = None
                    continue
                else:
                    yield self.pending_macro
                    self.pending_macro = None
            if token in self.macros:
                self.pending_macro = token
                continue
            if not self.stack:
                # not processing any macros... just write each token out directly
                yield token
                continue
            if token == ")":
                generated = self.stack.pop().execute()
                if self.stack:
                    self.stack[-1].on_tokens(generated)
                else:
                    for token in generated:
                        yield token
            elif token == "(":
                self.stack.append(ParenExpr())
            elif token == ",":
                self.stack[-1].on_comma()
            else:
                self.stack[-1].on_tokens([token])

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
                    for line in input:
                        try:
                            output.write(self.translate_line(line))
                        except MacroError as e:
                            print(e, file=sys.stderr)
                            print("---> %s" % line.rstrip('\n'), file=sys.stderr)
                            print("At %s:%d" % (self.source_file, self.source_line), file=sys.stderr)
                            sys.exit(1)
                ok = True
            finally:
                if not ok and os.path.exists(oname):
                    os.unlink(oname)

    def translate_line(self, line):
        if line.startswith('#'):
            parts = line.split(" ")
            if len(parts) >= 3 and parts[0] == '#' and parts[1].isdigit() and parts[2].startswith('"'):
                self.source_file = decode_string(parts[2])
                self.source_line = int(parts[1]) - 1
            return line
        self.source_line += 1
        return "".join(self.on_tokens(tokenize(line))).rstrip("\n") + "\n"


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: %s <input> <output>" % sys.argv[0], file=sys.stderr)
        sys.exit(1)
    default_parser().translate(sys.argv[1], sys.argv[2])
