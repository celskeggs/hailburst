import string

from siren.core import MacroError
from siren.language import decode_string
from siren.token import argument, python_token

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


def register(parser):
    parser.add_macro("debugf_core", debugf_core)


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
