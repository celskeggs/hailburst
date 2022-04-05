import inspect


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
