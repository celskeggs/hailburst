import hashlib
import os
import sys

from siren.core import MacroError
from siren.language import decode_string
from siren.macro import BraceExpr, MacroExpr, ParenExpr
from siren.tokenize import tokenize


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
