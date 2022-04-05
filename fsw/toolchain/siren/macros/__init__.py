from . import anonymous_symbol, debugf_core, macro_define, static_repeat, symbol_join, symbol_str

MODULES = [anonymous_symbol, debugf_core, macro_define, static_repeat, symbol_join, symbol_str]


def register_all(parser):
    for module in MODULES:
        module.register(parser)
