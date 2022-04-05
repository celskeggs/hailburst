from siren.core import MacroError


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
