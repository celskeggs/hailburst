from siren.token import Token


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
